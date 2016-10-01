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

#include "cocaine/detail/storage/files.hpp"

#include "cocaine/context.hpp"
#include "cocaine/dynamic.hpp"
#include "cocaine/logging.hpp"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/optional/optional.hpp>

#include <blackhole/logger.hpp>

#include <numeric>

using namespace cocaine::storage;

namespace fs = boost::filesystem;

using blackhole::attribute_list;

files_t::files_t(context_t& context, const std::string& name, const dynamic_t& args):
    category_type(context, name, args),
    m_log(context.log(name)),
    m_parent_path(args.as_object().at("path").as_string()),
    io_loop(),
    io_work(asio::io_service::work(io_loop)),
    thread([&](){
        io_loop.run();
    })
{ }

files_t::~files_t() {
    io_work = boost::none;
    thread.join();
}

void
files_t::read(const std::string& collection, const std::string& key, callback<std::string> cb) {
    io_loop.post([=]() {
        try {
            cb(make_ready_future(read_sync(collection, key)));
        } catch (...) {
            cb(make_exceptional_future<std::string>());
        }
    });
}

void
files_t::write(const std::string& collection,
               const std::string& key,
               const std::string& blob,
               const std::vector<std::string>& tags,
               callback<void> cb)
{
    io_loop.post([=]() {
        try {
            write_sync(collection, key, blob, tags);
            cb(make_ready_future());
        } catch (...) {
            cb(make_exceptional_future<void>());
        }
    });
}

void
files_t::remove(const std::string& collection, const std::string& key, callback<void> cb) {
    io_loop.post([=]() {
        try {
            remove_sync(collection, key);
            cb(make_ready_future());
        } catch (...) {
            cb(make_exceptional_future<void>());
        }
    });
}

void
files_t::find(const std::string& collection, const std::vector<std::string>& tags, callback<std::vector<std::string>> cb) {
    io_loop.post([=]() {
        try {
            cb(make_ready_future(find_sync(collection, tags)));
        } catch (...) {
            cb(make_exceptional_future<std::vector<std::string>>());
        }
    });
}

std::string
files_t::read_sync(const std::string& collection, const std::string& key) {
    const fs::path file_path(m_parent_path / collection / key);

    if(!fs::exists(file_path)) {
        throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory), file_path.string());
    }

    COCAINE_LOG_DEBUG(m_log, "reading object '{}'", key, attribute_list({{"collection", collection}}));

    fs::ifstream stream(file_path, fs::ifstream::in | fs::ifstream::binary);

    if(!stream) {
        throw std::system_error(std::make_error_code(std::errc::permission_denied), file_path.string());
    }

    return std::string{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void
files_t::write_sync(const std::string& collection,
                    const std::string& key,
                    const std::string& blob,
                    const std::vector<std::string>& tags) {
    const fs::path store_path(m_parent_path / collection);
    const auto store_status = fs::status(store_path);

    if (!fs::exists(store_status)) {
        COCAINE_LOG_INFO(m_log, "creating collection", {{ "collection", collection }});
        fs::create_directories(store_path);
    } else if (!fs::is_directory(store_status)) {
        throw std::system_error(std::make_error_code(std::errc::not_a_directory), store_path.string());
    }

    const fs::path file_path(store_path / key);

    COCAINE_LOG_DEBUG(m_log, "writing object '{}'", key, attribute_list({{"collection", collection}}));

    fs::ofstream stream(file_path, fs::ofstream::out | fs::ofstream::trunc | fs::ofstream::binary);

    if (!stream) {
        throw std::system_error(std::make_error_code(std::errc::permission_denied), file_path.string());
    }

    for (auto it = tags.begin(); it != tags.end(); ++it) {
        const auto tag_path = store_path / *it;
        const auto tag_status = fs::status(tag_path);

        if (!fs::exists(tag_status)) {
            fs::create_directory(tag_path);
        } else if (!fs::is_directory(tag_status)) {
            throw std::system_error(std::make_error_code(std::errc::not_a_directory), tag_path.string());
        }

        if (fs::is_symlink(tag_path / key)) {
            continue;
        }

        fs::create_symlink(file_path, tag_path / key);
    }

    stream.write(blob.c_str(), blob.size());
    stream.close();
}

void
files_t::remove_sync(const std::string& collection, const std::string& key) {
    const auto file_path(m_parent_path / collection / key);

    if (!fs::exists(file_path)) {
        return;
    }

    COCAINE_LOG_DEBUG(m_log, "removing object '{}'", key, attribute_list({{"collection", collection}}));

    fs::remove(file_path);
}

namespace {

struct intersect {
    template<class T>
    T
    operator()(const T& accumulator, T& keys) const {
        T result;

        auto builder = std::back_inserter(result);

        std::sort(keys.begin(), keys.end());
        std::set_intersection(accumulator.begin(), accumulator.end(), keys.begin(), keys.end(), builder);

        return result;
    }
};

} // namespace

std::vector<std::string>
files_t::find_sync(const std::string& collection, const std::vector<std::string>& tags) {
    const fs::path store_path(m_parent_path / collection);

    if (!fs::exists(store_path) || tags.empty()) {
        return {};
    }

    std::vector<std::vector<std::string>> result;

    for (auto tag = tags.begin(); tag != tags.end(); ++tag) {
        auto tagged = result.insert(result.end(), std::vector<std::string>());

        if (!fs::exists(store_path / *tag)) {
            // If one of the tags doesn't exist, the intersection is evidently empty.
            return {};
        }

        fs::directory_iterator it(store_path / *tag), end;

        while (it != end) {
            const std::string object = it->path().filename().native();
            if (!fs::exists(*it)) {
                COCAINE_LOG_DEBUG(m_log, "purging object '{}' from tag '{}'", object, *tag);

                // Remove the symlink if the object was removed.
                fs::remove(*it++);

                continue;
            }

            tagged->push_back(object);

            ++it;
        }
    }

    std::vector<std::string> initial = std::move(result.back());

    // NOTE: Pop the initial accumulator value from the result queue, so that it
    // won't be intersected with itself later.
    result.pop_back();

    // NOTE: Sort the initial accumulator value here once, because it will always
    // be kept sorted inside the functor by std::set_intersection().
    std::sort(initial.begin(), initial.end());

    return std::accumulate(result.begin(), result.end(), initial, intersect());
}