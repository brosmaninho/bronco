#include "ocr_api.h"
#include "ocr_engine.h"
#include "../translation/translator.h"

#include <Windows.h>
#include <string>
#include <vector>

/// Internal implementation wrapping OcrEngine + Translator behind the C API.
struct BroncoOcrEngine {
    bronco::ocr::OcrEngine ocrEngine;
    bronco::translation::Translator translator;
    bool initialized = false;

    // Storage for result strings (valid until next process_frame call)
    struct ResultStorage {
        std::string original;
        std::string translated;
    };
    std::vector<ResultStorage> resultStorage;
};

extern "C" {

BRONCO_OCR_API BroncoOcrHandle bronco_ocr_create(void)
{
    try
    {
        auto* engine = new BroncoOcrEngine();
        OutputDebugStringA("[Bronco OCR] Engine instance created\n");
        return engine;
    }
    catch (...)
    {
        OutputDebugStringA("[Bronco OCR] Failed to create engine instance\n");
        return nullptr;
    }
}

BRONCO_OCR_API int bronco_ocr_initialize(
    BroncoOcrHandle handle,
    const char* tessDataPath,
    const char* language,
    const char* dictionaryPath,
    const char* locale,
    float confidenceThreshold,
    int cacheCapacity)
{
    if (!handle || !tessDataPath || !language || !dictionaryPath || !locale)
        return 0;

    try
    {
        // Initialize OCR engine
        if (!handle->ocrEngine.initialize(tessDataPath, language))
        {
            OutputDebugStringA("[Bronco OCR] Failed to initialize Tesseract\n");
            return 0;
        }

        handle->ocrEngine.setConfidenceThreshold(confidenceThreshold);

        // Initialize translator
        if (!handle->translator.initialize(
                dictionaryPath,
                locale,
                static_cast<std::size_t>(cacheCapacity > 0 ? cacheCapacity : 5000)))
        {
            OutputDebugStringA("[Bronco OCR] Failed to initialize translator\n");
            handle->ocrEngine.shutdown();
            return 0;
        }

        handle->initialized = true;
        OutputDebugStringA("[Bronco OCR] Engine initialized successfully\n");
        return 1;
    }
    catch (...)
    {
        OutputDebugStringA("[Bronco OCR] Exception during initialization\n");
        return 0;
    }
}

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
    int* outResultCount)
{
    if (!handle || !handle->initialized || !pixelData || !outResults || !outResultCount)
        return 0;

    if (regionCount <= 0)
    {
        *outResultCount = 0;
        return 1;
    }

    try
    {
        // Build region list
        std::vector<bronco::ocr::ScreenRegion> regions;
        regions.reserve(regionCount);

        for (int i = 0; i < regionCount; ++i)
        {
            bronco::ocr::ScreenRegion region;
            region.x = regionXs[i];
            region.y = regionYs[i];
            region.width = regionWidths[i];
            region.height = regionHeights[i];
            regions.push_back(region);
        }

        // Run OCR
        auto ocrResults = handle->ocrEngine.recognizeRegions(
            pixelData, screenWidth, screenHeight, regions);

        if (ocrResults.empty())
        {
            *outResultCount = 0;
            return 1;
        }

        // Translate results
        auto translations = handle->translator.translateBatch(ocrResults);

        // Store results (strings must remain valid until next call)
        handle->resultStorage.clear();
        handle->resultStorage.reserve(translations.size());

        int count = 0;
        for (std::size_t i = 0; i < translations.size() && i < ocrResults.size(); ++i)
        {
            BroncoOcrEngine::ResultStorage storage;
            storage.original = translations[i].original;
            storage.translated = translations[i].translated;
            handle->resultStorage.push_back(std::move(storage));

            outResults[count].originalText = handle->resultStorage.back().original.c_str();
            outResults[count].translatedText = handle->resultStorage.back().translated.c_str();
            outResults[count].confidence = ocrResults[i].confidence;
            outResults[count].regionX = ocrResults[i].region.x;
            outResults[count].regionY = ocrResults[i].region.y;
            outResults[count].regionWidth = ocrResults[i].region.width;
            outResults[count].regionHeight = ocrResults[i].region.height;
            ++count;
        }

        *outResultCount = count;
        return 1;
    }
    catch (...)
    {
        OutputDebugStringA("[Bronco OCR] Exception during frame processing\n");
        *outResultCount = 0;
        return 0;
    }
}

BRONCO_OCR_API void bronco_ocr_shutdown(BroncoOcrHandle handle)
{
    if (!handle) return;

    handle->ocrEngine.shutdown();
    handle->initialized = false;
    handle->resultStorage.clear();
    OutputDebugStringA("[Bronco OCR] Engine shut down\n");
}

BRONCO_OCR_API void bronco_ocr_destroy(BroncoOcrHandle handle)
{
    if (!handle) return;

    if (handle->initialized)
    {
        bronco_ocr_shutdown(handle);
    }

    delete handle;
    OutputDebugStringA("[Bronco OCR] Engine instance destroyed\n");
}

} // extern "C"
