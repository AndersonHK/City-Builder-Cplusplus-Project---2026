#include "Renderer.h"

#include "RuntimePaths.h"

#include <GL/glew.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "City.h"
#include "CrashLogger.h"
#include "GeneratedMeshCatalog.h"
#include "LotMaterials.h"
#include "InGameWindow.h"
#include "LotRenderSurfacePatterns.h"
#include "RendererAlgorithms.h"
#include "RoadAtlas.h"
#include "RoadRenderState.h"
#include "ShaderProgram.h"
#include "SimulationDate.h"
#include "TransportCostMap.h"

namespace {
const float kPi = 3.14159265358979323846f;
const float kRoadGhostAlpha = 0.46f;
const float kLotGhostAlpha = 0.42f;
const float kRegionCellWorldSize = 1024.0f;
const float kRegionCellWorldGap = 0.0f;
const float kCityCameraMinimumNearPlane = 1.0f;
const float kCityCameraNearPlaneDistanceFactor = 0.025f;
const float kCityCameraMinimumFarPlane = 256.0f;
const float kCityCameraFarPlaneTilePadding = 2.25f;
const float kCityCameraFarPlaneWorldPadding = 128.0f;
const GLint kLegacyTileStateTextureInternalFormat = GL_RG16_SNORM;
const GLint kLegacyScalarOverlayTextureInternalFormat = GL_R16;
const GLenum kRendererScalarPayloadUploadFormat = GL_RED;
const GLenum kRendererScalarPayloadUploadType = GL_UNSIGNED_SHORT;
static_assert(kRendererScalarPayloadBitDepth == 16, "The legacy OpenGL scalar-overlay upload format must change with the renderer scalar payload bit depth.");
static_assert(kRendererSignedScalarPayloadBitDepth == 16, "The legacy OpenGL tile-state texture format must change with the signed scalar payload bit depth.");

RendererOverlaySemantic RendererOverlaySemanticForOverlayMode(OverlayMode overlayMode) noexcept {
    switch (overlayMode) {
    case OverlayMode::LandValue:
        return RendererOverlaySemantic::LandValue;
    case OverlayMode::ParkEffect:
        return RendererOverlaySemantic::ParkEffect;
    case OverlayMode::AirPollution:
        return RendererOverlaySemantic::AirPollution;
    case OverlayMode::RciDesirability:
        return RendererOverlaySemantic::RciDesirability;
    case OverlayMode::TrafficCapacity:
    case OverlayMode::None:
    case OverlayMode::Rci:
    default:
        return RendererOverlaySemantic::TrafficCapacity;
    }
}

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

    Plane()
        : a(0.0f),
          b(0.0f),
          c(0.0f),
          d(0.0f) {
    }

    Plane(float aValue, float bValue, float cValue, float dValue)
        : a(aValue),
          b(bValue),
          c(cValue),
          d(dValue) {
    }
};

struct Aabb {
    Vec3 minimum;
    Vec3 maximum;
};

struct Frustum {
    Plane planes[6];

    Frustum()
        : planes() {
    }
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

    CameraSpec()
        : target(),
          up(0.0f, 1.0f, 0.0f),
          yawRadians(0.0f),
          pitchRadians(0.0f),
          distance(0.0f),
          verticalFieldOfViewRadians(0.0f),
          orthographicHeight(0.0f),
          nearPlane(0.0f),
          farPlane(0.0f),
          projectionMode(CameraProjectionPerspective) {
    }
};

struct CameraState {
    Vec3 position;
    Vec3 target;
    Mat4 view;
    Mat4 projection;
    Mat4 viewProjection;
    Mat4 inverseViewProjection;
    Frustum frustum;

    CameraState()
        : position(),
          target(),
          view(),
          projection(),
          viewProjection(),
          inverseViewProjection(),
          frustum() {
    }
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
    float surfacePattern;
    float surfaceDirection;
    float padding0;
    float padding1;
};

struct LotMeshDrawBatch {
    std::uint16_t meshHandle;
    GLsizei firstVertex;
    GLsizei vertexCount;
    GLsizei instanceOffset;
    GLsizei instanceCount;

    LotMeshDrawBatch()
        : meshHandle(0u),
          firstVertex(0),
          vertexCount(0),
          instanceOffset(0),
          instanceCount(0) {
    }
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

struct AreaOverlayInstanceData {
    float originX;
    float originZ;
    float sizeX;
    float sizeZ;
    float colorR;
    float colorG;
    float colorB;
    float colorA;

    AreaOverlayInstanceData()
        : originX(0.0f),
          originZ(0.0f),
          sizeX(1.0f),
          sizeZ(1.0f),
          colorR(1.0f),
          colorG(0.0f),
          colorB(0.0f),
          colorA(0.25f) {
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

struct RoadPreviewValidationKey {
    bool active;
    Int2 startTile;
    Int2 cornerTile;
    Int2 endTile;
    RoadTemplateKind templateKind;
    RoadFamily family;
    TransportLayerId layer;
    RoadStrokeOperation operation;
    RoadTrafficSide trafficSide;
    RoadDirectionMode directionMode;
    int laneCount;
    std::uint16_t templateId;
    std::uint8_t templateFootprint;
    std::uint64_t validationRevision;

    RoadPreviewValidationKey()
        : active(false),
          templateKind(RoadTemplateKind::Street),
          family(RoadFamily::None),
          layer(TransportLayerId::Ground),
          operation(RoadStrokeOperation::Place),
          trafficSide(RoadTrafficSide::RightHand),
          directionMode(RoadDirectionMode::TwoWay),
          laneCount(0),
          templateId(0),
          templateFootprint(0),
          validationRevision(0) {
    }
};

struct RciPreviewValidationKey {
    bool active;
    int activeTool;
    int startTileX;
    int startTileY;
    int currentTileX;
    int currentTileY;
    std::uint16_t zoningType;
    std::string dragToolId;
    std::string activeRciToolId;
    bool shiftModifierDown;
    bool controlModifierDown;
    std::uint64_t validationRevision;

    RciPreviewValidationKey()
        : active(false),
          activeTool(0),
          startTileX(0),
          startTileY(0),
          currentTileX(0),
          currentTileY(0),
          zoningType(TileZoningNone),
          shiftModifierDown(false),
          controlModifierDown(false),
          validationRevision(0) {
    }
};

struct LotPreviewValidationKey {
    bool active;
    std::string assetId;
    int tileX;
    int tileY;
    int rotationSteps;
    std::uint64_t validationRevision;

    LotPreviewValidationKey()
        : active(false),
          tileX(0),
          tileY(0),
          rotationSteps(0),
          validationRevision(0) {
    }
};

std::uint64_t MixPreviewValidationRevision(std::uint64_t seed, std::uint64_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

std::uint64_t BuildPreviewValidationRevision(std::uint64_t renderStateRevision, std::uint64_t lotRevision, std::uint64_t zoningLotRevision, std::uint64_t roadRevision) {
    std::uint64_t revision = MixPreviewValidationRevision(0u, renderStateRevision);
    revision = MixPreviewValidationRevision(revision, lotRevision);
    revision = MixPreviewValidationRevision(revision, zoningLotRevision);
    revision = MixPreviewValidationRevision(revision, roadRevision);
    return revision;
}

bool IsRciZoningTool(ActiveTool activeTool) {
    return activeTool == ActiveTool::ZoneRci ||
        activeTool == ActiveTool::ZoneUnzone;
}

RoadPreviewValidationKey BuildRoadPreviewValidationKey(const RoadStrokeCommand& roadStrokeCommand, std::uint64_t validationRevision) {
    RoadPreviewValidationKey key;
    key.active = true;
    key.startTile = roadStrokeCommand.startTile;
    key.cornerTile = roadStrokeCommand.cornerTile;
    key.endTile = roadStrokeCommand.endTile;
    key.templateKind = roadStrokeCommand.templateKind;
    key.family = roadStrokeCommand.family;
    key.layer = roadStrokeCommand.layer;
    key.operation = roadStrokeCommand.operation;
    key.trafficSide = roadStrokeCommand.roadTemplate.trafficSide;
    key.directionMode = roadStrokeCommand.roadTemplate.directionMode;
    key.laneCount = roadStrokeCommand.roadTemplate.laneCount;
    key.templateId = roadStrokeCommand.roadTemplate.identity.id;
    key.templateFootprint = roadStrokeCommand.roadTemplate.identity.footprint;
    key.validationRevision = validationRevision;
    return key;
}

RciPreviewValidationKey BuildRciPreviewValidationKey(const ViewState& viewState, std::uint64_t validationRevision) {
    RciPreviewValidationKey key;
    key.active = viewState.zoneDragActive && IsRciZoningTool(viewState.activeTool);
    key.activeTool = static_cast<int>(viewState.activeTool);
    key.startTileX = viewState.zoneDragStartX;
    key.startTileY = viewState.zoneDragStartY;
    key.currentTileX = viewState.zoneDragCurrentX;
    key.currentTileY = viewState.zoneDragCurrentY;
    key.zoningType = viewState.zoneDragType;
    key.dragToolId = viewState.zoneDragToolId;
    key.activeRciToolId = viewState.activeRciToolId;
    key.shiftModifierDown = viewState.shiftModifierDown;
    key.controlModifierDown = viewState.controlModifierDown;
    key.validationRevision = validationRevision;
    return key;
}

LotPreviewValidationKey BuildLotPreviewValidationKey(const std::string& assetId, int tileX, int tileY, int rotationSteps, std::uint64_t validationRevision) {
    LotPreviewValidationKey key;
    key.active = true;
    key.assetId = assetId;
    key.tileX = tileX;
    key.tileY = tileY;
    key.rotationSteps = rotationSteps;
    key.validationRevision = validationRevision;
    return key;
}

bool SameRoadPreviewValidationKey(const RoadPreviewValidationKey& left, const RoadPreviewValidationKey& right) {
    return left.active == right.active &&
        left.startTile == right.startTile &&
        left.cornerTile == right.cornerTile &&
        left.endTile == right.endTile &&
        left.templateKind == right.templateKind &&
        left.family == right.family &&
        left.layer == right.layer &&
        left.operation == right.operation &&
        left.trafficSide == right.trafficSide &&
        left.directionMode == right.directionMode &&
        left.laneCount == right.laneCount &&
        left.templateId == right.templateId &&
        left.templateFootprint == right.templateFootprint &&
        left.validationRevision == right.validationRevision;
}

bool SameRciPreviewValidationKey(const RciPreviewValidationKey& left, const RciPreviewValidationKey& right) {
    return left.active == right.active &&
        left.activeTool == right.activeTool &&
        left.startTileX == right.startTileX &&
        left.startTileY == right.startTileY &&
        left.currentTileX == right.currentTileX &&
        left.currentTileY == right.currentTileY &&
        left.zoningType == right.zoningType &&
        left.dragToolId == right.dragToolId &&
        left.activeRciToolId == right.activeRciToolId &&
        left.shiftModifierDown == right.shiftModifierDown &&
        left.controlModifierDown == right.controlModifierDown &&
        left.validationRevision == right.validationRevision;
}

bool SameLotPreviewValidationKey(const LotPreviewValidationKey& left, const LotPreviewValidationKey& right) {
    return left.active == right.active &&
        left.assetId == right.assetId &&
        left.tileX == right.tileX &&
        left.tileY == right.tileY &&
        left.rotationSteps == right.rotationSteps &&
        left.validationRevision == right.validationRevision;
}

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

struct GlobalMemoryDeleter {
    void operator()(HGLOBAL handle) const {
        if (handle != 0) {
            GlobalFree(handle);
        }
    }
};

bool CopyBackBufferToClipboard(int framebufferWidth, int framebufferHeight) {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        return false;
    }

    const std::size_t width = static_cast<std::size_t>(framebufferWidth);
    const std::size_t height = static_cast<std::size_t>(framebufferHeight);
    const std::size_t bytesPerPixel = 4u;
    if (width > (std::numeric_limits<std::size_t>::max() / height) ||
        (width * height) > (std::numeric_limits<std::size_t>::max() / bytesPerPixel)) {
        return false;
    }

    const std::size_t pixelBytes = width * height * bytesPerPixel;
    const std::size_t dibBytes = sizeof(BITMAPINFOHEADER) + pixelBytes;
    if (pixelBytes > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) ||
        dibBytes > static_cast<std::size_t>(std::numeric_limits<SIZE_T>::max())) {
        return false;
    }

    std::vector<std::uint8_t> rgbaPixels(pixelBytes, 0u);
    GLint previousReadFramebuffer = 0;
    GLint previousReadBuffer = 0;
    GLint previousPackAlignment = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, framebufferWidth, framebufferHeight, GL_RGBA, GL_UNSIGNED_BYTE, &rgbaPixels[0]);
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFramebuffer);
    glReadBuffer(previousReadBuffer);

    std::unique_ptr<void, GlobalMemoryDeleter> clipboardMemory(
        GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(dibBytes)));
    if (clipboardMemory.get() == 0) {
        return false;
    }

    std::uint8_t* dib = static_cast<std::uint8_t*>(GlobalLock(clipboardMemory.get()));
    if (dib == 0) {
        return false;
    }

    BITMAPINFOHEADER* header = reinterpret_cast<BITMAPINFOHEADER*>(dib);
    *header = BITMAPINFOHEADER();
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = framebufferWidth;
    header->biHeight = framebufferHeight;
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biCompression = BI_RGB;
    header->biSizeImage = static_cast<DWORD>(pixelBytes);

    std::uint8_t* bgraPixels = dib + sizeof(BITMAPINFOHEADER);
    std::size_t pixelIndex = 0u;
    for (; pixelIndex < width * height; ++pixelIndex) {
        bgraPixels[(pixelIndex * 4u) + 0u] = rgbaPixels[(pixelIndex * 4u) + 2u];
        bgraPixels[(pixelIndex * 4u) + 1u] = rgbaPixels[(pixelIndex * 4u) + 1u];
        bgraPixels[(pixelIndex * 4u) + 2u] = rgbaPixels[(pixelIndex * 4u) + 0u];
        bgraPixels[(pixelIndex * 4u) + 3u] = 0xffu;
    }
    GlobalUnlock(clipboardMemory.get());

    if (OpenClipboard(0) == FALSE) {
        return false;
    }

    if (EmptyClipboard() == FALSE) {
        CloseClipboard();
        return false;
    }

    if (SetClipboardData(CF_DIB, clipboardMemory.get()) == 0) {
        CloseClipboard();
        return false;
    }

    clipboardMemory.release();
    CloseClipboard();
    return true;
}

class RendererCallbacks {
public:
    // Bridges GLFW callbacks back into the app controller.
    RendererCallbacks(AppController& appController, bool isFullscreen, int windowedWidth, int windowedHeight)
        : appController_(appController),
          isFullscreen_(isFullscreen),
          screenshotRequested_(false),
          windowedX_(0),
          windowedY_(0),
          windowedWidth_(windowedWidth),
          windowedHeight_(windowedHeight) {
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
            if (key == GLFW_KEY_PRINT_SCREEN && action == GLFW_PRESS) {
                callbacks->requestScreenshot();
                return;
            }

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

    bool consumeScreenshotRequest() {
        if (!screenshotRequested_) {
            return false;
        }

        screenshotRequested_ = false;
        return true;
    }

private:
    void requestScreenshot() {
        screenshotRequested_ = true;
    }

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
    bool screenshotRequested_;
    int windowedX_;
    int windowedY_;
    int windowedWidth_;
    int windowedHeight_;
};

void CopyRequestedScreenshotToClipboard(RendererCallbacks& callbacks, int framebufferWidth, int framebufferHeight) {
    if (!callbacks.consumeScreenshotRequest()) {
        return;
    }

    if (!CopyBackBufferToClipboard(framebufferWidth, framebufferHeight)) {
        LogWarning("Renderer::run", "PrintScreen capture requested, but copying the current back buffer to the clipboard failed.");
    }
}

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

// Uploads one generated road atlas image into an OpenGL texture.
GLuint CreateRoadAtlasTexture(const RoadAtlasImage& image) {
    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.empty() ? 0 : &image.pixels[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return textureId;
}

// Builds the path to the copied runtime shader file.
std::string BuildShaderPath() {
    return RuntimeExecutablePath("Basic.shader");
}

std::string BuildDataPath(const std::string& relativePath) {
    return RuntimeDataPath(relativePath);
}

// Configures one instanced vertex attribute for OpenGL.
void SetupInstanceAttribute(GLuint attributeIndex, GLint componentCount, GLsizei stride, std::size_t offsetBytes) {
    glEnableVertexAttribArray(attributeIndex);
    glVertexAttribPointer(attributeIndex, componentCount, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetBytes));
    glVertexAttribDivisor(attributeIndex, 1);
}

struct GlyphPattern {
    std::uint32_t codepoint;
    const char* rows[7];
};

const char* const* GlyphRows(std::uint32_t codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') {
        codepoint = codepoint - 'a' + 'A';
    }

    static const GlyphPattern glyphs[] = {
        {' ', {"00000", "00000", "00000", "00000", "00000", "00000", "00000"}},
        {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
        {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
        {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
        {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
        {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
        {'5', {"11111", "10000", "10000", "11110", "00001", "00001", "11110"}},
        {'6', {"01110", "10000", "10000", "11110", "10001", "10001", "01110"}},
        {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
        {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
        {'9', {"01110", "10001", "10001", "01111", "00001", "00001", "01110"}},
        {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
        {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
        {'C', {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}},
        {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
        {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
        {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
        {'G', {"01110", "10001", "10000", "10111", "10001", "10001", "01111"}},
        {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
        {'I', {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}},
        {'J', {"00111", "00010", "00010", "00010", "10010", "10010", "01100"}},
        {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
        {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
        {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
        {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
        {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
        {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
        {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
        {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
        {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
        {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
        {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
        {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
        {'W', {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}},
        {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
        {'Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
        {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
        {'#', {"01010", "01010", "11111", "01010", "11111", "01010", "01010"}},
        {':', {"00000", "00100", "00100", "00000", "00100", "00100", "00000"}},
        {'/', {"00001", "00010", "00010", "00100", "01000", "01000", "10000"}},
        {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
        {'_', {"00000", "00000", "00000", "00000", "00000", "00000", "11111"}},
        {',', {"00000", "00000", "00000", "00000", "00100", "00100", "01000"}},
        {'.', {"00000", "00000", "00000", "00000", "00000", "01100", "01100"}},
        {'(', {"00010", "00100", "01000", "01000", "01000", "00100", "00010"}},
        {')', {"01000", "00100", "00010", "00010", "00010", "00100", "01000"}},
        {'+', {"00000", "00100", "00100", "11111", "00100", "00100", "00000"}},
        {'=', {"00000", "00000", "11111", "00000", "11111", "00000", "00000"}},
        {'?', {"01110", "10001", "00001", "00010", "00100", "00000", "00100"}}
    };

    std::size_t glyphIndex = 0;
    for (; glyphIndex < sizeof(glyphs) / sizeof(glyphs[0]); ++glyphIndex) {
        if (glyphs[glyphIndex].codepoint == codepoint) {
            return glyphs[glyphIndex].rows;
        }
    }

    return glyphs[sizeof(glyphs) / sizeof(glyphs[0]) - 1u].rows;
}

bool NextUtf8Codepoint(const std::string& text, std::size_t& byteIndex, std::uint32_t& codepoint) {
    return RendererNextUtf8Codepoint(text, byteIndex, codepoint);
}

void AddUiQuad(std::vector<UiQuadInstanceData>& quads, float x, float y, float width, float height, const Vec4& color) {
    if (width <= 0.0f || height <= 0.0f || color.w <= 0.0f) {
        return;
    }

    UiQuadInstanceData quad;
    quad.x = x;
    quad.y = y;
    quad.width = width;
    quad.height = height;
    quad.colorR = color.x;
    quad.colorG = color.y;
    quad.colorB = color.z;
    quad.colorA = color.w;
    quads.push_back(quad);
}

std::string FormatIntegerWithCommas(int value) {
    if (value <= 0) {
        return "0";
    }

    std::string reversedDigits;
    int digitCount = 0;
    while (value > 0) {
        if (digitCount == 3) {
            reversedDigits.push_back(',');
            digitCount = 0;
        }

        reversedDigits.push_back(static_cast<char>('0' + (value % 10)));
        value /= 10;
        ++digitCount;
    }

    std::reverse(reversedDigits.begin(), reversedDigits.end());
    return reversedDigits;
}

std::string BuildPopulationLabel(const std::string& prefix, int population) {
    std::ostringstream stream;
    stream << prefix << FormatIntegerWithCommas(population);
    return stream.str();
}

void AppendHudTextPanel(std::vector<UiQuadInstanceData>& quads, const std::string& text, float x, float y, float width) {
    const float height = 28.0f;
    AddUiQuad(quads, x, y, width, height, Vec4(0.025f, 0.032f, 0.038f, 0.70f));
    AddUiQuad(quads, x, y + height - 2.0f, width, 2.0f, Vec4(0.20f, 0.28f, 0.27f, 0.78f));
    RendererAppendTextQuads(text, x + 10.0f, y + 7.0f, width - 20.0f, height - 8.0f, UiColor(0.92f, 0.96f, 0.92f, 1.0f), false, quads);
}

std::string TrimLeadingSpaces(const std::string& text) {
    std::string::size_type first = 0u;
    while (first < text.size() && text[first] == ' ') {
        ++first;
    }
    return text.substr(first);
}

void AppendWrappedWarningLine(std::vector<std::string>& lines, const std::string& text, std::size_t maxCharacters, std::size_t maxLines) {
    if (lines.size() >= maxLines) {
        return;
    }

    std::string remaining = TrimLeadingSpaces(text);
    if (remaining.empty()) {
        lines.push_back(std::string());
        return;
    }

    while (remaining.size() > maxCharacters && lines.size() < maxLines) {
        std::string::size_type split = remaining.rfind(' ', maxCharacters);
        if (split == std::string::npos || split < 8u) {
            split = maxCharacters;
        }
        lines.push_back(remaining.substr(0u, split));
        remaining = TrimLeadingSpaces(remaining.substr(split));
    }

    if (!remaining.empty() && lines.size() < maxLines) {
        lines.push_back(remaining);
    }
}

std::vector<std::string> BuildWarningMessageLines(const std::string& message, std::size_t maxCharacters, std::size_t maxLines) {
    std::vector<std::string> lines;
    std::istringstream stream(message);
    std::string sourceLine;
    while (std::getline(stream, sourceLine)) {
        AppendWrappedWarningLine(lines, sourceLine, maxCharacters, maxLines);
        if (lines.size() >= maxLines) {
            break;
        }
    }

    if (!stream.eof() && !lines.empty()) {
        lines.back() = "...";
    }

    return lines;
}

void AppendApplicationWarningPanel(std::vector<UiQuadInstanceData>& quads, const ApplicationWarning& warning, int framebufferWidth, int framebufferHeight) {
    const float screenWidth = static_cast<float>(std::max(1, framebufferWidth));
    const float screenHeight = static_cast<float>(std::max(1, framebufferHeight));
    const float panelWidth = std::max(340.0f, std::min(720.0f, screenWidth - 48.0f));
    const float panelHeight = 280.0f;
    const float panelX = std::max(16.0f, (screenWidth - panelWidth) * 0.5f);
    const float panelY = std::max(20.0f, (screenHeight - panelHeight) * 0.5f);
    const std::size_t maxCharacters = static_cast<std::size_t>(std::max(24.0f, (panelWidth - 48.0f) / 12.0f));
    const std::vector<std::string> lines = BuildWarningMessageLines(warning.message, maxCharacters, 9u);

    AddUiQuad(quads, 0.0f, 0.0f, screenWidth, screenHeight, Vec4(0.0f, 0.0f, 0.0f, 0.48f));
    AddUiQuad(quads, panelX, panelY, panelWidth, panelHeight, Vec4(0.035f, 0.047f, 0.058f, 0.96f));
    AddUiQuad(quads, panelX, panelY, panelWidth, 3.0f, Vec4(0.58f, 0.42f, 0.18f, 0.96f));
    AddUiQuad(quads, panelX, panelY + panelHeight - 2.0f, panelWidth, 2.0f, Vec4(0.13f, 0.18f, 0.19f, 0.92f));
    AddUiQuad(quads, panelX, panelY, 2.0f, panelHeight, Vec4(0.27f, 0.31f, 0.29f, 0.92f));
    AddUiQuad(quads, panelX + panelWidth - 2.0f, panelY, 2.0f, panelHeight, Vec4(0.13f, 0.18f, 0.19f, 0.92f));

    RendererAppendTextQuads(
        warning.title,
        panelX + 24.0f,
        panelY + 24.0f,
        panelWidth - 48.0f,
        24.0f,
        UiColor(0.98f, 0.86f, 0.60f, 1.0f),
        false,
        quads);

    std::size_t lineIndex = 0u;
    for (; lineIndex < lines.size(); ++lineIndex) {
        RendererAppendTextQuads(
            lines[lineIndex],
            panelX + 24.0f,
            panelY + 62.0f + static_cast<float>(lineIndex) * 20.0f,
            panelWidth - 48.0f,
            18.0f,
            UiColor(0.90f, 0.96f, 0.93f, 1.0f),
            false,
            quads);
    }
}

void BuildTextQuads(const TextFieldElement& field, float windowX, float windowY, std::vector<UiQuadInstanceData>& quads) {
    const float scale = 2.0f;
    const float characterAdvance = 6.0f * scale;
    const float originX = windowX + static_cast<float>(field.x());
    const float originY = windowY + static_cast<float>(field.y());
    const float maxX = originX + static_cast<float>(field.width());
    const float maxY = originY + static_cast<float>(field.height());
    const Vec4 textColor(0.88f, 0.94f, 0.91f, 1.0f);

    float cursorX = originX;
    std::size_t byteIndex = 0;
    while (byteIndex < field.text().size()) {
        std::uint32_t codepoint = 0u;
        if (!NextUtf8Codepoint(field.text(), byteIndex, codepoint) || codepoint == '\n') {
            break;
        }

        if (cursorX + (5.0f * scale) > maxX || originY + (7.0f * scale) > maxY) {
            break;
        }

        const char* const* rows = GlyphRows(codepoint);
        int row = 0;
        for (; row < 7; ++row) {
            int column = 0;
            for (; column < 5; ++column) {
                if (rows[row][column] == '1') {
                    AddUiQuad(quads, cursorX + static_cast<float>(column) * scale, originY + static_cast<float>(row) * scale, scale, scale, textColor);
                }
            }
        }

        cursorX += characterAdvance;
    }
}

std::vector<UiQuadInstanceData> BuildWindowQuads(const InGameWindow& window) {
    return RendererBuildWindowQuads(window);
}

void DrawUiQuadInstances(
    ShaderProgram& shaderProgram,
    GLint viewProjectionLocation,
    GLint renderModeLocation,
    GLuint uiInstanceBufferId,
    GLuint uiVertexArrayId,
    int framebufferWidth,
    int framebufferHeight,
    const std::vector<UiQuadInstanceData>& uiQuadInstances) {
    glBindBuffer(GL_ARRAY_BUFFER, uiInstanceBufferId);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(uiQuadInstances.size() * sizeof(UiQuadInstanceData)),
        uiQuadInstances.empty() ? 0 : &uiQuadInstances[0],
        GL_DYNAMIC_DRAW);

    if (uiQuadInstances.empty()) {
        return;
    }

    const Mat4 uiProjection = Orthographic(0.0f, static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight), 0.0f, -1.0f, 1.0f);
    shaderProgram.bind();
    glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, uiProjection.data);
    glUniform1i(renderModeLocation, 6);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(uiVertexArrayId);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(uiQuadInstances.size()));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
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
    SetupInstanceAttribute(3, 4, sizeof(LotInstanceData), sizeof(float) * 8);

    glBindVertexArray(0);
}

void ConfigureGeneratedLotVertexArray(GLuint vertexArrayId, GLuint meshVertexBufferId, GLuint instanceBufferId) {
    glBindVertexArray(vertexArrayId);

    glBindBuffer(GL_ARRAY_BUFFER, meshVertexBufferId);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GeneratedMeshVertex), 0);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(GeneratedMeshVertex), reinterpret_cast<void*>(sizeof(float) * 3));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(GeneratedMeshVertex), reinterpret_cast<void*>(sizeof(float) * 6));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(GeneratedMeshVertex), reinterpret_cast<void*>(sizeof(float) * 9));

    glBindBuffer(GL_ARRAY_BUFFER, instanceBufferId);
    SetupInstanceAttribute(1, 4, sizeof(LotInstanceData), 0);
    SetupInstanceAttribute(2, 4, sizeof(LotInstanceData), sizeof(float) * 4);
    SetupInstanceAttribute(3, 4, sizeof(LotInstanceData), sizeof(float) * 8);

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

void ConfigureAreaOverlayVertexArray(GLuint vertexArrayId, GLuint tileVertexBufferId, GLuint instanceBufferId) {
    glBindVertexArray(vertexArrayId);

    glBindBuffer(GL_ARRAY_BUFFER, tileVertexBufferId);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceBufferId);
    SetupInstanceAttribute(1, 4, sizeof(AreaOverlayInstanceData), 0);
    SetupInstanceAttribute(2, 4, sizeof(AreaOverlayInstanceData), sizeof(float) * 4);

    glBindVertexArray(0);
}

void ConfigureUiVertexArray(GLuint vertexArrayId, GLuint tileVertexBufferId, GLuint instanceBufferId) {
    glBindVertexArray(vertexArrayId);

    glBindBuffer(GL_ARRAY_BUFFER, tileVertexBufferId);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceBufferId);
    SetupInstanceAttribute(1, 4, sizeof(UiQuadInstanceData), 0);
    SetupInstanceAttribute(2, 4, sizeof(UiQuadInstanceData), sizeof(float) * 4);

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

AreaOverlayInstanceData BuildAreaOverlayInstance(int minTileX, int minTileY, int maxTileX, int maxTileY, float colorR, float colorG, float colorB, float colorA);
bool LotInstanceIntersectsTileRect(const LotRenderInstance& lot, int minTileX, int minTileY, int maxTileX, int maxTileY);

LotInstanceData BuildLotInstance(const LotRenderInstance& lot) {
    LotInstanceData instance;
    instance.originX = static_cast<float>(lot.originX) + lot.renderOffsetX;
    instance.originZ = static_cast<float>(lot.originY) + lot.renderOffsetY;
    instance.sizeX = lot.renderWidth > 0.0f ? lot.renderWidth : static_cast<float>(lot.width);
    instance.sizeZ = lot.renderHeightOverride > 0.0f ? lot.renderHeightOverride : static_cast<float>(lot.height);
    instance.height = lot.renderHeight;
    instance.colorR = lot.colorR;
    instance.colorG = lot.colorG;
    instance.colorB = lot.colorB;
    instance.surfacePattern = static_cast<float>(lot.surfacePattern);
    instance.surfaceDirection = static_cast<float>(lot.surfaceDirection);
    instance.padding0 = static_cast<float>(lot.meshRotation);
    instance.padding1 = 0.0f;
    return instance;
}

bool RenderInstanceIsRciLot(const LotRenderInstance& lot) {
    return lot.zoningType == TileZoningResidentialLow ||
        lot.zoningType == TileZoningResidentialHigh ||
        lot.zoningType == TileZoningIndustrial;
}

void RciOverlayColor(std::uint16_t zoningType, float& red, float& green, float& blue) {
    if (zoningType == TileZoningResidentialLow) {
        red = 112.0f / 255.0f;
        green = 235.0f / 255.0f;
        blue = 117.0f / 255.0f;
        return;
    }

    if (zoningType == TileZoningResidentialHigh) {
        red = 26.0f / 255.0f;
        green = 122.0f / 255.0f;
        blue = 51.0f / 255.0f;
        return;
    }

    if (zoningType == TileZoningIndustrial) {
        red = 238.0f / 255.0f;
        green = 211.0f / 255.0f;
        blue = 58.0f / 255.0f;
        return;
    }

    red = 0.72f;
    green = 0.72f;
    blue = 0.72f;
}

// Converts published lot render records into instanced box payloads.
std::vector<LotInstanceData> BuildLotInstances(const std::vector<LotRenderInstance>& lots, bool hideRciLots = false) {
    std::vector<LotInstanceData> instances;
    instances.reserve(lots.size());

    std::size_t lotIndex = 0;
    for (; lotIndex < lots.size(); ++lotIndex) {
        if (hideRciLots && RenderInstanceIsRciLot(lots[lotIndex])) {
            continue;
        }

        instances.push_back(BuildLotInstance(lots[lotIndex]));
    }

    return instances;
}

std::map<std::uint16_t, GeneratedMeshRange> BuildRuntimeMeshRanges(const GeneratedMeshCatalog& catalog, const std::vector<RenderMeshBinding>* bindings, bool distant = false) {
    std::map<std::uint16_t, GeneratedMeshRange> ranges;
    const GeneratedMeshRange* boxRange = catalog.findMesh("box");
    const GeneratedMeshRange* placeholderRange = catalog.findMesh("missing_mesh_placeholder");
    const GeneratedMeshRange* missingMeshRange = placeholderRange != 0 ? placeholderRange : boxRange;
    if (bindings == 0) {
        if (boxRange != 0) {
            ranges[0u] = *boxRange;
        }
        return ranges;
    }

    std::size_t bindingIndex = 0;
    for (; bindingIndex < bindings->size(); ++bindingIndex) {
        const RenderMeshBinding& binding = (*bindings)[bindingIndex];
        const GeneratedMeshRange* range = distant ? catalog.findMesh(binding.key + "_distant") : nullptr;
        if (!range) range = catalog.findMesh(binding.key);
        if (range == 0) {
            static std::set<std::string> warnedMissingMeshKeys;
            if (warnedMissingMeshKeys.insert(binding.key).second) {
                LogWarning("Renderer", "Generated mesh catalog missing mesh '" + binding.key + "'; using missing mesh placeholder.");
            }
            range = missingMeshRange;
        }
        if (range != 0) {
            ranges[binding.handle] = *range;
        }
    }

    if (ranges.find(0u) == ranges.end()) {
        if (boxRange != 0) {
            ranges[0u] = *boxRange;
        }
    }

    return ranges;
}

bool BuildGeneratedLotInstances(
    const std::vector<LotRenderInstance>& lots,
    const std::map<std::uint16_t, GeneratedMeshRange>& meshRanges,
    bool hideRciLots,
    std::vector<LotInstanceData>& instances,
    std::vector<LotMeshDrawBatch>& batches) {
    instances.clear();
    batches.clear();
    if (meshRanges.empty()) {
        return false;
    }

    std::map<std::uint16_t, std::vector<LotInstanceData> > groupedInstances;
    std::size_t lotIndex = 0;
    for (; lotIndex < lots.size(); ++lotIndex) {
        const LotRenderInstance& lot = lots[lotIndex];
        if (hideRciLots && RenderInstanceIsRciLot(lot)) {
            continue;
        }

        std::uint16_t meshHandle = lot.renderMeshHandle;
        if (meshRanges.find(meshHandle) == meshRanges.end()) {
            meshHandle = 0u;
        }
        groupedInstances[meshHandle].push_back(BuildLotInstance(lot));
    }

    std::map<std::uint16_t, std::vector<LotInstanceData> >::const_iterator groupIterator = groupedInstances.begin();
    for (; groupIterator != groupedInstances.end(); ++groupIterator) {
        const std::map<std::uint16_t, GeneratedMeshRange>::const_iterator rangeIterator = meshRanges.find(groupIterator->first);
        if (rangeIterator == meshRanges.end()) {
            continue;
        }

        LotMeshDrawBatch batch;
        batch.meshHandle = groupIterator->first;
        batch.firstVertex = rangeIterator->second.firstVertex;
        batch.vertexCount = rangeIterator->second.vertexCount;
        batch.instanceOffset = static_cast<GLsizei>(instances.size());
        batch.instanceCount = static_cast<GLsizei>(groupIterator->second.size());
        batches.push_back(batch);
        instances.insert(instances.end(), groupIterator->second.begin(), groupIterator->second.end());
    }

    return true;
}

bool BuildGeneratedLotInstancesInTileRect(
    const std::vector<LotRenderInstance>& lots,
    int minTileX,
    int minTileY,
    int maxTileX,
    int maxTileY,
    const std::map<std::uint16_t, GeneratedMeshRange>& meshRanges,
    std::vector<LotInstanceData>& instances,
    std::vector<LotMeshDrawBatch>& batches) {
    std::vector<LotRenderInstance> filteredLots;
    std::size_t lotIndex = 0;
    for (; lotIndex < lots.size(); ++lotIndex) {
        if (LotInstanceIntersectsTileRect(lots[lotIndex], minTileX, minTileY, maxTileX, maxTileY)) {
            filteredLots.push_back(lots[lotIndex]);
        }
    }

    return BuildGeneratedLotInstances(filteredLots, meshRanges, false, instances, batches);
}

void DrawGeneratedLotBatches(const std::vector<LotMeshDrawBatch>& batches) {
    std::size_t batchIndex = 0;
    for (; batchIndex < batches.size(); ++batchIndex) {
        const LotMeshDrawBatch& batch = batches[batchIndex];
        glDrawArraysInstancedBaseInstance(
            GL_TRIANGLES,
            batch.firstVertex,
            batch.vertexCount,
            batch.instanceCount,
            static_cast<GLuint>(batch.instanceOffset));
    }
}

void FlushRciParcelOverlayInstance(
    bool hasParcel,
    std::uint16_t zoningType,
    int minTileX,
    int minTileY,
    int maxTileX,
    int maxTileY,
    std::vector<AreaOverlayInstanceData>& instances) {
    if (!hasParcel) {
        return;
    }

    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    RciOverlayColor(zoningType, red, green, blue);
    instances.push_back(BuildAreaOverlayInstance(minTileX, minTileY, maxTileX, maxTileY, red, green, blue, 0.48f));
}

std::vector<AreaOverlayInstanceData> BuildRciParcelOverlayInstances(
    const std::vector<LotRenderInstance>& lots,
    const std::vector<RciLot>* zoningLots,
    bool includeBuiltLots) {
    std::vector<AreaOverlayInstanceData> instances;
    instances.reserve(lots.size() + (zoningLots == 0 ? 0u : zoningLots->size()));

    bool hasCurrentParcel = false;
    int currentLotId = -1;
    std::uint16_t currentZoningType = TileZoningNone;
    int minTileX = 0;
    int minTileY = 0;
    int maxTileX = 0;
    int maxTileY = 0;

    if (includeBuiltLots) {
        std::size_t lotIndex = 0;
        for (; lotIndex < lots.size(); ++lotIndex) {
            const LotRenderInstance& lot = lots[lotIndex];
            if (!RenderInstanceIsRciLot(lot)) {
                continue;
            }

            if (!hasCurrentParcel || lot.lotId != currentLotId) {
                FlushRciParcelOverlayInstance(hasCurrentParcel, currentZoningType, minTileX, minTileY, maxTileX, maxTileY, instances);
                hasCurrentParcel = true;
                currentLotId = lot.lotId;
                currentZoningType = lot.zoningType;
                minTileX = lot.originX;
                minTileY = lot.originY;
                maxTileX = lot.originX + lot.width - 1;
                maxTileY = lot.originY + lot.height - 1;
            } else {
                minTileX = std::min(minTileX, lot.originX);
                minTileY = std::min(minTileY, lot.originY);
                maxTileX = std::max(maxTileX, lot.originX + lot.width - 1);
                maxTileY = std::max(maxTileY, lot.originY + lot.height - 1);
            }
        }
        FlushRciParcelOverlayInstance(hasCurrentParcel, currentZoningType, minTileX, minTileY, maxTileX, maxTileY, instances);
    }

    if (zoningLots != 0) {
        std::size_t zoningLotIndex = 0;
        for (; zoningLotIndex < zoningLots->size(); ++zoningLotIndex) {
            const RciLot& lot = (*zoningLots)[zoningLotIndex];
            instances.push_back(BuildAreaOverlayInstance(
                lot.rect.minTileX,
                lot.rect.minTileY,
                lot.rect.maxTileX,
                lot.rect.maxTileY,
                lot.color.r,
                lot.color.g,
                lot.color.b,
                0.48f));
        }
    }

    return instances;
}

bool LotInstanceIntersectsTileRect(const LotRenderInstance& lot, int minTileX, int minTileY, int maxTileX, int maxTileY) {
    const int selectionMaxExclusiveX = maxTileX + 1;
    const int selectionMaxExclusiveY = maxTileY + 1;
    return lot.originX < selectionMaxExclusiveX &&
        lot.originX + lot.width > minTileX &&
        lot.originY < selectionMaxExclusiveY &&
        lot.originY + lot.height > minTileY;
}

std::vector<LotInstanceData> BuildLotInstancesInTileRect(const std::vector<LotRenderInstance>& lots, int minTileX, int minTileY, int maxTileX, int maxTileY) {
    std::vector<LotInstanceData> instances;

    std::size_t lotIndex = 0;
    for (; lotIndex < lots.size(); ++lotIndex) {
        if (LotInstanceIntersectsTileRect(lots[lotIndex], minTileX, minTileY, maxTileX, maxTileY)) {
            instances.push_back(BuildLotInstance(lots[lotIndex]));
        }
    }

    return instances;
}

AreaOverlayInstanceData BuildAreaOverlayInstance(int minTileX, int minTileY, int maxTileX, int maxTileY, float colorR, float colorG, float colorB, float colorA) {
    AreaOverlayInstanceData instance;
    instance.originX = static_cast<float>(minTileX);
    instance.originZ = static_cast<float>(minTileY);
    instance.sizeX = static_cast<float>(maxTileX - minTileX + 1);
    instance.sizeZ = static_cast<float>(maxTileY - minTileY + 1);
    instance.colorR = colorR;
    instance.colorG = colorG;
    instance.colorB = colorB;
    instance.colorA = colorA;
    return instance;
}

AreaOverlayInstanceData BuildAreaOverlayInstance(int minTileX, int minTileY, int maxTileX, int maxTileY) {
    return BuildAreaOverlayInstance(minTileX, minTileY, maxTileX, maxTileY, 1.0f, 0.05f, 0.03f, 0.28f);
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
        const RoadArrowGlyph previewArrowGlyph = ChooseArrowGlyph(cell.arrowIntentMask);
        instance.arrowGlyph = previewArrowGlyph == RoadArrowGlyph::None
            ? 0.0f
            : static_cast<float>(static_cast<std::uint8_t>(previewArrowGlyph) | kRoadArrowDebugFlag);
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
    spec.nearPlane = std::max(kCityCameraMinimumNearPlane, spec.distance * kCityCameraNearPlaneDistanceFactor);
    spec.farPlane = std::max(
        kCityCameraMinimumFarPlane,
        spec.distance + static_cast<float>(viewState.visibleTiles) * kCityCameraFarPlaneTilePadding + kCityCameraFarPlaneWorldPadding);
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
    return static_cast<GLshort>(RendererPackTileStateScalar(value));
}

// Packs air pollution and park effect for one visible chunk.
void FillTileStateChunkPixels(const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect, std::vector<GLshort>& texturePixels) {
    if (snapshot.tiles == 0) {
        return;
    }

    RendererFillTileStateChunkPixels(*snapshot.tiles, snapshot.width, chunkRect, texturePixels);
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

    RendererFillTileLiftChunkPixels(*snapshot.lotOccupancy, snapshot.width, chunkRect, texturePixels);
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
        static_cast<std::size_t>(chunkRect.startY) * static_cast<std::size_t>(snapshot.width) + static_cast<std::size_t>(chunkRect.startX);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, snapshot.width);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        chunkRect.startX,
        chunkRect.startY,
        chunkRect.width,
        chunkRect.height,
        kRendererScalarPayloadUploadFormat,
        kRendererScalarPayloadUploadType,
        &(*snapshot.tileOverlayState)[startOffset]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void FillZoningOverlayChunkValues(const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) {
    const std::size_t chunkValueCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (textureValues.size() != chunkValueCount) {
        textureValues.resize(chunkValueCount, 0u);
    }

    if (snapshot.tiles == 0) {
        std::fill(textureValues.begin(), textureValues.end(), 0u);
        return;
    }

    RendererFillZoningOverlayChunkValues(*snapshot.tiles, snapshot.width, chunkRect, textureValues);
}

void FillLandValueOverlayChunkValues(const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) {
    const std::size_t chunkValueCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (textureValues.size() != chunkValueCount) {
        textureValues.resize(chunkValueCount, 0u);
    }

    if (snapshot.tiles == 0) {
        std::fill(textureValues.begin(), textureValues.end(), 0u);
        return;
    }

    RendererFillLandValueOverlayChunkValues(*snapshot.tiles, snapshot.width, chunkRect, textureValues);
}

void FillAirPollutionOverlayChunkValues(const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) {
    const std::size_t chunkValueCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (textureValues.size() != chunkValueCount) {
        textureValues.resize(chunkValueCount, 0u);
    }

    if (snapshot.tiles == 0) {
        std::fill(textureValues.begin(), textureValues.end(), 0u);
        return;
    }

    RendererFillAirPollutionOverlayChunkValues(*snapshot.tiles, snapshot.width, chunkRect, textureValues);
}

void FillParkEffectOverlayChunkValues(const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) {
    const std::size_t chunkValueCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (textureValues.size() != chunkValueCount) {
        textureValues.resize(chunkValueCount, 0u);
    }

    if (snapshot.tiles == 0) {
        std::fill(textureValues.begin(), textureValues.end(), 0u);
        return;
    }

    RendererFillParkEffectOverlayChunkValues(*snapshot.tiles, snapshot.width, chunkRect, textureValues);
}

void UploadScalarOverlayChunkTexture(GLuint textureId, const ChunkRect& chunkRect, const std::vector<RendererScalarPayload>& textureValues) {
    if (textureValues.empty()) {
        return;
    }

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        chunkRect.startX,
        chunkRect.startY,
        chunkRect.width,
        chunkRect.height,
        kRendererScalarPayloadUploadFormat,
        kRendererScalarPayloadUploadType,
        &textureValues[0]);
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
            const std::uint8_t placeholderPixels[4] = {20u, 28u, 38u, 255u};
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA8,
                1,
                1,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                placeholderPixels);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            cache.previewRevision = std::numeric_limits<std::uint64_t>::max();
        }
    }
}

void UploadRegionPreviewTexture(RegionPreviewTextureCache& cache, const City& city, const std::vector<std::uint8_t>& previewPixels) {
    if (cache.textureId == 0 || previewPixels.empty()) {
        return;
    }

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
        &previewPixels[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    cache.previewRevision = city.previewRevision();
}

void UploadRegionPreviewPlaceholderTexture(RegionPreviewTextureCache& cache, const City& city) {
    if (cache.textureId == 0) {
        return;
    }

    const std::uint8_t placeholderPixels[4] = {20u, 28u, 38u, 255u};
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, cache.textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        1,
        1,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        placeholderPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    cache.previewRevision = city.previewRevision();
}

float LoadingBarYForHeight(float height) {
    float barY = height * 0.75f;
    if (barY + 96.0f > height) {
        barY = std::max(42.0f, height - 96.0f);
    }
    return barY;
}

void ClearScreenRect(int framebufferWidth, int framebufferHeight, float x, float y, float width, float height, float red, float green, float blue, float alpha) {
    const int scissorX = std::max(0, static_cast<int>(x + 0.5f));
    const int scissorY = std::max(0, framebufferHeight - static_cast<int>(y + height + 0.5f));
    const int scissorWidth = std::max(0, std::min(framebufferWidth - scissorX, static_cast<int>(width + 0.5f)));
    const int scissorHeight = std::max(0, std::min(framebufferHeight - scissorY, static_cast<int>(height + 0.5f)));
    if (scissorWidth <= 0 || scissorHeight <= 0) {
        return;
    }

    glScissor(scissorX, scissorY, scissorWidth, scissorHeight);
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
}

void DrawBootstrapLoadingScreen(GLFWwindow* window, float progress) {
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    framebufferWidth = std::max(1, framebufferWidth);
    framebufferHeight = std::max(1, framebufferHeight);

    const float width = static_cast<float>(framebufferWidth);
    const float height = static_cast<float>(framebufferHeight);
    const float clampedProgress = std::max(0.0f, std::min(progress, 1.0f));
    const float barWidth = std::max(180.0f, std::min(560.0f, width - 96.0f));
    const float barHeight = 20.0f;
    const float barX = (width - barWidth) * 0.5f;
    const float barY = LoadingBarYForHeight(height);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.018f, 0.025f, 0.030f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_SCISSOR_TEST);
    ClearScreenRect(framebufferWidth, framebufferHeight, 0.0f, 0.0f, width, 4.0f, 0.18f, 0.33f, 0.30f, 0.70f);
    ClearScreenRect(framebufferWidth, framebufferHeight, 0.0f, height - 4.0f, width, 4.0f, 0.18f, 0.33f, 0.30f, 0.50f);
    ClearScreenRect(framebufferWidth, framebufferHeight, barX - 2.0f, barY - 2.0f, barWidth + 4.0f, barHeight + 4.0f, 0.36f, 0.48f, 0.44f, 0.95f);
    ClearScreenRect(framebufferWidth, framebufferHeight, barX, barY, barWidth, barHeight, 0.055f, 0.070f, 0.076f, 0.98f);
    ClearScreenRect(framebufferWidth, framebufferHeight, barX + 3.0f, barY + 3.0f, (barWidth - 6.0f) * clampedProgress, barHeight - 6.0f, 0.24f, 0.62f, 0.50f, 0.96f);
    glDisable(GL_SCISSOR_TEST);

    glfwSwapBuffers(window);
    glfwPollEvents();
}

bool RuntimeFileExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
}

void RunOptionalAssetGenerator(GLFWwindow* window, float progressStart, float progressEnd) {
    const std::string executablePath = RuntimeExecutablePath("CityBuilderAssetGenerator.exe");
    DrawBootstrapLoadingScreen(window, progressStart);
    if (!RuntimeFileExists(executablePath)) {
        LogWarning("AssetGenerator", "Optional asset generator not found: " + executablePath);
        DrawBootstrapLoadingScreen(window, progressEnd);
        return;
    }

    const std::string dataDirectory = RuntimeDataDirectory();
    std::string commandLine = "\"" + executablePath + "\" \"" + dataDirectory + "\"";
    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');

    STARTUPINFOA startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo;
    ZeroMemory(&processInfo, sizeof(processInfo));

    if (!CreateProcessA(
            0,
            &mutableCommandLine[0],
            0,
            0,
            FALSE,
            CREATE_NO_WINDOW,
            0,
            RuntimeExecutableDirectory().c_str(),
            &startupInfo,
            &processInfo)) {
        std::ostringstream message;
        message << "Optional asset generator failed to launch with error " << GetLastError() << ": " << executablePath;
        LogWarning("AssetGenerator", message.str());
        DrawBootstrapLoadingScreen(window, progressEnd);
        return;
    }

    float progress = progressStart;
    DWORD waitResult = WAIT_TIMEOUT;
    while ((waitResult = WaitForSingleObject(processInfo.hProcess, 16u)) == WAIT_TIMEOUT) {
        progress = std::min(progressEnd - 0.01f, progress + 0.0025f);
        DrawBootstrapLoadingScreen(window, progress);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (exitCode != 0u) {
        std::ostringstream message;
        message << "Optional asset generator exited with code " << exitCode << ".";
        LogWarning("AssetGenerator", message.str());
        DrawBootstrapLoadingScreen(window, progressEnd);
        return;
    }

    LogInfo("AssetGenerator", "Generated startup assets.");
    DrawBootstrapLoadingScreen(window, progressEnd);
}

}

// Connects the renderer to immutable snapshots and user input state.
Renderer::Renderer(GameSession& gameSession, AppController& appController, const AppConfig& appConfig)
    : gameSession_(gameSession),
      appController_(appController),
      appConfig_(appConfig) {
}

// Owns the GLFW/OpenGL frame loop, uploads, culling, drawing, and status metrics.
int Renderer::run() {
    CrashScope crashScope("Renderer::run");

    if (glfwInit() == GLFW_FALSE) {
        LogError("Renderer::run", "GLFW initialization failed.");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 32);
    glfwWindowHint(GLFW_STENCIL_BITS, 0);

    // GLFW fullscreen is chosen at window creation by passing a monitor. The
    // callback also needs the configured windowed size so Alt+Enter can restore
    // something intentional instead of the monitor-sized fullscreen dimensions.
    const int configuredWindowedWidth = std::max(640, appConfig_.window.windowedWidth);
    const int configuredWindowedHeight = std::max(480, appConfig_.window.windowedHeight);
    GLFWmonitor* startupMonitor = 0;
    const GLFWvidmode* startupVideoMode = 0;
    if (appConfig_.window.fullscreen) {
        startupMonitor = glfwGetPrimaryMonitor();
        startupVideoMode = startupMonitor == 0 ? 0 : glfwGetVideoMode(startupMonitor);
        if (startupMonitor == 0 || startupVideoMode == 0) {
            startupMonitor = 0;
            startupVideoMode = 0;
            LogError("Renderer::run", "Fullscreen startup requested, but no primary monitor video mode was available. Falling back to windowed mode.");
        }
    }

    const int initialWindowWidth = startupVideoMode == 0 ? configuredWindowedWidth : startupVideoMode->width;
    const int initialWindowHeight = startupVideoMode == 0 ? configuredWindowedHeight : startupVideoMode->height;
    GLFWwindow* window = glfwCreateWindow(initialWindowWidth, initialWindowHeight, "Project Prime", startupMonitor, 0);
    if (window == 0) {
        glfwTerminate();
        LogError("Renderer::run", "Window creation failed.");
        return 1;
    }
    SetApplicationDialogOwner(glfwGetWin32Window(window));

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        LogError("Renderer::run", "GLEW initialization failed.");
        ClearApplicationDialogOwner(glfwGetWin32Window(window));
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    GLint defaultFramebufferDepthBits = 0;
    glGetIntegerv(GL_DEPTH_BITS, &defaultFramebufferDepthBits);
    LogInfo("Renderer::run", "Default framebuffer depth bits: " + std::to_string(defaultFramebufferDepthBits));

    DrawBootstrapLoadingScreen(window, 0.02f);
    RunOptionalAssetGenerator(window, 0.04f, 0.10f);
    gameSession_.setLoadingPresenter([&] (const LoadingStatus& loadingStatus) {
        DrawBootstrapLoadingScreen(window, loadingStatus.progress);
    });
    SimulationRuntime& simulationRuntime = gameSession_.runtime();
    DrawBootstrapLoadingScreen(window, 0.22f);

    RendererCallbacks callbacks(appController_, startupMonitor != 0, configuredWindowedWidth, configuredWindowedHeight);
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

    GeneratedMeshCatalog generatedMeshCatalog;
    bool generatedLotMeshesLoaded = false;
    GLuint generatedLotVertexBufferId = 0;
    {
        std::string meshCatalogError;
        const std::string meshCatalogPath = RuntimeDataPath("Generated\\module_meshes.txt");
        if (generatedMeshCatalog.loadFromFile(meshCatalogPath, meshCatalogError)) {
            glGenBuffers(1, &generatedLotVertexBufferId);
            glBindBuffer(GL_ARRAY_BUFFER, generatedLotVertexBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(generatedMeshCatalog.vertices().size() * sizeof(GeneratedMeshVertex)),
                generatedMeshCatalog.vertices().empty() ? 0 : &generatedMeshCatalog.vertices()[0],
                GL_STATIC_DRAW);
            generatedLotMeshesLoaded = true;
            LogInfo("Renderer", "Loaded generated mesh catalog: " + meshCatalogPath);
        } else {
            LogWarning("Renderer", meshCatalogError + " Falling back to box lot meshes.");
        }
    }

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
        kLegacyTileStateTextureInternalFormat,
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
        kLegacyScalarOverlayTextureInternalFormat,
        simulationRuntime.mapWidth(),
        simulationRuntime.mapHeight(),
        0,
        kRendererScalarPayloadUploadFormat,
        kRendererScalarPayloadUploadType,
        0);

    GLuint zoningOverlayTextureId = 0;
    glGenTextures(1, &zoningOverlayTextureId);
    glBindTexture(GL_TEXTURE_2D, zoningOverlayTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        kLegacyScalarOverlayTextureInternalFormat,
        simulationRuntime.mapWidth(),
        simulationRuntime.mapHeight(),
        0,
        kRendererScalarPayloadUploadFormat,
        kRendererScalarPayloadUploadType,
        0);

    const GLuint roadBaseAtlasTextureId = CreateRoadAtlasTexture(BuildRoadBaseAtlas(true));
    const GLuint roadBaseCleanAtlasTextureId = CreateRoadAtlasTexture(BuildRoadBaseAtlas(false));
    const GLuint roadArrowAtlasTextureId = CreateRoadAtlasTexture(BuildRoadArrowAtlas());

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

    GLuint generatedLotVertexArrayId = 0;
    if (generatedLotMeshesLoaded) {
        glGenVertexArrays(1, &generatedLotVertexArrayId);
        ConfigureGeneratedLotVertexArray(generatedLotVertexArrayId, generatedLotVertexBufferId, lotInstanceBufferId);
    }

    GLuint lotGhostVertexArrayId = 0;
    GLuint lotGhostInstanceBufferId = 0;
    glGenVertexArrays(1, &lotGhostVertexArrayId);
    glGenBuffers(1, &lotGhostInstanceBufferId);
    ConfigureLotVertexArray(lotGhostVertexArrayId, lotVertexBufferId, lotGhostInstanceBufferId);

    GLuint generatedLotGhostVertexArrayId = 0;
    if (generatedLotMeshesLoaded) {
        glGenVertexArrays(1, &generatedLotGhostVertexArrayId);
        ConfigureGeneratedLotVertexArray(generatedLotGhostVertexArrayId, generatedLotVertexBufferId, lotGhostInstanceBufferId);
    }

    GLuint areaOverlayVertexArrayId = 0;
    GLuint areaOverlayInstanceBufferId = 0;
    glGenVertexArrays(1, &areaOverlayVertexArrayId);
    glGenBuffers(1, &areaOverlayInstanceBufferId);
    ConfigureAreaOverlayVertexArray(areaOverlayVertexArrayId, tileVertexBufferId, areaOverlayInstanceBufferId);

    GLuint zoningLotOverlayVertexArrayId = 0;
    GLuint zoningLotOverlayInstanceBufferId = 0;
    glGenVertexArrays(1, &zoningLotOverlayVertexArrayId);
    glGenBuffers(1, &zoningLotOverlayInstanceBufferId);
    ConfigureAreaOverlayVertexArray(zoningLotOverlayVertexArrayId, tileVertexBufferId, zoningLotOverlayInstanceBufferId);

    GLuint regionPreviewVertexArrayId = 0;
    GLuint regionPreviewInstanceBufferId = 0;
    glGenVertexArrays(1, &regionPreviewVertexArrayId);
    glGenBuffers(1, &regionPreviewInstanceBufferId);
    ConfigureRegionPreviewVertexArray(regionPreviewVertexArrayId, tileVertexBufferId, regionPreviewInstanceBufferId);

    GLuint uiVertexArrayId = 0;
    GLuint uiInstanceBufferId = 0;
    glGenVertexArrays(1, &uiVertexArrayId);
    glGenBuffers(1, &uiInstanceBufferId);
    ConfigureUiVertexArray(uiVertexArrayId, tileVertexBufferId, uiInstanceBufferId);

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
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, City::kPreviewWidth, City::kPreviewHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, cityPreviewDepthRenderbufferId);
    bool cityPreviewFramebufferComplete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!cityPreviewFramebufferComplete) {
        LogError("Renderer::run", "City preview framebuffer is incomplete.");
    }
    GLint cityPreviewDepthBits = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &cityPreviewDepthBits);
    LogInfo("Renderer::run", "City preview depth bits: " + std::to_string(cityPreviewDepthBits));
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ShaderProgram shaderProgram;
    if (!shaderProgram.loadFromFile(BuildShaderPath())) {
        DestroyTileChunkCaches(chunkCaches);
        DestroyRoadChunkCaches(roadChunkCaches);
        glDeleteRenderbuffers(1, &cityPreviewDepthRenderbufferId);
        glDeleteTextures(1, &cityPreviewColorTextureId);
        glDeleteFramebuffers(1, &cityPreviewFramebufferId);
        glDeleteBuffers(1, &uiInstanceBufferId);
        glDeleteVertexArrays(1, &uiVertexArrayId);
        glDeleteBuffers(1, &regionPreviewInstanceBufferId);
        glDeleteVertexArrays(1, &regionPreviewVertexArrayId);
        glDeleteBuffers(1, &roadGhostInstanceBufferId);
        glDeleteVertexArrays(1, &roadGhostVertexArrayId);
        glDeleteBuffers(1, &routeArrowInstanceBufferId);
        glDeleteVertexArrays(1, &routeArrowVertexArrayId);
        glDeleteBuffers(1, &areaOverlayInstanceBufferId);
        glDeleteVertexArrays(1, &areaOverlayVertexArrayId);
        glDeleteBuffers(1, &zoningLotOverlayInstanceBufferId);
        glDeleteVertexArrays(1, &zoningLotOverlayVertexArrayId);
        glDeleteBuffers(1, &lotGhostInstanceBufferId);
        glDeleteVertexArrays(1, &lotGhostVertexArrayId);
        if (generatedLotGhostVertexArrayId != 0) {
            glDeleteVertexArrays(1, &generatedLotGhostVertexArrayId);
        }
        glDeleteBuffers(1, &lotInstanceBufferId);
        glDeleteVertexArrays(1, &lotVertexArrayId);
        if (generatedLotVertexArrayId != 0) {
            glDeleteVertexArrays(1, &generatedLotVertexArrayId);
        }
        glDeleteTextures(1, &roadArrowAtlasTextureId);
        glDeleteTextures(1, &roadBaseCleanAtlasTextureId);
        glDeleteTextures(1, &roadBaseAtlasTextureId);
        glDeleteTextures(1, &zoningOverlayTextureId);
        glDeleteTextures(1, &tileOverlayTextureId);
        glDeleteTextures(1, &groundRoadStateTextureId);
        glDeleteTextures(1, &tileLiftTextureId);
        glDeleteTextures(1, &tileStateTextureId);
        glDeleteBuffers(1, &lotVertexBufferId);
        if (generatedLotVertexBufferId != 0) {
            glDeleteBuffers(1, &generatedLotVertexBufferId);
        }
        glDeleteBuffers(1, &tileVertexBufferId);
        ClearApplicationDialogOwner(glfwGetWin32Window(window));
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    shaderProgram.bind();
    const GLint viewProjectionLocation = glGetUniformLocation(shaderProgram.programId(), "uViewProjection");
    GLuint lotMaterialTexture = 0;
    glGenTextures(1, &lotMaterialTexture);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D_ARRAY, lotMaterialTexture);
    const std::vector<unsigned char> lotMaterialPixels = BuildLotMaterialPixels();
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, kLotTextureSize, kLotTextureSize, MaterialCount, 0, GL_RGBA, GL_UNSIGNED_BYTE, lotMaterialPixels.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glUniform1i(glGetUniformLocation(shaderProgram.programId(), "uLotMaterials"), 7);
    glActiveTexture(GL_TEXTURE0);
    const GLint renderModeLocation = glGetUniformLocation(shaderProgram.programId(), "uRenderMode");
    const GLint tileTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uTileStateTexture");
    const GLint tileLiftTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uTileLiftTexture");
    const GLint tileOverlayTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uTileOverlayTexture");
    const GLint groundRoadStateTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uGroundRoadStateTexture");
    const GLint roadBaseAtlasTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadBaseAtlasTexture");
    const GLint roadArrowAtlasTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadArrowAtlasTexture");
    const GLint roadAtlasGridLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadAtlasGrid");
    const GLint roadDebugVisibleLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadDebugVisible");
    const GLint roadAlphaScaleLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadAlphaScale");
    const GLint roadTintColorLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadTintColor");
    const GLint roadTintStrengthLocation = glGetUniformLocation(shaderProgram.programId(), "uRoadTintStrength");
    const GLint lotAlphaScaleLocation = glGetUniformLocation(shaderProgram.programId(), "uLotAlphaScale");
    const GLint lotTintColorLocation = glGetUniformLocation(shaderProgram.programId(), "uLotTintColor");
    const GLint lotTintStrengthLocation = glGetUniformLocation(shaderProgram.programId(), "uLotTintStrength");
    const GLint regionPreviewTextureLocation = glGetUniformLocation(shaderProgram.programId(), "uRegionPreviewTexture");
    const GLint zoningOverlayVisibleLocation = glGetUniformLocation(shaderProgram.programId(), "uZoningOverlayVisible");
    const GLint tileOverlaySemanticModeLocation = glGetUniformLocation(shaderProgram.programId(), "uTileOverlaySemanticMode");
    const GLint tileOverlayGradientDirectionLocation = glGetUniformLocation(shaderProgram.programId(), "uTileOverlayGradientDirection");
    glUniform1i(tileTextureLocation, 0);
    glUniform1i(groundRoadStateTextureLocation, 1);
    glUniform1i(roadBaseAtlasTextureLocation, 2);
    glUniform1i(roadArrowAtlasTextureLocation, 3);
    glUniform1i(tileLiftTextureLocation, 4);
    glUniform1i(tileOverlayTextureLocation, 5);
    glUniform1i(regionPreviewTextureLocation, 6);
    glUniform2f(roadAtlasGridLocation, static_cast<float>(kRoadAtlasColumns), static_cast<float>(kRoadAtlasRows));
    glUniform1i(roadDebugVisibleLocation, 1);
    glUniform1f(roadAlphaScaleLocation, 1.0f);
    glUniform3f(roadTintColorLocation, 1.0f, 1.0f, 1.0f);
    glUniform1f(roadTintStrengthLocation, 0.0f);
    glUniform1f(lotAlphaScaleLocation, 1.0f);
    glUniform3f(lotTintColorLocation, 1.0f, 1.0f, 1.0f);
    glUniform1f(lotTintStrengthLocation, 0.0f);
    glUniform1i(zoningOverlayVisibleLocation, 1);
    glUniform1i(tileOverlaySemanticModeLocation, RendererOverlaySemanticIndex(RendererOverlaySemantic::TrafficCapacity));
    glUniform1i(tileOverlayGradientDirectionLocation, RendererOverlayGradientDirectionIndex(RendererOverlayGradientDirection::GoodToBad));

    std::vector<GLshort> tileStateChunkPixels;
    std::vector<std::uint8_t> tileLiftChunkPixels;
    std::vector<RendererScalarPayload> zoningOverlayChunkValues;
    std::vector<RendererScalarPayload> landValueOverlayChunkValues;
    std::vector<RendererScalarPayload> parkEffectOverlayChunkValues;
    std::vector<RendererScalarPayload> airPollutionOverlayChunkValues;
    std::vector<RendererScalarPayload> desirabilityOverlayChunkValues;
    std::vector<std::uint8_t> emptyGroundRoadChunkPixels;
    std::vector<LotInstanceData> lotInstances;
    std::vector<LotInstanceData> lotGhostInstances;
    std::vector<LotInstanceData> bulldozeLotInstances;
    std::vector<LotMeshDrawBatch> lotMeshBatches;
    std::vector<LotMeshDrawBatch> lotGhostMeshBatches;
    std::vector<LotMeshDrawBatch> bulldozeLotMeshBatches;
    std::vector<AreaOverlayInstanceData> areaOverlayInstances;
    std::vector<AreaOverlayInstanceData> zoningLotOverlayInstances;
    std::vector<RoadInstanceData> roadGhostInstances;
    std::vector<RouteArrowInstanceData> routeArrowInstances;
    std::vector<RegionPreviewTextureCache> regionPreviewTextureCaches;
    std::vector<UiQuadInstanceData> uiQuadInstances;
    auto drawLoadingScreen = [&] (const LoadingStatus& loadingStatus) {
        if (!loadingStatus.active) {
            return;
        }

        int loadingFramebufferWidth = 0;
        int loadingFramebufferHeight = 0;
        glfwGetFramebufferSize(window, &loadingFramebufferWidth, &loadingFramebufferHeight);
        loadingFramebufferWidth = std::max(1, loadingFramebufferWidth);
        loadingFramebufferHeight = std::max(1, loadingFramebufferHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, loadingFramebufferWidth, loadingFramebufferHeight);
        glClearColor(0.018f, 0.025f, 0.030f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        uiQuadInstances.clear();
        RendererAppendLoadingScreenQuads(
            loadingStatus.label,
            loadingStatus.progress,
            loadingFramebufferWidth,
            loadingFramebufferHeight,
            uiQuadInstances);
        DrawUiQuadInstances(
            shaderProgram,
            viewProjectionLocation,
            renderModeLocation,
            uiInstanceBufferId,
            uiVertexArrayId,
            loadingFramebufferWidth,
            loadingFramebufferHeight,
            uiQuadInstances);
        CopyRequestedScreenshotToClipboard(callbacks, loadingFramebufferWidth, loadingFramebufferHeight);
        glfwSwapBuffers(window);
    };

    gameSession_.setLoadingPresenter(drawLoadingScreen);
    LoadingStatus startupLoadingStatus;
    startupLoadingStatus.active = true;
    startupLoadingStatus.label = "Starting";
    startupLoadingStatus.progress = 0.04f;
    drawLoadingScreen(startupLoadingStatus);

    InGameWindow queryWindow;
    startupLoadingStatus.label = "Loading UI";
    startupLoadingStatus.progress = 0.08f;
    drawLoadingScreen(startupLoadingStatus);
    if (!queryWindow.loadFromXmlFile(BuildDataPath("UI\\lot_query.xml"))) {
        throw std::runtime_error("Renderer::run: failed to load query window UI XML.");
    }
    appController_.loadLocaleFromJsonFile(BuildDataPath("Locale\\en-US.json"));
    appController_.loadUiLayoutFromXmlFile(BuildDataPath("UI\\city_tools.xml"));
    startupLoadingStatus.label = "Loading tools";
    startupLoadingStatus.progress = 0.14f;
    drawLoadingScreen(startupLoadingStatus);
    appController_.loadRciToolsFromXmlFile(BuildDataPath("RCI\\rci_tools.xml"));
    std::uint64_t lastUploadedLotRevision = std::numeric_limits<std::uint64_t>::max();
    bool lastUploadedLotsDistant = false;
    std::uint64_t lastUploadedZoningLotRevision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t lastUploadedRciParcelLotRevision = std::numeric_limits<std::uint64_t>::max();
    OverlayMode lastUploadedRciParcelOverlayMode = OverlayMode::None;
    std::uint64_t lastUploadedQueryRouteRevision = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::uint64_t> lastUploadedGroundRoadChunkRevisions(chunkCaches.size(), std::numeric_limits<std::uint64_t>::max());
    std::vector<std::uint64_t> lastUploadedTileOverlayChunkRevisions(chunkCaches.size(), std::numeric_limits<std::uint64_t>::max());
    std::vector<std::uint64_t> lastUploadedZoningOverlayChunkRevisions(chunkCaches.size(), std::numeric_limits<std::uint64_t>::max());
    OverlayMode lastUploadedTileOverlayMode = OverlayMode::None;
    OverlayMode lastUploadedLotOverlayMode = OverlayMode::None;
    std::string lastUploadedDesirabilityOverlayToolId;
    bool lastFrameWasRegion = true;
    std::uint64_t lastHandledRenderStateRevision = gameSession_.renderStateRevision();
    std::uint64_t lastRegionPreviewCacheRevision = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::uint8_t> cityPreviewReadPixels(static_cast<std::size_t>(City::kPreviewWidth) * static_cast<std::size_t>(City::kPreviewHeight) * 4u, 0u);
    bool roadGhostPlacementValid = true;
    float roadGhostAlphaScale = kRoadGhostAlpha;
    bool lotGhostPlacementValid = true;
    std::uint64_t lastPreviewValidationRevision = BuildPreviewValidationRevision(gameSession_.renderStateRevision(), 0u, 0u, 0u);
    RoadPreviewValidationKey cachedRoadGhostValidationKey;
    bool hasCachedRoadGhostValidation = false;
    bool cachedRoadGhostPlacementValid = true;
    std::vector<RoadInstanceData> cachedRoadGhostInstances;
    RciPreviewValidationKey cachedRciGhostValidationKey;
    bool hasCachedRciGhostValidation = false;
    RciPlan cachedRciGhostPlan;
    bool hasCachedRciGhostPlan = false;
    bool cachedRciRoadGhostPlacementValid = true;
    std::vector<RoadInstanceData> cachedRciRoadGhostInstances;
    LotPreviewValidationKey cachedLotGhostValidationKey;
    bool hasCachedLotGhostValidation = false;
    bool cachedLotGhostPlacementValid = true;
    bool cachedLotGhostHasRenderInstances = false;
    std::vector<LotRenderInstance> cachedLotGhostRenderInstances;

    auto drawRegionPreviewLoadingScreen = [&] (const std::string& label, float progress) {
        LoadingStatus regionLoadingStatus;
        regionLoadingStatus.active = true;
        regionLoadingStatus.label = label;
        regionLoadingStatus.progress = progress;
        drawLoadingScreen(regionLoadingStatus);
    };

    auto staleRegionPreviewCount = [&] () -> std::size_t {
        const std::vector<std::unique_ptr<City> >& cities = gameSession_.region().cities();
        if (cities.empty()) {
            return 0u;
        }

        if (lastRegionPreviewCacheRevision != gameSession_.region().revision() ||
            regionPreviewTextureCaches.size() != cities.size()) {
            return cities.size();
        }

        std::size_t staleCount = 0u;
        std::size_t cityIndex = 0u;
        for (; cityIndex < cities.size(); ++cityIndex) {
            if (cityIndex >= regionPreviewTextureCaches.size() ||
                regionPreviewTextureCaches[cityIndex].textureId == 0 ||
                regionPreviewTextureCaches[cityIndex].previewRevision != cities[cityIndex]->previewRevision()) {
                ++staleCount;
            }
        }
        return staleCount;
    };

    auto regionPreviewLoadingProgress = [&] () -> float {
        const std::vector<std::unique_ptr<City> >& cities = gameSession_.region().cities();
        if (cities.empty()) {
            return 1.0f;
        }

        if (lastRegionPreviewCacheRevision != gameSession_.region().revision() ||
            regionPreviewTextureCaches.size() != cities.size()) {
            return 0.12f;
        }

        std::size_t readyCount = 0u;
        std::size_t cityIndex = 0u;
        for (; cityIndex < cities.size() && cityIndex < regionPreviewTextureCaches.size(); ++cityIndex) {
            if (regionPreviewTextureCaches[cityIndex].textureId != 0 &&
                regionPreviewTextureCaches[cityIndex].previewRevision == cities[cityIndex]->previewRevision()) {
                ++readyCount;
            }
        }

        const float normalizedReady = static_cast<float>(readyCount) / static_cast<float>(std::max<std::size_t>(cities.size(), 1u));
        return 0.12f + 0.78f * normalizedReady;
    };

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
        lastUploadedZoningOverlayChunkRevisions.assign(chunkCaches.size(), std::numeric_limits<std::uint64_t>::max());
        lastUploadedTileOverlayMode = OverlayMode::None;
        lastUploadedLotOverlayMode = OverlayMode::None;
        lastUploadedDesirabilityOverlayToolId.clear();
        lastUploadedLotRevision = std::numeric_limits<std::uint64_t>::max();
        lastUploadedZoningLotRevision = std::numeric_limits<std::uint64_t>::max();
        lastUploadedRciParcelLotRevision = std::numeric_limits<std::uint64_t>::max();
        lastUploadedRciParcelOverlayMode = OverlayMode::None;
        lastUploadedQueryRouteRevision = std::numeric_limits<std::uint64_t>::max();
        lotInstances.clear();
        lotGhostInstances.clear();
        bulldozeLotInstances.clear();
        lotMeshBatches.clear();
        lotGhostMeshBatches.clear();
        bulldozeLotMeshBatches.clear();
        areaOverlayInstances.clear();
        zoningLotOverlayInstances.clear();
        roadGhostInstances.clear();
        routeArrowInstances.clear();
        hasCachedRoadGhostValidation = false;
        cachedRoadGhostPlacementValid = true;
        cachedRoadGhostInstances.clear();
        hasCachedRciGhostValidation = false;
        cachedRciGhostPlan = RciPlan();
        hasCachedRciGhostPlan = false;
        cachedRciRoadGhostPlacementValid = true;
        cachedRciRoadGhostInstances.clear();
        hasCachedLotGhostValidation = false;
        cachedLotGhostPlacementValid = true;
        cachedLotGhostHasRenderInstances = false;
        cachedLotGhostRenderInstances.clear();
    };

    auto renderCityPreview = [&] (const CitySaveState& citySaveState, std::vector<std::uint8_t>& previewPixels) -> bool {
        if (!cityPreviewFramebufferComplete) {
            return false;
        }
        previewPixels.resize(static_cast<std::size_t>(City::kPreviewWidth) * static_cast<std::size_t>(City::kPreviewHeight) * 4u);

        simulationRuntime.stop();
        simulationRuntime.importCitySaveState(citySaveState, false);
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
            const std::map<std::uint16_t, GeneratedMeshRange> meshRanges = generatedLotMeshesLoaded
                ? BuildRuntimeMeshRanges(generatedMeshCatalog, snapshot.renderMeshBindings, true)
                : std::map<std::uint16_t, GeneratedMeshRange>();
            if (!generatedLotMeshesLoaded || !BuildGeneratedLotInstances(*snapshot.lots, meshRanges, false, lotInstances, lotMeshBatches)) {
                lotInstances = BuildLotInstances(*snapshot.lots);
                lotMeshBatches.clear();
            }
            glBindBuffer(GL_ARRAY_BUFFER, lotInstanceBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(lotInstances.size() * sizeof(LotInstanceData)),
                lotInstances.empty() ? 0 : &lotInstances[0],
                GL_DYNAMIC_DRAW);
        } else {
            lotInstances.clear();
            lotMeshBatches.clear();
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
        glUniform1i(roadDebugVisibleLocation, 1);

        glUniform1i(renderModeLocation, 0);
        glUniform1i(zoningOverlayVisibleLocation, 0);
        for (uploadChunkIndex = 0; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
            const TileChunkRenderCache& cache = chunkCaches[uploadChunkIndex];
            glBindVertexArray(cache.vertexArrayId);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, cache.instanceCount);
        }
        glUniform1i(zoningOverlayVisibleLocation, 1);

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
            glUniform1i(renderModeLocation, lotMeshBatches.empty() ? 1 : 9);
            glUniform1f(lotAlphaScaleLocation, 1.0f);
            glUniform3f(lotTintColorLocation, 1.0f, 1.0f, 1.0f);
            glUniform1f(lotTintStrengthLocation, 0.0f);
            if (lotMeshBatches.empty()) {
                glBindVertexArray(lotVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(lotInstances.size()));
            } else {
                glBindVertexArray(generatedLotVertexArrayId);
                DrawGeneratedLotBatches(lotMeshBatches);
            }
        }

        glBindVertexArray(0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, City::kPreviewWidth, City::kPreviewHeight, GL_RGBA, GL_UNSIGNED_BYTE, &previewPixels[0]);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        simulationRuntime.releasePublishedSnapshot(snapshot);
        return true;
    };

    gameSession_.setCityPreviewRenderer([&] (const CitySaveState& citySaveState, std::vector<std::uint8_t>& previewPixels) -> bool {
        return renderCityPreview(citySaveState, previewPixels);
    });
    gameSession_.loadOrCreateRegion();

    auto updateRegionPreviewTextures = [&] () -> bool {
        if (lastRegionPreviewCacheRevision != gameSession_.region().revision()) {
            DestroyRegionPreviewTextureCaches(regionPreviewTextureCaches);
            lastRegionPreviewCacheRevision = gameSession_.region().revision();
        }
        SynchronizeRegionPreviewTextures(gameSession_.region(), regionPreviewTextureCaches);

        std::size_t previewCityIndex = 0;
        for (; previewCityIndex < gameSession_.region().cities().size() && previewCityIndex < regionPreviewTextureCaches.size(); ++previewCityIndex) {
            City& city = *gameSession_.region().cities()[previewCityIndex];
            RegionPreviewTextureCache& cache = regionPreviewTextureCaches[previewCityIndex];
            if (cache.previewRevision == city.previewRevision()) {
                continue;
            }

            CitySaveState previewState;
            bool hasPreviewState = false;
            bool canPersistRenderedPreview = false;
            if (city.hasSaveState()) {
                previewState = city.saveState();
                hasPreviewState = true;
                canPersistRenderedPreview = !city.isSaveStateDirty();
            } else if (gameSession_.loadCityPreviewPixels(city, cityPreviewReadPixels)) {
                UploadRegionPreviewTexture(cache, city, cityPreviewReadPixels);
                if (cache.previewRevision == city.previewRevision()) {
                    continue;
                }
            } else {
                UploadRegionPreviewPlaceholderTexture(cache, city);
                continue;
            }

            if (hasPreviewState && renderCityPreview(previewState, cityPreviewReadPixels)) {
                UploadRegionPreviewTexture(cache, city, cityPreviewReadPixels);
                if (canPersistRenderedPreview) {
                    gameSession_.saveCityPreviewPixels(city, cityPreviewReadPixels);
                }
                city.unloadCleanSaveState();
                if (cache.previewRevision == city.previewRevision()) {
                    continue;
                }
            }
            if (cache.previewRevision != city.previewRevision()) {
                UploadRegionPreviewPlaceholderTexture(cache, city);
            }
        }

        if (regionPreviewTextureCaches.size() != gameSession_.region().cities().size()) {
            return false;
        }

        std::size_t cacheIndex = 0;
        for (; cacheIndex < gameSession_.region().cities().size(); ++cacheIndex) {
            if (regionPreviewTextureCaches[cacheIndex].previewRevision != gameSession_.region().cities()[cacheIndex]->previewRevision()) {
                return false;
            }
        }

        return true;
    };

    int renderedFrames = 0;
    RendererFrameMetrics lastFrameMetrics;
    std::chrono::steady_clock::time_point counterStart = std::chrono::steady_clock::now();
    SimulationDateSettings dateSettings = appConfig_.dateSettings;

    while (!glfwWindowShouldClose(window)) {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        appController_.setFramebufferSize(framebufferWidth, framebufferHeight);
        const bool shiftDown =
            glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        const bool controlDown =
            glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        appController_.setModifierKeys(shiftDown, controlDown);

        if (gameSession_.isLoading()) {
            drawLoadingScreen(gameSession_.loadingStatus());
            glfwPollEvents();
            continue;
        }

        appController_.refreshQueryResultIfNeeded();
        const ViewState viewState = appController_.viewState();
        const bool rciOverlayActive = viewState.overlayMode == OverlayMode::Rci;
        if (gameSession_.isRegionMode()) {
            const bool returningFromCity = !lastFrameWasRegion;
            const std::size_t stalePreviewCountBeforeUpdate = staleRegionPreviewCount();
            if (stalePreviewCountBeforeUpdate > 0u) {
                if (returningFromCity) {
                    std::cout << "Refreshing " << stalePreviewCountBeforeUpdate << " stale region preview"
                        << (stalePreviewCountBeforeUpdate == 1u ? "." : "s.") << std::endl;
                }
                drawRegionPreviewLoadingScreen(returningFromCity ? "Loading region" : "Loading region previews", regionPreviewLoadingProgress());
            }

            if (!updateRegionPreviewTextures()) {
                appController_.setHoveredRegionCity(0, 0, false);
                drawRegionPreviewLoadingScreen(returningFromCity ? "Loading region" : "Loading region previews", regionPreviewLoadingProgress());
                glfwPollEvents();
                lastFrameWasRegion = true;
                continue;
            }
            if (stalePreviewCountBeforeUpdate > 0u) {
                drawRegionPreviewLoadingScreen(returningFromCity ? "Loading region" : "Loading region previews", 1.0f);
            }

            const CameraState regionCameraState = BuildRegionCameraState(viewState, gameSession_.region());
            int hoveredRegionX = 0;
            int hoveredRegionY = 0;
            const bool hasHoveredRegion = TryPickRegionCity(viewState, regionCameraState, gameSession_.region(), hoveredRegionX, hoveredRegionY);
            appController_.setHoveredRegionCity(hoveredRegionX, hoveredRegionY, hasHoveredRegion);
            appController_.processPendingRegionClick();
            if (!gameSession_.isRegionMode() || gameSession_.isLoading()) {
                glfwPollEvents();
                lastFrameWasRegion = true;
                continue;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.08f, 0.11f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
            uiQuadInstances.clear();
            AppendHudTextPanel(
                uiQuadInstances,
                BuildPopulationLabel("Region Population: ", gameSession_.region().population()),
                std::max(16.0f, static_cast<float>(framebufferWidth) - 406.0f),
                16.0f,
                390.0f);
            {
                std::vector<std::string> regionMenuIds;
                regionMenuIds.push_back("region_exit");
                if (appController_.uiLayout().menuVisible("region_escape_menu")) {
                    regionMenuIds.push_back("region_escape_menu");
                }
                if (appController_.uiLayout().menuVisible("exit_confirm_dialog")) {
                    regionMenuIds.push_back("exit_confirm_dialog");
                }
                if (appController_.uiLayout().menuVisible("quit_region_confirm_dialog")) {
                    regionMenuIds.push_back("quit_region_confirm_dialog");
                }
                if (appController_.uiLayout().menuVisible("city_switch_confirm_dialog")) {
                    regionMenuIds.push_back("city_switch_confirm_dialog");
                }
                const std::vector<std::string> activeActions;
                std::vector<UiQuadInstanceData> menuQuadInstances = RendererBuildUiMenuQuads(
                    appController_.uiLayout(),
                    framebufferWidth,
                    framebufferHeight,
                    activeActions,
                    regionMenuIds);
                uiQuadInstances.insert(uiQuadInstances.end(), menuQuadInstances.begin(), menuQuadInstances.end());
            }
            const ApplicationWarning* regionWarning = gameSession_.currentApplicationWarning();
            if (regionWarning != 0) {
                AppendApplicationWarningPanel(uiQuadInstances, *regionWarning, framebufferWidth, framebufferHeight);
                std::vector<std::string> warningMenuIds;
                warningMenuIds.push_back("app_warning_dialog");
                const std::vector<std::string> activeActions;
                std::vector<UiQuadInstanceData> warningMenuQuadInstances = RendererBuildUiMenuQuads(
                    appController_.uiLayout(),
                    framebufferWidth,
                    framebufferHeight,
                    activeActions,
                    warningMenuIds);
                uiQuadInstances.insert(uiQuadInstances.end(), warningMenuQuadInstances.begin(), warningMenuQuadInstances.end());
            }
            DrawUiQuadInstances(
                shaderProgram,
                viewProjectionLocation,
                renderModeLocation,
                uiInstanceBufferId,
                uiVertexArrayId,
                framebufferWidth,
                framebufferHeight,
                uiQuadInstances);
            CopyRequestedScreenshotToClipboard(callbacks, framebufferWidth, framebufferHeight);
            glfwSwapBuffers(window);
            glfwPollEvents();
            if (appController_.quitRequested()) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            lastFrameWasRegion = true;
            continue;
        }

        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.08f, 0.11f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const std::uint64_t renderStateRevision = gameSession_.renderStateRevision();
        if (lastFrameWasRegion || lastHandledRenderStateRevision != renderStateRevision) {
            invalidateCityRenderCaches();
            lastFrameWasRegion = false;
            lastHandledRenderStateRevision = renderStateRevision;
        }

        const CameraState cameraState = BuildCameraState(viewState);

        int hoveredTileX = 0;
        int hoveredTileY = 0;
        const bool hasHoveredTile = TryPickGroundTile(viewState, cameraState, simulationRuntime.mapWidth(), simulationRuntime.mapHeight(), hoveredTileX, hoveredTileY);
        appController_.setHoveredTile(hoveredTileX, hoveredTileY, hasHoveredTile);

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS) {
            appController_.onLeftMouseButtonHeld();
        }

        RendererFrameMetrics frameMetrics;
        frameMetrics.totalChunkCount = static_cast<int>(chunkCaches.size());

        const std::uint64_t previewValidationRevision = lastPreviewValidationRevision;
        const ViewState previewViewState = appController_.viewState();

        RciPlan rciGhostPlan;
        bool hasRciGhostPlan = false;
        const RciPreviewValidationKey rciGhostValidationKey = BuildRciPreviewValidationKey(previewViewState, previewValidationRevision);
        if (rciGhostValidationKey.active) {
            if (!hasCachedRciGhostValidation || !SameRciPreviewValidationKey(cachedRciGhostValidationKey, rciGhostValidationKey)) {
                cachedRciGhostValidationKey = rciGhostValidationKey;
                cachedRciGhostPlan = RciPlan();
                cachedRciRoadGhostInstances.clear();
                cachedRciRoadGhostPlacementValid = true;
                hasCachedRciGhostPlan = appController_.rciPreviewPlan(cachedRciGhostPlan);
                if (hasCachedRciGhostPlan) {
                    std::size_t roadPlanIndex = 0;
                    for (; roadPlanIndex < cachedRciGhostPlan.roadPlans.size(); ++roadPlanIndex) {
                        const RoadStrokeCommand rciRoadStroke = BuildRciRoadStrokeCommand(cachedRciGhostPlan.roadPlans[roadPlanIndex]);
                        std::vector<RoadInstanceData> rciRoadInstances = BuildRoadPreviewInstances(rciRoadStroke, simulationRuntime.mapWidth(), simulationRuntime.mapHeight());
                        cachedRciRoadGhostInstances.insert(cachedRciRoadGhostInstances.end(), rciRoadInstances.begin(), rciRoadInstances.end());
                        cachedRciRoadGhostPlacementValid = simulationRuntime.canPlaceRoadStroke(rciRoadStroke) && cachedRciRoadGhostPlacementValid;
                    }
                }
                hasCachedRciGhostValidation = true;
            }

            if (hasCachedRciGhostPlan) {
                rciGhostPlan = cachedRciGhostPlan;
                hasRciGhostPlan = true;
            }
        } else {
            hasCachedRciGhostValidation = false;
            cachedRciGhostPlan = RciPlan();
            hasCachedRciGhostPlan = false;
            cachedRciRoadGhostPlacementValid = true;
            cachedRciRoadGhostInstances.clear();
        }
        int zonePreviewMinTileX = 0;
        int zonePreviewMinTileY = 0;
        int zonePreviewMaxTileX = 0;
        int zonePreviewMaxTileY = 0;
        std::uint16_t zonePreviewType = TileZoningNone;
        const bool hasZonePreviewRect = appController_.zonePreviewRect(
            zonePreviewMinTileX,
            zonePreviewMinTileY,
            zonePreviewMaxTileX,
            zonePreviewMaxTileY,
            zonePreviewType);

        RoadStrokeCommand roadGhostCommand;
        const bool hasRoadGhostCommand = appController_.roadPreviewStroke(roadGhostCommand);
        if (hasRoadGhostCommand) {
            const RoadPreviewValidationKey roadGhostValidationKey = BuildRoadPreviewValidationKey(roadGhostCommand, previewValidationRevision);
            if (!hasCachedRoadGhostValidation || !SameRoadPreviewValidationKey(cachedRoadGhostValidationKey, roadGhostValidationKey)) {
                cachedRoadGhostValidationKey = roadGhostValidationKey;
                cachedRoadGhostInstances = BuildRoadPreviewInstances(roadGhostCommand, simulationRuntime.mapWidth(), simulationRuntime.mapHeight());
                cachedRoadGhostPlacementValid = simulationRuntime.canPlaceRoadStroke(roadGhostCommand);
                hasCachedRoadGhostValidation = true;
            }
        } else {
            hasCachedRoadGhostValidation = false;
            cachedRoadGhostPlacementValid = true;
            cachedRoadGhostInstances.clear();
        }

        if (hasRoadGhostCommand || (hasRciGhostPlan && !cachedRciRoadGhostInstances.empty())) {
            const std::chrono::steady_clock::time_point roadGhostUploadStart = std::chrono::steady_clock::now();
            roadGhostInstances.clear();
            roadGhostPlacementValid = true;
            roadGhostAlphaScale = hasRoadGhostCommand ? kRoadGhostAlpha : 0.50f;
            if (hasRoadGhostCommand) {
                roadGhostInstances.insert(roadGhostInstances.end(), cachedRoadGhostInstances.begin(), cachedRoadGhostInstances.end());
                roadGhostPlacementValid = cachedRoadGhostPlacementValid;
            }

            if (hasRciGhostPlan) {
                roadGhostInstances.insert(roadGhostInstances.end(), cachedRciRoadGhostInstances.begin(), cachedRciRoadGhostInstances.end());
                roadGhostPlacementValid = cachedRciRoadGhostPlacementValid && roadGhostPlacementValid;
            }

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
            roadGhostPlacementValid = true;
            roadGhostAlphaScale = kRoadGhostAlpha;
        }

        std::string lotGhostAssetId;
        int lotGhostTileX = 0;
        int lotGhostTileY = 0;
        int lotGhostRotationSteps = 0;
        const std::vector<LotRenderInstance>* lotGhostRenderInstances = 0;
        bool hasLotGhostRenderInstances = false;
        std::chrono::steady_clock::time_point lotGhostUploadStart;
        const bool hasLotGhostRequest = appController_.lotPreviewRequest(lotGhostAssetId, lotGhostTileX, lotGhostTileY, lotGhostRotationSteps);
        if (hasLotGhostRequest) {
            lotGhostUploadStart = std::chrono::steady_clock::now();
            lotGhostInstances.clear();
            lotGhostMeshBatches.clear();

            const LotPreviewValidationKey lotGhostValidationKey = BuildLotPreviewValidationKey(lotGhostAssetId, lotGhostTileX, lotGhostTileY, lotGhostRotationSteps, previewValidationRevision);
            if (!hasCachedLotGhostValidation || !SameLotPreviewValidationKey(cachedLotGhostValidationKey, lotGhostValidationKey)) {
                cachedLotGhostValidationKey = lotGhostValidationKey;
                cachedLotGhostRenderInstances.clear();
                cachedLotGhostPlacementValid = false;
                cachedLotGhostHasRenderInstances = simulationRuntime.buildLotPreviewInstances(lotGhostAssetId, lotGhostTileX, lotGhostTileY, lotGhostRotationSteps, cachedLotGhostRenderInstances, cachedLotGhostPlacementValid);
                if (!cachedLotGhostHasRenderInstances) {
                    cachedLotGhostPlacementValid = false;
                }
                hasCachedLotGhostValidation = true;
            }

            lotGhostPlacementValid = cachedLotGhostPlacementValid;
            if (cachedLotGhostHasRenderInstances) {
                lotGhostRenderInstances = &cachedLotGhostRenderInstances;
                hasLotGhostRenderInstances = true;
            }
        } else {
            hasCachedLotGhostValidation = false;
            cachedLotGhostPlacementValid = true;
            cachedLotGhostHasRenderInstances = false;
            cachedLotGhostRenderInstances.clear();
            lotGhostInstances.clear();
            lotGhostMeshBatches.clear();
            lotGhostPlacementValid = true;
        }

        const PublishedWorldSnapshot snapshot = simulationRuntime.acquirePublishedSnapshot();
        lastPreviewValidationRevision = BuildPreviewValidationRevision(renderStateRevision, snapshot.lotRevision, snapshot.zoningLotRevision, snapshot.roadRevision);

        if (hasLotGhostRequest) {
            if (hasLotGhostRenderInstances && lotGhostRenderInstances != 0) {
                const std::map<std::uint16_t, GeneratedMeshRange> meshRanges = generatedLotMeshesLoaded
                    ? BuildRuntimeMeshRanges(generatedMeshCatalog, snapshot.renderMeshBindings, viewState.visibleTiles > 64)
                    : std::map<std::uint16_t, GeneratedMeshRange>();
                if (!generatedLotMeshesLoaded || !BuildGeneratedLotInstances(*lotGhostRenderInstances, meshRanges, false, lotGhostInstances, lotGhostMeshBatches)) {
                    std::size_t lotGhostIndex = 0;
                    for (; lotGhostIndex < lotGhostRenderInstances->size(); ++lotGhostIndex) {
                        lotGhostInstances.push_back(BuildLotInstance((*lotGhostRenderInstances)[lotGhostIndex]));
                    }
                    lotGhostMeshBatches.clear();
                }
            } else {
                lotGhostInstances.clear();
                lotGhostMeshBatches.clear();
            }
            glBindBuffer(GL_ARRAY_BUFFER, lotGhostInstanceBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(lotGhostInstances.size() * sizeof(LotInstanceData)),
                lotGhostInstances.empty() ? 0 : &lotGhostInstances[0],
                GL_DYNAMIC_DRAW);
            frameMetrics.lotGhostUploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lotGhostUploadStart).count();
            frameMetrics.lotGhostInstanceCount = static_cast<int>(lotGhostInstances.size());
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

        int bulldozeMinTileX = 0;
        int bulldozeMinTileY = 0;
        int bulldozeMaxTileX = 0;
        int bulldozeMaxTileY = 0;
        bool areaOverlayUsesParcelStyle = false;
        if (appController_.bulldozePreviewRect(bulldozeMinTileX, bulldozeMinTileY, bulldozeMaxTileX, bulldozeMaxTileY)) {
            areaOverlayInstances.clear();
            areaOverlayInstances.push_back(BuildAreaOverlayInstance(bulldozeMinTileX, bulldozeMinTileY, bulldozeMaxTileX, bulldozeMaxTileY));
            glBindBuffer(GL_ARRAY_BUFFER, areaOverlayInstanceBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(areaOverlayInstances.size() * sizeof(AreaOverlayInstanceData)),
                &areaOverlayInstances[0],
                GL_DYNAMIC_DRAW);

            bulldozeLotInstances.clear();
            bulldozeLotMeshBatches.clear();
            if (snapshot.lots != 0) {
                const std::map<std::uint16_t, GeneratedMeshRange> meshRanges = generatedLotMeshesLoaded
                    ? BuildRuntimeMeshRanges(generatedMeshCatalog, snapshot.renderMeshBindings, viewState.visibleTiles > 64)
                    : std::map<std::uint16_t, GeneratedMeshRange>();
                if (!generatedLotMeshesLoaded ||
                    !BuildGeneratedLotInstancesInTileRect(*snapshot.lots, bulldozeMinTileX, bulldozeMinTileY, bulldozeMaxTileX, bulldozeMaxTileY, meshRanges, bulldozeLotInstances, bulldozeLotMeshBatches)) {
                    bulldozeLotInstances = BuildLotInstancesInTileRect(*snapshot.lots, bulldozeMinTileX, bulldozeMinTileY, bulldozeMaxTileX, bulldozeMaxTileY);
                    bulldozeLotMeshBatches.clear();
                }
            }
            glBindBuffer(GL_ARRAY_BUFFER, lotGhostInstanceBufferId);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(bulldozeLotInstances.size() * sizeof(LotInstanceData)),
                bulldozeLotInstances.empty() ? 0 : &bulldozeLotInstances[0],
                GL_DYNAMIC_DRAW);
        } else {
            bulldozeLotInstances.clear();
            bulldozeLotMeshBatches.clear();
            if (hasRciGhostPlan) {
                areaOverlayInstances.clear();
                areaOverlayUsesParcelStyle = rciGhostPlan.mode != RciPlanMode::Area;
                if (!rciGhostPlan.lots.empty()) {
                    std::size_t lotIndex = 0;
                    for (; lotIndex < rciGhostPlan.lots.size(); ++lotIndex) {
                        const RciLot& lot = rciGhostPlan.lots[lotIndex];
                        areaOverlayInstances.push_back(BuildAreaOverlayInstance(
                            lot.rect.minTileX,
                            lot.rect.minTileY,
                            lot.rect.maxTileX,
                            lot.rect.maxTileY,
                            lot.color.r,
                            lot.color.g,
                            lot.color.b,
                            0.50f));
                    }
                } else {
                    std::size_t zoneRectIndex = 0;
                    for (; zoneRectIndex < rciGhostPlan.zoneRects.size(); ++zoneRectIndex) {
                        const RciRect& rect = rciGhostPlan.zoneRects[zoneRectIndex];
                        areaOverlayInstances.push_back(BuildAreaOverlayInstance(
                            rect.minTileX,
                            rect.minTileY,
                            rect.maxTileX,
                            rect.maxTileY,
                            rciGhostPlan.color.r,
                            rciGhostPlan.color.g,
                            rciGhostPlan.color.b,
                            0.50f));
                    }
                }
                glBindBuffer(GL_ARRAY_BUFFER, areaOverlayInstanceBufferId);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(areaOverlayInstances.size() * sizeof(AreaOverlayInstanceData)),
                    areaOverlayInstances.empty() ? 0 : &areaOverlayInstances[0],
                    GL_DYNAMIC_DRAW);
            } else if (hasZonePreviewRect) {
                areaOverlayInstances.clear();
                areaOverlayInstances.push_back(BuildAreaOverlayInstance(
                    zonePreviewMinTileX,
                    zonePreviewMinTileY,
                    zonePreviewMaxTileX,
                    zonePreviewMaxTileY,
                    zonePreviewType == TileZoningNone ? 0.66f : 0.72f,
                    zonePreviewType == TileZoningNone ? 0.66f : 0.92f,
                    zonePreviewType == TileZoningNone ? 0.70f : 0.44f,
                    0.36f));
                glBindBuffer(GL_ARRAY_BUFFER, areaOverlayInstanceBufferId);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(areaOverlayInstances.size() * sizeof(AreaOverlayInstanceData)),
                    &areaOverlayInstances[0],
                    GL_DYNAMIC_DRAW);
            } else {
                areaOverlayInstances.clear();
            }
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
            if (snapshot.chunkRevisions != 0) {
                for (uploadChunkIndex = 0; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
                    const std::uint64_t publishedRevision = (*snapshot.chunkRevisions)[uploadChunkIndex];
                    if (lastUploadedZoningOverlayChunkRevisions[uploadChunkIndex] == publishedRevision) {
                        continue;
                    }

                    if (visibleChunkFlags[uploadChunkIndex] == 0u) {
                        continue;
                    }

                    const ChunkRect& chunkRect = chunkCaches[uploadChunkIndex].chunkRect;
                    FillZoningOverlayChunkValues(snapshot, chunkRect, zoningOverlayChunkValues);
                    UploadScalarOverlayChunkTexture(zoningOverlayTextureId, chunkRect, zoningOverlayChunkValues);
                    lastUploadedZoningOverlayChunkRevisions[uploadChunkIndex] = publishedRevision;
                }
            }

            if (lastUploadedTileOverlayMode != viewState.overlayMode ||
                lastUploadedDesirabilityOverlayToolId != viewState.overlayRciTypeId) {
                lastUploadedTileOverlayChunkRevisions.assign(chunkCaches.size(), std::numeric_limits<std::uint64_t>::max());
                lastUploadedTileOverlayMode = viewState.overlayMode;
                lastUploadedDesirabilityOverlayToolId = viewState.overlayRciTypeId;
            }

            if (viewState.overlayMode == OverlayMode::TrafficCapacity && snapshot.tileOverlayState != 0 && snapshot.tileOverlayChunkRevisions != 0) {
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
            } else if ((viewState.overlayMode == OverlayMode::LandValue || viewState.overlayMode == OverlayMode::ParkEffect || viewState.overlayMode == OverlayMode::AirPollution) && snapshot.tiles != 0) {
                for (uploadChunkIndex = 0; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
                    const std::uint64_t publishedRevision = snapshot.generation;
                    if (lastUploadedTileOverlayChunkRevisions[uploadChunkIndex] == publishedRevision) {
                        continue;
                    }

                    if (visibleChunkFlags[uploadChunkIndex] == 0u) {
                        ++frameMetrics.tileOverlayDeferredChunkCount;
                        continue;
                    }

                    const ChunkRect& chunkRect = chunkCaches[uploadChunkIndex].chunkRect;
                    if (viewState.overlayMode == OverlayMode::LandValue) {
                        FillLandValueOverlayChunkValues(snapshot, chunkRect, landValueOverlayChunkValues);
                        UploadScalarOverlayChunkTexture(tileOverlayTextureId, chunkRect, landValueOverlayChunkValues);
                    } else if (viewState.overlayMode == OverlayMode::ParkEffect) {
                        FillParkEffectOverlayChunkValues(snapshot, chunkRect, parkEffectOverlayChunkValues);
                        UploadScalarOverlayChunkTexture(tileOverlayTextureId, chunkRect, parkEffectOverlayChunkValues);
                    } else {
                        FillAirPollutionOverlayChunkValues(snapshot, chunkRect, airPollutionOverlayChunkValues);
                        UploadScalarOverlayChunkTexture(tileOverlayTextureId, chunkRect, airPollutionOverlayChunkValues);
                    }
                    lastUploadedTileOverlayChunkRevisions[uploadChunkIndex] = publishedRevision;
                    ++frameMetrics.tileOverlayUploadedChunkCount;
                }
            } else if (viewState.overlayMode == OverlayMode::RciDesirability && snapshot.tiles != 0 && !viewState.overlayRciTypeId.empty()) {
                for (uploadChunkIndex = 0; uploadChunkIndex < chunkCaches.size(); ++uploadChunkIndex) {
                    const std::uint64_t publishedRevision = snapshot.generation;
                    if (lastUploadedTileOverlayChunkRevisions[uploadChunkIndex] == publishedRevision) {
                        continue;
                    }

                    if (visibleChunkFlags[uploadChunkIndex] == 0u) {
                        ++frameMetrics.tileOverlayDeferredChunkCount;
                        continue;
                    }

                    const ChunkRect& chunkRect = chunkCaches[uploadChunkIndex].chunkRect;
                    if (simulationRuntime.fillRciDesirabilityOverlayChunkValues(viewState.overlayRciTypeId, snapshot, chunkRect, desirabilityOverlayChunkValues)) {
                        UploadScalarOverlayChunkTexture(tileOverlayTextureId, chunkRect, desirabilityOverlayChunkValues);
                        lastUploadedTileOverlayChunkRevisions[uploadChunkIndex] = publishedRevision;
                        ++frameMetrics.tileOverlayUploadedChunkCount;
                    }
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

            if (snapshot.lots != 0 && (snapshot.lotRevision != lastUploadedLotRevision || viewState.overlayMode != lastUploadedLotOverlayMode || lastUploadedLotsDistant != (viewState.visibleTiles > 64))) {
                const std::chrono::steady_clock::time_point lotUploadStart = std::chrono::steady_clock::now();
                const std::map<std::uint16_t, GeneratedMeshRange> meshRanges = generatedLotMeshesLoaded
                    ? BuildRuntimeMeshRanges(generatedMeshCatalog, snapshot.renderMeshBindings, viewState.visibleTiles > 64)
                    : std::map<std::uint16_t, GeneratedMeshRange>();
                if (!generatedLotMeshesLoaded || !BuildGeneratedLotInstances(*snapshot.lots, meshRanges, rciOverlayActive, lotInstances, lotMeshBatches)) {
                    lotInstances = BuildLotInstances(*snapshot.lots, rciOverlayActive);
                    lotMeshBatches.clear();
                }
                glBindBuffer(GL_ARRAY_BUFFER, lotInstanceBufferId);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(lotInstances.size() * sizeof(LotInstanceData)),
                    lotInstances.empty() ? 0 : &lotInstances[0],
                    GL_DYNAMIC_DRAW);
                lastUploadedLotRevision = snapshot.lotRevision;
                lastUploadedLotsDistant = viewState.visibleTiles > 64;
                lastUploadedLotOverlayMode = viewState.overlayMode;
                frameMetrics.lotUploadMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lotUploadStart).count();
            }

            if (snapshot.lots != 0 &&
                (snapshot.lotRevision != lastUploadedRciParcelLotRevision ||
                 snapshot.zoningLotRevision != lastUploadedZoningLotRevision ||
                 viewState.overlayMode != lastUploadedRciParcelOverlayMode)) {
                zoningLotOverlayInstances = BuildRciParcelOverlayInstances(*snapshot.lots, snapshot.zoningLots, rciOverlayActive);
                glBindBuffer(GL_ARRAY_BUFFER, zoningLotOverlayInstanceBufferId);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(zoningLotOverlayInstances.size() * sizeof(AreaOverlayInstanceData)),
                    zoningLotOverlayInstances.empty() ? 0 : &zoningLotOverlayInstances[0],
                    GL_DYNAMIC_DRAW);
                lastUploadedRciParcelLotRevision = snapshot.lotRevision;
                lastUploadedZoningLotRevision = snapshot.zoningLotRevision;
                lastUploadedRciParcelOverlayMode = viewState.overlayMode;
            }

            shaderProgram.bind();
            glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, cameraState.viewProjection.data);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tileStateTextureId);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, groundRoadStateTextureId);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, viewState.roadDebugGraphicsEnabled ? roadBaseAtlasTextureId : roadBaseCleanAtlasTextureId);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, roadArrowAtlasTextureId);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, tileLiftTextureId);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, zoningOverlayTextureId);
            glUniform1i(roadDebugVisibleLocation, viewState.roadDebugGraphicsEnabled ? 1 : 0);

            glUniform1i(renderModeLocation, 0);
            glUniform1i(zoningOverlayVisibleLocation, 1);
            const std::chrono::steady_clock::time_point tileDrawStart = std::chrono::steady_clock::now();
            std::size_t visibleIndex = 0;
            for (; visibleIndex < visibleChunkIndices.size(); ++visibleIndex) {
                const TileChunkRenderCache& cache = chunkCaches[visibleChunkIndices[visibleIndex]];
                glBindVertexArray(cache.vertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, cache.instanceCount);
            }
            frameMetrics.tileDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tileDrawStart).count();

            if (!zoningLotOverlayInstances.empty()) {
                glUniform1i(renderModeLocation, 8);
                glDepthMask(GL_FALSE);
                glDisable(GL_DEPTH_TEST);
                glBindVertexArray(zoningLotOverlayVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(zoningLotOverlayInstances.size()));
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
            }

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

            if (!areaOverlayInstances.empty()) {
                glUniform1i(renderModeLocation, areaOverlayUsesParcelStyle ? 8 : 7);
                glDepthMask(GL_FALSE);
                glDisable(GL_DEPTH_TEST);
                glBindVertexArray(areaOverlayVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(areaOverlayInstances.size()));
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
            }

            if (!roadGhostInstances.empty()) {
                glUniform1f(roadAlphaScaleLocation, roadGhostPlacementValid ? roadGhostAlphaScale : 0.55f);
                if (roadGhostPlacementValid) {
                    glUniform3f(roadTintColorLocation, 0.55f, 0.82f, 1.0f);
                    glUniform1f(roadTintStrengthLocation, 0.38f);
                } else {
                    glUniform3f(roadTintColorLocation, 1.0f, 0.12f, 0.09f);
                    glUniform1f(roadTintStrengthLocation, 0.72f);
                }
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
                glUniform1i(renderModeLocation, lotGhostMeshBatches.empty() ? 1 : 9);
                glUniform1f(lotAlphaScaleLocation, lotGhostPlacementValid ? kLotGhostAlpha : 0.55f);
                if (lotGhostPlacementValid) {
                    glUniform3f(lotTintColorLocation, 0.76f, 1.0f, 0.58f);
                    glUniform1f(lotTintStrengthLocation, 0.42f);
                } else {
                    glUniform3f(lotTintColorLocation, 1.0f, 0.12f, 0.09f);
                    glUniform1f(lotTintStrengthLocation, 0.72f);
                }
                glDepthMask(GL_FALSE);
                const std::chrono::steady_clock::time_point lotGhostDrawStart = std::chrono::steady_clock::now();
                if (lotGhostMeshBatches.empty()) {
                    glBindVertexArray(lotGhostVertexArrayId);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(lotGhostInstances.size()));
                } else {
                    glBindVertexArray(generatedLotGhostVertexArrayId);
                    DrawGeneratedLotBatches(lotGhostMeshBatches);
                }
                frameMetrics.lotGhostDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lotGhostDrawStart).count();
                glDepthMask(GL_TRUE);
                glUniform1f(lotAlphaScaleLocation, 1.0f);
                glUniform3f(lotTintColorLocation, 1.0f, 1.0f, 1.0f);
                glUniform1f(lotTintStrengthLocation, 0.0f);
            }

            if (!lotInstances.empty()) {
                glUniform1i(renderModeLocation, lotMeshBatches.empty() ? 1 : 9);
                glUniform1f(lotAlphaScaleLocation, 1.0f);
                glUniform3f(lotTintColorLocation, 1.0f, 1.0f, 1.0f);
                glUniform1f(lotTintStrengthLocation, 0.0f);
                const std::chrono::steady_clock::time_point lotDrawStart = std::chrono::steady_clock::now();
                if (lotMeshBatches.empty()) {
                    glBindVertexArray(lotVertexArrayId);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(lotInstances.size()));
                } else {
                    glBindVertexArray(generatedLotVertexArrayId);
                    DrawGeneratedLotBatches(lotMeshBatches);
                }
                frameMetrics.lotDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lotDrawStart).count();
            }

            if (!bulldozeLotInstances.empty()) {
                glUniform1i(renderModeLocation, bulldozeLotMeshBatches.empty() ? 1 : 9);
                glUniform1f(lotAlphaScaleLocation, 0.70f);
                glUniform3f(lotTintColorLocation, 1.0f, 0.05f, 0.03f);
                glUniform1f(lotTintStrengthLocation, 0.78f);
                glDepthMask(GL_FALSE);
                glDepthFunc(GL_LEQUAL);
                if (bulldozeLotMeshBatches.empty()) {
                    glBindVertexArray(lotGhostVertexArrayId);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(bulldozeLotInstances.size()));
                } else {
                    glBindVertexArray(generatedLotGhostVertexArrayId);
                    DrawGeneratedLotBatches(bulldozeLotMeshBatches);
                }
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                glUniform1f(lotAlphaScaleLocation, 1.0f);
                glUniform3f(lotTintColorLocation, 1.0f, 1.0f, 1.0f);
                glUniform1f(lotTintStrengthLocation, 0.0f);
            }

            const std::chrono::steady_clock::time_point tileOverlayDrawStart = std::chrono::steady_clock::now();
            const bool drawTileOverlay =
                (viewState.overlayMode == OverlayMode::TrafficCapacity && snapshot.tileOverlayState != 0) ||
                (viewState.overlayMode == OverlayMode::LandValue && snapshot.tiles != 0) ||
                (viewState.overlayMode == OverlayMode::ParkEffect && snapshot.tiles != 0) ||
                (viewState.overlayMode == OverlayMode::AirPollution && snapshot.tiles != 0) ||
                (viewState.overlayMode == OverlayMode::RciDesirability && snapshot.tiles != 0 && !viewState.overlayRciTypeId.empty());
            if (drawTileOverlay) {
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D, tileOverlayTextureId);
                const RendererOverlaySemantic tileOverlaySemantic = RendererOverlaySemanticForOverlayMode(viewState.overlayMode);
                glUniform1i(tileOverlaySemanticModeLocation, RendererOverlaySemanticIndex(tileOverlaySemantic));
                glUniform1i(
                    tileOverlayGradientDirectionLocation,
                    RendererOverlayGradientDirectionIndex(RendererOverlayGradientDirectionForSemantic(tileOverlaySemantic)));
                glUniform1i(renderModeLocation, 3);
                glDepthMask(GL_FALSE);
                glDisable(GL_DEPTH_TEST);
                for (visibleIndex = 0; visibleIndex < visibleChunkIndices.size(); ++visibleIndex) {
                    const TileChunkRenderCache& cache = chunkCaches[visibleChunkIndices[visibleIndex]];
                    glBindVertexArray(cache.vertexArrayId);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, cache.instanceCount);
                }
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
            }
            frameMetrics.tileOverlayDrawMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tileOverlayDrawStart).count();

            if (!routeArrowInstances.empty()) {
                glUniform1i(renderModeLocation, 4);
                glDepthMask(GL_FALSE);
                glDisable(GL_DEPTH_TEST);
                glBindVertexArray(routeArrowVertexArrayId);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(routeArrowInstances.size()));
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
            }

            queryWindow.clearText();
            queryWindow.setVisible(!viewState.queryWindowLines.empty());
            if (!viewState.queryWindowLines.empty()) {
                queryWindow.setText("title", viewState.queryWindowLines[0]);
                std::size_t lineIndex = 1;
                for (; lineIndex < viewState.queryWindowLines.size(); ++lineIndex) {
                    queryWindow.setLineText(lineIndex - 1u, viewState.queryWindowLines[lineIndex]);
                }
                queryWindow.updateLayout();
            }

            uiQuadInstances = BuildWindowQuads(queryWindow);
            AppendHudTextPanel(
                uiQuadInstances,
                FormatSimulationDateForTick(snapshot.simulationTick, dateSettings),
                16.0f,
                16.0f,
                236.0f);
            AppendHudTextPanel(
                uiQuadInstances,
                BuildPopulationLabel("Population: ", snapshot.population),
                std::max(16.0f, static_cast<float>(framebufferWidth) - 356.0f),
                16.0f,
                340.0f);
            {
                std::vector<std::string> cityMenuIds;
                cityMenuIds.push_back("date_speed");
                cityMenuIds.push_back("side_tools");
                cityMenuIds.push_back("rci_tools");
                cityMenuIds.push_back("side_overlays");
                cityMenuIds.push_back("rci_desirability_overlays");
                cityMenuIds.push_back("menu_toggle");
                cityMenuIds.push_back("overlay_toggle");
                if (appController_.uiLayout().menuVisible("escape_menu")) {
                    cityMenuIds.push_back("escape_menu");
                }
                if (appController_.uiLayout().menuVisible("exit_confirm_dialog")) {
                    cityMenuIds.push_back("exit_confirm_dialog");
                }
                if (appController_.uiLayout().menuVisible("quit_region_confirm_dialog")) {
                    cityMenuIds.push_back("quit_region_confirm_dialog");
                }
                std::vector<UiQuadInstanceData> menuQuadInstances = RendererBuildUiMenuQuads(appController_.uiLayout(), framebufferWidth, framebufferHeight, appController_.activeUiActions(), cityMenuIds);
                uiQuadInstances.insert(uiQuadInstances.end(), menuQuadInstances.begin(), menuQuadInstances.end());
            }
            const ApplicationWarning* cityWarning = gameSession_.currentApplicationWarning();
            if (cityWarning != 0) {
                AppendApplicationWarningPanel(uiQuadInstances, *cityWarning, framebufferWidth, framebufferHeight);
                std::vector<std::string> warningMenuIds;
                warningMenuIds.push_back("app_warning_dialog");
                const std::vector<std::string> activeActions;
                std::vector<UiQuadInstanceData> warningMenuQuadInstances = RendererBuildUiMenuQuads(
                    appController_.uiLayout(),
                    framebufferWidth,
                    framebufferHeight,
                    activeActions,
                    warningMenuIds);
                uiQuadInstances.insert(uiQuadInstances.end(), warningMenuQuadInstances.begin(), warningMenuQuadInstances.end());
            }

            DrawUiQuadInstances(
                shaderProgram,
                viewProjectionLocation,
                renderModeLocation,
                uiInstanceBufferId,
                uiVertexArrayId,
                framebufferWidth,
                framebufferHeight,
                uiQuadInstances);

            glBindVertexArray(0);
        } else {
            appController_.setHoveredTile(0, 0, false);
        }

        simulationRuntime.releasePublishedSnapshot(snapshot);
        CopyRequestedScreenshotToClipboard(callbacks, framebufferWidth, framebufferHeight);
        glfwSwapBuffers(window);
        glfwPollEvents();
        if (appController_.quitRequested()) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

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

    gameSession_.clearLoadingPresenter();
    glDeleteTextures(1, &lotMaterialTexture);
    gameSession_.clearCityPreviewRenderer();
    DestroyTileChunkCaches(chunkCaches);
    DestroyRoadChunkCaches(roadChunkCaches);
    DestroyRegionPreviewTextureCaches(regionPreviewTextureCaches);
    glDeleteRenderbuffers(1, &cityPreviewDepthRenderbufferId);
    glDeleteTextures(1, &cityPreviewColorTextureId);
    glDeleteFramebuffers(1, &cityPreviewFramebufferId);
    glDeleteBuffers(1, &uiInstanceBufferId);
    glDeleteVertexArrays(1, &uiVertexArrayId);
    glDeleteBuffers(1, &regionPreviewInstanceBufferId);
    glDeleteVertexArrays(1, &regionPreviewVertexArrayId);
    glDeleteBuffers(1, &roadGhostInstanceBufferId);
    glDeleteVertexArrays(1, &roadGhostVertexArrayId);
    glDeleteBuffers(1, &routeArrowInstanceBufferId);
    glDeleteVertexArrays(1, &routeArrowVertexArrayId);
    glDeleteBuffers(1, &areaOverlayInstanceBufferId);
    glDeleteVertexArrays(1, &areaOverlayVertexArrayId);
    glDeleteBuffers(1, &zoningLotOverlayInstanceBufferId);
    glDeleteVertexArrays(1, &zoningLotOverlayVertexArrayId);
    glDeleteBuffers(1, &lotGhostInstanceBufferId);
    glDeleteVertexArrays(1, &lotGhostVertexArrayId);
    if (generatedLotGhostVertexArrayId != 0) {
        glDeleteVertexArrays(1, &generatedLotGhostVertexArrayId);
    }
    glDeleteBuffers(1, &lotInstanceBufferId);
    glDeleteVertexArrays(1, &lotVertexArrayId);
    if (generatedLotVertexArrayId != 0) {
        glDeleteVertexArrays(1, &generatedLotVertexArrayId);
    }
    glDeleteTextures(1, &roadArrowAtlasTextureId);
    glDeleteTextures(1, &roadBaseCleanAtlasTextureId);
    glDeleteTextures(1, &roadBaseAtlasTextureId);
    glDeleteTextures(1, &zoningOverlayTextureId);
    glDeleteTextures(1, &tileOverlayTextureId);
    glDeleteTextures(1, &groundRoadStateTextureId);
    glDeleteTextures(1, &tileLiftTextureId);
    glDeleteTextures(1, &tileStateTextureId);
    glDeleteBuffers(1, &lotVertexBufferId);
    if (generatedLotVertexBufferId != 0) {
        glDeleteBuffers(1, &generatedLotVertexBufferId);
    }
    glDeleteBuffers(1, &tileVertexBufferId);
    ClearApplicationDialogOwner(glfwGetWin32Window(window));
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
