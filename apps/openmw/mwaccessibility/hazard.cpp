#include "hazard.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <utility>

#include <components/esm3/loadscpt.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/ptr.hpp"

namespace MWAccessibility
{
    namespace
    {
        // Strip a Morrowind script comment: everything from an unquoted ';' to
        // the end of the line. Done per line before looking for a damage verb so
        // commented-out code can never be mistaken for a live hazard.
        std::string_view stripComment(std::string_view line)
        {
            bool inQuotes = false;
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '"')
                    inQuotes = !inQuotes;
                else if (line[i] == ';' && !inQuotes)
                    return line.substr(0, i);
            }
            return line;
        }

        // Parse the numeric argument that follows a damage verb, e.g. the "20.0"
        // in `HurtStandingActor, 20.0`. The comma is optional in Morrowind
        // script syntax and spacing is arbitrary. Returns 0 when no number is
        // present, which is a real possibility in a malformed mod script -- the
        // caller still treats the object as a hazard (the verb IS there), just
        // with an unknown rate.
        float parseDamageArgument(std::string_view rest)
        {
            std::size_t i = 0;
            while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t' || rest[i] == ','))
                ++i;
            const std::size_t start = i;
            while (i < rest.size()
                && ((rest[i] >= '0' && rest[i] <= '9') || rest[i] == '.' || rest[i] == '-' || rest[i] == '+'))
                ++i;
            if (i == start)
                return 0.f;
            // Hand-rolled rather than std::from_chars/strtof: the input is a
            // view into a larger buffer (not null-terminated) and this avoids
            // both a copy and a locale-dependent decimal separator.
            float value = 0.f;
            float sign = 1.f;
            std::size_t j = start;
            if (rest[j] == '-')
            {
                sign = -1.f;
                ++j;
            }
            else if (rest[j] == '+')
                ++j;
            for (; j < i && rest[j] != '.'; ++j)
                value = value * 10.f + static_cast<float>(rest[j] - '0');
            if (j < i && rest[j] == '.')
            {
                float scale = 0.1f;
                for (++j; j < i; ++j)
                {
                    value += static_cast<float>(rest[j] - '0') * scale;
                    scale *= 0.1f;
                }
            }
            return sign * value;
        }

        // Does `line` invoke `verb` as a statement? Requires the verb to be
        // followed by a separator (space, tab or comma) or end of line, so a
        // longer identifier that merely starts with the verb's name doesn't
        // match.
        bool lineInvokes(std::string_view line, std::string_view verb, std::string_view& restOut)
        {
            const std::size_t at = Misc::StringUtils::ciFind(line, verb);
            if (at == std::string_view::npos)
                return false;
            // Must not be preceded by an identifier character, or we would match
            // the tail of some longer word.
            if (at > 0)
            {
                const char prev = line[at - 1];
                if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_')
                    return false;
            }
            const std::size_t after = at + verb.size();
            if (after < line.size())
            {
                const char next = line[after];
                if (next != ' ' && next != '\t' && next != ',')
                    return false;
            }
            restOut = line.substr(std::min(after, line.size()));
            return true;
        }

        // Substance names we can recognise from a refId or mesh path. Keyed on
        // the vocabulary Bethesda's own assets use, which mods overwhelmingly
        // reuse (a modder placing lava places in_lava_*, or names their own mesh
        // with "lava" in it). Anything unrecognised stays "hazard" rather than
        // being guessed at.
        struct SubstanceHint
        {
            std::string_view mKey;
            std::string_view mName;
        };

        constexpr SubstanceHint kSubstances[] = {
            { "lava", "Lava" },
            { "magma", "Lava" },
            { "acid", "Acid" },
            { "poison", "Poison" },
            { "slime", "Slime" },
            { "fire", "Fire" },
            { "flame", "Fire" },
            { "steam", "Steam" },
            { "spike", "Spikes" },
            { "blade", "Blades" },
        };
    }

    HazardEffect parseHazardScript(std::string_view scriptText)
    {
        HazardEffect result;
        std::size_t pos = 0;
        while (pos <= scriptText.size())
        {
            std::size_t end = scriptText.find('\n', pos);
            if (end == std::string_view::npos)
                end = scriptText.size();
            std::string_view line = stripComment(scriptText.substr(pos, end - pos));
            pos = end + 1;

            std::string_view rest;
            HazardContact contact = HazardContact::None;
            if (lineInvokes(line, "HurtStandingActor", rest))
                contact = HazardContact::Standing;
            else if (lineInvokes(line, "HurtCollidingActor", rest))
                contact = HazardContact::Colliding;
            if (contact == HazardContact::None)
                continue;

            const float dps = parseDamageArgument(rest);
            // Keep the WORST call in a script that has several: a hazard which
            // escalates should be ranked by what it can do at its worst.
            if (!result.isHazard() || dps > result.mDamagePerSecond)
            {
                result.mContact = contact;
                result.mDamagePerSecond = dps;
            }
            if (end == scriptText.size())
                break;
        }
        return result;
    }

    std::string hazardDisplayName(std::string_view objectName, std::string_view refId, std::string_view model)
    {
        // An author-given name always wins: it is the word a sighted player
        // would see in the tooltip, so it is the word to speak.
        if (!objectName.empty())
            return std::string(objectName);

        for (const SubstanceHint& hint : kSubstances)
            if (Misc::StringUtils::ciFind(refId, hint.mKey) != std::string_view::npos
                || Misc::StringUtils::ciFind(model, hint.mKey) != std::string_view::npos)
                return std::string(hint.mName);

        // Unrecognised and unnamed. "Hazard" is vague but true, and the
        // announcement still carries direction and distance, which is what the
        // player acts on. Speaking a record id here would be worse than useless.
        return "Hazard";
    }

    std::vector<HazardGroup> groupHazards(
        const std::vector<HazardObject>& objects, const osg::Vec3f& from, float mergeRadius)
    {
        std::vector<HazardGroup> groups;
        if (objects.empty())
            return groups;

        // Single-link clustering: a piece joins a group if it is within
        // mergeRadius of ANY piece already in it, so a long tiled channel of
        // lava merges along its length rather than only within one radius of
        // where it started.
        std::vector<bool> used(objects.size(), false);
        const float mergeRadius2 = mergeRadius * mergeRadius;

        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            if (used[i])
                continue;
            used[i] = true;
            std::vector<std::size_t> members{ i };
            // Grow the cluster until no unused piece touches it.
            for (std::size_t scan = 0; scan < members.size(); ++scan)
            {
                for (std::size_t j = 0; j < objects.size(); ++j)
                {
                    if (used[j])
                        continue;
                    // Group only pieces of the SAME substance: a fire jet
                    // sitting over a lava pool are two different warnings, and
                    // merging them would speak one name for both.
                    if (!Misc::StringUtils::ciEqual(objects[j].mName, objects[members[scan]].mName))
                        continue;
                    if ((objects[j].mPos - objects[members[scan]].mPos).length2() <= mergeRadius2)
                    {
                        used[j] = true;
                        members.push_back(j);
                    }
                }
            }

            HazardGroup group;
            group.mName = objects[members.front()].mName;
            group.mPieceCount = static_cast<int>(members.size());
            // Stable identity: the group's seed piece. Independent of \p from,
            // so a caller can recognise this pool again on a later frame from a
            // different standing position.
            group.mIdentityPos = objects[members.front()].mPos;
            group.mPieces.reserve(members.size());
            float bestDist2 = -1.f;
            for (const std::size_t m : members)
            {
                group.mPieces.push_back(objects[m].mPos);
                const float d2 = (objects[m].mPos - from).length2();
                if (bestDist2 < 0.f || d2 < bestDist2)
                {
                    bestDist2 = d2;
                    group.mNearestPos = objects[m].mPos;
                }
                if (objects[m].mDamagePerSecond > group.mDamagePerSecond)
                    group.mDamagePerSecond = objects[m].mDamagePerSecond;
                // Standing damage is the more urgent contact type for someone
                // walking, so it wins when a group somehow mixes both.
                if (group.mContact == HazardContact::None || objects[m].mContact == HazardContact::Standing)
                    group.mContact = objects[m].mContact;
            }
            group.mNearestDistance = std::sqrt(std::max(0.f, bestDist2));
            groups.push_back(std::move(group));
        }

        std::sort(groups.begin(), groups.end(),
            [](const HazardGroup& a, const HazardGroup& b) { return a.mNearestDistance < b.mNearestDistance; });
        return groups;
    }

    bool hazardGroupContains(const HazardGroup& group, const osg::Vec3f& pos)
    {
        // Exact-ish comparison: these are the same float values that came out of
        // the cell's refs, so a tiny epsilon is enough and avoids matching a
        // genuinely different piece that happens to sit nearby.
        return std::any_of(
            group.mPieces.begin(), group.mPieces.end(), [&](const osg::Vec3f& p) { return (p - pos).length2() < 1.f; });
    }

    std::vector<HazardObject> collectCellHazards(const MWWorld::Ptr& player)
    {
        MWWorld::CellStore* cell = player.getCell();
        if (!cell)
            return {};

        const auto& scripts = MWBase::Environment::get().getESMStore()->get<ESM::Script>();

        // One script backs many placed objects (every lava tile shares the
        // "lava" script), so remember each verdict and parse a given script once
        // per cell scan rather than once per tile.
        std::vector<std::pair<std::string, HazardEffect>> seen;
        std::vector<HazardObject> found;

        cell->forEachConst([&](const MWWorld::ConstPtr& ptr) {
            if (ptr.getCellRef().getCount() <= 0)
                return true;
            const ESM::RefId scriptId = ptr.getClass().getScript(ptr);
            if (scriptId.empty())
                return true;

            // toString(), NOT getRefIdString(): the latter throws for any RefId
            // that isn't string-backed, which would take the game down.
            const std::string scriptKey = scriptId.toString();
            HazardEffect effect;
            const auto cached
                = std::find_if(seen.begin(), seen.end(), [&](const auto& entry) { return entry.first == scriptKey; });
            if (cached != seen.end())
                effect = cached->second;
            else
            {
                if (const ESM::Script* record = scripts.search(scriptId))
                    effect = parseHazardScript(record->mScriptText);
                seen.emplace_back(scriptKey, effect);
            }
            if (!effect.isHazard())
                return true;

            HazardObject object;
            object.mName = hazardDisplayName(
                ptr.getClass().getName(ptr), ptr.getCellRef().getRefId().toString(), ptr.getClass().getModel(ptr));
            object.mPos = ptr.getRefData().getPosition().asVec3();
            object.mDamagePerSecond = effect.mDamagePerSecond;
            object.mContact = effect.mContact;
            found.push_back(std::move(object));
            return true;
        });

        return found;
    }
}
