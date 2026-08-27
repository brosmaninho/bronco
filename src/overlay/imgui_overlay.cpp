#include "imgui_overlay.h"
#include "../config/config.h"
#include "../log/logger.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <mutex>
#include <atomic>

// Forward declare the ImGui Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace bronco::overlay {

namespace {
    // State
    std::atomic<bool> g_initialized{false};
    std::atomic<int> g_visible{1}; // Use int for fetch_xor atomicity
    std::mutex g_translationMutex;
    std::vector<TranslatedEntry> g_translations;

    // D3D11 resources
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    ID3D11RenderTargetView* g_renderTargetView = nullptr;

    // Window procedure hook
    HWND g_hwnd = nullptr;
    WNDPROC g_originalWndProc = nullptr;

    // Timestamp of overlay initialization (for welcome message)
    ULONGLONG g_initTimestamp = 0;

    // Our window procedure that intercepts input for ImGui and handles hotkeys
    LRESULT WINAPI hookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Check for toggle hotkey (configured key, default VK_F8)
        if (msg == WM_KEYDOWN)
        {
            int hotkey = bronco::Config::instance().toggleHotkey();
            if (static_cast<int>(wParam) == hotkey)
            {
                toggleVisibility();
                return 0; // Consume the key
            }
        }

        // Only let ImGui process input when the overlay is visible AND ImGui
        // actually wants to capture the input (e.g., mouse over an ImGui window).
        // For a passive overlay that does not have interactive widgets, we should
        // NOT consume mouse events - let them pass through to the game always.
        // Only forward keyboard input to ImGui when visible.
        if (g_visible.load() != 0)
        {
            // Forward keyboard messages to ImGui but NEVER consume mouse messages.
            // This ensures game camera rotation (right-click) and target selection
            // (left-click) always work.
            bool isMouseMessage = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
                                  msg == WM_NCMOUSEMOVE || msg == WM_NCLBUTTONDOWN ||
                                  msg == WM_NCRBUTTONDOWN;

            if (!isMouseMessage)
            {
                ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            }
        }

        // Always pass ALL messages (including mouse) to the game
        return CallWindowProcW(g_originalWndProc, hWnd, msg, wParam, lParam);
    }

    void createRenderTarget(IDXGISwapChain* swapChain)
    {
        ID3D11Texture2D* backBuffer = nullptr;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
        if (backBuffer)
        {
            g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTargetView);
            backBuffer->Release();
        }
    }

    void cleanupRenderTarget()
    {
        if (g_renderTargetView)
        {
            g_renderTargetView->Release();
            g_renderTargetView = nullptr;
        }
    }
} // anonymous namespace

bool initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (g_initialized.load()) return true;

    g_hwnd = hwnd;
    g_device = device;
    g_context = context;

    // Initialize ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    bronco::log::info("overlay::initialize: ImGui context created");

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // Disable ImGui mouse/keyboard capture so input always reaches the game
    io.WantCaptureMouse = false;
    io.WantCaptureKeyboard = false;

    // Set dark style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 0.9f;
    style.WindowRounding = 5.0f;

    // Initialize platform/renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);

    // Hook the window procedure for input handling
    g_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hookedWndProc)));

    bronco::log::info("overlay::initialize: WndProc hooked");

    // Record init timestamp for welcome message
    g_initTimestamp = GetTickCount64();

    g_initialized.store(true);
    bronco::log::info("overlay::initialize: ImGui overlay initialized");
    return true;
}

void render(IDXGISwapChain* swapChain)
{
    if (!g_initialized.load()) return;

    // If not visible, do not render anything (respect toggle)
    if (!g_visible.load()) return;

    // Ensure render target exists (recreated after ResizeBuffers invalidation)
    if (!g_renderTargetView)
    {
        createRenderTarget(swapChain);
    }

    if (!g_renderTargetView) return;

    // Start new ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Ensure ImGui does not capture mouse/keyboard from the game
    ImGuiIO& io = ImGui::GetIO();
    io.WantCaptureMouse = false;
    io.WantCaptureKeyboard = false;

    // --- Always draw "Bronco v0.3" indicator in top-right corner ---
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const char* indicator = "Bronco v0.3";
        ImVec2 textSize = ImGui::CalcTextSize(indicator);
        float padding = 6.0f;
        float x = io.DisplaySize.x - textSize.x - padding - 10.0f;
        float y = 10.0f;

        // Semi-transparent green text
        drawList->AddText(ImVec2(x, y), IM_COL32(0, 255, 0, 128), indicator);
    }

    // --- Welcome message for the first 5 seconds ---
    {
        ULONGLONG elapsed = GetTickCount64() - g_initTimestamp;
        if (elapsed < 5000)
        {
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            const char* welcomeMsg = "[Bronco] Overlay ativo! Pressione F8 para toggle.";
            ImVec2 textSize = ImGui::CalcTextSize(welcomeMsg);
            float x = (io.DisplaySize.x - textSize.x) * 0.5f;
            float y = 40.0f;

            // Dark background
            drawList->AddRectFilled(
                ImVec2(x - 8.0f, y - 4.0f),
                ImVec2(x + textSize.x + 8.0f, y + textSize.y + 4.0f),
                IM_COL32(20, 20, 20, 200),
                4.0f);

            // White text
            drawList->AddText(ImVec2(x, y), IM_COL32(255, 255, 255, 255), welcomeMsg);
        }
    }

    // --- Render translations ---
    {
        std::lock_guard<std::mutex> lock(g_translationMutex);

        if (!g_translations.empty())
        {
            // Create a transparent fullscreen window for overlays
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("##BroncoOverlay",
                nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoBackground);

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            for (const auto& entry : g_translations)
            {
                // Draw a semi-transparent background behind the translation
                ImVec2 textPos(entry.x, entry.y);
                ImVec2 textSize = ImGui::CalcTextSize(entry.translated.c_str());

                drawList->AddRectFilled(
                    ImVec2(textPos.x - 4, textPos.y - 2),
                    ImVec2(textPos.x + textSize.x + 4, textPos.y + textSize.y + 2),
                    IM_COL32(20, 20, 20, 200),
                    3.0f);

                // Draw the translated text
                drawList->AddText(textPos, IM_COL32(255, 255, 255, 255),
                    entry.translated.c_str());
            }

            ImGui::End();
        }
    }

    // Render ImGui
    ImGui::Render();
    g_context->OMSetRenderTargets(1, &g_renderTargetView, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void shutdown()
{
    if (!g_initialized.load()) return;

    // Restore original window procedure
    if (g_originalWndProc && g_hwnd)
    {
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
        g_originalWndProc = nullptr;
    }

    // Shutdown ImGui
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanupRenderTarget();

    g_initialized.store(false);
    bronco::log::info("overlay::shutdown: ImGui overlay shut down");
}

void invalidateRenderTarget()
{
    cleanupRenderTarget();
}

void setTranslations(const std::vector<TranslatedEntry>& entries)
{
    std::lock_guard<std::mutex> lock(g_translationMutex);
    g_translations = entries;
}

void toggleVisibility()
{
    // Atomic toggle using fetch_xor - no TOCTOU race
    g_visible.fetch_xor(1);
}

bool isVisible()
{
    return g_visible.load() != 0;
}

} // namespace bronco::overlay
