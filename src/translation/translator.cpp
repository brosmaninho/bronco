#include "translator.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

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

bool Translator::loadSkillTooltips(const std::string& skillDataPath, const std::string& locale)
{
    // File layout mirrors the dictionaries: <skillDataPath>/<locale>/skills_tooltips.json.
    std::filesystem::path file =
        std::filesystem::path(skillDataPath) / locale / "skills_tooltips.json";
    return m_skillTooltips.loadFromFile(file);
}

const SkillTooltipStore& Translator::skillTooltips() const
{
    return m_skillTooltips;
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
        result.matched = true;

        // Cache the result
        m_cache.put(sourceText, result);

        return result;
    }

    return std::nullopt;
}

namespace {
    /// Trim leading/trailing whitespace (space, tab, CR) from a single line.
    std::string trimLine(const std::string& line)
    {
        const char* ws = " \t\r";
        auto start = line.find_first_not_of(ws);
        if (start == std::string::npos) return std::string();
        auto end = line.find_last_not_of(ws);
        return line.substr(start, end - start + 1);
    }
} // anonymous namespace

std::vector<TranslationResult> Translator::translateBatch(
    const std::vector<bronco::ocr::OcrResult>& ocrResults)
{
    std::vector<TranslationResult> results;
    // Each OcrResult may contain multiple lines; reserve a modest amount.
    results.reserve(ocrResults.size() * 4);

    for (const auto& ocr : ocrResults)
    {
        // Split the recognized multi-line blob into individual lines and look up
        // each line independently. This is the core matching fix: 'Plaguelands'
        // on its own line and the description line each resolve via the
        // dictionary, and any unmatched line is still surfaced as raw OCR text.
        std::size_t pos = 0;
        const std::string& text = ocr.text;
        while (pos <= text.size())
        {
            std::size_t nl = text.find('\n', pos);
            std::string rawLine =
                (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);

            std::string line = trimLine(rawLine);
            if (!line.empty())
            {
                auto match = translate(line);
                if (match.has_value())
                {
                    TranslationResult r = std::move(match.value());
                    r.matched = true;
                    results.push_back(std::move(r));
                }
                else
                {
                    // No dictionary match: still surface the raw OCR line so the
                    // user can see OCR is working. translated == original here.
                    TranslationResult r;
                    r.original = line;
                    r.translated = line;
                    r.fromCache = false;
                    r.matched = false;
                    results.push_back(std::move(r));
                }
            }

            if (nl == std::string::npos) break;
            pos = nl + 1;
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
