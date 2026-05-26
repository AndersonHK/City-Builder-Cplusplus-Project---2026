#pragma once

#include <cstdint>

class SimulationTime {
public:
    static constexpr std::uint64_t ticksPerDay() {
        return 4u;
    }

    static constexpr std::uint64_t daysToTicks(std::uint64_t days) {
        return days * ticksPerDay();
    }

    static constexpr std::uint64_t tickToDay(std::uint64_t tick) {
        return tick / ticksPerDay();
    }
};
