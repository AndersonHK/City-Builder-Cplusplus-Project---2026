#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct Vertex {
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;

    Vertex(float xValue, float yValue, float zValue, float red, float green, float blue)
        : x(xValue),
          y(yValue),
          z(zValue),
          r(red),
          g(green),
          b(blue) {
    }
};

class MeshBuilder {
public:
    void addTriangle(const Vertex& a, const Vertex& b, const Vertex& c) {
        vertices_.push_back(a);
        vertices_.push_back(b);
        vertices_.push_back(c);
    }

    void addQuad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) {
        addTriangle(a, b, c);
        addTriangle(c, d, a);
    }

    void addBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ, float red, float green, float blue) {
        const Vertex nwb(minX, minY, minZ, red, green, blue);
        const Vertex neb(maxX, minY, minZ, red, green, blue);
        const Vertex seb(maxX, minY, maxZ, red, green, blue);
        const Vertex swb(minX, minY, maxZ, red, green, blue);
        const Vertex nwt(minX, maxY, minZ, red, green, blue);
        const Vertex net(maxX, maxY, minZ, red, green, blue);
        const Vertex set(maxX, maxY, maxZ, red, green, blue);
        const Vertex swt(minX, maxY, maxZ, red, green, blue);

        addQuad(nwb, neb, net, nwt);
        addQuad(swb, swt, set, seb);
        addQuad(nwb, nwt, swt, swb);
        addQuad(neb, seb, set, net);
        addQuad(nwt, net, set, swt);
        addQuad(nwb, swb, seb, neb);
    }

    void addPyramid(float minX, float minY, float minZ, float maxX, float maxY, float maxZ, float red, float green, float blue) {
        const Vertex nw(minX, minY, minZ, red, green, blue);
        const Vertex ne(maxX, minY, minZ, red, green, blue);
        const Vertex se(maxX, minY, maxZ, red, green, blue);
        const Vertex sw(minX, minY, maxZ, red, green, blue);
        const Vertex apex((minX + maxX) * 0.5f, maxY, (minZ + maxZ) * 0.5f, red, green, blue);
        addTriangle(nw, ne, apex);
        addTriangle(ne, se, apex);
        addTriangle(se, sw, apex);
        addTriangle(sw, nw, apex);
        addQuad(nw, sw, se, ne);
    }

    void addGabledBlock(float bodyHeight, float roofRed, float roofGreen, float roofBlue) {
        addGabledBlock(0.0f, 0.0f, 1.0f, 1.0f, bodyHeight, 1.0f, roofRed, roofGreen, roofBlue);
    }

    void addGabledBlock(float minX, float minZ, float maxX, float maxZ, float bodyHeight, float peakHeight, float roofRed, float roofGreen, float roofBlue) {
        const float width = std::max(0.01f, maxX - minX);
        const float depth = std::max(0.01f, maxZ - minZ);
        const float insetX = std::min(0.05f, width * 0.16f);
        const float insetZ = std::min(0.05f, depth * 0.16f);
        addBox(minX + insetX, 0.0f, minZ + insetZ, maxX - insetX, bodyHeight, maxZ - insetZ, 1.0f, 1.0f, 1.0f);

        const float ridgeX = (minX + maxX) * 0.5f;
        const Vertex nw(minX, bodyHeight, minZ, roofRed, roofGreen, roofBlue);
        const Vertex ne(maxX, bodyHeight, minZ, roofRed, roofGreen, roofBlue);
        const Vertex sw(minX, bodyHeight, maxZ, roofRed, roofGreen, roofBlue);
        const Vertex se(maxX, bodyHeight, maxZ, roofRed, roofGreen, roofBlue);
        const Vertex nr(ridgeX, peakHeight, minZ, roofRed, roofGreen, roofBlue);
        const Vertex sr(ridgeX, peakHeight, maxZ, roofRed, roofGreen, roofBlue);
        addTriangle(nw, ne, nr);
        addTriangle(sw, sr, se);
        addQuad(nw, nr, sr, sw);
        addQuad(ne, se, sr, nr);
    }

    const std::vector<Vertex>& vertices() const {
        return vertices_;
    }

private:
    std::vector<Vertex> vertices_;
};

class MeshRecipe {
public:
    virtual ~MeshRecipe() {
    }

    virtual std::string key() const = 0;
    virtual void build(MeshBuilder& builder) const = 0;
};

class BoxRecipe : public MeshRecipe {
public:
    std::string key() const override {
        return "box";
    }

    void build(MeshBuilder& builder) const override {
        builder.addBox(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    }
};

class MissingMeshPlaceholderRecipe : public MeshRecipe {
public:
    std::string key() const override {
        return "missing_mesh_placeholder";
    }

    void build(MeshBuilder& builder) const override {
        builder.addBox(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);

        const int checks = 4;
        const float step = 1.0f / static_cast<float>(checks);
        const float plate = 0.012f;
        const float white = 8.0f;
        int row = 0;
        for (; row < checks; ++row) {
            int column = 0;
            for (; column < checks; ++column) {
                if (((row + column) & 1) != 0) {
                    continue;
                }

                const float minA = static_cast<float>(column) * step;
                const float maxA = static_cast<float>(column + 1) * step;
                const float minB = static_cast<float>(row) * step;
                const float maxB = static_cast<float>(row + 1) * step;
                builder.addBox(minA, 1.0f, minB, maxA, 1.0f + plate, maxB, white, white, white);
                builder.addBox(minA, minB, -plate, maxA, maxB, 0.0f, white, white, white);
                builder.addBox(minA, minB, 1.0f, maxA, maxB, 1.0f + plate, white, white, white);
                builder.addBox(-plate, minA, minB, 0.0f, maxA, maxB, white, white, white);
                builder.addBox(1.0f, minA, minB, 1.0f + plate, maxA, maxB, white, white, white);
            }
        }
    }
};

class FlatSurfaceRecipe : public MeshRecipe {
public:
    std::string key() const override {
        return "flat_surface";
    }

    void build(MeshBuilder& builder) const override {
        builder.addBox(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    }
};

class GabledRoofRecipe : public MeshRecipe {
public:
    std::string key() const override {
        return "gabled_roof";
    }

    void build(MeshBuilder& builder) const override {
        builder.addGabledBlock(0.68f, 0.62f, 0.38f, 0.25f);
    }
};

class SegmentedRowhouseRoofRecipe : public MeshRecipe {
public:
    SegmentedRowhouseRoofRecipe(const std::string& meshKey, int segmentCount)
        : meshKey_(meshKey),
          segmentCount_(segmentCount) {
    }

    std::string key() const override {
        return meshKey_;
    }

    void build(MeshBuilder& builder) const override {
        const float segmentWidth = 1.0f / static_cast<float>(std::max(1, segmentCount_));
        const float gap = 0.012f;
        int segmentIndex = 0;
        for (; segmentIndex < segmentCount_; ++segmentIndex) {
            const float minX = segmentWidth * static_cast<float>(segmentIndex) + (segmentIndex == 0 ? 0.0f : gap);
            const float maxX = segmentWidth * static_cast<float>(segmentIndex + 1) - (segmentIndex + 1 == segmentCount_ ? 0.0f : gap);
            builder.addGabledBlock(minX, 0.0f, maxX, 1.0f, 0.68f, 1.0f, 0.62f, 0.38f, 0.25f);
        }
    }

private:
    std::string meshKey_;
    int segmentCount_;
};

class PyramidRecipe : public MeshRecipe {
public:
    std::string key() const override {
        return "pyramid";
    }

    void build(MeshBuilder& builder) const override {
        builder.addPyramid(0.08f, 0.0f, 0.08f, 0.92f, 1.0f, 0.92f, 1.0f, 1.0f, 1.0f);
    }
};

class PineTreeRecipe : public MeshRecipe {
public:
    std::string key() const override {
        return "pine_tree";
    }

    void build(MeshBuilder& builder) const override {
        builder.addBox(0.0f, 0.0f, 0.0f, 1.0f, 0.08f, 1.0f, 1.0f, 1.08f, 0.92f);
        builder.addBox(0.45f, 0.08f, 0.45f, 0.55f, 0.42f, 0.55f, 1.15f, 0.58f, 0.22f);
        builder.addPyramid(0.23f, 0.32f, 0.23f, 0.77f, 0.78f, 0.77f, 0.62f, 1.25f, 0.62f);
        builder.addPyramid(0.30f, 0.58f, 0.30f, 0.70f, 1.0f, 0.70f, 0.50f, 1.12f, 0.50f);
    }
};

class IndustrialPropsRecipe : public MeshRecipe {
public:
    std::string key() const override {
        return "industrial_props";
    }

    void build(MeshBuilder& builder) const override {
        builder.addBox(0.0f, 0.0f, 0.0f, 1.0f, 0.12f, 1.0f, 1.0f, 1.0f, 1.0f);
        builder.addBox(0.12f, 0.12f, 0.18f, 0.42f, 0.34f, 0.38f, 2.05f, 1.72f, 0.20f);
        builder.addBox(0.07f, 0.13f, 0.20f, 0.16f, 0.20f, 0.36f, 2.05f, 1.72f, 0.20f);
        builder.addBox(0.10f, 0.34f, 0.18f, 0.25f, 0.52f, 0.38f, 2.05f, 1.72f, 0.20f);
        builder.addBox(0.58f, 0.12f, 0.18f, 0.78f, 0.30f, 0.38f, 1.18f, 0.76f, 0.36f);
        builder.addBox(0.78f, 0.12f, 0.18f, 0.92f, 0.26f, 0.35f, 1.18f, 0.76f, 0.36f);
        builder.addBox(0.62f, 0.12f, 0.54f, 0.85f, 0.34f, 0.76f, 1.18f, 0.76f, 0.36f);
    }
};

class SmokestackRecipe : public MeshRecipe {
public:
    std::string key() const override {
        return "smokestack";
    }

    void build(MeshBuilder& builder) const override {
        builder.addBox(0.12f, 0.0f, 0.12f, 0.88f, 0.18f, 0.88f, 1.0f, 1.0f, 1.0f);
        builder.addBox(0.38f, 0.18f, 0.38f, 0.62f, 1.0f, 0.62f, 0.72f, 0.72f, 0.72f);
    }
};

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

bool WriteCatalog(const std::string& outputPath, const std::vector<std::unique_ptr<MeshRecipe> >& recipes) {
    std::ofstream stream(outputPath.c_str(), std::ios::out | std::ios::trunc);
    if (!stream.is_open()) {
        std::cerr << "Unable to write " << outputPath << std::endl;
        return false;
    }

    stream << "CBGM 1\n";
    std::size_t recipeIndex = 0;
    for (; recipeIndex < recipes.size(); ++recipeIndex) {
        MeshBuilder builder;
        recipes[recipeIndex]->build(builder);
        stream << "mesh " << recipes[recipeIndex]->key() << "\n";
        const std::vector<Vertex>& vertices = builder.vertices();
        std::size_t vertexIndex = 0;
        for (; vertexIndex < vertices.size(); ++vertexIndex) {
            const Vertex& vertex = vertices[vertexIndex];
            stream << "v "
                << vertex.x << " " << vertex.y << " " << vertex.z << " "
                << vertex.r << " " << vertex.g << " " << vertex.b << "\n";
        }
        stream << "endmesh\n";
    }

    return true;
}
}

int main(int argc, char** argv) {
    const std::string dataDirectory = argc > 1 && argv[1] != 0 ? argv[1] : "Data";
    const std::string generatedDirectory = JoinPath(dataDirectory, "Generated");
    CreateDirectoryA(generatedDirectory.c_str(), 0);

    std::vector<std::unique_ptr<MeshRecipe> > recipes;
    recipes.push_back(std::unique_ptr<MeshRecipe>(new BoxRecipe()));
    recipes.push_back(std::unique_ptr<MeshRecipe>(new MissingMeshPlaceholderRecipe()));
    recipes.push_back(std::unique_ptr<MeshRecipe>(new FlatSurfaceRecipe()));
    recipes.push_back(std::unique_ptr<MeshRecipe>(new GabledRoofRecipe()));
    recipes.push_back(std::unique_ptr<MeshRecipe>(new SegmentedRowhouseRoofRecipe("rowhouse_2_roof", 3)));
    recipes.push_back(std::unique_ptr<MeshRecipe>(new SegmentedRowhouseRoofRecipe("rowhouse_3_roof", 3)));
    recipes.push_back(std::unique_ptr<MeshRecipe>(new PyramidRecipe()));
    recipes.push_back(std::unique_ptr<MeshRecipe>(new PineTreeRecipe()));
    recipes.push_back(std::unique_ptr<MeshRecipe>(new IndustrialPropsRecipe()));
    recipes.push_back(std::unique_ptr<MeshRecipe>(new SmokestackRecipe()));

    const std::string outputPath = JoinPath(generatedDirectory, "module_meshes.txt");
    if (!WriteCatalog(outputPath, recipes)) {
        return EXIT_FAILURE;
    }

    std::cout << "Generated module mesh catalog: " << outputPath << std::endl;
    return EXIT_SUCCESS;
}
