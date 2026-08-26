#include "translator.h"

#include <algorithm>
#include <cctype>

namespace bronco::translation {

Translator::Translator() = default;
Translator::~Translator() = default;

bool Translator::initialize(
    const std::string& dictionaryPath,
    const std::string& locale,
    std::size_t cacheCapacity)
{
    m_dictionaryPath = dictionaryPath;
    m_cache.resize(cacheCapacity);

    return m_dictionaries.loadLocale(dictionaryPath, locale);
}

std::optional<TranslationResult> Translator::translate(const std::string& sourceText)
{
    if (sourceText.empty()) return std::nullopt;

    // Check cache first
    auto cached = m_cache.get(sourceText);
    if (cached.has_value())
    {
        auto result = cached.value();
        result.fromCache = true;
        return result;
    }

    // Try all dictionaries
    auto translated = m_dictionaries.translate(sourceText);
    if (translated.has_value())
    {
        TranslationResult result;
        result.original = sourceText;
        result.translated = translated.value();
        result.fromCache = false;

        // Cache the result
        m_cache.put(sourceText, result);

        return result;
    }

    return std::nullopt;
}

std::vector<TranslationResult> Translator::translateBatch(
    const std::vector<bronco::ocr::OcrResult>& ocrResults)
{
    std::vector<TranslationResult> results;
    results.reserve(ocrResults.size());

    for (const auto& ocr : ocrResults)
    {
        auto result = translate(ocr.text);
        if (result.has_value())
        {
            results.push_back(std::move(result.value()));
        }
    }

    return results;
}

bool Translator::setLocale(const std::string& locale)
{
    // Clear cache when switching locale
    m_cache.clear();

    return m_dictionaries.loadLocale(m_dictionaryPath, locale);
}

const std::string& Translator::currentLocale() const
{
    return m_dictionaries.currentLocale();
}

void Translator::clearCache()
{
    m_cache.clear();
}

std::size_t Translator::cacheSize() const
{
    return m_cache.size();
}

std::size_t Translator::dictionarySize() const
{
    return m_dictionaries.totalEntries();
}

} // namespace bronco::translation
