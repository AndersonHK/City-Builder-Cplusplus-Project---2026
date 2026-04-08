#include <cstdlib>
#include <exception>
#include <iostream>

#include "AppController.h"
#include "Renderer.h"
#include "SimulationRuntime.h"

int main() {
    try {
        RuntimeOptions runtimeOptions;
        runtimeOptions.fastForward = true;
        runtimeOptions.detectL2CacheSize = true;
        runtimeOptions.manualL2BytesPerLogicalThread = 0;
        runtimeOptions.usableL2Fraction = 0.75;

        SimulationRuntime simulationRuntime(runtimeOptions);
        simulationRuntime.start();

        AppController appController(simulationRuntime);
        Renderer renderer(simulationRuntime, appController);
        const int rendererExitCode = renderer.run();

        simulationRuntime.stop();
        return rendererExitCode;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << std::endl;
    } catch (...) {
        std::cerr << "Fatal error: unknown exception." << std::endl;
    }

    return EXIT_FAILURE;
}
