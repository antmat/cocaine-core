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

#include "cocaine/detail/service/node.hpp"
#include "cocaine/detail/service/node/app.hpp"

#include "cocaine/api/storage.hpp"

#include "cocaine/context.hpp"
#include "cocaine/logging.hpp"

#include "cocaine/traits/dynamic.hpp"

#include "cocaine/tuple.hpp"

#include <blackhole/scoped_attributes.hpp>

#include <boost/spirit/include/karma_char.hpp>
#include <boost/spirit/include/karma_generate.hpp>
#include <boost/spirit/include/karma_list.hpp>
#include <boost/spirit/include/karma_string.hpp>

using namespace cocaine::io;
using namespace cocaine::service;

namespace ph = std::placeholders;

node_t::node_t(context_t& context, asio::io_service& asio, const std::string& name, const dynamic_t& args):
    category_type(context, asio, name, args),
    dispatch<node_tag>(name),
    m_context(context),
    m_log(context.log(name))
{
    on<node::start_app>(std::bind(&node_t::on_start_app, this, ph::_1, ph::_2));
    on<node::pause_app>(std::bind(&node_t::on_pause_app, this, ph::_1));
    on<node::list>(std::bind(&node_t::on_list, this));

    const auto runlist_id = args.as_object().at("runlist", "default").as_string();
    const auto storage = api::storage(m_context, "core");

    typedef std::map<std::string, std::string> runlist_t;

    runlist_t runlist;

    {
        blackhole::scoped_attributes_t attributes(*m_log, {
            blackhole::attribute::make("runlist", runlist_id)
        });

        COCAINE_LOG_INFO(m_log, "reading runlist");

        try {
            runlist = storage->get<runlist_t>("runlists", runlist_id);
        } catch(const storage_error_t& e) {
            COCAINE_LOG_WARNING(m_log, "unable to read runlist: %s", e.what());
        }
    }

    if(runlist.empty()) {
        return;
    }

    COCAINE_LOG_INFO(m_log, "starting %d app(s)", runlist.size());

    std::vector<std::string> errored;

    for(auto it = runlist.begin(); it != runlist.end(); ++it) {
        blackhole::scoped_attributes_t attributes(*m_log, {
            blackhole::attribute::make("app", it->first)
        });

        try {
            on_start_app(it->first, it->second);
        } catch(const std::exception& e) {
            COCAINE_LOG_ERROR(m_log, "unable to initialize app: %s", e.what());
            errored.push_back(it->first);
        } catch(...) {
            COCAINE_LOG_ERROR(m_log, "unable to initialize app");
            errored.push_back(it->first);
        }
    }

    if(!errored.empty()) {
        std::ostringstream stream;
        std::ostream_iterator<char> builder(stream);

        boost::spirit::karma::generate(builder, boost::spirit::karma::string % ", ", errored);

        COCAINE_LOG_ERROR(m_log, "couldn't start %d app(s): %s", errored.size(), stream.str());
    }
}

node_t::~node_t() {
    auto ptr = m_apps.synchronize();

    if(ptr->empty()) {
        return;
    }

    COCAINE_LOG_INFO(m_log, "stopping %d apps", ptr->size());

    for(auto it = ptr->begin(); it != ptr->end(); ++it) {
        COCAINE_LOG_INFO(m_log, "trying to stop app '%s'", it->first);
        it->second->pause();
    }

    ptr->clear();
}

const basic_dispatch_t&
node_t::prototype() const {
    return *this;
}

void
node_t::on_start_app(const std::string& name, const std::string& profile) {
    m_apps.apply([&](std::map<std::string, std::shared_ptr<app_t>>& apps) {
        COCAINE_LOG_DEBUG(m_log, "starting app '%s'", name);

        auto it = apps.find(name);
        if(it != apps.end()) {
            throw cocaine::error_t("app '%s' is already running", name);
        }

        auto app = std::make_shared<app_t>(m_context, name, profile);
        app->start();

        apps.insert(std::make_pair(name, app));
    });
}

void
node_t::on_pause_app(const std::string& name) {
    auto ptr = m_apps.synchronize();
    auto it = ptr->find(name);

    COCAINE_LOG_INFO(m_log, "trying to stop app '%s'", name);

    if(it == ptr->end()) {
        throw cocaine::error_t("app '%s' is not running", name);
    }

    it->second->pause();
    ptr->erase(it);
}

results::list
node_t::on_list() const {
    dynamic_t::array_t result;

    auto ptr = m_apps.synchronize();
    auto builder = std::back_inserter(result);

    std::transform(ptr->begin(), ptr->end(), builder, tuple::nth_element<0>());

    return result;
}
