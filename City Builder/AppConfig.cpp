#include "AppConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GLFW/glfw3.h>

namespace {
std::string Trim(const std::string& value) {
    std::string::size_type first = 0u;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    std::string::size_type last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1u])) != 0) {
        --last;
    }

    return value.substr(first, last - first);
}

std::string ToLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::string NormalizeKeyName(std::string value) {
    value = Trim(value);
    std::string normalized;
    std::size_t characterIndex = 0;
    for (; characterIndex < value.size(); ++characterIndex) {
        const unsigned char character = static_cast<unsigned char>(value[characterIndex]);
        if (std::isspace(character) != 0 || character == '-' || character == '_') {
            continue;
        }

        normalized.push_back(static_cast<char>(std::toupper(character)));
    }

    return normalized;
}

bool ParseBool(const std::string& text, bool& value) {
    const std::string normalized = ToLower(Trim(text));
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        value = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        value = false;
        return true;
    }
    return false;
}

bool ParseInt(const std::string& text, int& value) {
    char* parseEnd = 0;
    const long parsed = std::strtol(Trim(text).c_str(), &parseEnd, 10);
    if (parseEnd == 0 || *parseEnd != '\0') {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

bool ParseKeyCode(const std::string& text, int& keyCode) {
    const std::string normalized = NormalizeKeyName(text);
    if (normalized.empty()) {
        return false;
    }

    if (normalized.size() == 1u) {
        const char character = normalized[0];
        if ((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')) {
            keyCode = static_cast<int>(character);
            return true;
        }
    }

    if (normalized.size() >= 2u && normalized[0] == 'F') {
        int functionKey = 0;
        if (ParseInt(normalized.substr(1u), functionKey) && functionKey >= 1 && functionKey <= 25) {
            keyCode = GLFW_KEY_F1 + functionKey - 1;
            return true;
        }
    }

    // AppController compares these directly with GLFW callback keys, so avoid
    // inventing a separate app key enum until input grows beyond keyboard keys.
    static const std::pair<const char*, int> namedKeys[] = {
        {"SPACE", GLFW_KEY_SPACE},
        {"APOSTROPHE", GLFW_KEY_APOSTROPHE},
        {"QUOTE", GLFW_KEY_APOSTROPHE},
        {"COMMA", GLFW_KEY_COMMA},
        {"MINUS", GLFW_KEY_MINUS},
        {"DASH", GLFW_KEY_MINUS},
        {"PERIOD", GLFW_KEY_PERIOD},
        {"DOT", GLFW_KEY_PERIOD},
        {"SLASH", GLFW_KEY_SLASH},
        {"SEMICOLON", GLFW_KEY_SEMICOLON},
        {"EQUAL", GLFW_KEY_EQUAL},
        {"EQUALS", GLFW_KEY_EQUAL},
        {"LEFTBRACKET", GLFW_KEY_LEFT_BRACKET},
        {"RIGHTBRACKET", GLFW_KEY_RIGHT_BRACKET},
        {"BACKSLASH", GLFW_KEY_BACKSLASH},
        {"GRAVEACCENT", GLFW_KEY_GRAVE_ACCENT},
        {"BACKTICK", GLFW_KEY_GRAVE_ACCENT},
        {"ESCAPE", GLFW_KEY_ESCAPE},
        {"ESC", GLFW_KEY_ESCAPE},
        {"ENTER", GLFW_KEY_ENTER},
        {"RETURN", GLFW_KEY_ENTER},
        {"KPENTER", GLFW_KEY_KP_ENTER},
        {"KEYPADENTER", GLFW_KEY_KP_ENTER},
        {"TAB", GLFW_KEY_TAB},
        {"BACKSPACE", GLFW_KEY_BACKSPACE},
        {"INSERT", GLFW_KEY_INSERT},
        {"INS", GLFW_KEY_INSERT},
        {"DELETE", GLFW_KEY_DELETE},
        {"DEL", GLFW_KEY_DELETE},
        {"RIGHT", GLFW_KEY_RIGHT},
        {"LEFT", GLFW_KEY_LEFT},
        {"DOWN", GLFW_KEY_DOWN},
        {"UP", GLFW_KEY_UP},
        {"PAGEUP", GLFW_KEY_PAGE_UP},
        {"PAGEDOWN", GLFW_KEY_PAGE_DOWN},
        {"HOME", GLFW_KEY_HOME},
        {"END", GLFW_KEY_END},
        {"CAPSLOCK", GLFW_KEY_CAPS_LOCK},
        {"SCROLLLOCK", GLFW_KEY_SCROLL_LOCK},
        {"NUMLOCK", GLFW_KEY_NUM_LOCK},
        {"PRINTSCREEN", GLFW_KEY_PRINT_SCREEN},
        {"PAUSE", GLFW_KEY_PAUSE}
    };

    std::size_t keyIndex = 0;
    for (; keyIndex < sizeof(namedKeys) / sizeof(namedKeys[0]); ++keyIndex) {
        if (normalized == namedKeys[keyIndex].first) {
            keyCode = namedKeys[keyIndex].second;
            return true;
        }
    }

    return ParseInt(normalized, keyCode);
}

bool ParseDateFormat(const std::string& text, SimulationDateFormat& format) {
    std::string normalized = ToLower(Trim(text));
    normalized.erase(
        std::remove_if(
            normalized.begin(),
            normalized.end(),
            [](char character) {
                return character == '-' || character == '_' || character == '/' || std::isspace(static_cast<unsigned char>(character)) != 0;
            }),
        normalized.end());

    if (normalized == "yyyymmdd") {
        format = SimulationDateFormat::YearMonthDay;
        return true;
    }
    if (normalized == "mmddyyyy") {
        format = SimulationDateFormat::MonthDayYear;
        return true;
    }
    if (normalized == "ddmmyyyy") {
        format = SimulationDateFormat::DayMonthYear;
        return true;
    }
    return false;
}

void ApplyHotkey(HotkeyConfig& hotkeys, const std::string& key, const std::string& value) {
    int keyCode = 0;
    if (!ParseKeyCode(value, keyCode)) {
        return;
    }

    if (key == "save") {
        hotkeys.save = keyCode;
    } else if (key == "load") {
        hotkeys.load = keyCode;
    } else if (key == "exit_to_region") {
        hotkeys.exitToRegion = keyCode;
    } else if (key == "toggle_road_debug") {
        hotkeys.toggleRoadDebug = keyCode;
    } else if (key == "pan_right") {
        hotkeys.panRight = keyCode;
    } else if (key == "pan_left") {
        hotkeys.panLeft = keyCode;
    } else if (key == "pan_down") {
        hotkeys.panDown = keyCode;
    } else if (key == "pan_up") {
        hotkeys.panUp = keyCode;
    } else if (key == "pollution_brush") {
        hotkeys.pollutionBrush = keyCode;
    } else if (key == "place_smokestack") {
        hotkeys.placeSmokestack = keyCode;
    } else if (key == "place_park") {
        hotkeys.placePark = keyCode;
    } else if (key == "place_factory") {
        hotkeys.placeFactory = keyCode;
    } else if (key == "place_house") {
        hotkeys.placeHouse = keyCode;
    } else if (key == "road_street") {
        hotkeys.roadStreet = keyCode;
    } else if (key == "road_highway") {
        hotkeys.roadHighway = keyCode;
    } else if (key == "toggle_traffic_overlay") {
        hotkeys.toggleTrafficOverlay = keyCode;
    } else if (key == "add_park_module") {
        hotkeys.addParkModule = keyCode;
    } else if (key == "remove_module") {
        hotkeys.removeModule = keyCode;
    } else if (key == "bulldozer") {
        hotkeys.bulldozer = keyCode;
    } else if (key == "query") {
        hotkeys.query = keyCode;
    } else if (key == "rotate_counterclockwise") {
        hotkeys.rotateCounterClockwise = keyCode;
    } else if (key == "rotate_clockwise") {
        hotkeys.rotateClockwise = keyCode;
    } else if (key == "decrease_road_lanes") {
        hotkeys.decreaseRoadLanes = keyCode;
    } else if (key == "increase_road_lanes") {
        hotkeys.increaseRoadLanes = keyCode;
    } else if (key == "toggle_road_traffic_side") {
        hotkeys.toggleRoadTrafficSide = keyCode;
    } else if (key == "cycle_road_direction") {
        hotkeys.cycleRoadDirection = keyCode;
    }
}

std::string GetExecutableDirectory() {
    char modulePath[MAX_PATH];
    const DWORD pathLength = GetModuleFileNameA(0, modulePath, MAX_PATH);
    std::string fullPath(modulePath, modulePath + pathLength);
    const std::string::size_type separatorIndex = fullPath.find_last_of("\\/");
    if (separatorIndex == std::string::npos) {
        return ".";
    }

    return fullPath.substr(0, separatorIndex);
}
}

HotkeyConfig::HotkeyConfig()
    : save(GLFW_KEY_F1),
      load(GLFW_KEY_F2),
      exitToRegion(GLFW_KEY_F3),
      toggleRoadDebug(GLFW_KEY_F11),
      panRight(GLFW_KEY_RIGHT),
      panLeft(GLFW_KEY_LEFT),
      panDown(GLFW_KEY_DOWN),
      panUp(GLFW_KEY_UP),
      pollutionBrush('Q'),
      placeSmokestack('W'),
      placePark('E'),
      placeFactory('F'),
      placeHouse('G'),
      roadStreet('R'),
      roadHighway('H'),
      toggleTrafficOverlay('T'),
      addParkModule('M'),
      removeModule('Y'),
      bulldozer('B'),
      query('A'),
      rotateCounterClockwise(GLFW_KEY_COMMA),
      rotateClockwise(GLFW_KEY_PERIOD),
      decreaseRoadLanes(GLFW_KEY_LEFT_BRACKET),
      increaseRoadLanes(GLFW_KEY_RIGHT_BRACKET),
      toggleRoadTrafficSide('C'),
      cycleRoadDirection('O') {
}

WindowConfig::WindowConfig()
    : fullscreen(true),
      windowedWidth(2048),
      windowedHeight(2048) {
}

DebugConfig::DebugConfig()
    : printQueryValuesToConsole(false) {
}

AppConfig::AppConfig() {
}

bool AppConfig::loadFromFile(const std::string& filePath) {
    std::ifstream stream(filePath.c_str());
    if (!stream) {
        return false;
    }

    std::string section;
    std::string line;
    while (std::getline(stream, line)) {
        const std::string::size_type commentIndex = line.find_first_of("#;");
        if (commentIndex != std::string::npos) {
            line = line.substr(0u, commentIndex);
        }

        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = ToLower(Trim(line.substr(1u, line.size() - 2u)));
            continue;
        }

        const std::string::size_type equalsIndex = line.find('=');
        if (equalsIndex == std::string::npos) {
            continue;
        }

        const std::string key = ToLower(Trim(line.substr(0u, equalsIndex)));
        const std::string value = Trim(line.substr(equalsIndex + 1u));
        if (section == "window") {
            if (key == "fullscreen") {
                ParseBool(value, window.fullscreen);
            } else if (key == "windowed_width") {
                ParseInt(value, window.windowedWidth);
            } else if (key == "windowed_height") {
                ParseInt(value, window.windowedHeight);
            }
        } else if (section == "debug") {
            if (key == "print_query_values_to_console") {
                ParseBool(value, debug.printQueryValuesToConsole);
            }
        } else if (section == "date") {
            if (key == "format") {
                ParseDateFormat(value, dateSettings.format);
            }
        } else if (section == "hotkeys") {
            ApplyHotkey(hotkeys, key, value);
        }
    }

    // Clamp here rather than in Renderer so every user of WindowConfig sees the
    // same minimum usable dimensions.
    window.windowedWidth = std::max(640, window.windowedWidth);
    window.windowedHeight = std::max(480, window.windowedHeight);
    return true;
}

std::string DefaultAppConfigPath() {
    return GetExecutableDirectory() + "\\Data\\config.ini";
}
