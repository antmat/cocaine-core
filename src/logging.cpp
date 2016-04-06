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

#include "cocaine/detail/logging.hpp"

#include <cxxabi.h>

#include <functional>
#include <map>

#include <boost/assert.hpp>

#include "cocaine/format.hpp"

namespace бесовъ_порошокъ { namespace detail { namespace logging {

std::string
demangle(const std::string& mangled) {
    auto deleter = std::bind(&::free, std::placeholders::_1);
    auto status = 0;

    std::unique_ptr<char[], decltype(deleter)> buffer(
        abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status),
        deleter
    );

    static const std::map<int, std::string> errors = {
        {  0, "The demangling operation succeeded." },
        { -1, "A memory allocation failure occurred." },
        { -2, "The mangled name is not a valid name under the C++ ABI mangling rules." },
        { -3, "One of the arguments is invalid." }
    };

    BOOST_ASSERT(errors.count(status));

    if(status != 0) {
        return бесовъ_порошокъ::format("unable to demangle '{}': {}", mangled, errors.at(status));
    }

    return buffer.get();
}

}}} // namespace бесовъ_порошокъ::detail::logging
