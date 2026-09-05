#pragma once

#include <cctype>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// Lightweight attribute helpers for the small, authored XML files used by tools
// and UI. These intentionally stay simple and fail back to caller defaults.
namespace SimpleXmlDetail {
inline bool IsAttributeBoundary(char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0 ||
        character == '<' ||
        character == '/';
}

inline std::string::size_type FindAttributeValueStart(const std::string& tag, const std::string& attributeName) {
    const std::string needle = attributeName + "=\"";
    std::string::size_type searchStart = 0u;
    while (true) {
        const std::string::size_type attributeStart = tag.find(needle, searchStart);
        if (attributeStart == std::string::npos) {
            return std::string::npos;
        }

        if (attributeStart == 0u || IsAttributeBoundary(tag[attributeStart - 1u])) {
            return attributeStart + needle.size();
        }

        searchStart = attributeStart + 1u;
    }
}

inline bool IsOnlyWhitespaceFrom(const char* value) {
    const char* cursor = value;
    while (cursor != 0 && *cursor != '\0') {
        if (std::isspace(static_cast<unsigned char>(*cursor)) == 0) {
            return false;
        }
        ++cursor;
    }

    return true;
}

inline bool TryParseInt(const std::string& value, int& parsedValue) {
    if (value.empty()) {
        return false;
    }

    char* parseEnd = 0;
    const long parsed = std::strtol(value.c_str(), &parseEnd, 10);
    if (parseEnd == value.c_str() || !IsOnlyWhitespaceFrom(parseEnd) || parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }

    parsedValue = static_cast<int>(parsed);
    return true;
}

inline bool TryParseFloat(const std::string& value, float& parsedValue) {
    if (value.empty()) {
        return false;
    }

    char* parseEnd = 0;
    const double parsed = std::strtod(value.c_str(), &parseEnd);
    if (parseEnd == value.c_str() || !IsOnlyWhitespaceFrom(parseEnd)) {
        return false;
    }

    parsedValue = static_cast<float>(parsed);
    return true;
}

inline std::string ToLowerAscii(std::string value) {
    for (std::string::size_type characterIndex = 0u; characterIndex < value.size(); ++characterIndex) {
        value[characterIndex] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[characterIndex])));
    }

    return value;
}
}

inline std::string XmlReadFileToString(const std::string& filePath) {
    std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
    if (!file) {
        return std::string();
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

inline std::string XmlAttributeValue(const std::string& tag, const std::string& attributeName, const std::string& fallback) {
    const std::string::size_type valueStart = SimpleXmlDetail::FindAttributeValueStart(tag, attributeName);
    if (valueStart == std::string::npos) {
        return fallback;
    }

    const std::string::size_type valueEnd = tag.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        return fallback;
    }

    return tag.substr(valueStart, valueEnd - valueStart);
}

inline bool XmlAttributeExists(const std::string& tag, const std::string& attributeName) {
    return SimpleXmlDetail::FindAttributeValueStart(tag, attributeName) != std::string::npos;
}

inline int XmlAttributeIntValue(const std::string& tag, const std::string& attributeName, int fallback) {
    const std::string value = XmlAttributeValue(tag, attributeName, std::string());
    int parsed = fallback;
    if (!SimpleXmlDetail::TryParseInt(value, parsed)) {
        return fallback;
    }

    return parsed;
}

inline int XmlAttributeIntValueAny(const std::string& tag, const std::string& primaryName, const std::string& alternateName, int fallback) {
    const std::string primaryValue = XmlAttributeValue(tag, primaryName, std::string());
    int parsed = fallback;
    if (SimpleXmlDetail::TryParseInt(primaryValue, parsed)) {
        return parsed;
    }

    return XmlAttributeIntValue(tag, alternateName, fallback);
}

inline float XmlAttributeFloatValue(const std::string& tag, const std::string& attributeName, float fallback) {
    const std::string value = XmlAttributeValue(tag, attributeName, std::string());
    float parsed = fallback;
    if (!SimpleXmlDetail::TryParseFloat(value, parsed)) {
        return fallback;
    }

    return parsed;
}

inline bool XmlAttributeBoolValue(const std::string& tag, const std::string& attributeName, bool fallback) {
    const std::string value = SimpleXmlDetail::ToLowerAscii(XmlAttributeValue(tag, attributeName, std::string()));
    if (value.empty()) {
        return fallback;
    }

    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }

    return fallback;
}
