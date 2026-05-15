#include <cstdlib>
#include <exception>
#include <iostream>

#include "AppController.h"
#include "CrashLogger.h"
#include "GameSession.h"
#include "Renderer.h"

// Boots the simulation, controller, and renderer for the desktop prototype.
int main() {
    InitializeCrashLogger("City Builder");
    CrashScope crashScope("main");

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
        LogCrashAndShowWindow("main", error);
    } catch (...) {
        LogCrashAndShowWindow("main", "unknown exception.");
    }

    return EXIT_FAILURE;
}
