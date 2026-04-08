#include "Renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "ShaderProgram.h"

namespace {
class RendererCallbacks {
public:
    explicit RendererCallbacks(AppController& appController)
        : appController_(appController) {
    }

    static void CursorPositionCallback(GLFWwindow* window, double mouseX, double mouseY) {
        RendererCallbacks* callbacks = reinterpret_cast<RendererCallbacks*>(glfwGetWindowUserPointer(window));
        if (callbacks != 0) {
            callbacks->appController_.onCursorMoved(mouseX, mouseY);
        }
    }

    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        (void)mods;
        RendererCallbacks* callbacks = reinterpret_cast<RendererCallbacks*>(glfwGetWindowUserPointer(window));
        if (callbacks == 0) {
            return;
        }

        if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
            callbacks->appController_.onLeftMouseButtonPressed();
        }
    }

    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        (void)scancode;
        (void)mods;
        RendererCallbacks* callbacks = reinterpret_cast<RendererCallbacks*>(glfwGetWindowUserPointer(window));
        if (callbacks != 0) {
            callbacks->appController_.onKeyPressed(key, action);
        }
    }

    static void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
        (void)xOffset;
        RendererCallbacks* callbacks = reinterpret_cast<RendererCallbacks*>(glfwGetWindowUserPointer(window));
        if (callbacks != 0) {
            callbacks->appController_.onScroll(yOffset);
        }
    }

private:
    AppController& appController_;
};

std::string GetExecutableDirectory() {
    char modulePath[MAX_PATH];
    const DWORD pathLength = GetModuleFileNameA(0, modulePath, MAX_PATH);
    std::string fullPath(modulePath, modulePath + pathLength);
    const std::string::size_type lastSeparatorIndex = fullPath.find_last_of("\\/");
    if (lastSeparatorIndex == std::string::npos) {
        return ".";
    }

    return fullPath.substr(0, lastSeparatorIndex);
}

std::string BuildShaderPath() {
    return GetExecutableDirectory() + "\\Basic.shader";
}
}

Renderer::Renderer(SimulationRuntime& simulationRuntime, AppController& appController)
    : simulationRuntime_(simulationRuntime),
      appController_(appController) {
}

int Renderer::run() {
    if (glfwInit() == GLFW_FALSE) {
        std::cerr << "GLFW initialization failed." << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(2048, 2048, "Project Prime", 0, 0);
    if (window == 0) {
        glfwTerminate();
        std::cerr << "Window creation failed." << std::endl;
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW initialization failed." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    RendererCallbacks callbacks(appController_);
    glfwSetWindowUserPointer(window, &callbacks);
    glfwSetCursorPosCallback(window, &RendererCallbacks::CursorPositionCallback);
    glfwSetMouseButtonCallback(window, &RendererCallbacks::MouseButtonCallback);
    glfwSetKeyCallback(window, &RendererCallbacks::KeyCallback);
    glfwSetScrollCallback(window, &RendererCallbacks::ScrollCallback);

    const float positions[] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f,
        -0.5f, -0.5f
    };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    unsigned int vertexArrayId = 0;
    glGenVertexArrays(1, &vertexArrayId);
    glBindVertexArray(vertexArrayId);

    unsigned int vertexBufferId = 0;
    glGenBuffers(1, &vertexBufferId);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufferId);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<const void*>(sizeof(float) * 2));

    ShaderProgram shaderProgram;
    if (!shaderProgram.loadFromFile(BuildShaderPath())) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    shaderProgram.bind();

    const int maxVisibleTiles = 512;
    std::vector<GLsizei> drawCounts(maxVisibleTiles, 6);
    std::vector<GLint> drawStarts(maxVisibleTiles, 0);
    int rowIndex = 0;
    for (; rowIndex < maxVisibleTiles; ++rowIndex) {
        drawStarts[rowIndex] = rowIndex * 6;
    }

    std::vector<float> rowVertices(static_cast<std::size_t>(maxVisibleTiles) * 24u, 0.0f);
    std::vector<float> lotVertices(24u, 0.0f);

    int renderedFrames = 0;
    std::chrono::steady_clock::time_point counterStart = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);

        const ViewState viewState = appController_.viewState();
        const PublishedWorldSnapshot snapshot = simulationRuntime_.acquirePublishedSnapshot();

        if (snapshot.tiles != 0) {
            int tileScreenX = 0;
            for (; tileScreenX < viewState.visibleTiles; ++tileScreenX) {
                int tileScreenY = 0;
                for (; tileScreenY < viewState.visibleTiles; ++tileScreenY) {
                    const std::size_t vertexBase = static_cast<std::size_t>(tileScreenY) * 24u;
                    const float xOffset = (static_cast<float>(tileScreenX) / static_cast<float>(viewState.visibleTiles)) * 2.0f - 1.0f;
                    const float yOffset = (static_cast<float>(tileScreenY) / static_cast<float>(viewState.visibleTiles)) * 2.0f - 1.0f;
                    const float tileWidth = 1.0f / static_cast<float>(viewState.visibleTiles);
                    const float tileHeight = 1.0f / static_cast<float>(viewState.visibleTiles);

                    rowVertices[vertexBase + 0] = positions[0] * tileWidth * 2.0f + xOffset;
                    rowVertices[vertexBase + 1] = positions[1] * tileHeight * 2.0f + yOffset;
                    rowVertices[vertexBase + 4] = positions[2] * tileWidth * 2.0f + xOffset;
                    rowVertices[vertexBase + 5] = positions[3] * tileHeight * 2.0f + yOffset;
                    rowVertices[vertexBase + 8] = positions[4] * tileWidth * 2.0f + xOffset;
                    rowVertices[vertexBase + 9] = positions[5] * tileHeight * 2.0f + yOffset;
                    rowVertices[vertexBase + 12] = rowVertices[vertexBase + 8];
                    rowVertices[vertexBase + 13] = rowVertices[vertexBase + 9];
                    rowVertices[vertexBase + 16] = positions[8] * tileWidth * 2.0f + xOffset;
                    rowVertices[vertexBase + 17] = positions[9] * tileHeight * 2.0f + yOffset;
                    rowVertices[vertexBase + 20] = rowVertices[vertexBase + 0];
                    rowVertices[vertexBase + 21] = rowVertices[vertexBase + 1];

                    const int worldTileX = viewState.cameraX + tileScreenX;
                    const int worldTileY = viewState.cameraY + tileScreenY;
                    const Tile& tile = (*snapshot.tiles)[static_cast<std::size_t>(worldTileY) * static_cast<std::size_t>(snapshot.width) + static_cast<std::size_t>(worldTileX)];

                    const float red = 0.5f + (static_cast<float>(tile.airPollution) / 1280000.0f);
                    const float green = 0.5f + (static_cast<float>(tile.landValue) / 1280000.0f);

                    rowVertices[vertexBase + 2] = red;
                    rowVertices[vertexBase + 3] = green;
                    rowVertices[vertexBase + 6] = red;
                    rowVertices[vertexBase + 7] = green;
                    rowVertices[vertexBase + 10] = red;
                    rowVertices[vertexBase + 11] = green;
                    rowVertices[vertexBase + 14] = red;
                    rowVertices[vertexBase + 15] = green;
                    rowVertices[vertexBase + 18] = red;
                    rowVertices[vertexBase + 19] = green;
                    rowVertices[vertexBase + 22] = red;
                    rowVertices[vertexBase + 23] = green;
                }

                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(rowVertices.size() * sizeof(float)), &rowVertices[0], GL_DYNAMIC_DRAW);
                glMultiDrawArrays(GL_TRIANGLES, &drawStarts[0], &drawCounts[0], viewState.visibleTiles);
            }

            std::size_t lotIndex = 0;
            for (; lotIndex < snapshot.lots.size(); ++lotIndex) {
                const LotRenderInstance& lot = snapshot.lots[lotIndex];
                const int relativeX = lot.originX - viewState.cameraX;
                const int relativeY = lot.originY - viewState.cameraY;
                if (relativeX + lot.width < 0 || relativeY + lot.height < 0) {
                    continue;
                }

                if (relativeX >= viewState.visibleTiles || relativeY >= viewState.visibleTiles) {
                    continue;
                }

                const float xOffset = (static_cast<float>(relativeX) / static_cast<float>(viewState.visibleTiles)) * 2.0f - 1.0f;
                const float yOffset = (static_cast<float>(relativeY) / static_cast<float>(viewState.visibleTiles)) * 2.0f - 1.0f;
                const float tileWidth = static_cast<float>(lot.width) / static_cast<float>(viewState.visibleTiles);
                const float tileHeight = static_cast<float>(lot.height) / static_cast<float>(viewState.visibleTiles);

                lotVertices[0] = positions[0] * tileWidth * 2.0f + xOffset;
                lotVertices[1] = positions[1] * tileHeight * 2.0f + yOffset;
                lotVertices[4] = positions[2] * tileWidth * 2.0f + xOffset;
                lotVertices[5] = positions[3] * tileHeight * 2.0f + yOffset;
                lotVertices[8] = positions[4] * tileWidth * 2.0f + xOffset;
                lotVertices[9] = positions[5] * tileHeight * 2.0f + yOffset;
                lotVertices[12] = lotVertices[8];
                lotVertices[13] = lotVertices[9];
                lotVertices[16] = positions[8] * tileWidth * 2.0f + xOffset;
                lotVertices[17] = positions[9] * tileHeight * 2.0f + yOffset;
                lotVertices[20] = lotVertices[0];
                lotVertices[21] = lotVertices[1];

                int colorIndex = 2;
                for (; colorIndex < 24; colorIndex += 4) {
                    lotVertices[colorIndex] = 0.0f;
                    lotVertices[colorIndex + 1] = 0.0f;
                }

                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lotVertices.size() * sizeof(float)), &lotVertices[0], GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }

        simulationRuntime_.releasePublishedSnapshot(snapshot);
        glfwSwapBuffers(window);
        glfwPollEvents();

        ++renderedFrames;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - counterStart).count() >= 1) {
            std::cout << "FPS " << renderedFrames << " Updates " << simulationRuntime_.updatesPerSecond() << std::endl;
            renderedFrames = 0;
            counterStart = now;
        }
    }

    glDeleteBuffers(1, &vertexBufferId);
    glDeleteVertexArrays(1, &vertexArrayId);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
