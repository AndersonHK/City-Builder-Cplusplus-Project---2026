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
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "City.h"
#include "RoadRenderState.h"
#include "ShaderProgram.h"

namespace {
const float kPi = 3.14159265358979323846f;
const int kRoadAtlasColumns = 8;
const int kRoadAtlasRows = 8;
const int kRoadAtlasTileSize = 32;
const float kTileStateScalarScale = 640000.0f;
const float kRoadGhostAlpha = 0.46f;
const float kLotGhostAlpha = 0.42f;
const float kRegionCellWorldSize = 1024.0f;
const float kRegionCellWorldGap = 0.0f;
const std::uint8_t kOccupiedTileLiftMask = 255u;

struct Vec3 {
    float x;
    float y;
    float z;

    // Initializes a zero vector.
    Vec3()
        : x(0.0f),
          y(0.0f),
          z(0.0f) {
    }

    // Stores an explicit 3D vector.
    Vec3(float xValue, float yValue, float zValue)
        : x(xValue),
          y(yValue),
          z(zValue) {
    }
};

struct Vec2 {
    float x;
    float y;

    // Initializes a zero 2D vector.
    Vec2()
        : x(0.0f),
          y(0.0f) {
    }

    // Stores an explicit 2D vector.
    Vec2(float xValue, float yValue)
        : x(xValue),
          y(yValue) {
    }
};

struct Vec4 {
    float x;
    float y;
    float z;
    float w;

    // Initializes a zero homogeneous/color vector.
    Vec4()
        : x(0.0f),
          y(0.0f),
          z(0.0f),
          w(0.0f) {
    }

    // Stores an explicit homogeneous/color vector.
    Vec4(float xValue, float yValue, float zValue, float wValue)
        : x(xValue),
          y(yValue),
          z(zValue),
          w(wValue) {
    }
};

struct Mat4 {
    float data[16];

    // Initializes all matrix elements to zero.
    Mat4() {
        int index = 0;
        for (; index < 16; ++index) {
            data[index] = 0.0f;
        }
    }

    // Builds an identity matrix for renderer-local math.
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

enum CameraProjectionMode {
    CameraProjectionPerspective,
    CameraProjectionOrthographic
};

struct CameraSpec {
    Vec3 target;
    Vec3 up;
    float yawRadians;
    float pitchRadians;
    float distance;
    float verticalFieldOfViewRadians;
    float orthographicHeight;
    float nearPlane;
    float farPlane;
    CameraProjectionMode projectionMode;
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

struct RoadInstanceData {
    float originX;
    float originZ;
    float lift;
    float baseGlyph;
    float arrowGlyph;
    float surfaceEdgeMask;
    float dividerMask;
};

struct RouteArrowInstanceData {
    float originX;
    float originZ;
    float sizeX;
    float sizeZ;
    float directionX;
    float directionZ;
    float lift;
    float alpha;
    float colorR;
    float colorG;
    float colorB;
    float colorPadding;
};

struct RegionPreviewInstanceData {
    float originX;
    float originZ;
    float sizeX;
    float sizeZ;

    RegionPreviewInstanceData()
        : originX(0.0f),
          originZ(0.0f),
          sizeX(1.0f),
          sizeZ(1.0f) {
    }
};

struct RegionPreviewTextureCache {
    int regionX;
    int regionY;
    GLuint textureId;
    std::uint64_t previewRevision;

    RegionPreviewTextureCache()
        : regionX(0),
          regionY(0),
          textureId(0),
          previewRevision(std::numeric_limits<std::uint64_t>::max()) {
    }
};

struct RoadPreviewAxisKey {
    int tileX;
    int tileY;
    RoadAxis axis;
};

struct RoadPreviewCell {
    int tileX;
    int tileY;
    std::uint8_t junctionMask;
    std::uint8_t arrowIntentMask;
    std::uint8_t sidewalkEdges;
    std::uint8_t sameDirectionDividerEdges;
    std::uint8_t opposingDirectionDividerEdges;

    RoadPreviewCell()
        : tileX(0),
          tileY(0),
          junctionMask(0),
          arrowIntentMask(0),
          sidewalkEdges(0),
          sameDirectionDividerEdges(0),
          opposingDirectionDividerEdges(0) {
    }
};

struct TileChunkRenderCache {
    ChunkRect chunkRect;
    Aabb worldBounds;
    GLuint vertexArrayId;
    GLuint instanceBufferId;
    std::vector<TileInstanceData> instances;
    std::uint64_t lastUploadedTileStateGeneration;
    std::uint64_t lastUploadedLiftRevision;
    GLsizei instanceCount;

    // Starts a tile chunk with no GPU cache freshness.
    TileChunkRenderCache()
        : vertexArrayId(0),
          instanceBufferId(0),
          lastUploadedTileStateGeneration(std::numeric_limits<std::uint64_t>::max()),
          lastUploadedLiftRevision(std::numeric_limits<std::uint64_t>::max()),
          instanceCount(0) {
    }
};

struct RoadChunkRenderCache {
    ChunkRect chunkRect;
    Aabb worldBounds;
    GLuint vertexArrayId;
    GLuint instanceBufferId;
    std::vector<RoadInstanceData> instances;
    std::uint64_t lastUploadedRevision;
    GLsizei instanceCount;

    // Starts an elevated-road chunk with no uploaded instances.
    RoadChunkRenderCache()
        : vertexArrayId(0),
          instanceBufferId(0),
          lastUploadedRevision(0),
          instanceCount(0) {
    }
};

struct RendererFrameMetrics {
    long long cullMicros;
    long long tileUploadMicros;
    long long tileStatePackMicros;
    long long tileStateUploadMicros;
    long long tileLiftUploadMicros;
    long long groundRoadUploadMicros;
    long long elevatedRoadUploadMicros;
    long long roadGhostUploadMicros;
    long long tileOverlayUploadMicros;
    long long lotUploadMicros;
    long long lotGhostUploadMicros;
    long long tileDrawMicros;
    long long elevatedRoadDrawMicros;
    long long roadGhostDrawMicros;
    long long tileOverlayDrawMicros;
    long long lotDrawMicros;
    long long lotGhostDrawMicros;
    long long tileStateUploadedTileCount;
    long long tileStateUploadedBytes;
    long long tileLiftUploadedTileCount;
    long long tileLiftUploadedBytes;
    int visibleChunkCount;
    int visibleElevatedRoadChunkCount;
    int totalChunkCount;
    int tileStateUploadedChunkCount;
    int tileStateDeferredChunkCount;
    int tileLiftUploadedChunkCount;
    int tileLiftDeferredChunkCount;
    int tileOverlayUploadedChunkCount;
    int tileOverlayDeferredChunkCount;
    int dirtyGroundRoadChunkCount;
    int deferredGroundRoadChunkCount;
    int dirtyElevatedRoadChunkCount;
    int deferredElevatedRoadChunkCount;
    int roadGhostInstanceCount;
    int lotGhostInstanceCount;

    // Initializes all per-frame renderer metrics to zero.
    RendererFrameMetrics()
        : cullMicros(0),
          tileUploadMicros(0),
          tileStatePackMicros(0),
          tileStateUploadMicros(0),
          tileLiftUploadMicros(0),
          groundRoadUploadMicros(0),
          elevatedRoadUploadMicros(0),
          roadGhostUploadMicros(0),
          tileOverlayUploadMicros(0),
          lotUploadMicros(0),
          lotGhostUploadMicros(0),
          tileDrawMicros(0),
          elevatedRoadDrawMicros(0),
          roadGhostDrawMicros(0),
          tileOverlayDrawMicros(0),
          lotDrawMicros(0),
          lotGhostDrawMicros(0),
          tileStateUploadedTileCount(0),
          tileStateUploadedBytes(0),
          tileLiftUploadedTileCount(0),
          tileLiftUploadedBytes(0),
          visibleChunkCount(0),
          visibleElevatedRoadChunkCount(0),
          totalChunkCount(0),
          tileStateUploadedChunkCount(0),
          tileStateDeferredChunkCount(0),
          tileLiftUploadedChunkCount(0),
          tileLiftDeferredChunkCount(0),
          tileOverlayUploadedChunkCount(0),
          tileOverlayDeferredChunkCount(0),
          dirtyGroundRoadChunkCount(0),
          deferredGroundRoadChunkCount(0),
          dirtyElevatedRoadChunkCount(0),
          deferredElevatedRoadChunkCount(0),
          roadGhostInstanceCount(0),
          lotGhostInstanceCount(0) {
    }
};

class RendererCallbacks {
public:
    // Bridges GLFW callbacks back into the app controller.
    explicit RendererCallbacks(AppController& appController)
        : appController_(appController),
          isFullscreen_(false),
          windowedX_(0),
          windowedY_(0),
          windowedWidth_(2048),
          windowedHeight_(2048) {
    }

    // Forwards GLFW cursor movement into view state.
    static void CursorPositionCallback(GLFWwindow* window, double mouseX, double mouseY) {
        RendererCallbacks* callbacks = reinterpret_cast<RendererCallbacks*>(glfwGetWindowUserPointer(window));
        if (callbacks != 0) {
            callbacks->appController_.onCursorMoved(mouseX, mouseY);
        }
    }

    // Forwards GLFW mouse button actions into tool state.
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        (void)mods;
        RendererCallbacks* callbacks = reinterpret_cast<RendererCallbacks*>(glfwGetWindowUserPointer(window));
        if (callbacks == 0) {
            return;
        }

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        callbacks->appController_.onCursorMoved(mouseX, mouseY);

        if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
            callbacks->appController_.onLeftMouseButtonPressed();
        } else if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_RELEASE) {
            callbacks->appController_.onLeftMouseButtonReleased();
        }
    }

    // Forwards GLFW keyboard input into tool and camera state.
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        (void)scancode;
        RendererCallbacks* callbacks = reinterpret_cast<RendererCallbacks*>(glfwGetWindowUserPointer(window));
        if (callbacks != 0) {
            if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) && action == GLFW_PRESS && (mods & GLFW_MOD_ALT) != 0) {
                callbacks->toggleFullscreen(window);
                return;
            }

            callbacks->appController_.onKeyPressed(key, action);
        }
    }

    // Forwards GLFW scroll input into zoom state.
    static void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
        (void)xOffset;
        RendererCallbacks* callbacks = reinterpret_cast<RendererCallbacks*>(glfwGetWindowUserPointer(window));
        if (callbacks != 0) {
            callbacks->appController_.onScroll(yOffset);
        }
    }

private:
    // Switches between windowed mode and the primary monitor's current video mode.
    void toggleFullscreen(GLFWwindow* window) {
        if (!isFullscreen_) {
            glfwGetWindowPos(window, &windowedX_, &windowedY_);
            glfwGetWindowSize(window, &windowedWidth_, &windowedHeight_);

            GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* videoMode = primaryMonitor == 0 ? 0 : glfwGetVideoMode(primaryMonitor);
            if (primaryMonitor == 0 || videoMode == 0) {
                return;
            }

            glfwSetWindowMonitor(window, primaryMonitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
            isFullscreen_ = true;
            return;
        }

        glfwSetWindowMonitor(window, 0, windowedX_, windowedY_, windowedWidth_, windowedHeight_, GLFW_DONT_CARE);
        isFullscreen_ = false;
    }

    AppController& appController_;
    bool isFullscreen_;
    int windowedX_;
    int windowedY_;
    int windowedWidth_;
    int windowedHeight_;
};

// Converts degrees to radians for camera and projection setup.
float DegreesToRadians(float degrees) {
    return degrees * (kPi / 180.0f);
}

// Adds two renderer-local 3D vectors.
Vec3 operator+(const Vec3& left, const Vec3& right) {
    return Vec3(left.x + right.x, left.y + right.y, left.z + right.z);
}

// Subtracts two renderer-local 3D vectors.
Vec3 operator-(const Vec3& left, const Vec3& right) {
    return Vec3(left.x - right.x, left.y - right.y, left.z - right.z);
}

// Scales a renderer-local 3D vector.
Vec3 operator*(const Vec3& vector, float scalar) {
    return Vec3(vector.x * scalar, vector.y * scalar, vector.z * scalar);
}

// Computes a 3D dot product for camera and frustum math.
float Dot(const Vec3& left, const Vec3& right) {
    return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
}

// Computes a 3D cross product for camera basis vectors.
Vec3 Cross(const Vec3& left, const Vec3& right) {
    return Vec3(
        (left.y * right.z) - (left.z * right.y),
        (left.z * right.x) - (left.x * right.z),
        (left.x * right.y) - (left.y * right.x));
}

// Computes vector length for normalization.
float Length(const Vec3& vector) {
    return std::sqrt(Dot(vector, vector));
}

// Returns a unit-length vector, preserving zero vectors safely.
Vec3 Normalize(const Vec3& vector) {
    const float vectorLength = Length(vector);
    if (vectorLength <= std::numeric_limits<float>::epsilon()) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }

    return vector * (1.0f / vectorLength);
}

// Multiplies two column-major matrices for view/projection composition.
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

// Transforms a homogeneous vector by a column-major matrix.
Vec4 Multiply(const Mat4& matrix, const Vec4& vector) {
    return Vec4(
        matrix.data[0] * vector.x + matrix.data[4] * vector.y + matrix.data[8] * vector.z + matrix.data[12] * vector.w,
        matrix.data[1] * vector.x + matrix.data[5] * vector.y + matrix.data[9] * vector.z + matrix.data[13] * vector.w,
        matrix.data[2] * vector.x + matrix.data[6] * vector.y + matrix.data[10] * vector.z + matrix.data[14] * vector.w,
        matrix.data[3] * vector.x + matrix.data[7] * vector.y + matrix.data[11] * vector.z + matrix.data[15] * vector.w);
}

// Builds the perspective projection used by the fixed-pitch camera.
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

// Builds an orthographic projection with OpenGL clip-space depth.
Mat4 Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
    Mat4 matrix;
    matrix.data[0] = 2.0f / (right - left);
    matrix.data[5] = 2.0f / (top - bottom);
    matrix.data[10] = -2.0f / (farPlane - nearPlane);
    matrix.data[12] = -(right + left) / (right - left);
    matrix.data[13] = -(top + bottom) / (top - bottom);
    matrix.data[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    matrix.data[15] = 1.0f;
    return matrix;
}

// Builds the camera view matrix from eye, target, and up vectors.
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

// Inverts a 4x4 matrix for raycasting from clip space to world space.
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

// Normalizes a frustum plane so distance tests are stable.
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

// Extracts world-space culling planes from the view-projection matrix.
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

float AspectRatioForFramebuffer(int framebufferWidth, int framebufferHeight) {
    return static_cast<float>(std::max(1, framebufferWidth)) / static_cast<float>(std::max(1, framebufferHeight));
}

CameraState BuildCameraFromSpec(const CameraSpec& spec, float aspectRatio) {
    CameraState cameraState;
    cameraState.target = spec.target;

    const Vec3 viewDirection(
        std::cos(spec.pitchRadians) * std::cos(spec.yawRadians),
        std::sin(spec.pitchRadians),
        std::cos(spec.pitchRadians) * std::sin(spec.yawRadians));
    cameraState.position = cameraState.target + (viewDirection * spec.distance);
    cameraState.view = LookAt(cameraState.position, cameraState.target, spec.up);

    if (spec.projectionMode == CameraProjectionOrthographic) {
        const float halfHeight = std::max(1.0f, spec.orthographicHeight) * 0.5f;
        const float halfWidth = halfHeight * std::max(0.001f, aspectRatio);
        cameraState.projection = Orthographic(-halfWidth, halfWidth, -halfHeight, halfHeight, spec.nearPlane, spec.farPlane);
    } else {
        cameraState.projection = Perspective(spec.verticalFieldOfViewRadians, aspectRatio, spec.nearPlane, spec.farPlane);
    }

    cameraState.viewProjection = Multiply(cameraState.projection, cameraState.view);
    cameraState.inverseViewProjection = Inverse(cameraState.viewProjection);
    cameraState.frustum = ExtractFrustum(cameraState.viewProjection);
    return cameraState;
}

// Tests whether a chunk bounds box intersects the camera frustum.
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

// Clamps a floating value into the texture/color range.
float Clamp01(float value) {
    return std::max(0.0f, std::min(value, 1.0f));
}

// Recovers the road family represented by an atlas base glyph.
RoadFamily BaseGlyphFamily(RoadBaseGlyph baseGlyph) {
    switch (baseGlyph) {
        case RoadBaseGlyph::LocalIsolated:
        case RoadBaseGlyph::LocalDeadEndNorth:
        case RoadBaseGlyph::LocalDeadEndEast:
        case RoadBaseGlyph::LocalDeadEndSouth:
        case RoadBaseGlyph::LocalDeadEndWest:
        case RoadBaseGlyph::LocalStraightVertical:
        case RoadBaseGlyph::LocalStraightHorizontal:
        case RoadBaseGlyph::LocalCornerNorthEast:
        case RoadBaseGlyph::LocalCornerSouthEast:
        case RoadBaseGlyph::LocalCornerSouthWest:
        case RoadBaseGlyph::LocalCornerNorthWest:
        case RoadBaseGlyph::LocalTeeMissingNorth:
        case RoadBaseGlyph::LocalTeeMissingEast:
        case RoadBaseGlyph::LocalTeeMissingSouth:
        case RoadBaseGlyph::LocalTeeMissingWest:
        case RoadBaseGlyph::LocalCross:
            return RoadFamily::LocalStreet;

        case RoadBaseGlyph::HighwayIsolated:
        case RoadBaseGlyph::HighwayDeadEndNorth:
        case RoadBaseGlyph::HighwayDeadEndEast:
        case RoadBaseGlyph::HighwayDeadEndSouth:
        case RoadBaseGlyph::HighwayDeadEndWest:
        case RoadBaseGlyph::HighwayStraightVertical:
        case RoadBaseGlyph::HighwayStraightHorizontal:
        case RoadBaseGlyph::HighwayCornerNorthEast:
        case RoadBaseGlyph::HighwayCornerSouthEast:
        case RoadBaseGlyph::HighwayCornerSouthWest:
        case RoadBaseGlyph::HighwayCornerNorthWest:
        case RoadBaseGlyph::HighwayTeeMissingNorth:
        case RoadBaseGlyph::HighwayTeeMissingEast:
        case RoadBaseGlyph::HighwayTeeMissingSouth:
        case RoadBaseGlyph::HighwayTeeMissingWest:
        case RoadBaseGlyph::HighwayCross:
            return RoadFamily::Highway;

        default:
            return RoadFamily::None;
    }
}

// Recovers the cardinal exit mask represented by an atlas base glyph.
std::uint8_t BaseGlyphJunctionMask(RoadBaseGlyph baseGlyph) {
    switch (baseGlyph) {
        case RoadBaseGlyph::LocalDeadEndNorth:
        case RoadBaseGlyph::HighwayDeadEndNorth:
            return kRoadDirectionNorth;
        case RoadBaseGlyph::LocalDeadEndEast:
        case RoadBaseGlyph::HighwayDeadEndEast:
            return kRoadDirectionEast;
        case RoadBaseGlyph::LocalDeadEndSouth:
        case RoadBaseGlyph::HighwayDeadEndSouth:
            return kRoadDirectionSouth;
        case RoadBaseGlyph::LocalDeadEndWest:
        case RoadBaseGlyph::HighwayDeadEndWest:
            return kRoadDirectionWest;
        case RoadBaseGlyph::LocalStraightVertical:
        case RoadBaseGlyph::HighwayStraightVertical:
            return kRoadDirectionNorth | kRoadDirectionSouth;
        case RoadBaseGlyph::LocalStraightHorizontal:
        case RoadBaseGlyph::HighwayStraightHorizontal:
            return kRoadDirectionEast | kRoadDirectionWest;
        case RoadBaseGlyph::LocalCornerNorthEast:
        case RoadBaseGlyph::HighwayCornerNorthEast:
            return kRoadDirectionNorth | kRoadDirectionEast;
        case RoadBaseGlyph::LocalCornerSouthEast:
        case RoadBaseGlyph::HighwayCornerSouthEast:
            return kRoadDirectionSouth | kRoadDirectionEast;
        case RoadBaseGlyph::LocalCornerSouthWest:
        case RoadBaseGlyph::HighwayCornerSouthWest:
            return kRoadDirectionSouth | kRoadDirectionWest;
        case RoadBaseGlyph::LocalCornerNorthWest:
        case RoadBaseGlyph::HighwayCornerNorthWest:
            return kRoadDirectionNorth | kRoadDirectionWest;
        case RoadBaseGlyph::LocalTeeMissingNorth:
        case RoadBaseGlyph::HighwayTeeMissingNorth:
            return kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest;
        case RoadBaseGlyph::LocalTeeMissingEast:
        case RoadBaseGlyph::HighwayTeeMissingEast:
            return kRoadDirectionNorth | kRoadDirectionSouth | kRoadDirectionWest;
        case RoadBaseGlyph::LocalTeeMissingSouth:
        case RoadBaseGlyph::HighwayTeeMissingSouth:
            return kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionWest;
        case RoadBaseGlyph::LocalTeeMissingWest:
        case RoadBaseGlyph::HighwayTeeMissingWest:
            return kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth;
        case RoadBaseGlyph::LocalCross:
        case RoadBaseGlyph::HighwayCross:
            return kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest;
        default:
            return 0;
    }
}

// Measures distance from an atlas sample point to a line segment.
float DistanceToSegment(const Vec2& point, const Vec2& startPoint, const Vec2& endPoint) {
    const Vec2 pointOffset(point.x - startPoint.x, point.y - startPoint.y);
    const Vec2 lineOffset(endPoint.x - startPoint.x, endPoint.y - startPoint.y);
    const float lineLengthSquared = (lineOffset.x * lineOffset.x) + (lineOffset.y * lineOffset.y);
    const float projection = lineLengthSquared <= 0.000001f
        ? 0.0f
        : Clamp01(((pointOffset.x * lineOffset.x) + (pointOffset.y * lineOffset.y)) / lineLengthSquared);
    const Vec2 closestPoint(startPoint.x + (lineOffset.x * projection), startPoint.y + (lineOffset.y * projection));
    const float dx = point.x - closestPoint.x;
    const float dy = point.y - closestPoint.y;
    return std::sqrt((dx * dx) + (dy * dy));
}

// Generates an antialiased line mask for procedural road atlas glyphs.
float CpuLineMask(const Vec2& point, const Vec2& startPoint, const Vec2& endPoint, float thickness) {
    const float distance = DistanceToSegment(point, startPoint, endPoint);
    return 1.0f - Clamp01((distance - thickness) / 0.02f);
}

// Generates an antialiased arrow mask for directional road overlays.
float CpuArrowMask(const Vec2& point, const Vec2& direction) {
    const float length = std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
    if (length <= 0.000001f) {
        return 0.0f;
    }

    const Vec2 normalizedDirection(direction.x / length, direction.y / length);
    const Vec2 perpendicular(-normalizedDirection.y, normalizedDirection.x);
    const Vec2 centerPoint(0.5f, 0.5f);
    const Vec2 tipPoint(centerPoint.x + normalizedDirection.x * 0.24f, centerPoint.y + normalizedDirection.y * 0.24f);
    const float shaft = CpuLineMask(point,
        Vec2(centerPoint.x - normalizedDirection.x * 0.10f, centerPoint.y - normalizedDirection.y * 0.10f),
        Vec2(tipPoint.x - normalizedDirection.x * 0.05f, tipPoint.y - normalizedDirection.y * 0.05f),
        0.028f);
    const float leftHead = CpuLineMask(point,
        Vec2(tipPoint.x - normalizedDirection.x * 0.10f + perpendicular.x * 0.06f, tipPoint.y - normalizedDirection.y * 0.10f + perpendicular.y * 0.06f),
        tipPoint,
        0.024f);
    const float rightHead = CpuLineMask(point,
        Vec2(tipPoint.x - normalizedDirection.x * 0.10f - perpendicular.x * 0.06f, tipPoint.y - normalizedDirection.y * 0.10f - perpendicular.y * 0.06f),
        tipPoint,
        0.024f);
    return std::max(shaft, std::max(leftHead, rightHead));
}

// Alpha-blends one generated atlas pixel into an RGBA buffer.
void BlendPixel(std::vector<std::uint8_t>& pixels, int textureWidth, int pixelX, int pixelY, const Vec4& colorValue) {
    const std::size_t pixelOffset = (static_cast<std::size_t>(pixelY) * static_cast<std::size_t>(textureWidth) + static_cast<std::size_t>(pixelX)) * 4u;
    const float srcAlpha = Clamp01(colorValue.w);
    const float dstAlpha = static_cast<float>(pixels[pixelOffset + 3u]) / 255.0f;
    const float outAlpha = srcAlpha + (dstAlpha * (1.0f - srcAlpha));
    if (outAlpha <= 0.000001f) {
        pixels[pixelOffset + 0u] = 0;
        pixels[pixelOffset + 1u] = 0;
        pixels[pixelOffset + 2u] = 0;
        pixels[pixelOffset + 3u] = 0;
        return;
    }

    const float dstR = static_cast<float>(pixels[pixelOffset + 0u]) / 255.0f;
    const float dstG = static_cast<float>(pixels[pixelOffset + 1u]) / 255.0f;
    const float dstB = static_cast<float>(pixels[pixelOffset + 2u]) / 255.0f;
    const float outR = ((colorValue.x * srcAlpha) + (dstR * dstAlpha * (1.0f - srcAlpha))) / outAlpha;
    const float outG = ((colorValue.y * srcAlpha) + (dstG * dstAlpha * (1.0f - srcAlpha))) / outAlpha;
    const float outB = ((colorValue.z * srcAlpha) + (dstB * dstAlpha * (1.0f - srcAlpha))) / outAlpha;

    pixels[pixelOffset + 0u] = static_cast<std::uint8_t>(Clamp01(outR) * 255.0f + 0.5f);
    pixels[pixelOffset + 1u] = static_cast<std::uint8_t>(Clamp01(outG) * 255.0f + 0.5f);
    pixels[pixelOffset + 2u] = static_cast<std::uint8_t>(Clamp01(outB) * 255.0f + 0.5f);
    pixels[pixelOffset + 3u] = static_cast<std::uint8_t>(Clamp01(outAlpha) * 255.0f + 0.5f);
}

// Paints one road surface glyph into the generated road atlas.
void PaintRoadBaseGlyph(std::vector<std::uint8_t>& pixels, int textureWidth, int cellX, int cellY, RoadBaseGlyph glyph) {
    const RoadFamily family = BaseGlyphFamily(glyph);
    if (family == RoadFamily::None) {
        return;
    }

    const std::uint8_t junctionMask = BaseGlyphJunctionMask(glyph);
    const std::uint8_t surfaceEdgeMask = 0;
    const Vec4 roadColor = family == RoadFamily::LocalStreet ? Vec4(0.22f, 0.23f, 0.24f, 1.0f) : Vec4(0.16f, 0.19f, 0.25f, 1.0f);
    const Vec4 sidewalkColor(0.74f, 0.72f, 0.66f, 1.0f);
    const Vec4 markingColor = family == RoadFamily::LocalStreet ? Vec4(0.88f, 0.84f, 0.74f, 1.0f) : Vec4(0.93f, 0.86f, 0.32f, 1.0f);

    int localY = 0;
    for (; localY < kRoadAtlasTileSize; ++localY) {
        int localX = 0;
        for (; localX < kRoadAtlasTileSize; ++localX) {
            const int pixelX = cellX * kRoadAtlasTileSize + localX;
            const int pixelY = cellY * kRoadAtlasTileSize + localY;
            const float u = (static_cast<float>(localX) + 0.5f) / static_cast<float>(kRoadAtlasTileSize);
            const float v = (static_cast<float>(localY) + 0.5f) / static_cast<float>(kRoadAtlasTileSize);

            Vec4 pixelColor = roadColor;
            if (family == RoadFamily::LocalStreet) {
                if ((surfaceEdgeMask & kRoadDirectionNorth) != 0 && v < 0.16f) {
                    pixelColor = sidewalkColor;
                } else if ((surfaceEdgeMask & kRoadDirectionEast) != 0 && u > 0.84f) {
                    pixelColor = sidewalkColor;
                } else if ((surfaceEdgeMask & kRoadDirectionSouth) != 0 && v > 0.84f) {
                    pixelColor = sidewalkColor;
                } else if ((surfaceEdgeMask & kRoadDirectionWest) != 0 && u < 0.16f) {
                    pixelColor = sidewalkColor;
                }
            }

            const Vec2 point(u, v);
            float markingMask = 0.0f;
            if (glyph == RoadBaseGlyph::LocalIsolated || glyph == RoadBaseGlyph::HighwayIsolated) {
                markingMask = CpuLineMask(point, Vec2(0.34f, 0.50f), Vec2(0.66f, 0.50f), 0.03f);
            } else {
                const Vec2 center(0.5f, 0.5f);
                if ((junctionMask & kRoadDirectionNorth) != 0) {
                    markingMask = std::max(markingMask, CpuLineMask(point, center, Vec2(0.5f, 0.08f), 0.028f));
                }
                if ((junctionMask & kRoadDirectionEast) != 0) {
                    markingMask = std::max(markingMask, CpuLineMask(point, center, Vec2(0.92f, 0.5f), 0.028f));
                }
                if ((junctionMask & kRoadDirectionSouth) != 0) {
                    markingMask = std::max(markingMask, CpuLineMask(point, center, Vec2(0.5f, 0.92f), 0.028f));
                }
                if ((junctionMask & kRoadDirectionWest) != 0) {
                    markingMask = std::max(markingMask, CpuLineMask(point, center, Vec2(0.08f, 0.5f), 0.028f));
                }
            }

            if (markingMask > 0.0f) {
                BlendPixel(pixels, textureWidth, pixelX, pixelY, pixelColor);
                BlendPixel(pixels, textureWidth, pixelX, pixelY, Vec4(markingColor.x, markingColor.y, markingColor.z, markingMask));
            } else {
                BlendPixel(pixels, textureWidth, pixelX, pixelY, pixelColor);
            }
        }
    }
}

// Converts an arrow glyph id to its atlas-space direction vector.
Vec2 ArrowGlyphDirection(RoadArrowGlyph glyph) {
    switch (glyph) {
        case RoadArrowGlyph::North:
            return Vec2(0.0f, -1.0f);
        case RoadArrowGlyph::East:
            return Vec2(1.0f, 0.0f);
        case RoadArrowGlyph::South:
            return Vec2(0.0f, 1.0f);
        case RoadArrowGlyph::West:
            return Vec2(-1.0f, 0.0f);
        case RoadArrowGlyph::NorthEast:
            return Vec2(1.0f, -1.0f);
        case RoadArrowGlyph::SouthEast:
            return Vec2(1.0f, 1.0f);
        case RoadArrowGlyph::SouthWest:
            return Vec2(-1.0f, 1.0f);
        case RoadArrowGlyph::NorthWest:
            return Vec2(-1.0f, -1.0f);
        default:
            return Vec2(0.0f, 0.0f);
    }
}

// Paints one road directional overlay glyph into the generated atlas.
void PaintRoadArrowGlyph(std::vector<std::uint8_t>& pixels, int textureWidth, int cellX, int cellY, RoadArrowGlyph glyph) {
    if (glyph == RoadArrowGlyph::None) {
        return;
    }

    const Vec2 direction = ArrowGlyphDirection(glyph);
    const Vec4 arrowColor(0.93f, 0.86f, 0.32f, 1.0f);

    int localY = 0;
    for (; localY < kRoadAtlasTileSize; ++localY) {
        int localX = 0;
        for (; localX < kRoadAtlasTileSize; ++localX) {
            const float u = (static_cast<float>(localX) + 0.5f) / static_cast<float>(kRoadAtlasTileSize);
            const float v = (static_cast<float>(localY) + 0.5f) / static_cast<float>(kRoadAtlasTileSize);
            const float alpha = CpuArrowMask(Vec2(u, v), direction);
            if (alpha <= 0.0f) {
                continue;
            }

            const int pixelX = cellX * kRoadAtlasTileSize + localX;
            const int pixelY = cellY * kRoadAtlasTileSize + localY;
            BlendPixel(pixels, textureWidth, pixelX, pixelY, Vec4(arrowColor.x, arrowColor.y, arrowColor.z, alpha));
        }
    }
}

// Builds a CPU-generated road atlas and uploads it once to OpenGL.
GLuint CreateRoadAtlasTexture(bool arrowAtlas) {
    const int textureWidth = kRoadAtlasColumns * kRoadAtlasTileSize;
    const int textureHeight = kRoadAtlasRows * kRoadAtlasTileSize;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(textureWidth) * static_cast<std::size_t>(textureHeight) * 4u, 0);

    const int glyphCount = arrowAtlas
        ? static_cast<int>(RoadArrowGlyph::NorthWest) + 1
        : static_cast<int>(RoadBaseGlyph::HighwayCross) + 1;
    int glyphIndex = 0;
    for (; glyphIndex < glyphCount; ++glyphIndex) {
        const int cellX = glyphIndex % kRoadAtlasColumns;
        const int cellY = glyphIndex / kRoadAtlasColumns;
        if (arrowAtlas) {
            PaintRoadArrowGlyph(pixels, textureWidth, cellX, cellY, static_cast<RoadArrowGlyph>(glyphIndex));
        } else {
            PaintRoadBaseGlyph(pixels, textureWidth, cellX, cellY, static_cast<RoadBaseGlyph>(glyphIndex));
        }
    }

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.empty() ? 0 : &pixels[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return textureId;
}

// Finds the executable directory so the runtime shader can be loaded beside it.
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

// Builds the path to the copied runtime shader file.
std::string BuildShaderPath() {
    return GetExecutableDirectory() + "\\Basic.shader";
}

// Configures one instanced vertex attribute for OpenGL.
void SetupInstanceAttribute(GLuint attributeIndex, GLint componentCount, GLsizei stride, std::size_t offsetBytes) {
    glEnableVertexAttribArray(attributeIndex);
    glVertexAttribPointer(attributeIndex, componentCount, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetBytes));
    glVertexAttribDivisor(attributeIndex, 1);
}

// Wires static tile vertices and per-tile instance data for a chunk VAO.
void ConfigureTileChunkVertexArray(GLuint vertexArrayId, GLuint tileVertexBufferId, GLuint instanceBufferId) {
    glBindVertexArray(vertexArrayId);

    glBindBuffer(GL_ARRAY_BUFFER, tileVertexBufferId);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceBufferId);
    SetupInstanceAttribute(1, 4, sizeof(TileInstanceData), 0);

    glBindVertexArray(0);
}

// Wires box vertices and per-lot instance data for placeholder prism drawing.
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

// Wires tile quad vertices and per-road instance data for elevated roads.
void ConfigureRoadChunkVertexArray(GLuint vertexArrayId, GLuint tileVertexBufferId, GLuint instanceBufferId) {
    glBindVertexArray(vertexArrayId);

    glBindBuffer(GL_ARRAY_BUFFER, tileVertexBufferId);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceBufferId);
    SetupInstanceAttribute(1, 4, sizeof(RoadInstanceData), 0);
    SetupInstanceAttribute(2, 4, sizeof(RoadInstanceData), sizeof(float) * 4);

    glBindVertexArray(0);
}

// Wires stretched route-arrow quads for queried commute paths.
void ConfigureRouteArrowVertexArray(GLuint vertexArrayId, GLuint tileVertexBufferId, GLuint instanceBufferId) {
    glBindVertexArray(vertexArrayId);

    glBindBuffer(GL_ARRAY_BUFFER, tileVertexBufferId);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceBufferId);
    SetupInstanceAttribute(1, 4, sizeof(RouteArrowInstanceData), 0);
    SetupInstanceAttribute(2, 4, sizeof(RouteArrowInstanceData), sizeof(float) * 4);
    SetupInstanceAttribute(3, 4, sizeof(RouteArrowInstanceData), sizeof(float) * 8);

    glBindVertexArray(0);
}

void ConfigureRegionPreviewVertexArray(GLuint vertexArrayId, GLuint tileVertexBufferId, GLuint instanceBufferId) {
    glBindVertexArray(vertexArrayId);

    glBindBuffer(GL_ARRAY_BUFFER, tileVertexBufferId);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceBufferId);
    SetupInstanceAttribute(1, 4, sizeof(RegionPreviewInstanceData), 0);

    glBindVertexArray(0);
}

// Builds conservative world bounds used to cull one chunk.
Aabb BuildChunkBounds(const ChunkRect& chunkRect) {
    Aabb bounds;
    bounds.minimum = Vec3(static_cast<float>(chunkRect.startX), 0.0f, static_cast<float>(chunkRect.startY));
    bounds.maximum = Vec3(
        static_cast<float>(chunkRect.startX + chunkRect.width),
        1.2f,
        static_cast<float>(chunkRect.startY + chunkRect.height));
    return bounds;
}

// Builds static tile origin and UV data for one persistent chunk buffer.
std::vector<TileInstanceData> BuildTileChunkInstances(int mapWidth, int mapHeight, const ChunkRect& chunkRect) {
    std::vector<TileInstanceData> instances;
    instances.reserve(static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height));

    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            TileInstanceData instance;
            instance.originX = static_cast<float>(tileX);
            instance.originZ = static_cast<float>(tileY);
            instance.tileU = (static_cast<float>(tileX) + 0.5f) / static_cast<float>(mapWidth);
            instance.tileV = (static_cast<float>(tileY) + 0.5f) / static_cast<float>(mapHeight);
            instances.push_back(instance);
        }
    }

    return instances;
}

LotInstanceData BuildLotInstance(const LotRenderInstance& lot) {
    LotInstanceData instance;
    instance.originX = static_cast<float>(lot.originX);
    instance.originZ = static_cast<float>(lot.originY);
    instance.sizeX = static_cast<float>(lot.width);
    instance.sizeZ = static_cast<float>(lot.height);
    instance.height = lot.renderHeight;
    instance.colorR = lot.colorR;
    instance.colorG = lot.colorG;
    instance.colorB = lot.colorB;
    return instance;
}

// Converts published lot render records into instanced box payloads.
std::vector<LotInstanceData> BuildLotInstances(const std::vector<LotRenderInstance>& lots) {
    std::vector<LotInstanceData> instances;
    instances.reserve(lots.size());

    std::size_t lotIndex = 0;
    for (; lotIndex < lots.size(); ++lotIndex) {
        instances.push_back(BuildLotInstance(lots[lotIndex]));
    }

    return instances;
}

// Returns the world-space vertical offset for a road layer.
float RoadLayerLift(TransportLayerId layer) {
    switch (layer) {
        case TransportLayerId::Ground:
            return 0.035f;

        case TransportLayerId::Elevated:
            return 0.60f;

        case TransportLayerId::Underground:
            return -0.25f;

        default:
            return 0.035f;
    }
}

std::vector<RouteArrowInstanceData> BuildRouteArrowInstances(const std::vector<CommuteRouteSegment>& segments) {
    std::vector<RouteArrowInstanceData> instances;
    instances.reserve(segments.size());

    std::size_t segmentIndex = 0;
    for (; segmentIndex < segments.size(); ++segmentIndex) {
        const CommuteRouteSegment& segment = segments[segmentIndex];
        const int directionX = RoadDirectionDeltaX(segment.direction);
        const int directionY = RoadDirectionDeltaY(segment.direction);
        if (directionX == 0 && directionY == 0) {
            continue;
        }

        RouteArrowInstanceData instance;
        instance.directionX = static_cast<float>(directionX);
        instance.directionZ = static_cast<float>(directionY);
        instance.lift = RoadLayerLift(segment.layer) + 0.09f;
        instance.alpha = 0.88f;
        if (segment.mode == TransportMode::Pedestrian) {
            instance.colorR = 1.0f;
            instance.colorG = 0.22f;
            instance.colorB = 0.66f;
        } else {
            instance.colorR = 0.08f;
            instance.colorG = 0.95f;
            instance.colorB = 0.26f;
        }
        instance.colorPadding = 0.0f;

        const int minX = std::min(segment.startTileX, segment.endTileX);
        const int minY = std::min(segment.startTileY, segment.endTileY);
        const int maxX = std::max(segment.startTileX, segment.endTileX);
        const int maxY = std::max(segment.startTileY, segment.endTileY);
        if (directionX != 0) {
            instance.originX = static_cast<float>(minX);
            instance.originZ = static_cast<float>(segment.startTileY) + 0.22f;
            instance.sizeX = static_cast<float>(maxX - minX + 1);
            instance.sizeZ = 0.56f;
        } else {
            instance.originX = static_cast<float>(segment.startTileX) + 0.22f;
            instance.originZ = static_cast<float>(minY);
            instance.sizeX = 0.56f;
            instance.sizeZ = static_cast<float>(maxY - minY + 1);
        }

        instances.push_back(instance);
    }

    return instances;
}

bool RoadPreviewAxisKeyLess(const RoadPreviewAxisKey& left, const RoadPreviewAxisKey& right) {
    if (left.tileY != right.tileY) {
        return left.tileY < right.tileY;
    }
    if (left.tileX != right.tileX) {
        return left.tileX < right.tileX;
    }
    return static_cast<int>(left.axis) < static_cast<int>(right.axis);
}

bool HasRoadPreviewAxisPlacement(const std::vector<RoadPreviewAxisKey>& axisKeys, int tileX, int tileY, RoadAxis axis) {
    RoadPreviewAxisKey key;
    key.tileX = tileX;
    key.tileY = tileY;
    key.axis = axis;
    return std::binary_search(axisKeys.begin(), axisKeys.end(), key, RoadPreviewAxisKeyLess);
}

std::uint8_t RoadPreviewConnectionMask(const RoadLanePlacement& lanePlacement, const std::vector<RoadPreviewAxisKey>& axisKeys) {
    std::uint8_t connectionMask = 0;
    if (lanePlacement.axis == RoadAxis::Horizontal) {
        if (HasRoadPreviewAxisPlacement(axisKeys, lanePlacement.tileX - 1, lanePlacement.tileY, RoadAxis::Horizontal)) {
            connectionMask |= kRoadDirectionWest;
        }
        if (HasRoadPreviewAxisPlacement(axisKeys, lanePlacement.tileX + 1, lanePlacement.tileY, RoadAxis::Horizontal)) {
            connectionMask |= kRoadDirectionEast;
        }
    } else if (lanePlacement.axis == RoadAxis::Vertical) {
        if (HasRoadPreviewAxisPlacement(axisKeys, lanePlacement.tileX, lanePlacement.tileY - 1, RoadAxis::Vertical)) {
            connectionMask |= kRoadDirectionNorth;
        }
        if (HasRoadPreviewAxisPlacement(axisKeys, lanePlacement.tileX, lanePlacement.tileY + 1, RoadAxis::Vertical)) {
            connectionMask |= kRoadDirectionSouth;
        }
    }

    return connectionMask;
}

std::vector<RoadInstanceData> BuildRoadPreviewInstances(const RoadStrokeCommand& roadStrokeCommand, int mapWidth, int mapHeight) {
    std::vector<RoadInstanceData> instances;
    if (roadStrokeCommand.family == RoadFamily::None) {
        return instances;
    }

    std::vector<RoadTilePlacement> placements;
    placements.reserve(4096);
    Road road(roadStrokeCommand.roadTemplate);
    if (!road.appendStrokePlacements(roadStrokeCommand.startTile, roadStrokeCommand.cornerTile, roadStrokeCommand.endTile, mapWidth, mapHeight, placements)) {
        return instances;
    }
    if (placements.empty()) {
        return instances;
    }

    std::vector<RoadPreviewAxisKey> axisKeys;
    axisKeys.reserve(placements.size());
    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        RoadPreviewAxisKey key;
        key.tileX = placements[placementIndex].tileX;
        key.tileY = placements[placementIndex].tileY;
        key.axis = placements[placementIndex].lanePlacement.axis;
        axisKeys.push_back(key);
    }
    std::sort(axisKeys.begin(), axisKeys.end(), RoadPreviewAxisKeyLess);

    std::sort(placements.begin(), placements.end(), [](const RoadTilePlacement& left, const RoadTilePlacement& right) {
        if (left.tileIndex != right.tileIndex) {
            return left.tileIndex < right.tileIndex;
        }
        return left.lanePlacement.laneIndex < right.lanePlacement.laneIndex;
    });

    instances.reserve(placements.size());
    placementIndex = 0;
    while (placementIndex < placements.size()) {
        const int tileIndex = placements[placementIndex].tileIndex;
        RoadPreviewCell cell;
        cell.tileX = placements[placementIndex].tileX;
        cell.tileY = placements[placementIndex].tileY;

        while (placementIndex < placements.size() && placements[placementIndex].tileIndex == tileIndex) {
            const RoadLanePlacement& lanePlacement = placements[placementIndex].lanePlacement;
            cell.junctionMask |= RoadPreviewConnectionMask(lanePlacement, axisKeys);
            cell.arrowIntentMask |= lanePlacement.arrowTravelMask;
            cell.sidewalkEdges |= lanePlacement.sidewalkEdgeMask;
            cell.sameDirectionDividerEdges |= lanePlacement.sameDirectionDividerMask;
            cell.opposingDirectionDividerEdges |= lanePlacement.opposingDirectionDividerMask;
            ++placementIndex;
        }

        const RoadRenderVariant renderVariant = ChooseRenderVariant(cell.junctionMask);
        RoadInstanceData instance;
        instance.originX = static_cast<float>(cell.tileX);
        instance.originZ = static_cast<float>(cell.tileY);
        instance.lift = RoadLayerLift(roadStrokeCommand.layer);
        instance.baseGlyph = static_cast<float>(ChooseBaseGlyph(roadStrokeCommand.family, renderVariant, cell.junctionMask));
        instance.arrowGlyph = static_cast<float>(ChooseArrowGlyph(cell.arrowIntentMask));
        instance.surfaceEdgeMask = static_cast<float>(PackLaneGraphicMask(cell.sidewalkEdges, 0));
        instance.dividerMask = static_cast<float>(PackDividerMask(cell.sameDirectionDividerEdges, cell.opposingDirectionDividerEdges));
        instances.push_back(instance);
    }

    return instances;
}

// Builds elevated-road instances for a visible dirty chunk.
std::vector<RoadInstanceData> BuildRoadChunkInstances(const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect) {
    std::vector<RoadInstanceData> instances;
    if (snapshot.roads == 0) {
        return instances;
    }

    instances.reserve(static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height));
    const std::size_t totalTileCount = static_cast<std::size_t>(snapshot.width) * static_cast<std::size_t>(snapshot.height);
    const TransportLayerId layer = TransportLayerId::Elevated;

    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const int tileIndexValue = tileY * snapshot.width + tileX;
            const std::size_t slot = TransportNetwork::slotIndex(layer, tileIndexValue, totalTileCount);
            const ResolvedRoadCell& roadCell = (*snapshot.roads)[slot];
            if (roadCell.family == static_cast<std::uint8_t>(RoadFamily::None)) {
                continue;
            }

            RoadInstanceData instance;
            instance.originX = static_cast<float>(tileX);
            instance.originZ = static_cast<float>(tileY);
            instance.lift = RoadLayerLift(layer);
            instance.baseGlyph = static_cast<float>(roadCell.baseGlyph);
            instance.arrowGlyph = static_cast<float>(roadCell.arrowGlyph);
            instance.surfaceEdgeMask = static_cast<float>(roadCell.surfaceEdgeMask);
            instance.dividerMask = static_cast<float>(roadCell.dividerMask);
            instances.push_back(instance);
        }
    }

    return instances;
}

// Derives camera matrices and frustum from the current view state.
CameraState BuildCameraState(const ViewState& viewState) {
    const float aspectRatio = AspectRatioForFramebuffer(viewState.framebufferWidth, viewState.framebufferHeight);
    const float halfSpan = static_cast<float>(viewState.visibleTiles) * 0.5f;
    CameraSpec spec;
    spec.target = Vec3(
        static_cast<float>(viewState.cameraX) + static_cast<float>(viewState.visibleTiles) * 0.5f,
        0.0f,
        static_cast<float>(viewState.cameraY) + static_cast<float>(viewState.visibleTiles) * 0.5f);
    spec.up = Vec3(0.0f, 1.0f, 0.0f);
    spec.yawRadians = DegreesToRadians(-45.0f);
    spec.pitchRadians = DegreesToRadians(56.0f);
    spec.distance = std::max(halfSpan * (2.10f + std::max(0.0f, 1.0f - aspectRatio)), 16.0f);
    spec.verticalFieldOfViewRadians = DegreesToRadians(48.0f);
    spec.orthographicHeight = static_cast<float>(viewState.visibleTiles);
    spec.nearPlane = 0.1f;
    spec.farPlane = 4096.0f;
    spec.projectionMode = CameraProjectionPerspective;
    return BuildCameraFromSpec(spec, aspectRatio);
}

CameraState BuildTopDownOrthographicCameraState(
    float centerX,
    float centerZ,
    float spanX,
    float spanZ,
    int framebufferWidth,
    int framebufferHeight,
    float padding) {
    const float aspectRatio = AspectRatioForFramebuffer(framebufferWidth, framebufferHeight);
    const float paddedSpanX = std::max(1.0f, spanX + padding * 2.0f);
    const float paddedSpanZ = std::max(1.0f, spanZ + padding * 2.0f);
    const float orthographicHeight = std::max(paddedSpanZ, paddedSpanX / std::max(0.001f, aspectRatio));
    const float cameraDistance = std::max(2048.0f, std::max(paddedSpanX, paddedSpanZ) * 2.0f);

    CameraSpec spec;
    spec.target = Vec3(centerX, 0.0f, centerZ);
    spec.up = Vec3(0.0f, 0.0f, -1.0f);
    spec.yawRadians = 0.0f;
    spec.pitchRadians = DegreesToRadians(90.0f);
    spec.distance = cameraDistance;
    spec.verticalFieldOfViewRadians = DegreesToRadians(48.0f);
    spec.orthographicHeight = orthographicHeight;
    spec.nearPlane = 0.1f;
    spec.farPlane = cameraDistance + orthographicHeight + 4096.0f;
    spec.projectionMode = CameraProjectionOrthographic;
    return BuildCameraFromSpec(spec, aspectRatio);
}

// Raycasts the mouse cursor against the ground plane to find a tile.
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

// Packs a signed tile scalar into the normalized texture format.
GLshort PackTileStateScalar(int value) {
    const float normalizedValue = std::max(-1.0f, std::min(static_cast<float>(value) / kTileStateScalarScale, 1.0f));
    if (normalizedValue <= -1.0f) {
        return static_cast<GLshort>(-32768);
    }

    return static_cast<GLshort>(std::lround(normalizedValue * 32767.0f));
}

// Packs air pollution and land value for one visible chunk.
void FillTileStateChunkPixels(const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect, std::vector<GLshort>& texturePixels) {
    if (snapshot.tiles == 0) {
        return;
    }

    const std::size_t chunkTileCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (texturePixels.size() != chunkTileCount * 2u) {
        texturePixels.resize(chunkTileCount * 2u, 0);
    }

    std::size_t writeIndex = 0;
    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const std::size_t sourceIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(snapshot.width) + static_cast<std::size_t>(tileX);
            const Tile& tile = (*snapshot.tiles)[sourceIndex];
            texturePixels[writeIndex++] = PackTileStateScalar(tile.airPollution);
            texturePixels[writeIndex++] = PackTileStateScalar(tile.landValue);
        }
    }
}

// Uploads one packed tile-state chunk into the persistent map texture.
void UploadTileStateChunkTexture(GLuint textureId, const ChunkRect& chunkRect, const std::vector<GLshort>& texturePixels) {
    if (texturePixels.empty()) {
        return;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        chunkRect.startX,
        chunkRect.startY,
        chunkRect.width,
        chunkRect.height,
        GL_RG,
        GL_SHORT,
        &texturePixels[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// Packs lot occupancy into the tile-lift mask for one chunk.
void FillTileLiftChunkPixels(const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect, std::vector<std::uint8_t>& texturePixels) {
    const std::size_t chunkTileCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (texturePixels.size() != chunkTileCount) {
        texturePixels.resize(chunkTileCount, 0u);
    }

    if (snapshot.lotOccupancy == 0) {
        std::fill(texturePixels.begin(), texturePixels.end(), 0u);
        return;
    }

    std::size_t writeIndex = 0;
    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const std::size_t sourceIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(snapshot.width) + static_cast<std::size_t>(tileX);
            texturePixels[writeIndex++] = (*snapshot.lotOccupancy)[sourceIndex] < 0 ? 0u : kOccupiedTileLiftMask;
        }
    }
}

// Uploads one tile-lift mask chunk into the persistent map texture.
void UploadTileLiftChunkTexture(GLuint textureId, const ChunkRect& chunkRect, const std::vector<std::uint8_t>& texturePixels) {
    if (texturePixels.empty()) {
        return;
    }

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        chunkRect.startX,
        chunkRect.startY,
        chunkRect.width,
        chunkRect.height,
        GL_RED,
        GL_UNSIGNED_BYTE,
        &texturePixels[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void UploadEmptyGroundRoadChunkTexture(GLuint textureId, const ChunkRect& chunkRect, std::vector<std::uint8_t>& texturePixels) {
    const std::size_t chunkByteCount =
        static_cast<std::size_t>(chunkRect.width) *
        static_cast<std::size_t>(chunkRect.height) *
        kGroundRoadRenderChannelsPerTile;
    if (texturePixels.size() != chunkByteCount) {
        texturePixels.resize(chunkByteCount, 0u);
    } else {
        std::fill(texturePixels.begin(), texturePixels.end(), 0u);
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        chunkRect.startX,
        chunkRect.startY,
        chunkRect.width,
        chunkRect.height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        &texturePixels[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// Uploads packed ground-road overlay bytes for one dirty visible chunk.
void UpdateGroundRoadChunkTexture(GLuint textureId, const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect) {
    if (snapshot.groundRoadRenderState == 0 || snapshot.groundRoadRenderState->empty()) {
        return;
    }

    const std::size_t startOffset =
        (static_cast<std::size_t>(chunkRect.startY) * static_cast<std::size_t>(snapshot.width) + static_cast<std::size_t>(chunkRect.startX)) *
        kGroundRoadRenderChannelsPerTile;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, snapshot.width);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        chunkRect.startX,
        chunkRect.startY,
        chunkRect.width,
        chunkRect.height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        &(*snapshot.groundRoadRenderState)[startOffset]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void UpdateTileOverlayChunkTexture(GLuint textureId, const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect) {
    if (snapshot.tileOverlayState == 0 || snapshot.tileOverlayState->empty()) {
        return;
    }

    const std::size_t startOffset =
        (static_cast<std::size_t>(chunkRect.startY) * static_cast<std::size_t>(snapshot.width) + static_cast<std::size_t>(chunkRect.startX)) * 4u;
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, snapshot.width);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        chunkRect.startX,
        chunkRect.startY,
        chunkRect.width,
        chunkRect.height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        &(*snapshot.tileOverlayState)[startOffset]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// Deletes OpenGL objects owned by tile chunk caches.
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

// Deletes OpenGL objects owned by elevated-road chunk caches.
void DestroyRoadChunkCaches(std::vector<RoadChunkRenderCache>& chunkCaches) {
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

Vec3 RegionCityOrigin(const City& city) {
    const float spacing = kRegionCellWorldSize + kRegionCellWorldGap;
    return Vec3(static_cast<float>(city.regionX()) * spacing, 0.0f, static_cast<float>(city.regionY()) * spacing);
}

CameraState BuildRegionCameraState(const ViewState& viewState, const Region& region) {
    float minimumX = 0.0f;
    float minimumZ = 0.0f;
    float maximumX = kRegionCellWorldSize;
    float maximumZ = kRegionCellWorldSize;
    bool hasCity = false;

    std::size_t cityIndex = 0;
    for (; cityIndex < region.cities().size(); ++cityIndex) {
        const Vec3 origin = RegionCityOrigin(*region.cities()[cityIndex]);
        if (!hasCity) {
            minimumX = origin.x;
            minimumZ = origin.z;
            maximumX = origin.x + kRegionCellWorldSize;
            maximumZ = origin.z + kRegionCellWorldSize;
            hasCity = true;
        } else {
            minimumX = std::min(minimumX, origin.x);
            minimumZ = std::min(minimumZ, origin.z);
            maximumX = std::max(maximumX, origin.x + kRegionCellWorldSize);
            maximumZ = std::max(maximumZ, origin.z + kRegionCellWorldSize);
        }
    }

    const float aspectRatio = AspectRatioForFramebuffer(viewState.framebufferWidth, viewState.framebufferHeight);
    const float spanX = std::max(1.0f, maximumX - minimumX);
    const float spanZ = std::max(1.0f, maximumZ - minimumZ);
    const float span = std::max(spanX, spanZ);

    CameraSpec spec;
    spec.target = Vec3((minimumX + maximumX) * 0.5f, 0.0f, (minimumZ + maximumZ) * 0.5f);
    spec.up = Vec3(0.0f, 1.0f, 0.0f);
    spec.yawRadians = DegreesToRadians(-45.0f);
    spec.pitchRadians = DegreesToRadians(56.0f);
    spec.distance = std::max(span * (1.55f + std::max(0.0f, 1.0f - aspectRatio)), 16.0f);
    spec.verticalFieldOfViewRadians = DegreesToRadians(48.0f);
    spec.orthographicHeight = span;
    spec.nearPlane = 0.1f;
    spec.farPlane = 16384.0f;
    spec.projectionMode = CameraProjectionPerspective;
    return BuildCameraFromSpec(spec, aspectRatio);
}

bool TryPickRegionCity(const ViewState& viewState, const CameraState& cameraState, const Region& region, int& regionX, int& regionY) {
    const float viewportWidth = static_cast<float>(std::max(1, viewState.framebufferWidth));
    const float viewportHeight = static_cast<float>(std::max(1, viewState.framebufferHeight));
    const float normalizedX = (2.0f * static_cast<float>(viewState.mouseX) / viewportWidth) - 1.0f;
    const float normalizedY = 1.0f - (2.0f * static_cast<float>(viewState.mouseY) / viewportHeight);

    Vec4 nearWorld = Multiply(cameraState.inverseViewProjection, Vec4(normalizedX, normalizedY, -1.0f, 1.0f));
    Vec4 farWorld = Multiply(cameraState.inverseViewProjection, Vec4(normalizedX, normalizedY, 1.0f, 1.0f));
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
    std::size_t cityIndex = 0;
    for (; cityIndex < region.cities().size(); ++cityIndex) {
        const City& city = *region.cities()[cityIndex];
        const Vec3 origin = RegionCityOrigin(city);
        if (hitPoint.x >= origin.x && hitPoint.x <= origin.x + kRegionCellWorldSize &&
            hitPoint.z >= origin.z && hitPoint.z <= origin.z + kRegionCellWorldSize) {
            regionX = city.regionX();
            regionY = city.regionY();
            return true;
        }
    }

    return false;
}

void DestroyRegionPreviewTextureCaches(std::vector<RegionPreviewTextureCache>& caches) {
    std::size_t cacheIndex = 0;
    for (; cacheIndex < caches.size(); ++cacheIndex) {
        if (caches[cacheIndex].textureId != 0) {
            glDeleteTextures(1, &caches[cacheIndex].textureId);
            caches[cacheIndex].textureId = 0;
        }
    }
    caches.clear();
}

void SynchronizeRegionPreviewTextures(const Region& region, std::vector<RegionPreviewTextureCache>& caches) {
    bool recreate = caches.size() != region.cities().size();
    if (!recreate) {
        std::size_t cityIndex = 0;
        for (; cityIndex < region.cities().size(); ++cityIndex) {
            if (caches[cityIndex].regionX != region.cities()[cityIndex]->regionX() ||
                caches[cityIndex].regionY != region.cities()[cityIndex]->regionY()) {
                recreate = true;
                break;
            }
        }
    }

    if (recreate) {
        DestroyRegionPreviewTextureCaches(caches);
        caches.resize(region.cities().size());
    }

    std::size_t cityIndex = 0;
    for (; cityIndex < region.cities().size(); ++cityIndex) {
        const City& city = *region.cities()[cityIndex];
        RegionPreviewTextureCache& cache = caches[cityIndex];
        cache.regionX = city.regionX();
        cache.regionY = city.regionY();

        if (cache.textureId == 0) {
            glGenTextures(1, &cache.textureId);
            glBindTexture(GL_TEXTURE_2D, cache.textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            cache.previewRevision = std::numeric_limits<std::uint64_t>::max();
        }

        if (cache.previewRevision != city.previewRevision() && !city.previewPixels().empty()) {
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, cache.textureId);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA8,
                city.previewWidth(),
                city.previewHeight(),
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                &city.previewPixels()[0]);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            cache.previewRevision = city.previewRevision();
        }
    }
}

}

// Connects the renderer to immutable snapshots and user input state.
Renderer::Renderer(GameSession& gameSession, AppController& appController)
    : gameSession_(gameSession),
      appController_(appController) {
}

// Owns the GLFW/OpenGL frame loop, uploads, culling, drawing, and status metrics.
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

    SimulationRuntime& simulationRuntime = gameSession_.runtime();

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RG16_SNORM,
        simulationRuntime.mapWidth(),
        simulationRuntime.mapHeight(),
        0,
        GL_RG,
        GL_SHORT,
        0);

    GLuint tileLiftTextureId = 0;
    glGenTextures(1, &tileLiftTextureId);
    glBindTexture(GL_TEXTURE_2D, tileLiftTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R8,
        simulationRuntime.mapWidth(),
        simulationRuntime.mapHeight(),
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        0);

    GLuint groundRoadStateTextureId = 0;
    glGenTextures(1, &groundRoadStateTextureId);
    glBindTexture(GL_TEXTURE_2D, groundRoadStateTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        simulationRuntime.mapWidth(),
        simulationRuntime.mapHeight(),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        0);

    GLuint tileOverlayTextureId = 0;
    glGenTextures(1, &tileOverlayTextureId);
    glBindTexture(GL_TEXTURE_2D, tileOverlayTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        simulationRuntime.mapWidth(),
        simulationRuntime.mapHeight(),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        0);

    const GLuint roadBaseAtlasTextureId = CreateRoadAtlasTexture(false);
    const GLuint roadArrowAtlasTextureId = CreateRoadAtlasTexture(true);

    std::vector<TileChunkRenderCache> chunkCaches(simulationRuntime.chunkLayout().size());
    std::size_t chunkIndex = 0;
    for (; chunkIndex < chunkCaches.size(); ++chunkIndex) {
        chunkCaches[chunkIndex].chunkRect = simulationRuntime.chunkLayout()[chunkIndex];
        chunkCaches[chunkIndex].worldBounds = BuildChunkBounds(chunkCaches[chunkIndex].chunkRect);
        glGenVertexArrays(1, &chunkCaches[chunkIndex].vertexArrayId);
        glGenBuffers(1, &chunkCaches[chunkIndex].instanceBufferId);
        ConfigureTileChunkVertexArray(chunkCaches[chunkIndex].vertexArrayId, tileVertexBufferId, chunkCaches[chunkIndex].instanceBufferId);
        chunkCaches[chunkIndex].instances = BuildTileChunkInstances(simulationRuntime.mapWidth(), simulationRuntime.mapHeight(), chunkCaches[chunkIndex].chunkRect);
        chunkCaches[chunkIndex].instanceCount = static_cast<GLsizei>(chunkCaches[chunkIndex].instances.size());
        glBindBuffer(GL_ARRAY_BUFFER, chunkCaches[chunkIndex].instanceBufferId);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(chunkCaches[chunkIndex].instances.size() * sizeof(TileInstanceData)),
            chunkCaches[chunkIndex].instances.empty() ? 0 : &chunkCaches[chunkIndex].instances[0],
            GL_STATIC_DRAW);
    }

    std::vector<RoadChunkRenderCache> roadChunkCaches(simulationRuntime.chunkLayout().size());
    for (chunkIndex = 0; chunkIndex < roadChunkCaches.size(); ++chunkIndex) {
        roadChunkCaches[chunkIndex].chunkRect = simulationRuntime.chunkLayout()[chunkIndex];
        roadChunkCaches[chunkIndex].worldBounds = BuildChunkBounds(roadChunkCaches[chunkIndex].chunkRect);
        glGenVertexArrays(1, &roadChunkCaches[chunkIndex].vertexArrayId);
        glGenBuffers(1, &roadChunkCaches[chunkIndex].instanceBufferId);
        ConfigureRoadChunkVertexArray(roadChunkCaches[chunkIndex].vertexArrayId, tileVertexBufferId, roadChunkCaches[chunkIndex].instanceBufferId);
    }

    GLuint roadGhostVertexArrayId = 0;
    GLuint roadGhostInstanceBufferId = 0;
    glGenVertexArrays(1, &roadGhostVertexArrayId);
    glGenBuffers(1, &roadGhostInstanceBufferId);
    ConfigureRoadChunkVertexArray(roadGhostVertexArrayId, tileVertexBufferId, roadGhostInstanceBufferId);

    GLuint routeArrowVertexArrayId = 0;
    GLuint routeArrowInstanceBufferId = 0;
    glGenVertexArrays(1, &routeArrowVertexArrayId);
    glGenBuffers(1, &routeArrowInstanceBufferId);
    ConfigureRouteArrowVertexArray(routeArrowVertexArrayId, tileVertexBufferId, routeArrowInstanceBufferId);

    GLuint lotVertexArrayId = 0;
    GLuint lotInstanceBufferId = 0;
    glGenVertexArrays(1, &lotVertexArrayId);
    glGenBuffers(1, &lotInstanceBufferId);
    ConfigureLotVertexArray(lotVertexArrayId, lotVertexBufferId, lotInstanceBufferId);

    GLuint lotGhostVertexArrayId = 0;
    GLuint lotGhostInstanceBufferId = 0;
    glGenVertexArrays(1, &lotGhostVertexArrayId);
    glGenBuffers(1, &lotGhostInstanceBufferId);
    ConfigureLotVertexArray(lotGhostVertexArrayId, lotVertexBufferId, lotGhostInstanceBufferId);

    GLuint regionPreviewVertexArrayId = 0;
    GLuint regionPreviewInstanceBufferId = 0;
    glGenVertexArrays(1, &regionPreviewVertexArrayId);
    glGenBuffers(1, &regionPreviewInstanceBufferId);
    ConfigureRegionPreviewVertexArray(regionPreviewVertexArrayId, tileVertexBufferId, regionPreviewInstanceBufferId);

    GLuint cityPreviewFramebufferId = 0;
    GLuint cityPreviewColorTextureId = 0;
    GLuint cityPreviewDepthRenderbufferId = 0;
    glGenFramebuffers(1, &cityPreviewFramebufferId);
    glBindFramebuffer(GL_FRAMEBUFFER, cityPreviewFramebufferId);
    glGenTextures(1, &cityPreviewColorTextureId);
    glBindTexture(GL_TEXTURE_2D, cityPreviewColorTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, City::kPreviewWidth, City::kPreviewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cityPreviewColorTextureId, 0);
    glGenRenderbuffers(1, &cityPreviewDepthRenderbufferId);
    glBindRenderbuffer(GL_RENDERBUFFER, cityPreviewDepthRenderbufferId);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, City::kPreviewWidth, City::kPreviewHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, cityPreviewDepthRenderbufferId);
    const bool cityPreviewFramebufferComplete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!cityPreviewFramebufferComplete) {
        std::cerr << "City preview framebuffer is incomplete." << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ShaderProgram shaderProgram;
    if (!shaderProgram.loadFromFile(BuildShaderPath())) {
        DestroyTileChunkCaches(chunkCaches);
        DestroyRoadChunkCaches(roadChunkCaches);
        glDeleteRenderbuffers(1, &cityPreviewDepthRenderbufferId);
        glDeleteTextures(1, &cityPreviewColorTextureId);
        glDeleteFramebuffers(1, &cityPreviewFramebufferId);
        glDeleteBuffers(1, &regionPreviewInstanceBufferId);
        glDeleteVertexArrays(1, &regionPreviewVertexArrayId);
        glDeleteBuffers(1, &roadGhostInstanceBufferId);
        glDeleteVertexArrays(1, &roadGhostVertexArrayId);
        glDeleteBuffers(1, &routeArrowInstanceBufferId);
        glDeleteVertexArrays(1, &routeArrowVertexArrayId);
        glDeleteBuffers(1, &lotGhostInstanceBufferId);
        glDeleteVertexArrays(1, &lotGhostVertexArrayId);
        glDeleteBuffers(1, &lotInstanceBufferId);
        glDeleteVertexArrays(1, &lotVertexArrayId);
        glDeleteTextures(1, &roadArrowAtlasTextureId);
        glDeleteTextures(1, &roadBaseAtlasTextureId);
        glDeleteTextures(1, &tileOverlayTextureId);
        glDeleteTextures(1, &groundRoadStateTextureId);
        glDeleteTextures(1, &tileLiftTextureId);
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
    const GLint tileLiftTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uTileLiftTexture");
    const GLint tileOverlayTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uTileOverlayTexture");
    const GLint groundRoadStateTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uGroundRoadStateTexture");
    const GLint roadBaseAtlasTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadBaseAtlasTexture");
    const GLint roadArrowAtlasTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadArrowAtlasTexture");
    const GLint roadAtlasGridLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadAtlasGrid");
    const GLint roadAlphaScaleLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadAlphaScale");
    const GLint roadTintColorLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadTintColor");
    const GLint roadTintStrengthLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadTintStrength");
    const GLint lotAlphaScaleLocation = glGetUniformLocation(shaderProgram.programId(), "uLotAlphaScale");
    const GLint lotTintColorLocation = glGetUniformLocation(shaderProgram.programId(), "uLotTintColor");
    const GLint lotTintStrengthLocation = glGetUniformLocation(shaderProgram.programId(), "uLotTintStrength");
    const GLint regionPreviewTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uRegionPreviewTexture");
    glUniform1i(tileTextureLocation, 0);
    glUniform1i(groundRoadStateTextureLocation, 1);
    glUniform1i(roadBaseAtlasTextureLocation, 2);
    glUniform1i(roadArrowAtlasTextureLocation, 3);
    glUniform1i(tileLiftTextureLocation, 4);
    glUniform1i(tileOverlayTextureLocation, 5);
    glUniform1i(regionPreviewTextureLocation, 6);
    glUniform2f(roadAtlasGridLocation, static_cast<float>(kRoadAtlasColumns), static_cast<float>(kRoadAtlasRows));
    glUniform1f(roadAlphaScaleLocation, 1.0f);
    glUniform3f(roadTintColorLocation, 1.0f, 1.0f, 1.0f);
    glUniform1f(roadTintStrengthLocation, 0.0f);
    glUniform1f(lotAlphaScaleLocation, 1.0f);
    glUniform3f(lotTintColorLocation, 1.0f, 1.0f, 1.0f);
    glUniform1f(lotTintStrengthLocation, 0.0f);

    std::vector<GLshort> tileStateChunkPixels;
    std::vector<std::uint8_t> tileLiftChunkPixels;
    std::vector<std::uint8_t> emptyGroundRoadChunkPixels;
    std::vector<LotInstanceData> lotInstances;
    std::vector<LotInstanceData> lotGhostInstances;
    std::vector<RoadInstanceData> roadGhostInstances;
    std::vector<RouteArrowInstanceData> routeArrowInstances;
    std::vector<RegionPreviewTextureCache> regionPreviewTextureCaches;
    std::uint64_t lastUploadedLotRevision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t lastUploadedQueryRouteRevision = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::uint64_t> lastUploadedGroundRoadChunkRevisions(chunkCaches.size(), std::numeric_limits<std::uint64_t>::max());
    std::vector<std::uint64_t> lastUploadedTileOverlayChunkRevisions(chunkCaches.size(), std::numeric_limits<std::uint64_t>::max());
    bool lastFrameWasRegion = true;
    std::uint64_t lastHandledRenderStateRevision = gameSession_.renderStateRevision();
    std::uint64_t lastRegionPreviewCacheRevision = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::uint8_t> cityPreviewReadPixels(static_cast<std::size_t>(City::kPreviewWidth) * static_cast<std::size_t>(City::kPreviewHeight) * 4u, 0u);

    auto invalidateCityRenderCaches = [&] () {
        std::size_t cacheIndex = 0;
        for (; cacheIndex < chunkCaches.size(); ++cacheIndex) {
            chunkCaches[cacheIndex].lastUploadedTileStateGeneration = std::numeric_limits<std::uint64_t>::max();
            chunkCaches[cacheIndex].lastUploadedLiftRevision = std::numeric_limits<std::uint64_t>::max();
        }

        for (cacheIndex = 0; cacheIndex < roadChunkCaches.size(); ++cacheIndex) {
            roadChunkCaches[cacheIndex].lastUploadedRevision = std::numeric_limits<std::uint64_t>::max();
            roadChunkCaches[cacheIndex].instanceCount = 0;
            roadChunkCaches[cacheIndex].instances.clear();
        }

        lastUploadedGroundRoadChunkRevisions.assign(chunkCaches.size(), std::numeric_limits<std::uint64_t>::max());
        lastUploadedTileOverlayChunkRevisions.assign(chunkCaches.size(), std::numeric_limits<std::uint64_t>::max());
        lastUploadedLotRevision = std::numeric_limits<std::uint64_t>::max();
        lastUploadedQueryRouteRevision = std::numeric_limits<std::uint64_t>::max();
        lotInstances.clear();
        lotGhostInstances.clear();
        roadGhostInstances.clear();
        routeArrowInstances.clear();
    };

    auto renderCityPreview = [&] (City& city) -> bool {
        if (!cityPreviewFramebufferComplete) {
            return false;
        }

        simulationRuntime.stop();
        simulationRuntime.importCitySaveState(city.saveState());
        const PublishedWorldSnapshot snapshot = simulationRuntime.acquirePublishedSnapshot();
        if (snapshot.tiles == 0 || snapshot.chunkRevisions == 0) {
            simulationRuntime.releasePublishedSnapshot(snapshot);
            return false;
        }

        const CameraState previewCameraState = BuildTopDownOrthographicCameraState(
            static_cast<float>(simulationRuntime.mapWidth()) * 0.5f,
            static_cast<float>(simulationRuntime.mapHeight()) * 0.5f,
            static_cast<float>(simulationRuntime.mapWidth()),
            static_cast<float>(simulationRuntime.mapHeight()),
            City::kPreviewWidth,
            City::kPreviewHeight,
            0.0f);

        glBindFramebuffer(GL_FRAMEBUFFER, cityPreviewFramebufferId);
        glViewport(0, 0, City::kPreviewWidth, City::kPreviewHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.08f, 0.11f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        std::size_t uploadChunkIndex = 0;
        for (; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
            const ChunkRect& chunkRect = chunkCaches[uploadChunkIndex].chunkRect;
            FillTileStateChunkPixels(snapshot, chunkRect, tileStateChunkPixels);
            UploadTileStateChunkTexture(tileStateTextureId, chunkRect, tileStateChunkPixels);
            FillTileLiftChunkPixels(snapshot, chunkRect, tileLiftChunkPixels);
            UploadTileLiftChunkTexture(tileLiftTextureId, chunkRect, tileLiftChunkPixels);
            if (snapshot.groundRoadRenderState != 0 && !snapshot.groundRoadRenderState->empty()) {
                UpdateGroundRoadChunkTexture(groundRoadStateTextureId, snapshot, chunkRect);
            } else {
                UploadEmptyGroundRoadChunkTexture(groundRoadStateTextureId, chunkRect, emptyGroundRoadChunkPixels);
            }
        }

        for (uploadChunkIndex = 0; uploadChunkIndex < roadChunkCaches.size(); ++uploadChunkIndex) {
            roadChunkCaches[uploadChunkIndex].instances = BuildRoadChunkInstances(snapshot, roadChunkCaches[uploadChunkIndex].chunkRect);
            roadChunkCaches[uploadChunkIndex].instanceCount = static_cast<GLsizei>(roadChunkCaches[uploadChunkIndex].instances.size());
            glBindBuffer(GL_ARRAY_BUFFER, roadChunkCaches[uploadChunkIndex].instanceBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(roadChunkCaches[uploadChunkIndex].instances.size() * sizeof(RoadInstanceData)),
                roadChunkCaches[uploadChunkIndex].instances.empty() ? 0 : &roadChunkCaches[uploadChunkIndex].instances[0],
                GL_DYNAMIC_DRAW);
        }

        if (snapshot.lots != 0) {
            lotInstances = BuildLotInstances(*snapshot.lots);
            glBindBuffer(GL_ARRAY_BUFFER, lotInstanceBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(lotInstances.size() * sizeof(LotInstanceData)),
                lotInstances.empty() ? 0 : &lotInstances[0],
                GL_DYNAMIC_DRAW);
        } else {
            lotInstances.clear();
        }

        shaderProgram.bind();
        glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, previewCameraState.viewProjection.data);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tileStateTextureId);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, groundRoadStateTextureId);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, roadBaseAtlasTextureId);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, roadArrowAtlasTextureId);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, tileLiftTextureId);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, tileOverlayTextureId);

        glUniform1i(renderModeLocation, 0);
        for (uploadChunkIndex = 0; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
            const TileChunkRenderCache& cache = chunkCaches[uploadChunkIndex];
            glBindVertexArray(cache.vertexArrayId);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, cache.instanceCount);
        }

        glUniform1i(renderModeLocation, 2);
        glUniform1f(roadAlphaScaleLocation, 1.0f);
        glUniform3f(roadTintColorLocation, 1.0f, 1.0f, 1.0f);
        glUniform1f(roadTintStrengthLocation, 0.0f);
        for (uploadChunkIndex = 0; uploadChunkIndex < roadChunkCaches.size(); ++uploadChunkIndex) {
            const RoadChunkRenderCache& roadCache = roadChunkCaches[uploadChunkIndex];
            if (roadCache.instanceCount == 0) {
                continue;
            }

            glBindVertexArray(roadCache.vertexArrayId);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, roadCache.instanceCount);
        }

        if (!lotInstances.empty()) {
            glUniform1i(renderModeLocation, 1);
            glUniform1f(lotAlphaScaleLocation, 1.0f);
            glUniform3f(lotTintColorLocation, 1.0f, 1.0f, 1.0f);
            glUniform1f(lotTintStrengthLocation, 0.0f);
            glBindVertexArray(lotVertexArrayId);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(lotInstances.size()));
        }

        glBindVertexArray(0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, City::kPreviewWidth, City::kPreviewHeight, GL_RGBA, GL_UNSIGNED_BYTE, &cityPreviewReadPixels[0]);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        city.setPreviewPixels(cityPreviewReadPixels);
        simulationRuntime.releasePublishedSnapshot(snapshot);
        return true;
    };

    int renderedFrames = 0;
    RendererFrameMetrics lastFrameMetrics;
    std::chrono::steady_clock::time_point counterStart = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        appController_.setFramebufferSize(framebufferWidth, framebufferHeight);

        if (gameSession_.isLoading()) {
            glfwPollEvents();
            continue;
        }

        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.08f, 0.11f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const ViewState viewState = appController_.viewState();
        if (gameSession_.isRegionMode()) {
            const CameraState regionCameraState = BuildRegionCameraState(viewState, gameSession_.region());
            int hoveredRegionX = 0;
            int hoveredRegionY = 0;
            const bool hasHoveredRegion = TryPickRegionCity(viewState, regionCameraState, gameSession_.region(), hoveredRegionX, hoveredRegionY);
            appController_.setHoveredRegionCity(hoveredRegionX, hoveredRegionY, hasHoveredRegion);

            std::size_t previewCityIndex = 0;
            for (; previewCityIndex < gameSession_.region().cities().size(); ++previewCityIndex) {
                City& city = *gameSession_.region().cities()[previewCityIndex];
                if (!city.hasPreviewPixels()) {
                    renderCityPreview(city);
                }
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.08f, 0.11f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            if (lastRegionPreviewCacheRevision != gameSession_.region().revision()) {
                DestroyRegionPreviewTextureCaches(regionPreviewTextureCaches);
                lastRegionPreviewCacheRevision = gameSession_.region().revision();
            }
            SynchronizeRegionPreviewTextures(gameSession_.region(), regionPreviewTextureCaches);
            shaderProgram.bind();
            glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, regionCameraState.viewProjection.data);
            glUniform1i(renderModeLocation, 5);
            glActiveTexture(GL_TEXTURE6);

            std::size_t cityIndex = 0;
            for (; cityIndex < gameSession_.region().cities().size(); ++cityIndex) {
                const City& city = *gameSession_.region().cities()[cityIndex];
                if (cityIndex >= regionPreviewTextureCaches.size() || regionPreviewTextureCaches[cityIndex].textureId == 0) {
                    continue;
                }

                const Vec3 origin = RegionCityOrigin(city);
                RegionPreviewInstanceData instance;
                instance.originX = origin.x;
                instance.originZ = origin.z;
                instance.sizeX = kRegionCellWorldSize;
                instance.sizeZ = kRegionCellWorldSize;

                glBindTexture(GL_TEXTURE_2D, regionPreviewTextureCaches[cityIndex].textureId);
                glBindBuffer(GL_ARRAY_BUFFER, regionPreviewInstanceBufferId);
                glBufferData(GL_ARRAY_BUFFER, sizeof(RegionPreviewInstanceData), &instance, GL_DYNAMIC_DRAW);
                glBindVertexArray(regionPreviewVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
            }

            glBindVertexArray(0);
            appController_.processPendingRegionClick();
            glfwSwapBuffers(window);
            glfwPollEvents();
            lastFrameWasRegion = true;
            continue;
        }

        const std::uint64_t renderStateRevision = gameSession_.renderStateRevision();
        if (lastFrameWasRegion || lastHandledRenderStateRevision != renderStateRevision) {
            invalidateCityRenderCaches();
            lastFrameWasRegion = false;
            lastHandledRenderStateRevision = renderStateRevision;
        }

        const CameraState cameraState = BuildCameraState(viewState);
        const PublishedWorldSnapshot snapshot = simulationRuntime.acquirePublishedSnapshot();

        int hoveredTileX = 0;
        int hoveredTileY = 0;
        const bool hasHoveredTile = TryPickGroundTile(viewState, cameraState, simulationRuntime.mapWidth(), simulationRuntime.mapHeight(), hoveredTileX, hoveredTileY);
        appController_.setHoveredTile(hoveredTileX, hoveredTileY, hasHoveredTile);

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS) {
            appController_.onLeftMouseButtonHeld();
        }

        RendererFrameMetrics frameMetrics;
        frameMetrics.totalChunkCount = static_cast<int>(chunkCaches.size());

        RoadStrokeCommand roadGhostCommand;
        if (appController_.roadPreviewStroke(roadGhostCommand)) {
            const std::chrono::steady_clock::time_point roadGhostUploadStart = std::chrono::steady_clock::now();
            roadGhostInstances = BuildRoadPreviewInstances(roadGhostCommand, simulationRuntime.mapWidth(), simulationRuntime.mapHeight());
            glBindBuffer(GL_ARRAY_BUFFER, roadGhostInstanceBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(roadGhostInstances.size() * sizeof(RoadInstanceData)),
                roadGhostInstances.empty() ? 0 : &roadGhostInstances[0],
                GL_DYNAMIC_DRAW);
            frameMetrics.roadGhostUploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - roadGhostUploadStart).count();
            frameMetrics.roadGhostInstanceCount = static_cast<int>(roadGhostInstances.size());
        } else {
            roadGhostInstances.clear();
        }

        std::string lotGhostAssetId;
        int lotGhostTileX = 0;
        int lotGhostTileY = 0;
        int lotGhostRotationSteps = 0;
        if (appController_.lotPreviewRequest(lotGhostAssetId, lotGhostTileX, lotGhostTileY, lotGhostRotationSteps)) {
            const std::chrono::steady_clock::time_point lotGhostUploadStart = std::chrono::steady_clock::now();
            LotRenderInstance lotGhostRenderInstance;
            lotGhostInstances.clear();
            if (simulationRuntime.buildLotPreviewInstance(lotGhostAssetId, lotGhostTileX, lotGhostTileY, lotGhostRotationSteps, lotGhostRenderInstance)) {
                lotGhostInstances.push_back(BuildLotInstance(lotGhostRenderInstance));
            }
            glBindBuffer(GL_ARRAY_BUFFER, lotGhostInstanceBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(lotGhostInstances.size() * sizeof(LotInstanceData)),
                lotGhostInstances.empty() ? 0 : &lotGhostInstances[0],
                GL_DYNAMIC_DRAW);
            frameMetrics.lotGhostUploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lotGhostUploadStart).count();
            frameMetrics.lotGhostInstanceCount = static_cast<int>(lotGhostInstances.size());
        } else {
            lotGhostInstances.clear();
        }

        if (viewState.queryRouteRevision != lastUploadedQueryRouteRevision) {
            routeArrowInstances = BuildRouteArrowInstances(viewState.queriedCommuteRouteSegments);
            glBindBuffer(GL_ARRAY_BUFFER, routeArrowInstanceBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(routeArrowInstances.size() * sizeof(RouteArrowInstanceData)),
                routeArrowInstances.empty() ? 0 : &routeArrowInstances[0],
                GL_DYNAMIC_DRAW);
            lastUploadedQueryRouteRevision = viewState.queryRouteRevision;
        }

        if (snapshot.tiles != 0 && snapshot.chunkRevisions != 0) {
            const std::chrono::steady_clock::time_point cullStart = std::chrono::steady_clock::now();
            std::vector<std::size_t> visibleChunkIndices;
            visibleChunkIndices.reserve(chunkCaches.size());
            std::vector<unsigned char> visibleChunkFlags(chunkCaches.size(), 0u);

            std::size_t cullChunkIndex = 0;
            for (; cullChunkIndex < chunkCaches.size(); ++cullChunkIndex) {
                if (!IntersectsFrustum(cameraState.frustum, chunkCaches[cullChunkIndex].worldBounds)) {
                    continue;
                }

                visibleChunkIndices.push_back(cullChunkIndex);
                visibleChunkFlags[cullChunkIndex] = 1u;
            }

            frameMetrics.visibleChunkCount = static_cast<int>(visibleChunkIndices.size());
            frameMetrics.cullMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - cullStart).count();

            std::size_t uploadChunkIndex = 0;
            for (; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
                if (chunkCaches[uploadChunkIndex].lastUploadedTileStateGeneration == snapshot.generation) {
                    continue;
                }

                if (visibleChunkFlags[uploadChunkIndex] == 0u) {
                    ++frameMetrics.tileStateDeferredChunkCount;
                    continue;
                }

                const ChunkRect& chunkRect = chunkCaches[uploadChunkIndex].chunkRect;
                const std::chrono::steady_clock::time_point packStart = std::chrono::steady_clock::now();
                FillTileStateChunkPixels(snapshot, chunkRect, tileStateChunkPixels);
                frameMetrics.tileStatePackMicros += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - packStart).count();

                const std::chrono::steady_clock::time_point uploadStart = std::chrono::steady_clock::now();
                UploadTileStateChunkTexture(tileStateTextureId, chunkRect, tileStateChunkPixels);
                frameMetrics.tileStateUploadMicros += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - uploadStart).count();

                const long long uploadedTiles = static_cast<long long>(chunkRect.width) * static_cast<long long>(chunkRect.height);
                frameMetrics.tileStateUploadedTileCount += uploadedTiles;
                frameMetrics.tileStateUploadedBytes += uploadedTiles * 2ll * static_cast<long long>(sizeof(GLshort));
                ++frameMetrics.tileStateUploadedChunkCount;
                chunkCaches[uploadChunkIndex].lastUploadedTileStateGeneration = snapshot.generation;
            }
            frameMetrics.tileUploadMicros += frameMetrics.tileStatePackMicros + frameMetrics.tileStateUploadMicros;

            if (snapshot.lotOccupancy != 0) {
                const std::chrono::steady_clock::time_point liftUploadStart = std::chrono::steady_clock::now();
                for (uploadChunkIndex = 0; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
                    const std::uint64_t publishedRevision = (*snapshot.chunkRevisions)[uploadChunkIndex];
                    if (chunkCaches[uploadChunkIndex].lastUploadedLiftRevision == publishedRevision) {
                        continue;
                    }

                    if (visibleChunkFlags[uploadChunkIndex] == 0u) {
                        ++frameMetrics.tileLiftDeferredChunkCount;
                        continue;
                    }

                    const ChunkRect& chunkRect = chunkCaches[uploadChunkIndex].chunkRect;
                    FillTileLiftChunkPixels(snapshot, chunkRect, tileLiftChunkPixels);
                    UploadTileLiftChunkTexture(tileLiftTextureId, chunkRect, tileLiftChunkPixels);
                    chunkCaches[uploadChunkIndex].lastUploadedLiftRevision = publishedRevision;

                    const long long uploadedTiles = static_cast<long long>(chunkRect.width) * static_cast<long long>(chunkRect.height);
                    frameMetrics.tileLiftUploadedTileCount += uploadedTiles;
                    frameMetrics.tileLiftUploadedBytes += uploadedTiles;
                    ++frameMetrics.tileLiftUploadedChunkCount;
                }
                frameMetrics.tileLiftUploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - liftUploadStart).count();
                frameMetrics.tileUploadMicros += frameMetrics.tileLiftUploadMicros;
            }

            const std::chrono::steady_clock::time_point groundRoadUploadStart = std::chrono::steady_clock::now();
            if (snapshot.groundRoadRenderState != 0 && snapshot.groundRoadChunkRevisions != 0) {
                for (uploadChunkIndex = 0; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
                    const std::uint64_t publishedRevision = (*snapshot.groundRoadChunkRevisions)[uploadChunkIndex];
                    if (lastUploadedGroundRoadChunkRevisions[uploadChunkIndex] == publishedRevision) {
                        continue;
                    }

                    if (visibleChunkFlags[uploadChunkIndex] == 0u) {
                        ++frameMetrics.deferredGroundRoadChunkCount;
                        continue;
                    }

                    UpdateGroundRoadChunkTexture(groundRoadStateTextureId, snapshot, chunkCaches[uploadChunkIndex].chunkRect);
                    lastUploadedGroundRoadChunkRevisions[uploadChunkIndex] = publishedRevision;
                    ++frameMetrics.dirtyGroundRoadChunkCount;
                }
            }
            frameMetrics.groundRoadUploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - groundRoadUploadStart).count();

            const std::chrono::steady_clock::time_point tileOverlayUploadStart = std::chrono::steady_clock::now();
            if (viewState.overlayMode != OverlayMode::None && snapshot.tileOverlayState != 0 && snapshot.tileOverlayChunkRevisions != 0) {
                for (uploadChunkIndex = 0; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
                    const std::uint64_t publishedRevision = (*snapshot.tileOverlayChunkRevisions)[uploadChunkIndex];
                    if (lastUploadedTileOverlayChunkRevisions[uploadChunkIndex] == publishedRevision) {
                        continue;
                    }

                    if (visibleChunkFlags[uploadChunkIndex] == 0u) {
                        ++frameMetrics.tileOverlayDeferredChunkCount;
                        continue;
                    }

                    UpdateTileOverlayChunkTexture(tileOverlayTextureId, snapshot, chunkCaches[uploadChunkIndex].chunkRect);
                    lastUploadedTileOverlayChunkRevisions[uploadChunkIndex] = publishedRevision;
                    ++frameMetrics.tileOverlayUploadedChunkCount;
                }
            }
            frameMetrics.tileOverlayUploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tileOverlayUploadStart).count();

            const std::chrono::steady_clock::time_point elevatedRoadUploadStart = std::chrono::steady_clock::now();
            if (snapshot.roads != 0 && snapshot.elevatedRoadChunkRevisions != 0) {
                for (uploadChunkIndex = 0; uploadChunkIndex < roadChunkCaches.size(); ++uploadChunkIndex) {
                    const std::uint64_t publishedRevision = (*snapshot.elevatedRoadChunkRevisions)[uploadChunkIndex];
                    if (roadChunkCaches[uploadChunkIndex].lastUploadedRevision == publishedRevision) {
                        continue;
                    }

                    if (visibleChunkFlags[uploadChunkIndex] == 0u) {
                        ++frameMetrics.deferredElevatedRoadChunkCount;
                        continue;
                    }

                    roadChunkCaches[uploadChunkIndex].instances = BuildRoadChunkInstances(snapshot, roadChunkCaches[uploadChunkIndex].chunkRect);
                    roadChunkCaches[uploadChunkIndex].instanceCount = static_cast<GLsizei>(roadChunkCaches[uploadChunkIndex].instances.size());
                    roadChunkCaches[uploadChunkIndex].lastUploadedRevision = publishedRevision;

                    glBindBuffer(GL_ARRAY_BUFFER, roadChunkCaches[uploadChunkIndex].instanceBufferId);
                    glBufferData(
                        GL_ARRAY_BUFFER,
                        static_cast<GLsizeiptr>(roadChunkCaches[uploadChunkIndex].instances.size() * sizeof(RoadInstanceData)),
                        roadChunkCaches[uploadChunkIndex].instances.empty() ? 0 : &roadChunkCaches[uploadChunkIndex].instances[0],
                        GL_DYNAMIC_DRAW);
                    ++frameMetrics.dirtyElevatedRoadChunkCount;
                }
            }
            frameMetrics.elevatedRoadUploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - elevatedRoadUploadStart).count();

            if (snapshot.lots != 0 && snapshot.lotRevision != lastUploadedLotRevision) {
                const std::chrono::steady_clock::time_point lotUploadStart = std::chrono::steady_clock::now();
                lotInstances = BuildLotInstances(*snapshot.lots);
                glBindBuffer(GL_ARRAY_BUFFER, lotInstanceBufferId);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(lotInstances.size() * sizeof(LotInstanceData)),
                    lotInstances.empty() ? 0 : &lotInstances[0],
                    GL_DYNAMIC_DRAW);
                lastUploadedLotRevision = snapshot.lotRevision;
                frameMetrics.lotUploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lotUploadStart).count();
            }

            shaderProgram.bind();
            glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, cameraState.viewProjection.data);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tileStateTextureId);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, groundRoadStateTextureId);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, roadBaseAtlasTextureId);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, roadArrowAtlasTextureId);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, tileLiftTextureId);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, tileOverlayTextureId);

            glUniform1i(renderModeLocation, 0);
            const std::chrono::steady_clock::time_point tileDrawStart = std::chrono::steady_clock::now();
            std::size_t visibleIndex = 0;
            for (; visibleIndex < visibleChunkIndices.size(); ++visibleIndex) {
                const TileChunkRenderCache& cache = chunkCaches[visibleChunkIndices[visibleIndex]];
                glBindVertexArray(cache.vertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, cache.instanceCount);
            }
            frameMetrics.tileDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tileDrawStart).count();

            glUniform1i(renderModeLocation, 2);
            glUniform1f(roadAlphaScaleLocation, 1.0f);
            glUniform3f(roadTintColorLocation, 1.0f, 1.0f, 1.0f);
            glUniform1f(roadTintStrengthLocation, 0.0f);
            const std::chrono::steady_clock::time_point elevatedRoadDrawStart = std::chrono::steady_clock::now();
            for (visibleIndex = 0; visibleIndex < visibleChunkIndices.size(); ++visibleIndex) {
                const RoadChunkRenderCache& roadCache = roadChunkCaches[visibleChunkIndices[visibleIndex]];
                if (roadCache.instanceCount == 0) {
                    continue;
                }

                glBindVertexArray(roadCache.vertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, roadCache.instanceCount);
                ++frameMetrics.visibleElevatedRoadChunkCount;
            }
            frameMetrics.elevatedRoadDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - elevatedRoadDrawStart).count();

            if (!roadGhostInstances.empty()) {
                glUniform1f(roadAlphaScaleLocation, kRoadGhostAlpha);
                glUniform3f(roadTintColorLocation, 0.55f, 0.82f, 1.0f);
                glUniform1f(roadTintStrengthLocation, 0.38f);
                glDepthMask(GL_FALSE);
                const std::chrono::steady_clock::time_point roadGhostDrawStart = std::chrono::steady_clock::now();
                glBindVertexArray(roadGhostVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(roadGhostInstances.size()));
                frameMetrics.roadGhostDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - roadGhostDrawStart).count();
                glDepthMask(GL_TRUE);
                glUniform1f(roadAlphaScaleLocation, 1.0f);
                glUniform3f(roadTintColorLocation, 1.0f, 1.0f, 1.0f);
                glUniform1f(roadTintStrengthLocation, 0.0f);
            }

            if (!lotGhostInstances.empty()) {
                glUniform1i(renderModeLocation, 1);
                glUniform1f(lotAlphaScaleLocation, kLotGhostAlpha);
                glUniform3f(lotTintColorLocation, 0.76f, 1.0f, 0.58f);
                glUniform1f(lotTintStrengthLocation, 0.42f);
                glDepthMask(GL_FALSE);
                const std::chrono::steady_clock::time_point lotGhostDrawStart = std::chrono::steady_clock::now();
                glBindVertexArray(lotGhostVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(lotGhostInstances.size()));
                frameMetrics.lotGhostDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lotGhostDrawStart).count();
                glDepthMask(GL_TRUE);
                glUniform1f(lotAlphaScaleLocation, 1.0f);
                glUniform3f(lotTintColorLocation, 1.0f, 1.0f, 1.0f);
                glUniform1f(lotTintStrengthLocation, 0.0f);
            }

            if (!lotInstances.empty()) {
                glUniform1i(renderModeLocation, 1);
                glUniform1f(lotAlphaScaleLocation, 1.0f);
                glUniform3f(lotTintColorLocation, 1.0f, 1.0f, 1.0f);
                glUniform1f(lotTintStrengthLocation, 0.0f);
                const std::chrono::steady_clock::time_point lotDrawStart = std::chrono::steady_clock::now();
                glBindVertexArray(lotVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(lotInstances.size()));
                frameMetrics.lotDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lotDrawStart).count();
            }

            if (viewState.overlayMode != OverlayMode::None && snapshot.tileOverlayState != 0) {
                glUniform1i(renderModeLocation, 3);
                glDepthMask(GL_FALSE);
                glDisable(GL_DEPTH_TEST);
                const std::chrono::steady_clock::time_point tileOverlayDrawStart = std::chrono::steady_clock::now();
                for (visibleIndex = 0; visibleIndex < visibleChunkIndices.size(); ++visibleIndex) {
                    const TileChunkRenderCache& cache = chunkCaches[visibleChunkIndices[visibleIndex]];
                    glBindVertexArray(cache.vertexArrayId);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, cache.instanceCount);
                }
                frameMetrics.tileOverlayDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tileOverlayDrawStart).count();
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
            }

            if (!routeArrowInstances.empty()) {
                glUniform1i(renderModeLocation, 4);
                glDepthMask(GL_FALSE);
                glDisable(GL_DEPTH_TEST);
                glBindVertexArray(routeArrowVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(routeArrowInstances.size()));
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
            }

            glBindVertexArray(0);
        } else {
            appController_.setHoveredTile(0, 0, false);
        }

        simulationRuntime.releasePublishedSnapshot(snapshot);
        glfwSwapBuffers(window);
        glfwPollEvents();

        lastFrameMetrics = frameMetrics;
        ++renderedFrames;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - counterStart).count() >= 1) {
            const RuntimeTimingSnapshot runtimeTimings = simulationRuntime.timingSnapshot();
            std::cout
                << "FPS " << renderedFrames
                << " Updates " << simulationRuntime.updatesPerSecond()
                << " Sim(us) n=" << runtimeTimings.neighborPassMicros
                << " cmd=" << runtimeTimings.commandPassMicros
                << " lot=" << runtimeTimings.lotEffectsMicros
                << " local=" << runtimeTimings.localPassMicros
                << " pub=" << runtimeTimings.publishMicros
                << " wb=" << runtimeTimings.writeBufferWaitMicros
                << " Render(us) cull=" << lastFrameMetrics.cullMicros
                << " tileUp=" << lastFrameMetrics.tileUploadMicros
                << " tileState(pack/up)=" << lastFrameMetrics.tileStatePackMicros << "/" << lastFrameMetrics.tileStateUploadMicros
                << " tileStateChunks=" << lastFrameMetrics.tileStateUploadedChunkCount << " defer=" << lastFrameMetrics.tileStateDeferredChunkCount
                << " tileStateTiles=" << lastFrameMetrics.tileStateUploadedTileCount
                << " tileStateBytes=" << lastFrameMetrics.tileStateUploadedBytes
                << " liftUp=" << lastFrameMetrics.tileLiftUploadMicros
                << " liftChunks=" << lastFrameMetrics.tileLiftUploadedChunkCount << " defer=" << lastFrameMetrics.tileLiftDeferredChunkCount
                << " liftBytes=" << lastFrameMetrics.tileLiftUploadedBytes
                << " groundRoadUp=" << lastFrameMetrics.groundRoadUploadMicros
                << " overlayUp=" << lastFrameMetrics.tileOverlayUploadMicros
                << " overlayChunks=" << lastFrameMetrics.tileOverlayUploadedChunkCount << " defer=" << lastFrameMetrics.tileOverlayDeferredChunkCount
                << " elevRoadUp=" << lastFrameMetrics.elevatedRoadUploadMicros
                << " roadGhostUp=" << lastFrameMetrics.roadGhostUploadMicros
                << " lotUp=" << lastFrameMetrics.lotUploadMicros
                << " lotGhostUp=" << lastFrameMetrics.lotGhostUploadMicros
                << " tileDraw=" << lastFrameMetrics.tileDrawMicros
                << " elevRoadDraw=" << lastFrameMetrics.elevatedRoadDrawMicros
                << " roadGhostDraw=" << lastFrameMetrics.roadGhostDrawMicros
                << " overlayDraw=" << lastFrameMetrics.tileOverlayDrawMicros
                << " lotDraw=" << lastFrameMetrics.lotDrawMicros
                << " lotGhostDraw=" << lastFrameMetrics.lotGhostDrawMicros
                << " chunks=" << lastFrameMetrics.visibleChunkCount << "/" << lastFrameMetrics.totalChunkCount
                << " elevChunks=" << lastFrameMetrics.visibleElevatedRoadChunkCount
                << " roadGhostInstances=" << lastFrameMetrics.roadGhostInstanceCount
                << " lotGhostInstances=" << lastFrameMetrics.lotGhostInstanceCount
                << " dirtyGround=" << lastFrameMetrics.dirtyGroundRoadChunkCount << " deferGround=" << lastFrameMetrics.deferredGroundRoadChunkCount
                << " dirtyElev=" << lastFrameMetrics.dirtyElevatedRoadChunkCount << " deferElev=" << lastFrameMetrics.deferredElevatedRoadChunkCount
                << std::endl;
            renderedFrames = 0;
            counterStart = now;
        }
    }

    DestroyTileChunkCaches(chunkCaches);
    DestroyRoadChunkCaches(roadChunkCaches);
    DestroyRegionPreviewTextureCaches(regionPreviewTextureCaches);
    glDeleteRenderbuffers(1, &cityPreviewDepthRenderbufferId);
    glDeleteTextures(1, &cityPreviewColorTextureId);
    glDeleteFramebuffers(1, &cityPreviewFramebufferId);
    glDeleteBuffers(1, &regionPreviewInstanceBufferId);
    glDeleteVertexArrays(1, &regionPreviewVertexArrayId);
    glDeleteBuffers(1, &roadGhostInstanceBufferId);
    glDeleteVertexArrays(1, &roadGhostVertexArrayId);
    glDeleteBuffers(1, &routeArrowInstanceBufferId);
    glDeleteVertexArrays(1, &routeArrowVertexArrayId);
    glDeleteBuffers(1, &lotGhostInstanceBufferId);
    glDeleteVertexArrays(1, &lotGhostVertexArrayId);
    glDeleteBuffers(1, &lotInstanceBufferId);
    glDeleteVertexArrays(1, &lotVertexArrayId);
    glDeleteTextures(1, &roadArrowAtlasTextureId);
    glDeleteTextures(1, &roadBaseAtlasTextureId);
    glDeleteTextures(1, &tileOverlayTextureId);
    glDeleteTextures(1, &groundRoadStateTextureId);
    glDeleteTextures(1, &tileLiftTextureId);
    glDeleteTextures(1, &tileStateTextureId);
    glDeleteBuffers(1, &lotVertexBufferId);
    glDeleteBuffers(1, &tileVertexBufferId);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
