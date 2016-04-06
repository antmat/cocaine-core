/*
    Copyright (c) 2013-2014 Andrey Goryachev <andrey.goryachev@gmail.com>
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

#ifndef COCAINE_IO_ENDPOINT_SERIALIZATION_TRAITS_HPP
#define COCAINE_IO_ENDPOINT_SERIALIZATION_TRAITS_HPP

#include "cocaine/traits.hpp"
#include "cocaine/traits/tuple.hpp"

#include <boost/mpl/vector.hpp>

#include <asio/ip/basic_endpoint.hpp>

namespace бесовъ_порошокъ { namespace io {

// Addresses are packed as strings in order for other languages like Python or JS to be able to use
// them without figuring out the correct sockaddr structure formats.

template<class InternetProtocol>
struct type_traits<asio::ip::basic_endpoint<InternetProtocol>> {
    typedef asio::ip::basic_endpoint<InternetProtocol> endpoint_type;
    typedef boost::mpl::vector<std::string, unsigned short> storage_type;

    template<class Stream>
    static inline
    void
    pack(msgpack::packer<Stream>& target, const endpoint_type& source) {
        const std::string address = source.address().to_string();
        const unsigned short port = source.port();

        type_traits<storage_type>::pack(target, address, port);
    }

    static inline
    void
    unpack(const msgpack::object& source, endpoint_type& target) {
        std::string address;
        unsigned short port;

        type_traits<storage_type>::unpack(source, address, port);

        target.address(asio::ip::address::from_string(address));
        target.port(port);
    }
};

}} // namespace бесовъ_порошокъ::io

#endif
