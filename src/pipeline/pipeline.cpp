#include "pipeline.h"
#include "../config/config.h"
#include "../ocr/ocr_engine.h"
#include "../ocr/ocr_loader.h"
#include "../overlay/imgui_overlay.h"
#include "../log/logger.h"

#include <algorithm>
#include <chrono>
#include <string>

namespace bronco::pipeline {

namespace {
    // OCR loader (loads bronco_ocr.dll at runtime via LoadLibrary)
    bronco::ocr::OcrLoader g_ocrLoader;
    bool g_modulesInitialized = false;

    /// Initialize OCR module via the dynamic loader.
    bool initializeModules()
    {
        if (g_modulesInitialized) return true;

        auto& config = bronco::Config::instance();

        // Initialize OCR engine + translator via the loader (loads bronco_ocr.dll)
        if (!g_ocrLoader.initialize(
            config.tessDataPath(),
            config.ocrLanguage(),
            config.dictionaryPath(),
            config.targetLocale(),
            config.ocrConfidenceThreshold(),
            static_cast<int>(config.cacheCapacity())))
        {
            bronco::log::error("Pipeline: Failed to initialize OCR loader");
            return false;
        }

        g_modulesInitialized = true;
        bronco::log::info("Pipeline: OCR loader initialized (bronco_ocr.dll loaded)");
        return true;
    }
} // anonymous namespace

Pipeline::Pipeline() = default;

Pipeline::~Pipeline()
{
    shutdown();
}

bool Pipeline::initialize(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain)
{
    if (m_running.load()) return true;
    if (!device || !context || !swapChain) return false;

    m_device = device;
    m_context = context;

    // Get swap chain dimensions to create staging texture
    DXGI_SWAP_CHAIN_DESC desc = {};
    swapChain->GetDesc(&desc);

    // Create a staging texture for GPU -> CPU copy
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = desc.BufferDesc.Width;
    stagingDesc.Height = desc.BufferDesc.Height;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = desc.BufferDesc.Format;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);
    if (FAILED(hr))
    {
        bronco::log::error("Pipeline: Failed to create staging texture");
        return false;
    }

    m_captureWidth = static_cast<int>(desc.BufferDesc.Width);
    m_captureHeight = static_cast<int>(desc.BufferDesc.Height);

    // Start the background worker thread
    m_running.store(true);
    m_worker = std::thread(&Pipeline::workerThread, this);

    m_lastCaptureTime = GetTickCount64();
    bronco::log::info("Pipeline: Initialized and worker thread started");
    return true;
}

void Pipeline::shutdown()
{
    m_running.store(false);

    if (m_worker.joinable())
    {
        m_worker.join();
    }

    if (m_stagingTexture)
    {
        m_stagingTexture->Release();
        m_stagingTexture = nullptr;
    }

    if (g_modulesInitialized)
    {
        g_ocrLoader.shutdown();
        g_modulesInitialized = false;
    }

    m_device = nullptr;
    m_context = nullptr;

    bronco::log::info("Pipeline: Shut down");
}

void Pipeline::onPresent(IDXGISwapChain* swapChain)
{
    if (!m_running.load() || !swapChain) return;

    // Only capture at the configured interval (using GetTickCount64 to avoid wraparound)
    ULONGLONG now = GetTickCount64();
    ULONGLONG intervalMs = static_cast<ULONGLONG>(bronco::Config::instance().ocrIntervalMs());
    if ((now - m_lastCaptureTime) < intervalMs) return;

    // Only capture if the worker has consumed the previous frame
    if (m_captureReady.load()) return;

    // Capture backbuffer pixels
    if (captureBackbuffer(swapChain))
    {
        m_lastCaptureTime = now;
        m_captureReady.store(true);
    }
}

bool Pipeline::isRunning() const
{
    return m_running.load();
}

void Pipeline::invalidateStagingTexture()
{
    // Release the current staging texture; it will be recreated on next capture
    // with the new backbuffer dimensions.
    if (m_stagingTexture)
    {
        m_stagingTexture->Release();
        m_stagingTexture = nullptr;
    }
    bronco::log::info("Pipeline: Staging texture invalidated (resize)");
}

void Pipeline::workerThread()
{
    // Initialize OCR via the loader on this thread (avoids blocking render thread).
    // If this fails, the worker exits but does NOT set m_running to false.
    // The overlay continues to function without OCR.
    if (!initializeModules())
    {
        bronco::log::error("Pipeline: Worker thread exiting - module init failed (overlay continues without OCR)");
        return;
    }

    while (m_running.load())
    {
        // Wait for a captured frame
        if (!m_captureReady.load())
        {
            Sleep(5); // Brief sleep to avoid busy-waiting
            continue;
        }

        // Copy captured pixels under lock
        std::vector<uint8_t> pixels;
        int width, height;
        {
            std::lock_guard<std::mutex> lock(m_captureMutex);
            pixels = m_capturedPixels;
            width = m_captureWidth;
            height = m_captureHeight;
        }
        m_captureReady.store(false);

        if (pixels.empty()) continue;

        // Determine which OCR regions to scan this cycle.
        //
        // Default mode (ocr_follow_mouse = true): GW2 renders tooltips where the
        // mouse is, so a single region centered on the cursor is far more
        // effective than the fixed 1920x1080 regions. We convert the screen
        // cursor position to client coordinates via ScreenToClient, scale those
        // client coordinates into the captured backbuffer pixel space (client
        // size and backbuffer size can differ under windowed/borderless or DPI
        // scaling), then clamp the region so it lies fully within
        // [0,width)x[0,height). Clamping is REQUIRED because
        // OcrEngine::recognizeRegions() silently SKIPS any region where x<0,
        // y<0, x+width>screenWidth or y+height>screenHeight.
        //
        // Fallback mode (ocr_follow_mouse = false): use the fixed regions from
        // config, exactly as before.
        auto& config = bronco::Config::instance();
        std::vector<bronco::ocr::ScreenRegion> regions;

        if (config.ocrFollowMouse())
        {
            int regionWidth = config.ocrFollowWidth();
            int regionHeight = config.ocrFollowHeight();

            // Never let the region be larger than the captured frame.
            if (regionWidth > width) regionWidth = width;
            if (regionHeight > height) regionHeight = height;
            if (regionWidth <= 0 || regionHeight <= 0) continue;

            POINT pt = {};
            HWND hwnd = bronco::overlay::gameWindow();
            RECT clientRect = {};

            // The follow-mouse region is only meaningful when we can map the
            // cursor into the captured backbuffer's pixel space. That requires
            // both a valid game HWND (so ScreenToClient is anchored to the game
            // window rather than an arbitrary screen origin) and a valid,
            // non-zero client rect (so we can scale client -> backbuffer). When
            // either is missing (e.g., before overlay::initialize has run, or a
            // failed GetClientRect) we skip the follow region for this cycle and
            // let the code below fall back to the fixed Config::ocrRegions()
            // path, rather than building a mis-placed region from raw screen
            // coordinates (which would land in the wrong place on multi-monitor
            // or windowed layouts).
            if (GetCursorPos(&pt) && hwnd && GetClientRect(hwnd, &clientRect))
            {
                const int clientW = clientRect.right - clientRect.left;
                const int clientH = clientRect.bottom - clientRect.top;

                if (clientW > 0 && clientH > 0)
                {
                    // Convert the screen cursor to window-client coordinates.
                    ScreenToClient(hwnd, &pt);

                    // Client coordinates only equal backbuffer pixels when the
                    // client area maps 1:1 onto the backbuffer. In windowed /
                    // borderless or DPI-scaled setups the client size differs
                    // from the captured frame (width/height come from
                    // desc.BufferDesc.Width/Height), so scale the client-space
                    // cursor into backbuffer pixel space. Use double-precision
                    // intermediate math to avoid int overflow/truncation, then
                    // clamp below. When the sizes already match, the scale is a
                    // no-op.
                    double cursorX = static_cast<double>(pt.x);
                    double cursorY = static_cast<double>(pt.y);
                    if (clientW != width)
                    {
                        cursorX = cursorX * (static_cast<double>(width) / clientW);
                    }
                    if (clientH != height)
                    {
                        cursorY = cursorY * (static_cast<double>(height) / clientH);
                    }

                    int x = static_cast<int>(cursorX) - regionWidth / 2;
                    int y = static_cast<int>(cursorY) - regionHeight / 2;

                    // Clamp so the region lies fully within the captured frame,
                    // which is exactly what recognizeRegions() requires (it
                    // silently skips regions where x<0, y<0, x+w>width or
                    // y+h>height).
                    if (x < 0) x = 0;
                    if (y < 0) y = 0;
                    if (x + regionWidth > width) x = width - regionWidth;
                    if (y + regionHeight > height) y = height - regionHeight;

                    bronco::ocr::ScreenRegion follow;
                    follow.x = x;
                    follow.y = y;
                    follow.width = regionWidth;
                    follow.height = regionHeight;
                    follow.label = "follow_mouse";
                    regions.push_back(follow);

                    std::string regionMsg = "Pipeline: follow-mouse region x=" +
                        std::to_string(x) + " y=" + std::to_string(y) +
                        " w=" + std::to_string(regionWidth) +
                        " h=" + std::to_string(regionHeight) +
                        " (client=" + std::to_string(clientW) + "x" + std::to_string(clientH) +
                        " frame=" + std::to_string(width) + "x" + std::to_string(height) +
                        " scaledCursor=" + std::to_string(static_cast<int>(cursorX)) +
                        "," + std::to_string(static_cast<int>(cursorY)) + ")";
                    bronco::log::info(regionMsg.c_str());
                }
            }
        }

        // If follow-mouse produced no region (disabled, GetCursorPos failed, no
        // valid game HWND yet, or a zero-size client rect), use the fixed
        // regions from config as a fallback.
        if (regions.empty())
        {
            regions = config.ocrRegions();
        }
        if (regions.empty()) continue;

        // Build arrays for the C API
        std::vector<int> regionXs, regionYs, regionWidths, regionHeights;
        regionXs.reserve(regions.size());
        regionYs.reserve(regions.size());
        regionWidths.reserve(regions.size());
        regionHeights.reserve(regions.size());

        for (const auto& r : regions)
        {
            regionXs.push_back(r.x);
            regionYs.push_back(r.y);
            regionWidths.push_back(r.width);
            regionHeights.push_back(r.height);
        }

        // Process frame via the OCR loader (calls into bronco_ocr.dll)
        std::vector<BroncoOcrResult> ocrResults;
        bool success = g_ocrLoader.processFrame(
            pixels.data(), width, height,
            regionXs.data(), regionYs.data(),
            regionWidths.data(), regionHeights.data(),
            static_cast<int>(regions.size()),
            ocrResults);

        if (!success || ocrResults.empty()) continue;

        // Build overlay entries from OCR results
        std::vector<bronco::overlay::TranslatedEntry> overlayEntries;
        overlayEntries.reserve(ocrResults.size());

        for (const auto& result : ocrResults)
        {
            bronco::overlay::TranslatedEntry entry;
            entry.original = result.originalText ? result.originalText : "";
            entry.translated = result.translatedText ? result.translatedText : "";
            entry.matched = (result.matched != 0);

            // Diagnostic logging: report the recognized text and whether a
            // dictionary translation was found. We now key this off the
            // authoritative 'matched' flag from the OCR DLL instead of the old
            // (translated != original) heuristic, because raw fallback entries
            // now have translated == original by design. Skip empty text to
            // avoid log spam.
            if (!entry.original.empty())
            {
                std::string ocrMsg = "Pipeline: OCR text=\"" + entry.original + "\" -> " +
                    (entry.matched
                        ? ("translation found: \"" + entry.translated + "\"")
                        : std::string("no dictionary match"));
                bronco::log::info(ocrMsg.c_str());
            }

            entry.x = static_cast<float>(result.regionX);
            entry.y = static_cast<float>(result.regionY);
            entry.width = static_cast<float>(result.regionWidth);
            entry.height = static_cast<float>(result.regionHeight);
            overlayEntries.push_back(std::move(entry));
        }

        // Update overlay (thread-safe via overlay's internal mutex)
        bronco::overlay::setTranslations(overlayEntries);
    }
}

bool Pipeline::captureBackbuffer(IDXGISwapChain* swapChain)
{
    if (!m_device || !m_context) return false;

    // Get the backbuffer
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || !backBuffer) return false;

    // Check backbuffer dimensions against our staging texture
    D3D11_TEXTURE2D_DESC bbDesc = {};
    backBuffer->GetDesc(&bbDesc);

    int bbWidth = static_cast<int>(bbDesc.Width);
    int bbHeight = static_cast<int>(bbDesc.Height);

    // Recreate staging texture if dimensions changed or it was invalidated
    if (!m_stagingTexture || bbWidth != m_captureWidth || bbHeight != m_captureHeight)
    {
        if (m_stagingTexture)
        {
            m_stagingTexture->Release();
            m_stagingTexture = nullptr;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = bbDesc.Width;
        stagingDesc.Height = bbDesc.Height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = bbDesc.Format;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);
        if (FAILED(hr))
        {
            backBuffer->Release();
            bronco::log::error("Pipeline: Failed to recreate staging texture after resize");
            return false;
        }

        m_captureWidth = bbWidth;
        m_captureHeight = bbHeight;
        bronco::log::info("Pipeline: Staging texture recreated for new dimensions");
    }

    // Copy backbuffer to staging texture
    m_context->CopyResource(m_stagingTexture, backBuffer);
    backBuffer->Release();

    // Map the staging texture to read pixels
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = m_context->Map(m_stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    // Copy pixel data into our buffer
    const int bytesPerPixel = 4; // BGRA
    std::size_t dataSize = static_cast<std::size_t>(m_captureWidth) * m_captureHeight * bytesPerPixel;

    {
        std::lock_guard<std::mutex> lock(m_captureMutex);
        m_capturedPixels.resize(dataSize);

        // Handle row pitch (may include padding)
        const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
        const int dstRowPitch = m_captureWidth * bytesPerPixel;

        for (int row = 0; row < m_captureHeight; ++row)
        {
            std::copy_n(
                src + row * mapped.RowPitch,
                dstRowPitch,
                m_capturedPixels.data() + row * dstRowPitch);
        }
    }

    m_context->Unmap(m_stagingTexture, 0);
    return true;
}

Pipeline& instance()
{
    static Pipeline s_instance;
    return s_instance;
}

} // namespace bronco::pipeline
