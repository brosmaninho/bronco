#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <vector>
#include <filesystem>

namespace bronco::translation {

/// Dictionary categories supported for translation.
enum class Category {
    Items,
    Skills,
    NpcDialogues
};

/// A single dictionary that maps source text to translated text for one category.
class Dictionary {
public:
    Dictionary() = default;

    /// Load translations from a JSON file.
    /// Expected format: { "entries": [ {"en": "...", "translated": "..."}, ... ] }
    /// @param filePath Path to the JSON dictionary file
    /// @return true if loaded successfully
    bool loadFromFile(const std::filesystem::path& filePath);

    /// Look up a translation by source text (case-insensitive).
    /// @param sourceText The English text to translate
    /// @return Translated text if found, std::nullopt otherwise
    std::optional<std::string> lookup(const std::string& sourceText) const;

    /// Get the number of entries in this dictionary.
    std::size_t size() const;

    /// Get the category of this dictionary.
    Category category() const { return m_category; }

    /// Set the category of this dictionary.
    void setCategory(Category cat) { m_category = cat; }

private:
    Category m_category = Category::Items;
    std::unordered_map<std::string, std::string> m_entries;

    /// Normalize a string for case-insensitive lookup.
    static std::string normalize(const std::string& text);
};

/// Manages all dictionaries for a specific locale.
class DictionaryManager {
public:
    DictionaryManager() = default;

    /// Load all dictionary files for a locale from the given base directory.
    /// Looks for items.json, skills.json, npc_dialogues.json in baseDir/<locale>/
    /// @param baseDir Base dictionaries directory (e.g., "data/dictionaries")
    /// @param locale Locale code (e.g., "pt-br")
    /// @return true if at least one dictionary was loaded
    bool loadLocale(const std::filesystem::path& baseDir, const std::string& locale);

    /// Look up a translation across all loaded dictionaries.
    /// @param sourceText English text to translate
    /// @return Translated text if found in any dictionary
    std::optional<std::string> translate(const std::string& sourceText) const;

    /// Look up a translation in a specific category.
    std::optional<std::string> translate(const std::string& sourceText, Category category) const;

    /// Get the currently loaded locale.
    const std::string& currentLocale() const { return m_locale; }

    /// Get total entry count across all dictionaries.
    std::size_t totalEntries() const;

    /// Get list of available locales by scanning the base directory.
    static std::vector<std::string> availableLocales(const std::filesystem::path& baseDir);

private:
    std::string m_locale;
    std::unordered_map<Category, Dictionary> m_dictionaries;
};

} // namespace bronco::translation
