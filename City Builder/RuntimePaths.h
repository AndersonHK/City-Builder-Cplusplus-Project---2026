#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

inline std::string RuntimeJoinPath(const std::string& basePath, const std::string& relativePath) {
    if (relativePath.empty()) {
        return basePath;
    }
    if (basePath.empty()) {
        return relativePath;
    }
    const char lastCharacter = basePath[basePath.size() - 1u];
    if (lastCharacter == '\\' || lastCharacter == '/') {
        return basePath + relativePath;
    }

    return basePath + "\\" + relativePath;
}

// Returns the directory that contains the currently running executable. Runtime
// data loads are based on the deployed binary, never the developer's source path.
inline std::string RuntimeExecutableDirectory() {
    char modulePath[MAX_PATH];
    const DWORD pathLength = GetModuleFileNameA(0, modulePath, MAX_PATH);
    if (pathLength == 0 || pathLength >= MAX_PATH) {
        return ".";
    }

    std::string fullPath(modulePath, modulePath + pathLength);
    const std::string::size_type separatorIndex = fullPath.find_last_of("\\/");
    if (separatorIndex == std::string::npos) {
        return ".";
    }

    return fullPath.substr(0, separatorIndex);
}

// Runtime assets are copied beside the executable under Data by the project
// post-build step.
inline std::string RuntimeDataDirectory() {
    return RuntimeJoinPath(RuntimeExecutableDirectory(), "Data");
}

// Build a path inside the deployed Data directory. Use this instead of assuming
// a source-tree layout or embedding an absolute developer-machine path.
inline std::string RuntimeDataPath(const std::string& relativePath) {
    return RuntimeJoinPath(RuntimeDataDirectory(), relativePath);
}

// Build a path beside the executable for non-Data runtime files, such as the
// legacy shader copied next to the binary.
inline std::string RuntimeExecutablePath(const std::string& relativePath) {
    return RuntimeJoinPath(RuntimeExecutableDirectory(), relativePath);
}
