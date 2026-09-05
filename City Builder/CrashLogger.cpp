#include "CrashLogger.h"

#include "RuntimePaths.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace {
std::mutex gCrashLogMutex;
std::string gApplicationName = "City Builder";
HWND gApplicationDialogOwner = 0;
int gCrashLogSuppressionDepth = 0;
thread_local std::string gCurrentCrashScope = "unknown";

std::string Timestamp() {
    SYSTEMTIME localTime;
    GetLocalTime(&localTime);

    std::ostringstream builder;
    builder
        << std::setfill('0')
        << std::setw(4) << localTime.wYear << "-"
        << std::setw(2) << localTime.wMonth << "-"
        << std::setw(2) << localTime.wDay << " "
        << std::setw(2) << localTime.wHour << ":"
        << std::setw(2) << localTime.wMinute << ":"
        << std::setw(2) << localTime.wSecond << "."
        << std::setw(3) << localTime.wMilliseconds;
    return builder.str();
}

std::string SeverityLine(const std::string& severity, const std::string& scope, const std::string& message) {
    std::ostringstream builder;
    builder << Timestamp() << " [" << severity << "] [" << scope << "] " << message;
    return builder.str();
}

void EnsureLogDirectory(const std::string& directory) {
    CreateDirectoryA(directory.c_str(), 0);
}

std::string LogDirectory() {
    const std::string dataDirectory = RuntimeDataDirectory();
    EnsureLogDirectory(dataDirectory);

    const std::string logsDirectory = dataDirectory + "\\Logs";
    EnsureLogDirectory(logsDirectory);
    return logsDirectory;
}

void WriteLine(const std::string& severity, const std::string& scope, const std::string& message) {
    const std::string line = SeverityLine(severity, scope, message);

    std::lock_guard<std::mutex> lock(gCrashLogMutex);
    if (gCrashLogSuppressionDepth > 0) {
        return;
    }

    std::cerr << line << std::endl;

    std::ofstream log(CrashLogFilePath().c_str(), std::ios::out | std::ios::app);
    if (log) {
        log << line << std::endl;
    }
}

HWND CurrentDialogOwner() {
    return gApplicationDialogOwner != 0 && IsWindow(gApplicationDialogOwner) ? gApplicationDialogOwner : 0;
}

int ShowApplicationDialog(const std::string& title, const std::string& message, UINT iconFlags) {
    HWND owner = CurrentDialogOwner();
    UINT flags = MB_OK | iconFlags;
    flags |= owner == 0 ? MB_TASKMODAL : MB_APPLMODAL;
    return MessageBoxA(owner, message.c_str(), title.c_str(), flags);
}

std::string ExceptionCodeString(DWORD exceptionCode) {
    std::ostringstream builder;
    builder << "0x" << std::hex << std::uppercase << exceptionCode;
    return builder.str();
}

LONG WINAPI UnhandledCrashFilter(EXCEPTION_POINTERS* exceptionPointers) {
    std::ostringstream message;
    message << "Unhandled structured exception";
    if (exceptionPointers != 0 && exceptionPointers->ExceptionRecord != 0) {
        message << " code=" << ExceptionCodeString(exceptionPointers->ExceptionRecord->ExceptionCode);
        message << " address=" << exceptionPointers->ExceptionRecord->ExceptionAddress;
    }

    LogCrashAndShowWindow(CurrentCrashScope(), message.str());
    return EXCEPTION_EXECUTE_HANDLER;
}

void TerminateCrashHandler() {
    const std::exception_ptr activeException = std::current_exception();
    if (activeException) {
        try {
            std::rethrow_exception(activeException);
        } catch (const std::exception& error) {
            LogCrashAndShowWindow(CurrentCrashScope(), error);
        } catch (...) {
            LogCrashAndShowWindow(CurrentCrashScope(), "Unhandled non-standard exception during terminate.");
        }
    } else {
        LogCrashAndShowWindow(CurrentCrashScope(), "std::terminate called without an active exception.");
    }

    std::_Exit(EXIT_FAILURE);
}

void SignalCrashHandler(int signalValue) {
    std::ostringstream message;
    message << "Process signal " << signalValue;
    LogCrashAndShowWindow(CurrentCrashScope(), message.str());
    std::_Exit(EXIT_FAILURE);
}
}

CrashScope::CrashScope(const char* scopeName)
    : previousScope_(gCurrentCrashScope) {
    gCurrentCrashScope = scopeName == 0 ? "unknown" : scopeName;
}

CrashScope::~CrashScope() {
    gCurrentCrashScope = previousScope_;
}

ScopedCrashLogSuppression::ScopedCrashLogSuppression()
    : active_(true) {
    std::lock_guard<std::mutex> lock(gCrashLogMutex);
    ++gCrashLogSuppressionDepth;
}

ScopedCrashLogSuppression::~ScopedCrashLogSuppression() {
    if (!active_) {
        return;
    }

    std::lock_guard<std::mutex> lock(gCrashLogMutex);
    if (gCrashLogSuppressionDepth > 0) {
        --gCrashLogSuppressionDepth;
    }
}

void InitializeCrashLogger(const std::string& applicationName) {
    gApplicationName = applicationName.empty() ? "City Builder" : applicationName;
    SetUnhandledExceptionFilter(UnhandledCrashFilter);
    std::set_terminate(TerminateCrashHandler);
    std::signal(SIGABRT, SignalCrashHandler);
    std::signal(SIGFPE, SignalCrashHandler);
    std::signal(SIGILL, SignalCrashHandler);
    std::signal(SIGSEGV, SignalCrashHandler);
    LogInfo("CrashLogger", "Crash logger initialized. Log file: " + CrashLogFilePath());
}

const std::string& CurrentCrashScope() {
    return gCurrentCrashScope;
}

std::string CrashLogFilePath() {
    return LogDirectory() + "\\city_builder.log";
}

void LogInfo(const std::string& scope, const std::string& message) {
    WriteLine("info", scope, message);
}

void LogWarning(const std::string& scope, const std::string& message) {
    WriteLine("warning", scope, message);
}

void LogError(const std::string& scope, const std::string& message) {
    WriteLine("error", scope, message);
}

void LogException(const std::string& scope, const std::exception& error) {
    LogError(scope, error.what());
}

void SetApplicationDialogOwner(void* nativeWindowHandle) {
    gApplicationDialogOwner = static_cast<HWND>(nativeWindowHandle);
}

void ClearApplicationDialogOwner(void* nativeWindowHandle) {
    HWND handle = static_cast<HWND>(nativeWindowHandle);
    if (nativeWindowHandle == 0 || gApplicationDialogOwner == handle) {
        gApplicationDialogOwner = 0;
    }
}

int LogCrashAndShowWindow(const std::string& scope, const std::string& message) {
    WriteLine("fatal", scope, message);

    std::ostringstream dialogText;
    dialogText
        << gApplicationName << " encountered a fatal error.\n\n"
        << "Scope: " << scope << "\n"
        << "Error: " << message << "\n\n"
        << "Log file:\n" << CrashLogFilePath();

    return ShowApplicationDialog("City Builder Crash", dialogText.str(), MB_ICONERROR);
}

int LogCrashAndShowWindow(const std::string& scope, const std::exception& error) {
    return LogCrashAndShowWindow(scope, error.what());
}
