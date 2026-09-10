#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "RendererAlgorithms.h"
#include "ShaderProgram.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace {
int checks = 0;
void expect(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
void projection(GLuint program, float extent) {
    const float matrix[] = {2/extent,0,0,0, 0,0,-1,0, 0,2/extent,0,0, -1,-1,0,1};
    glUniformMatrix4fv(glGetUniformLocation(program, "uViewProjection"), 1, GL_FALSE, matrix);
}
void quad(GLuint buffer, float leftHeight, float rightHeight) {
    const float vertices[] = {0,leftHeight,0, 1,rightHeight,0, 1,rightHeight,1,
                              0,leftHeight,0, 1,rightHeight,1, 0,leftHeight,1};
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> result(512 * 512 * 4);
    glReadPixels(0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}
}

int main(int argc, char** argv) {
    GLFWwindow* window = nullptr;
    try {
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(512, 512, "Renderer regression checks", nullptr, nullptr);
        if (!window) throw std::runtime_error("Hidden OpenGL 4.6 context unavailable");
        glfwMakeContextCurrent(window);
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) throw std::runtime_error("GLEW initialization failed");
        while (glGetError() != GL_NO_ERROR) {}
        {
            ShaderProgram shader;
            shader.loadFromFile(argc > 1 ? argv[1] : "City Builder/Basic.shader");
            shader.bind();
            const auto program = shader.programId();
            // Different sampler types cannot share a texture unit, even in unused branches.
            glUniform1i(glGetUniformLocation(program, "uLotMaterials"), 7);
            GLuint arrayTexture, vao, buffer;
            glGenTextures(1, &arrayTexture);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D_ARRAY, arrayTexture);
            const unsigned char white[] = {255,255,255,255};
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glGenVertexArrays(1, &vao); glBindVertexArray(vao);
            glGenBuffers(1, &buffer); glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
            glVertexAttrib3f(4, 1,1,1); glVertexAttrib3f(5, 0,1,0); glVertexAttrib4f(6, 0,0,0,1);
            glViewport(0,0,512,512);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
            glClearColor(0,0,0,1);
            const auto drawRibbon = [&](const std::vector<CommuteRouteSegment>& segments) {
                const auto spans = BuildRouteArrowInstances(segments);
                for (const auto& a : spans) {
                    glVertexAttrib4f(1, a.originX,a.originZ,a.sizeX,a.sizeZ);
                    glVertexAttrib4f(2, a.directionX,a.directionZ,a.lift,a.alpha);
                    glVertexAttrib4f(3, a.colorR,a.colorG,a.colorB,0);
                    glVertexAttrib4f(7, a.startPhase,a.endPhase,a.startNormalX,a.startNormalZ);
                    glVertexAttrib2f(8, a.endNormalX,a.endNormalZ);
                    quad(buffer, 0,0);
                }
                return spans;
            };

            for (int mode : {1, 9}) {
                projection(program, 1);
                glUniform1i(glGetUniformLocation(program, "uRenderMode"), mode);
                glUniform1f(glGetUniformLocation(program, "uLotTintStrength"), 1);
                glVertexAttrib4f(1, 0,0,1,1); glVertexAttrib4f(2, 1,1,1,1); glVertexAttrib4f(3, 0,0,0,0);
                for (bool oldOrder : {true, false}) {
                    glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    const auto opaque = [&] {
                        glDepthMask(GL_TRUE);
                        glUniform1f(glGetUniformLocation(program, "uLotAlphaScale"), 1);
                        glUniform3f(glGetUniformLocation(program, "uLotTintColor"), 0,0,1);
                        quad(buffer, .5f, .5f);
                    };
                    const auto ghost = [&] {
                        glDepthMask(GL_FALSE);
                        glUniform1f(glGetUniformLocation(program, "uLotAlphaScale"), .42f);
                        glUniform3f(glGetUniformLocation(program, "uLotTintColor"), 1,0,0);
                        quad(buffer, .25f, .75f);
                        glDepthMask(GL_TRUE);
                    };
                    if (oldOrder) { ghost(); opaque(); } else { opaque(); ghost(); }
                    const auto image = pixels();
                    const auto back = (256*512+128)*4, front = (256*512+384)*4;
                    expect(image[back] < 5 && image[back+2] > 245, "Opaque geometry occludes ghost fragments behind it");
                    expect(oldOrder ? image[front] < 5 : image[front] > 90 && image[front+2] > 130,
                           "Opaque-first draw restores correctly blended ghost fragments in front, for boxes and generated meshes");
                }
            }
            projection(program, 64);
            glUniform1i(glGetUniformLocation(program, "uRenderMode"), 4);
            for (int dx=-1; dx<=1; ++dx) for (int dy=-1; dy<=1; ++dy) {
                if (!dx && !dy) continue;
                CommuteRouteSegment s;
                s.startTileX = dx < 0 ? 61 : dx > 0 ? 2 : 32;
                s.startTileY = dy < 0 ? 61 : dy > 0 ? 2 : 32;
                s.endTileX = s.startTileX + dx*59;
                s.endTileY = s.startTileY + dy*59;
                const auto a = BuildRouteArrowInstances({s})[0];
                glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glVertexAttrib4f(1, a.originX,a.originZ,a.sizeX,a.sizeZ);
                glVertexAttrib4f(2, a.directionX,a.directionZ,a.lift,a.alpha);
                glVertexAttrib4f(3, a.colorR,a.colorG,a.colorB,0);
                glVertexAttrib4f(7, a.startPhase,a.endPhase,a.startNormalX,a.startNormalZ);
                glVertexAttrib2f(8, a.endNormalX,a.endNormalZ);
                quad(buffer, 0,0);
                const auto image = pixels();
                for (int i=1; i<100; ++i) {
                    const float t=i/100.0f;
                    const int x=static_cast<int>((a.originX + a.directionX*a.sizeX*t)*8);
                    const int y=static_cast<int>((a.originZ + a.directionZ*a.sizeX*t)*8);
                    int green=0;
                    for (int yy=y-1; yy<=y+1; ++yy) for (int xx=x-1; xx<=x+1; ++xx)
                        green=std::max(green, static_cast<int>(image[(yy*512+xx)*4+1]));
                    expect(green > 25, "GPU route ribbon covers its complete centerline, including long and diagonal paths");
                }
            }
            // A source clock spans a bend and a speed change: three broad arrows,
            // with the second road twice as slow as the first.
            auto clock = std::make_shared<const std::vector<float>>(std::vector<float>{0, 10, 30});
            CommuteRouteSegment first, second;
            first.startTileX = first.startTileY = 4;
            first.endTileX = 24; first.endTileY = 4;
            first.elapsedSeconds = clock.get(); first.timingEnd = 1;
            second.startTileX = 24; second.startTileY = 4;
            second.endTileX = 24; second.endTileY = 24;
            second.elapsedSeconds = clock.get(); second.timingBegin = 1; second.timingEnd = 2;
            glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            const auto timedSpans = drawRibbon({first, second});
            const auto timedPixels = pixels();
            for (const auto& span : timedSpans) {
                const float phase = (span.startPhase + span.endPhase) * 0.5f;
                if (phase - std::floor(phase) < 0.04f) continue; // intentional arrow separator
                const int x = static_cast<int>((span.originX + span.directionX * span.sizeX * 0.5f) * 8);
                const int y = static_cast<int>((span.originZ + span.directionZ * span.sizeX * 0.5f) * 8);
                int green = 0;
                for (int yy = y - 1; yy <= y + 1; ++yy) for (int xx = x - 1; xx <= x + 1; ++xx)
                    green = std::max(green, static_cast<int>(timedPixels[(yy * 512 + xx) * 4 + 1]));
                expect(green > 25, "Timed arrow blocks remain visible through rounded joins");
            }
            if (argc > 2) {
                std::ifstream sample(argv[2]);
                std::vector<float> xs, zs;
                auto times = std::make_shared<std::vector<float>>();
                float x, z, seconds;
                while (sample >> x >> z >> seconds) { xs.push_back(x); zs.push_back(z); times->push_back(seconds); }
                expect(xs.size() > 1, "Saved-city GPU sample contains a route");
                const float minX = *std::min_element(xs.begin(), xs.end()), minZ = *std::min_element(zs.begin(), zs.end());
                const float extent = std::max(*std::max_element(xs.begin(), xs.end()) - minX,
                                              *std::max_element(zs.begin(), zs.end()) - minZ) + 8;
                std::vector<CommuteRouteSegment> saved;
                for (std::size_t i = 1; i < xs.size(); ++i) {
                    CommuteRouteSegment s;
                    s.startTileX = static_cast<int>(std::round(xs[i - 1] - minX)) + 4;
                    s.startTileY = static_cast<int>(std::round(zs[i - 1] - minZ)) + 4;
                    s.endTileX = static_cast<int>(std::round(xs[i] - minX)) + 4;
                    s.endTileY = static_cast<int>(std::round(zs[i] - minZ)) + 4;
                    s.elapsedSeconds = times.get(); s.timingBegin = i - 1; s.timingEnd = i;
                    saved.push_back(s);
                }
                projection(program, extent);
                glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                const auto spans = drawRibbon(saved);
                expect(!spans.empty() && std::abs(spans.back().endPhase - times->back() / kRouteArrowSeconds) < .001f,
                       "Real-save GPU geometry preserves total travel time");
                const auto capture = pixels();
                expect(std::count_if(capture.begin(), capture.end(), [](unsigned char c) { return c > 25 && c < 245; }) > 100,
                       "Real-save timed ribbon produces visible GPU output");
                if (argc > 3) {
                    std::ofstream output(argv[3], std::ios::binary);
                    output << "P6\n512 512\n255\n";
                    for (int y = 511; y >= 0; --y) for (int x = 0; x < 512; ++x)
                        output.write(reinterpret_cast<const char*>(&capture[(y * 512 + x) * 4]), 3);
                }
                std::cout << "Saved-city ribbon: " << times->back() << " seconds, " << spans.size() << " spans.\n";
            }
            expect(glGetError() == GL_NO_ERROR, "Graphics checks produce no OpenGL errors");
            glDeleteBuffers(1, &buffer); glDeleteVertexArrays(1, &vao); glDeleteTextures(1, &arrayTexture);
        }
        glfwDestroyWindow(window); glfwTerminate();
        std::cout << "Renderer graphics tests passed: " << checks << " checks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
