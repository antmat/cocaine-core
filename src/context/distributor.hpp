#pragma once

#include "cocaine/errors.hpp"

namespace cocaine {

template <class Pool>
struct distributor {
    using result_type = typename Pool::value_type;

    virtual
    auto
    next(Pool& pool) -> result_type& = 0;
};

template <class Pool>
struct load_distributor: public distributor<Pool> {
    using result_type = typename Pool::value_type;

    auto
    next(Pool& pool) -> result_type& override {
        auto comp = [](const result_type& lhs, const result_type& rhs) {
            return lhs->utilization() < rhs->utilization();
        };
        return *std::min_element(pool.begin(), pool.end(), comp);
    }
};

template <class Pool>
struct rr_distributor: public distributor<Pool> {
    using result_type = typename Pool::value_type;
    std::atomic_int counter;

    rr_distributor() : counter(0) {}

    auto
    next(Pool& pool) -> result_type& override {
        return pool[counter++ % pool.size()];
    }
};

template <class Pool>
struct bucket_random_distributor: public distributor<Pool> {
    using result_type = typename Pool::value_type;

    auto
    next(Pool& pool) -> result_type& override {
        static double max_value = 1.0;
        static double bucket_size = 0.02;
        size_t cur_bucket = max_value/bucket_size;
        std::vector<std::reference_wrapper<result_type>> units;
        units.reserve(pool.size());
        for(auto& value: pool) {
            size_t bucket = static_cast<size_t>(value->utilization()/bucket_size);
            if(bucket < cur_bucket) {
                cur_bucket = bucket;
                units.clear();
                units.push_back(std::ref(*value));
            } else if(bucket == cur_bucket) {
                units.push_back(std::ref(*value));
            }
        }
        return units[rand() % units.size()];
    }
};

template <class Pool>
auto
new_distributor(const std::string& name) -> std::unique_ptr<distributor<Pool>> {
    using ptr_t = std::unique_ptr<distributor<Pool>>;
    if(name == "load") {
        return ptr_t(new load_distributor<Pool>());
    } else if (name == "rr") {
        return ptr_t(new rr_distributor<Pool>());
    } else if (name == "bucket_random") {
        return ptr_t(new bucket_random_distributor<Pool>());
    }
    throw error_t("unknown engine dispatcher type {}", name);
}

} // namespace cocaine
