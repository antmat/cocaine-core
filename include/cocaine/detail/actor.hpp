/*
    Copyright (c) 2011-2013 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2013 Other contributors as noted in the AUTHORS file.

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

#ifndef COCAINE_ACTOR_HPP
#define COCAINE_ACTOR_HPP

#include "cocaine/common.hpp"

#include "cocaine/asio/tcp.hpp"

// TODO: Drop this.
#include "cocaine/idl/locator.hpp"
#include "cocaine/rpc/result_of.hpp"

#include <list>

#define BOOST_BIND_NO_PLACEHOLDERS
#include <boost/thread/thread.hpp>

namespace cocaine {

class actor_t {
    COCAINE_DECLARE_NONCOPYABLE(actor_t)

    class execution_unit_t;

    public:
        actor_t(context_t& context, std::shared_ptr<io::reactor_t> reactor, std::unique_ptr<io::dispatch_t>&& prototype);
        actor_t(context_t& context, std::shared_ptr<io::reactor_t> reactor, std::unique_ptr<api::service_t>&& service);

       ~actor_t();

        void
        run(std::vector<io::tcp::endpoint> endpoints);

        void
        terminate();

    public:
        auto
        location() const -> std::vector<io::tcp::endpoint>;

        typedef result_of<io::locator::resolve>::type metadata_t;

        metadata_t
        metadata() const;

    private:
        void
        on_connect(const std::shared_ptr<io::socket<io::tcp>>& socket);

    private:
        context_t& m_context;

        const std::unique_ptr<logging::log_t> m_log;
        const std::shared_ptr<io::reactor_t> m_reactor;

        // Initial dispatch. It's the protocol dispatch that will be initially assigned to all the
        // new sessions. In case of secure actors, this might as well be the protocol dispatch to
        // switch to after the authentication process completes successfully.
        std::shared_ptr<io::dispatch_t> m_prototype;

        // Actor I/O connectors. Actors have a separate thread to accept new connections. The same
        // thread is also shared with the dispatch whenever it needs an event loop to do something.
        std::list<io::connector<io::acceptor<io::tcp>>> m_connectors;
        std::unique_ptr<boost::thread> m_thread;
};

} // namespace cocaine

#endif
