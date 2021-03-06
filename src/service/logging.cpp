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

#include "cocaine/detail/service/logging.hpp"

#include "cocaine/context.hpp"
#include "cocaine/logging.hpp"

#include "cocaine/traits/attributes.hpp"
#include "cocaine/traits/enum.hpp"
#include "cocaine/traits/vector.hpp"

using namespace blackhole;

using namespace cocaine;
using namespace cocaine::logging;
using namespace cocaine::service;

namespace ph = std::placeholders;

logging_t::logging_t(context_t& context, asio::io_service& asio, const std::string& name, const dynamic_t& args):
    category_type(context, asio, name, args),
    dispatch<io::log_tag>(name)
{
    const auto backend = args.as_object().at("backend", "core").as_string();

    try {
        if(backend == "core") {
            wrapper = context.log(format("%s/core", name));
        } else {
            logger.reset(new logger_t(repository_t::instance().create<logger_t>(
                backend,
                context.config.logging.loggers.at(backend).verbosity
            )));
            wrapper.reset(new log_t(*logger, {{ "source",  format("%s/%s", name, backend)}}));
        }
    } catch(const std::out_of_range&) {
        throw cocaine::error_t("logger '%s' is not configured", backend);
    }

    on<io::log::emit>(std::bind(&logging_t::on_emit, this, ph::_1, ph::_2, ph::_3, ph::_4));
    on<io::log::verbosity>([&]() {
        return wrapper->log().verbosity();
    });
}

auto
logging_t::prototype() const -> const io::basic_dispatch_t& {
    return *this;
}

void
logging_t::on_emit(logging::priorities level,
                   const std::string& source,
                   const std::string& message,
                   blackhole::attribute::set_t attributes)
{
    if(auto record = wrapper->open_record(level, std::move(attributes))) {
        record.insert(cocaine::logging::keyword::source() = std::move(source));
        record.message(std::move(message));

        wrapper->push(std::move(record));
    }
}
