#include "pipeline.h"
#include "../config/config.h"
#include "../ocr/ocr_loader.h"
#include "../overlay/imgui_overlay.h"

#include <chrono>

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
            OutputDebugStringA("[Bronco] Pipeline: Failed to initialize OCR loader\n");
            return false;
        }

        g_modulesInitialized = true;
        OutputDebugStringA("[Bronco] Pipeline: OCR loader initialized (bronco_ocr.dll loaded)\n");
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
        OutputDebugStringA("[Bronco] Pipeline: Failed to create staging texture\n");
        return false;
    }

    m_captureWidth = static_cast<int>(desc.BufferDesc.Width);
    m_captureHeight = static_cast<int>(desc.BufferDesc.Height);

    // Start the background worker thread
    m_running.store(true);
    m_worker = std::thread(&Pipeline::workerThread, this);

    m_lastCaptureTime = GetTickCount64();
    OutputDebugStringA("[Bronco] Pipeline: Initialized and worker thread started\n");
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

    OutputDebugStringA("[Bronco] Pipeline: Shut down\n");
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
    OutputDebugStringA("[Bronco] Pipeline: Staging texture invalidated (resize)\n");
}

void Pipeline::workerThread()
{
    // Initialize OCR via the loader on this thread (avoids blocking render thread)
    if (!initializeModules())
    {
        OutputDebugStringA("[Bronco] Pipeline: Worker thread exiting - module init failed\n");
        m_running.store(false);
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

        // Get configured OCR regions
        auto regions = bronco::Config::instance().ocrRegions();
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
            OutputDebugStringA("[Bronco] Pipeline: Failed to recreate staging texture after resize\n");
            return false;
        }

        m_captureWidth = bbWidth;
        m_captureHeight = bbHeight;
        OutputDebugStringA("[Bronco] Pipeline: Staging texture recreated for new dimensions\n");
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
