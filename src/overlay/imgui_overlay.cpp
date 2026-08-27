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

    // Snapshot of whether the overlay is currently "unlocked" for mouse
    // interaction. The overlay is 100% passive by default and only captures the
    // mouse while the ALT modifier is held AND the overlay is visible. This
    // atomic is written once per frame from render() (on the render thread) and
    // read from hookedWndProc (on the window thread), giving the WndProc a
    // stable, self-consistent value. When false, no mouse message is EVER
    // consumed, so the game always keeps full mouse control; the previous model
    // (consume whenever the cursor was over the panel) still fought the game for
    // the mouse, so it was replaced by this ALT-gated model.
    std::atomic<bool> g_captureMouse{false};

    /// Returns true when the ALT modifier key is currently held down. The high
    /// bit of GetKeyState indicates the key is pressed. VK_MENU is the virtual
    /// key for either ALT key. This is the single source of truth for the
    /// "unlock" gesture used by both render() and hookedWndProc.
    bool isAltHeld()
    {
        return (GetKeyState(VK_MENU) & 0x8000) != 0;
    }
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

        // Determine whether this is a mouse message up front so every mouse
        // code path can be reasoned about explicitly.
        const bool isMouseMessage =
            (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
            msg == WM_NCMOUSEMOVE || msg == WM_NCLBUTTONDOWN ||
            msg == WM_NCRBUTTONDOWN;

        // The overlay is "unlocked" only while it is visible AND the user is
        // holding ALT. This is the single gate that decides whether we are
        // allowed to capture the mouse for the draggable panel. When it is
        // false the overlay is 100% passive and NEVER consumes a mouse message,
        // so the game always keeps full mouse control (this is the whole point
        // of the fix). We read the per-frame atomic snapshot from render() and
        // also re-check ALT live here so a release of ALT is honored instantly.
        const bool unlocked =
            (g_visible.load() != 0) && g_captureMouse.load() && isAltHeld();

        if (unlocked)
        {
            // Unlocked: hand the message to ImGui so the panel can react to the
            // mouse (title-bar drag, etc.) ...
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

            // ... and consume mouse messages so the drag does not leak into the
            // game camera. Non-mouse messages still fall through below.
            if (isMouseMessage)
            {
                return 0;
            }
        }
        else if (isMouseMessage && g_visible.load() != 0)
        {
            // Locked but visible: keep ImGui's internal mouse position roughly
            // current by forwarding WM_MOUSEMOVE only, but NEVER consume it. We
            // deliberately do NOT return 0 for any mouse message here so control
            // always falls through to CallWindowProcW below and the game keeps
            // the mouse. Non-move mouse messages are left entirely untouched.
            if (msg == WM_MOUSEMOVE)
            {
                ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            }
        }

        // Every non-captured message (all keyboard except the consumed toggle
        // hotkey, and ALL mouse messages whenever ALT is not held) reaches the
        // game here. When ALT is not held there is no path that returns 0 for a
        // mouse message, so the game never loses mouse control.
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

    // If not visible, do not render anything (respect toggle). Also clear the
    // mouse-capture snapshot so hookedWndProc never consumes mouse messages
    // while the overlay is hidden (the game must be in full "game mode").
    if (!g_visible.load())
    {
        g_captureMouse.store(false);
        return;
    }

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

    // The panel is passive by default: it is only movable / interactive while
    // the user holds ALT ("unlocked"). This is read once here and used both to
    // choose the window flags below and to update the per-frame capture
    // snapshot after the window is built, so it must be declared in the
    // function scope (not inside the window block).
    const bool altHeld = isAltHeld();

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

        // When ALT is not held (locked) we add NoMove + NoInputs so the window
        // cannot be dragged and swallows no mouse input, keeping the overlay
        // 100% passive. When ALT is held we use the movable flags so the title
        // bar can be dragged.
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;
        if (!altHeld)
        {
            windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
        }

        if (ImGui::Begin("Bronco - Tradutor##BroncoWindow", nullptr, windowFlags))
        {
            // Welcome message for the first 5 seconds.
            ULONGLONG elapsed = GetTickCount64() - g_initTimestamp;
            if (elapsed < 5000)
            {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                    "Overlay ativo! Pressione F8 para toggle.");
                ImGui::Separator();
            }

            // Persistent hint so the user always knows how to move the panel.
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Segure ALT para mover o painel. F8 liga/desliga.");
            ImGui::Separator();

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
                    if (entry.matched)
                    {
                        // Dictionary match: show the translation prominently in
                        // the default bright text color.
                        ImGui::TextWrapped("%s", entry.translated.c_str());
                    }
                    else
                    {
                        // Unmatched line: show the raw recognized OCR text in a
                        // dimmer gray so the user can see OCR is working even
                        // when there is no dictionary entry for the line.
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                        ImGui::TextWrapped("%s", entry.original.c_str());
                        ImGui::PopStyleColor();
                    }
                }
            }
        }
        ImGui::End();
    }

    // Snapshot the "unlocked" state for this frame so hookedWndProc (running on
    // the window thread) reads a stable value. The panel is only unlocked for
    // mouse capture while the ALT modifier is held; otherwise the overlay is
    // fully passive and hookedWndProc will never consume a mouse message. The
    // WndProc also re-checks ALT live, so releasing ALT is honored immediately
    // even before the next frame updates this atomic.
    g_captureMouse.store(altHeld);

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
    int previous = g_visible.fetch_xor(1);

    // If we just hid the overlay (previous state was visible), immediately clear
    // the mouse-capture snapshot so the game returns to "game mode" right away
    // instead of waiting for the next render frame.
    if (previous != 0)
    {
        g_captureMouse.store(false);
    }
}

bool isVisible()
{
    return g_visible.load() != 0;
}

HWND gameWindow()
{
    return g_hwnd;
}

} // namespace bronco::overlay
