#pragma once

#include "cocaine/dynamic.hpp"
#include "cocaine/errors.hpp"

namespace cocaine {

// distributor of elements of Pool
// Pool should provide value_type typedef and 'operator[](size_t)' call
// value_type should provide operator* and oprator->,
// resulting type should provide 'double utilization()' call with returned values in [0.0 1.0] range
template <class Pool>
struct distributor {
    using result_type = typename Pool::value_type;

    virtual
    auto
    next(Pool& pool) -> result_type& = 0;
};


// Distributes elements of the Pool according only to 'utilization()' of the elements.
// Simply returns the lowest value.
template <class Pool>
struct load_distributor: public distributor<Pool> {
    using result_type = typename Pool::value_type;

    load_distributor(const dynamic_t&) {}

    auto
    next(Pool& pool) -> result_type& override {
        auto comp = [](const result_type& lhs, const result_type& rhs) {
            return lhs->utilization() < rhs->utilization();
        };
        return *std::min_element(pool.begin(), pool.end(), comp);
    }
};

// Distributes elements of the Pool according to primitive round robin algorithm
// Always returns element with next index (in a circular manner)
template <class Pool>
struct rr_distributor: public distributor<Pool> {
    using result_type = typename Pool::value_type;
    std::atomic_int counter;

    rr_distributor(const dynamic_t&) : counter(0) {}

    auto
    next(Pool& pool) -> result_type& override {
        return pool[counter++ % pool.size()];
    }
};

// Split elements to several buckets according to utilization
// Chooses bucket with lowest value and returns random element from it
template <class Pool>
struct bucket_random_distributor: public distributor<Pool> {
    using result_type = typename Pool::value_type;

    static constexpr const double max_value = 1.0;
    double bucket_size;

    bucket_random_distributor(const dynamic_t& args) :
        bucket_size(args.as_object().at("bucket_size", 0.02).as_double())
    {}

    auto
    next(Pool& pool) -> result_type& override {
        size_t cur_bucket = max_value/bucket_size;
        std::vector<std::reference_wrapper<result_type>> units;
        units.reserve(pool.size());
        for(auto& value: pool) {
            size_t bucket = static_cast<size_t>(value->utilization()/bucket_size);
            if(bucket < cur_bucket) {
                cur_bucket = bucket;
                units.clear();
                units.push_back(std::ref(value));
            } else if(bucket == cur_bucket) {
                units.push_back(std::ref(value));
            }
        }
        return units[rand() % units.size()];
    }
};


// Creates new distributor by name.
template <class Pool>
auto
new_distributor(const std::string& name, const dynamic_t& args) -> std::unique_ptr<distributor<Pool>> {
    using ptr_t = std::unique_ptr<distributor<Pool>>;
    if(name == "load") {
        return ptr_t(new load_distributor<Pool>(args));
    } else if (name == "rr") {
        return ptr_t(new rr_distributor<Pool>(args));
    } else if (name == "bucket_random") {
        return ptr_t(new bucket_random_distributor<Pool>(args));
    }
    throw error_t("unknown engine dispatcher type {}", name);
}

} // namespace cocaine
