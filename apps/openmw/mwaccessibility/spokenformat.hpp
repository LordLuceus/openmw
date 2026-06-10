#ifndef GAME_MWACCESSIBILITY_SPOKENFORMAT_H
#define GAME_MWACCESSIBILITY_SPOKENFORMAT_H

#include <cstddef>
#include <string>

namespace MWAccessibility
{
    // Pure, engine-free formatting helpers for spoken output. Kept in their own
    // translation unit (no MWWorld / MWBase / SDL dependencies) so they can be
    // unit-tested in isolation -- the rest of the scanner is fused to engine
    // singletons and can't be. See apps/openmw_tests/mwaccessibility.

    // Morrowind world units: 64 units = 1 yard = 0.9144 metres, so ~70 units
    // per metre. Shared by the scanner's distance/elevation/range maths.
    inline constexpr float kUnitsPerMetre = 69.99f;

    // Spoken distance, e.g. "4.2 metres" (one decimal under 10 m) or "23 metres"
    // (rounded to a whole number at or above 10 m). \p units is a distance in
    // Morrowind world units.
    std::string formatDistance(float units);

    // Spoken vertical offset of a target relative to the player, e.g.
    // "2.0 metres up" / "3 metres down". Returns "" when within roughly one
    // floor-step of level (a ~0.75 m dead-band), so things on the "same" level
    // don't clutter announcements. \p dzUnits is target.z - player.z in world
    // units (positive = target is higher). Same 10 m decimal/whole threshold as
    // formatDistance.
    std::string formatElevation(float dzUnits);

    // Spoken disambiguation suffix for the i-th (0-based) duplicate among
    // same-named objects: A, B, ... Z, then AA, AB, ... for the (rare) case of
    // more than 26 same-named objects. Bijective base-26 (A=1), so there is no
    // "A0"-style gap.
    std::string letterForIndex(std::size_t i);
}

#endif
