#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class LocalizationCatalog {
public:
    static const int kInvalidStringId = -1;

    LocalizationCatalog();

    bool loadFromJsonFile(const std::string& filePath, std::string& errorMessage);
    void clear();
    void appendLoadedString(const std::string& stringKey, const std::string& value);

    int findStringId(const std::string& stringKey) const;
    int requireStringId(const std::string& stringKey, const std::string& context) const;
    const std::string& stringForId(int stringId) const;
    const std::string& stringForKey(const std::string& stringKey, const std::string& context) const;

private:
    std::vector<std::string> keys_;
    std::vector<std::string> values_;
    std::unordered_map<std::string, int> keyToId_;
};
