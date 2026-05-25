#include "AssetLoader.h"

#include "CrashLogger.h"
#include "SimulationTime.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

std::string ToLowerAscii(std::string text) {
    std::size_t index = 0;
    for (; index < text.size(); ++index) {
        text[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[index])));
    }

    return text;
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

bool FileExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Parses a required integer XML attribute.
int ParseRequiredInt(const std::string& attributes, const std::string& attributeName) {
    return std::stoi(GetRequiredAttribute(attributes, attributeName));
}

// Parses an optional integer XML attribute.
int ParseOptionalInt(const std::string& attributes, const std::string& attributeName, int defaultValue) {
    const std::string value = GetOptionalAttribute(attributes, attributeName, "");
    if (value.empty()) {
        return defaultValue;
    }

    return std::stoi(value);
}

int ParseOptionalDayDurationAsTicks(const std::string& attributes, const std::string& attributeName, int defaultValue) {
    const std::string value = GetOptionalAttribute(attributes, attributeName, "");
    if (value.empty()) {
        return defaultValue;
    }

    const int days = std::max(0, std::stoi(value));
    return static_cast<int>(SimulationTime::daysToTicks(static_cast<std::uint64_t>(days)));
}

int ParseRequiredDayDurationAsTicks(const std::string& attributes, const std::string& attributeName) {
    const int days = std::max(0, std::stoi(GetRequiredAttribute(attributes, attributeName)));
    return static_cast<int>(SimulationTime::daysToTicks(static_cast<std::uint64_t>(days)));
}

// Parses an optional float XML attribute.
float ParseOptionalFloat(const std::string& attributes, const std::string& attributeName, float defaultValue) {
    const std::string value = GetOptionalAttribute(attributes, attributeName, "");
    if (value.empty()) {
        return defaultValue;
    }

    return static_cast<float>(std::atof(value.c_str()));
}

// Parses a required float XML attribute.
float ParseRequiredFloat(const std::string& attributes, const std::string& attributeName) {
    return static_cast<float>(std::atof(GetRequiredAttribute(attributes, attributeName).c_str()));
}

float ParseOptionalRatio(const std::string& attributes, const std::string& attributeName, float defaultValue) {
    const std::string value = GetOptionalAttribute(attributes, attributeName, "");
    if (value.empty()) {
        return defaultValue;
    }

    const float parsed = static_cast<float>(std::atof(value.c_str()));
    return parsed > 10.0f ? parsed / 100.0f : parsed;
}

std::uint8_t ParseDirectionName(const std::string& directionText) {
    const std::string direction = ToLowerAscii(Trim(directionText));
    if (direction == "north" || direction == "n") {
        return kRoadDirectionNorth;
    }
    if (direction == "east" || direction == "e") {
        return kRoadDirectionEast;
    }
    if (direction == "south" || direction == "s") {
        return kRoadDirectionSouth;
    }
    if (direction == "west" || direction == "w") {
        return kRoadDirectionWest;
    }

    throw std::runtime_error("Unknown direction: " + directionText);
}

std::uint16_t ParseZoningTypeName(const std::string& zoningTypeText) {
    const std::string zoningType = ToLowerAscii(Trim(zoningTypeText));
    if (zoningType.empty() || zoningType == "none" || zoningType == "empty" || zoningType == "0") {
        return TileZoningNone;
    }
    if (zoningType == "residential_low" || zoningType == "low_residential" || zoningType == "low_density_residential" || zoningType == "tilezoningresidentiallow" || zoningType == "3") {
        return TileZoningResidentialLow;
    }
    if (zoningType == "residential" || zoningType == "residence" || zoningType == "r" || zoningType == "1" || zoningType == "tilezoningresidential") {
        return TileZoningResidentialHigh;
    }
    if (zoningType == "residential_high" || zoningType == "high_residential" || zoningType == "high_density_residential" || zoningType == "tilezoningresidentialhigh") {
        return TileZoningResidentialHigh;
    }
    if (zoningType == "industrial" || zoningType == "industry" || zoningType == "i" || zoningType == "2" || zoningType == "tilezoningindustrial") {
        return TileZoningIndustrial;
    }

    throw std::runtime_error("Unknown lot zoning type: " + zoningTypeText);
}

std::string DefaultRciTypeIdForZoningType(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidentialLow || zoningType == TileZoningResidentialHigh) {
        return "low_wealth_residential";
    }
    if (zoningType == TileZoningIndustrial) {
        return "dirty_industry";
    }

    return std::string();
}

RciDesirabilityField ParseRciDesirabilityFieldName(const std::string& fieldText) {
    const std::string field = ToLowerAscii(Trim(fieldText));
    if (field == "airpollution" || field == "air_pollution" || field == "pollution") {
        return RciDesirabilityField::AirPollution;
    }
    if (field == "parkeffect" || field == "park_effect" || field == "parks") {
        return RciDesirabilityField::ParkEffect;
    }

    throw std::runtime_error("Unknown RCI desirability field: " + fieldText);
}

std::uint8_t ParseTransportModeMask(const std::string& modesText) {
    std::uint8_t modeMask = 0;
    std::string token;
    std::size_t index = 0;
    for (; index <= modesText.size(); ++index) {
        const char character = index < modesText.size() ? modesText[index] : ',';
        if (character == ',' || character == '|' || std::isspace(static_cast<unsigned char>(character)) != 0) {
            const std::string normalizedToken = ToLowerAscii(Trim(token));
            token.clear();
            if (normalizedToken.empty()) {
                continue;
            }

            if (normalizedToken == "car" || normalizedToken == "cars") {
                modeMask |= kTransportModeCar;
            } else if (normalizedToken == "pedestrian" || normalizedToken == "pedestrians" || normalizedToken == "walk") {
                modeMask |= kTransportModePedestrian;
            } else {
                throw std::runtime_error("Unknown transport mode in access declaration: " + normalizedToken);
            }
            continue;
        }

        token.push_back(character);
    }

    if (modeMask == 0u) {
        throw std::runtime_error("Access declaration must include at least one transport mode.");
    }

    return modeMask;
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
LotModule LoadModuleAsset(const std::string& filePath, const std::string& fileName, const CityParameterRegistry& parameterRegistry) {
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
    module.density = GetOptionalAttribute(rootTag.attributes, "density", "");
    if (module.id.empty()) {
        throw std::runtime_error("Module id cannot be empty: " + filePath);
    }
    if (!module.density.empty() &&
        module.density != "low" &&
        module.density != "medium" &&
        module.density != "high") {
        throw std::runtime_error("Module '" + module.id + "' has invalid density '" + module.density + "' in " + filePath);
    }

    bool hasSize = false;
    bool hasEffects = false;
    bool isInsideParametersBlock = false;

    std::size_t tokenIndex = 1;
    for (; tokenIndex < tokens.size(); ++tokenIndex) {
        const ParsedTag tag = ParseTag(tokens[tokenIndex]);
        if (tag.isClosing) {
            if (tag.name == "parameters") {
                isInsideParametersBlock = false;
                continue;
            }

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
            module.parkEffectEmit = ParseOptionalInt(tag.attributes, "parkEffect", 0);
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

        if (tag.name == "parameters" && !tag.isSelfClosing) {
            isInsideParametersBlock = true;
            continue;
        }

        if ((tag.name == "driver" || tag.name == "satisfaction") && tag.isSelfClosing && isInsideParametersBlock) {
            const std::string parameterId = GetRequiredAttribute(tag.attributes, "id");
            const int resolvedParameterId = parameterRegistry.parameterId(parameterId);
            if (resolvedParameterId < 0) {
                throw std::runtime_error("Module '" + module.id + "' references unknown city parameter '" + parameterId + "' in " + filePath);
            }

            const CityParameterKind expectedKind = tag.name == "driver" ? CityParameterKind::Driver : CityParameterKind::Satisfaction;
            if (parameterRegistry.definition(resolvedParameterId).kind != expectedKind) {
                throw std::runtime_error("Module '" + module.id + "' parameter '" + parameterId + "' uses the wrong parameter kind in " + filePath);
            }

            CityParameterContribution contribution;
            contribution.parameterId = resolvedParameterId;
            contribution.amount = ParseRequiredFloat(tag.attributes, "amount");
            module.parameterContributions.push_back(contribution);
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
    lotAsset.name = GetOptionalAttribute(rootTag.attributes, "name", lotAsset.id);
    lotAsset.densityBand = GetOptionalAttribute(rootTag.attributes, "densityBand", "");
    lotAsset.zoningType = ParseZoningTypeName(GetOptionalAttribute(rootTag.attributes, "zoningType", ""));
    lotAsset.rciTypeId = GetOptionalAttribute(rootTag.attributes, "rciType", DefaultRciTypeIdForZoningType(lotAsset.zoningType));
    lotAsset.constructionTicks = ParseOptionalInt(rootTag.attributes, "constructionTicks", lotAsset.constructionTicks);
    lotAsset.constructionTicks = ParseOptionalDayDurationAsTicks(rootTag.attributes, "constructionDays", lotAsset.constructionTicks);
    if (lotAsset.id.empty()) {
        throw std::runtime_error("Lot id cannot be empty: " + filePath);
    }
    if (lotAsset.zoningType != TileZoningNone && lotAsset.name.empty()) {
        throw std::runtime_error("RCI lot name cannot be empty: " + filePath);
    }
    if (lotAsset.zoningType != TileZoningNone && lotAsset.densityBand.empty()) {
        throw std::runtime_error("RCI lot densityBand cannot be empty: " + filePath);
    }
    if (lotAsset.zoningType != TileZoningNone && lotAsset.rciTypeId.empty()) {
        throw std::runtime_error("RCI lot rciType cannot be empty: " + filePath);
    }

    bool hasAnchor = false;
    bool hasFootprint = false;
    bool isInsideModulesBlock = false;
    bool isInsideAccessBlock = false;

    std::size_t tokenIndex = 1;
    for (; tokenIndex < tokens.size(); ++tokenIndex) {
        const ParsedTag tag = ParseTag(tokens[tokenIndex]);
        if (tag.isClosing) {
            if (tag.name == "modules") {
                isInsideModulesBlock = false;
                continue;
            }

            if (tag.name == "access") {
                isInsideAccessBlock = false;
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

        if (tag.name == "footprint" && tag.isSelfClosing) {
            lotAsset.footprintOrigin.x = ParseRequiredInt(tag.attributes, "x");
            lotAsset.footprintOrigin.y = ParseRequiredInt(tag.attributes, "y");
            lotAsset.footprintWidth = ParseRequiredInt(tag.attributes, "width");
            lotAsset.footprintHeight = ParseRequiredInt(tag.attributes, "height");
            hasFootprint = true;
            continue;
        }

        if (tag.name == "front" && tag.isSelfClosing) {
            lotAsset.frontDirection = ParseDirectionName(GetRequiredAttribute(tag.attributes, "direction"));
            lotAsset.hasFrontDirection = true;
            continue;
        }

        if (tag.name == "renderOrigin" && tag.isSelfClosing) {
            lotAsset.renderOrigin.x = ParseRequiredInt(tag.attributes, "x");
            lotAsset.renderOrigin.y = ParseRequiredInt(tag.attributes, "y");
            continue;
        }

        if (tag.name == "construction" && tag.isSelfClosing) {
            const std::string constructionDaysValue = GetOptionalAttribute(tag.attributes, "constructionDays", "");
            const std::string daysValue = GetOptionalAttribute(tag.attributes, "days", "");
            if (!constructionDaysValue.empty()) {
                lotAsset.constructionTicks = ParseRequiredDayDurationAsTicks(tag.attributes, "constructionDays");
            } else if (!daysValue.empty()) {
                lotAsset.constructionTicks = ParseRequiredDayDurationAsTicks(tag.attributes, "days");
            } else if (!GetOptionalAttribute(tag.attributes, "constructionTicks", "").empty()) {
                lotAsset.constructionTicks = ParseRequiredInt(tag.attributes, "constructionTicks");
            } else {
                lotAsset.constructionTicks = ParseRequiredInt(tag.attributes, "ticks");
            }
            continue;
        }

        if (tag.name == "modules" && !tag.isSelfClosing) {
            isInsideModulesBlock = true;
            continue;
        }

        if (tag.name == "access" && !tag.isSelfClosing) {
            isInsideAccessBlock = true;
            continue;
        }

        if (tag.name == "connection" && tag.isSelfClosing && isInsideAccessBlock) {
            LotAccessDefinition accessDefinition;
            accessDefinition.localTile.x = ParseRequiredInt(tag.attributes, "x");
            accessDefinition.localTile.y = ParseRequiredInt(tag.attributes, "y");
            const std::string directionText = GetOptionalAttribute(tag.attributes, "direction", "");
            if (directionText.empty()) {
                if (!lotAsset.hasFrontDirection) {
                    throw std::runtime_error("Lot access connection without direction requires a <front> direction in " + filePath);
                }
                accessDefinition.direction = lotAsset.frontDirection;
            } else {
                accessDefinition.direction = ParseDirectionName(directionText);
            }
            accessDefinition.modeMask = ParseTransportModeMask(GetRequiredAttribute(tag.attributes, "modes"));
            lotAsset.accessDefinitions.push_back(accessDefinition);
            continue;
        }

        if (tag.name == "perimeter" && tag.isSelfClosing && isInsideAccessBlock) {
            if (!hasFootprint) {
                throw std::runtime_error("Lot perimeter access requires an explicit <footprint> before <access> in " + filePath);
            }

            const std::uint8_t modeMask = ParseTransportModeMask(GetRequiredAttribute(tag.attributes, "modes"));
            int tileX = lotAsset.footprintOrigin.x;
            for (; tileX < lotAsset.footprintOrigin.x + lotAsset.footprintWidth; ++tileX) {
                LotAccessDefinition northAccess;
                northAccess.localTile = Int2(tileX, lotAsset.footprintOrigin.y);
                northAccess.direction = kRoadDirectionNorth;
                northAccess.modeMask = modeMask;
                lotAsset.accessDefinitions.push_back(northAccess);

                LotAccessDefinition southAccess;
                southAccess.localTile = Int2(tileX, lotAsset.footprintOrigin.y + lotAsset.footprintHeight - 1);
                southAccess.direction = kRoadDirectionSouth;
                southAccess.modeMask = modeMask;
                lotAsset.accessDefinitions.push_back(southAccess);
            }

            int tileY = lotAsset.footprintOrigin.y;
            for (; tileY < lotAsset.footprintOrigin.y + lotAsset.footprintHeight; ++tileY) {
                LotAccessDefinition westAccess;
                westAccess.localTile = Int2(lotAsset.footprintOrigin.x, tileY);
                westAccess.direction = kRoadDirectionWest;
                westAccess.modeMask = modeMask;
                lotAsset.accessDefinitions.push_back(westAccess);

                LotAccessDefinition eastAccess;
                eastAccess.localTile = Int2(lotAsset.footprintOrigin.x + lotAsset.footprintWidth - 1, tileY);
                eastAccess.direction = kRoadDirectionEast;
                eastAccess.modeMask = modeMask;
                lotAsset.accessDefinitions.push_back(eastAccess);
            }
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

    if (hasFootprint && (lotAsset.footprintWidth <= 0 || lotAsset.footprintHeight <= 0)) {
        throw std::runtime_error("Lot XML footprint dimensions must be positive: " + filePath);
    }

    return lotAsset;
}

// Verifies that a lot references real modules and has a valid footprint.
void ValidateLotAsset(LotAsset& lotAsset, const std::vector<LotModule>& modules) {
    std::set<std::string> moduleIds;
    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules.size(); ++moduleIndex) {
        moduleIds.insert(modules[moduleIndex].id);
    }

    std::vector<Int2> moduleTiles;
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
                    moduleTiles.push_back(Int2(placement.localOrigin.x + tileX, placement.localOrigin.y + tileY));
                }
            }

            break;
        }
    }

    if (moduleTiles.empty()) {
        throw std::runtime_error("Lot asset '" + lotAsset.id + "' does not occupy any module tiles.");
    }

    if (lotAsset.footprintWidth <= 0 || lotAsset.footprintHeight <= 0) {
        int minX = moduleTiles[0].x;
        int minY = moduleTiles[0].y;
        int maxX = moduleTiles[0].x;
        int maxY = moduleTiles[0].y;
        std::size_t tileIndex = 1;
        for (; tileIndex < moduleTiles.size(); ++tileIndex) {
            minX = std::min(minX, moduleTiles[tileIndex].x);
            minY = std::min(minY, moduleTiles[tileIndex].y);
            maxX = std::max(maxX, moduleTiles[tileIndex].x);
            maxY = std::max(maxY, moduleTiles[tileIndex].y);
        }

        lotAsset.footprintOrigin = Int2(minX, minY);
        lotAsset.footprintWidth = maxX - minX + 1;
        lotAsset.footprintHeight = maxY - minY + 1;
    }

    const int footprintMaxX = lotAsset.footprintOrigin.x + lotAsset.footprintWidth;
    const int footprintMaxY = lotAsset.footprintOrigin.y + lotAsset.footprintHeight;
    if (lotAsset.anchor.x < lotAsset.footprintOrigin.x || lotAsset.anchor.x >= footprintMaxX ||
        lotAsset.anchor.y < lotAsset.footprintOrigin.y || lotAsset.anchor.y >= footprintMaxY) {
        throw std::runtime_error("Lot asset '" + lotAsset.id + "' anchor must be inside its footprint.");
    }

    std::size_t tileIndex = 0;
    for (; tileIndex < moduleTiles.size(); ++tileIndex) {
        if (moduleTiles[tileIndex].x < lotAsset.footprintOrigin.x || moduleTiles[tileIndex].x >= footprintMaxX ||
            moduleTiles[tileIndex].y < lotAsset.footprintOrigin.y || moduleTiles[tileIndex].y >= footprintMaxY) {
            throw std::runtime_error("Lot asset '" + lotAsset.id + "' has an initial module outside its footprint.");
        }
    }

    for (placementIndex = 0; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        lotAsset.initialModules[placementIndex].localOrigin.x -= lotAsset.anchor.x;
        lotAsset.initialModules[placementIndex].localOrigin.y -= lotAsset.anchor.y;
    }

    std::size_t accessIndex = 0;
    for (; accessIndex < lotAsset.accessDefinitions.size(); ++accessIndex) {
        LotAccessDefinition& accessDefinition = lotAsset.accessDefinitions[accessIndex];
        if (accessDefinition.localTile.x < lotAsset.footprintOrigin.x || accessDefinition.localTile.x >= footprintMaxX ||
            accessDefinition.localTile.y < lotAsset.footprintOrigin.y || accessDefinition.localTile.y >= footprintMaxY) {
            throw std::runtime_error("Lot asset '" + lotAsset.id + "' has an access connection outside its footprint.");
        }

        const int exteriorX = accessDefinition.localTile.x + RoadDirectionDeltaX(accessDefinition.direction);
        const int exteriorY = accessDefinition.localTile.y + RoadDirectionDeltaY(accessDefinition.direction);
        if (exteriorX >= lotAsset.footprintOrigin.x && exteriorX < footprintMaxX &&
            exteriorY >= lotAsset.footprintOrigin.y && exteriorY < footprintMaxY) {
            throw std::runtime_error("Lot asset '" + lotAsset.id + "' has an access connection that points inside the footprint.");
        }

        accessDefinition.localTile.x -= lotAsset.anchor.x;
        accessDefinition.localTile.y -= lotAsset.anchor.y;
    }

    lotAsset.footprintOrigin.x -= lotAsset.anchor.x;
    lotAsset.footprintOrigin.y -= lotAsset.anchor.y;
    lotAsset.renderOrigin.x -= lotAsset.anchor.x;
    lotAsset.renderOrigin.y -= lotAsset.anchor.y;
    lotAsset.anchor = Int2(0, 0);
}

TransportCongestionCurve LoadCongestionCurve(const std::string& filePath) {
    const std::vector<std::string> tokens = ExtractTagTokens(ReadTextFile(filePath));
    if (tokens.empty()) {
        throw std::runtime_error("Empty congestion XML: " + filePath);
    }

    const ParsedTag rootTag = ParseTag(tokens[0]);
    if (rootTag.name != "congestion" || rootTag.isClosing) {
        throw std::runtime_error("Congestion XML must start with <congestion>: " + filePath);
    }

    TransportCongestionCurve congestionCurve;
    congestionCurve.points.clear();

    std::size_t tokenIndex = 1;
    for (; tokenIndex < tokens.size(); ++tokenIndex) {
        const ParsedTag tag = ParseTag(tokens[tokenIndex]);
        if (tag.isClosing) {
            if (tag.name == "congestion") {
                break;
            }

            throw std::runtime_error("Unexpected closing tag in congestion XML: " + filePath);
        }

        if (tag.name == "point" && tag.isSelfClosing) {
            const float utilization = ParseRequiredFloat(tag.attributes, "utilization");
            const float speedMultiplier = ParseRequiredFloat(tag.attributes, "speedMultiplier");
            if (utilization < 0.0f) {
                throw std::runtime_error("Congestion utilization must be non-negative in " + filePath);
            }
            if (speedMultiplier <= 0.0f) {
                throw std::runtime_error("Congestion speedMultiplier must be positive in " + filePath);
            }

            congestionCurve.points.push_back(TransportCongestionPoint(utilization, speedMultiplier));
            continue;
        }

        throw std::runtime_error("Unsupported congestion tag: <" + tag.name + "> in " + filePath);
    }

    if (congestionCurve.points.empty()) {
        throw std::runtime_error("Congestion XML must contain at least one <point>: " + filePath);
    }

    std::sort(congestionCurve.points.begin(), congestionCurve.points.end(), [](const TransportCongestionPoint& left, const TransportCongestionPoint& right) {
        return left.utilization < right.utilization;
    });

    std::size_t pointIndex = 1;
    for (; pointIndex < congestionCurve.points.size(); ++pointIndex) {
        if (std::fabs(congestionCurve.points[pointIndex].utilization - congestionCurve.points[pointIndex - 1u].utilization) <= 0.0001f) {
            throw std::runtime_error("Congestion XML has duplicate utilization points: " + filePath);
        }
    }

    return congestionCurve;
}

void ApplyRoadLaneCapacity(RoadLaneCapacityConfig& config, const std::string& typeText, int capacity, bool& seenSlow, bool& seenMedium, bool& seenFast, bool& seenPedestrian, const std::string& filePath) {
    if (capacity <= 0) {
        throw std::runtime_error("Road lane capacity must be positive in " + filePath);
    }

    const std::string type = ToLowerAscii(Trim(typeText));
    if (type == "slow") {
        if (seenSlow) {
            throw std::runtime_error("Duplicate slow road lane capacity in " + filePath);
        }
        config.slow = capacity;
        seenSlow = true;
        return;
    }
    if (type == "medium") {
        if (seenMedium) {
            throw std::runtime_error("Duplicate medium road lane capacity in " + filePath);
        }
        config.medium = capacity;
        seenMedium = true;
        return;
    }
    if (type == "fast") {
        if (seenFast) {
            throw std::runtime_error("Duplicate fast road lane capacity in " + filePath);
        }
        config.fast = capacity;
        seenFast = true;
        return;
    }
    if (type == "pedestrian" || type == "sidewalk") {
        if (seenPedestrian) {
            throw std::runtime_error("Duplicate pedestrian road lane capacity in " + filePath);
        }
        config.pedestrian = capacity;
        seenPedestrian = true;
        return;
    }

    throw std::runtime_error("Unknown road lane capacity type '" + typeText + "' in " + filePath);
}

RoadLaneCapacityConfig LoadRoadLaneCapacities(const std::string& filePath) {
    const std::vector<std::string> tokens = ExtractTagTokens(ReadTextFile(filePath));
    if (tokens.empty()) {
        throw std::runtime_error("Empty road lane capacity XML: " + filePath);
    }

    const ParsedTag rootTag = ParseTag(tokens[0]);
    if (rootTag.name != "roadLaneCapacities" || rootTag.isClosing) {
        throw std::runtime_error("Road lane capacity XML must start with <roadLaneCapacities>: " + filePath);
    }

    RoadLaneCapacityConfig config;
    bool seenSlow = false;
    bool seenMedium = false;
    bool seenFast = false;
    bool seenPedestrian = false;

    std::size_t tokenIndex = 1;
    for (; tokenIndex < tokens.size(); ++tokenIndex) {
        const ParsedTag tag = ParseTag(tokens[tokenIndex]);
        if (tag.isClosing) {
            if (tag.name == "roadLaneCapacities") {
                break;
            }

            throw std::runtime_error("Unexpected closing tag in road lane capacity XML: " + filePath);
        }

        if (tag.name == "lane" && tag.isSelfClosing) {
            ApplyRoadLaneCapacity(
                config,
                GetRequiredAttribute(tag.attributes, "type"),
                ParseRequiredInt(tag.attributes, "capacity"),
                seenSlow,
                seenMedium,
                seenFast,
                seenPedestrian,
                filePath);
            continue;
        }

        throw std::runtime_error("Unsupported road lane capacity tag: <" + tag.name + "> in " + filePath);
    }

    if (!seenSlow || !seenMedium || !seenFast || !seenPedestrian) {
        throw std::runtime_error("Road lane capacity XML must define slow, medium, fast, and pedestrian capacities in " + filePath);
    }

    return config;
}

std::vector<float> LoadInitialDemands(const std::string& filePath, const CityParameterRegistry& parameterRegistry) {
    std::vector<float> demands(parameterRegistry.count(), 0.0f);
    const std::vector<std::string> tokens = ExtractTagTokens(ReadTextFile(filePath));
    if (tokens.empty()) {
        throw std::runtime_error("Empty initial demands XML: " + filePath);
    }

    const ParsedTag rootTag = ParseTag(tokens[0]);
    if (rootTag.name != "initialDemands" || rootTag.isClosing) {
        throw std::runtime_error("Initial demands XML must start with <initialDemands>: " + filePath);
    }

    std::size_t tokenIndex = 1;
    for (; tokenIndex < tokens.size(); ++tokenIndex) {
        const ParsedTag tag = ParseTag(tokens[tokenIndex]);
        if (tag.isClosing) {
            if (tag.name == "initialDemands") {
                break;
            }

            throw std::runtime_error("Unexpected closing tag in initial demands XML: " + filePath);
        }

        if (tag.name == "demand" && tag.isSelfClosing) {
            const std::string parameterId = GetRequiredAttribute(tag.attributes, "id");
            const int resolvedParameterId = parameterRegistry.parameterId(parameterId);
            if (resolvedParameterId < 0) {
                throw std::runtime_error("Initial demand references unknown city parameter '" + parameterId + "' in " + filePath);
            }

            demands[static_cast<std::size_t>(resolvedParameterId)] = ParseRequiredFloat(tag.attributes, "amount");
            continue;
        }

        throw std::runtime_error("Unsupported initial demands tag: <" + tag.name + "> in " + filePath);
    }

    return demands;
}

const RciGrowthRule* FindGrowthRule(const std::vector<RciGrowthRule>& growthRules, std::uint16_t zoningType) {
    std::size_t ruleIndex = 0;
    for (; ruleIndex < growthRules.size(); ++ruleIndex) {
        if (growthRules[ruleIndex].zoningType == zoningType) {
            return &growthRules[ruleIndex];
        }
    }

    return 0;
}

const RciDesirabilityRule* FindDesirabilityRule(const std::vector<RciDesirabilityRule>& desirabilityRules, const std::string& rciTypeId) {
    std::size_t ruleIndex = 0;
    for (; ruleIndex < desirabilityRules.size(); ++ruleIndex) {
        if (desirabilityRules[ruleIndex].rciTypeId == rciTypeId) {
            return &desirabilityRules[ruleIndex];
        }
    }

    return 0;
}

void ValidateRciGrowthRule(const RciGrowthRule& rule, const std::string& filePath) {
    if (rule.zoningType == TileZoningNone) {
        throw std::runtime_error("RCI growth rule has no zoning type in " + filePath);
    }
    if (rule.desirabilityThreshold < kRciDesirabilityDisplayMinimum ||
        rule.desirabilityThreshold > kRciDesirabilityDisplayCap) {
        throw std::runtime_error(
            "RCI growth desirabilityThreshold must be between " +
            std::to_string(kRciDesirabilityDisplayMinimum) +
            " and " +
            std::to_string(kRciDesirabilityDisplayCap) +
            " in " + filePath);
    }
    if (rule.densityPoints.empty()) {
        throw std::runtime_error("RCI growth rule is missing maxDensityPerTile entries in " + filePath);
    }

    int previousPopulation = -1;
    std::size_t pointIndex = 0;
    for (; pointIndex < rule.densityPoints.size(); ++pointIndex) {
        const RciDensityPoint& point = rule.densityPoints[pointIndex];
        if (point.population < 0) {
            throw std::runtime_error("RCI maxDensityPerTile population must be non-negative in " + filePath);
        }
        if (point.maxDensityPerTile <= 0.0f) {
            throw std::runtime_error("RCI maxDensityPerTile value must be positive in " + filePath);
        }
        if (point.population <= previousPopulation) {
            throw std::runtime_error("RCI maxDensityPerTile population entries must be unique and increasing in " + filePath);
        }
        previousPopulation = point.population;
    }
}

void ValidateRciDesirabilityRule(const RciDesirabilityRule& rule, const std::string& filePath) {
    if (rule.rciTypeId.empty()) {
        throw std::runtime_error("RCI desirability rule has no RCI type in " + filePath);
    }
    if (rule.baseline < kRciDesirabilityDisplayMinimum ||
        rule.baseline > kRciDesirabilityDisplayCap) {
        throw std::runtime_error(
            "RCI desirability baseline must be between " +
            std::to_string(kRciDesirabilityDisplayMinimum) +
            " and " +
            std::to_string(kRciDesirabilityDisplayCap) +
            " in " + filePath);
    }
    if (rule.sensitivities.empty()) {
        throw std::runtime_error("RCI desirability rule is missing sensitivity entries in " + filePath);
    }

    std::size_t sensitivityIndex = 0;
    for (; sensitivityIndex < rule.sensitivities.size(); ++sensitivityIndex) {
        const RciDesirabilitySensitivity& sensitivity = rule.sensitivities[sensitivityIndex];
        if (sensitivity.normalizer <= 0) {
            throw std::runtime_error("RCI desirability sensitivity normalizer must be positive in " + filePath);
        }
        if (sensitivity.points.empty()) {
            throw std::runtime_error("RCI desirability sensitivity is missing point entries in " + filePath);
        }

        std::size_t pointIndex = 0;
        for (; pointIndex < sensitivity.points.size(); ++pointIndex) {
            if (pointIndex > 0 && sensitivity.points[pointIndex].value <= sensitivity.points[pointIndex - 1u].value) {
                throw std::runtime_error("RCI desirability point values must be unique and increasing in " + filePath);
            }
        }
    }
}

void LoadRciConstructorSettings(
    const std::string& filePath,
    int& attemptsPerTick,
    float& overbuildMultiplier,
    int& baselineLandValue,
    std::vector<RciGrowthRule>& growthRules,
    std::vector<RciDesirabilityRule>& desirabilityRules) {
    const std::vector<std::string> tokens = ExtractTagTokens(ReadTextFile(filePath));
    if (tokens.empty()) {
        throw std::runtime_error("Empty RCI XML: " + filePath);
    }

    const ParsedTag rootTag = ParseTag(tokens[0]);
    if (rootTag.name != "rciTools" || rootTag.isClosing) {
        throw std::runtime_error("RCI XML must start with <rciTools>: " + filePath);
    }

    attemptsPerTick = ParseOptionalInt(rootTag.attributes, "constructorAttemptsPerTick", attemptsPerTick);
    attemptsPerTick = ParseOptionalInt(rootTag.attributes, "constructorRetries", attemptsPerTick);
    overbuildMultiplier = ParseOptionalRatio(rootTag.attributes, "constructorOverbuildPercent", overbuildMultiplier);
    overbuildMultiplier = ParseOptionalRatio(rootTag.attributes, "constructorOverbuildMultiplier", overbuildMultiplier);
    baselineLandValue = ParseOptionalInt(rootTag.attributes, "baselineLandValue", baselineLandValue);
    baselineLandValue = ParseOptionalInt(rootTag.attributes, "defaultLandValue", baselineLandValue);
    growthRules.clear();
    desirabilityRules.clear();

    RciGrowthRule* activeGrowthRule = 0;
    RciDesirabilityRule* activeDesirabilityRule = 0;
    RciDesirabilitySensitivity* activeSensitivity = 0;
    std::size_t tokenIndex = 1;
    for (; tokenIndex < tokens.size(); ++tokenIndex) {
        const ParsedTag tag = ParseTag(tokens[tokenIndex]);
        if (tag.isClosing) {
            if (tag.name == "rciTools") {
                break;
            }

            if (tag.name == "rciGrowth" || tag.name == "zone") {
                activeGrowthRule = 0;
                continue;
            }

            if (tag.name == "rciDesirability") {
                activeDesirabilityRule = 0;
                activeSensitivity = 0;
                continue;
            }

            if (tag.name == "sensitivity") {
                activeSensitivity = 0;
                continue;
            }

            continue;
        }

        if (tag.name == "constructor" && tag.isSelfClosing) {
            attemptsPerTick = ParseOptionalInt(tag.attributes, "attemptsPerTick", attemptsPerTick);
            attemptsPerTick = ParseOptionalInt(tag.attributes, "retries", attemptsPerTick);
            overbuildMultiplier = ParseOptionalRatio(tag.attributes, "overbuildPercent", overbuildMultiplier);
            overbuildMultiplier = ParseOptionalRatio(tag.attributes, "overbuildMultiplier", overbuildMultiplier);
            continue;
        }

        if (tag.name == "landValue" && tag.isSelfClosing) {
            baselineLandValue = ParseOptionalInt(tag.attributes, "baseline", baselineLandValue);
            baselineLandValue = ParseOptionalInt(tag.attributes, "baselineLandValue", baselineLandValue);
            baselineLandValue = ParseOptionalInt(tag.attributes, "default", baselineLandValue);
            continue;
        }

        if (tag.name == "rciGrowth" && !tag.isClosing) {
            RciGrowthRule growthRule;
            growthRule.zoningType = ParseZoningTypeName(GetRequiredAttribute(tag.attributes, "zoningType"));
            growthRule.desirabilityThreshold = ParseRequiredInt(tag.attributes, "desirabilityThreshold");
            if (FindGrowthRule(growthRules, growthRule.zoningType) != 0) {
                throw std::runtime_error("Duplicate RCI growth rule in " + filePath);
            }
            growthRules.push_back(growthRule);
            activeGrowthRule = &growthRules.back();
            if (tag.isSelfClosing) {
                activeGrowthRule = 0;
            }
            continue;
        }

        if (tag.name == "zone" && !tag.isClosing) {
            const std::string threshold = GetOptionalAttribute(tag.attributes, "desirabilityThreshold", "");
            if (!threshold.empty()) {
                RciGrowthRule growthRule;
                growthRule.zoningType = ParseZoningTypeName(GetRequiredAttribute(tag.attributes, "zoningType"));
                growthRule.desirabilityThreshold = std::stoi(threshold);
                if (FindGrowthRule(growthRules, growthRule.zoningType) != 0) {
                    throw std::runtime_error("Duplicate RCI zone growth rule in " + filePath);
                }
                growthRules.push_back(growthRule);
                activeGrowthRule = &growthRules.back();
                if (tag.isSelfClosing) {
                    activeGrowthRule = 0;
                }
            }
            continue;
        }

        if (tag.name == "rciDesirability" && !tag.isClosing) {
            RciDesirabilityRule desirabilityRule;
            const std::string explicitRciTypeId = GetOptionalAttribute(tag.attributes, "rciType", GetOptionalAttribute(tag.attributes, "rciTypeId", ""));
            if (!explicitRciTypeId.empty()) {
                desirabilityRule.rciTypeId = explicitRciTypeId;
                desirabilityRule.zoningType = ParseZoningTypeName(GetOptionalAttribute(tag.attributes, "zoningType", ""));
            } else {
                desirabilityRule.zoningType = ParseZoningTypeName(GetRequiredAttribute(tag.attributes, "zoningType"));
                desirabilityRule.rciTypeId = DefaultRciTypeIdForZoningType(desirabilityRule.zoningType);
            }
            desirabilityRule.baseline = ParseOptionalInt(tag.attributes, "baseline", desirabilityRule.baseline);
            if (FindDesirabilityRule(desirabilityRules, desirabilityRule.rciTypeId) != 0) {
                throw std::runtime_error("Duplicate RCI desirability rule in " + filePath);
            }
            desirabilityRules.push_back(desirabilityRule);
            activeDesirabilityRule = &desirabilityRules.back();
            activeSensitivity = 0;
            if (tag.isSelfClosing) {
                activeDesirabilityRule = 0;
            }
            continue;
        }

        if (tag.name == "sensitivity" && !tag.isClosing && activeDesirabilityRule != 0) {
            RciDesirabilitySensitivity sensitivity;
            sensitivity.field = ParseRciDesirabilityFieldName(GetRequiredAttribute(tag.attributes, "field"));
            sensitivity.normalizer = ParseOptionalInt(tag.attributes, "normalizer", sensitivity.normalizer);
            sensitivity.normalizer = ParseOptionalInt(tag.attributes, "scale", sensitivity.normalizer);
            activeDesirabilityRule->sensitivities.push_back(sensitivity);
            activeSensitivity = &activeDesirabilityRule->sensitivities.back();
            if (tag.isSelfClosing) {
                activeSensitivity = 0;
            }
            continue;
        }

        if ((tag.name == "maxDensityPerTile" || tag.name == "density") && tag.isSelfClosing && activeGrowthRule != 0) {
            RciDensityPoint densityPoint;
            densityPoint.population = ParseRequiredInt(tag.attributes, "population");
            densityPoint.maxDensityPerTile = ParseOptionalFloat(tag.attributes, "value", -1.0f);
            densityPoint.maxDensityPerTile = ParseOptionalFloat(tag.attributes, "perTile", densityPoint.maxDensityPerTile);
            densityPoint.maxDensityPerTile = ParseOptionalFloat(tag.attributes, "density", densityPoint.maxDensityPerTile);
            if (densityPoint.maxDensityPerTile <= 0.0f) {
                throw std::runtime_error("RCI maxDensityPerTile entry must define value, perTile, or density in " + filePath);
            }
            activeGrowthRule->densityPoints.push_back(densityPoint);
            continue;
        }

        if (tag.name == "point" && tag.isSelfClosing && activeSensitivity != 0) {
            RciDesirabilityPoint point;
            point.value = ParseRequiredFloat(tag.attributes, "value");
            point.desirabilityDelta = ParseRequiredInt(tag.attributes, "desirabilityDelta");
            activeSensitivity->points.push_back(point);
            continue;
        }
    }

    std::size_t ruleIndex = 0;
    for (; ruleIndex < growthRules.size(); ++ruleIndex) {
        std::sort(growthRules[ruleIndex].densityPoints.begin(), growthRules[ruleIndex].densityPoints.end(), [](const RciDensityPoint& left, const RciDensityPoint& right) {
            return left.population < right.population;
        });
        ValidateRciGrowthRule(growthRules[ruleIndex], filePath);
    }

    for (ruleIndex = 0; ruleIndex < desirabilityRules.size(); ++ruleIndex) {
        std::size_t sensitivityIndex = 0;
        for (; sensitivityIndex < desirabilityRules[ruleIndex].sensitivities.size(); ++sensitivityIndex) {
            std::sort(
                desirabilityRules[ruleIndex].sensitivities[sensitivityIndex].points.begin(),
                desirabilityRules[ruleIndex].sensitivities[sensitivityIndex].points.end(),
                [](const RciDesirabilityPoint& left, const RciDesirabilityPoint& right) {
                    return left.value < right.value;
                });
        }
        ValidateRciDesirabilityRule(desirabilityRules[ruleIndex], filePath);
    }
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
bool LoadGameAssets(const std::string& dataDirectory, const CityParameterRegistry& parameterRegistry, LoadedGameAssets& assets, std::string& errorMessage) {
    CrashScope crashScope("LoadGameAssets");
    assets.modules.clear();
    assets.lots.clear();
    assets.rciGrowthRules.clear();
    assets.rciDesirabilityRules.clear();
    assets.initialDemands.assign(parameterRegistry.count(), 0.0f);
    assets.congestionCurve = TransportCongestionCurve();
    assets.roadLaneCapacities = RoadLaneCapacityConfig();
    assets.rciConstructorAttemptsPerTick = 5;
    assets.rciConstructorOverbuildMultiplier = 1.2f;
    assets.rciBaselineLandValue = 0;

    try {
        const std::string modulesDirectory = dataDirectory + "\\Modules";
        const std::string lotsDirectory = dataDirectory + "\\Lots";
        const std::string congestionPath = dataDirectory + "\\TransportNetwork\\congestion.xml";
        const std::string roadLaneCapacitiesPath = dataDirectory + "\\TransportNetwork\\lane_capacities.xml";
        const std::string initialDemandsPath = dataDirectory + "\\RCI\\initial_demands.xml";
        const std::string rciToolsPath = dataDirectory + "\\RCI\\rci_tools.xml";

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
            LotModule module = LoadModuleAsset(modulesDirectory + "\\" + moduleFiles[fileIndex], moduleFiles[fileIndex], parameterRegistry);
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

        if (FileExists(congestionPath)) {
            assets.congestionCurve = LoadCongestionCurve(congestionPath);
        }
        if (FileExists(roadLaneCapacitiesPath)) {
            assets.roadLaneCapacities = LoadRoadLaneCapacities(roadLaneCapacitiesPath);
        }
        if (FileExists(initialDemandsPath)) {
            assets.initialDemands = LoadInitialDemands(initialDemandsPath, parameterRegistry);
        }
        if (FileExists(rciToolsPath)) {
            LoadRciConstructorSettings(
                rciToolsPath,
                assets.rciConstructorAttemptsPerTick,
                assets.rciConstructorOverbuildMultiplier,
                assets.rciBaselineLandValue,
                assets.rciGrowthRules,
                assets.rciDesirabilityRules);
        }

        std::set<std::uint16_t> constructorZoningTypes;
        std::set<std::string> constructorRciTypeIds;
        std::size_t lotIndex = 0;
        for (; lotIndex < assets.lots.size(); ++lotIndex) {
            if (assets.lots[lotIndex].zoningType != TileZoningNone) {
                constructorZoningTypes.insert(assets.lots[lotIndex].zoningType);
                constructorRciTypeIds.insert(assets.lots[lotIndex].rciTypeId);
            }
        }
        std::set<std::uint16_t>::const_iterator zoningTypeIterator = constructorZoningTypes.begin();
        for (; zoningTypeIterator != constructorZoningTypes.end(); ++zoningTypeIterator) {
            if (FindGrowthRule(assets.rciGrowthRules, *zoningTypeIterator) == 0) {
                throw std::runtime_error("RCI constructor lot assets require a matching RCI zone density rule in " + rciToolsPath);
            }
        }
        std::set<std::string>::const_iterator rciTypeIterator = constructorRciTypeIds.begin();
        for (; rciTypeIterator != constructorRciTypeIds.end(); ++rciTypeIterator) {
            if (FindDesirabilityRule(assets.rciDesirabilityRules, *rciTypeIterator) == 0) {
                throw std::runtime_error("RCI constructor lot assets require a matching rciDesirability rule for " + *rciTypeIterator + " in " + rciToolsPath);
            }
        }

        assets.rciConstructorAttemptsPerTick = std::max(1, assets.rciConstructorAttemptsPerTick);
        assets.rciConstructorOverbuildMultiplier = std::max(0.0f, assets.rciConstructorOverbuildMultiplier);
        assets.rciBaselineLandValue = std::max(0, std::min(assets.rciBaselineLandValue, kLandValueDisplayCap));
    } catch (const std::exception& error) {
        LogException("LoadGameAssets", error);
        errorMessage = error.what();
        assets.modules.clear();
        assets.lots.clear();
        assets.rciGrowthRules.clear();
        assets.rciDesirabilityRules.clear();
        assets.initialDemands.clear();
        assets.congestionCurve = TransportCongestionCurve();
        assets.roadLaneCapacities = RoadLaneCapacityConfig();
        assets.rciBaselineLandValue = 0;
        return false;
    }

    errorMessage.clear();
    return true;
}
