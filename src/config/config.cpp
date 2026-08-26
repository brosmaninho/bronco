#include "config.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace bronco {

Config& Config::instance()
{
    static Config s_instance;
    return s_instance;
}

bool Config::load()
{
    // Determine the config path relative to the DLL location
    // When placed in the GW2 folder, config/ is a sibling directory
    return load("config/bronco_config.json");
}

bool Config::load(const std::filesystem::path& configPath)
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

bool Config::save() const
{
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

} // namespace bronco
