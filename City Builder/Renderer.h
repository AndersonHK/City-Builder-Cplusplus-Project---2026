#pragma once

#include "AppConfig.h"
#include "AppController.h"
#include "GameSession.h"

class Renderer {
public:
    Renderer(GameSession& gameSession, AppController& appController, const AppConfig& appConfig);
    int run();

private:
    GameSession& gameSession_;
    AppController& appController_;
    const AppConfig& appConfig_;
};
