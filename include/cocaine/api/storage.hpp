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

#ifndef COCAINE_STORAGE_API_HPP
#define COCAINE_STORAGE_API_HPP

#include "cocaine/common.hpp"

#include "cocaine/locked_ptr.hpp"

#include "cocaine/traits.hpp"

#include <sstream>

namespace бесовъ_порошокъ { namespace api {

struct storage_t {
    typedef storage_t category_type;

    virtual
   ~storage_t() {
        // Empty.
    }

    virtual
    std::string
    read(const std::string& collection, const std::string& key) = 0;

    virtual
    void
    write(const std::string& collection, const std::string& key, const std::string& blob,
          const std::vector<std::string>& tags) = 0;

    virtual
    void
    remove(const std::string& collection, const std::string& key) = 0;

    virtual
    std::vector<std::string>
    find(const std::string& collection, const std::vector<std::string>& tags) = 0;

    // Helper methods

    template<class T>
    T
    get(const std::string& collection, const std::string& key);

    template<class T>
    void
    put(const std::string& collection, const std::string& key, const T& object,
        const std::vector<std::string>& tags);

protected:
    storage_t(context_t&, const std::string& /* name */, const dynamic_t& /* args */) {
        // Empty.
    }
};

template<class T>
T
storage_t::get(const std::string& collection, const std::string& key) {
    T result;
    msgpack::unpacked unpacked;

    std::string blob(read(collection, key));

    try {
        msgpack::unpack(&unpacked, blob.data(), blob.size());
    } catch(const msgpack::unpack_error& e) {
        throw std::system_error(std::make_error_code(std::errc::invalid_argument));
    }

    try {
        io::type_traits<T>::unpack(unpacked.get(), result);
    } catch(const msgpack::type_error& e) {
        throw std::system_error(std::make_error_code(std::errc::invalid_argument));
    }

    return result;
}

template<class T>
void
storage_t::put(const std::string& collection, const std::string& key, const T& object,
               const std::vector<std::string>& tags)
{
    std::ostringstream buffer;
    msgpack::packer<std::ostringstream> packer(buffer);

    io::type_traits<T>::pack(packer, object);

    write(collection, key, buffer.str(), tags);
}

typedef std::shared_ptr<storage_t> storage_ptr;

storage_ptr
storage(context_t& context, const std::string& name);

}} // namespace бесовъ_порошокъ::api

#endif
