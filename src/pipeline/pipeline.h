#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <cstdint>

namespace bronco::pipeline {

/// The Pipeline orchestrates the full translation flow:
///   Capture backbuffer -> Copy to staging texture -> Map pixels ->
///   OCR recognition -> Translation lookup -> Update overlay
///
/// It runs OCR on a background thread at a configurable interval to avoid
/// blocking the render thread. The overlay is updated atomically via
/// overlay::setTranslations().
class Pipeline {
public:
    Pipeline();
    ~Pipeline();

    // Non-copyable
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    /// Initialize the pipeline with the device used for rendering.
    /// Must be called after the device and swap chain are available.
    /// @param device The D3D11 device
    /// @param context The immediate device context
    /// @param swapChain The active swap chain
    /// @return true on success
    bool initialize(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain);

    /// Shut down the pipeline, stopping the background thread and releasing resources.
    void shutdown();

    /// Notify the pipeline that a frame has been presented.
    /// Called from the hooked Present(). Triggers a capture if the OCR interval has elapsed.
    void onPresent(IDXGISwapChain* swapChain);

    /// Invalidate the staging texture so it is recreated on the next capture.
    /// Must be called when the swap chain is resized (e.g., from hookedResizeBuffers).
    void invalidateStagingTexture();

    /// Check if the pipeline is running.
    bool isRunning() const;

private:
    /// Background worker that processes captured frames.
    void workerThread();

    /// Capture the current backbuffer into the staging texture.
    bool captureBackbuffer(IDXGISwapChain* swapChain);

    // D3D11 resources for staging (GPU -> CPU copy)
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ID3D11Texture2D* m_stagingTexture = nullptr;

    // Captured pixel buffer (protected by m_captureMutex)
    std::mutex m_captureMutex;
    std::vector<uint8_t> m_capturedPixels;
    int m_captureWidth = 0;
    int m_captureHeight = 0;
    std::atomic<bool> m_captureReady{false};

    // Background processing thread
    std::thread m_worker;
    std::atomic<bool> m_running{false};

    // Timing (using GetTickCount64 to avoid 32-bit wraparound after ~49 days)
    ULONGLONG m_lastCaptureTime = 0;
};

/// Get the global pipeline instance.
Pipeline& instance();

} // namespace bronco::pipeline
