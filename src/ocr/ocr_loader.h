#pragma once

#include "ocr_api.h"

#include <Windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

namespace bronco::ocr {

/// OcrLoader dynamically loads bronco_ocr.dll via LoadLibrary/GetProcAddress.
/// This avoids import-time dependencies on Tesseract, leptonica, sqlite3, etc.
/// The DLL is loaded lazily on first use (when the pipeline worker needs OCR).
class OcrLoader {
public:
    OcrLoader();
    ~OcrLoader();

    // Non-copyable
    OcrLoader(const OcrLoader&) = delete;
    OcrLoader& operator=(const OcrLoader&) = delete;

    /// Initialize: load bronco_ocr.dll and set up the OCR engine.
    /// @param tessDataPath Path to tessdata directory
    /// @param language Tesseract language code (e.g., "eng")
    /// @param dictionaryPath Path to dictionary base directory
    /// @param locale Target locale for translation (e.g., "pt-br")
    /// @param confidenceThreshold Minimum OCR confidence (0-100)
    /// @param cacheCapacity LRU cache size for translations
    /// @return true on success
    bool initialize(
        const std::string& tessDataPath,
        const std::string& language,
        const std::string& dictionaryPath,
        const std::string& locale,
        float confidenceThreshold,
        int cacheCapacity);

    /// Process a frame: run OCR + translation on specified regions.
    /// @param pixelData BGRA pixel data
    /// @param screenWidth Width in pixels
    /// @param screenHeight Height in pixels
    /// @param regionXs Array of region X coordinates
    /// @param regionYs Array of region Y coordinates
    /// @param regionWidths Array of region widths
    /// @param regionHeights Array of region heights
    /// @param regionCount Number of regions
    /// @param outResults Output results (resized on return)
    /// @return true on success
    bool processFrame(
        const uint8_t* pixelData,
        int screenWidth,
        int screenHeight,
        const int* regionXs,
        const int* regionYs,
        const int* regionWidths,
        const int* regionHeights,
        int regionCount,
        std::vector<BroncoOcrResult>& outResults);

    /// Shut down the OCR engine.
    void shutdown();

    /// Check if the loader and engine are ready.
    bool isReady() const;

private:
    /// Attempt to load the DLL and resolve all function pointers.
    bool loadDll();

    HMODULE m_dllHandle = nullptr;
    BroncoOcrHandle m_engineHandle = nullptr;
    std::atomic<bool> m_ready{false};

    // Function pointer types
    using PFN_Create = BroncoOcrHandle(*)(void);
    using PFN_Initialize = int(*)(BroncoOcrHandle, const char*, const char*, const char*, const char*, float, int);
    using PFN_ProcessFrame = int(*)(BroncoOcrHandle, const uint8_t*, int, int, const int*, const int*, const int*, const int*, int, BroncoOcrResult*, int*);
    using PFN_Shutdown = void(*)(BroncoOcrHandle);
    using PFN_Destroy = void(*)(BroncoOcrHandle);

    PFN_Create m_fnCreate = nullptr;
    PFN_Initialize m_fnInitialize = nullptr;
    PFN_ProcessFrame m_fnProcessFrame = nullptr;
    PFN_Shutdown m_fnShutdown = nullptr;
    PFN_Destroy m_fnDestroy = nullptr;
};

} // namespace bronco::ocr
