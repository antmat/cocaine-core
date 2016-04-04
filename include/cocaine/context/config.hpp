/*
    Copyright (c) 2011-2015 Andrey Sibiryov <me@kobology.ru>
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

#ifndef COCAINE_CONTEXT_CONFIG_HPP
#define COCAINE_CONTEXT_CONFIG_HPP

#include "cocaine/common.hpp"

#include <boost/optional/optional_fwd.hpp>

#include <map>
#include <string>
#include <vector>

namespace cocaine {

// Configuration

struct config_t {
public:
    struct path_t {
        virtual
        const std::vector<std::string>&
        plugins() const = 0;

        virtual
        const std::string&
        runtime() const = 0;
    };

    struct network_t {
        struct ports_t {
            virtual
            const std::map<std::string, port_t>&
            pinned() const = 0;

            virtual
            const std::tuple<port_t, port_t>&
            shared() const = 0;
        };

        virtual
        const ports_t&
        ports() const = 0;

        virtual
        const std::string&
        endpoint() const = 0;

        virtual
        const std::string&
        hostname() const = 0;

        virtual
        size_t
        pool() const = 0;
    };

    struct logging_t {
        virtual
        const dynamic_t&
        loggers() const = 0;

        virtual
        logging::priorities
        severity() const = 0;
    };

    struct component_t {
        virtual
        const std::string&
        type() const = 0;

        virtual
        const dynamic_t&
        args() const = 0;
    };

    struct component_group_t {
        typedef std::function<void(const std::string name, const component_t&)> component_visitor_t;

        virtual
        size_t size() const = 0;

        virtual
        boost::optional<const component_t&>
        get(const std::string& name) const = 0;

        virtual
        void
        visit(const component_visitor_t& visitor) const = 0;
    };

    virtual
    const network_t&
    network() const = 0;

    virtual
    const logging_t&
    logging() const = 0;

    virtual
    const path_t&
    path() const = 0;

    virtual
    const component_group_t&
    services() const = 0;

    virtual
    const component_group_t&
    storages() const = 0;

    virtual
    const component_group_t&
    unicorns() const = 0;

    static
    int
    versions();

};

std::unique_ptr<config_t>
make_config(const std::string& source);

} // namespace cocaine

#endif
