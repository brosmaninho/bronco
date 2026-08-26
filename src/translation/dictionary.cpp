#include "dictionary.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace bronco::translation {

// --- Dictionary ---

std::string Dictionary::normalize(const std::string& text)
{
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool Dictionary::loadFromFile(const std::filesystem::path& filePath)
{
    try
    {
        std::ifstream file(filePath);
        if (!file.is_open()) return false;

        nlohmann::json json;
        file >> json;

        if (!json.contains("entries") || !json["entries"].is_array())
            return false;

        m_entries.clear();

        for (const auto& entry : json["entries"])
        {
            if (entry.contains("en") && entry.contains("translated"))
            {
                std::string key = normalize(entry["en"].get<std::string>());
                std::string value = entry["translated"].get<std::string>();
                m_entries[key] = value;
            }
        }

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::optional<std::string> Dictionary::lookup(const std::string& sourceText) const
{
    auto it = m_entries.find(normalize(sourceText));
    if (it != m_entries.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::size_t Dictionary::size() const
{
    return m_entries.size();
}

// --- DictionaryManager ---

bool DictionaryManager::loadLocale(const std::filesystem::path& baseDir, const std::string& locale)
{
    m_dictionaries.clear();
    m_locale = locale;

    std::filesystem::path localeDir = baseDir / locale;
    if (!std::filesystem::exists(localeDir))
    {
        return false;
    }

    bool anyLoaded = false;

    // Load items dictionary
    {
        Dictionary dict;
        dict.setCategory(Category::Items);
        if (dict.loadFromFile(localeDir / "items.json"))
        {
            m_dictionaries[Category::Items] = std::move(dict);
            anyLoaded = true;
        }
    }

    // Load skills dictionary
    {
        Dictionary dict;
        dict.setCategory(Category::Skills);
        if (dict.loadFromFile(localeDir / "skills.json"))
        {
            m_dictionaries[Category::Skills] = std::move(dict);
            anyLoaded = true;
        }
    }

    // Load NPC dialogues dictionary
    {
        Dictionary dict;
        dict.setCategory(Category::NpcDialogues);
        if (dict.loadFromFile(localeDir / "npc_dialogues.json"))
        {
            m_dictionaries[Category::NpcDialogues] = std::move(dict);
            anyLoaded = true;
        }
    }

    return anyLoaded;
}

std::optional<std::string> DictionaryManager::translate(const std::string& sourceText) const
{
    // Search all dictionaries in priority order
    for (const auto& [category, dict] : m_dictionaries)
    {
        auto result = dict.lookup(sourceText);
        if (result.has_value())
        {
            return result;
        }
    }
    return std::nullopt;
}

std::optional<std::string> DictionaryManager::translate(const std::string& sourceText, Category category) const
{
    auto it = m_dictionaries.find(category);
    if (it != m_dictionaries.end())
    {
        return it->second.lookup(sourceText);
    }
    return std::nullopt;
}

std::size_t DictionaryManager::totalEntries() const
{
    std::size_t total = 0;
    for (const auto& [_, dict] : m_dictionaries)
    {
        total += dict.size();
    }
    return total;
}

std::vector<std::string> DictionaryManager::availableLocales(const std::filesystem::path& baseDir)
{
    std::vector<std::string> locales;

    if (!std::filesystem::exists(baseDir)) return locales;

    for (const auto& entry : std::filesystem::directory_iterator(baseDir))
    {
        if (entry.is_directory())
        {
            locales.push_back(entry.path().filename().string());
        }
    }

    std::sort(locales.begin(), locales.end());
    return locales;
}

} // namespace bronco::translation
