#include "skill_tooltips.h"
#include "dictionary.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace bronco::translation {

namespace {

/// Read a string field from a fact object defensively. Returns empty string if
/// the key is missing or not a string.
std::string jsonStr(const nlohmann::json& obj, const char* key)
{
    if (obj.contains(key) && obj[key].is_string())
    {
        return obj[key].get<std::string>();
    }
    return std::string();
}

/// Format a number as a string without a trailing ".0" for whole values.
/// nlohmann numbers may be int or double; std::to_string(double) yields e.g.
/// "10.000000", which is ugly in a tooltip. We collapse integral doubles.
std::string numToStr(const nlohmann::json& value)
{
    if (value.is_number_integer())
    {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned())
    {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float())
    {
        double d = value.get<double>();
        double rounded = static_cast<double>(static_cast<long long>(d));
        if (d == rounded)
        {
            return std::to_string(static_cast<long long>(d));
        }
        std::string s = std::to_string(d);
        // Trim trailing zeros (and a dangling decimal point) from the float.
        auto dot = s.find('.');
        if (dot != std::string::npos)
        {
            auto last = s.find_last_not_of('0');
            if (last != std::string::npos)
            {
                if (s[last] == '.') --last;
                s.erase(last + 1);
            }
        }
        return s;
    }
    return std::string();
}

/// Return true if `fact` has a numeric member under `key`.
bool hasNumber(const nlohmann::json& fact, const char* key)
{
    return fact.contains(key) && fact[key].is_number();
}

/// Build the label for a fact: prefer translated 'label', fall back to
/// 'label_en', then to 'text'. May be empty.
std::string factLabel(const nlohmann::json& fact)
{
    std::string label = jsonStr(fact, "label");
    if (label.empty()) label = jsonStr(fact, "label_en");
    if (label.empty()) label = jsonStr(fact, "text");
    return label;
}

/// Compose a fully formatted PT-BR line from a single fact. Returns nullopt
/// only when the resulting line would be empty. Handles every fact kind seen
/// in the real GW2 data plus unknown/empty kinds gracefully (fallback to the
/// translated label plus any single present numeric value), so nothing is
/// lost and nothing crashes.
std::optional<std::string> formatFact(const nlohmann::json& fact)
{
    if (!fact.is_object()) return std::nullopt;

    const std::string type = jsonStr(fact, "type");
    const std::string label = factLabel(fact);

    if (type == "Damage" || type == "Heal")
    {
        // Damage/Heal: label plus optional hit count "(xN)" when > 1.
        std::string line = label.empty() ? std::string("Dano") : label;
        if (hasNumber(fact, "hit_count"))
        {
            long long hits = fact["hit_count"].get<long long>();
            if (hits > 1)
            {
                line += " (x" + std::to_string(hits) + ")";
            }
        }
        return line;
    }

    if (type == "Duration" || type == "Time")
    {
        // Duration/Time: label + ": N segundo(s)".
        if (hasNumber(fact, "duration"))
        {
            long long dur = fact["duration"].get<long long>();
            std::string line = label.empty() ? std::string("Duração") : label;
            line += ": " + std::to_string(dur) + " " + (dur == 1 ? "segundo" : "segundos");
            return line;
        }
        return label.empty() ? std::nullopt : std::optional<std::string>(label);
    }

    if (type == "Recharge")
    {
        std::string line = label.empty() ? std::string("Recarga") : label;
        if (hasNumber(fact, "value"))
        {
            line += ": " + numToStr(fact["value"]) + " s";
        }
        return line;
    }

    if (type == "Range" || type == "Number" || type == "Distance" || type == "Radius")
    {
        std::string line = label;
        if (hasNumber(fact, "value"))
        {
            std::string v = numToStr(fact["value"]);
            line = (line.empty() ? v : line + ": " + v);
        }
        else if (hasNumber(fact, "distance"))
        {
            std::string v = numToStr(fact["distance"]);
            line = (line.empty() ? v : line + ": " + v);
        }
        return line.empty() ? std::nullopt : std::optional<std::string>(line);
    }

    if (type == "Percent")
    {
        std::string line = label;
        if (hasNumber(fact, "percent"))
        {
            std::string v = numToStr(fact["percent"]);
            line = (line.empty() ? v + "%" : line + ": " + v + "%");
        }
        return line.empty() ? std::nullopt : std::optional<std::string>(line);
    }

    if (type == "StunBreak" || type == "Unblockable" || type == "NoData")
    {
        // Boolean/marker facts: just the label.
        return label.empty() ? std::nullopt : std::optional<std::string>(label);
    }

    if (type == "Buff" || type == "PrefixedBuff")
    {
        // Buff facts carry the meaningful info in 'status' (translated), with
        // optional apply_count, duration, and a translated 'description'.
        std::string status = jsonStr(fact, "status");
        std::string line = status.empty() ? label : status;
        if (line.empty()) return std::nullopt;

        if (hasNumber(fact, "apply_count"))
        {
            long long count = fact["apply_count"].get<long long>();
            if (count > 1)
            {
                line += " (" + std::to_string(count) + ")";
            }
        }
        if (hasNumber(fact, "duration"))
        {
            line += ": " + numToStr(fact["duration"]) + "s";
        }

        std::string desc = jsonStr(fact, "description");
        if (!desc.empty())
        {
            line += " - " + desc;
        }

        // PrefixedBuff also carries a nested prefix with its own status.
        if (type == "PrefixedBuff" && fact.contains("prefix") && fact["prefix"].is_object())
        {
            const nlohmann::json& prefix = fact["prefix"];
            std::string prefixStatus = jsonStr(prefix, "status");
            if (!prefixStatus.empty())
            {
                line += " [" + prefixStatus + "]";
            }
        }

        return line;
    }

    if (type == "ComboField")
    {
        std::string line = label.empty() ? std::string("Campo de Combo") : label;
        std::string fieldType = jsonStr(fact, "field_type");
        if (!fieldType.empty())
        {
            line += ": " + fieldType;
        }
        return line;
    }

    if (type == "ComboFinisher")
    {
        std::string line = label.empty() ? std::string("Finalizador de Combo") : label;
        std::string finisherType = jsonStr(fact, "finisher_type");
        if (!finisherType.empty())
        {
            line += ": " + finisherType;
        }
        if (hasNumber(fact, "percent"))
        {
            long long pct = static_cast<long long>(fact["percent"].get<double>());
            if (pct != 100)
            {
                line += " (" + numToStr(fact["percent"]) + "%)";
            }
        }
        return line;
    }

    if (type == "AttributeAdjust")
    {
        std::string line = label;
        if (hasNumber(fact, "value"))
        {
            std::string v = numToStr(fact["value"]);
            line = (line.empty() ? v : line + ": " + v);
        }
        std::string target = jsonStr(fact, "target");
        if (!target.empty())
        {
            line += " (" + target + ")";
        }
        return line.empty() ? std::nullopt : std::optional<std::string>(line);
    }

    // Unknown / unlisted kind (e.g. "BuffArray" or empty type): emit the
    // translated label plus any single present numeric value so nothing is
    // lost and nothing crashes.
    {
        std::string line = label;
        std::string value;
        if (hasNumber(fact, "value")) value = numToStr(fact["value"]);
        else if (hasNumber(fact, "duration")) value = numToStr(fact["duration"]);
        else if (hasNumber(fact, "percent")) value = numToStr(fact["percent"]) + "%";
        else if (hasNumber(fact, "distance")) value = numToStr(fact["distance"]);

        if (!value.empty())
        {
            line = (line.empty() ? value : line + ": " + value);
        }
        return line.empty() ? std::nullopt : std::optional<std::string>(line);
    }
}

/// Build the short notes list from a skill's flags[] and categories[]. Values
/// are plain strings; if only English is present it is still shown (never
/// crashes). Each entry becomes its own short note line.
void buildNotes(const nlohmann::json& skill, std::vector<std::string>& notes)
{
    if (skill.contains("flags") && skill["flags"].is_array())
    {
        for (const auto& flag : skill["flags"])
        {
            if (flag.is_string())
            {
                std::string f = flag.get<std::string>();
                if (!f.empty()) notes.push_back(f);
            }
        }
    }
    if (skill.contains("categories") && skill["categories"].is_array())
    {
        for (const auto& cat : skill["categories"])
        {
            if (cat.is_string())
            {
                std::string c = cat.get<std::string>();
                if (!c.empty()) notes.push_back(c);
            }
        }
    }
}

} // anonymous namespace

bool SkillTooltipStore::loadFromFile(const std::filesystem::path& file)
{
    try
    {
        std::ifstream in(file);
        if (!in.is_open()) return false;

        nlohmann::json json;
        in >> json;

        if (!json.contains("skills") || !json["skills"].is_array())
            return false;

        m_tooltips.clear();

        for (const auto& skill : json["skills"])
        {
            if (!skill.is_object()) continue;

            std::string nameEn = jsonStr(skill, "name_en");
            if (nameEn.empty()) continue;

            SkillTooltip tip;
            // nameTranslated falls back to name_en when 'name' is absent.
            tip.nameTranslated = jsonStr(skill, "name");
            if (tip.nameTranslated.empty()) tip.nameTranslated = nameEn;
            tip.typeTranslated = jsonStr(skill, "type");
            tip.descriptionTranslated = jsonStr(skill, "description");

            buildNotes(skill, tip.notes);

            if (skill.contains("facts") && skill["facts"].is_array())
            {
                for (const auto& fact : skill["facts"])
                {
                    auto line = formatFact(fact);
                    if (line.has_value() && !line.value().empty())
                    {
                        tip.factLines.push_back(std::move(line.value()));
                    }
                }
            }

            // Use the SAME normalization as Dictionary::normalize for the key
            // so lookups match the existing name-matching behavior exactly.
            std::string key = Dictionary::normalize(nameEn);
            if (!key.empty())
            {
                m_tooltips[key] = std::move(tip);
            }
        }

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::optional<SkillTooltip> SkillTooltipStore::lookup(const std::string& ocrLine) const
{
    const std::string needle = Dictionary::normalize(ocrLine);
    if (needle.empty()) return std::nullopt;

    // Pass 1: exact normalized-name match.
    auto exact = m_tooltips.find(needle);
    if (exact != m_tooltips.end())
    {
        return exact->second;
    }

    // Pass 2: word-boundary "contains" match on name_en, reusing the same
    // "substantial key" rule as Dictionary::lookupContains (multi-word key of
    // normalized length >= 6, or single word of length >= 8), longest key wins.
    constexpr std::size_t kMultiWordMinLen = 6;
    constexpr std::size_t kSingleWordMinLen = 8;

    const SkillTooltip* best = nullptr;
    std::size_t bestKeyLen = 0;

    for (const auto& [key, tip] : m_tooltips)
    {
        const bool multiWord = key.find(' ') != std::string::npos;
        const std::size_t minLen = multiWord ? kMultiWordMinLen : kSingleWordMinLen;
        if (key.size() < minLen) continue;
        if (key.size() <= bestKeyLen) continue;

        if (Dictionary::containsOnWordBoundary(needle, key))
        {
            bestKeyLen = key.size();
            best = &tip;
        }
    }

    if (best != nullptr)
    {
        return *best;
    }
    return std::nullopt;
}

std::size_t SkillTooltipStore::size() const
{
    return m_tooltips.size();
}

} // namespace bronco::translation
