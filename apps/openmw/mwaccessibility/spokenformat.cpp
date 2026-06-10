#include "spokenformat.hpp"

#include <cmath>
#include <cstdio>

namespace MWAccessibility
{
    std::string formatDistance(float units)
    {
        float metres = units / kUnitsPerMetre;
        char buf[32];
        if (metres < 10.0f)
            std::snprintf(buf, sizeof(buf), "%.1f metres", metres);
        else
            std::snprintf(buf, sizeof(buf), "%d metres", static_cast<int>(metres + 0.5f));
        return buf;
    }

    std::string formatElevation(float dzUnits)
    {
        // ~0.75 m dead-band: a single stair step is well under this, so minor
        // height differences on the "same" level stay silent.
        constexpr float kLevelDeadBand = 52.5f; // ~0.75 m
        if (std::abs(dzUnits) <= kLevelDeadBand)
            return std::string();
        const float metres = std::abs(dzUnits) / kUnitsPerMetre;
        char buf[32];
        if (metres < 10.0f)
            std::snprintf(buf, sizeof(buf), "%.1f metres %s", metres, dzUnits > 0.0f ? "up" : "down");
        else
            std::snprintf(buf, sizeof(buf), "%d metres %s", static_cast<int>(metres + 0.5f),
                dzUnits > 0.0f ? "up" : "down");
        return buf;
    }

    std::string letterForIndex(std::size_t i)
    {
        std::string out;
        ++i; // 1-based for bijective base-26 (A=1).
        while (i > 0)
        {
            std::size_t rem = (i - 1) % 26;
            out.insert(out.begin(), static_cast<char>('A' + rem));
            i = (i - 1) / 26;
        }
        return out;
    }
}
