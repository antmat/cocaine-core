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

#include "cocaine/detail/engine.hpp"

#include "cocaine/context.hpp"
#include "cocaine/logging.hpp"

#include "cocaine/detail/chamber.hpp"

#include "cocaine/rpc/asio/channel.hpp"
#include "cocaine/rpc/session.hpp"

#include <blackhole/scoped_attributes.hpp>

using namespace asio;
using namespace asio::ip;

using namespace blackhole;

using namespace cocaine;

class execution_unit_t::gc_action_t:
    public std::enable_shared_from_this<gc_action_t>
{
    execution_unit_t *const parent;
    const boost::posix_time::seconds repeat;

public:
    template<class Interval>
    gc_action_t(execution_unit_t *const parent_, Interval repeat_):
        parent(parent_),
        repeat(repeat_)
    { }

    void
    operator()();

private:
    void
    finalize(const std::error_code& ec);
};

void
execution_unit_t::gc_action_t::operator()() {
    parent->m_cron.expires_from_now(repeat);

    parent->m_cron.async_wait(std::bind(&gc_action_t::finalize,
        shared_from_this(),
        std::placeholders::_1
    ));
}

void
execution_unit_t::gc_action_t::finalize(const std::error_code& ec) {
    if(ec == asio::error::operation_aborted) {
        return;
    }

    size_t recycled = 0;

    for(auto it = parent->m_sessions.begin(); it != parent->m_sessions.end();) {
        if(!it->second->memory_pressure()) {
            recycled++;
            it = parent->m_sessions.erase(it);
            continue;
        }

        ++it;
    }

    if(recycled) {
        COCAINE_LOG_DEBUG(parent->m_log, "recycled %d session(s)", recycled);
    }

    operator()();
}

execution_unit_t::execution_unit_t(context_t& context):
    m_asio(new io_service()),
    m_chamber(new io::chamber_t("core:asio", m_asio)),
    m_cron(*m_asio)
{
    m_log = context.log("core:asio", {
        attribute::make("engine", boost::lexical_cast<std::string>(m_chamber->thread_id()))
    });

    m_asio->post(std::bind(&gc_action_t::operator(),
        std::make_shared<gc_action_t>(this, boost::posix_time::seconds(kCollectionInterval))
    ));

    COCAINE_LOG_DEBUG(m_log, "engine started");
}

execution_unit_t::~execution_unit_t() {
    m_asio->post([this] {
        COCAINE_LOG_DEBUG(m_log, "stopping engine");

        for(auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
            // Close the connections.
            it->second->detach(std::error_code());
        }

        m_cron.cancel();
    });

    // NOTE: This will block until all the outstanding operations are complete.
    m_chamber = nullptr;
}

std::shared_ptr<session_t>
execution_unit_t::attach(const std::shared_ptr<tcp::socket>& ptr, const io::dispatch_ptr_t& dispatch) {
    int socket;

    if((socket = ::dup(ptr->native_handle())) == -1) {
        throw std::system_error(errno, std::system_category(), "unable to clone client's socket");
    }

    std::shared_ptr<session_t> session;

    try {
        // Local endpoint address of the socket to be cloned.
        const auto endpoint = ptr->local_endpoint();

        // Copy the socket into the new reactor.
        auto channel = std::make_unique<io::channel<tcp>>(std::make_unique<tcp::socket>(
           *m_asio,
            endpoint.protocol(),
            socket
        ));

        // Disable Nagle's algorithm, since most of the service clients do not send or receive more
        // than a couple of kilobytes of data.
        channel->socket->set_option(tcp::no_delay(true));

        auto session_log = std::make_unique<logging::log_t>(*m_log, attribute::set_t({
            attribute::make("endpoint", boost::lexical_cast<std::string>(ptr->remote_endpoint())),
            attribute::make("service",  dispatch ? dispatch->name() : "<none>"),
        }));

        COCAINE_LOG_DEBUG(session_log, "attached connection to engine, load: %.2f%%", utilization() * 100);

        // Create a new inactive session.
        session = std::make_shared<session_t>(std::move(session_log), std::move(channel), dispatch);
    } catch(const std::system_error& e) {
        throw std::system_error(e.code(), "client has disappeared while creating session");
    }

    m_asio->dispatch([=]() mutable {
        (m_sessions[socket] = std::move(session))->pull();
    });

    return session;
}

double
execution_unit_t::utilization() const {
    return m_chamber->load_avg1();
}
