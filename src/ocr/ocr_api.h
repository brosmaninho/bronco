#pragma once

// C API for bronco_ocr.dll
// This DLL contains the heavy OCR (Tesseract) and translation (sqlite3) components.
// The main d3d11.dll proxy loads this DLL via LoadLibrary at runtime to avoid
// import-time dependencies that would prevent GW2 from starting.

#ifdef BRONCO_OCR_EXPORTS
    #define BRONCO_OCR_API __declspec(dllexport)
#else
    #define BRONCO_OCR_API __declspec(dllimport)
#endif

#include <cstdint>

/// Maximum number of BroncoOcrResult entries bronco_ocr_process_frame will ever
/// write. The caller (OcrLoader) allocates exactly this many slots, and the DLL
/// stops writing when it reaches this cap so the caller-allocated array is never
/// overflowed. One OCR region can produce many recognized lines, so this is much
/// larger than the region count. A full reconstructed skill tooltip emits a
/// name header, type, description, notes, and one line per fact, so this cap is
/// raised well above the old value to fit a complete multi-line tooltip.
#define BRONCO_OCR_MAX_RESULTS 96

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque handle to the OCR engine instance.
typedef struct BroncoOcrEngine* BroncoOcrHandle;

/// Result from a single OCR region recognition.
struct BroncoOcrResult {
    const char* originalText;     // OCR'd English text (owned by engine, valid until next call)
    const char* translatedText;   // Translated text (owned by engine, valid until next call)
    float confidence;             // OCR confidence (0-100)
    int regionX;
    int regionY;
    int regionWidth;
    int regionHeight;
    int matched;                  // 1 = dictionary match, 0 = raw OCR line (no translation)
};

/// Create a new OCR engine instance.
/// @return Handle to the engine, or nullptr on failure.
BRONCO_OCR_API BroncoOcrHandle bronco_ocr_create(void);

/// Initialize the OCR engine with tessdata path, language, dictionary path, and locale.
/// @param handle Engine handle from bronco_ocr_create
/// @param tessDataPath Path to tessdata directory
/// @param language Tesseract language code (e.g., "eng")
/// @param dictionaryPath Path to dictionary base directory
/// @param locale Target locale for translation (e.g., "pt-br")
/// @param skillDataPath Path to the skilldata base directory (e.g.,
///        "data/skilldata"); the DLL loads <skillDataPath>/<locale>/skills_tooltips.json
/// @param confidenceThreshold Minimum OCR confidence (0-100)
/// @param cacheCapacity LRU cache size for translations
/// @return 1 on success, 0 on failure
BRONCO_OCR_API int bronco_ocr_initialize(
    BroncoOcrHandle handle,
    const char* tessDataPath,
    const char* language,
    const char* dictionaryPath,
    const char* locale,
    const char* skillDataPath,
    float confidenceThreshold,
    int cacheCapacity);

/// Process a frame: run OCR on specified regions and translate results.
/// @param handle Engine handle
/// @param pixelData BGRA pixel data of the full screen
/// @param screenWidth Width in pixels
/// @param screenHeight Height in pixels
/// @param regionXs Array of region X coordinates
/// @param regionYs Array of region Y coordinates
/// @param regionWidths Array of region widths
/// @param regionHeights Array of region heights
/// @param regionCount Number of regions
/// @param outResults Output array (caller-allocated). One region can yield
///        multiple recognized lines (and a matched skill emits a full
///        multi-line tooltip), so the caller must provide at least
///        BRONCO_OCR_MAX_RESULTS slots; this function writes at most
///        BRONCO_OCR_MAX_RESULTS results.
/// @param outResultCount Number of valid results written (0..BRONCO_OCR_MAX_RESULTS)
/// @return 1 on success, 0 on failure
BRONCO_OCR_API int bronco_ocr_process_frame(
    BroncoOcrHandle handle,
    const uint8_t* pixelData,
    int screenWidth,
    int screenHeight,
    const int* regionXs,
    const int* regionYs,
    const int* regionWidths,
    const int* regionHeights,
    int regionCount,
    BroncoOcrResult* outResults,
    int* outResultCount);

/// Shut down the engine and release resources.
/// @param handle Engine handle
BRONCO_OCR_API void bronco_ocr_shutdown(BroncoOcrHandle handle);

/// Destroy the engine instance and free memory.
/// @param handle Engine handle (becomes invalid after this call)
BRONCO_OCR_API void bronco_ocr_destroy(BroncoOcrHandle handle);

#ifdef __cplusplus
}
#endif
