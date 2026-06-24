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

    int compassSector(float absYaw)
    {
        // Normalize to [0, 2*PI).
        while (absYaw < 0)
            absYaw += 2 * kPi;
        while (absYaw >= 2 * kPi)
            absYaw -= 2 * kPi;
        // Each 45-degree sector centered on a compass point; offset by half a
        // sector so e.g. north covers [-22.5, +22.5) degrees.
        const float sector = 2 * kPi / 8.0f;
        return static_cast<int>((absYaw + sector / 2) / sector) % 8;
    }

    const char* compassLabel(float absYaw)
    {
        static const char* kPoints[8]
            = { "north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest" };
        return kPoints[compassSector(absYaw)];
    }
}
