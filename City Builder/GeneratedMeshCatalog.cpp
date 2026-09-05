#include "GeneratedMeshCatalog.h"

#include <fstream>
#include <sstream>
#include <cmath>
#include "LotMaterials.h"

bool GeneratedMeshCatalog::loadFromFile(const std::string& path, std::string& errorMessage) {
    clear();

    std::ifstream stream(path.c_str(), std::ios::in);
    if (!stream.is_open()) {
        errorMessage = "Unable to open generated mesh catalog: " + path;
        return false;
    }

    std::string magic;
    int version = 0;
    stream >> magic >> version;
    if (!stream || magic != "CBGM" || (version != 1 && version != 2)) {
        errorMessage = "Generated mesh catalog has an unsupported header: " + path;
        clear();
        return false;
    }

    std::string token;
    while (stream >> token) {
        if (token != "mesh") {
            errorMessage = "Generated mesh catalog expected 'mesh' but found '" + token + "'.";
            clear();
            return false;
        }

        std::string meshName;
        stream >> meshName;
        if (!stream || meshName.empty()) {
            errorMessage = "Generated mesh catalog contains an invalid mesh key.";
            clear();
            return false;
        }
        if (rangeIndexByMeshKey_.find(meshName) != rangeIndexByMeshKey_.end()) {
            errorMessage = "Generated mesh catalog contains duplicate mesh key '" + meshName + "'.";
            clear();
            return false;
        }

        GeneratedMeshRange range;
        range.name = meshName;
        range.firstVertex = static_cast<int>(vertices_.size());

        while (stream >> token) {
            if (token == "endmesh") {
                break;
            }
            if (token != "v") {
                errorMessage = "Generated mesh catalog expected vertex or endmesh in mesh '" + meshName + "'.";
                clear();
                return false;
            }

            GeneratedMeshVertex vertex;
            stream >> vertex.x >> vertex.y >> vertex.z >> vertex.colorR >> vertex.colorG >> vertex.colorB;
            if (version == 2) stream >> vertex.normalX >> vertex.normalY >> vertex.normalZ >> vertex.u >> vertex.v >> vertex.material >> vertex.ambient;
            if (!stream) {
                errorMessage = "Generated mesh catalog contains a malformed vertex in mesh '" + meshName + "'.";
                clear();
                return false;
            }
            const float fields[] = {vertex.x,vertex.y,vertex.z,vertex.colorR,vertex.colorG,vertex.colorB,
                vertex.normalX,vertex.normalY,vertex.normalZ,vertex.u,vertex.v,vertex.material,vertex.ambient};
            for (float value : fields) if (!std::isfinite(value)) {
                errorMessage = "Non-finite mesh vertex in '" + meshName + "'."; clear(); return false;
            }
            if (vertex.material < 0 || vertex.material >= MaterialCount || vertex.ambient < 0 || vertex.ambient > 1) {
                errorMessage = "Invalid material/AO in '" + meshName + "'."; clear(); return false;
            }
            vertices_.push_back(vertex);
        }

        if (token != "endmesh") {
            errorMessage = "Generated mesh catalog mesh '" + meshName + "' is missing endmesh.";
            clear();
            return false;
        }

        range.vertexCount = static_cast<int>(vertices_.size()) - range.firstVertex;
        if (range.vertexCount <= 0 || (range.vertexCount % 3) != 0) {
            errorMessage = "Generated mesh catalog mesh '" + meshName + "' must contain complete triangles.";
            clear();
            return false;
        }
        rangeIndexByMeshKey_[meshName] = ranges_.size();
        ranges_.push_back(range);
    }

    if (findMesh("box") == 0) {
        errorMessage = "Generated mesh catalog is missing the default box mesh.";
        clear();
        return false;
    }

    errorMessage.clear();
    return true;
}

void GeneratedMeshCatalog::clear() {
    vertices_.clear();
    ranges_.clear();
    rangeIndexByMeshKey_.clear();
}

const std::vector<GeneratedMeshVertex>& GeneratedMeshCatalog::vertices() const {
    return vertices_;
}

const GeneratedMeshRange* GeneratedMeshCatalog::findMesh(const std::string& meshKey) const {
    const std::unordered_map<std::string, std::size_t>::const_iterator iterator = rangeIndexByMeshKey_.find(meshKey);
    if (iterator == rangeIndexByMeshKey_.end()) {
        return 0;
    }

    return &ranges_[iterator->second];
}
