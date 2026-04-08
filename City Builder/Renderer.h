#pragma once

#include "AppController.h"
#include "SimulationRuntime.h"

class Renderer {
public:
    Renderer(SimulationRuntime& simulationRuntime, AppController& appController);
    int run();

private:
    SimulationRuntime& simulationRuntime_;
    AppController& appController_;
};
