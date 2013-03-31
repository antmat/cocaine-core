/*
    Copyright (c) 2011-2012 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2012 Other contributors as noted in the AUTHORS file.

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

#ifndef COCAINE_SERVICE_LOCATOR_HPP
#define COCAINE_SERVICE_LOCATOR_HPP

#include "cocaine/dispatch.hpp"

namespace cocaine {

namespace io { namespace locator {
    struct description_t;
}}

class actor_t;

class locator_t:
    public dispatch_t
{
    public:
        locator_t(context_t& context);
       ~locator_t();

        void
        attach(const std::string& name,
               std::unique_ptr<actor_t>&& service);

    private:
        io::locator::description_t
        resolve(const std::string& name) const;

    private:
        std::unique_ptr<logging::log_t> m_log;

        typedef std::vector<
            std::pair<std::string, std::unique_ptr<actor_t>>
        > service_list_t;

        // NOTE: These are the instances of all the configured services, stored
        // as a vector of pairs to preserve initialization order.
        service_list_t m_services;
};

} // namespace cocaine

#endif