#include "shared/graphics/backend/gpu_timing.h"

#include <cmath>

#include "test_util.h"

int main() {
    CHECK(std::abs(gpu_timestamp_ms(100, 1100, 64, 1000) - 1.0) < 0.000001);
    CHECK(std::abs(gpu_timestamp_ms(250, 10, 8, 100000) - 1.6) < 0.000001);
    CHECK(std::abs(gpu_timestamp_ms(UINT64_MAX - 9, 10, 64, 100000) - 2.0) < 0.000001);
    CHECK(gpu_timestamp_ms(100, 100, 64, 1) == 0);
    CHECK(gpu_timestamp_ms(0, 100, 0, 1) == 0);
    CHECK(gpu_timestamp_ms(0, 100, 65, 1) == 0);
    CHECK(gpu_timestamp_ms(0, 100, 64, 0) == 0);
    return test::finish("GPU timestamp checks");
}
