#include "pipeline.h"
#include "../config/config.h"
#include "../ocr/ocr_engine.h"
#include "../translation/translator.h"
#include "../overlay/imgui_overlay.h"

#include <chrono>

namespace bronco::pipeline {

namespace {
    // OCR engine and translator are owned by the pipeline worker thread
    bronco::ocr::OcrEngine g_ocrEngine;
    bronco::translation::Translator g_translator;
    bool g_modulesInitialized = false;

    /// Initialize OCR and translation modules using current config.
    bool initializeModules()
    {
        if (g_modulesInitialized) return true;

        auto& config = bronco::Config::instance();

        // Initialize OCR engine
        if (!g_ocrEngine.initialize(config.tessDataPath(), config.ocrLanguage()))
        {
            OutputDebugStringA("[Bronco] Pipeline: Failed to initialize OCR engine\n");
            return false;
        }

        g_ocrEngine.setConfidenceThreshold(config.ocrConfidenceThreshold());

        // Initialize translator with dictionaries
        if (!g_translator.initialize(
            config.dictionaryPath(),
            config.targetLocale(),
            config.cacheCapacity()))
        {
            OutputDebugStringA("[Bronco] Pipeline: Failed to initialize translator\n");
            return false;
        }

        g_modulesInitialized = true;
        OutputDebugStringA("[Bronco] Pipeline: OCR and translation modules initialized\n");
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

    m_lastCaptureTime = GetTickCount();
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
        g_ocrEngine.shutdown();
        g_modulesInitialized = false;
    }

    m_device = nullptr;
    m_context = nullptr;

    OutputDebugStringA("[Bronco] Pipeline: Shut down\n");
}

void Pipeline::onPresent(IDXGISwapChain* swapChain)
{
    if (!m_running.load() || !swapChain) return;

    // Only capture at the configured interval
    DWORD now = GetTickCount();
    int intervalMs = bronco::Config::instance().ocrIntervalMs();
    if (static_cast<int>(now - m_lastCaptureTime) < intervalMs) return;

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

void Pipeline::workerThread()
{
    // Initialize OCR and translation on this thread (avoids blocking render)
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

        // Run OCR on the captured frame
        auto ocrResults = g_ocrEngine.recognizeRegions(
            pixels.data(), width, height, regions);

        if (ocrResults.empty()) continue;

        // Translate OCR results
        auto translations = g_translator.translateBatch(ocrResults);

        if (translations.empty()) continue;

        // Build overlay entries with screen positions from the OCR regions
        std::vector<bronco::overlay::TranslatedEntry> overlayEntries;
        overlayEntries.reserve(translations.size());

        for (size_t i = 0; i < translations.size() && i < ocrResults.size(); ++i)
        {
            bronco::overlay::TranslatedEntry entry;
            entry.original = translations[i].original;
            entry.translated = translations[i].translated;
            entry.x = static_cast<float>(ocrResults[i].region.x);
            entry.y = static_cast<float>(ocrResults[i].region.y);
            entry.width = static_cast<float>(ocrResults[i].region.width);
            entry.height = static_cast<float>(ocrResults[i].region.height);
            overlayEntries.push_back(std::move(entry));
        }

        // Update overlay (thread-safe via overlay's internal mutex)
        bronco::overlay::setTranslations(overlayEntries);
    }
}

bool Pipeline::captureBackbuffer(IDXGISwapChain* swapChain)
{
    if (!m_stagingTexture || !m_context) return false;

    // Get the backbuffer
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || !backBuffer) return false;

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
