#pragma once

#include "dictionary.h"
#include "../cache/lru_cache.h"
#include "../ocr/ocr_engine.h"

#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace bronco::translation {

/// Translation result containing original and translated text.
struct TranslationResult {
    std::string original;
    std::string translated;
    Category category;
    bool fromCache = false;
};

/// Main translator that combines OCR results with dictionary lookups.
/// Uses LRU cache to avoid redundant lookups for repeated text.
class Translator {
public:
    Translator();
    ~Translator();

    /// Initialize the translator with dictionaries for the specified locale.
    /// @param dictionaryPath Base path to dictionary files
    /// @param locale Target locale (e.g., "pt-br")
    /// @param cacheCapacity Maximum number of cached translations
    /// @return true if initialization succeeded
    bool initialize(
        const std::string& dictionaryPath,
        const std::string& locale,
        std::size_t cacheCapacity = 5000);

    /// Translate a single text string.
    /// @param sourceText English text to translate
    /// @return Translation result, or std::nullopt if no translation found
    std::optional<TranslationResult> translate(const std::string& sourceText);

    /// Translate a batch of OCR results.
    /// @param ocrResults Results from OCR engine
    /// @return Vector of successful translations
    std::vector<TranslationResult> translateBatch(
        const std::vector<bronco::ocr::OcrResult>& ocrResults);

    /// Change the active locale (reloads dictionaries).
    /// @param locale New locale code
    /// @return true if the new locale was loaded successfully
    bool setLocale(const std::string& locale);

    /// Get the currently active locale.
    const std::string& currentLocale() const;

    /// Clear the translation cache.
    void clearCache();

    /// Get cache statistics.
    std::size_t cacheSize() const;
    std::size_t dictionarySize() const;

private:
    DictionaryManager m_dictionaries;
    bronco::cache::LruCache<std::string, TranslationResult> m_cache;
    std::string m_dictionaryPath;
};

} // namespace bronco::translation
