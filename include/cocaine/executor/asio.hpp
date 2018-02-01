#pragma once

#include "cocaine/api/executor.hpp"

#include <asio/io_service.hpp>

#include <boost/optional/optional.hpp>
#include <boost/thread/thread.hpp>

#define OWNING_ASIO_INIT ::cocaine::executor::owning_asio_t::format_origin(__FILE__, __LINE__)

namespace cocaine {
namespace executor {

// Runs asio::io_service in a spawned thread and posts all callbacks provided to spawn in it.
class owning_asio_t: public api::executor_t {
public:
    enum class stop_policy_t {
        graceful,
        force
    };

    // Choose wheteher to wait wor async operations completion in dtor or not
    __attribute__((deprecated("use ctor with loop name, e.g. owning_asio_t(OWNING_ASIO_INIT)")))
    owning_asio_t(stop_policy_t stop_policy = stop_policy_t::graceful);

    owning_asio_t(const std::string& name, stop_policy_t stop_policy = stop_policy_t::graceful);

    ~owning_asio_t();

    auto
    spawn(work_t work) -> void override;

    auto
    asio() -> asio::io_service&;

    static auto
    format_origin(std::string filename, size_t line) -> std::string;

private:
    auto run(const std::string& name) -> void;

    asio::io_service io_loop;
    boost::optional<asio::io_service::work> work;
    boost::thread thread;
    stop_policy_t stop_policy;
};

// Posts callbacks passed to spawn to externally provided io_service
class borrowing_asio_t: public api::executor_t {
public:
    borrowing_asio_t(asio::io_service& io_loop);

    auto
    spawn(work_t work) -> void override;

private:
    asio::io_service& io_loop;
};

} // namespace executor
} // namespace cocaine
