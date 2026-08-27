#include "dictionary.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace bronco::translation {

// --- Dictionary ---

std::string Dictionary::normalize(const std::string& text)
{
    // Normalize for robust matching against imperfect OCR:
    //  1. lowercase every character,
    //  2. collapse every run of whitespace (space, tab, CR, newline, etc.)
    //     into a single ASCII space,
    //  3. trim leading/trailing space.
    // Keys are stored using this same normalize() in loadFromFile(), so both
    // the stored keys and the query text pass through identical rules.
    std::string result;
    result.reserve(text.size());

    bool pendingSpace = false; // a whitespace run is pending; emit one space
    bool seenNonSpace = false; // suppress leading spaces entirely

    for (unsigned char c : text)
    {
        if (std::isspace(c))
        {
            // Mark that a space should separate the next non-space token,
            // but only if we have already emitted a non-space character.
            if (seenNonSpace)
            {
                pendingSpace = true;
            }
            continue;
        }

        if (pendingSpace)
        {
            result.push_back(' ');
            pendingSpace = false;
        }

        result.push_back(static_cast<char>(std::tolower(c)));
        seenNonSpace = true;
    }

    // Any pendingSpace here is a trailing whitespace run, which we intentionally
    // drop (trim trailing).
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

std::optional<std::string> Dictionary::lookupContains(const std::string& sourceText) const
{
    const std::string needle = normalize(sourceText);
    if (needle.empty()) return std::nullopt;

    // Performance note: this is a linear scan over all entries (~6000 for the
    // pt-br skills dictionary) per queried line. That is acceptable for the
    // current per-frame line counts; if it ever becomes a hotspot, an index
    // (e.g. a token/substring inverted index) would replace this scan.
    const std::string* bestValue = nullptr;
    std::size_t bestKeyLen = 0;

    for (const auto& [key, value] : m_entries)
    {
        // Keys are already normalized (stored via normalize() in loadFromFile).
        if (key.size() < 4) continue;

        // Match if the source text contains the key, OR the key contains the
        // source text. Prefer the longest key (most specific match).
        const bool contains =
            (needle.find(key) != std::string::npos) ||
            (key.find(needle) != std::string::npos);

        if (contains && key.size() > bestKeyLen)
        {
            bestKeyLen = key.size();
            bestValue = &value;
        }
    }

    if (bestValue != nullptr)
    {
        return *bestValue;
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
    // Single routing point for all dictionary matching. For each category, try
    // the exact fast path first (lookup), then the fuzzy substring matcher
    // (lookupContains). Return the first category that yields any match,
    // preferring exact over contains within a single dictionary.
    for (const auto& [category, dict] : m_dictionaries)
    {
        auto exact = dict.lookup(sourceText);
        if (exact.has_value())
        {
            return exact;
        }

        auto fuzzy = dict.lookupContains(sourceText);
        if (fuzzy.has_value())
        {
            return fuzzy;
        }
    }
    return std::nullopt;
}

std::optional<std::string> DictionaryManager::translate(const std::string& sourceText, Category category) const
{
    auto it = m_dictionaries.find(category);
    if (it != m_dictionaries.end())
    {
        // Exact first, then fuzzy substring match on this one dictionary.
        auto exact = it->second.lookup(sourceText);
        if (exact.has_value())
        {
            return exact;
        }
        return it->second.lookupContains(sourceText);
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
