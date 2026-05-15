#pragma once

#include <exception>
#include <string>

class CrashScope {
public:
    explicit CrashScope(const char* scopeName);
    ~CrashScope();

private:
    std::string previousScope_;
};

class ScopedCrashLogSuppression {
public:
    ScopedCrashLogSuppression();
    ~ScopedCrashLogSuppression();

    ScopedCrashLogSuppression(const ScopedCrashLogSuppression&) = delete;
    ScopedCrashLogSuppression& operator=(const ScopedCrashLogSuppression&) = delete;

private:
    bool active_;
};

void InitializeCrashLogger(const std::string& applicationName);
const std::string& CurrentCrashScope();
std::string CrashLogFilePath();
void LogInfo(const std::string& scope, const std::string& message);
void LogWarning(const std::string& scope, const std::string& message);
void LogError(const std::string& scope, const std::string& message);
void LogException(const std::string& scope, const std::exception& error);
int LogCrashAndShowWindow(const std::string& scope, const std::string& message);
int LogCrashAndShowWindow(const std::string& scope, const std::exception& error);
