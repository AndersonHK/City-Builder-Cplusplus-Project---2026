#include "Localization.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
std::string ReadUtf8File(const std::string& filePath) {
    std::ifstream stream(filePath.c_str(), std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("Unable to open locale file: " + filePath);
    }

    std::ostringstream builder;
    builder << stream.rdbuf();
    return builder.str();
}

void SkipWhitespace(const std::string& text, std::size_t& index) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
}

void ExpectCharacter(const std::string& text, std::size_t& index, char expected) {
    SkipWhitespace(text, index);
    if (index >= text.size() || text[index] != expected) {
        std::ostringstream error;
        error << "Locale JSON expected '" << expected << "' at byte " << index;
        throw std::runtime_error(error.str());
    }
    ++index;
}

int HexDigitValue(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    return -1;
}

void AppendUtf8Codepoint(std::string& output, unsigned int codepoint) {
    if (codepoint <= 0x7Fu) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        output.push_back(static_cast<char>(0xC0u | ((codepoint >> 6) & 0x1Fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        output.push_back(static_cast<char>(0xE0u | ((codepoint >> 12) & 0x0Fu)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

std::string ParseJsonString(const std::string& text, std::size_t& index) {
    SkipWhitespace(text, index);
    if (index >= text.size() || text[index] != '"') {
        std::ostringstream error;
        error << "Locale JSON expected string at byte " << index;
        throw std::runtime_error(error.str());
    }

    ++index;
    std::string value;
    while (index < text.size()) {
        const char character = text[index++];
        if (character == '"') {
            return value;
        }

        if (character != '\\') {
            value.push_back(character);
            continue;
        }

        if (index >= text.size()) {
            throw std::runtime_error("Locale JSON has unterminated escape sequence.");
        }

        const char escaped = text[index++];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;

            case 'b':
                value.push_back('\b');
                break;

            case 'f':
                value.push_back('\f');
                break;

            case 'n':
                value.push_back('\n');
                break;

            case 'r':
                value.push_back('\r');
                break;

            case 't':
                value.push_back('\t');
                break;

            case 'u': {
                if (index + 4u > text.size()) {
                    throw std::runtime_error("Locale JSON has incomplete unicode escape.");
                }

                unsigned int codepoint = 0u;
                int digitIndex = 0;
                for (; digitIndex < 4; ++digitIndex) {
                    const int digitValue = HexDigitValue(text[index++]);
                    if (digitValue < 0) {
                        throw std::runtime_error("Locale JSON has invalid unicode escape.");
                    }
                    codepoint = (codepoint << 4) | static_cast<unsigned int>(digitValue);
                }
                AppendUtf8Codepoint(value, codepoint);
                break;
            }

            default:
                throw std::runtime_error("Locale JSON has unsupported escape sequence.");
        }
    }

    throw std::runtime_error("Locale JSON has unterminated string.");
}

void LoadFlatStringObject(const std::string& text, LocalizationCatalog& catalog) {
    std::size_t index = 0;
    if (text.size() >= 3u &&
        static_cast<unsigned char>(text[0]) == 0xEFu &&
        static_cast<unsigned char>(text[1]) == 0xBBu &&
        static_cast<unsigned char>(text[2]) == 0xBFu) {
        index = 3u;
    }

    ExpectCharacter(text, index, '{');
    SkipWhitespace(text, index);
    if (index < text.size() && text[index] == '}') {
        ++index;
        return;
    }

    while (index < text.size()) {
        const std::string key = ParseJsonString(text, index);
        if (key.empty()) {
            throw std::runtime_error("Locale JSON string id cannot be empty.");
        }

        ExpectCharacter(text, index, ':');
        const std::string value = ParseJsonString(text, index);
        if (catalog.findStringId(key) != LocalizationCatalog::kInvalidStringId) {
            throw std::runtime_error("Duplicate locale string id: " + key);
        }

        catalog.appendLoadedString(key, value);

        SkipWhitespace(text, index);
        if (index < text.size() && text[index] == ',') {
            ++index;
            continue;
        }
        if (index < text.size() && text[index] == '}') {
            ++index;
            break;
        }

        std::ostringstream error;
        error << "Locale JSON expected ',' or '}' at byte " << index;
        throw std::runtime_error(error.str());
    }

    SkipWhitespace(text, index);
    if (index != text.size()) {
        std::ostringstream error;
        error << "Locale JSON has trailing content at byte " << index;
        throw std::runtime_error(error.str());
    }
}
}

LocalizationCatalog::LocalizationCatalog() {
}

bool LocalizationCatalog::loadFromJsonFile(const std::string& filePath, std::string& errorMessage) {
    clear();
    try {
        LoadFlatStringObject(ReadUtf8File(filePath), *this);
    } catch (const std::exception& error) {
        clear();
        errorMessage = error.what();
        return false;
    }

    errorMessage.clear();
    return true;
}

void LocalizationCatalog::clear() {
    keys_.clear();
    values_.clear();
    keyToId_.clear();
}

void LocalizationCatalog::appendLoadedString(const std::string& stringKey, const std::string& value) {
    if (findStringId(stringKey) != kInvalidStringId) {
        throw std::runtime_error("Duplicate locale string id: " + stringKey);
    }

    const int nextId = static_cast<int>(keys_.size());
    keys_.push_back(stringKey);
    values_.push_back(value);
    keyToId_[stringKey] = nextId;
}

int LocalizationCatalog::findStringId(const std::string& stringKey) const {
    const std::unordered_map<std::string, int>::const_iterator iterator = keyToId_.find(stringKey);
    return iterator == keyToId_.end() ? kInvalidStringId : iterator->second;
}

int LocalizationCatalog::requireStringId(const std::string& stringKey, const std::string& context) const {
    const int stringId = findStringId(stringKey);
    if (stringId != kInvalidStringId) {
        return stringId;
    }

    throw std::runtime_error("Missing locale string id '" + stringKey + "' for " + context);
}

const std::string& LocalizationCatalog::stringForId(int stringId) const {
    if (stringId < 0 || stringId >= static_cast<int>(values_.size())) {
        throw std::runtime_error("Invalid locale integer string id.");
    }

    return values_[static_cast<std::size_t>(stringId)];
}

const std::string& LocalizationCatalog::stringForKey(const std::string& stringKey, const std::string& context) const {
    return stringForId(requireStringId(stringKey, context));
}
