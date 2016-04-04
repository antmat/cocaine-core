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

#include "cocaine/rpc/actor.hpp"

#include "cocaine/api/service.hpp"

#include "cocaine/context.hpp"
#include "cocaine/context/config.hpp"
#include "cocaine/context/mapper.hpp"
#include "cocaine/idl/context.hpp"
#include "cocaine/logging.hpp"

#include "cocaine/detail/chamber.hpp"
#include "cocaine/engine.hpp"

#include "cocaine/rpc/dispatch.hpp"

#include <blackhole/logger.hpp>

using namespace cocaine;
using namespace cocaine::io;

using namespace asio;
using ip::tcp;

// Actor internals

class actor_t::accept_action_t:
    public std::enable_shared_from_this<accept_action_t>
{
    actor_t *const parent;
    tcp::socket    socket;

public:
    accept_action_t(actor_t *const parent_):
        parent(parent_),
        socket(*parent->m_asio)
    { }

    void
    operator()();

private:
    void
    finalize(const std::error_code& ec);
};

void
actor_t::accept_action_t::operator()() {
    parent->m_acceptor.apply([this](std::unique_ptr<tcp::acceptor>& ptr) {
        if(!ptr) {
            COCAINE_LOG_ERROR(parent->m_log, "abnormal termination of actor connection pump");
            return;
        }

        ptr->async_accept(socket, std::bind(&accept_action_t::finalize, shared_from_this(),
            std::placeholders::_1));
    });
}

void
actor_t::accept_action_t::finalize(const std::error_code& ec) {
    // Prepare the internal socket object for consequential operations by moving its contents to a
    // heap-allocated object, which in turn might be attached to an engine.
    auto ptr = std::make_unique<tcp::socket>(std::move(socket));

    switch(ec.value()) {
    case 0:
        COCAINE_LOG_DEBUG(parent->m_log, "accepted connection on fd {:d}", ptr->native_handle());

        try {
            parent->m_context.engine().attach(std::move(ptr), parent->m_prototype);
        } catch(const std::system_error& e) {
            COCAINE_LOG_ERROR(parent->m_log, "unable to attach connection to engine: {}",
                error::to_string(e));
            ptr = nullptr;
        }

        break;

    case asio::error::operation_aborted:
        return;

    default:
        COCAINE_LOG_ERROR(parent->m_log, "unable to accept connection: [{:d}] {}", ec.value(),
            ec.message());
        break;
    }

    // TODO: Find out if it's always a good idea to continue accepting connections no matter what.
    // For example, destroying a socket from outside this thread will trigger weird stuff on Linux.
    operator()();
}

// Actor

actor_t::actor_t(context_t& context, const std::shared_ptr<io_service>& asio,
                 std::unique_ptr<basic_dispatch_t> prototype)
:
    m_context(context),
    m_log(context.log("core/asio", {{"service", prototype->name()}})),
    m_asio(asio),
    m_prototype(std::move(prototype))
{ }

actor_t::actor_t(context_t& context, const std::shared_ptr<io_service>& asio,
                 std::unique_ptr<api::service_t> service)
:
    m_context(context),
    m_log(context.log("core/asio", {{"service", service->prototype().name()}})),
    m_asio(asio)
{
    const basic_dispatch_t* prototype = &service->prototype();

    // Aliasing the pointer to the service to point to the dispatch (sub-)object.
    m_prototype = dispatch_ptr_t(
        std::shared_ptr<api::service_t>(std::move(service)),
        prototype
    );
}

actor_t::~actor_t() {
    // Empty.
}

std::vector<tcp::endpoint>
actor_t::endpoints() const {
    tcp::resolver::iterator begin;

    try {
        const auto local = m_acceptor.apply(
            [](const std::unique_ptr<tcp::acceptor>& ptr) -> tcp::endpoint
        {
            if(ptr) {
                return ptr->local_endpoint();
            } else {
                throw std::system_error(std::make_error_code(std::errc::not_connected));
            }
        });

        if(!local.address().is_unspecified()) {
            return std::vector<tcp::endpoint>({local});
        }

        const tcp::resolver::query::flags flags = tcp::resolver::query::address_configured
                                                | tcp::resolver::query::numeric_service;

        begin = tcp::resolver(*m_asio).resolve(tcp::resolver::query(
            m_context.config().network().hostname(), std::to_string(local.port()),
            flags
        ));
    } catch(const std::system_error& e) {
        COCAINE_LOG_ERROR(m_log, "unable to resolve local endpoints: {}", error::to_string(e));
        return std::vector<tcp::endpoint>();
    }

    // For unspecified bind addresses, actual address set has to be resolved first. In other words,
    // unspecified means every available and reachable address for the host.
    std::vector<tcp::endpoint> endpoints;

    std::transform(begin, tcp::resolver::iterator(), std::back_inserter(endpoints), std::bind(
       &tcp::resolver::iterator::value_type::endpoint,
        std::placeholders::_1
    ));

    return endpoints;
}

bool
actor_t::is_active() const {
    return static_cast<bool>(*m_acceptor.synchronize());
}

const basic_dispatch_t&
actor_t::prototype() const {
    return *m_prototype;
}

void
actor_t::run() {
    m_acceptor.apply([this](std::unique_ptr<tcp::acceptor>& ptr) {
        std::error_code ec;
        tcp::endpoint endpoint;

        try {
            auto addr = asio::ip::address::from_string(m_context.config().network().endpoint());
            endpoint = tcp::endpoint{addr, m_context.mapper().assign(m_prototype->name())};
        } catch(const std::system_error& e) {
            COCAINE_LOG_ERROR(m_log, "unable to assign a local endpoint to service: {}", error::to_string(e));
            throw;
        }

        try {
            ptr = std::make_unique<tcp::acceptor>(*m_asio, endpoint);
        } catch(const std::system_error& e) {
            COCAINE_LOG_ERROR(m_log, "unable to bind local endpoint {} for service: {}", endpoint, error::to_string(e));
            throw;
        }

        COCAINE_LOG_INFO(m_log, "exposing service on local endpoint {}", ptr->local_endpoint(ec));
    });

    m_asio->post(std::bind(&accept_action_t::operator(),
        std::make_shared<accept_action_t>(this)
    ));

    // The post() above won't be executed until this thread is started.
    m_chamber = std::make_unique<chamber_t>(m_prototype->name(), m_asio);
}

void
actor_t::terminate() {
    // Do not wait for the service to finish all its stuff (like timers, etc). Graceful termination
    // happens only in engine chambers, because that's where client connections are being handled.
    m_asio->stop();

    // Does not block, unlike the one in execution_unit_t's destructors.
    m_chamber = nullptr;

    m_acceptor.apply([this](std::unique_ptr<tcp::acceptor>& ptr) {
        std::error_code ec;
        const auto endpoint = ptr->local_endpoint(ec);

        COCAINE_LOG_INFO(m_log, "removing service from local endpoint {}", endpoint);

        ptr = nullptr;
    });

    // Be ready to restart the actor.
    m_asio->reset();

    // Mark this service's port as free.
    m_context.mapper().retain(m_prototype->name());
}
