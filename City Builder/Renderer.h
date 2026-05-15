#pragma once

#include "AppController.h"
#include "GameSession.h"

class Renderer {
public:
    Renderer(GameSession& gameSession, AppController& appController);
    int run();

private:
    GameSession& gameSession_;
    AppController& appController_;
};
