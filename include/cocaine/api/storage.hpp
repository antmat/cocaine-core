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
#include "cocaine/utility/future.hpp"

#include <sstream>

namespace cocaine { namespace api {

struct storage_t {
    typedef storage_t category_type;

    template<class T>
    using callback = std::function<void(std::future<T>)>;

    virtual
   ~storage_t() {
        // Empty.
    }

    virtual
    void
    read(callback<std::string> cb, const std::string& collection, const std::string& key) = 0;

    virtual
    void
    write(callback<void> cb,
          const std::string& collection,
          const std::string& key,
          const std::string& blob,
          const std::vector<std::string>& tags) = 0;

    virtual
    void
    remove(callback<void> cb, const std::string& collection, const std::string& key) = 0;

    virtual
    void
    find(callback<std::vector<std::string>> cb, const std::string& collection, const std::vector<std::string>& tags) = 0;

    // Helper methods

    template<class T>
    void
    get(callback<T> cb, const std::string& collection, const std::string& key);

    template<class T>
    void
    put(callback<void> cb, const std::string& collection, const std::string& key, const T& object,
        const std::vector<std::string>& tags);

    std::string
    read_sync(const std::string& collection, const std::string& key);

    void
    write_sync(const std::string& collection,
               const std::string& key,
               const std::string& blob,
               const std::vector<std::string>& tags);

    void
    remove_sync(const std::string& collection, const std::string& key);

    std::vector<std::string>
    find_sync(const std::string& collection, const std::vector<std::string>& tags);

    template<class T>
    T
    get_sync(const std::string& collection, const std::string& key);

    template<class T>
    void
    put_sync(const std::string& collection, const std::string& key, const T& object, const std::vector<std::string>& tags);

protected:
    storage_t(context_t&, const std::string& /* name */, const dynamic_t& /* args */) {
        // Empty.
    }
private:
    template <class R, class F, class... Args>
    R
    wrap_sync(F f, const Args&... args) {
        std::future<R> result;
        std::mutex m;
        std::condition_variable cv;
        std::unique_lock<std::mutex> lock(m);
        f([&](std::future<R> future) {
            result = std::move(future);
            cv.notify_one();
        }, args...);
        cv.wait(lock);
        return result.get();
    }
};

template<class T>
void
storage_t::get(callback<T> cb, const std::string& collection, const std::string& key) {

    // TODO: move inside lambda as we move on c++14
    auto inner_cb = [=](std::future<std::string> f) {
        T result;
        msgpack::unpacked unpacked;
        std::string blob;

        try {
            blob = f.get();
        } catch(const std::exception& e) {
            return cb(make_exceptional_future<T>(e));
        }

        try {
            msgpack::unpack(&unpacked, blob.data(), blob.size());
        } catch(const msgpack::unpack_error& e) {
            return cb(make_exceptional_future<T>(std::make_error_code(std::errc::invalid_argument), e.what()));
        }

        try {
            io::type_traits<T>::unpack(unpacked.get(), result);
        } catch(const msgpack::type_error& e) {
            return cb(make_exceptional_future<T>(std::make_error_code(std::errc::invalid_argument), e.what()));
        }

        return cb(make_ready_future(result));
    };
    read(std::move(inner_cb), collection, key);
}

template<class T>
void
storage_t::put(callback<void> cb,
               const std::string& collection,
               const std::string& key,
               const T& object,
               const std::vector<std::string>& tags)
{
    std::ostringstream buffer;
    msgpack::packer<std::ostringstream> packer(buffer);

    io::type_traits<T>::pack(packer, object);

    write(std::move(cb), collection, key, buffer.str(), tags);
}

template<class T>
T
storage_t::get_sync(const std::string& collection, const std::string& key) {
    namespace ph = std::placeholders;
    return wrap_sync<T>(std::bind(&storage_t::get<T>, this, ph::_1, ph::_2, ph::_3), collection, key);
}

template<class T>
void
storage_t::put_sync(const std::string& collection, const std::string& key, const T& object, const std::vector<std::string>& tags) {
    namespace ph = std::placeholders;
    wrap_sync<void>(std::bind(&storage_t::put<T>, this, ph::_1, ph::_2, ph::_3, ph::_4, ph::_5), collection, key, object, tags);
}


typedef std::shared_ptr<storage_t> storage_ptr;

storage_ptr
storage(context_t& context, const std::string& name);

}} // namespace cocaine::api

#endif
