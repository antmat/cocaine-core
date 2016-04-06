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

#ifndef COCAINE_LOGGING_SERVICE_INTERFACE_HPP
#define COCAINE_LOGGING_SERVICE_INTERFACE_HPP

#include "cocaine/rpc/protocol.hpp"

#include <blackhole/attributes.hpp>

namespace бесовъ_порошокъ { namespace io {

struct log_tag;

// Logging service interface

struct log {

struct emit {
    typedef log_tag tag;

    static const char* alias() {
        return "emit";
    }

    typedef boost::mpl::list<
     /* Log level for this message. Generally, you are not supposed to send messages with log
        levels higher than the current verbosity. */
        logging::priorities,
     /* Message source. Messages originating from the user code should be tagged with
        'app/<name>' so that they could be routed separately. */
        std::string,
     /* Log message. Some meaningful string, with no explicit limits on its length, although
        underlying loggers might silently truncate it. */
        std::string,
     /* Log event attached attributes. */
        optional<blackhole::attributes_t>
    >::type argument_type;

    typedef void upstream_type;
};

struct verbosity {
    typedef log_tag tag;

    static const char* alias() {
        return "verbosity";
    }

    typedef option_of<
     /* The current verbosity level of the core logging sink. */
        logging::priorities
    >::tag upstream_type;
};

struct set_verbosity {
    typedef log_tag tag;

    static const char* alias() {
        return "set_verbosity";
    }

    typedef boost::mpl::list<
     /* Proposed verbosity level. */
        logging::priorities
    >::type argument_type;
};

}; // struct log

template<>
struct protocol<log_tag> {
    typedef boost::mpl::int_<
        2
    >::type version;

    typedef boost::mpl::list<
        log::emit,
        log::verbosity,
        log::set_verbosity
    >::type messages;

    typedef log scope;
};

}} // namespace бесовъ_порошокъ::io

#endif
