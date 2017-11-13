#pragma once

namespace cocaine {

template <class Pool>
struct engine_dispatcher_t {
    using result_type = typename Pool::value_type;

    virtual
    auto
    dispatch(Pool& pool) -> result_type& = 0;
};

template <class Pool>
struct load_engine_dispatcher_t: public engine_dispatcher_t<Pool> {
    using result_type = typename Pool::value_type;

    auto
    dispatch(Pool& pool) -> result_type& override {
        auto comp = [](const result_type& lhs, const result_type& rhs) {
            return lhs->utilization() < rhs->utilization();
        };
        return **std::min_element(pool.begin(), pool.end(), comp);
    }
};

template <class Pool>
struct rr_engine_dispatcher_t: public engine_dispatcher_t {
    using result_type = typename Pool::value_type;
    std::atomic_int counter;

    rr_engine_dispatcher_t() : counter(0) {}

    auto
    dispatch(Pool& pool) -> result_type& override {
        return *pool[counter++ % pool.size()];
    }
};

template <class Pool>
struct bucket_random_dispatcher_t: public engine_dispatcher_t {
    using result_type = typename Pool::value_type;

    auto
    dispatch(Pool& pool) -> result_type& {
        static double max_value = 1.0;
        static double bucket_size = 0.02;
        size_t cur_bucket = max_value/bucket_size;
        std::vector<std::reference_wrapper<result_type>> units;
        units.reserve(pool.size());
        for(auto& value: pool) {
            size_t bucket = static_cast<size_t>(value->utilization()/bucket_size);
            if(bucket < cur_bucket) {
                cur_bucket = cur_bucket;
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
engine_dispatcher(const std::string& name) -> std::unique_ptr<engine_dispatcher_t> {
    if(name == "load") {
        return new load_engine_dispatcher_t();
    } else if (name == "rr") {
        return new rr_engine_dispatcher_t();
    } else if (name == "bucket_random") {
        return new bucket_random_dispatcher_t();
    }
    throw error_t("unknown engine dispatcher type {}", name);
}

} // namespace cocaine
