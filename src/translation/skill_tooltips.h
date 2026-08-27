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

    /// GW2 skill id. 0 when the dataset predates FEAT-001 (no 'id' field) or
    /// the field was absent/not an integer. Used to key the chain index and to
    /// walk next/prev links.
    long long id = 0;
    /// Optional links to the next / previous skill in a GW2 skill chain. A
    /// skill that is NOT part of a chain has NEITHER set (the JSON keys are
    /// OMITTED, never null), so std::nullopt means "no link" == "not chained".
    std::optional<long long> nextChain;
    std::optional<long long> prevChain;
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

    /// Look up the FULL skill chain for an OCR line, ordered head-to-tail.
    /// Name matching is identical to lookup() (exact then word-boundary
    /// contains). Behavior:
    ///   - nothing matched            -> empty vector
    ///   - matched but not chained    -> single-element vector { matched }
    ///     (a skill with no id, or with neither next_chain nor prev_chain)
    ///   - matched chain member       -> every member of the chain, from the
    ///     head (the member with no prev_chain) forward through next_chain, in
    ///     order. This mirrors reconstruct_chain in the Python generator.
    /// Reconstruction is cycle-safe (visited set + a max-length cap) so a
    /// corrupt dataset can never loop forever. Works on the OLD dataset too:
    /// with no id/chain fields every skill is treated as non-chain and this
    /// returns the single matched tooltip.
    /// @param ocrLine The raw or noisy OCR line
    /// @return The ordered chain (head-to-tail), or a single tooltip, or empty
    std::vector<SkillTooltip> lookupChain(const std::string& ocrLine) const;

    /// Number of loaded skill tooltips.
    std::size_t size() const;

private:
    // Keyed by normalize(name_en).
    std::unordered_map<std::string, SkillTooltip> m_tooltips;
    // Keyed by GW2 skill id (only populated for skills carrying a nonzero id).
    // Used to reconstruct a chain from any matched member.
    std::unordered_map<long long, SkillTooltip> m_byId;
};

} // namespace bronco::translation
