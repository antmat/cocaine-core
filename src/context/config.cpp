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

#include "cocaine/context/config.hpp"
#include "cocaine/defaults.hpp"
#include "cocaine/dynamic.hpp"
#include "cocaine/errors.hpp"

#include <asio/io_service.hpp>
#include <asio/ip/host_name.hpp>
#include <asio/ip/tcp.hpp>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/optional/optional.hpp>
#include <boost/thread/thread.hpp>

#include "rapidjson/reader.h"

namespace бесовъ_порошокъ {

namespace fs = boost::filesystem;

namespace {

struct dynamic_reader_t {
    void
    Null() {
        m_stack.emplace(dynamic_t::null);
    }

    void
    Bool(bool v) {
        m_stack.emplace(v);
    }

    void
    Int(int v) {
        m_stack.emplace(v);
    }

    void
    Uint(unsigned v) {
        m_stack.emplace(dynamic_t::uint_t(v));
    }

    void
    Int64(int64_t v) {
        m_stack.emplace(v);
    }

    void
    Uint64(uint64_t v) {
        m_stack.emplace(dynamic_t::uint_t(v));
    }

    void
    Double(double v) {
        m_stack.emplace(v);
    }

    void
    String(const char* data, size_t size, bool) {
        m_stack.emplace(dynamic_t::string_t(data, size));
    }

    void
    StartObject() {
        // Empty.
    }

    void
    EndObject(size_t size) {
        dynamic_t::object_t object;

        for(size_t i = 0; i < size; ++i) {
            dynamic_t value = std::move(m_stack.top());
            m_stack.pop();

            std::string key = std::move(m_stack.top().as_string());
            m_stack.pop();

            object[key] = std::move(value);
        }

        m_stack.emplace(std::move(object));
    }

    void
    StartArray() {
        // Empty.
    }

    void
    EndArray(size_t size) {
        dynamic_t::array_t array(size);

        for(size_t i = size; i != 0; --i) {
            array[i - 1] = std::move(m_stack.top());
            m_stack.pop();
        }

        m_stack.emplace(std::move(array));
    }

    dynamic_t
    Result() {
        return m_stack.top();
    }

private:
    std::stack<dynamic_t> m_stack;
};

struct rapidjson_ifstream_t {
    rapidjson_ifstream_t(fs::ifstream* backend) :
        m_backend(backend)
    { }

    char
    Peek() const {
        int next = m_backend->peek();

        if(next == std::char_traits<char>::eof()) {
            return '\0';
        } else {
            return next;
        }
    }

    char
    Take() {
        int next = m_backend->get();

        if(next == std::char_traits<char>::eof()) {
            return '\0';
        } else {
            return next;
        }
    }

    size_t
    Tell() const {
        return m_backend->gcount();
    }

    char*
    PutBegin() {
        assert(false);
        return 0;
    }

    void
    Put(char) {
        assert(false);
    }

    size_t
    PutEnd(char*) {
        assert(false);
        return 0;
    }

private:
    fs::ifstream* m_backend;
};

} // namespace

template<size_t Version>
struct config : public config_t {
public:
    struct path_t : public config_t::path_t {
        virtual
        const std::vector<std::string>&
        plugins() const {
            return m_plugins;
        }

        virtual
        const std::string&
        runtime() const {
            return m_runtime;
        }

        path_t(const dynamic_t::object_t& source) {
            const auto& plugins = source.at("plugins", defaults::plugins_path);
            if(plugins.is_array()) {
                for(const auto& plugin_entry: plugins.as_array()) {
                    m_plugins.push_back(plugin_entry.as_string());
                }
            } else if (plugins.is_string()) {
                m_plugins.push_back(plugins.as_string());
            } else {
                throw бесовъ_порошокъ::error_t("\"plugins\" section value should be either string or array of strings");
            }
            m_runtime = source.at("runtime", defaults::runtime_path).as_string();

            const auto runtime_path_status = fs::status(m_runtime);

            if(!fs::exists(runtime_path_status)) {
                throw бесовъ_порошокъ::error_t("directory {} does not exist", m_runtime);
            } else if(!fs::is_directory(runtime_path_status)) {
                throw бесовъ_порошокъ::error_t("{} is not a directory", m_runtime);
            }
        }

        std::vector<std::string> m_plugins;
        std::string m_runtime;
    };

    struct network_t : public config_t::network_t {
        struct ports_t : public config_t::network_t::ports_t {
            virtual
            const std::map<std::string, port_t>&
            pinned() const {
                return m_pinned;
            }

            virtual
            const std::tuple<port_t, port_t>&
            shared() const {
                return m_shared;
            };

            ports_t(const dynamic_t::object_t& source) {
                auto pinned = source.at("pinned", dynamic_t::empty_object);
                if(!pinned.is_object()) {
                    throw бесовъ_порошокъ::error_t("invalid configuration for \"pinned\" section - {}", boost::lexical_cast<std::string>(pinned));
                }
                for(const auto& pair : pinned.as_object()) {
                    if(!pair.second.is_uint()) {
                        throw бесовъ_порошокъ::error_t("invalid configuration for \"pinned\" section - {}", boost::lexical_cast<std::string>(pinned));
                    }
                    m_pinned[pair.first] = pair.second.as_uint();
                }
                auto shared = source.at("shared", dynamic_t::array_t{0u,0u});
                if(!shared.is_array() ||
                    shared.as_array().size() != 2 ||
                    !shared.as_array()[0].is_uint() ||
                    !shared.as_array()[1].is_uint())
                {
                    throw бесовъ_порошокъ::error_t("invalid configuration for \"shared\" section - {}", boost::lexical_cast<std::string>(shared));
                }
                m_shared = std::make_tuple(shared.as_array()[0].as_uint(), shared.as_array()[1].as_uint());
            }

            std::map<std::string, port_t> m_pinned;
            std::tuple<port_t, port_t> m_shared;
        };

        virtual
        const ports_t&
        ports() const {
            return m_ports;
        }

        virtual
        const std::string&
        endpoint() const {
            return m_endpoint;
        }

        virtual
        const std::string&
        hostname() const {
            return m_hostname;
        };

        virtual
        size_t
        pool() const {
            return m_pool;
        }

        network_t(const dynamic_t::object_t& source) :
            m_ports(source)
        {
            m_endpoint = source.at("endpoint", defaults::endpoint).as_string();

            asio::io_service asio;
            asio::ip::tcp::resolver resolver(asio);
            asio::ip::tcp::resolver::iterator it, end;

            try {
                it = resolver.resolve(asio::ip::tcp::resolver::query(
                    asio::ip::host_name(), std::string(),
                    asio::ip::tcp::resolver::query::canonical_name
                ));
            } catch(const std::system_error& e) {
                auto msg = format("unable to determine local hostname - {}", error::to_string(e));
                throw std::system_error(e.code(), std::move(msg));
            }

            m_hostname = source.at("hostname", it->host_name()).as_string();
            m_pool     = source.at("pool", boost::thread::hardware_concurrency() * 2).as_uint();

            if(m_pool <= 0) {
                throw бесовъ_порошокъ::error_t("network I/O pool size must be positive");
            }
        }

        ports_t m_ports;
        std::string m_endpoint;
        std::string m_hostname;
        size_t m_pool;
    };

    struct logging_t : public config_t::logging_t {
        virtual
        const dynamic_t&
        loggers() const {
            return m_loggers;
        }

        virtual
        logging::priorities
        severity() const {
            return m_severity;
        }

        logging_t(const dynamic_t::object_t& source) {
            if(source.count("loggers") == 0) {
                throw error_t("missing \"logging.loggers\" field in configuration file");
            }
            m_loggers = source.at("loggers");

            static std::map<std::string, logging::priorities> priorities{
                {"debug",   logging::debug  },
                {"info",    logging::info   },
                {"warning", logging::warning},
                {"error",   logging::error  }
            };

            auto severity = source.at("severity", "info").as_string();
            try {
                m_severity = priorities.at(severity);
            } catch (const std::out_of_range&) {
                throw бесовъ_порошокъ::error_t("severity \"{}\" not found", severity);
            }
        }

        dynamic_t m_loggers;
        logging::priorities m_severity;
    };

    struct component_t : public config_t::component_t {
        virtual
        const std::string&
        type() const {
            return m_type;
        }

        virtual
        const dynamic_t&
        args() const {
            return m_args;
        }

        component_t() {}

        component_t(const dynamic_t& source) :
            m_type(source.as_object().at("type", "unspecified").as_string()),
            m_args(source.as_object().at("args", dynamic_t::empty_object))
        {}

        std::string m_type;
        dynamic_t   m_args;
    };

    typedef std::map<std::string, component_t> component_map_t;

    struct component_group_t : public config_t::component_group_t {
        virtual
        size_t size() const {
            return components.size();
        }

        virtual
        boost::optional<const config_t::component_t&>
        get(const std::string& name) const {
            auto it = components.find(name);
            if(it == components.end()) {
                return boost::none;
            }
            return it->second;
        }

        virtual
        void
        each(const callable_t& callable) const {
            for(const auto& p : components) {
                callable(p.first, p.second);
            }
        }

        component_group_t() {}

        component_group_t(std::string name, const dynamic_t::object_t& source) :
            m_name(std::move(name))
        {
            for(const auto& p : source) {
                if(!p.second.is_object()) {
                    throw error::error_t("invalid configuration for {} components section - {} (in {})",
                                         m_name,
                                         boost::lexical_cast<std::string>(p.second));
                }
                components[p.first] = component_t(p.second);
            }
        }

        std::string m_name;
        component_map_t components;
    };

    virtual
    const network_t&
    network() const {
        return m_network;
    }

    virtual
    const logging_t&
    logging() const {
        return m_logging;
    }

    virtual
    const path_t&
    path() const {
        return m_path;
    }

    virtual
    const component_group_t&
    services() const {
        return component_groups.at("services");
    }

    virtual
    const component_group_t&
    storages() const {
        return component_groups.at("storages");
    }

    virtual
    const component_group_t&
    unicorns() const {
        return component_groups.at("unicorns");
    }

    static
    dynamic_t
    read_source_file(const std::string& source_file) {
        const auto source_file_status = fs::status(source_file);

        if(!fs::exists(source_file_status) || !fs::is_regular_file(source_file_status)) {
            throw бесовъ_порошокъ::error_t("configuration file path is invalid");
        }

        fs::ifstream stream(source_file);

        if(!stream) {
            throw бесовъ_порошокъ::error_t("unable to read configuration file");
        }

        rapidjson::MemoryPoolAllocator<> json_allocator;
        rapidjson::Reader json_reader(&json_allocator);
        rapidjson_ifstream_t json_stream(&stream);

        dynamic_reader_t configuration_constructor;

        if(!json_reader.Parse<rapidjson::kParseDefaultFlags>(json_stream, configuration_constructor)) {
            if(json_reader.HasParseError()) {
                throw бесовъ_порошокъ::error_t("configuration file is corrupted - {}", json_reader.GetParseError());
            } else {
                throw бесовъ_порошокъ::error_t("configuration file is corrupted");
            }
        }
        auto root = configuration_constructor.Result();

        if(root.as_object().at("version", 0).to<unsigned int>() != Version) {
            throw бесовъ_порошокъ::error_t("configuration file version is invalid");
        }

        return root;
    }

    static
    std::map<std::string, component_group_t>
    init_components_groups(const dynamic_t& source) {
        auto services_src = source.as_object().at("services", dynamic_t::empty_object).as_object();
        auto storages_src = source.as_object().at("storages", dynamic_t::empty_object).as_object();
        auto unicorns_src = source.as_object().at("unicorns", dynamic_t::empty_object).as_object();

        std::map<std::string, component_group_t> groups;
        groups["services"] = component_group_t("services", services_src);
        groups["storages"] = component_group_t("storages", storages_src);
        groups["unicorns"] = component_group_t("unicorns", unicorns_src);
        return groups;
    };

    config(const std::string& source_file) :
        m_source(read_source_file(source_file)),
        m_path(m_source.as_object().at("paths", dynamic_t::empty_object).as_object()),
        m_network(m_source.as_object().at("network", dynamic_t::empty_object).as_object()),
        m_logging(m_source.as_object().at("logging", dynamic_t::empty_object).as_object()),
        component_groups(init_components_groups(m_source))
    {
    }

    dynamic_t m_source;
    path_t m_path;
    network_t m_network;
    logging_t m_logging;
    std::map<std::string, component_group_t> component_groups;
};

int
config_t::versions() {
    return COCAINE_VERSION;
}

std::unique_ptr<config_t>
make_config(const std::string& source) {
    return std::unique_ptr<config_t>(new config<4>(source));
}

} //  namespace бесовъ_порошокъ
