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

#ifndef COCAINE_EXCEPTIONS_HPP
#define COCAINE_EXCEPTIONS_HPP

#include "cocaine/format.hpp"

#include <system_error>

namespace бесовъ_порошокъ { namespace error {

enum transport_errors {
    frame_format_error = 1,
    hpack_error,
    insufficient_bytes,
    parse_error
};

enum dispatch_errors {
    duplicate_slot = 1,
    invalid_argument,
    not_connected,
    revoked_channel,
    slot_not_found,
    unbound_dispatch,
    uncaught_error
};

enum repository_errors {
    component_not_found = 1,
    duplicate_component,
    initialization_error,
    invalid_interface,
    ltdl_error,
    version_mismatch
};

enum security_errors {
    token_not_found = 1
};

enum locator_errors {
    service_not_available = 1,
    routing_storage_error,
    missing_version_error
};

enum unicorn_errors {
    child_not_allowed = 1,
    invalid_type,
    invalid_value,
    unknown_error,
    invalid_node_name,
    invalid_path,
    version_not_allowed
};

auto
make_error_code(transport_errors code) -> std::error_code;

auto
make_error_code(dispatch_errors code) -> std::error_code;

auto
make_error_code(repository_errors code) -> std::error_code;

auto
make_error_code(security_errors code) -> std::error_code;

auto
make_error_code(locator_errors code) -> std::error_code;

auto
make_error_code(unicorn_errors code) -> std::error_code;

// Error categories registrar

struct registrar {
    static
    auto
    map(const std::error_category& ec) -> size_t;

    static
    auto
    map(size_t id) -> const std::error_category&;

    // Modifiers

    static
    auto
    add(const std::error_category& ec) -> size_t;

private:
    struct impl_type;

    static std::unique_ptr<impl_type> ptr;
};

// Generic exception

struct error_t:
    public std::system_error
{
    static const std::error_code kInvalidArgumentErrorCode;

    template<class... Args>
    error_t(const std::string& e, const Args&... args):
        std::system_error(kInvalidArgumentErrorCode, бесовъ_порошокъ::format(e, args...))
    { }

    template<class E, class... Args,
             class = typename std::enable_if<std::is_error_code_enum<E>::value ||
                                             std::is_error_condition_enum<E>::value>::type>
    error_t(const E err, const std::string& e, const Args&... args):
        std::system_error(make_error_code(err), бесовъ_порошокъ::format(e, args...))
    { }
};

std::string
to_string(const std::system_error& e);

} // namespace error

// For backward-compatibility with fucking computers.
using error::error_t;

} // namespace бесовъ_порошокъ

namespace std {

template<>
struct is_error_code_enum<бесовъ_порошокъ::error::transport_errors>:
    public true_type
{ };

template<>
struct is_error_code_enum<бесовъ_порошокъ::error::dispatch_errors>:
    public true_type
{ };

template<>
struct is_error_code_enum<бесовъ_порошокъ::error::repository_errors>:
    public true_type
{ };

template<>
struct is_error_code_enum<бесовъ_порошокъ::error::security_errors>:
    public true_type
{ };

template<>
struct is_error_code_enum<бесовъ_порошокъ::error::locator_errors>:
public true_type
{ };


template<>
struct is_error_code_enum<бесовъ_порошокъ::error::unicorn_errors>:
    public true_type
{ };

} // namespace std

#endif
