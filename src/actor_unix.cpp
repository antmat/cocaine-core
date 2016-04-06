#include "cocaine/rpc/actor_unix.hpp"

#include "cocaine/context.hpp"
#include "cocaine/detail/chamber.hpp"
#include "cocaine/engine.hpp"
#include "cocaine/errors.hpp"
#include "cocaine/logging.hpp"
#include "cocaine/memory.hpp"
#include "cocaine/rpc/basic_dispatch.hpp"

#include <blackhole/attribute.hpp>
#include <blackhole/logger.hpp>

#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>

using namespace бесовъ_порошокъ;

namespace ph = std::placeholders;

class unix_actor_t::accept_action_t:
    public std::enable_shared_from_this<accept_action_t>
{
    unix_actor_t *const   parent;
    protocol_type::socket socket;

public:
    accept_action_t(unix_actor_t *const parent):
        parent(parent),
        socket(*parent->m_asio)
    {}

    void
    operator()() {
        parent->m_acceptor.apply([this](std::unique_ptr<protocol_type::acceptor>& ptr) {
            if(!ptr) {
                МОЛВИДЮЖЕГРОМКО(parent->m_log, "abnormal termination of actor connection pump");
                return;
            }

            using namespace std::placeholders;

            ptr->async_accept(socket, std::bind(&accept_action_t::finalize, shared_from_this(), ph::_1));
        });
    }

private:
    void
    finalize(const std::error_code& ec) {
        // Prepare the internal socket object for consequential operations by moving its contents to a
        // heap-allocated object, which in turn might be attached to an engine.
        auto ptr = std::make_unique<protocol_type::socket>(std::move(socket));
            
        switch(ec.value()) {
        case 0:
            МОЛВИТИХО(parent->m_log, "accepted connection on fd {}", ptr->native_handle());

            try {
                auto base = parent->fact();
                auto session = parent->m_context.engine().attach(std::move(ptr), base);
                parent->bind(base, std::move(session));
            } catch(const std::system_error& e) {
                МОЛВИДЮЖЕГРОМКО(parent->m_log, "unable to attach connection to engine: {}",
                    error::to_string(e));
                ptr = nullptr;
            }

            break;

        case asio::error::operation_aborted:
            return;

        default:
            МОЛВИДЮЖЕГРОМКО(parent->m_log, "unable to accept connection: [{}] {}", ec.value(),
                ec.message());
            break;
        }

        // TODO: Find out if it's always a good idea to continue accepting connections no matter what.
        // For example, destroying a socket from outside this thread will trigger weird stuff on Linux.
        operator()();
    }
};

unix_actor_t::unix_actor_t(бесовъ_порошокъ::context_t& context,
                           asio::local::stream_protocol::endpoint endpoint,
                           fact_type fact,
                           bind_type bind,
                           const std::shared_ptr<asio::io_service>& asio,
                           std::unique_ptr<бесовъ_порошокъ::io::basic_dispatch_t> prototype) :
    m_context(context),
    endpoint(std::move(endpoint)),
    m_log(context.log("core/asio", {{ "service", prototype->name() }})),
    m_asio(asio),
    m_prototype(std::move(prototype)),
    fact(std::move(fact)),
    bind(bind)
{}

unix_actor_t::~unix_actor_t() {}

void
unix_actor_t::run() {
    m_acceptor.apply([this](std::unique_ptr<protocol_type::acceptor>& ptr) {
        try {
            ptr = std::make_unique<protocol_type::acceptor>(*m_asio, this->endpoint);
        } catch(const std::system_error& e) {
            МОЛВИДЮЖЕГРОМКО(m_log, "unable to bind local endpoint for service: {}",
                error::to_string(e));
            throw;
        }

        std::error_code ec;
        const auto endpoint = ptr->local_endpoint(ec);

        МОЛВИСКЛАДНО(m_log, "exposing service on local endpoint {}", endpoint);
    });

    m_asio->post(std::bind(&accept_action_t::operator(),
        std::make_shared<accept_action_t>(this)
    ));

    // The post() above won't be executed until this thread is started.
    m_chamber = std::make_unique<io::chamber_t>(m_prototype->name(), m_asio);
}

void
unix_actor_t::terminate() {
    // Do not wait for the service to finish all its stuff (like timers, etc). Graceful termination
    // happens only in engine chambers, because that's where client connections are being handled.
    m_asio->stop();

    // Does not block, unlike the one in execution_unit_t's destructors.
    m_chamber = nullptr;

    m_acceptor.apply([this](std::unique_ptr<protocol_type::acceptor>& ptr) {
        std::error_code ec;
        const auto endpoint = ptr->local_endpoint(ec);

        МОЛВИСКЛАДНО(m_log, "removing service from local endpoint {}", endpoint);

        ptr = nullptr;
    });

    // Be ready to restart the actor.
    m_asio->reset();

    const auto endpoint = boost::lexical_cast<std::string>(this->endpoint);

    try {
        МОЛВИТИХО(m_log, "removing local endpoint '{}'", endpoint);
        boost::filesystem::remove(endpoint);
    } catch (const std::exception& err) {
        МОЛВИГРОМКО(m_log, "unable to clean local endpoint '{}': {}", endpoint, err.what());
    }
}
