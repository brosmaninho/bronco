#include "imgui_overlay.h"
#include "../config/config.h"
#include "../log/logger.h"
#include "../proxy/d3d11_proxy.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <mutex>
#include <atomic>
#include <string>

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

    // Persistent storage for the imgui.ini path. ImGui stores the char* pointer
    // directly (it does NOT copy the string), so this must outlive the ImGui
    // context. A function-local static string keeps the buffer alive for the
    // lifetime of the process.
    std::string& iniPathStorage()
    {
        static std::string s_iniPath;
        return s_iniPath;
    }

    /// Resolve the path to "bronco_imgui.ini" next to our proxy DLL so the
    /// window position persists between sessions. Returns an empty string if the
    /// module path cannot be resolved (ImGui then falls back to the default).
    std::string resolveIniPath()
    {
        std::wstring dir;
        HMODULE ourModule = bronco::proxy::getOurModule();
        if (ourModule)
        {
            wchar_t modulePath[MAX_PATH] = {};
            DWORD len = GetModuleFileNameW(ourModule, modulePath, MAX_PATH);
            if (len > 0 && len < MAX_PATH)
            {
                std::wstring full(modulePath, len);
                auto pos = full.find_last_of(L'\\');
                if (pos != std::wstring::npos)
                {
                    dir = full.substr(0, pos + 1);
                }
            }
        }

        std::wstring wide = dir + L"bronco_imgui.ini";

        // Convert to UTF-8 for ImGui's char* API.
        int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
            nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return std::string();
        }

        std::string result(static_cast<size_t>(needed - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
            result.data(), needed, nullptr, nullptr);
        return result;
    }

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

        // Forward messages to ImGui only while the overlay is visible so its
        // window (title bar drag, etc.) can react to the mouse.
        if (g_visible.load() != 0)
        {
            // Let ImGui update its internal input state for every message.
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

            // Determine whether this is a mouse message.
            bool isMouseMessage = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
                                  msg == WM_NCMOUSEMOVE || msg == WM_NCLBUTTONDOWN ||
                                  msg == WM_NCRBUTTONDOWN;

            // When the cursor is over the Bronco window, ImGui wants the mouse.
            // Consume the message so it does NOT reach the game (prevents the
            // camera from moving / target from being selected while dragging the
            // panel). When ImGui does not want the mouse, fall through so the game
            // gets normal camera/click behavior.
            if (isMouseMessage && ImGui::GetCurrentContext() != nullptr &&
                ImGui::GetIO().WantCaptureMouse)
            {
                return 0;
            }
        }

        // Pass the message (keyboard + non-captured mouse) to the game.
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

    // Persist the window position/size in a bronco_imgui.ini next to our DLL.
    // ImGui keeps the char* pointer without copying it, so the backing string
    // must outlive the context (function-local static).
    {
        std::string& iniPath = iniPathStorage();
        iniPath = resolveIniPath();
        if (!iniPath.empty())
        {
            io.IniFilename = iniPath.c_str();
            bronco::log::info(("overlay::initialize: imgui ini path = " + iniPath).c_str());
        }
        else
        {
            bronco::log::error("overlay::initialize: could not resolve ini path, using default");
        }
    }

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

    ImGuiIO& io = ImGui::GetIO();

    // --- Draggable Bronco window ---
    // A real ImGui window with a title bar so the user can drag it anywhere.
    // It is semi-transparent and shows the active translations (or a waiting
    // message when there are none). Position/size persist via imgui.ini.
    {
        // First-time placement only: default to the top-right corner. ImGui will
        // override this with the saved position from bronco_imgui.ini if present.
        ImGui::SetNextWindowSize(ImVec2(340.0f, 240.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x - 360.0f, 20.0f), ImGuiCond_FirstUseEver);

        // Semi-transparent background for this window only.
        ImGui::SetNextWindowBgAlpha(0.65f);

        // Draggable window: NO NoInputs / NoMove flags, so the title bar can be
        // grabbed with the mouse.
        if (ImGui::Begin("Bronco - Tradutor##BroncoWindow", nullptr,
            ImGuiWindowFlags_NoCollapse))
        {
            // Welcome message for the first 5 seconds.
            ULONGLONG elapsed = GetTickCount64() - g_initTimestamp;
            if (elapsed < 5000)
            {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                    "Overlay ativo! Pressione F8 para toggle.");
                ImGui::Separator();
            }

            std::lock_guard<std::mutex> lock(g_translationMutex);

            if (g_translations.empty())
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                    "Aguardando texto para traduzir...");
            }
            else
            {
                for (const auto& entry : g_translations)
                {
                    ImGui::TextWrapped("%s", entry.translated.c_str());
                }
            }
        }
        ImGui::End();
    }

    // Render ImGui
    ImGui::Render();

    // --- Save D3D11 pipeline state ---
    // The game may have set custom blend, rasterizer, or depth stencil states
    // that prevent our overlay from rendering. Save and restore around our draw.
    ID3D11RenderTargetView* savedRTV = nullptr;
    ID3D11DepthStencilView* savedDSV = nullptr;
    g_context->OMGetRenderTargets(1, &savedRTV, &savedDSV);

    D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_context->RSGetViewports(&numViewports, savedViewports);

    ID3D11BlendState* savedBlendState = nullptr;
    FLOAT savedBlendFactor[4] = {};
    UINT savedSampleMask = 0;
    g_context->OMGetBlendState(&savedBlendState, savedBlendFactor, &savedSampleMask);

    ID3D11RasterizerState* savedRasterizerState = nullptr;
    g_context->RSGetState(&savedRasterizerState);

    ID3D11DepthStencilState* savedDepthStencilState = nullptr;
    UINT savedStencilRef = 0;
    g_context->OMGetDepthStencilState(&savedDepthStencilState, &savedStencilRef);

    // --- Set up state for ImGui rendering ---
    // Set our render target (no depth stencil - overlay is always on top)
    g_context->OMSetRenderTargets(1, &g_renderTargetView, nullptr);

    // Set viewport matching the backbuffer dimensions (REQUIRED for DX11 to render)
    D3D11_VIEWPORT vp = {};
    vp.Width = io.DisplaySize.x;
    vp.Height = io.DisplaySize.y;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    g_context->RSSetViewports(1, &vp);

    // Clear blend state so our alpha blending works correctly
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    // Clear rasterizer state (no culling, no scissor override from game)
    g_context->RSSetState(nullptr);

    // Clear depth stencil state (we do not use depth testing)
    g_context->OMSetDepthStencilState(nullptr, 0);

    // Draw ImGui
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Log first successful render frame
    {
        static bool firstRenderLogged = false;
        if (!firstRenderLogged)
        {
            bronco::log::info("overlay::render: first successful render frame");
            firstRenderLogged = true;
        }
    }

    // --- Restore D3D11 pipeline state ---
    g_context->OMSetRenderTargets(1, &savedRTV, savedDSV);
    if (numViewports > 0)
        g_context->RSSetViewports(numViewports, savedViewports);
    g_context->OMSetBlendState(savedBlendState, savedBlendFactor, savedSampleMask);
    g_context->RSSetState(savedRasterizerState);
    g_context->OMSetDepthStencilState(savedDepthStencilState, savedStencilRef);

    // Release COM references from Get* calls
    if (savedRTV) savedRTV->Release();
    if (savedDSV) savedDSV->Release();
    if (savedBlendState) savedBlendState->Release();
    if (savedRasterizerState) savedRasterizerState->Release();
    if (savedDepthStencilState) savedDepthStencilState->Release();
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
