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

    /// Look up a translation by source text (case-insensitive, exact match).
    /// The source is normalized (lowercased, whitespace-collapsed, trimmed) and
    /// looked up as a whole-string key in the entry map.
    /// @param sourceText The English text to translate
    /// @return Translated text if found, std::nullopt otherwise
    std::optional<std::string> lookup(const std::string& sourceText) const;

    /// Fuzzy substring lookup for imperfect OCR text.
    /// Normalizes the source text (lowercase + whitespace-collapse + trim), then
    /// scans every entry looking for a key that occurs inside the normalized
    /// source. Matching is intentionally strict to avoid false positives from
    /// common short English verbs that also happen to be skill names
    /// (e.g. "slash", "throw", "leap"):
    ///   1. Only the source-contains-key direction is used (the reverse
    ///      key-contains-source direction was dropped: a tiny OCR fragment must
    ///      not claim a much longer multi-word key).
    ///   2. A key qualifies only if it is "substantial": either it is
    ///      multi-word (contains a space) with normalized length >= 6, or it is
    ///      a single word of length >= 8. Shorter single words are ignored so
    ///      ordinary words cannot hijack a whole line.
    ///   3. The key must occur on WORD BOUNDARIES within the source (bounded by
    ///      start/end-of-string or a space on each side), so "leap" does not
    ///      match inside "leaping" or "please".
    /// Among all qualifying matches the entry with the LONGEST key wins (most
    /// specific match).
    ///
    /// This tolerates OCR reading a known term (e.g. a skill name) with extra
    /// surrounding noise or different spacing/newlines than the dictionary key.
    /// @param sourceText The (possibly noisy) OCR text to translate
    /// @return Translated text of the longest matching key, std::nullopt otherwise
    std::optional<std::string> lookupContains(const std::string& sourceText) const;

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

    /// Return true if `key` occurs inside `source` on word boundaries, i.e.
    /// every occurrence check requires the character immediately before and
    /// after the match to be a space or a string boundary. Both arguments are
    /// expected to already be normalized. Used by lookupContains() so a key
    /// like "leap" matches "use leap now" but not "leaping" or "please".
    static bool containsOnWordBoundary(const std::string& source, const std::string& key);
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
