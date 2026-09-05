#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct GeneratedMeshVertex {
    float x;
    float y;
    float z;
    float colorR;
    float colorG;
    float colorB;
    float normalX = 0.0f;
    float normalY = 1.0f;
    float normalZ = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float material = 0.0f;
    float ambient = 1.0f;

    GeneratedMeshVertex()
        : x(0.0f),
          y(0.0f),
          z(0.0f),
          colorR(1.0f),
          colorG(1.0f),
          colorB(1.0f) {
    }
};

struct GeneratedMeshRange {
    std::string name;
    int firstVertex;
    int vertexCount;

    GeneratedMeshRange()
        : firstVertex(0),
          vertexCount(0) {
    }
};

class GeneratedMeshCatalog {
public:
    bool loadFromFile(const std::string& path, std::string& errorMessage);
    void clear();

    const std::vector<GeneratedMeshVertex>& vertices() const;
    const GeneratedMeshRange* findMesh(const std::string& meshKey) const;

private:
    std::vector<GeneratedMeshVertex> vertices_;
    std::vector<GeneratedMeshRange> ranges_;
    std::unordered_map<std::string, std::size_t> rangeIndexByMeshKey_;
};
