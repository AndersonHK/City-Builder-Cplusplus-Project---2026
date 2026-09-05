#define NOMINMAX

#include "RciTool.h"
#include "Tile.h"
#include "TransportTypes.h"

#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <direct.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
const int kGridSize = 64;
const int kRoadFootprint = 2;

struct Color {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

const Color kEmpty = {255u, 255u, 255u};
const Color kResidential = {0u, 192u, 0u};
const Color kIndustrial = {255u, 224u, 0u};
const Color kStreet = {192u, 192u, 192u};
const Color kRoad = {144u, 144u, 144u};
const Color kAvenue = {96u, 96u, 96u};
const Color kHighway = {48u, 48u, 48u};
const Color kOccupied = {192u, 0u, 192u};

struct Bitmap {
    int width;
    int height;
    std::vector<Color> pixels;

    Bitmap()
        : width(0),
          height(0) {
    }

    Bitmap(int bitmapWidth, int bitmapHeight, Color fill)
        : width(bitmapWidth),
          height(bitmapHeight),
          pixels(static_cast<std::size_t>(bitmapWidth * bitmapHeight), fill) {
    }
};

struct SandboxCase {
    std::string name;
    std::string toolId;
    RciPlanMode mode;
    RciRect drag;
};

struct TestRunner {
    int checks;
    int failures;

    TestRunner()
        : checks(0),
          failures(0) {
    }

    void expect(bool condition, const std::string& message) {
        ++checks;
        if (condition) {
            return;
        }

        ++failures;
        std::cout << "FAIL: " << message << std::endl;
    }

    int finish() const {
        if (failures == 0) {
            std::cout << "RCI smart grid sandbox tests passed: " << checks << " checks." << std::endl;
            return 0;
        }

        std::cout << "RCI smart grid sandbox tests failed: " << failures << " of " << checks << " checks." << std::endl;
        return 1;
    }
};

bool SameColor(Color left, Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b;
}

bool IsEmptyColor(Color color) {
    return SameColor(color, kEmpty);
}

bool IsRciColor(Color color) {
    return SameColor(color, kResidential) || SameColor(color, kIndustrial);
}

bool IsGroundRoadColor(Color color) {
    return SameColor(color, kStreet) || SameColor(color, kRoad) || SameColor(color, kAvenue);
}

bool IsHighwayColor(Color color) {
    return SameColor(color, kHighway);
}

bool IsPaintableColor(Color color) {
    return IsEmptyColor(color) || IsRciColor(color);
}

std::size_t PixelIndex(const Bitmap& bitmap, int x, int y) {
    return static_cast<std::size_t>((y * bitmap.width) + x);
}

bool InBounds(const Bitmap& bitmap, int x, int y) {
    return x >= 0 && y >= 0 && x < bitmap.width && y < bitmap.height;
}

Color GetPixel(const Bitmap& bitmap, int x, int y) {
    return bitmap.pixels[PixelIndex(bitmap, x, y)];
}

void SetPixel(Bitmap& bitmap, int x, int y, Color color) {
    if (!InBounds(bitmap, x, y)) {
        return;
    }

    bitmap.pixels[PixelIndex(bitmap, x, y)] = color;
}

void FillRect(Bitmap& bitmap, const RciRect& rect, Color color) {
    int y = rect.minTileY;
    for (; y <= rect.maxTileY; ++y) {
        int x = rect.minTileX;
        for (; x <= rect.maxTileX; ++x) {
            SetPixel(bitmap, x, y, color);
        }
    }
}

void DrawVerticalRoad(Bitmap& bitmap, int x, int y0, int y1, Color color, int width = kRoadFootprint) {
    FillRect(bitmap, RciRect(x, y0, x + width - 1, y1), color);
}

void DrawHorizontalRoad(Bitmap& bitmap, int y, int x0, int x1, Color color, int width = kRoadFootprint) {
    FillRect(bitmap, RciRect(x0, y, x1, y + width - 1), color);
}

void DrawRoadPlan(Bitmap& bitmap, const RciRoadPlan& roadPlan, Color color) {
    if (roadPlan.startTileX == roadPlan.endTileX) {
        const int minY = std::min(roadPlan.startTileY, roadPlan.endTileY);
        const int maxY = std::max(roadPlan.startTileY, roadPlan.endTileY);
        DrawVerticalRoad(bitmap, roadPlan.startTileX, minY, maxY, color);
    } else if (roadPlan.startTileY == roadPlan.endTileY) {
        const int minX = std::min(roadPlan.startTileX, roadPlan.endTileX);
        const int maxX = std::max(roadPlan.startTileX, roadPlan.endTileX);
        DrawHorizontalRoad(bitmap, roadPlan.startTileY, minX, maxX, color);
    }
}

std::uint16_t ReadU16(std::ifstream& file) {
    unsigned char bytes[2] = {0u, 0u};
    file.read(reinterpret_cast<char*>(bytes), 2);
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}

std::uint32_t ReadU32(std::ifstream& file) {
    unsigned char bytes[4] = {0u, 0u, 0u, 0u};
    file.read(reinterpret_cast<char*>(bytes), 4);
    return static_cast<std::uint32_t>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
}

std::int32_t ReadS32(std::ifstream& file) {
    return static_cast<std::int32_t>(ReadU32(file));
}

void WriteU16(std::ofstream& file, std::uint16_t value) {
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu)};
    file.write(reinterpret_cast<const char*>(bytes), 2);
}

void WriteU32(std::ofstream& file, std::uint32_t value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu),
        static_cast<unsigned char>((value >> 16) & 0xffu),
        static_cast<unsigned char>((value >> 24) & 0xffu)};
    file.write(reinterpret_cast<const char*>(bytes), 4);
}

void WriteS32(std::ofstream& file, std::int32_t value) {
    WriteU32(file, static_cast<std::uint32_t>(value));
}

bool LoadBmp(const std::string& path, Bitmap& bitmap, std::string& error) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        error = "could not open";
        return false;
    }

    const std::uint16_t signature = ReadU16(file);
    if (signature != 0x4d42u) {
        error = "not a BMP";
        return false;
    }

    ReadU32(file);
    ReadU16(file);
    ReadU16(file);
    const std::uint32_t pixelOffset = ReadU32(file);
    const std::uint32_t headerSize = ReadU32(file);
    if (headerSize < 40u) {
        error = "unsupported BMP header";
        return false;
    }

    const std::int32_t width = ReadS32(file);
    const std::int32_t signedHeight = ReadS32(file);
    const std::uint16_t planes = ReadU16(file);
    const std::uint16_t bitsPerPixel = ReadU16(file);
    const std::uint32_t compression = ReadU32(file);
    ReadU32(file);
    ReadS32(file);
    ReadS32(file);
    const std::uint32_t colorsUsed = ReadU32(file);
    ReadU32(file);

    if (planes != 1u ||
        width <= 0 ||
        signedHeight == 0 ||
        (bitsPerPixel != 1u && bitsPerPixel != 4u && bitsPerPixel != 8u && bitsPerPixel != 24u && bitsPerPixel != 32u) ||
        compression != 0u) {
        error = "expected uncompressed 1-bit, 4-bit, 8-bit, 24-bit, or 32-bit BMP";
        return false;
    }

    const int height = signedHeight < 0 ? -signedHeight : signedHeight;
    std::vector<Color> palette;
    if (bitsPerPixel <= 8u) {
        const std::uint32_t defaultPaletteSize = 1u << bitsPerPixel;
        const std::uint32_t paletteEntries = colorsUsed == 0u ? defaultPaletteSize : colorsUsed;
        const std::uint32_t paletteStart = 14u + headerSize;
        if (pixelOffset < paletteStart || ((pixelOffset - paletteStart) / 4u) < paletteEntries) {
            error = "indexed BMP palette is missing or truncated";
            return false;
        }

        file.seekg(static_cast<std::streamoff>(paletteStart), std::ios::beg);
        std::uint32_t paletteIndex = 0u;
        for (; paletteIndex < paletteEntries; ++paletteIndex) {
            unsigned char entry[4] = {0u, 0u, 0u, 0u};
            file.read(reinterpret_cast<char*>(entry), 4);
            if (!file) {
                error = "truncated BMP palette";
                return false;
            }
            palette.push_back(Color{entry[2], entry[1], entry[0]});
        }
    }

    const int rowStride = static_cast<int>((((static_cast<std::uint32_t>(width) * bitsPerPixel) + 31u) / 32u) * 4u);
    bitmap = Bitmap(width, height, kEmpty);
    std::vector<unsigned char> row(static_cast<std::size_t>(rowStride), 0u);

    file.seekg(static_cast<std::streamoff>(pixelOffset), std::ios::beg);
    int fileRow = 0;
    for (; fileRow < height; ++fileRow) {
        file.read(reinterpret_cast<char*>(&row[0]), rowStride);
        if (!file) {
            error = "truncated BMP data";
            return false;
        }

        const int y = signedHeight < 0 ? fileRow : height - 1 - fileRow;
        int x = 0;
        for (; x < width; ++x) {
            Color color = kEmpty;
            if (bitsPerPixel == 32u) {
                const std::size_t offset = static_cast<std::size_t>(x * 4);
                color = Color{row[offset + 2], row[offset + 1], row[offset]};
            } else if (bitsPerPixel == 24u) {
                const std::size_t offset = static_cast<std::size_t>(x * 3);
                color = Color{row[offset + 2], row[offset + 1], row[offset]};
            } else if (bitsPerPixel == 8u) {
                const std::uint8_t paletteIndex = row[static_cast<std::size_t>(x)];
                if (paletteIndex >= palette.size()) {
                    error = "indexed BMP pixel references a missing palette entry";
                    return false;
                }
                color = palette[paletteIndex];
            } else if (bitsPerPixel == 4u) {
                const std::uint8_t packed = row[static_cast<std::size_t>(x / 2)];
                const std::uint8_t paletteIndex = (x % 2) == 0 ? static_cast<std::uint8_t>((packed >> 4) & 0x0fu) : static_cast<std::uint8_t>(packed & 0x0fu);
                if (paletteIndex >= palette.size()) {
                    error = "indexed BMP pixel references a missing palette entry";
                    return false;
                }
                color = palette[paletteIndex];
            } else if (bitsPerPixel == 1u) {
                const std::uint8_t packed = row[static_cast<std::size_t>(x / 8)];
                const std::uint8_t paletteIndex = static_cast<std::uint8_t>((packed >> (7 - (x % 8))) & 0x01u);
                if (paletteIndex >= palette.size()) {
                    error = "indexed BMP pixel references a missing palette entry";
                    return false;
                }
                color = palette[paletteIndex];
            }
            SetPixel(bitmap, x, y, color);
        }
    }

    return true;
}

bool WriteBmp(const std::string& path, const Bitmap& bitmap, std::string& error) {
    std::ofstream file(path.c_str(), std::ios::binary);
    if (!file) {
        error = "could not create";
        return false;
    }

    const int bytesPerPixel = 3;
    const int rowStride = ((bitmap.width * bytesPerPixel) + 3) & ~3;
    const std::uint32_t pixelDataSize = static_cast<std::uint32_t>(rowStride * bitmap.height);
    const std::uint32_t pixelOffset = 14u + 40u;
    const std::uint32_t fileSize = pixelOffset + pixelDataSize;

    WriteU16(file, 0x4d42u);
    WriteU32(file, fileSize);
    WriteU16(file, 0u);
    WriteU16(file, 0u);
    WriteU32(file, pixelOffset);
    WriteU32(file, 40u);
    WriteS32(file, bitmap.width);
    WriteS32(file, bitmap.height);
    WriteU16(file, 1u);
    WriteU16(file, 24u);
    WriteU32(file, 0u);
    WriteU32(file, pixelDataSize);
    WriteS32(file, 2835);
    WriteS32(file, 2835);
    WriteU32(file, 0u);
    WriteU32(file, 0u);

    std::vector<unsigned char> row(static_cast<std::size_t>(rowStride), 0u);
    int fileRow = 0;
    for (; fileRow < bitmap.height; ++fileRow) {
        std::fill(row.begin(), row.end(), 0u);
        const int y = bitmap.height - 1 - fileRow;
        int x = 0;
        for (; x < bitmap.width; ++x) {
            const Color color = GetPixel(bitmap, x, y);
            const std::size_t offset = static_cast<std::size_t>(x * bytesPerPixel);
            row[offset] = color.b;
            row[offset + 1] = color.g;
            row[offset + 2] = color.r;
        }
        file.write(reinterpret_cast<const char*>(&row[0]), rowStride);
    }

    if (!file) {
        error = "failed while writing";
        return false;
    }

    return true;
}

std::string JoinPath(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }

    const char last = left[left.size() - 1u];
    if (last == '\\' || last == '/') {
        return left + right;
    }

    return left + "\\" + right;
}

std::string ParentPath(const std::string& path) {
    const std::string::size_type slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return std::string();
    }

    return path.substr(0u, slash);
}

bool FileExists(const std::string& path) {
    std::ifstream file(path.c_str(), std::ios::binary);
    return file.good();
}

bool DirectoryExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectory(const std::string& path) {
    if (DirectoryExists(path)) {
        return true;
    }

    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
}

std::string ExeDirectory() {
    char buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameA(0, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::string();
    }

    return ParentPath(std::string(buffer, buffer + length));
}

std::string WorkingDirectory() {
    char buffer[MAX_PATH] = {0};
    if (_getcwd(buffer, MAX_PATH) == 0) {
        return std::string();
    }

    return std::string(buffer);
}

std::string FindSandboxDirectory() {
    const std::string cwd = WorkingDirectory();
    const std::string exeDir = ExeDirectory();
    std::vector<std::string> candidates;
    candidates.push_back(JoinPath(cwd, "City Builder\\Data\\RCI\\SandboxGridTests"));
    candidates.push_back(JoinPath(cwd, "Data\\RCI\\SandboxGridTests"));
    candidates.push_back(JoinPath(exeDir, "Data\\RCI\\SandboxGridTests"));
    candidates.push_back(JoinPath(exeDir, "..\\..\\Data\\RCI\\SandboxGridTests"));
    candidates.push_back(JoinPath(exeDir, "..\\..\\..\\City Builder\\Data\\RCI\\SandboxGridTests"));

    std::size_t index = 0u;
    for (; index < candidates.size(); ++index) {
        if (DirectoryExists(candidates[index])) {
            return candidates[index];
        }
    }

    const std::string fallback = JoinPath(cwd, "City Builder\\Data\\RCI\\SandboxGridTests");
    EnsureDirectory(JoinPath(cwd, "City Builder\\Data\\RCI"));
    EnsureDirectory(fallback);
    return fallback;
}

std::string FindActualOutputDirectory() {
    const std::string exeDir = ExeDirectory();
    const std::string outputDir = JoinPath(exeDir.empty() ? WorkingDirectory() : exeDir, "RciSmartGridSandboxActuals");
    EnsureDirectory(outputDir);
    return outputDir;
}

std::vector<SandboxCase> SandboxCases() {
    std::vector<SandboxCase> cases;
    const RciRect fullDrag(8, 8, 55, 55);
    cases.push_back(SandboxCase{"normal_res_empty_64", "residential_low", RciPlanMode::LotsAndRoads, fullDrag});
    cases.push_back(SandboxCase{"normal_res_edge_street_64", "residential_low", RciPlanMode::LotsAndRoads, fullDrag});
    cases.push_back(SandboxCase{"normal_res_existing_grid_64", "residential_low", RciPlanMode::LotsAndRoads, fullDrag});
    cases.push_back(SandboxCase{"normal_ind_existing_avenue_64", "industrial", RciPlanMode::LotsAndRoads, fullDrag});
    cases.push_back(SandboxCase{"normal_res_existing_highway_64", "residential_low", RciPlanMode::LotsAndRoads, fullDrag});
    cases.push_back(SandboxCase{"shift_res_existing_roads_64", "residential_low", RciPlanMode::Lots, fullDrag});
    cases.push_back(SandboxCase{"normal_res_existing_rci_and_blocker_64", "residential_low", RciPlanMode::LotsAndRoads, fullDrag});
    cases.push_back(SandboxCase{"normal_res_offset_grid_snap_64", "residential_low", RciPlanMode::LotsAndRoads, fullDrag});
    return cases;
}

Bitmap MakeTemplateInput(const SandboxCase& testCase) {
    Bitmap bitmap(kGridSize, kGridSize, kEmpty);

    if (testCase.name == "normal_res_edge_street_64") {
        DrawHorizontalRoad(bitmap, 6, 4, 59, kStreet);
    } else if (testCase.name == "normal_res_existing_grid_64") {
        DrawHorizontalRoad(bitmap, 15, 4, 59, kStreet);
        DrawHorizontalRoad(bitmap, 31, 4, 59, kStreet);
        DrawHorizontalRoad(bitmap, 47, 4, 59, kStreet);
        DrawVerticalRoad(bitmap, 15, 4, 59, kStreet);
        DrawVerticalRoad(bitmap, 31, 4, 59, kStreet);
        DrawVerticalRoad(bitmap, 47, 4, 59, kStreet);
    } else if (testCase.name == "normal_ind_existing_avenue_64") {
        DrawVerticalRoad(bitmap, 28, 0, 63, kAvenue, 4);
        DrawHorizontalRoad(bitmap, 40, 8, 55, kRoad);
        FillRect(bitmap, RciRect(12, 12, 19, 19), kResidential);
    } else if (testCase.name == "normal_res_existing_highway_64") {
        DrawHorizontalRoad(bitmap, 29, 0, 63, kHighway, 4);
        DrawVerticalRoad(bitmap, 46, 12, 63, kHighway, 4);
        DrawHorizontalRoad(bitmap, 12, 8, 42, kStreet);
    } else if (testCase.name == "shift_res_existing_roads_64") {
        DrawHorizontalRoad(bitmap, 24, 8, 55, kStreet);
        DrawVerticalRoad(bitmap, 38, 8, 55, kRoad);
    } else if (testCase.name == "normal_res_existing_rci_and_blocker_64") {
        DrawHorizontalRoad(bitmap, 6, 4, 59, kStreet);
        FillRect(bitmap, RciRect(12, 12, 22, 20), kIndustrial);
        FillRect(bitmap, RciRect(36, 34, 47, 44), kResidential);
        FillRect(bitmap, RciRect(26, 26, 31, 31), kOccupied);
    } else if (testCase.name == "normal_res_offset_grid_snap_64") {
        DrawVerticalRoad(bitmap, 29, 0, 7, kStreet);
        DrawVerticalRoad(bitmap, 29, 56, 63, kStreet);
        DrawHorizontalRoad(bitmap, 22, 0, 7, kStreet);
        DrawHorizontalRoad(bitmap, 22, 56, 63, kStreet);
        DrawHorizontalRoad(bitmap, 43, 4, 59, kRoad);
    }

    return bitmap;
}

int RoadRunLength(const Bitmap& bitmap, int x, int y, int dx, int dy) {
    int length = 1;
    int cursorX = x + dx;
    int cursorY = y + dy;
    while (InBounds(bitmap, cursorX, cursorY) && IsGroundRoadColor(GetPixel(bitmap, cursorX, cursorY))) {
        ++length;
        cursorX += dx;
        cursorY += dy;
    }

    cursorX = x - dx;
    cursorY = y - dy;
    while (InBounds(bitmap, cursorX, cursorY) && IsGroundRoadColor(GetPixel(bitmap, cursorX, cursorY))) {
        ++length;
        cursorX -= dx;
        cursorY -= dy;
    }

    return length;
}

std::uint8_t InferGroundRoadAxisMask(const Bitmap& bitmap, int x, int y) {
    const int horizontal = RoadRunLength(bitmap, x, y, 1, 0);
    const int vertical = RoadRunLength(bitmap, x, y, 0, 1);
    std::uint8_t axisMask = 0u;
    if (horizontal >= vertical && horizontal > 1) {
        axisMask |= AxisMaskFor(RoadAxis::Horizontal);
    }
    if (vertical >= horizontal && vertical > 1) {
        axisMask |= AxisMaskFor(RoadAxis::Vertical);
    }
    if (axisMask == 0u) {
        axisMask = AxisMaskFor(RoadAxis::Horizontal) | AxisMaskFor(RoadAxis::Vertical);
    }
    return axisMask;
}

RciPlanningContext BuildContextFromBitmap(const SandboxCase& testCase, const Bitmap& bitmap) {
    RciPlanningContext context;
    context.mapWidth = bitmap.width;
    context.mapHeight = bitmap.height;
    context.bounds = testCase.drag;
    context.mode = testCase.mode;
    const std::size_t totalTiles = static_cast<std::size_t>(bitmap.width * bitmap.height);
    context.paintableTiles.assign(totalTiles, 0u);
    context.groundRoadTiles.assign(totalTiles, 0u);
    context.groundRoadAxisMasks.assign(totalTiles, 0u);

    int y = 0;
    for (; y < bitmap.height; ++y) {
        int x = 0;
        for (; x < bitmap.width; ++x) {
            const Color color = GetPixel(bitmap, x, y);
            const std::size_t index = PixelIndex(bitmap, x, y);
            if (IsGroundRoadColor(color)) {
                context.groundRoadTiles[index] = 1u;
                context.groundRoadAxisMasks[index] = InferGroundRoadAxisMask(bitmap, x, y);
            }

            if (x >= testCase.drag.minTileX &&
                x <= testCase.drag.maxTileX &&
                y >= testCase.drag.minTileY &&
                y <= testCase.drag.maxTileY &&
                IsPaintableColor(color) &&
                !IsGroundRoadColor(color) &&
                !IsHighwayColor(color) &&
                !SameColor(color, kOccupied)) {
                context.paintableTiles[index] = 1u;
            }
        }
    }

    return context;
}

Color ZoneColorForTool(const RciTool& tool) {
    if (tool.zoningType() == TileZoningIndustrial) {
        return kIndustrial;
    }

    return kResidential;
}

void PaintPlanRect(Bitmap& output, const RciPlanningContext& context, const RciRect& rect, Color color) {
    int y = rect.minTileY;
    for (; y <= rect.maxTileY; ++y) {
        int x = rect.minTileX;
        for (; x <= rect.maxTileX; ++x) {
            if (x < 0 || y < 0 || x >= context.mapWidth || y >= context.mapHeight) {
                continue;
            }

            const std::size_t index = static_cast<std::size_t>((y * context.mapWidth) + x);
            if (context.paintableTiles[index] != 0u) {
                SetPixel(output, x, y, color);
            }
        }
    }
}

bool BuildActualImage(const SandboxCase& testCase, const RciToolCatalog& catalog, const Bitmap& input, Bitmap& actual, std::string& error) {
    if (input.width != kGridSize || input.height != kGridSize) {
        error = "input must be 64x64";
        return false;
    }

    const RciTool* tool = catalog.findTool(testCase.toolId);
    if (tool == 0) {
        error = "missing RCI tool: " + testCase.toolId;
        return false;
    }

    const RciPlanningContext context = BuildContextFromBitmap(testCase, input);
    RciPlan plan;
    if (!tool->buildPlan(context, plan)) {
        error = "planner returned no plan";
        return false;
    }

    actual = input;
    const Color zoneColor = ZoneColorForTool(*tool);
    std::size_t rectIndex = 0u;
    for (; rectIndex < plan.paintRects.size(); ++rectIndex) {
        PaintPlanRect(actual, context, plan.paintRects[rectIndex], zoneColor);
    }
    for (rectIndex = 0u; rectIndex < plan.zoneRects.size(); ++rectIndex) {
        PaintPlanRect(actual, context, plan.zoneRects[rectIndex], zoneColor);
    }

    std::size_t roadIndex = 0u;
    for (; roadIndex < plan.roadPlans.size(); ++roadIndex) {
        DrawRoadPlan(actual, plan.roadPlans[roadIndex], kStreet);
    }

    return true;
}

int GroundRoadCoverageHorizontal(const Bitmap& bitmap, const RciRect& rect, int roadY) {
    int coverage = 0;
    int x = rect.minTileX;
    for (; x <= rect.maxTileX; ++x) {
        if (InBounds(bitmap, x, roadY) && IsGroundRoadColor(GetPixel(bitmap, x, roadY))) {
            ++coverage;
        }
    }
    return coverage;
}

int GroundRoadCoverageVertical(const Bitmap& bitmap, const RciRect& rect, int roadX) {
    int coverage = 0;
    int y = rect.minTileY;
    for (; y <= rect.maxTileY; ++y) {
        if (InBounds(bitmap, roadX, y) && IsGroundRoadColor(GetPixel(bitmap, roadX, y))) {
            ++coverage;
        }
    }
    return coverage;
}

bool ValidateRoadAccessDepths(const SandboxCase& testCase, const RciTool& tool, const Bitmap& actual, std::string& error) {
    if (testCase.mode != RciPlanMode::LotsAndRoads) {
        return true;
    }

    const Color zoneColor = ZoneColorForTool(tool);
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(actual.width * actual.height), 0u);
    std::vector<int> queue;
    int startY = testCase.drag.minTileY;
    for (; startY <= testCase.drag.maxTileY; ++startY) {
        int startX = testCase.drag.minTileX;
        for (; startX <= testCase.drag.maxTileX; ++startX) {
            const std::size_t startIndex = PixelIndex(actual, startX, startY);
            if (visited[startIndex] != 0u || !SameColor(GetPixel(actual, startX, startY), zoneColor)) {
                continue;
            }

            RciRect rect(startX, startY, startX, startY);
            queue.clear();
            queue.push_back(static_cast<int>(startIndex));
            visited[startIndex] = 1u;
            std::size_t readIndex = 0u;
            for (; readIndex < queue.size(); ++readIndex) {
                const int currentIndex = queue[readIndex];
                const int y = currentIndex / actual.width;
                const int x = currentIndex - (y * actual.width);
                rect.minTileX = std::min(rect.minTileX, x);
                rect.maxTileX = std::max(rect.maxTileX, x);
                rect.minTileY = std::min(rect.minTileY, y);
                rect.maxTileY = std::max(rect.maxTileY, y);

                const int neighbors[4][2] = {
                    {1, 0},
                    {-1, 0},
                    {0, 1},
                    {0, -1}};
                int neighborIndex = 0;
                for (; neighborIndex < 4; ++neighborIndex) {
                    const int neighborX = x + neighbors[neighborIndex][0];
                    const int neighborY = y + neighbors[neighborIndex][1];
                    if (neighborX < testCase.drag.minTileX ||
                        neighborX > testCase.drag.maxTileX ||
                        neighborY < testCase.drag.minTileY ||
                        neighborY > testCase.drag.maxTileY ||
                        !SameColor(GetPixel(actual, neighborX, neighborY), zoneColor)) {
                        continue;
                    }

                    const std::size_t neighborPixelIndex = PixelIndex(actual, neighborX, neighborY);
                    if (visited[neighborPixelIndex] == 0u) {
                        visited[neighborPixelIndex] = 1u;
                        queue.push_back(static_cast<int>(neighborPixelIndex));
                    }
                }
            }

            const int minimumHorizontalCoverage = std::min(rect.width(), std::max(kRoadFootprint, tool.minWidth()));
            const int minimumVerticalCoverage = std::min(rect.height(), std::max(kRoadFootprint, tool.minWidth()));
            const bool northRoad = GroundRoadCoverageHorizontal(actual, rect, rect.minTileY - 1) >= minimumHorizontalCoverage;
            const bool southRoad = GroundRoadCoverageHorizontal(actual, rect, rect.maxTileY + 1) >= minimumHorizontalCoverage;
            const bool westRoad = GroundRoadCoverageVertical(actual, rect, rect.minTileX - 1) >= minimumVerticalCoverage;
            const bool eastRoad = GroundRoadCoverageVertical(actual, rect, rect.maxTileX + 1) >= minimumVerticalCoverage;
            const bool horizontalDepthValid = (northRoad || southRoad) &&
                rect.height() <= tool.maxDepth() * ((northRoad && southRoad) ? 2 : 1);
            const bool verticalDepthValid = (westRoad || eastRoad) &&
                rect.width() <= tool.maxDepth() * ((westRoad && eastRoad) ? 2 : 1);
            if (!horizontalDepthValid && !verticalDepthValid) {
                std::ostringstream stream;
                stream << testCase.name << ": component (" << rect.minTileX << "," << rect.minTileY << ")-(" <<
                    rect.maxTileX << "," << rect.maxTileY << ") exceeds road-access depth limits";
                error = stream.str();
                return false;
            }
        }
    }

    return true;
}

bool EnsureTemplateInput(const std::string& sandboxDir, const SandboxCase& testCase) {
    const std::string path = JoinPath(sandboxDir, testCase.name + "_input.bmp");
    if (FileExists(path)) {
        return true;
    }

    std::string error;
    const Bitmap bitmap = MakeTemplateInput(testCase);
    if (!WriteBmp(path, bitmap, error)) {
        std::cout << "FAIL: could not create " << path << ": " << error << std::endl;
        return false;
    }

    std::cout << "Created " << path << std::endl;
    return true;
}

bool LoadCaseBitmap(const std::string& path, Bitmap& bitmap, std::string& error) {
    if (!LoadBmp(path, bitmap, error)) {
        error = path + ": " + error;
        return false;
    }

    if (bitmap.width != kGridSize || bitmap.height != kGridSize) {
        std::ostringstream stream;
        stream << path << ": expected 64x64 BMP, got " << bitmap.width << "x" << bitmap.height;
        error = stream.str();
        return false;
    }

    return true;
}

int CountMismatchedPixels(const Bitmap& actual, const Bitmap& expected) {
    if (actual.width != expected.width || actual.height != expected.height) {
        return actual.width * actual.height;
    }

    int mismatches = 0;
    std::size_t index = 0u;
    for (; index < actual.pixels.size(); ++index) {
        if (!SameColor(actual.pixels[index], expected.pixels[index])) {
            ++mismatches;
        }
    }

    return mismatches;
}

bool BlessExpectedImages(const std::string& sandboxDir, const RciToolCatalog& catalog, const std::vector<SandboxCase>& cases) {
    bool success = true;
    std::size_t caseIndex = 0u;
    for (; caseIndex < cases.size(); ++caseIndex) {
        const SandboxCase& testCase = cases[caseIndex];
        if (!EnsureTemplateInput(sandboxDir, testCase)) {
            success = false;
            continue;
        }

        const std::string inputPath = JoinPath(sandboxDir, testCase.name + "_input.bmp");
        const std::string expectedPath = JoinPath(sandboxDir, testCase.name + "_expected.bmp");
        Bitmap input;
        std::string error;
        if (!LoadCaseBitmap(inputPath, input, error)) {
            std::cout << "FAIL: " << error << std::endl;
            success = false;
            continue;
        }

        Bitmap actual;
        if (!BuildActualImage(testCase, catalog, input, actual, error)) {
            std::cout << "FAIL: " << testCase.name << ": " << error << std::endl;
            success = false;
            continue;
        }

        if (!WriteBmp(expectedPath, actual, error)) {
            std::cout << "FAIL: could not write " << expectedPath << ": " << error << std::endl;
            success = false;
            continue;
        }

        std::cout << "Blessed " << expectedPath << std::endl;
    }

    return success;
}

void RunComparisonTests(const std::string& sandboxDir, const std::string& actualDir, const RciToolCatalog& catalog, const std::vector<SandboxCase>& cases, TestRunner& runner) {
    std::size_t caseIndex = 0u;
    for (; caseIndex < cases.size(); ++caseIndex) {
        const SandboxCase& testCase = cases[caseIndex];
        const std::string inputPath = JoinPath(sandboxDir, testCase.name + "_input.bmp");
        const std::string expectedPath = JoinPath(sandboxDir, testCase.name + "_expected.bmp");
        const std::string actualPath = JoinPath(actualDir, testCase.name + "_actual.bmp");

        Bitmap input;
        Bitmap expected;
        Bitmap actual;
        std::string error;
        if (!LoadCaseBitmap(inputPath, input, error)) {
            runner.expect(false, error);
            continue;
        }
        if (!LoadCaseBitmap(expectedPath, expected, error)) {
            runner.expect(false, error + " (run with --bless to create it)");
            continue;
        }
        if (!BuildActualImage(testCase, catalog, input, actual, error)) {
            runner.expect(false, testCase.name + ": " + error);
            continue;
        }

        const RciTool* tool = catalog.findTool(testCase.toolId);
        if (tool == 0 || !ValidateRoadAccessDepths(testCase, *tool, actual, error)) {
            runner.expect(false, error);
            continue;
        }
        runner.expect(true, testCase.name + " road-access depth");

        const int mismatches = CountMismatchedPixels(actual, expected);
        if (mismatches != 0) {
            std::string writeError;
            WriteBmp(actualPath, actual, writeError);
            std::ostringstream stream;
            stream << testCase.name << ": " << mismatches << " mismatched pixels; actual written to " << actualPath;
            runner.expect(false, stream.str());
        } else {
            runner.expect(true, testCase.name);
        }
    }
}
}

int main(int argc, char** argv) {
    bool bless = false;
    int argIndex = 1;
    for (; argIndex < argc; ++argIndex) {
        if (std::strcmp(argv[argIndex], "--bless") == 0) {
            bless = true;
        }
    }

    const std::string sandboxDir = FindSandboxDirectory();
    RciToolCatalog catalog;
    const std::string rciToolsPath = JoinPath(ParentPath(sandboxDir), "rci_tools.xml");
    catalog.loadFromXmlFile(rciToolsPath);

    const std::vector<SandboxCase> cases = SandboxCases();
    if (bless) {
        return BlessExpectedImages(sandboxDir, catalog, cases) ? 0 : 1;
    }

    TestRunner runner;
    RunComparisonTests(sandboxDir, FindActualOutputDirectory(), catalog, cases, runner);
    return runner.finish();
}
