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

#ifndef COCAINE_IO_MAP_SERIALIZATION_TRAITS_HPP
#define COCAINE_IO_MAP_SERIALIZATION_TRAITS_HPP

#include "cocaine/traits.hpp"

#include <map>

namespace бесовъ_порошокъ { namespace io {

template<class K, class V>
struct type_traits<std::map<K, V>> {
    template<class Stream>
    static inline
    void
    pack(msgpack::packer<Stream>& target, const std::map<K, V>& source) {
        target.pack_map(source.size());

        for(auto it = source.begin(); it != source.end(); ++it) {
            type_traits<K>::pack(target, it->first);
            type_traits<V>::pack(target, it->second);
        }
    }

    static inline
    void
    unpack(const msgpack::object& source, std::map<K, V>& target) {
        if(source.type != msgpack::type::MAP) {
            throw msgpack::type_error();
        }

        target.clear();

        for(size_t i = 0; i < source.via.map.size; ++i) {
            std::pair<K, V> map_pair;

            type_traits<K>::unpack(source.via.map.ptr[i].key, map_pair.first);
            type_traits<V>::unpack(source.via.map.ptr[i].val, map_pair.second);

            target.insert(std::move(map_pair));
        }
    }
};

}} // namespace бесовъ_порошокъ::io

#endif
