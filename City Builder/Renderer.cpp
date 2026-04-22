#include "Renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "ShaderProgram.h"

namespace {
const float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x;
    float y;
    float z;

    Vec3()
        : x(0.0f),
          y(0.0f),
          z(0.0f) {
    }

    Vec3(float xValue, float yValue, float zValue)
        : x(xValue),
          y(yValue),
          z(zValue) {
    }
};

struct Vec4 {
    float x;
    float y;
    float z;
    float w;

    Vec4()
        : x(0.0f),
          y(0.0f),
          z(0.0f),
          w(0.0f) {
    }

    Vec4(float xValue, float yValue, float zValue, float wValue)
        : x(xValue),
          y(yValue),
          z(zValue),
          w(wValue) {
    }
};

struct Mat4 {
    float data[16];

    Mat4() {
        int index = 0;
        for (; index < 16; ++index) {
            data[index] = 0.0f;
        }
    }

    static Mat4 Identity() {
        Mat4 matrix;
        matrix.data[0] = 1.0f;
        matrix.data[5] = 1.0f;
        matrix.data[10] = 1.0f;
        matrix.data[15] = 1.0f;
        return matrix;
    }
};

struct Plane {
    float a;
    float b;
    float c;
    float d;
};

struct Aabb {
    Vec3 minimum;
    Vec3 maximum;
};

struct Frustum {
    Plane planes[6];
};

struct CameraState {
    Vec3 position;
    Vec3 target;
    Mat4 view;
    Mat4 projection;
    Mat4 viewProjection;
    Mat4 inverseViewProjection;
    Frustum frustum;
};

struct TileInstanceData {
    float originX;
    float originZ;
    float tileU;
    float tileV;
    float lift;
};

struct LotInstanceData {
    float originX;
    float originZ;
    float sizeX;
    float sizeZ;
    float height;
    float colorR;
    float colorG;
    float colorB;
};

struct TileChunkRenderCache {
    ChunkRect chunkRect;
    Aabb worldBounds;
    GLuint vertexArrayId;
    GLuint instanceBufferId;
    std::vector<TileInstanceData> instances;
    std::uint64_t lastUploadedRevision;
    GLsizei instanceCount;

    TileChunkRenderCache()
        : vertexArrayId(0),
          instanceBufferId(0),
          lastUploadedRevision(0),
          instanceCount(0) {
    }
};

struct RendererFrameMetrics {
    long long cullMicros;
    long long uploadMicros;
    long long drawMicros;
    int visibleChunkCount;
    int totalChunkCount;

    RendererFrameMetrics()
        : cullMicros(0),
          uploadMicros(0),
          drawMicros(0),
          visibleChunkCount(0),
          totalChunkCount(0) {
    }
};

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

float DegreesToRadians(float degrees) {
    return degrees * (kPi / 180.0f);
}

Vec3 operator+(const Vec3& left, const Vec3& right) {
    return Vec3(left.x + right.x, left.y + right.y, left.z + right.z);
}

Vec3 operator-(const Vec3& left, const Vec3& right) {
    return Vec3(left.x - right.x, left.y - right.y, left.z - right.z);
}

Vec3 operator*(const Vec3& vector, float scalar) {
    return Vec3(vector.x * scalar, vector.y * scalar, vector.z * scalar);
}

float Dot(const Vec3& left, const Vec3& right) {
    return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
}

Vec3 Cross(const Vec3& left, const Vec3& right) {
    return Vec3(
        (left.y * right.z) - (left.z * right.y),
        (left.z * right.x) - (left.x * right.z),
        (left.x * right.y) - (left.y * right.x));
}

float Length(const Vec3& vector) {
    return std::sqrt(Dot(vector, vector));
}

Vec3 Normalize(const Vec3& vector) {
    const float vectorLength = Length(vector);
    if (vectorLength <= std::numeric_limits<float>::epsilon()) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }

    return vector * (1.0f / vectorLength);
}

Mat4 Multiply(const Mat4& left, const Mat4& right) {
    Mat4 result;
    int column = 0;
    for (; column < 4; ++column) {
        int row = 0;
        for (; row < 4; ++row) {
            result.data[column * 4 + row] =
                (left.data[0 * 4 + row] * right.data[column * 4 + 0]) +
                (left.data[1 * 4 + row] * right.data[column * 4 + 1]) +
                (left.data[2 * 4 + row] * right.data[column * 4 + 2]) +
                (left.data[3 * 4 + row] * right.data[column * 4 + 3]);
        }
    }

    return result;
}

Vec4 Multiply(const Mat4& matrix, const Vec4& vector) {
    return Vec4(
        matrix.data[0] * vector.x + matrix.data[4] * vector.y + matrix.data[8] * vector.z + matrix.data[12] * vector.w,
        matrix.data[1] * vector.x + matrix.data[5] * vector.y + matrix.data[9] * vector.z + matrix.data[13] * vector.w,
        matrix.data[2] * vector.x + matrix.data[6] * vector.y + matrix.data[10] * vector.z + matrix.data[14] * vector.w,
        matrix.data[3] * vector.x + matrix.data[7] * vector.y + matrix.data[11] * vector.z + matrix.data[15] * vector.w);
}

Mat4 Perspective(float verticalFieldOfViewRadians, float aspectRatio, float nearPlane, float farPlane) {
    Mat4 matrix;
    const float tanHalfFov = std::tan(verticalFieldOfViewRadians * 0.5f);
    matrix.data[0] = 1.0f / (aspectRatio * tanHalfFov);
    matrix.data[5] = 1.0f / tanHalfFov;
    matrix.data[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    matrix.data[11] = -1.0f;
    matrix.data[14] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
    return matrix;
}

Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 forward = Normalize(target - eye);
    const Vec3 right = Normalize(Cross(forward, up));
    const Vec3 cameraUp = Cross(right, forward);

    Mat4 matrix = Mat4::Identity();
    matrix.data[0] = right.x;
    matrix.data[1] = cameraUp.x;
    matrix.data[2] = -forward.x;
    matrix.data[4] = right.y;
    matrix.data[5] = cameraUp.y;
    matrix.data[6] = -forward.y;
    matrix.data[8] = right.z;
    matrix.data[9] = cameraUp.z;
    matrix.data[10] = -forward.z;
    matrix.data[12] = -Dot(right, eye);
    matrix.data[13] = -Dot(cameraUp, eye);
    matrix.data[14] = Dot(forward, eye);
    return matrix;
}

Mat4 Inverse(const Mat4& matrix) {
    Mat4 inverse;

    inverse.data[0] = matrix.data[5] * matrix.data[10] * matrix.data[15] -
        matrix.data[5] * matrix.data[11] * matrix.data[14] -
        matrix.data[9] * matrix.data[6] * matrix.data[15] +
        matrix.data[9] * matrix.data[7] * matrix.data[14] +
        matrix.data[13] * matrix.data[6] * matrix.data[11] -
        matrix.data[13] * matrix.data[7] * matrix.data[10];

    inverse.data[4] = -matrix.data[4] * matrix.data[10] * matrix.data[15] +
        matrix.data[4] * matrix.data[11] * matrix.data[14] +
        matrix.data[8] * matrix.data[6] * matrix.data[15] -
        matrix.data[8] * matrix.data[7] * matrix.data[14] -
        matrix.data[12] * matrix.data[6] * matrix.data[11] +
        matrix.data[12] * matrix.data[7] * matrix.data[10];

    inverse.data[8] = matrix.data[4] * matrix.data[9] * matrix.data[15] -
        matrix.data[4] * matrix.data[11] * matrix.data[13] -
        matrix.data[8] * matrix.data[5] * matrix.data[15] +
        matrix.data[8] * matrix.data[7] * matrix.data[13] +
        matrix.data[12] * matrix.data[5] * matrix.data[11] -
        matrix.data[12] * matrix.data[7] * matrix.data[9];

    inverse.data[12] = -matrix.data[4] * matrix.data[9] * matrix.data[14] +
        matrix.data[4] * matrix.data[10] * matrix.data[13] +
        matrix.data[8] * matrix.data[5] * matrix.data[14] -
        matrix.data[8] * matrix.data[6] * matrix.data[13] -
        matrix.data[12] * matrix.data[5] * matrix.data[10] +
        matrix.data[12] * matrix.data[6] * matrix.data[9];

    inverse.data[1] = -matrix.data[1] * matrix.data[10] * matrix.data[15] +
        matrix.data[1] * matrix.data[11] * matrix.data[14] +
        matrix.data[9] * matrix.data[2] * matrix.data[15] -
        matrix.data[9] * matrix.data[3] * matrix.data[14] -
        matrix.data[13] * matrix.data[2] * matrix.data[11] +
        matrix.data[13] * matrix.data[3] * matrix.data[10];

    inverse.data[5] = matrix.data[0] * matrix.data[10] * matrix.data[15] -
        matrix.data[0] * matrix.data[11] * matrix.data[14] -
        matrix.data[8] * matrix.data[2] * matrix.data[15] +
        matrix.data[8] * matrix.data[3] * matrix.data[14] +
        matrix.data[12] * matrix.data[2] * matrix.data[11] -
        matrix.data[12] * matrix.data[3] * matrix.data[10];

    inverse.data[9] = -matrix.data[0] * matrix.data[9] * matrix.data[15] +
        matrix.data[0] * matrix.data[11] * matrix.data[13] +
        matrix.data[8] * matrix.data[1] * matrix.data[15] -
        matrix.data[8] * matrix.data[3] * matrix.data[13] -
        matrix.data[12] * matrix.data[1] * matrix.data[11] +
        matrix.data[12] * matrix.data[3] * matrix.data[9];

    inverse.data[13] = matrix.data[0] * matrix.data[9] * matrix.data[14] -
        matrix.data[0] * matrix.data[10] * matrix.data[13] -
        matrix.data[8] * matrix.data[1] * matrix.data[14] +
        matrix.data[8] * matrix.data[2] * matrix.data[13] +
        matrix.data[12] * matrix.data[1] * matrix.data[10] -
        matrix.data[12] * matrix.data[2] * matrix.data[9];

    inverse.data[2] = matrix.data[1] * matrix.data[6] * matrix.data[15] -
        matrix.data[1] * matrix.data[7] * matrix.data[14] -
        matrix.data[5] * matrix.data[2] * matrix.data[15] +
        matrix.data[5] * matrix.data[3] * matrix.data[14] +
        matrix.data[13] * matrix.data[2] * matrix.data[7] -
        matrix.data[13] * matrix.data[3] * matrix.data[6];

    inverse.data[6] = -matrix.data[0] * matrix.data[6] * matrix.data[15] +
        matrix.data[0] * matrix.data[7] * matrix.data[14] +
        matrix.data[4] * matrix.data[2] * matrix.data[15] -
        matrix.data[4] * matrix.data[3] * matrix.data[14] -
        matrix.data[12] * matrix.data[2] * matrix.data[7] +
        matrix.data[12] * matrix.data[3] * matrix.data[6];

    inverse.data[10] = matrix.data[0] * matrix.data[5] * matrix.data[15] -
        matrix.data[0] * matrix.data[7] * matrix.data[13] -
        matrix.data[4] * matrix.data[1] * matrix.data[15] +
        matrix.data[4] * matrix.data[3] * matrix.data[13] +
        matrix.data[12] * matrix.data[1] * matrix.data[7] -
        matrix.data[12] * matrix.data[3] * matrix.data[5];

    inverse.data[14] = -matrix.data[0] * matrix.data[5] * matrix.data[14] +
        matrix.data[0] * matrix.data[6] * matrix.data[13] +
        matrix.data[4] * matrix.data[1] * matrix.data[14] -
        matrix.data[4] * matrix.data[2] * matrix.data[13] -
        matrix.data[12] * matrix.data[1] * matrix.data[6] +
        matrix.data[12] * matrix.data[2] * matrix.data[5];

    inverse.data[3] = -matrix.data[1] * matrix.data[6] * matrix.data[11] +
        matrix.data[1] * matrix.data[7] * matrix.data[10] +
        matrix.data[5] * matrix.data[2] * matrix.data[11] -
        matrix.data[5] * matrix.data[3] * matrix.data[10] -
        matrix.data[9] * matrix.data[2] * matrix.data[7] +
        matrix.data[9] * matrix.data[3] * matrix.data[6];

    inverse.data[7] = matrix.data[0] * matrix.data[6] * matrix.data[11] -
        matrix.data[0] * matrix.data[7] * matrix.data[10] -
        matrix.data[4] * matrix.data[2] * matrix.data[11] +
        matrix.data[4] * matrix.data[3] * matrix.data[10] +
        matrix.data[8] * matrix.data[2] * matrix.data[7] -
        matrix.data[8] * matrix.data[3] * matrix.data[6];

    inverse.data[11] = -matrix.data[0] * matrix.data[5] * matrix.data[11] +
        matrix.data[0] * matrix.data[7] * matrix.data[9] +
        matrix.data[4] * matrix.data[1] * matrix.data[11] -
        matrix.data[4] * matrix.data[3] * matrix.data[9] -
        matrix.data[8] * matrix.data[1] * matrix.data[7] +
        matrix.data[8] * matrix.data[3] * matrix.data[5];

    inverse.data[15] = matrix.data[0] * matrix.data[5] * matrix.data[10] -
        matrix.data[0] * matrix.data[6] * matrix.data[9] -
        matrix.data[4] * matrix.data[1] * matrix.data[10] +
        matrix.data[4] * matrix.data[2] * matrix.data[9] +
        matrix.data[8] * matrix.data[1] * matrix.data[6] -
        matrix.data[8] * matrix.data[2] * matrix.data[5];

    float determinant = matrix.data[0] * inverse.data[0] + matrix.data[1] * inverse.data[4] + matrix.data[2] * inverse.data[8] + matrix.data[3] * inverse.data[12];
    if (std::fabs(determinant) <= std::numeric_limits<float>::epsilon()) {
        return Mat4::Identity();
    }

    determinant = 1.0f / determinant;
    int index = 0;
    for (; index < 16; ++index) {
        inverse.data[index] *= determinant;
    }

    return inverse;
}

Plane NormalizePlane(const Plane& plane) {
    const float magnitude = std::sqrt((plane.a * plane.a) + (plane.b * plane.b) + (plane.c * plane.c));
    if (magnitude <= std::numeric_limits<float>::epsilon()) {
        return plane;
    }

    Plane normalizedPlane = plane;
    normalizedPlane.a /= magnitude;
    normalizedPlane.b /= magnitude;
    normalizedPlane.c /= magnitude;
    normalizedPlane.d /= magnitude;
    return normalizedPlane;
}

Frustum ExtractFrustum(const Mat4& viewProjection) {
    Frustum frustum;

    const float row0x = viewProjection.data[0];
    const float row0y = viewProjection.data[4];
    const float row0z = viewProjection.data[8];
    const float row0w = viewProjection.data[12];
    const float row1x = viewProjection.data[1];
    const float row1y = viewProjection.data[5];
    const float row1z = viewProjection.data[9];
    const float row1w = viewProjection.data[13];
    const float row2x = viewProjection.data[2];
    const float row2y = viewProjection.data[6];
    const float row2z = viewProjection.data[10];
    const float row2w = viewProjection.data[14];
    const float row3x = viewProjection.data[3];
    const float row3y = viewProjection.data[7];
    const float row3z = viewProjection.data[11];
    const float row3w = viewProjection.data[15];

    frustum.planes[0] = NormalizePlane(Plane{row3x + row0x, row3y + row0y, row3z + row0z, row3w + row0w});
    frustum.planes[1] = NormalizePlane(Plane{row3x - row0x, row3y - row0y, row3z - row0z, row3w - row0w});
    frustum.planes[2] = NormalizePlane(Plane{row3x + row1x, row3y + row1y, row3z + row1z, row3w + row1w});
    frustum.planes[3] = NormalizePlane(Plane{row3x - row1x, row3y - row1y, row3z - row1z, row3w - row1w});
    frustum.planes[4] = NormalizePlane(Plane{row3x + row2x, row3y + row2y, row3z + row2z, row3w + row2w});
    frustum.planes[5] = NormalizePlane(Plane{row3x - row2x, row3y - row2y, row3z - row2z, row3w - row2w});
    return frustum;
}

bool IntersectsFrustum(const Frustum& frustum, const Aabb& bounds) {
    int planeIndex = 0;
    for (; planeIndex < 6; ++planeIndex) {
        const Plane& plane = frustum.planes[planeIndex];
        const Vec3 positiveVertex(
            plane.a >= 0.0f ? bounds.maximum.x : bounds.minimum.x,
            plane.b >= 0.0f ? bounds.maximum.y : bounds.minimum.y,
            plane.c >= 0.0f ? bounds.maximum.z : bounds.minimum.z);

        const float distance = plane.a * positiveVertex.x + plane.b * positiveVertex.y + plane.c * positiveVertex.z + plane.d;
        if (distance < 0.0f) {
            return false;
        }
    }

    return true;
}

float Clamp01(float value) {
    return std::max(0.0f, std::min(value, 1.0f));
}

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

void SetupInstanceAttribute(GLuint attributeIndex, GLint componentCount, GLsizei stride, std::size_t offsetBytes) {
    glEnableVertexAttribArray(attributeIndex);
    glVertexAttribPointer(attributeIndex, componentCount, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetBytes));
    glVertexAttribDivisor(attributeIndex, 1);
}

void ConfigureTileChunkVertexArray(GLuint vertexArrayId, GLuint tileVertexBufferId, GLuint instanceBufferId) {
    glBindVertexArray(vertexArrayId);

    glBindBuffer(GL_ARRAY_BUFFER, tileVertexBufferId);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceBufferId);
    SetupInstanceAttribute(1, 4, sizeof(TileInstanceData), 0);
    SetupInstanceAttribute(2, 1, sizeof(TileInstanceData), sizeof(float) * 4);

    glBindVertexArray(0);
}

void ConfigureLotVertexArray(GLuint vertexArrayId, GLuint boxVertexBufferId, GLuint instanceBufferId) {
    glBindVertexArray(vertexArrayId);

    glBindBuffer(GL_ARRAY_BUFFER, boxVertexBufferId);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceBufferId);
    SetupInstanceAttribute(1, 4, sizeof(LotInstanceData), 0);
    SetupInstanceAttribute(2, 4, sizeof(LotInstanceData), sizeof(float) * 4);

    glBindVertexArray(0);
}

Aabb BuildChunkBounds(const ChunkRect& chunkRect) {
    Aabb bounds;
    bounds.minimum = Vec3(static_cast<float>(chunkRect.startX), 0.0f, static_cast<float>(chunkRect.startY));
    bounds.maximum = Vec3(
        static_cast<float>(chunkRect.startX + chunkRect.width),
        0.12f,
        static_cast<float>(chunkRect.startY + chunkRect.height));
    return bounds;
}

std::vector<TileInstanceData> BuildTileChunkInstances(const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect) {
    std::vector<TileInstanceData> instances;
    instances.reserve(static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height));

    if (snapshot.tiles == 0 || snapshot.lotOccupancy == 0) {
        return instances;
    }

    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const Tile& tile = (*snapshot.tiles)[static_cast<std::size_t>(tileY) * static_cast<std::size_t>(snapshot.width) + static_cast<std::size_t>(tileX)];

            TileInstanceData instance;
            instance.originX = static_cast<float>(tileX);
            instance.originZ = static_cast<float>(tileY);
            instance.tileU = (static_cast<float>(tileX) + 0.5f) / static_cast<float>(snapshot.width);
            instance.tileV = (static_cast<float>(tileY) + 0.5f) / static_cast<float>(snapshot.height);
            const int lotId = (*snapshot.lotOccupancy)[static_cast<std::size_t>(tileY) * static_cast<std::size_t>(snapshot.width) + static_cast<std::size_t>(tileX)];
            instance.lift = lotId < 0 ? 0.0f : 0.04f;
            instances.push_back(instance);
        }
    }

    return instances;
}

std::vector<LotInstanceData> BuildLotInstances(const std::vector<LotRenderInstance>& lots) {
    std::vector<LotInstanceData> instances;
    instances.reserve(lots.size());

    std::size_t lotIndex = 0;
    for (; lotIndex < lots.size(); ++lotIndex) {
        const LotRenderInstance& lot = lots[lotIndex];

        LotInstanceData instance;
        instance.originX = static_cast<float>(lot.originX);
        instance.originZ = static_cast<float>(lot.originY);
        instance.sizeX = static_cast<float>(lot.width);
        instance.sizeZ = static_cast<float>(lot.height);
        instance.height = lot.renderHeight;
        instance.colorR = lot.colorR;
        instance.colorG = lot.colorG;
        instance.colorB = lot.colorB;
        instances.push_back(instance);
    }

    return instances;
}

CameraState BuildCameraState(const ViewState& viewState) {
    CameraState cameraState;

    const float aspectRatio = static_cast<float>(std::max(1, viewState.framebufferWidth)) / static_cast<float>(std::max(1, viewState.framebufferHeight));
    const float pitchRadians = DegreesToRadians(56.0f);
    const float yawRadians = DegreesToRadians(-45.0f);
    const float halfSpan = static_cast<float>(std::max(32, viewState.visibleTiles)) * 0.5f;
    const float distance = std::max(halfSpan * (2.35f + std::max(0.0f, 1.0f - aspectRatio)), 28.0f);

    cameraState.target = Vec3(
        static_cast<float>(viewState.cameraX) + static_cast<float>(viewState.visibleTiles) * 0.5f,
        0.0f,
        static_cast<float>(viewState.cameraY) + static_cast<float>(viewState.visibleTiles) * 0.5f);

    const Vec3 viewDirection(
        std::cos(pitchRadians) * std::cos(yawRadians),
        std::sin(pitchRadians),
        std::cos(pitchRadians) * std::sin(yawRadians));
    cameraState.position = cameraState.target + (viewDirection * distance);

    cameraState.view = LookAt(cameraState.position, cameraState.target, Vec3(0.0f, 1.0f, 0.0f));
    cameraState.projection = Perspective(DegreesToRadians(48.0f), aspectRatio, 0.1f, 4096.0f);
    cameraState.viewProjection = Multiply(cameraState.projection, cameraState.view);
    cameraState.inverseViewProjection = Inverse(cameraState.viewProjection);
    cameraState.frustum = ExtractFrustum(cameraState.viewProjection);
    return cameraState;
}

bool TryPickGroundTile(const ViewState& viewState, const CameraState& cameraState, int mapWidth, int mapHeight, int& pickedTileX, int& pickedTileY) {
    const float viewportWidth = static_cast<float>(std::max(1, viewState.framebufferWidth));
    const float viewportHeight = static_cast<float>(std::max(1, viewState.framebufferHeight));

    const float normalizedX = (2.0f * static_cast<float>(viewState.mouseX) / viewportWidth) - 1.0f;
    const float normalizedY = 1.0f - (2.0f * static_cast<float>(viewState.mouseY) / viewportHeight);

    const Vec4 nearClip(normalizedX, normalizedY, -1.0f, 1.0f);
    const Vec4 farClip(normalizedX, normalizedY, 1.0f, 1.0f);

    Vec4 nearWorld = Multiply(cameraState.inverseViewProjection, nearClip);
    Vec4 farWorld = Multiply(cameraState.inverseViewProjection, farClip);
    if (std::fabs(nearWorld.w) <= std::numeric_limits<float>::epsilon() || std::fabs(farWorld.w) <= std::numeric_limits<float>::epsilon()) {
        return false;
    }

    nearWorld.x /= nearWorld.w;
    nearWorld.y /= nearWorld.w;
    nearWorld.z /= nearWorld.w;
    farWorld.x /= farWorld.w;
    farWorld.y /= farWorld.w;
    farWorld.z /= farWorld.w;

    const Vec3 rayOrigin(nearWorld.x, nearWorld.y, nearWorld.z);
    const Vec3 rayDirection = Normalize(Vec3(farWorld.x - nearWorld.x, farWorld.y - nearWorld.y, farWorld.z - nearWorld.z));
    if (std::fabs(rayDirection.y) <= 0.0001f) {
        return false;
    }

    const float rayDistance = -rayOrigin.y / rayDirection.y;
    if (rayDistance < 0.0f) {
        return false;
    }

    const Vec3 hitPoint = rayOrigin + (rayDirection * rayDistance);
    const int tileX = static_cast<int>(std::floor(hitPoint.x));
    const int tileY = static_cast<int>(std::floor(hitPoint.z));
    if (tileX < 0 || tileX >= mapWidth || tileY < 0 || tileY >= mapHeight) {
        return false;
    }

    pickedTileX = tileX;
    pickedTileY = tileY;
    return true;
}

void UpdateTileStateTexture(GLuint textureId, const PublishedWorldSnapshot& snapshot, std::vector<float>& texturePixels) {
    if (snapshot.tiles == 0) {
        return;
    }

    const std::size_t tileCount = static_cast<std::size_t>(snapshot.width) * static_cast<std::size_t>(snapshot.height);
    if (texturePixels.size() != tileCount * 2u) {
        texturePixels.resize(tileCount * 2u, 0.0f);
    }

    std::size_t tileIndex = 0;
    for (; tileIndex < tileCount; ++tileIndex) {
        const Tile& tile = (*snapshot.tiles)[tileIndex];
        texturePixels[tileIndex * 2u + 0u] = Clamp01(0.5f + (static_cast<float>(tile.airPollution) / 1280000.0f));
        texturePixels[tileIndex * 2u + 1u] = Clamp01(0.5f + (static_cast<float>(tile.landValue) / 1280000.0f));
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, snapshot.width, snapshot.height, GL_RG, GL_FLOAT, &texturePixels[0]);
}

void DestroyTileChunkCaches(std::vector<TileChunkRenderCache>& chunkCaches) {
    std::size_t chunkIndex = 0;
    for (; chunkIndex < chunkCaches.size(); ++chunkIndex) {
        if (chunkCaches[chunkIndex].instanceBufferId != 0) {
            glDeleteBuffers(1, &chunkCaches[chunkIndex].instanceBufferId);
        }

        if (chunkCaches[chunkIndex].vertexArrayId != 0) {
            glDeleteVertexArrays(1, &chunkCaches[chunkIndex].vertexArrayId);
        }
    }
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

    glewExperimental = GL_TRUE;
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

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    const float tileVertices[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f
    };

    const float boxVertices[] = {
        0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f,  0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,  1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,  1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f,  0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 0.0f
    };

    GLuint tileVertexBufferId = 0;
    glGenBuffers(1, &tileVertexBufferId);
    glBindBuffer(GL_ARRAY_BUFFER, tileVertexBufferId);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tileVertices), tileVertices, GL_STATIC_DRAW);

    GLuint lotVertexBufferId = 0;
    glGenBuffers(1, &lotVertexBufferId);
    glBindBuffer(GL_ARRAY_BUFFER, lotVertexBufferId);
    glBufferData(GL_ARRAY_BUFFER, sizeof(boxVertices), boxVertices, GL_STATIC_DRAW);

    GLuint tileStateTextureId = 0;
    glGenTextures(1, &tileStateTextureId);
    glBindTexture(GL_TEXTURE_2D, tileStateTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RG32F,
        simulationRuntime_.mapWidth(),
        simulationRuntime_.mapHeight(),
        0,
        GL_RG,
        GL_FLOAT,
        0);

    std::vector<TileChunkRenderCache> chunkCaches(simulationRuntime_.chunkLayout().size());
    std::size_t chunkIndex = 0;
    for (; chunkIndex < chunkCaches.size(); ++chunkIndex) {
        chunkCaches[chunkIndex].chunkRect = simulationRuntime_.chunkLayout()[chunkIndex];
        chunkCaches[chunkIndex].worldBounds = BuildChunkBounds(chunkCaches[chunkIndex].chunkRect);
        glGenVertexArrays(1, &chunkCaches[chunkIndex].vertexArrayId);
        glGenBuffers(1, &chunkCaches[chunkIndex].instanceBufferId);
        ConfigureTileChunkVertexArray(chunkCaches[chunkIndex].vertexArrayId, tileVertexBufferId, chunkCaches[chunkIndex].instanceBufferId);
    }

    GLuint lotVertexArrayId = 0;
    GLuint lotInstanceBufferId = 0;
    glGenVertexArrays(1, &lotVertexArrayId);
    glGenBuffers(1, &lotInstanceBufferId);
    ConfigureLotVertexArray(lotVertexArrayId, lotVertexBufferId, lotInstanceBufferId);

    ShaderProgram shaderProgram;
    if (!shaderProgram.loadFromFile(BuildShaderPath())) {
        DestroyTileChunkCaches(chunkCaches);
        glDeleteBuffers(1, &lotInstanceBufferId);
        glDeleteVertexArrays(1, &lotVertexArrayId);
        glDeleteTextures(1, &tileStateTextureId);
        glDeleteBuffers(1, &lotVertexBufferId);
        glDeleteBuffers(1, &tileVertexBufferId);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    shaderProgram.bind();
    const GLint viewProjectionLocation = glGetUniformLocation(shaderProgram.programId(), "uViewProjection");
    const GLint renderModeLocation = glGetUniformLocation(shaderProgram.programId(), "uRenderMode");
    const GLint tileTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uTileStateTexture");
    glUniform1i(tileTextureLocation, 0);

    std::vector<float> tileStatePixels;
    std::vector<LotInstanceData> lotInstances;
    std::uint64_t lastUploadedTileStateGeneration = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t lastUploadedLotRevision = std::numeric_limits<std::uint64_t>::max();

    int renderedFrames = 0;
    RendererFrameMetrics lastFrameMetrics;
    std::chrono::steady_clock::time_point counterStart = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        appController_.setFramebufferSize(framebufferWidth, framebufferHeight);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.08f, 0.11f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const ViewState viewState = appController_.viewState();
        const CameraState cameraState = BuildCameraState(viewState);
        const PublishedWorldSnapshot snapshot = simulationRuntime_.acquirePublishedSnapshot();

        int hoveredTileX = 0;
        int hoveredTileY = 0;
        const bool hasHoveredTile = TryPickGroundTile(viewState, cameraState, simulationRuntime_.mapWidth(), simulationRuntime_.mapHeight(), hoveredTileX, hoveredTileY);
        appController_.setHoveredTile(hoveredTileX, hoveredTileY, hasHoveredTile);

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS) {
            appController_.onLeftMouseButtonHeld();
        }

        RendererFrameMetrics frameMetrics;
        frameMetrics.totalChunkCount = static_cast<int>(chunkCaches.size());

        if (snapshot.tiles != 0 && snapshot.chunkRevisions != 0) {
            const std::chrono::steady_clock::time_point uploadStart = std::chrono::steady_clock::now();

            if (snapshot.generation != lastUploadedTileStateGeneration) {
                // Chunk instance data stays stable while live simulation scalars stream through a texture each publish.
                UpdateTileStateTexture(tileStateTextureId, snapshot, tileStatePixels);
                lastUploadedTileStateGeneration = snapshot.generation;
            }

            std::size_t uploadChunkIndex = 0;
            for (; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
                const std::uint64_t publishedRevision = (*snapshot.chunkRevisions)[uploadChunkIndex];
                if (chunkCaches[uploadChunkIndex].lastUploadedRevision == publishedRevision) {
                    continue;
                }

                chunkCaches[uploadChunkIndex].instances = BuildTileChunkInstances(snapshot, chunkCaches[uploadChunkIndex].chunkRect);
                chunkCaches[uploadChunkIndex].instanceCount = static_cast<GLsizei>(chunkCaches[uploadChunkIndex].instances.size());
                chunkCaches[uploadChunkIndex].lastUploadedRevision = publishedRevision;

                glBindBuffer(GL_ARRAY_BUFFER, chunkCaches[uploadChunkIndex].instanceBufferId);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(chunkCaches[uploadChunkIndex].instances.size() * sizeof(TileInstanceData)),
                    chunkCaches[uploadChunkIndex].instances.empty() ? 0 : &chunkCaches[uploadChunkIndex].instances[0],
                    GL_DYNAMIC_DRAW);
            }

            if (snapshot.lots != 0 && snapshot.lotRevision != lastUploadedLotRevision) {
                lotInstances = BuildLotInstances(*snapshot.lots);
                glBindBuffer(GL_ARRAY_BUFFER, lotInstanceBufferId);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(lotInstances.size() * sizeof(LotInstanceData)),
                    lotInstances.empty() ? 0 : &lotInstances[0],
                    GL_DYNAMIC_DRAW);
                lastUploadedLotRevision = snapshot.lotRevision;
            }

            frameMetrics.uploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - uploadStart).count();

            const std::chrono::steady_clock::time_point cullStart = std::chrono::steady_clock::now();
            std::vector<std::size_t> visibleChunkIndices;
            visibleChunkIndices.reserve(chunkCaches.size());

            std::size_t cullChunkIndex = 0;
            for (; cullChunkIndex < chunkCaches.size(); ++cullChunkIndex) {
                if (chunkCaches[cullChunkIndex].instanceCount == 0) {
                    continue;
                }

                if (!IntersectsFrustum(cameraState.frustum, chunkCaches[cullChunkIndex].worldBounds)) {
                    continue;
                }

                visibleChunkIndices.push_back(cullChunkIndex);
            }

            frameMetrics.visibleChunkCount = static_cast<int>(visibleChunkIndices.size());
            frameMetrics.cullMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - cullStart).count();

            const std::chrono::steady_clock::time_point drawStart = std::chrono::steady_clock::now();
            shaderProgram.bind();
            glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, cameraState.viewProjection.data);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tileStateTextureId);

            glUniform1i(renderModeLocation, 0);
            std::size_t visibleIndex = 0;
            for (; visibleIndex < visibleChunkIndices.size(); ++visibleIndex) {
                const TileChunkRenderCache& cache = chunkCaches[visibleChunkIndices[visibleIndex]];
                glBindVertexArray(cache.vertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, cache.instanceCount);
            }

            if (!lotInstances.empty()) {
                glUniform1i(renderModeLocation, 1);
                glBindVertexArray(lotVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(lotInstances.size()));
            }

            glBindVertexArray(0);
            frameMetrics.drawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - drawStart).count();
        } else {
            appController_.setHoveredTile(0, 0, false);
        }

        simulationRuntime_.releasePublishedSnapshot(snapshot);
        glfwSwapBuffers(window);
        glfwPollEvents();

        lastFrameMetrics = frameMetrics;
        ++renderedFrames;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - counterStart).count() >= 1) {
            const RuntimeTimingSnapshot runtimeTimings = simulationRuntime_.timingSnapshot();
            std::cout
                << "FPS " << renderedFrames
                << " Updates " << simulationRuntime_.updatesPerSecond()
                << " Sim(us) n=" << runtimeTimings.neighborPassMicros
                << " cmd=" << runtimeTimings.commandPassMicros
                << " lot=" << runtimeTimings.lotEffectsMicros
                << " local=" << runtimeTimings.localPassMicros
                << " pub=" << runtimeTimings.publishMicros
                << " wb=" << runtimeTimings.writeBufferWaitMicros
                << " Render(us) cull=" << lastFrameMetrics.cullMicros
                << " upload=" << lastFrameMetrics.uploadMicros
                << " draw=" << lastFrameMetrics.drawMicros
                << " chunks=" << lastFrameMetrics.visibleChunkCount << "/" << lastFrameMetrics.totalChunkCount
                << std::endl;
            renderedFrames = 0;
            counterStart = now;
        }
    }

    DestroyTileChunkCaches(chunkCaches);
    glDeleteBuffers(1, &lotInstanceBufferId);
    glDeleteVertexArrays(1, &lotVertexArrayId);
    glDeleteTextures(1, &tileStateTextureId);
    glDeleteBuffers(1, &lotVertexBufferId);
    glDeleteBuffers(1, &tileVertexBufferId);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
