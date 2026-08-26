#pragma once

#include "../ocr/ocr_engine.h"

#include <string>
#include <vector>
#include <filesystem>

namespace bronco {

/// Application configuration loaded from config/bronco_config.json.
/// Singleton pattern for global access.
class Config {
public:
    /// Get the singleton instance.
    static Config& instance();

    /// Load configuration from the default path (config/bronco_config.json).
    /// @return true if loaded successfully
    bool load();

    /// Load configuration from a specific file path.
    /// @return true if loaded successfully
    bool load(const std::filesystem::path& configPath);

    /// Save current configuration to the file it was loaded from.
    /// @return true if saved successfully
    bool save() const;

    // --- Getters ---

    const std::string& targetLocale() const { return m_targetLocale; }
    const std::string& dictionaryPath() const { return m_dictionaryPath; }
    const std::string& tessDataPath() const { return m_tessDataPath; }
    const std::string& ocrLanguage() const { return m_ocrLanguage; }
    std::size_t cacheCapacity() const { return m_cacheCapacity; }
    float fontSize() const { return m_fontSize; }
    int toggleHotkey() const { return m_toggleHotkey; }
    float ocrConfidenceThreshold() const { return m_ocrConfidenceThreshold; }
    int ocrIntervalMs() const { return m_ocrIntervalMs; }
    bool overlayEnabled() const { return m_overlayEnabled; }
    const std::vector<bronco::ocr::ScreenRegion>& ocrRegions() const { return m_ocrRegions; }

    // --- Setters ---

    void setTargetLocale(const std::string& locale) { m_targetLocale = locale; }
    void setFontSize(float size) { m_fontSize = size; }
    void setToggleHotkey(int vkey) { m_toggleHotkey = vkey; }
    void setCacheCapacity(std::size_t capacity) { m_cacheCapacity = capacity; }
    void setOcrConfidenceThreshold(float threshold) { m_ocrConfidenceThreshold = threshold; }
    void setOcrIntervalMs(int ms) { m_ocrIntervalMs = ms; }
    void setOverlayEnabled(bool enabled) { m_overlayEnabled = enabled; }

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::filesystem::path m_configPath;

    // Settings
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
    std::vector<bronco::ocr::ScreenRegion> m_ocrRegions;
};

} // namespace bronco
