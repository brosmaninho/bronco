#include "ocr_engine.h"

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#include <algorithm>

namespace bronco::ocr {

struct OcrEngine::Impl {
    std::unique_ptr<tesseract::TessBaseAPI> api;
    bool initialized = false;
};

OcrEngine::OcrEngine()
    : m_impl(std::make_unique<Impl>())
{
}

OcrEngine::~OcrEngine()
{
    shutdown();
}

bool OcrEngine::initialize(const std::string& tessDataPath, const std::string& language)
{
    if (m_impl->initialized) return true;

    m_impl->api = std::make_unique<tesseract::TessBaseAPI>();

    int result = m_impl->api->Init(tessDataPath.c_str(), language.c_str());
    if (result != 0)
    {
        OutputDebugStringA("[Bronco] Failed to initialize Tesseract OCR\n");
        m_impl->api.reset();
        return false;
    }

    // Set page segmentation mode for sparse text (game UI)
    m_impl->api->SetPageSegMode(tesseract::PSM_SPARSE_TEXT);

    m_impl->initialized = true;
    OutputDebugStringA("[Bronco] Tesseract OCR initialized\n");
    return true;
}

void OcrEngine::shutdown()
{
    if (m_impl && m_impl->api)
    {
        m_impl->api->End();
        m_impl->api.reset();
        m_impl->initialized = false;
    }
}

bool OcrEngine::isReady() const
{
    return m_impl && m_impl->initialized;
}

OcrResult OcrEngine::recognize(
    const uint8_t* pixelData,
    int width,
    int height,
    int bytesPerPixel,
    const ScreenRegion& region)
{
    OcrResult result;
    result.region = region;

    if (!isReady() || !pixelData)
    {
        return result;
    }

    // Set image data (BGRA format from DirectX)
    m_impl->api->SetImage(pixelData, width, height, bytesPerPixel, width * bytesPerPixel);
    m_impl->api->SetRectangle(0, 0, width, height);

    // Perform recognition
    char* outText = m_impl->api->GetUTF8Text();
    if (outText)
    {
        result.text = outText;
        delete[] outText;

        // Trim whitespace
        auto start = result.text.find_first_not_of(" \t\n\r");
        auto end = result.text.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && end != std::string::npos)
        {
            result.text = result.text.substr(start, end - start + 1);
        }
        else
        {
            result.text.clear();
        }
    }

    // Get confidence
    result.confidence = static_cast<float>(m_impl->api->MeanTextConf());

    return result;
}

std::vector<OcrResult> OcrEngine::recognizeRegions(
    const uint8_t* pixelData,
    int screenWidth,
    int screenHeight,
    const std::vector<ScreenRegion>& regions)
{
    std::vector<OcrResult> results;
    results.reserve(regions.size());

    if (!isReady() || !pixelData) return results;

    for (const auto& region : regions)
    {
        // Validate region bounds
        if (region.x < 0 || region.y < 0 ||
            region.x + region.width > screenWidth ||
            region.y + region.height > screenHeight)
        {
            continue;
        }

        // Extract region pixel data (BGRA, 4 bytes per pixel)
        const int bytesPerPixel = 4;
        const int rowPitch = screenWidth * bytesPerPixel;

        std::vector<uint8_t> regionData(region.width * region.height * bytesPerPixel);

        for (int row = 0; row < region.height; ++row)
        {
            const uint8_t* srcRow = pixelData + (region.y + row) * rowPitch + region.x * bytesPerPixel;
            uint8_t* dstRow = regionData.data() + row * region.width * bytesPerPixel;
            std::copy_n(srcRow, region.width * bytesPerPixel, dstRow);
        }

        auto result = recognize(regionData.data(), region.width, region.height, bytesPerPixel, region);

        // Only include results above confidence threshold
        if (result.confidence >= m_confidenceThreshold && !result.text.empty())
        {
            results.push_back(std::move(result));
        }
    }

    return results;
}

void OcrEngine::setConfidenceThreshold(float threshold)
{
    m_confidenceThreshold = std::clamp(threshold, 0.0f, 100.0f);
}

} // namespace bronco::ocr
