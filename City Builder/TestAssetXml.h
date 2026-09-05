#pragma once

#include <string>

void WriteTextAssetFile(const std::string& path, const std::string& text);
std::string MakeTempAssetDirectory(const std::string& name);
bool DirectoryExists(const std::string& path);
