#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <../src/context/distributor.hpp>

namespace cocaine {
namespace {


struct engine_mock_t {
    double u;

    engine_mock_t(double u) : u(u) {}

    auto
    utilization() const -> double {
        return u;
    }

    auto operator->() const -> const engine_mock_t* {
        return this;
    }

    auto operator* () const -> const engine_mock_t& {
        return *this;
    }

    auto operator* () -> engine_mock_t& {
        return *this;
    }
};

TEST(context, bucket_random_dispatcher) {
    auto d = new_distributor<std::vector<engine_mock_t>>("bucket_random", dynamic_t::object_t());
    std::vector<double> values {
        0.005, 0.002, 0.001, 0.15
    };
    std::vector<engine_mock_t> sample(values.begin(), values.end());
    std::map<double, size_t> counter;
    for(size_t i = 0; i < 3000; i++) {
        counter[d->next(sample)->utilization()]++;
    }
    ASSERT_NEAR(counter[values[0]], 1000, 50);
    ASSERT_NEAR(counter[values[1]], 1000, 50);
    ASSERT_NEAR(counter[values[2]], 1000, 50);
    ASSERT_EQ(counter[values[3]], 0);

    // One value with lowest value
    values[0] = 0.1;
    values[1] = 0.21;
    values[2] = 0.22;
    values[3] = 0.23;
    sample = std::vector<engine_mock_t>(values.begin(), values.end());
    counter.clear();
    for(size_t i = 0; i < 3000; i++) {
        counter[d->next(sample)->utilization()]++;
    }
    ASSERT_EQ(counter[values[0]], 3000);

    // Extend bucket size, use the same sample
    counter.clear();
    dynamic_t::object_t args;
    args["bucket_size"] = 0.3;
    d = new_distributor<std::vector<engine_mock_t>>("bucket_random", args);
    for(size_t i = 0; i < 3000; i++) {
        counter[d->next(sample)->utilization()]++;
    }
    EXPECT_NEAR(counter[values[0]], 750, 50);
    EXPECT_NEAR(counter[values[1]], 750, 50);
    EXPECT_NEAR(counter[values[2]], 750, 50);
    EXPECT_NEAR(counter[values[3]], 750, 50);
}

} // namespace
} // namespace cocaine
