#include "config.h"

#include <Windows.h>

#include <nlohmann/json.hpp>
#include <fstream>

namespace bronco {

Config& Config::instance()
{
    static Config s_instance;
    return s_instance;
}

void Config::ensureLoaded()
{
    // Fast path: already loaded (read lock only)
    {
        std::shared_lock<std::shared_mutex> readLock(m_mutex);
        if (m_loaded) return;
    }

    // Slow path: acquire write lock and load
    std::unique_lock<std::shared_mutex> writeLock(m_mutex);
    if (m_loaded) return; // Double-check after acquiring write lock

    // Do not hold the result - best effort load
    loadInternal("config/bronco_config.json");
    m_loaded = true;
}

bool Config::load()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    bool result = loadInternal("config/bronco_config.json");
    m_loaded = true;
    return result;
}

bool Config::load(const std::filesystem::path& configPath)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    bool result = loadInternal(configPath);
    m_loaded = true;
    return result;
}

bool Config::save()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    try
    {
        nlohmann::json json;
        json["target_locale"] = m_targetLocale;
        json["dictionary_path"] = m_dictionaryPath;
        json["tessdata_path"] = m_tessDataPath;
        json["ocr_language"] = m_ocrLanguage;
        json["cache_capacity"] = m_cacheCapacity;
        json["font_size"] = m_fontSize;
        json["toggle_hotkey"] = m_toggleHotkey;
        json["ocr_confidence_threshold"] = m_ocrConfidenceThreshold;
        json["ocr_interval_ms"] = m_ocrIntervalMs;
        json["overlay_enabled"] = m_overlayEnabled;

        // Save OCR regions
        nlohmann::json regions = nlohmann::json::array();
        for (const auto& r : m_ocrRegions)
        {
            nlohmann::json region;
            region["x"] = r.x;
            region["y"] = r.y;
            region["width"] = r.width;
            region["height"] = r.height;
            region["label"] = r.label;
            regions.push_back(region);
        }
        json["ocr_regions"] = regions;

        // Ensure directory exists
        if (m_configPath.has_parent_path())
        {
            std::filesystem::create_directories(m_configPath.parent_path());
        }

        std::ofstream file(m_configPath);
        if (!file.is_open()) return false;

        file << json.dump(4);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// --- Thread-safe getters ---

std::string Config::targetLocale() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_targetLocale;
}

std::string Config::dictionaryPath() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_dictionaryPath;
}

std::string Config::tessDataPath() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_tessDataPath;
}

std::string Config::ocrLanguage() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_ocrLanguage;
}

std::size_t Config::cacheCapacity() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_cacheCapacity;
}

float Config::fontSize() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_fontSize;
}

int Config::toggleHotkey() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_toggleHotkey;
}

float Config::ocrConfidenceThreshold() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_ocrConfidenceThreshold;
}

int Config::ocrIntervalMs() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_ocrIntervalMs;
}

bool Config::overlayEnabled() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_overlayEnabled;
}

std::vector<bronco::ocr::ScreenRegion> Config::ocrRegions() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_ocrRegions;
}

// --- Thread-safe setters ---

void Config::setTargetLocale(const std::string& locale)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_targetLocale = locale;
}

void Config::setFontSize(float size)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_fontSize = size;
}

void Config::setToggleHotkey(int vkey)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_toggleHotkey = vkey;
}

void Config::setCacheCapacity(std::size_t capacity)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_cacheCapacity = capacity;
}

void Config::setOcrConfidenceThreshold(float threshold)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_ocrConfidenceThreshold = threshold;
}

void Config::setOcrIntervalMs(int ms)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_ocrIntervalMs = ms;
}

void Config::setOverlayEnabled(bool enabled)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_overlayEnabled = enabled;
}

// --- Private implementation ---

bool Config::loadInternal(const std::filesystem::path& configPath)
{
    m_configPath = configPath;

    try
    {
        std::ifstream file(configPath);
        if (!file.is_open())
        {
            OutputDebugStringA("[Bronco] Config file not found, using defaults\n");
            return false;
        }

        nlohmann::json json;
        file >> json;

        // Parse settings with fallback to defaults
        if (json.contains("target_locale"))
            m_targetLocale = json["target_locale"].get<std::string>();

        if (json.contains("dictionary_path"))
            m_dictionaryPath = json["dictionary_path"].get<std::string>();

        if (json.contains("tessdata_path"))
            m_tessDataPath = json["tessdata_path"].get<std::string>();

        if (json.contains("ocr_language"))
            m_ocrLanguage = json["ocr_language"].get<std::string>();

        if (json.contains("cache_capacity"))
            m_cacheCapacity = json["cache_capacity"].get<std::size_t>();

        if (json.contains("font_size"))
            m_fontSize = json["font_size"].get<float>();

        if (json.contains("toggle_hotkey"))
            m_toggleHotkey = json["toggle_hotkey"].get<int>();

        if (json.contains("ocr_confidence_threshold"))
            m_ocrConfidenceThreshold = json["ocr_confidence_threshold"].get<float>();

        if (json.contains("ocr_interval_ms"))
            m_ocrIntervalMs = json["ocr_interval_ms"].get<int>();

        if (json.contains("overlay_enabled"))
            m_overlayEnabled = json["overlay_enabled"].get<bool>();

        // Parse OCR regions
        if (json.contains("ocr_regions") && json["ocr_regions"].is_array())
        {
            m_ocrRegions.clear();
            for (const auto& region : json["ocr_regions"])
            {
                bronco::ocr::ScreenRegion r;
                r.x = region.value("x", 0);
                r.y = region.value("y", 0);
                r.width = region.value("width", 0);
                r.height = region.value("height", 0);
                r.label = region.value("label", "");
                m_ocrRegions.push_back(r);
            }
        }

        OutputDebugStringA("[Bronco] Configuration loaded successfully\n");
        return true;
    }
    catch (const std::exception& e)
    {
        std::string msg = "[Bronco] Failed to parse config: ";
        msg += e.what();
        msg += "\n";
        OutputDebugStringA(msg.c_str());
        return false;
    }
}

} // namespace bronco
