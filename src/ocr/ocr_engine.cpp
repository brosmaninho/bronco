#include "ocr_engine.h"

#include <Windows.h>

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

    // Use single-block page segmentation so a multi-line tooltip is read as one
    // clean block of lines (title + description + stat lines) rather than
    // scattered sparse fragments. This gives us newline-separated lines that we
    // then split and look up individually. See:
    // https://tesseract-ocr.github.io/tessdoc/ImproveQuality.html
    m_impl->api->SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);

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

    if (!isReady() || !pixelData || width <= 0 || height <= 0)
    {
        return result;
    }

    // --- Image preprocessing: grayscale + invert into a 1-byte-per-pixel buffer ---
    //
    // The incoming buffer from DirectX is BGRA (bytesPerPixel==4, byte order
    // B,G,R,A per pixel); we also defensively handle a 3-byte BGR layout.
    //
    // Tesseract 4.x/5.x expects DARK text on a LIGHT background and blends any
    // alpha channel with white, which wrecks GW2's light-text-on-dark tooltips.
    // So we (1) drop the alpha channel, (2) convert to grayscale, and (3) INVERT
    // in a single pass: out = 255 - ((B+G+R)/3). This turns GW2's light-on-dark
    // tooltip into dark-on-light, which is what Tesseract reads best.
    // Reference: https://tesseract-ocr.github.io/tessdoc/ImproveQuality.html
    //
    // The grayscale buffer is a function-local std::vector<uint8_t> kept alive
    // until after GetUTF8Text() returns, since SetImage does not copy in all
    // Tesseract versions.
    const int bpp = (bytesPerPixel == 3) ? 3 : 4;
    std::vector<uint8_t> gray(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    for (int y = 0; y < height; ++y)
    {
        const uint8_t* srcRow = pixelData + static_cast<std::size_t>(y) * width * bpp;
        uint8_t* dstRow = gray.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x)
        {
            const uint8_t* px = srcRow + static_cast<std::size_t>(x) * bpp;
            const int b = px[0];
            const int g = px[1];
            const int r = px[2];
            const int avg = (b + g + r) / 3;
            dstRow[x] = static_cast<uint8_t>(255 - avg); // grayscale + invert
        }
    }

    // Feed the 1-byte-per-pixel grayscale image (bytes_per_pixel=1,
    // bytes_per_line=width). Do NOT pass the original BGRA buffer anymore.
    m_impl->api->SetImage(gray.data(), width, height, 1, width);
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

        // Region-level confidence gate. Per-line confidence is not readily
        // available from GetUTF8Text alone, so we keep a region-level
        // MeanTextConf check but at a LOWERED threshold (default 40, set via
        // config) so a mixed-content tooltip is not wrongly discarded whole.
        // Also require non-empty recognized text.
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
