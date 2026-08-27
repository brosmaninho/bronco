#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace bronco::ocr {

/// Defines a rectangular region on screen for OCR capture.
struct ScreenRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::string label;  // e.g., "tooltip", "npc_dialogue"
};

/// Result of an OCR operation on a screen region.
struct OcrResult {
    std::string text;
    float confidence = 0.0f;
    ScreenRegion region;
};

/// OCR engine using Tesseract 5 for text extraction from screen regions.
class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();

    // Non-copyable
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    /// Initialize Tesseract with the specified data path and language.
    /// @param tessDataPath Path to tessdata directory
    /// @param language Tesseract language code (e.g., "eng")
    /// @return true on success
    bool initialize(const std::string& tessDataPath, const std::string& language = "eng");

    /// Shut down Tesseract and free resources.
    void shutdown();

    /// Check if the engine is initialized and ready.
    bool isReady() const;

    /// Perform OCR on raw pixel data from a screen region.
    /// @param pixelData BGRA pixel data
    /// @param width Image width in pixels
    /// @param height Image height in pixels
    /// @param bytesPerPixel Bytes per pixel (typically 4 for BGRA)
    /// @param region The screen region this data was captured from
    /// @return OCR result with extracted text and confidence
    OcrResult recognize(
        const uint8_t* pixelData,
        int width,
        int height,
        int bytesPerPixel,
        const ScreenRegion& region);

    /// Perform OCR on multiple regions at once.
    /// @param pixelData Full screen BGRA pixel data
    /// @param screenWidth Full screen width
    /// @param screenHeight Full screen height
    /// @param regions Regions to process
    /// @return Vector of OCR results
    std::vector<OcrResult> recognizeRegions(
        const uint8_t* pixelData,
        int screenWidth,
        int screenHeight,
        const std::vector<ScreenRegion>& regions);

    /// Set minimum confidence threshold for valid results.
    void setConfidenceThreshold(float threshold);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    float m_confidenceThreshold = 40.0f;
};

} // namespace bronco::ocr
