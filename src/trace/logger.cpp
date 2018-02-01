/*
    Copyright (c) 2016 Anton Matveenko <antmat@me.com>
    Copyright (c) 2016 Other contributors as noted in the AUTHORS file.

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

#include "cocaine/context/filter.hpp"
#include "cocaine/format.hpp"
#include "cocaine/trace/logger.hpp"

using namespace blackhole;

namespace cocaine { namespace logging {

trace_wrapper_t::trace_wrapper_t(std::unique_ptr<blackhole::logger_t> log):
    inner(std::move(log)),
    m_filter(new filter_t([](severity_t, attribute_pack&) { return true; }))
{
}

auto trace_wrapper_t::attributes() const noexcept -> blackhole::attributes_t {
    if(!trace_t::current().empty()) {
        return trace_t::current().formatted_attributes<blackhole::attributes_t>();
    }

    return blackhole::attributes_t();
}

auto trace_wrapper_t::filter(filter_t new_filter) -> void {
    std::shared_ptr<filter_t> new_filter_ptr(new filter_t(std::move(new_filter)));
    m_filter.apply([&](std::shared_ptr<filter_t>& filter_ptr){
        filter_ptr.swap(new_filter_ptr);
    });
}

auto trace_wrapper_t::log(severity_t severity, const message_t& message) -> void {
    attribute_pack pack;
    log(severity, message, pack);
}

auto trace_wrapper_t::log(severity_t severity, const message_t& message, attribute_pack& pack) -> void {
    log_impl(severity, message, pack);
}

auto trace_wrapper_t::log(severity_t severity, const lazy_message_t& message, attribute_pack& pack) -> void {
    log_impl(severity, message, pack);
}

auto trace_wrapper_t::manager() -> scope::manager_t& {
    return inner->manager();
}

template<class Message>
auto trace_wrapper_t::log_impl(blackhole::severity_t severity, const Message& message, blackhole::attribute_pack& pack) -> void {
    // This all strange looking code is written for performance,
    // cause profiling of vicodyn showed out that up to 30% of CPU is
    // consumed in trace_wrapper_t and unneeded trace attribute formatting and copying took the biggest part
    // of that 30%.
    attribute_list attr_list;
    const auto& trace = trace_t::current();
    if(!trace.empty()) {
        attr_list.reserve(5ul);
        attr_list.emplace_back("trace_id", "");
        attr_list.emplace_back("span_id", "");
        attr_list.emplace_back("parent_id", "");
        attr_list.emplace_back("rpc_name", trace.rpc_name());
        attr_list.emplace_back("trace_bit", trace.verbose());
        pack.push_back(attr_list);
    }
    auto filter = *(m_filter.synchronize());

    // We run filter with empty trace attributes for perf and only format them if filter passed.
    // We assume here that no one will check those attributes for exact match.
    if(filter->operator()(severity, pack)) {
        if(!trace.empty()) {
            auto trace_id = cocaine::format("{:016x}", trace.get_trace_id());
            auto span_id = cocaine::format("{:016x}", trace.get_id());
            auto parent_id = cocaine::format("{:016x}", trace.get_parent_id());

            attr_list[0].second = trace_id;
            attr_list[1].second = span_id;
            attr_list[2].second = parent_id;
            // Code duplication is here because vars should be in scope
            inner->log(severity, message, pack);
        } else {
            inner->log(severity, message, pack);
        }
    }
}

}} // namespace cocaine::logging
