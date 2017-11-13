#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <../src/context/engine_dispatcher.hpp>

namespace cocaine {
namespace {

struct engine_mock_t {
    double u;

    engine_mock_t(double u) : u(u) {}

    auto
    utilization() -> double {
        return u;
    }
};

TEST(context, bucket_random_dispatcher) {
    auto dispatcher = engine_dispatcher<int>("bucket_random");
    std::vector<double> values {
        0.005, 0.002, 0.001, 0.15
    };
    std::vector<engine_mock_t> sample(values.begin(), values.end());
    std::map<double, size_t> counter;
    for(size_t i = 0; i < 3000; i++) {
        counter[dispatcher->dispatch(sample)]++;
    }
    ASSERT_NEAR(counter[values[0]], 1000, 50);
    ASSERT_NEAR(counter[values[1]], 1000, 50);
    ASSERT_NEAR(counter[values[2]], 1000, 50);
    ASSERT_EQ(counter[values[3]], 0);
}

} // namespace
} // namespace cocaine
