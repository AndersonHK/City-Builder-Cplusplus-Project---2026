#include "RendererAlgorithms.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
class TestRunner {
public:
    TestRunner()
        : failures_(0),
          checks_(0) {
    }

    void expect(bool condition, const std::string& message) {
        ++checks_;
        if (condition) {
            return;
        }

        ++failures_;
        std::cout << "FAIL: " << message << std::endl;
    }

    int finish() const {
        if (failures_ == 0) {
            std::cout << "Renderer tests passed: " << checks_ << " checks." << std::endl;
            return 0;
        }

        std::cout << "Renderer tests failed: " << failures_ << " of " << checks_ << " checks." << std::endl;
        return 1;
    }

private:
    int failures_;
    int checks_;
};

bool AlmostEqual(float left, float right) {
    return std::fabs(left - right) < 0.001f;
}

void TestTileStatePacking(TestRunner& runner) {
    runner.expect(RendererPackTileStateScalar(0) == 0, "zero scalar packs to zero");
    runner.expect(RendererPackTileStateScalar(640000) == 32767, "positive full-scale scalar packs to max snorm");
    runner.expect(RendererPackTileStateScalar(1280000) == 32767, "positive over-scale scalar clamps to max snorm");
    runner.expect(RendererPackTileStateScalar(-640000) == -32768, "negative full-scale scalar packs to min snorm");
    runner.expect(RendererPackTileStateScalar(-1280000) == -32768, "negative over-scale scalar clamps to min snorm");
    runner.expect(RendererPackTileStateScalar(320000) == 16384, "half-scale scalar rounds consistently");
}

void TestTileStateChunkPacking(TestRunner& runner) {
    std::vector<Tile> tiles(16);
    tiles[5].airPollution = 640000;
    tiles[5].landValue = -640000;
    tiles[6].airPollution = 320000;
    tiles[6].landValue = 0;
    tiles[9].airPollution = -320000;
    tiles[9].landValue = 640000;
    tiles[10].airPollution = 0;
    tiles[10].landValue = 320000;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<std::int16_t> pixels;
    RendererFillTileStateChunkPixels(tiles, 4, chunk, pixels);

    runner.expect(pixels.size() == 8u, "tile-state chunk writes two channels per tile");
    runner.expect(pixels[0] == 32767 && pixels[1] == -32768, "first chunk tile packs pollution and land value");
    runner.expect(pixels[2] == 16384 && pixels[3] == 0, "second chunk tile preserves row-major order");
    runner.expect(pixels[4] == -16384 && pixels[5] == 32767, "third chunk tile packs negative half-scale values");
    runner.expect(pixels[6] == 0 && pixels[7] == 16384, "fourth chunk tile completes row-major packing");
}

void TestTileLiftChunkPacking(TestRunner& runner) {
    std::vector<int> occupancy(16, -1);
    occupancy[5] = 10;
    occupancy[10] = 12;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<std::uint8_t> pixels;
    RendererFillTileLiftChunkPixels(occupancy, 4, chunk, pixels);

    runner.expect(pixels.size() == 4u, "lift chunk writes one byte per tile");
    runner.expect(pixels[0] == 255u, "occupied tile lifts to full mask");
    runner.expect(pixels[1] == 0u, "empty tile keeps zero lift mask");
    runner.expect(pixels[2] == 0u, "empty row-major tile keeps zero lift mask");
    runner.expect(pixels[3] == 255u, "occupied row-major tile lifts to full mask");
}

void TestUtf8Decoder(TestRunner& runner) {
    std::string text;
    text.push_back('A');
    text.push_back(static_cast<char>(0xC3));
    text.push_back(static_cast<char>(0xA9));
    text.push_back(static_cast<char>(0xFF));

    std::size_t byteIndex = 0;
    std::uint32_t codepoint = 0u;
    runner.expect(RendererNextUtf8Codepoint(text, byteIndex, codepoint) && codepoint == 'A', "ASCII codepoint decodes");
    runner.expect(RendererNextUtf8Codepoint(text, byteIndex, codepoint) && codepoint == 0xE9u, "two-byte UTF-8 codepoint decodes");
    runner.expect(RendererNextUtf8Codepoint(text, byteIndex, codepoint) && codepoint == '?', "invalid UTF-8 byte falls back to question mark");
    runner.expect(!RendererNextUtf8Codepoint(text, byteIndex, codepoint), "decoder stops at end of string");
}

void TestWindowQuads(TestRunner& runner) {
    InGameWindow window;
    window.setVisible(false);
    window.setText("title", "House");
    window.updateLayout();
    runner.expect(RendererBuildWindowQuads(window).empty(), "hidden window produces no UI quads");

    window.setVisible(true);
    std::vector<UiQuadInstanceData> quads = RendererBuildWindowQuads(window);
    runner.expect(quads.size() > 5u, "visible window produces frame and text quads");
    runner.expect(AlmostEqual(quads[0].x, 24.0f) && AlmostEqual(quads[0].y, 24.0f), "window background starts at fallback origin");
    runner.expect(AlmostEqual(quads[0].width, 440.0f), "window background uses fallback width");
    runner.expect(quads[0].height > 30.0f && quads[0].height < 80.0f, "hugging window height follows visible text");
    runner.expect(AlmostEqual(quads[0].colorA, 0.90f), "window background alpha is stable");

    bool allTextQuadsInsideWindow = true;
    std::size_t quadIndex = 5u;
    for (; quadIndex < quads.size(); ++quadIndex) {
        const UiQuadInstanceData& quad = quads[quadIndex];
        if (quad.x < quads[0].x || quad.y < quads[0].y ||
            quad.x + quad.width > quads[0].x + quads[0].width ||
            quad.y + quad.height > quads[0].y + quads[0].height) {
            allTextQuadsInsideWindow = false;
            break;
        }
    }
    runner.expect(allTextQuadsInsideWindow, "text quads stay inside the query window frame");
}
}

int main() {
    TestRunner runner;
    TestTileStatePacking(runner);
    TestTileStateChunkPacking(runner);
    TestTileLiftChunkPacking(runner);
    TestUtf8Decoder(runner);
    TestWindowQuads(runner);
    return runner.finish();
}
