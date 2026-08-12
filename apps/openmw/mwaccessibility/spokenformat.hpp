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

    // Pi. Shared by compassLabel and the scanner's yaw-normalisation maths.
    inline constexpr float kPi = 3.14159265358979323846f;
    // Half pi (90 degrees). The engine clamps player pitch (rot[0]) to +/- this;
    // shared by the scanner's pitch-aim stops.
    inline constexpr float kHalfPi = kPi / 2.f;

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

    // 8-point absolute compass label ("north", "northeast", ...) for a
    // world-space bearing \p absYaw in radians, where 0 = +Y = north and the
    // angle increases toward +X = east (matching the engine's atan2(x, y)
    // convention). A fixed reference frame: a given bearing is always the same
    // compass point regardless of which way the player faces. Input need not be
    // pre-normalised -- any real angle is folded into [0, 2*pi) first. Each
    // point covers a 45-degree sector centred on it (north = [-22.5, +22.5)).
    const char* compassLabel(float absYaw);

    // Which of the eight compass sectors a world-space bearing \p absYaw falls
    // in, as an index 0..7 (0 = north, 1 = northeast, ... 7 = northwest) -- the
    // integer counterpart of compassLabel, snapped identically (each sector
    // centred on its point, north = [-22.5, +22.5)). Input need not be
    // pre-normalised. Lets callers compare two bearings for "same compass
    // direction" (e.g. a direction filter) using the exact partition the spoken
    // labels use, so the maths can never disagree with what the player hears.
    int compassSector(float absYaw);

    // Approximate height of one storey in world units (~3.3 m). Used only to
    // bucket objects into vertical "levels" for scanner ordering -- NOT a real
    // measurement of any given building's floor spacing, just a threshold big
    // enough to ignore stairs/ramps/uneven ground (well above formatElevation's
    // ~0.75 m same-level dead-band) yet small enough to separate true storeys.
    inline constexpr float kFloorHeight = 230.0f;

    // Which vertical "level" an object on a given signed height offset from the
    // player sits on, as a signed band index: 0 = the player's own level,
    // +1/+2 = one/two storeys up, -1/-2 = down. \p dzUnits is objectZ - playerZ
    // in world units. Rounded so an object within half a storey of the player's
    // level counts as the same level (band 0), and likewise around each storey.
    int floorBand(float dzUnits);

    // Orders two objects for the scanner's level-grouped listing, returning true
    // if \p a should come before \p b. Objects are first grouped by vertical
    // level (floorBand); the player's OWN level comes first, then the next
    // nearest level outward (one up or one down before two), so cycling the
    // scanner sweeps one storey fully before moving to another instead of
    // bouncing between floors. Within a level, and to break ties between a level
    // the same number of storeys above vs below, the nearer (smaller horizontal
    // distance) object comes first; \p aHorizDist2 / \p bHorizDist2 are SQUARED
    // horizontal (x,y only) distances from the player. \p aDz / \p bDz are each
    // object's signed height offset (objectZ - playerZ). A strict weak ordering,
    // suitable as a std::sort comparator.
    bool lessByLevelThenDistance(float aDz, float aHorizDist2, float bDz, float bHorizDist2);

    // As lessByLevelThenDistance, but with a REACHABLE-FIRST override: an object
    // the player can actually touch from where they stand outranks everything
    // they cannot, regardless of which vertical band it falls in.
    //
    // WHY: floorBand() flips at only half a storey (~1.6 m), so a door raised a
    // step or two above the player -- utterly common in dungeons, where floors
    // are ramped and rooms interlock vertically -- was ranked a whole storey
    // away and sorted behind every object on the player's own level, however
    // distant. The player could reach out and open it, yet had to cycle past the
    // entire room to find it. Level grouping is still the right model for a
    // building with distinct storeys (its whole point is to sweep one floor at a
    // time); it is only wrong about things that are, physically, right here.
    //
    // \p aReachable / \p bReachable are "within activation reach" as the engine
    // computes it (including telekinesis and object bounds), so this mirrors what
    // the player can actually interact with rather than a distance guess. Ties
    // between two reachable (or two unreachable) objects fall through to the
    // normal level-then-distance ordering, keeping a strict weak ordering.
    bool lessByReachThenLevelThenDistance(bool aReachable, float aDz, float aHorizDist2, bool bReachable,
        float bDz, float bHorizDist2);
}

#endif
