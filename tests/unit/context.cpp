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
    auto d = new_distributor<std::vector<engine_mock_t>>("bucket_random");
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
}

} // namespace
} // namespace cocaine
