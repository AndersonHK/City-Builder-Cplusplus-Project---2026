#include <cstdlib>
#include <exception>
#include <iostream>

#include "AppController.h"
#include "GameSession.h"
#include "Renderer.h"

// Boots the simulation, controller, and renderer for the desktop prototype.
int main() {
    try {
        RuntimeOptions runtimeOptions;
        runtimeOptions.fastForward = true;
        runtimeOptions.detectL2CacheSize = true;
        runtimeOptions.manualL2BytesPerLogicalThread = 0;
        runtimeOptions.usableL2Fraction = 0.75;

        GameSession gameSession(runtimeOptions);
        gameSession.loadOrCreateRegion();

        AppController appController(gameSession);
        Renderer renderer(gameSession, appController);
        const int rendererExitCode = renderer.run();

        gameSession.shutdown();
        return rendererExitCode;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << std::endl;
    } catch (...) {
        std::cerr << "Fatal error: unknown exception." << std::endl;
    }

    return EXIT_FAILURE;
}
