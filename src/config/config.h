#pragma once

#include "../ocr/ocr_engine.h"

#include <string>
#include <vector>
#include <filesystem>
#include <mutex>
#include <shared_mutex>

namespace bronco {

/// Application configuration loaded from config/bronco_config.json.
/// Singleton pattern for global access. Thread-safe via shared_mutex.
class Config {
public:
    /// Get the singleton instance.
    static Config& instance();

    /// Ensure the config is loaded (deferred from DllMain to avoid loader lock).
    /// Safe to call multiple times; loads only once.
    void ensureLoaded();

    /// Load configuration from the default path (config/bronco_config.json).
    /// @return true if loaded successfully
    bool load();

    /// Load configuration from a specific file path.
    /// @return true if loaded successfully
    bool load(const std::filesystem::path& configPath);

    /// Save current configuration to the file it was loaded from.
    /// @return true if saved successfully
    bool save();

    // --- Getters (thread-safe reads) ---

    std::string targetLocale() const;
    std::string dictionaryPath() const;
    std::string tessDataPath() const;
    std::string ocrLanguage() const;
    std::size_t cacheCapacity() const;
    float fontSize() const;
    int toggleHotkey() const;
    float ocrConfidenceThreshold() const;
    int ocrIntervalMs() const;
    bool overlayEnabled() const;
    bool ocrFollowMouse() const;
    int ocrFollowWidth() const;
    int ocrFollowHeight() const;
    std::vector<bronco::ocr::ScreenRegion> ocrRegions() const;

    // --- Setters (thread-safe writes) ---

    void setTargetLocale(const std::string& locale);
    void setFontSize(float size);
    void setToggleHotkey(int vkey);
    void setCacheCapacity(std::size_t capacity);
    void setOcrConfidenceThreshold(float threshold);
    void setOcrIntervalMs(int ms);
    void setOverlayEnabled(bool enabled);

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    /// Internal load (caller must hold write lock).
    bool loadInternal(const std::filesystem::path& configPath);

    mutable std::shared_mutex m_mutex;
    bool m_loaded = false;
    std::filesystem::path m_configPath;

    // Settings (protected by m_mutex)
    std::string m_targetLocale = "pt-br";
    std::string m_dictionaryPath = "data/dictionaries";
    std::string m_tessDataPath = "data/tessdata";
    std::string m_ocrLanguage = "eng";
    std::size_t m_cacheCapacity = 5000;
    float m_fontSize = 16.0f;
    int m_toggleHotkey = 0x77; // VK_F8
    float m_ocrConfidenceThreshold = 60.0f;
    int m_ocrIntervalMs = 500;
    bool m_overlayEnabled = true;
    // Follow-mouse OCR: capture a region centered on the cursor each cycle.
    // This is the default because GW2 tooltips render where the mouse is.
    // When disabled, the fixed m_ocrRegions list is used as a fallback.
    bool m_ocrFollowMouse = true;
    int m_ocrFollowWidth = 500;
    int m_ocrFollowHeight = 400;
    std::vector<bronco::ocr::ScreenRegion> m_ocrRegions;
};

} // namespace bronco
