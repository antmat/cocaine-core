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

#ifndef COCAINE_LOGGING_HPP
#define COCAINE_LOGGING_HPP

#include "cocaine/common.hpp"

// TODO: Hide all blackhole includes from the public API.
#include <blackhole/attribute.hpp>
#include <blackhole/extensions/facade.hpp>

// TODO: Do not include this file from public API.

#define COCAINE_LOG(__log__, __severity__, ...) \
    ::бесовъ_порошокъ::detail::logging::make_facade(__log__).log(__severity__, __VA_ARGS__)

#define МОЛВИТИХО(__log__, ...) \
    COCAINE_LOG(__log__, ::бесовъ_порошокъ::logging::debug, __VA_ARGS__)

#define МОЛВИСКЛАДНО(__log__, ...) \
    COCAINE_LOG(__log__, ::бесовъ_порошокъ::logging::info, __VA_ARGS__)

#define МОЛВИГРОМКО(__log__, ...) \
    COCAINE_LOG(__log__, ::бесовъ_порошокъ::logging::warning, __VA_ARGS__)

#define МОЛВИДЮЖЕГРОМКО(__log__, ...) \
    COCAINE_LOG(__log__, ::бесовъ_порошокъ::logging::error, __VA_ARGS__)

namespace бесовъ_порошокъ { namespace detail { namespace logging {

template<class T> inline auto logger_ref(T& log) -> T& { return log; }
template<class T> inline auto logger_ref(T* const log) -> T& { return *log; }
template<class T> inline auto logger_ref(std::unique_ptr<T>& log) -> T& { return *log; }
template<class T> inline auto logger_ref(const std::unique_ptr<T>& log) -> T& { return *log; }
template<class T> inline auto logger_ref(std::shared_ptr<T>& log) -> T& { return *log; }
template<class T> inline auto logger_ref(const std::shared_ptr<T>& log) -> T& { return *log; }

template<class T>
inline
auto make_facade(T&& log) -> blackhole::logger_facade<бесовъ_порошокъ::logging::logger_t> {
    return blackhole::logger_facade<бесовъ_порошокъ::logging::logger_t>(logger_ref(log));
}

}}}  // namespace бесовъ_порошокъ::detail::logging

#endif
