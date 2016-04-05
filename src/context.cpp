/*
    Copyright (c) 2011-2014 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2014 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "cocaine/context.hpp"

#include "cocaine/api/service.hpp"
#include "cocaine/context/config.hpp"
#include "cocaine/context/filter.hpp"
#include "cocaine/context/mapper.hpp"
#include "cocaine/context/signal.hpp"
#include "cocaine/detail/essentials.hpp"
#include "cocaine/engine.hpp"
#include "cocaine/format.hpp"
#include "cocaine/idl/context.hpp"
#include "cocaine/logging.hpp"
#include "cocaine/rpc/actor.hpp"
#include "cocaine/repository/service.hpp"

#include <boost/optional/optional.hpp>

#include <blackhole/logger.hpp>
#include <blackhole/scope/holder.hpp>
#include <blackhole/wrapper.hpp>

#include <deque>
#include <boost/algorithm/string/join.hpp>
#include <cocaine/detail/trace/logger.hpp>

namespace cocaine {

namespace {

struct match {
    template<class T>
    bool
    operator()(const T& service) const {
        return name == service.first;
    }

    const std::string& name;
};

}

using namespace cocaine::io;

using blackhole::scope::holder_t;

class context_impl_t : public context_t {
public:
    typedef std::pair<std::string, std::unique_ptr<actor_t>> service_desc_t;
    typedef std::deque<service_desc_t> service_list_t;

    context_impl_t(std::unique_ptr<config_t> _config, std::unique_ptr<logging::logger_t> _log) :
        m_log(new logging::trace_wrapper_t(std::move(_log))),
        m_config(std::move(_config)),
        m_mapper(*m_config)
    {
        const holder_t scoped(*m_log, {{"source", "core"}});

        auto config_severity = m_config->logging().severity();
        auto filter = [=](filter_t::severity_t severity, filter_t::attribute_pack&) -> bool {
            return severity >= config_severity || !trace_t::current().empty();
        };
        logger_filter(filter_t(std::move(filter)));

        COCAINE_LOG_INFO(m_log, "initializing the core");

        m_repository = std::make_unique<api::repository_t>(log("repository"));

        // Load the builtin plugins.
        essentials::initialize(*m_repository);

        // Load the rest of plugins.
        m_repository->load(m_config->path().plugins());

        // Spin up all the configured services, launch execution units.
        COCAINE_LOG_INFO(m_log, "starting {:d} execution unit(s)", m_config->network().pool());

        while (m_pool.size() != m_config->network().pool()) {
            m_pool.emplace_back(std::make_unique<execution_unit_t>(*this));
        }

        COCAINE_LOG_INFO(m_log, "starting {:d} service(s)", m_config->services().size());

        std::vector<std::string> errored;

        m_config->services().each([&](const std::string& name, const config_t::component_t& service) mutable {
            const holder_t scoped(*m_log, {{"service", name}});

            const auto asio = std::make_shared<asio::io_service>();

            COCAINE_LOG_DEBUG(m_log, "starting service");

            try {
                insert(name, std::make_unique<actor_t>(*this, asio, repository().get<api::service_t>(
                    service.type(),
                    *this,
                    *asio,
                    name,
                    service.args()
                )));
            } catch (const std::system_error& e) {
                COCAINE_LOG_ERROR(m_log, "unable to initialize service: {}", error::to_string(e));
                errored.push_back(name);
            } catch (const std::exception& e) {
                COCAINE_LOG_ERROR(m_log, "unable to initialize service: {}", e.what());
                errored.push_back(name);
            }
        });

        if (!errored.empty()) {
            COCAINE_LOG_ERROR(m_log, "emergency core shutdown");

            // Signal and stop all the services, shut down execution units.
            terminate();

            const auto errored_str = boost::algorithm::join(errored, ", ");

            throw cocaine::error_t("couldn't start core because of {} service(s): {}",
                                   errored.size(), errored_str
            );
        } else {
            m_signals.invoke<io::context::prepared>();
        }
    }

    ~context_impl_t() {
        const holder_t scoped(*m_log, {{"source", "core"}});

        // Signal and stop all the services, shut down execution units.
        terminate();
    }

    std::unique_ptr<logging::logger_t>
    log(const std::string& source) {
        return log(source, {});
    }

    std::unique_ptr<logging::logger_t>
    log(const std::string& source, blackhole::attributes_t attributes) {
        attributes.push_back({"source", {source}});

        // TODO: Make it possible to use in-place operator+= to fill in more attributes?
        return std::make_unique<blackhole::wrapper_t>(*m_log, std::move(attributes));
    }

    void
    logger_filter(filter_t new_filter) {
        m_log->filter(std::move(new_filter));
    }

    const api::repository_t&
    repository() const {
        return *m_repository;
    }

    retroactive_signal<io::context_tag>&
    signal_hub() {
        return m_signals;
    }

    const config_t&
    config() const {
        return *m_config;
    }

    port_mapping_t&
    mapper(){
        return m_mapper;
    }

    void
    insert(const std::string& name, std::unique_ptr<actor_t> service) {
        const holder_t scoped(*m_log, {{"source", "core"}});

        const actor_t& actor = *service;

        m_services.apply([&](service_list_t& list) {
            auto it = std::find_if(list.begin(), list.end(), match{name});
            if(it != list.end()) {
                throw cocaine::error_t("service '{}' already exists", name);
            }

            service->run();

            COCAINE_LOG_DEBUG(m_log, "service has been started", {
                { "service", { name }}
            });

            list.emplace_back(name, std::move(service));
        });

        // Fire off the signal to alert concerned subscribers about the service removal event.
        m_signals.invoke<context::service::exposed>(actor.prototype().name(), std::forward_as_tuple(
            actor.endpoints(),
            actor.prototype().version(),
            actor.prototype().root()
        ));
    }

    std::unique_ptr<actor_t>
    remove(const std::string& name) {
        const holder_t scoped(*m_log, {{"source", "core"}});

        std::unique_ptr<actor_t> service;

        m_services.apply([&](service_list_t& list) {
            auto it = std::find_if(list.begin(), list.end(), match{name});
            if(it != list.end()) {
                service = std::move(it->second);
                list.erase(it);
            } else {
                throw cocaine::error_t("service '{}' doesn't exist", name);
            }
        });

        service->terminate();

        COCAINE_LOG_DEBUG(m_log, "service has been stopped", {
            { "service", name }
        });

        // Service is already terminated, so there's no reason to try to get its endpoints.
        std::vector<asio::ip::tcp::endpoint> nothing;

        // Fire off the signal to alert concerned subscribers about the service termination event.
        m_signals.invoke<context::service::removed>(service->prototype().name(), std::forward_as_tuple(
            nothing,
            service->prototype().version(),
            service->prototype().root()
        ));

        return service;
    }

    boost::optional<const actor_t&>
    locate(const std::string& name) const {
        return m_services.apply([&](const service_list_t& list) -> boost::optional<const actor_t&> {
            auto it = std::find_if(list.begin(), list.end(), match{name});
            if (it == list.end()) {
                return boost::none;
            }

            return boost::optional<const actor_t&>(it->second->is_active(), *it->second);
        });
    }

    execution_unit_t&
    engine() {
        typedef std::unique_ptr<execution_unit_t> unit_t;
        auto comp = [](const unit_t& lhs, const unit_t& rhs) {
            return lhs->utilization() < rhs->utilization();
        };
        return **std::min_element(m_pool.begin(), m_pool.end(), comp);
    }

    void
    terminate() {
        COCAINE_LOG_INFO(m_log, "stopping {:d} service(s)", m_services->size());

        // Fire off to alert concerned subscribers about the shutdown. This signal happens before all
        // the outstanding connections are closed, so services have a chance to send their last wishes.
        m_signals.invoke<context::shutdown>();

        // Stop the service from accepting new clients or doing any processing. Pop them from the active
        // service list into this temporary storage, and then destroy them all at once. This is needed
        // because sessions in the execution units might still have references to the services, and their
        // lives have to be extended until those sessions are active.
        std::vector<std::unique_ptr<actor_t>> actors;

        m_config->services().each([&](const std::string& name, const config_t::component_t&){
            try {
                actors.push_back(remove(name));
            } catch (...) {
                // A service might be absent because it has failed to start during the bootstrap.
            }
        });

        // There should be no outstanding services left. All the extra services spawned by others, like
        // app invocation services from the node service, should be dead by now.
        BOOST_ASSERT(m_services->empty());

        COCAINE_LOG_INFO(m_log, "stopping {:d} execution unit(s)", m_pool.size());

        m_pool.clear();

        // Destroy the service objects.
        actors.clear();

        COCAINE_LOG_INFO(m_log, "core has been terminated");
    }

private:
    // TODO: There was an idea to use the Repository to enable pluggable sinks and whatever else for
    // for the Blackhole, when all the common stuff is extracted to a separate library.
    std::unique_ptr<logging::trace_wrapper_t> m_log;

    // NOTE: This is the first object in the component tree, all the other dynamic components, be it
    // storages or isolates, have to be declared after this one.
    std::unique_ptr<api::repository_t> m_repository;

    // A pool of execution units - threads responsible for doing all the service invocations.
    std::vector<std::unique_ptr<execution_unit_t>> m_pool;

    // Services are stored as a vector of pairs to preserve the initialization order. Synchronized,
    // because services are allowed to start and stop other services during their lifetime.
    synchronized<service_list_t> m_services;

    // Context signalling hub.
    retroactive_signal<io::context_tag> m_signals;

    std::unique_ptr<config_t> m_config;

    // Service port mapping and pinning.
    port_mapping_t m_mapper;

};

std::unique_ptr<context_t>
get_context(std::unique_ptr<config_t> config, std::unique_ptr<logging::logger_t> log) {
    return std::unique_ptr<context_t>(new context_impl_t(std::move(config), std::move(log)));
}


} //  namespace cocaine
