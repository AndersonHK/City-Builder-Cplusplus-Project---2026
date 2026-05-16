#include "SimulationDate.h"

#include <algorithm>
#include <sstream>

namespace {
int DaysInMonth(int year, int month) {
    static const int kDaysByMonth[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month == 2 && SimulationIsLeapYear(year)) {
        return 29;
    }

    const int clampedMonth = std::max(1, std::min(month, 12));
    return kDaysByMonth[clampedMonth - 1];
}

void AppendPadded2(std::ostringstream& stream, int value) {
    if (value < 10) {
        stream << '0';
    }

    stream << value;
}
}

bool SimulationIsLeapYear(int year) {
    if (year % 400 == 0) {
        return true;
    }

    if (year % 100 == 0) {
        return false;
    }

    return year % 4 == 0;
}

SimulationDate CalculateSimulationDate(std::uint64_t tick) {
    SimulationDate date;
    date.year = SIMULATION_START_YEAR;
    date.month = SIMULATION_START_MONTH;
    date.day = SIMULATION_START_DAY;

    while (tick > 0u) {
        const int daysLeftInMonth = DaysInMonth(date.year, date.month) - date.day;
        if (tick <= static_cast<std::uint64_t>(daysLeftInMonth)) {
            date.day += static_cast<int>(tick);
            return date;
        }

        tick -= static_cast<std::uint64_t>(daysLeftInMonth + 1);
        date.day = 1;
        ++date.month;
        if (date.month > 12) {
            date.month = 1;
            ++date.year;
        }
    }

    return date;
}

std::string FormatSimulationDate(const SimulationDate& date, const SimulationDateSettings& settings) {
    std::ostringstream stream;
    switch (settings.format) {
        case SimulationDateFormat::MonthDayYear:
            AppendPadded2(stream, date.month);
            stream << '/';
            AppendPadded2(stream, date.day);
            stream << '/' << date.year;
            break;

        case SimulationDateFormat::DayMonthYear:
            AppendPadded2(stream, date.day);
            stream << '/';
            AppendPadded2(stream, date.month);
            stream << '/' << date.year;
            break;

        case SimulationDateFormat::YearMonthDay:
        default:
            stream << date.year << '/';
            AppendPadded2(stream, date.month);
            stream << '/';
            AppendPadded2(stream, date.day);
            break;
    }

    return stream.str();
}

std::string FormatSimulationDateForTick(std::uint64_t tick, const SimulationDateSettings& settings) {
    return FormatSimulationDate(CalculateSimulationDate(tick), settings);
}
