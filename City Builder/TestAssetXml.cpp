#include "TestAssetXml.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <fstream>

void WriteTextAssetFile(const std::string& path, const std::string& text) {
    std::ofstream stream(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    stream << text;
}

std::string MakeTempAssetDirectory(const std::string& name) {
    char tempPath[MAX_PATH];
    const DWORD length = GetTempPathA(MAX_PATH, tempPath);
    std::string root(length == 0 ? "." : std::string(tempPath, tempPath + length));
    if (!root.empty() && root[root.size() - 1] != '\\' && root[root.size() - 1] != '/') {
        root += "\\";
    }

    root += name + "_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64());
    CreateDirectoryA(root.c_str(), 0);
    CreateDirectoryA((root + "\\Modules").c_str(), 0);
    CreateDirectoryA((root + "\\Lots").c_str(), 0);
    CreateDirectoryA((root + "\\RCI").c_str(), 0);
    CreateDirectoryA((root + "\\TransportNetwork").c_str(), 0);
    return root;
}

bool DirectoryExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
