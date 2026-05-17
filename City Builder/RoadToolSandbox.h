#pragma once

#include "TransportCostMap.h"
#include "TransportNetwork.h"

#include <cstdint>
#include <string>
#include <vector>

struct RoadToolSandboxBounds {
    int minX;
    int minY;
    int maxX;
    int maxY;

    RoadToolSandboxBounds();
};

struct RoadToolSandboxExpectedGrid {
    std::string kind;
    RoadToolSandboxBounds bounds;
    std::string grid;
};

struct RoadToolSandboxAction {
    bool isBulldoze;
    Int2 startTile;
    Int2 cornerTile;
    Int2 endTile;
    int laneCount;
    RoadDirectionMode directionMode;

    RoadToolSandboxAction();
};

struct RoadToolSandboxFixture {
    std::string name;
    std::string description;
    int width;
    int height;
    std::vector<RoadToolSandboxAction> actions;
    std::vector<RoadToolSandboxExpectedGrid> expectedGrids;

    RoadToolSandboxFixture();
};

struct RoadToolSandboxFailure {
    std::string message;
};

struct RoadToolSandboxRunResult {
    std::string fixturePath;
    std::string fixtureName;
    std::string fixtureDescription;
    std::string rotationName;
    bool hasName;
    bool hasActions;
    bool hasExpectedGrids;
    std::vector<RoadToolSandboxFailure> actionFailures;
    std::vector<RoadToolSandboxFailure> expectationFailures;

    RoadToolSandboxRunResult();
};

struct RoadToolSandbox {
    TransportNetwork network;
    std::vector<int> lotOccupancy;
    std::vector<std::string> snapshots;

    RoadToolSandbox(int width, int height);

    bool dragStreet(const Int2& startTile, const Int2& cornerTile, const Int2& endTile, int laneCount = 1, RoadDirectionMode directionMode = RoadDirectionMode::TwoWay);
    bool bulldoze(int tileX, int tileY);
    std::string log() const;
};

std::uint8_t CostCellDirectionMask(const TransportCostCell& cell);
std::string DirectionGridForMode(const TransportNetwork& network, TransportMode mode, int minX, int minY, int maxX, int maxY);
std::string SandboxSnapshot(const std::string& action, const TransportNetwork& network);
std::string NetworkTopologySignature(const TransportNetwork& network);

std::vector<std::string> SandboxFixturePaths();
RoadToolSandboxFixture LoadSandboxFixture(const std::string& path);
RoadToolSandboxRunResult RunRoadToolSandboxFixture(const std::string& path, int clockwiseQuarterTurns);
