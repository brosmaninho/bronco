#include "ocr_loader.h"
#include "../proxy/d3d11_proxy.h"

#include <Windows.h>
#include <string>

namespace bronco::ocr {

OcrLoader::OcrLoader() = default;

OcrLoader::~OcrLoader()
{
    shutdown();
}

bool OcrLoader::loadDll()
{
    if (m_dllHandle) return true;

    // Resolve the path to bronco_ocr.dll relative to our proxy DLL's directory.
    // This avoids relying on the process working directory, which may differ
    // when GW2 is launched from an external launcher.
    std::wstring dllPath;
    HMODULE ourModule = bronco::proxy::getOurModule();
    if (ourModule)
    {
        wchar_t modulePath[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(ourModule, modulePath, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            // Find the last backslash to get the directory
            std::wstring dir(modulePath, len);
            auto pos = dir.find_last_of(L'\\');
            if (pos != std::wstring::npos)
            {
                dir = dir.substr(0, pos + 1);
            }
            dllPath = dir + L"bronco_ocr.dll";
        }
    }

    // Attempt to load from the resolved path, fall back to bare name if resolution failed
    if (!dllPath.empty())
    {
        m_dllHandle = LoadLibraryW(dllPath.c_str());
    }

    if (!m_dllHandle)
    {
        // Fallback: rely on standard DLL search order
        m_dllHandle = LoadLibraryW(L"bronco_ocr.dll");
    }

    if (!m_dllHandle)
    {
        OutputDebugStringA("[Bronco] OcrLoader: Failed to load bronco_ocr.dll\n");
        return false;
    }

    // Resolve function pointers
    m_fnCreate = reinterpret_cast<PFN_Create>(
        GetProcAddress(m_dllHandle, "bronco_ocr_create"));
    m_fnInitialize = reinterpret_cast<PFN_Initialize>(
        GetProcAddress(m_dllHandle, "bronco_ocr_initialize"));
    m_fnProcessFrame = reinterpret_cast<PFN_ProcessFrame>(
        GetProcAddress(m_dllHandle, "bronco_ocr_process_frame"));
    m_fnShutdown = reinterpret_cast<PFN_Shutdown>(
        GetProcAddress(m_dllHandle, "bronco_ocr_shutdown"));
    m_fnDestroy = reinterpret_cast<PFN_Destroy>(
        GetProcAddress(m_dllHandle, "bronco_ocr_destroy"));

    if (!m_fnCreate || !m_fnInitialize || !m_fnProcessFrame || !m_fnShutdown || !m_fnDestroy)
    {
        OutputDebugStringA("[Bronco] OcrLoader: Failed to resolve bronco_ocr.dll exports\n");
        FreeLibrary(m_dllHandle);
        m_dllHandle = nullptr;
        return false;
    }

    OutputDebugStringA("[Bronco] OcrLoader: bronco_ocr.dll loaded successfully\n");
    return true;
}

bool OcrLoader::initialize(
    const std::string& tessDataPath,
    const std::string& language,
    const std::string& dictionaryPath,
    const std::string& locale,
    float confidenceThreshold,
    int cacheCapacity)
{
    if (m_ready.load()) return true;

    // Load the DLL first
    if (!loadDll())
        return false;

    // Create engine instance
    m_engineHandle = m_fnCreate();
    if (!m_engineHandle)
    {
        OutputDebugStringA("[Bronco] OcrLoader: bronco_ocr_create failed\n");
        return false;
    }

    // Initialize the engine
    int result = m_fnInitialize(
        m_engineHandle,
        tessDataPath.c_str(),
        language.c_str(),
        dictionaryPath.c_str(),
        locale.c_str(),
        confidenceThreshold,
        cacheCapacity);

    if (result == 0)
    {
        OutputDebugStringA("[Bronco] OcrLoader: bronco_ocr_initialize failed\n");
        m_fnDestroy(m_engineHandle);
        m_engineHandle = nullptr;
        return false;
    }

    m_ready.store(true);
    OutputDebugStringA("[Bronco] OcrLoader: Engine initialized successfully\n");
    return true;
}

bool OcrLoader::processFrame(
    const uint8_t* pixelData,
    int screenWidth,
    int screenHeight,
    const int* regionXs,
    const int* regionYs,
    const int* regionWidths,
    const int* regionHeights,
    int regionCount,
    std::vector<BroncoOcrResult>& outResults)
{
    if (!m_ready.load() || !m_engineHandle) return false;

    outResults.resize(regionCount);
    int resultCount = 0;

    int success = m_fnProcessFrame(
        m_engineHandle,
        pixelData,
        screenWidth,
        screenHeight,
        regionXs,
        regionYs,
        regionWidths,
        regionHeights,
        regionCount,
        outResults.data(),
        &resultCount);

    if (success)
    {
        outResults.resize(resultCount);
    }
    else
    {
        outResults.clear();
    }

    return success != 0;
}

void OcrLoader::shutdown()
{
    if (m_engineHandle)
    {
        if (m_fnShutdown) m_fnShutdown(m_engineHandle);
        if (m_fnDestroy) m_fnDestroy(m_engineHandle);
        m_engineHandle = nullptr;
    }

    m_ready.store(false);

    if (m_dllHandle)
    {
        FreeLibrary(m_dllHandle);
        m_dllHandle = nullptr;
    }

    m_fnCreate = nullptr;
    m_fnInitialize = nullptr;
    m_fnProcessFrame = nullptr;
    m_fnShutdown = nullptr;
    m_fnDestroy = nullptr;

    OutputDebugStringA("[Bronco] OcrLoader: Shut down and unloaded bronco_ocr.dll\n");
}

bool OcrLoader::isReady() const
{
    return m_ready.load();
}

} // namespace bronco::ocr
