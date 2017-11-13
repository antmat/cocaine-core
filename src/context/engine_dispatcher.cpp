#include "engine_dispatcher.hpp"

namespace cocaine {

struct load_engine_dispatcher_t: public engine_dispatcher_t {
    auto
    dispatch(const std::vector<std::unique_ptr<execution_unit_t>>& pool) -> execution_unit_t& {
        typedef std::unique_ptr<execution_unit_t> unit_t;
        auto comp = [](const unit_t& lhs, const unit_t& rhs) {
            return lhs->utilization() < rhs->utilization();
        };
        return **std::min_element(pool.begin(), pool.end(), comp);
    }
};


struct rr_engine_dispatcher_t: public engine_dispatcher_t {
    std::atomic_int counter;

    rr_engine_dispatcher_t() : counter(0) {}

    auto
    dispatch(const std::vector<std::unique_ptr<execution_unit_t>>& pool) -> execution_unit_t& {
        return *pool[counter++ % pool.size()];
    }
};

struct bucket_random_dispatcher_t: public engine_dispatcher_t {
    auto
    dispatch(const std::vector<std::unique_ptr<execution_unit_t>>& pool) -> execution_unit_t& {
        static double max_value = 1.0;
        static double bucket_size = 0.02;
        size_t cur_bucket = max_value/bucket_size;
        std::vector<std::reference_wrapper<execution_unit_t>> units;
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
    }
};

} // namespace cocaine
