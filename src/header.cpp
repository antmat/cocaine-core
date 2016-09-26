/*
    Copyright (c) 2011-2015 Anton Matveenko <antmat@yandex-team.ru>
    Copyright (c) 2011-2015 Other contributors as noted in the AUTHORS file.

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

#include "cocaine/hpack/header.hpp"
#include "cocaine/hpack/static_table.hpp"

#include <cassert>
#include <numeric>
#include <sstream>

namespace cocaine { namespace hpack {

namespace header {

boost::optional<const header_t&>
find_first(const std::vector<header_t>& headers, const char* name, size_t sz) {
    auto it = std::find_if(headers.begin(), headers.end(), [&](const header_t& h){
        return h.name().size() == sz && h.name() == name;
    });
    if(it != headers.end()) {
        return boost::make_optional<const header_t&>(*it);
    }
    return boost::none;
}

boost::optional<const header_t&>
find_first(const std::vector<header_t>& headers, const std::string& name) {
    return find_first(headers, name.c_str(), name.size());
}

}

size_t
http2_integer_size(size_t sz, size_t bit_offset) {
    if(bit_offset == 0 || bit_offset > 7) {
        throw std::system_error(
            std::make_error_code(std::errc::invalid_argument),
            "Invalid bit_offset for http2_integer_size"
        );
    }
    // See packing here https://httpwg.github.io/specs/rfc7541.html#integer.representation
    // if integer fits to 8 - bit_offset bits
    if(sz < static_cast<size_t>(1 << (8 - bit_offset))) {
        return 1;
    }
    // One byte is first and we start to write in second one
    size_t ret = 2;
    sz -= (1 << (8 - bit_offset));
    while(sz > 127) {
        sz = sz >> 7;
        ret++;
    }
    return ret;
}

struct init_header_t {
    init_header_t(header_static_table_t::storage_t& _data) :
        data(_data)
    {}
    template<class Header>
    void
    operator()(Header) {
        data[boost::mpl::find<header_static_table_t::headers_storage, Header>::type::pos::value] = header_t::create<Header>();
    }
    header_static_table_t::storage_t& data;
};

static
header_static_table_t::storage_t
init_data() {
    header_static_table_t::storage_t data;
    init_header_t init(data);
    boost::mpl::for_each<header_static_table_t::headers_storage>(init);
    return data;
}

bool
header_t::operator==(const header_t& other) const {
    return data.name == other.data.name && data.value == other.data.value;
}

bool
header_t::name_equal(const header_t& other) const {
    return data.name == other.data.name;
}

header_t::header_t(std::string _name, std::string _value) :
    data({std::move(_name),std::move(_value)})
{}

const std::string&
header_t::name() const {
    return data.name;
}

const std::string&
header_t::value() const {
    return data.value;
}

size_t
header_t::http2_size() const {
    // 1 refer to string literals which has size with 1-bit padding.
    // See https://tools.ietf.org/html/draft-ietf-httpbis-header-compression-12#section-5.2
    return data.name.size() + http2_integer_size(data.name.size(), 1) +
        data.value.size() + http2_integer_size(data.value.size(), 1) + header_table_t::http2_header_overhead;
}

const header_static_table_t::storage_t&
header_static_table_t::get_headers() {
    static storage_t storage = init_data();
    return storage;
}

header_table_t::header_table_t() :
    capacity(max_data_capacity)
{}

size_t
header_table_t::data_size() const {
    size_t total = 0;
    for (const auto& h : headers) {
        total += h.http2_size();
    }
    return total;
}

size_t
header_table_t::data_capacity() const {
    return capacity;
}

size_t
header_table_t::size() const {
    return header_static_table_t::size + headers.size();
}

bool
header_table_t::empty() const {
    return headers.empty();
}


void
header_table_t::push(header_t header) {
    size_t header_size = header.http2_size();

    // Pop headers from table until there is enough room for new one or table is empty
    auto sz = data_size();
    while(sz + header_size > capacity && !empty()) {
        sz -= headers.back().http2_size();
        headers.pop_back();
    }

    // Header does not fit in the table. According to RFC we just clean the table and do not put the header inside.
    if(empty() && (data_size() + header_size > capacity)) {
        return;
    }

    headers.push_front(std::move(header));
}

size_t
header_table_t::find(const std::function<bool(const header_t&)> comp) {
    auto it = std::find_if(header_static_table_t::get_headers().begin(), header_static_table_t::get_headers().end(), comp);
    if(it != header_static_table_t::get_headers().end()) {
        return it - header_static_table_t::get_headers().begin();
    }
    auto dyn_it = std::find_if(headers.begin(), headers.end(), comp);
    if(dyn_it != headers.end()) {
        return dyn_it - headers.begin() + header_static_table_t::size;
    }
    return 0;
}

const header_t&
header_table_t::operator[](size_t idx) {
    if(idx == 0 || idx > headers.size() + header_static_table_t::size) {
        throw std::out_of_range("Invalid index for header table");
    }
    if(idx < header_static_table_t::size) {
        return header_static_table_t::get_headers()[idx];
    }
    return headers[idx - header_static_table_t::size];
}

}} // namespace cocaine::hpack
