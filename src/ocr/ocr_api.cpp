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
    const char* skillDataPath,
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

        // Load the reconstructed skill-tooltip dataset. This is ADDITIVE: a
        // missing skilldata file must NOT fail engine init, so the name-only
        // translation path keeps working. skillDataPath may be null if an older
        // caller does not pass it.
        if (skillDataPath)
        {
            if (handle->translator.loadSkillTooltips(skillDataPath, locale))
            {
                OutputDebugStringA("[Bronco OCR] Skill tooltips loaded\n");
            }
            else
            {
                OutputDebugStringA("[Bronco OCR] Skill tooltips not loaded (name-only path active)\n");
            }
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

        // Redesign for multi-line output: ONE OCR region can yield MANY lines
        // (tooltip title + description + stat lines). We split each region's
        // recognized text into individual lines here so we can attach THAT
        // region's geometry/confidence to every emitted line (region
        // association stays correct), and look each line up independently.
        //
        // CRITICAL: BroncoOcrResult.originalText/translatedText point into the
        // std::string storage held in handle->resultStorage. If that vector
        // reallocates while we still hold earlier elements' c_str() pointers,
        // those pointers dangle. We therefore reserve resultStorage to the hard
        // cap BEFORE the loop so no push_back ever reallocates, and we cap the
        // number of emitted results at BRONCO_OCR_MAX_RESULTS so we never write
        // past the caller-allocated array.
        handle->resultStorage.clear();
        handle->resultStorage.reserve(BRONCO_OCR_MAX_RESULTS);

        int count = 0;
        for (const auto& ocr : ocrResults)
        {
            if (count >= BRONCO_OCR_MAX_RESULTS) break;

            const std::string& text = ocr.text;
            std::size_t pos = 0;
            while (pos <= text.size())
            {
                if (count >= BRONCO_OCR_MAX_RESULTS) break;

                std::size_t nl = text.find('\n', pos);
                std::string rawLine =
                    (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);

                // Trim leading/trailing whitespace (space, tab, CR).
                std::string line;
                {
                    const char* ws = " \t\r";
                    auto s = rawLine.find_first_not_of(ws);
                    if (s != std::string::npos)
                    {
                        auto e = rawLine.find_last_not_of(ws);
                        line = rawLine.substr(s, e - s + 1);
                    }
                }

                if (!line.empty())
                {
                    // Emit one display line: push its display text into
                    // resultStorage (already reserved to BRONCO_OCR_MAX_RESULTS
                    // before the loop so no reallocation dangles pointers) and
                    // point outResults[count] at the stored string. The cap
                    // check on EVERY emitted line guarantees we never write past
                    // the caller-allocated array. Both original and translated
                    // point at the SAME stored display string for tooltip lines;
                    // the overlay renders the translated (PT-BR) text.
                    auto emitLine =
                        [&](const std::string& displayText, int matchedFlag) -> bool
                    {
                        if (count >= BRONCO_OCR_MAX_RESULTS) return false;

                        BroncoOcrEngine::ResultStorage storage;
                        storage.original = displayText;
                        storage.translated = displayText;
                        handle->resultStorage.push_back(std::move(storage));

                        outResults[count].originalText = handle->resultStorage.back().original.c_str();
                        outResults[count].translatedText = handle->resultStorage.back().translated.c_str();
                        outResults[count].confidence = ocr.confidence;
                        outResults[count].regionX = ocr.region.x;
                        outResults[count].regionY = ocr.region.y;
                        outResults[count].regionWidth = ocr.region.width;
                        outResults[count].regionHeight = ocr.region.height;
                        outResults[count].matched = matchedFlag;
                        ++count;
                        return true;
                    };

                    // First, try to reconstruct a FULL skill tooltip from the
                    // skilldata dataset using the raw OCR line (exact-then
                    // word-boundary-contains match on the English skill name).
                    auto tooltip = handle->translator.skillTooltips().lookup(line);
                    if (tooltip.has_value())
                    {
                        const auto& tip = tooltip.value();
                        // Name header, then type, then description, then notes,
                        // then each formatted fact line - all matched=1, all
                        // sharing this OCR result's region geometry.
                        emitLine(tip.nameTranslated, 1);
                        if (!tip.typeTranslated.empty())
                            emitLine(tip.typeTranslated, 1);
                        if (!tip.descriptionTranslated.empty())
                            emitLine(tip.descriptionTranslated, 1);
                        for (const auto& note : tip.notes)
                            emitLine(note, 1);
                        for (const auto& factLine : tip.factLines)
                            emitLine(factLine, 1);
                    }
                    else
                    {
                        // No tooltip: fall back to the previous single-line
                        // behavior (matched name translation, or raw line).
                        auto translation = handle->translator.translate(line);
                        if (translation.has_value())
                        {
                            emitLine(translation.value().translated, 1);
                        }
                        else
                        {
                            // No dictionary match: surface the raw OCR line so
                            // the user can see OCR is working.
                            emitLine(line, 0);
                        }
                    }
                }

                if (nl == std::string::npos) break;
                pos = nl + 1;
            }
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
