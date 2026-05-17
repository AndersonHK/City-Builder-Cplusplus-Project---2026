#pragma once

#include <string>

#include "SimulationDate.h"

// These values are GLFW key codes because RendererCallbacks forwards raw GLFW
// keyboard input to AppController. Keep this in sync with ApplyHotkey and
// Data/config.ini whenever a new configurable command is added.
struct HotkeyConfig {
    int save;
    int load;
    int escapeMenu;
    int exitToRegion;
    int toggleRoadDebug;
    int panRight;
    int panLeft;
    int panDown;
    int panUp;
    int pollutionBrush;
    int placeSmokestack;
    int placePark;
    int placeFactory;
    int placeHouse;
    int roadStreet;
    int roadRoad;
    int roadAvenue;
    int roadHighway;
    int toggleTrafficOverlay;
    int toggleLandValueOverlay;
    int addParkModule;
    int removeModule;
    int bulldozer;
    int query;
    int rotateCounterClockwise;
    int rotateClockwise;
    int decreaseRoadLanes;
    int increaseRoadLanes;
    int toggleRoadTrafficSide;
    int cycleRoadDirection;

    HotkeyConfig();
};

struct WindowConfig {
    bool fullscreen;
    int windowedWidth;
    int windowedHeight;

    WindowConfig();
};

struct DebugConfig {
    // Gates only verbose query-value console dumps. General status logging still
    // prints until there is a broader logging-level policy.
    bool printQueryValuesToConsole;

    DebugConfig();
};

struct AppConfig {
    WindowConfig window;
    HotkeyConfig hotkeys;
    DebugConfig debug;
    SimulationDateSettings dateSettings;

    AppConfig();
    // Tolerant startup parser: unknown or invalid entries leave compiled
    // defaults intact so a bad local INI cannot prevent the game from booting.
    bool loadFromFile(const std::string& filePath);
};

// Runtime config is resolved next to the executable, not the source tree, so
// post-build Data copying and user-edited output configs behave the same.
std::string DefaultAppConfigPath();
