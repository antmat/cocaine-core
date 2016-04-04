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
#include "cocaine/errors.hpp"

#include <asio/io_service.hpp>
#include <asio/ip/host_name.hpp>
#include <asio/ip/tcp.hpp>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/thread/thread.hpp>

#include "rapidjson/reader.h"

namespace cocaine {

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

template<>
struct dynamic_converter<config_t::component_t> {
    typedef config_t::component_t result_type;

    static
    result_type
    convert(const dynamic_t& from) {
        return config_t::component_t {
            from.as_object().at("type", "unspecified").as_string(),
            from.as_object().at("args", dynamic_t::object_t())
        };
    }
};

template<>
struct dynamic_converter<config_t::logging_t> {
    typedef config_t::logging_t result_type;

    static
    result_type
    convert(const dynamic_t& from) {
        result_type result{
            from.as_object().at("loggers"),
            logmask(from.as_object().at("severity", "info").as_string())
        };

        return result;
    }

    static inline
    logging::priorities
    logmask(const std::string& severity) {
        static std::map<std::string, logging::priorities> priorities{
            {"debug",   logging::debug  },
            {"info",    logging::info   },
            {"warning", logging::warning},
            {"error",   logging::error  }
        };

        try {
            return priorities.at(severity);
        } catch (const std::out_of_range&) {
            throw cocaine::error_t("severity \"{}\" not found", severity);
        }
    }
};


struct config_impl_t : public config_t {
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

        // Paths to load plugins from
        std::vector<std::string> m_plugins;
        std::string m_runtime;
    };




    config_impl_t(const std::string& source) {

    }



    const config_t::path_t&
    path() const {
        return m_path;
    }

    struct network_t : public config_t::network_t {
        struct ports_t : public config_t::network_t::ports_t {
            virtual
            const std::map<std::string, port_t>&
            pinned() const {
                return pinned;
            }

            virtual
            const std::tuple<port_t, port_t>&
            shared() const {
                return shared;
            };

            // Pinned ports for static service port allocation.
            std::map<std::string, port_t> pinned;

            // Port range to populate the dynamic port pool for service port allocation.
            std::tuple<port_t, port_t> shared;
        };

        virtual
        const ports_t&
        ports() const {
            return m_ports;
        }

        virtual
        const std::string&
            endpoint() const = 0;

        virtual
        const std:string&
        hostname() const = 0;

        virtual
        size_t
            pool() const = 0;
        // An endpoint where all the services will be bound. Note that binding on [::] will bind on
        // 0.0.0.0 too as long as the "net.ipv6.bindv6only" sysctl is set to 0 (default).
        asio::ip::address endpoint;

        // Local hostname. In case it can't be automatically detected by resolving a CNAME for the
        // contents of /etc/hostname via the default system resolver, it can be configured manually.
        std::string hostname;

        // I/O thread pool size.
        size_t pool;
    };

    struct logging_t : public config_t::logging_t {
        dynamic_t loggers;
        logging::priorities severity;
    };

    struct component_t : public config_t::component_t {
        std::string type;
        dynamic_t   args;
    };

    typedef std::map<std::string, component_t> component_map_t;

    path_t m_path;
    network_t m_network;
    logging_t m_logging;
    component_map_t m_services;
    component_map_t m_storages;
    component_map_t m_unicorns;


};


config_t::config_t(const std::string& source) {
    const auto source_file_status = fs::status(source);

    if(!fs::exists(source_file_status) || !fs::is_regular_file(source_file_status)) {
        throw cocaine::error_t("configuration file path is invalid");
    }

    fs::ifstream stream(source);

    if(!stream) {
        throw cocaine::error_t("unable to read configuration file");
    }

    rapidjson::MemoryPoolAllocator<> json_allocator;
    rapidjson::Reader json_reader(&json_allocator);
    rapidjson_ifstream_t json_stream(&stream);

    dynamic_reader_t configuration_constructor;

    if(!json_reader.Parse<rapidjson::kParseDefaultFlags>(json_stream, configuration_constructor)) {
        if(json_reader.HasParseError()) {
            throw cocaine::error_t("configuration file is corrupted - {}", json_reader.GetParseError());
        } else {
            throw cocaine::error_t("configuration file is corrupted");
        }
    }

    const auto root = configuration_constructor.Result();

    // Version validation

    if(root.as_object().at("version", 0).to<unsigned int>() != 4) {
        throw cocaine::error_t("configuration file version is invalid");
    }

    const auto path_config    = root.as_object().at("paths",   dynamic_t::object_t()).as_object();
    const auto network_config = root.as_object().at("network", dynamic_t::object_t()).as_object();

    // Path configuration

    // string argument for plugin folders is left for backward compatibility
    // TODO: drop string argument as possible value on next config version change
    const auto& plugins = path_config.at("plugins", defaults::plugins_path);
    if(plugins.is_array()) {
        for(const auto& plugin_entry: plugins.as_array()) {
            path.plugins.push_back(plugin_entry.as_string());
        }
    } else if (plugins.is_string()) {
        path.plugins.push_back(plugins.as_string());
    } else {
        throw cocaine::error_t("\"plugins\" value should be either string or array of strings", path.runtime);
    }
    path.runtime = path_config.at("runtime", defaults::runtime_path).as_string();

    const auto runtime_path_status = fs::status(path.runtime);

    if(!fs::exists(runtime_path_status)) {
        throw cocaine::error_t("directory {} does not exist", path.runtime);
    } else if(!fs::is_directory(runtime_path_status)) {
        throw cocaine::error_t("{} is not a directory", path.runtime);
    }

    // Network configuration

    network.endpoint = asio::ip::address::from_string(
        network_config.at("endpoint", defaults::endpoint).as_string()
    );

    asio::io_service asio;
    asio::ip::tcp::resolver resolver(asio);
    asio::ip::tcp::resolver::iterator it, end;

    try {
        it = resolver.resolve(asio::ip::tcp::resolver::query(
            asio::ip::host_name(), std::string(),
            asio::ip::tcp::resolver::query::canonical_name
        ));
    } catch(const std::system_error& e) {
        throw std::system_error(e.code(), "unable to determine local hostname");
    }

    network.hostname = network_config.at("hostname", it->host_name()).as_string();
    network.pool     = network_config.at("pool", boost::thread::hardware_concurrency() * 2).as_uint();

    if(network.pool <= 0) {
        throw cocaine::error_t("network I/O pool size must be positive");
    }

    if(network_config.count("pinned")) {
        network.ports.pinned = network_config.at("pinned").to<decltype(network.ports.pinned)>();
    }

    if(network_config.count("shared")) {
        network.ports.shared = network_config.at("shared").to<decltype(network.ports.shared)>();
    }

    // Blackhole logging configuration
    logging = root.as_object().at("logging",  dynamic_t::empty_object).to<config_t::logging_t>();

    // Component configuration
    services = root.as_object().at("services", dynamic_t::empty_object).to<config_t::component_map_t>();
    storages = root.as_object().at("storages", dynamic_t::empty_object).to<config_t::component_map_t>();
    unicorns = root.as_object().at("unicorns", dynamic_t::empty_object).to<config_t::component_map_t>();

#ifdef COCAINE_ALLOW_RAFT
    create_raft_cluster = false;
#endif
}

int
config_t::versions() {
    return COCAINE_VERSION;
}

} //  namespace cocaine
