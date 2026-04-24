#include "AssetLoader.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace {
struct ParsedTag {
    std::string name;
    std::string attributes;
    bool isClosing;
    bool isSelfClosing;

    // Starts with an empty tag until ParseTag fills the fields.
    ParsedTag()
        : isClosing(false),
          isSelfClosing(false) {
    }
};

// Reads a whole UTF-8-ish asset file into memory for the small XML parser.
std::string ReadTextFile(const std::string& path) {
    std::ifstream stream(path.c_str(), std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("Unable to open asset file: " + path);
    }

    std::ostringstream builder;
    builder << stream.rdbuf();
    return builder.str();
}

// Removes leading and trailing whitespace from XML tag fragments.
std::string Trim(const std::string& text) {
    std::size_t startIndex = 0;
    while (startIndex < text.size() && std::isspace(static_cast<unsigned char>(text[startIndex])) != 0) {
        ++startIndex;
    }

    std::size_t endIndex = text.size();
    while (endIndex > startIndex && std::isspace(static_cast<unsigned char>(text[endIndex - 1])) != 0) {
        --endIndex;
    }

    return text.substr(startIndex, endIndex - startIndex);
}

// Uses the file stem as the fallback asset id.
std::string StripExtension(const std::string& fileName) {
    const std::size_t extensionIndex = fileName.find_last_of('.');
    if (extensionIndex == std::string::npos) {
        return fileName;
    }

    return fileName.substr(0, extensionIndex);
}

// Splits one XML tag token into name, attributes, and closing flags.
ParsedTag ParseTag(const std::string& token) {
    ParsedTag tag;
    std::string trimmedToken = Trim(token);
    if (trimmedToken.empty()) {
        throw std::runtime_error("Encountered empty XML tag.");
    }

    if (trimmedToken[0] == '/') {
        tag.isClosing = true;
        trimmedToken = Trim(trimmedToken.substr(1));
    }

    if (!trimmedToken.empty() && trimmedToken[trimmedToken.size() - 1] == '/') {
        tag.isSelfClosing = true;
        trimmedToken = Trim(trimmedToken.substr(0, trimmedToken.size() - 1));
    }

    const std::size_t separatorIndex = trimmedToken.find_first_of(" \t\r\n");
    if (separatorIndex == std::string::npos) {
        tag.name = trimmedToken;
        tag.attributes.clear();
    } else {
        tag.name = trimmedToken.substr(0, separatorIndex);
        tag.attributes = Trim(trimmedToken.substr(separatorIndex + 1));
    }

    return tag;
}

// Fetches a required quoted XML attribute or throws with context.
std::string GetRequiredAttribute(const std::string& attributes, const std::string& attributeName) {
    const std::string attributePattern = attributeName + "=\"";
    const std::size_t startIndex = attributes.find(attributePattern);
    if (startIndex == std::string::npos) {
        throw std::runtime_error("Missing required attribute: " + attributeName);
    }

    const std::size_t valueStartIndex = startIndex + attributePattern.size();
    const std::size_t valueEndIndex = attributes.find('"', valueStartIndex);
    if (valueEndIndex == std::string::npos) {
        throw std::runtime_error("Unterminated attribute: " + attributeName);
    }

    return attributes.substr(valueStartIndex, valueEndIndex - valueStartIndex);
}

// Fetches an optional quoted XML attribute with a caller-provided default.
std::string GetOptionalAttribute(const std::string& attributes, const std::string& attributeName, const std::string& defaultValue) {
    const std::string attributePattern = attributeName + "=\"";
    const std::size_t startIndex = attributes.find(attributePattern);
    if (startIndex == std::string::npos) {
        return defaultValue;
    }

    const std::size_t valueStartIndex = startIndex + attributePattern.size();
    const std::size_t valueEndIndex = attributes.find('"', valueStartIndex);
    if (valueEndIndex == std::string::npos) {
        throw std::runtime_error("Unterminated attribute: " + attributeName);
    }

    return attributes.substr(valueStartIndex, valueEndIndex - valueStartIndex);
}

// Parses a required integer XML attribute.
int ParseRequiredInt(const std::string& attributes, const std::string& attributeName) {
    return std::stoi(GetRequiredAttribute(attributes, attributeName));
}

// Parses an optional float XML attribute.
float ParseOptionalFloat(const std::string& attributes, const std::string& attributeName, float defaultValue) {
    const std::string value = GetOptionalAttribute(attributes, attributeName, "");
    if (value.empty()) {
        return defaultValue;
    }

    return static_cast<float>(std::atof(value.c_str()));
}

// Extracts XML tags while skipping declarations and comments.
std::vector<std::string> ExtractTagTokens(const std::string& xmlText) {
    std::vector<std::string> tokens;
    std::size_t openIndex = xmlText.find('<');
    while (openIndex != std::string::npos) {
        const std::size_t closeIndex = xmlText.find('>', openIndex + 1);
        if (closeIndex == std::string::npos) {
            throw std::runtime_error("Malformed XML: missing closing >");
        }

        const std::string token = xmlText.substr(openIndex + 1, closeIndex - openIndex - 1);
        if (token.find('?') != 0 && token.find("!--") != 0) {
            tokens.push_back(token);
        }

        openIndex = xmlText.find('<', closeIndex + 1);
    }

    return tokens;
}

// Loads one module archetype from XML and validates its required fields.
LotModule LoadModuleAsset(const std::string& filePath, const std::string& fileName) {
    const std::vector<std::string> tokens = ExtractTagTokens(ReadTextFile(filePath));
    if (tokens.empty()) {
        throw std::runtime_error("Empty module XML: " + filePath);
    }

    const ParsedTag rootTag = ParseTag(tokens[0]);
    if (rootTag.name != "module" || rootTag.isClosing) {
        throw std::runtime_error("Module XML must start with <module>: " + filePath);
    }

    LotModule module;
    module.id = GetOptionalAttribute(rootTag.attributes, "id", StripExtension(fileName));
    if (module.id.empty()) {
        throw std::runtime_error("Module id cannot be empty: " + filePath);
    }

    bool hasSize = false;
    bool hasEffects = false;

    std::size_t tokenIndex = 1;
    for (; tokenIndex < tokens.size(); ++tokenIndex) {
        const ParsedTag tag = ParseTag(tokens[tokenIndex]);
        if (tag.isClosing) {
            if (tag.name != "module") {
                throw std::runtime_error("Unexpected closing tag in module XML: " + filePath);
            }

            break;
        }

        if (tag.name == "size" && tag.isSelfClosing) {
            module.width = ParseRequiredInt(tag.attributes, "width");
            module.height = ParseRequiredInt(tag.attributes, "height");
            hasSize = true;
            continue;
        }

        if (tag.name == "effects" && tag.isSelfClosing) {
            module.airPollutionEmit = ParseRequiredInt(tag.attributes, "airPollution");
            module.landValueEmit = ParseRequiredInt(tag.attributes, "landValue");
            hasEffects = true;
            continue;
        }

        if (tag.name == "render" && tag.isSelfClosing) {
            module.renderHeight = ParseOptionalFloat(tag.attributes, "height", module.renderHeight);
            module.colorR = ParseOptionalFloat(tag.attributes, "colorR", module.colorR);
            module.colorG = ParseOptionalFloat(tag.attributes, "colorG", module.colorG);
            module.colorB = ParseOptionalFloat(tag.attributes, "colorB", module.colorB);
            continue;
        }

        throw std::runtime_error("Unsupported module tag: <" + tag.name + "> in " + filePath);
    }

    if (!hasSize || !hasEffects) {
        throw std::runtime_error("Module XML missing required <size> or <effects> tag: " + filePath);
    }

    if (module.width <= 0 || module.height <= 0) {
        throw std::runtime_error("Module dimensions must be positive: " + filePath);
    }

    return module;
}

// Loads one lot archetype and its initial module placements from XML.
LotAsset LoadLotAsset(const std::string& filePath, const std::string& fileName) {
    const std::vector<std::string> tokens = ExtractTagTokens(ReadTextFile(filePath));
    if (tokens.empty()) {
        throw std::runtime_error("Empty lot XML: " + filePath);
    }

    const ParsedTag rootTag = ParseTag(tokens[0]);
    if (rootTag.name != "lot" || rootTag.isClosing) {
        throw std::runtime_error("Lot XML must start with <lot>: " + filePath);
    }

    LotAsset lotAsset;
    lotAsset.id = GetOptionalAttribute(rootTag.attributes, "id", StripExtension(fileName));
    if (lotAsset.id.empty()) {
        throw std::runtime_error("Lot id cannot be empty: " + filePath);
    }

    bool hasAnchor = false;
    bool isInsideModulesBlock = false;

    std::size_t tokenIndex = 1;
    for (; tokenIndex < tokens.size(); ++tokenIndex) {
        const ParsedTag tag = ParseTag(tokens[tokenIndex]);
        if (tag.isClosing) {
            if (tag.name == "modules") {
                isInsideModulesBlock = false;
                continue;
            }

            if (tag.name == "lot") {
                break;
            }

            throw std::runtime_error("Unexpected closing tag in lot XML: " + filePath);
        }

        if (tag.name == "anchor" && tag.isSelfClosing) {
            lotAsset.anchor.x = ParseRequiredInt(tag.attributes, "x");
            lotAsset.anchor.y = ParseRequiredInt(tag.attributes, "y");
            hasAnchor = true;
            continue;
        }

        if (tag.name == "renderOrigin" && tag.isSelfClosing) {
            lotAsset.renderOrigin.x = ParseRequiredInt(tag.attributes, "x");
            lotAsset.renderOrigin.y = ParseRequiredInt(tag.attributes, "y");
            continue;
        }

        if (tag.name == "modules" && !tag.isSelfClosing) {
            isInsideModulesBlock = true;
            continue;
        }

        if (tag.name == "moduleRef" && tag.isSelfClosing && isInsideModulesBlock) {
            LotModulePlacementDefinition placementDefinition;
            placementDefinition.moduleId = GetRequiredAttribute(tag.attributes, "id");
            placementDefinition.localOrigin.x = ParseRequiredInt(tag.attributes, "x");
            placementDefinition.localOrigin.y = ParseRequiredInt(tag.attributes, "y");
            lotAsset.initialModules.push_back(placementDefinition);
            continue;
        }

        throw std::runtime_error("Unsupported lot tag: <" + tag.name + "> in " + filePath);
    }

    if (!hasAnchor) {
        throw std::runtime_error("Lot XML missing required <anchor> tag: " + filePath);
    }

    if (lotAsset.initialModules.empty()) {
        throw std::runtime_error("Lot XML must define at least one initial module: " + filePath);
    }

    return lotAsset;
}

// Verifies that a lot references real modules and occupies its anchor tile.
void ValidateLotAsset(LotAsset& lotAsset, const std::vector<LotModule>& modules) {
    std::set<std::string> moduleIds;
    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules.size(); ++moduleIndex) {
        moduleIds.insert(modules[moduleIndex].id);
    }

    std::vector<Int2> occupiedTiles;
    std::size_t placementIndex = 0;
    for (; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        const LotModulePlacementDefinition& placement = lotAsset.initialModules[placementIndex];
        if (moduleIds.find(placement.moduleId) == moduleIds.end()) {
            throw std::runtime_error("Lot asset '" + lotAsset.id + "' references unknown module '" + placement.moduleId + "'");
        }

        std::size_t candidateModuleIndex = 0;
        for (; candidateModuleIndex < modules.size(); ++candidateModuleIndex) {
            if (modules[candidateModuleIndex].id != placement.moduleId) {
                continue;
            }

            int tileY = 0;
            for (; tileY < modules[candidateModuleIndex].height; ++tileY) {
                int tileX = 0;
                for (; tileX < modules[candidateModuleIndex].width; ++tileX) {
                    occupiedTiles.push_back(Int2(placement.localOrigin.x + tileX, placement.localOrigin.y + tileY));
                }
            }

            break;
        }
    }

    bool anchorIsOccupied = false;
    std::size_t tileIndex = 0;
    for (; tileIndex < occupiedTiles.size(); ++tileIndex) {
        if (occupiedTiles[tileIndex] == lotAsset.anchor) {
            anchorIsOccupied = true;
            break;
        }
    }

    if (!anchorIsOccupied) {
        throw std::runtime_error("Lot asset '" + lotAsset.id + "' anchor must be inside the tiles occupied by its initial modules.");
    }

    for (placementIndex = 0; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        lotAsset.initialModules[placementIndex].localOrigin.x -= lotAsset.anchor.x;
        lotAsset.initialModules[placementIndex].localOrigin.y -= lotAsset.anchor.y;
    }

    lotAsset.renderOrigin.x -= lotAsset.anchor.x;
    lotAsset.renderOrigin.y -= lotAsset.anchor.y;
    lotAsset.anchor = Int2(0, 0);
}

// Enumerates XML files in a data directory using the Win32 file API.
std::vector<std::string> CollectXmlFiles(const std::string& directoryPath) {
    std::vector<std::string> files;
    std::string searchPattern = directoryPath + "\\*.xml";

    WIN32_FIND_DATAA findData;
    HANDLE findHandle = FindFirstFileA(searchPattern.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return files;
    }

    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }

        files.push_back(findData.cFileName);
    } while (FindNextFileA(findHandle, &findData) != FALSE);

    FindClose(findHandle);
    std::sort(files.begin(), files.end());
    return files;
}
}

// Loads all module and lot archetypes, returning errors instead of throwing across the runtime boundary.
bool LoadGameAssets(const std::string& dataDirectory, LoadedGameAssets& assets, std::string& errorMessage) {
    assets.modules.clear();
    assets.lots.clear();

    try {
        const std::string modulesDirectory = dataDirectory + "\\Modules";
        const std::string lotsDirectory = dataDirectory + "\\Lots";

        const std::vector<std::string> moduleFiles = CollectXmlFiles(modulesDirectory);
        const std::vector<std::string> lotFiles = CollectXmlFiles(lotsDirectory);
        if (moduleFiles.empty()) {
            throw std::runtime_error("No module XML files found in " + modulesDirectory);
        }

        if (lotFiles.empty()) {
            throw std::runtime_error("No lot XML files found in " + lotsDirectory);
        }

        std::unordered_set<std::string> seenIds;
        std::size_t fileIndex = 0;
        for (; fileIndex < moduleFiles.size(); ++fileIndex) {
            LotModule module = LoadModuleAsset(modulesDirectory + "\\" + moduleFiles[fileIndex], moduleFiles[fileIndex]);
            if (!seenIds.insert(module.id).second) {
                throw std::runtime_error("Duplicate asset id: " + module.id);
            }

            assets.modules.push_back(module);
        }

        seenIds.clear();
        fileIndex = 0;
        for (; fileIndex < lotFiles.size(); ++fileIndex) {
            LotAsset lotAsset = LoadLotAsset(lotsDirectory + "\\" + lotFiles[fileIndex], lotFiles[fileIndex]);
            ValidateLotAsset(lotAsset, assets.modules);
            if (!seenIds.insert(lotAsset.id).second) {
                throw std::runtime_error("Duplicate lot asset id: " + lotAsset.id);
            }

            assets.lots.push_back(lotAsset);
        }
    } catch (const std::exception& error) {
        errorMessage = error.what();
        assets.modules.clear();
        assets.lots.clear();
        return false;
    }

    errorMessage.clear();
    return true;
}
