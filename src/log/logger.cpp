#include "logger.h"
#include "../proxy/d3d11_proxy.h"

#include <Windows.h>

#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace bronco::log {

namespace {
    std::ofstream g_logFile;
    std::mutex g_logMutex;
    bool g_initialized = false;

    /// Format the current time as [YYYY-MM-DD HH:MM:SS.mmm]
    std::string formatTimestamp()
    {
        using namespace std::chrono;

        auto now = system_clock::now();
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        auto timeT = system_clock::to_time_t(now);

        struct tm localTm = {};
        localtime_s(&localTm, &timeT);

        std::ostringstream oss;
        oss << '[' << std::put_time(&localTm, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count() << ']';
        return oss.str();
    }

    void ensureInitialized()
    {
        if (g_initialized) return;

        // Resolve path to bronco_log.txt relative to our proxy DLL
        std::wstring logPath;
        HMODULE ourModule = bronco::proxy::getOurModule();
        if (ourModule)
        {
            wchar_t modulePath[MAX_PATH] = {};
            DWORD len = GetModuleFileNameW(ourModule, modulePath, MAX_PATH);
            if (len > 0 && len < MAX_PATH)
            {
                std::wstring dir(modulePath, len);
                auto pos = dir.find_last_of(L'\\');
                if (pos != std::wstring::npos)
                {
                    dir = dir.substr(0, pos + 1);
                }
                logPath = dir + L"bronco_log.txt";
            }
        }

        // Fallback: write to current directory
        if (logPath.empty())
        {
            logPath = L"bronco_log.txt";
        }

        g_logFile.open(logPath, std::ios::app);
        g_initialized = true;
    }
} // anonymous namespace

void init()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    ensureInitialized();
}

void info(const char* msg)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    ensureInitialized();

    std::string line = formatTimestamp() + " [INFO] " + msg + "\n";

    if (g_logFile.is_open())
    {
        g_logFile << line;
        g_logFile.flush();
    }

    OutputDebugStringA(("[Bronco] " + std::string(msg) + "\n").c_str());
}

void error(const char* msg)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    ensureInitialized();

    std::string line = formatTimestamp() + " [ERROR] " + msg + "\n";

    if (g_logFile.is_open())
    {
        g_logFile << line;
        g_logFile.flush();
    }

    OutputDebugStringA(("[Bronco][ERROR] " + std::string(msg) + "\n").c_str());
}

} // namespace bronco::log
