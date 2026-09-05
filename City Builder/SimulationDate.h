#pragma once

#include <cstdint>
#include <string>

#ifndef SIMULATION_START_YEAR
#define SIMULATION_START_YEAR 1900
#endif

#ifndef SIMULATION_START_MONTH
#define SIMULATION_START_MONTH 1
#endif

#ifndef SIMULATION_START_DAY
#define SIMULATION_START_DAY 1
#endif

struct SimulationDate {
    int year;
    int month;
    int day;

    SimulationDate()
        : year(SIMULATION_START_YEAR),
          month(SIMULATION_START_MONTH),
          day(SIMULATION_START_DAY) {
    }
};

enum class SimulationDateFormat {
    YearMonthDay,
    MonthDayYear,
    DayMonthYear
};

struct SimulationDateSettings {
    SimulationDateFormat format;

    SimulationDateSettings()
        : format(SimulationDateFormat::YearMonthDay) {
    }
};

bool SimulationIsLeapYear(int year);
SimulationDate CalculateSimulationDate(std::uint64_t tick);
std::string FormatSimulationDate(const SimulationDate& date, const SimulationDateSettings& settings);
std::string FormatSimulationDateForTick(std::uint64_t tick, const SimulationDateSettings& settings);
