#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <filesystem>

namespace bronco::translation {

/// A fully reconstructed, PT-BR-translated skill tooltip ready to render.
/// All fields are already translated strings; factLines are FULLY FORMATTED
/// PT-BR lines (e.g. "Dano (x6)", "Duração: 10 segundos", "Recarga: 25 s") so
/// the overlay layer only needs to print them line by line.
struct SkillTooltip {
    std::string nameTranslated;         ///< Skill name (PT-BR, fallback to name_en)
    std::string typeTranslated;         ///< Skill type (e.g. "Profession", "Heal")
    std::string descriptionTranslated;  ///< Skill description (PT-BR)
    std::vector<std::string> notes;     ///< Short notes derived from flags/categories
    std::vector<std::string> factLines; ///< Fully formatted PT-BR fact lines
};

/// Store of reconstructed skill tooltips keyed by the normalized English skill
/// name. Loaded from data/skilldata/<locale>/skills_tooltips.json, which is
/// produced by the Python dictionary-generator tool (FEAT-001). The lookup key
/// uses the SAME normalization as Dictionary::normalize so behavior does not
/// diverge from the existing name matcher.
class SkillTooltipStore {
public:
    SkillTooltipStore() = default;

    /// Load skill tooltips from a JSON file.
    /// Expected format: { "skills": [ { "name_en", "name", "type",
    /// "description", "flags":[], "categories":[], "facts":[ {...} ] }, ... ] }.
    /// Defensive parsing: never throws to the caller (returns false on any
    /// parse failure), mirrors Dictionary::loadFromFile.
    /// @param file Path to skills_tooltips.json
    /// @return true if loaded successfully
    bool loadFromFile(const std::filesystem::path& file);

    /// Look up the full tooltip for an OCR line. Performs an exact
    /// normalized-name match first, then a word-boundary "contains" match on
    /// name_en (reusing Dictionary::containsOnWordBoundary and the same
    /// "substantial key" rule as Dictionary::lookupContains) so a matched OCR
    /// line yields its tooltip directly, independent of the dictionary value.
    /// @param ocrLine The raw or noisy OCR line
    /// @return The reconstructed tooltip if a skill name matched
    std::optional<SkillTooltip> lookup(const std::string& ocrLine) const;

    /// Number of loaded skill tooltips.
    std::size_t size() const;

private:
    // Keyed by normalize(name_en).
    std::unordered_map<std::string, SkillTooltip> m_tooltips;
};

} // namespace bronco::translation
