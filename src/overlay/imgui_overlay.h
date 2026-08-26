#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>

namespace bronco::overlay {

/// A translated text entry to display on the overlay.
struct TranslatedEntry {
    std::string original;
    std::string translated;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/// Initialize Dear ImGui with DirectX 11 backend.
/// @param hwnd Window handle of the game
/// @param device D3D11 device
/// @param context D3D11 device context
/// @return true if initialization succeeded
bool initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);

/// Render the translation overlay for the current frame.
/// @param swapChain The swap chain being presented
void render(IDXGISwapChain* swapChain);

/// Shut down ImGui and release resources.
void shutdown();

/// Set the translations to display on the overlay.
void setTranslations(const std::vector<TranslatedEntry>& entries);

/// Toggle overlay visibility (hotkey callback).
void toggleVisibility();

/// Check if the overlay is currently visible.
bool isVisible();

} // namespace bronco::overlay
