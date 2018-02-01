#include "cocaine/executor/asio.hpp"
#include "cocaine/format.hpp"

#if defined(__linux__)
#include <sys/prctl.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace cocaine {
namespace executor {

owning_asio_t::owning_asio_t(stop_policy_t policy):
    io_loop(),
    work(asio::io_service::work(io_loop)),
    thread(&owning_asio_t::run, this, "unknown"),
    stop_policy(policy)
{}

owning_asio_t::owning_asio_t(const std::string& name, stop_policy_t policy):
    io_loop(),
    work(asio::io_service::work(io_loop)),
    thread(&owning_asio_t::run, this, name),
    stop_policy(policy)
{}

owning_asio_t::~owning_asio_t() {
    work.reset();
    if(stop_policy == stop_policy_t::force) {
        io_loop.stop();
    }
    thread.join();
}

auto
owning_asio_t::spawn(work_t work) -> void {
    io_loop.post(std::move(work));
}

auto
owning_asio_t::asio() -> asio::io_service& {
    return io_loop;
}

auto
owning_asio_t::run(const std::string& name) -> void {
    // This is used to capture name on the stack to be shown in gdb for debugging.
    // setting thread name is not enough as
    // 1) it is limited to 16 symbols and
    // 2) this information is stored in procfs and is lost after crash.
    [=](std::string){
        io_loop.run();
    }(name);

}

auto
owning_asio_t::format_origin(std::string filename, size_t line) -> std::string {
    auto it = filename.find("/src/");
    if (it != std::string::npos) {
        filename = filename.substr(it);
    } else {
        it = filename.find("/include/");
        if (it != std::string::npos) {
            filename = filename.substr(it);
        }
    }
    return cocaine::format("{}:{}", filename, line);
}

borrowing_asio_t::borrowing_asio_t(asio::io_service& _io_loop) :
    io_loop(_io_loop)
{}

auto
borrowing_asio_t::spawn(work_t work) -> void {
    io_loop.post(std::move(work));
}

} // namespace executor
} // namespace cocaine
