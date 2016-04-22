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

#include "cocaine/detail/cluster/multicast.hpp"

#include "cocaine/context.hpp"
#include "cocaine/context/signal.hpp"
#include "cocaine/dynamic.hpp"
#include "cocaine/logging.hpp"

#include "cocaine/rpc/dispatch.hpp"
#include "cocaine/rpc/actor.hpp"

#include "cocaine/traits/endpoint.hpp"
#include "cocaine/traits/graph.hpp"
#include "cocaine/traits/tuple.hpp"
#include "cocaine/traits/vector.hpp"

#include <asio/io_service.hpp>
#include <asio/ip/multicast.hpp>

#include <blackhole/logger.hpp>

using namespace cocaine::io;
using namespace cocaine::cluster;

using namespace asio;
using namespace asio::ip;

using blackhole::attribute_list;

namespace cocaine {

namespace ph = std::placeholders;

template<>
struct dynamic_converter<address> {
    typedef address result_type;

    static
    result_type
    convert(const dynamic_t& source) {
        return address::from_string(source.as_string());
    }
};

template<>
struct dynamic_converter<multicast_cfg_t> {
    typedef multicast_cfg_t result_type;

    static
    result_type
    convert(const dynamic_t& source) {
        result_type result;

        try {
            result.endpoint = udp::endpoint(
                source.as_object().at("group").to<address>(),
                source.as_object().at("port", 10053u).as_uint()
            );
        } catch(std::out_of_range& e) {
            throw cocaine::error_t("no multicast group has been specified");
        }

        result.interval = boost::posix_time::seconds(
            source.as_object().at("interval", 5u).as_uint()
        );

        return result;
    }
};

} // namespace cocaine

struct
multicast_t::announce_t {
    // Maps node UUID to a list of node endpoints.
    typedef std::tuple<std::string, std::vector<tcp::endpoint>> tuple_type;

    std::array<char, 65536> buffer;
    udp::endpoint endpoint;
};

multicast_t::multicast_t(context_t& context, interface& locator, const std::string& name, const dynamic_t& args):
    category_type(context, locator, name, args),
    m_context(context),
    m_log(context.log(name)),
    m_locator(locator),
    m_cfg(args.to<multicast_cfg_t>()),
    m_socket(locator.asio()),
    m_timer(locator.asio())
{
    m_socket.open(m_cfg.endpoint.protocol());
    m_socket.set_option(socket_base::reuse_address(true));

    if(m_cfg.endpoint.address().is_v4()) {
        m_socket.bind(udp::endpoint(address_v4::any(), m_cfg.endpoint.port()));
    } else {
        m_socket.bind(udp::endpoint(address_v6::any(), m_cfg.endpoint.port()));
    }

    if(args.as_object().count("interface")) {
        auto interface = args.as_object().at("interface");

        if(m_cfg.endpoint.address().is_v4()) {
            m_socket.set_option(multicast::outbound_interface(interface.to<address>().to_v4()));
        } else {
            m_socket.set_option(multicast::outbound_interface(interface.as_uint()));
        }
    }

    m_socket.set_option(multicast::enable_loopback(args.as_object().at("loopback", false).as_bool()));
    m_socket.set_option(multicast::hops(args.as_object().at("hops", 1u).as_uint()));

    COCAINE_LOG_INFO(m_log, "joining multicast group '{}'", m_cfg.endpoint, attribute_list({
        {"uuid", m_locator.uuid()}
    }));

    m_socket.set_option(multicast::join_group(m_cfg.endpoint.address()));

    const auto announce = std::make_shared<announce_t>();

    m_socket.async_receive_from(buffer(announce->buffer.data(), announce->buffer.size()),
        announce->endpoint,
        std::bind(&multicast_t::on_receive, this, ph::_1, ph::_2, announce)
    );

    m_signals = std::make_shared<dispatch<context_tag>>(name);
    m_signals->on<context::prepared>(std::bind(&multicast_t::on_publish, this, std::error_code()));

    context.signal_hub().listen(m_signals, m_locator.asio());
}

multicast_t::~multicast_t() {
    m_timer.cancel();
    m_socket.close();

    for(auto it = m_expirations.begin(); it != m_expirations.end(); ++it) {
        it->second->cancel();
    }

    m_expirations.clear();
}

void
multicast_t::on_publish(const std::error_code& ec) {
    if(ec == asio::error::operation_aborted) {
        return;
    }

    const auto actor = m_context.locate("locator");

    if(!actor) {
        COCAINE_LOG_ERROR(m_log, "unable to announce local endpoints: locator is not available");
        return;
    }

    const auto endpoints = actor.get().endpoints();

    if(!endpoints.empty()) {
        COCAINE_LOG_DEBUG(m_log, "announcing {:d} local endpoint(s)", endpoints.size(), attribute_list({
            {"uuid", m_locator.uuid()}
        }));

        msgpack::sbuffer target;
        msgpack::packer<msgpack::sbuffer> packer(target);

        type_traits<announce_t::tuple_type>::pack(packer, std::forward_as_tuple(
            m_locator.uuid(),
            endpoints
        ));

        try {
            m_socket.send_to(buffer(target.data(), target.size()), m_cfg.endpoint);
        } catch(const std::system_error& e) {
            COCAINE_LOG_ERROR(m_log, "unable to announce local endpoints: {}", error::to_string(e));
        }
    } else {
        COCAINE_LOG_ERROR(m_log, "unable to announce local endpoints: node is not reachable");
    }

    m_timer.expires_from_now(m_cfg.interval);
    m_timer.async_wait(std::bind(&multicast_t::on_publish, this, ph::_1));
}

void
multicast_t::on_receive(const std::error_code& ec, size_t bytes_received,
                        const std::shared_ptr<announce_t>& ptr)
{
    if(ec) {
        if(ec != asio::error::operation_aborted) {
            COCAINE_LOG_ERROR(m_log, "unexpected error in multicast_t::on_receive(): [{:d}] {}",
                ec.value(), ec.message());
        }

        return;
    }

    msgpack::unpacked unpacked;

    try {
        msgpack::unpack(&unpacked, ptr->buffer.data(), bytes_received);
    } catch(const msgpack::unpack_error& e) {
        COCAINE_LOG_ERROR(m_log, "unable to unpack announce: {}", e.what());
        return;
    }

    std::string uuid;
    std::vector<tcp::endpoint> endpoints;

    try {
        type_traits<announce_t::tuple_type>::unpack(unpacked.get(), std::tie(uuid, endpoints));
    } catch(const msgpack::type_error& e) {
        COCAINE_LOG_ERROR(m_log, "unable to decode announce: {}", e.what());
        return;
    }

    if(uuid != m_locator.uuid()) {
        COCAINE_LOG_DEBUG(m_log, "received {:d} endpoint(s) from {}", endpoints.size(), ptr->endpoint, attribute_list({
            {"uuid", uuid}
        }));

        auto& expiration = m_expirations[uuid];

        if(!expiration) {
            expiration = std::make_unique<deadline_timer>(m_locator.asio());
        }

        // Link node always on announce - delegate decision of establishing connection to locator
        m_locator.link_node(uuid, endpoints);

        expiration->expires_from_now(m_cfg.interval * 3);
        expiration->async_wait(std::bind(&multicast_t::on_expired, this, ph::_1, uuid));
    }

    const auto announce = std::make_shared<announce_t>();

    m_socket.async_receive_from(buffer(announce->buffer.data(), announce->buffer.size()),
        announce->endpoint,
        std::bind(&multicast_t::on_receive, this, ph::_1, ph::_2, announce)
    );
}

void
multicast_t::on_expired(const std::error_code& ec, const std::string& uuid) {
    if(ec == asio::error::operation_aborted) {
        return;
    }

    COCAINE_LOG_ERROR(m_log, "remote endpoints have expired", {
        {"uuid", uuid}
    });

    m_locator.drop_node(uuid);
    m_expirations.erase(uuid);
}
