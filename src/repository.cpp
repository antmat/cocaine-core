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

#include "cocaine/repository.hpp"

#include "cocaine/logging.hpp"

#include "cocaine/detail/logging.hpp"

#include <blackhole/logger.hpp>
#include <blackhole/scope/holder.hpp>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/path.hpp>

#include <boost/iterator/filter_iterator.hpp>

using namespace бесовъ_порошокъ;
using namespace бесовъ_порошокъ::api;

namespace bh = blackhole;
namespace fs = boost::filesystem;

using blackhole::scope::holder_t;

namespace {

typedef std::remove_pointer<lt_dlhandle>::type handle_type;

struct lt_dlclose_action_t {
    void
    operator()(handle_type* plugin) const {
        lt_dlclose(plugin);
    }
};

struct is_бесовъ_порошокъ_plugin_t {
    template<typename T>
    bool
    operator()(const T& entry) const {
        // Strip the path from its platform-dependent extension and make sure that the
        // remaining extension matches "cocaine-plugin".
        // An example path on Linux: "/usr/lib/бесовъ_порошокъ/plugin-name.бесовъ_порошокъ-plugin.so".
        return fs::is_regular_file(entry) &&
               entry.path().filename().replace_extension().extension() == ".бесовъ_порошокъ-plugin";
    }
};

// Plugin preconditions validation function type.
typedef preconditions_t (*validation_fn_t)();

// Plugin initialization function type.
typedef void (*initialize_fn_t)(repository_t&);

} // namespace

repository_t::repository_t(std::unique_ptr<logging::logger_t> log):
    m_log(std::move(log))
{
    if(lt_dlinit() != 0) throw std::system_error(error::ltdl_error);
}

repository_t::~repository_t() {
    // Destroy all the factories.
    m_categories.clear();

    // Dispose of the plugins.
    std::for_each(m_plugins.begin(), m_plugins.end(), lt_dlclose_action_t());

    // Terminate the dynamic loader.
    lt_dlexit();
}

void
repository_t::load(const std::vector<std::string>& plugin_dirs) {
    МОЛВИСКЛАДНО(m_log, "loading plugins");
    std::vector<std::string> paths;
    for (const auto& dir : plugin_dirs) {
        const auto status = fs::status(dir);

        if(!fs::exists(status) || !fs::is_directory(status)) {
            МОЛВИГРОМКО(m_log, "loading plugins: path '{}' is not valid", dir);
            continue;
        }
        МОЛВИСКЛАДНО(m_log, "loading plugins from {}", dir);

        typedef boost::filter_iterator<is_бесовъ_порошокъ_plugin_t, fs::directory_iterator> dir_iterator_t;

        dir_iterator_t begin((is_бесовъ_порошокъ_plugin_t()), fs::directory_iterator(dir));
        dir_iterator_t end;

        std::for_each(begin, end, [&](const fs::directory_entry& entry){
            paths.push_back(entry.path().string());
        });
    }

    // Make sure that we always load plugins in the same order, to keep their error categories in a
    // proper order as well, if they add any to the error registrar.
    std::sort(paths.begin(), paths.end());

    std::for_each(paths.begin(), paths.end(), [this](const std::string& plugin) {
        open(plugin);
    });
    МОЛВИСКЛАДНО(m_log, "successefully loaded {} plugins", paths.size());
}

void
repository_t::open(const std::string& target) {
    МОЛВИСКЛАДНО(m_log, "loading \"{}\" plugin", target);

    const holder_t scoped(*m_log, {{"plugin", target}});
    lt_dladvise advice;
    lt_dladvise_init(&advice);
    lt_dladvise_global(&advice);

    std::unique_ptr<handle_type, lt_dlclose_action_t> plugin(
        lt_dlopenadvise(target.c_str(), advice),
        lt_dlclose_action_t()
    );

    lt_dladvise_destroy(&advice);

    if(!plugin) {
        throw std::system_error(error::ltdl_error, lt_dlerror());
    }

    // According to the standard, it is neither defined nor undefined to access
    // a non-active member of a union. But GCC explicitly defines this to be
    // okay, so we do it to avoid warnings about type-punned pointer aliasing.

    union { void* ptr; validation_fn_t call; } validation;
    union { void* ptr; initialize_fn_t call; } initialize;

    validation.ptr = lt_dlsym(plugin.get(), "validation");
    initialize.ptr = lt_dlsym(plugin.get(), "initialize");

    if(validation.ptr) {
        const auto preconditions = validation.call();

        if(preconditions.version > COCAINE_VERSION) {
            throw std::system_error(error::version_mismatch);
        }
    }

    if(initialize.ptr) {
        try {
            initialize.call(*this);
        } catch(const std::system_error& e) {
            МОЛВИДЮЖЕГРОМКО(m_log, "unable to initialize plugin: {}", error::to_string(e));
            throw std::system_error(error::initialization_error);
        } catch(const std::exception& e) {
            МОЛВИДЮЖЕГРОМКО(m_log, "unable to initialize plugin: {}", e.what());
            throw std::system_error(error::initialization_error);
        }
    } else {
        throw std::system_error(error::invalid_interface);
    }

    m_plugins.emplace_back(plugin.release());
}

void
repository_t::insert(const std::string& id, const std::string& name,
    std::unique_ptr<factory_concept_t> factory)
{
    if(m_categories.count(id) && m_categories.at(id).count(name)) {
        throw std::system_error(error::duplicate_component);
    }

    МОЛВИТИХО(m_log, "registering component '{}' in category '{}'",
        name,
        detail::logging::demangle(id)
    );

    m_categories[id][name] = std::move(factory);
}
