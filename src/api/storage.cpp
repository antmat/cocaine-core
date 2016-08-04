#include "cocaine/api/storage.hpp"

#include <cassert>

namespace cocaine {
namespace api {
namespace ph = std::placeholders;

std::string
storage_t::read_sync(const std::string& collection, const std::string& key) {
    return wrap_sync<std::string>(std::bind(&storage_t::read, this, ph::_1, ph::_2, ph::_3), collection, key);
}

void
storage_t::write_sync(const std::string& collection,
                      const std::string& key,
                      const std::string& blob,
                      const std::vector<std::string>& tags) {
    return wrap_sync<void>(std::bind(&storage_t::write, this, ph::_1, ph::_2, ph::_3, ph::_4, ph::_5), collection, key, blob, tags);
}

void
storage_t::remove_sync(const std::string& collection, const std::string& key) {
    return wrap_sync<void>(std::bind(&storage_t::remove, this, ph::_1, ph::_2, ph::_3), collection, key);
}

std::vector<std::string>
storage_t::find_sync(const std::string& collection, const std::vector<std::string>& tags) {
    return wrap_sync<std::vector<std::string>>(std::bind(&storage_t::find, this, ph::_1, ph::_2, ph::_3), collection, tags);
}

} // namesapce api
} // namespace cocaine