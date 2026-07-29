#ifndef GAME_MWACCESSIBILITY_VERTICALSHAFT_H
#define GAME_MWACCESSIBILITY_VERTICALSHAFT_H

#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

namespace MWWorld
{
    class Ptr;
}

namespace MWAccessibility
{
    // Detection of LEVITATION SHAFTS from a cell's own architecture.
    //
    // Telvanni towers (and the player strongholds built from the same kit) are
    // designed around a central vertical shaft you are meant to fly up and down:
    // there are no stairs between most levels. That structure is stated plainly
    // by the cell's static refs -- in_t_s_shaft_6way, in_t_s_shaft_01,
    // in_t_s_shaft_vconnect, in_t_s_hallshaft_cap and friends, all stacked on one
    // (x, y) axis -- but nothing in the game surfaces it, so a blind player has no
    // way to find the shaft and auto-walk has no reliable way to cross floors.
    //
    // Reading the ARCHITECTURE rather than the pathgrid is deliberate. A cell's
    // pathgrid is a walker's artefact: hand-authored, often sparse, and in Tel
    // Uvirith's upper tower it has no connected route at all above z~900, so
    // routing that depends on it silently fails from the top of the tower. The
    // shaft pieces have no such gaps -- they ARE the building -- and because
    // Bethesda's interior kits are shared, recognising the kit works in every
    // tower built from it, vanilla or modded, with no per-cell knowledge.
    //
    // This module is deliberately pure: it takes refIds and positions and returns
    // plain data, with no MWWorld/MWBase dependencies, so the fiddly part (which
    // pieces imply a floor opening, how levels are derived) can be unit-tested
    // instead of only being checkable in game. See
    // apps/openmw_tests/mwaccessibility/verticalshaft.cpp.

    // One architectural piece considered by the detector.
    struct ShaftPiece
    {
        std::string_view mRefId; // lower-case refId, e.g. "in_t_s_shaft_6way"
        osg::Vec3f mPos;
    };

    // Classification of a single piece.
    enum class ShaftPieceKind
    {
        NotShaft, // not part of a shaft at all
        Segment, // a plain vertical section of shaft
        Opening, // a junction you can enter/leave the shaft through
        Cap, // a closed end (floor or ceiling of the shaft)
    };

    // Classify a refId. Case-insensitive on ASCII. Recognises the Telvanni
    // interior kit ("in_t_s_shaft_*", "in_t_s_hallshaft_*"); other tilesets can be
    // added here and every caller benefits at once.
    ShaftPieceKind classifyShaftPiece(std::string_view refId);

    // A detected shaft: a set of shaft pieces sharing one vertical axis.
    struct VerticalShaft
    {
        // Horizontal axis of the shaft (the column's x/y).
        float mX = 0.f;
        float mY = 0.f;
        // Vertical span covered by the shaft pieces.
        float mBottom = 0.f;
        float mTop = 0.f;
        // Heights at which the shaft can be entered or left, ascending. Derived
        // from Opening pieces; a shaft with no detected openings is still useful
        // (you can often enter anywhere along it) but we report what we know.
        std::vector<float> mOpenings;
        // How many pieces made up this shaft -- a confidence signal, so callers
        // can ignore a lone incidental piece.
        int mPieceCount = 0;
    };

    // Group \p pieces into shafts by shared vertical axis.
    //
    // \p axisTolerance is how far apart (horizontally) two pieces may be and still
    // count as the same column. Kit pieces on one axis share an exact position in
    // practice, but mod-placed ones can be nudged slightly, so a small tolerance
    // avoids splitting one shaft into several.
    //
    // Only groups with at least \p minPieces entries are returned, which filters
    // out a stray decorative piece that isn't a usable shaft. Results are sorted
    // by descending piece count, so the most substantial shaft comes first.
    std::vector<VerticalShaft> detectShafts(
        const std::vector<ShaftPiece>& pieces, float axisTolerance = 48.f, int minPieces = 2);

    // Pick the shaft most useful for travelling from \p fromZ to \p toZ, or
    // nullptr when none of \p shafts spans the journey. "Useful" means the shaft
    // actually covers both heights (within \p slack, since you can fly a little
    // beyond the last piece) and, among those, is nearest horizontally to
    // \p fromX / \p fromY. Returns a pointer into \p shafts.
    const VerticalShaft* bestShaftForTravel(const std::vector<VerticalShaft>& shafts, float fromX, float fromY,
        float fromZ, float toZ, float slack = 256.f);

    // The opening of \p shaft nearest to \p z, for choosing where to leave the
    // shaft. Returns \p z itself when the shaft has no recorded openings (fly to
    // the target's own height and try there).
    float nearestOpening(const VerticalShaft& shaft, float z);

    // Something solid found in a shaft's column.
    struct ShaftObstruction
    {
        bool mBlocked = false;
        float mZ = 0.f; // height of the obstruction
    };

    // Does an obstruction at \p obstructionZ actually stand in the way of
    // travelling from \p fromZ to \p toZ?
    //
    // The subtlety is which end gets the benefit of the doubt. At the
    // DESTINATION end, solid ground is expected -- it's the floor you mean to
    // land on -- so anything within \p slack of \p toZ is not a blockage. At the
    // STARTING end there is no such grace: a platform parked directly under your
    // feet is precisely the thing that stops you descending, and reporting it is
    // the whole point.
    //
    // Works in both directions; ascending simply mirrors the reasoning (a ceiling
    // right at the destination is the floor of the level you're rising into).
    bool obstructionBlocksJourney(float obstructionZ, float fromZ, float toZ, float slack = 32.f);

    // Look for something solid in \p shaft's column between \p fromZ and \p toZ.
    //
    // Telvanni shafts are not always open: the player-stronghold kit runs a
    // rideable elevator platform up the same column, and a platform parked on
    // another floor seals the shaft completely. Without this, auto-walk steers
    // into the shaft and grinds against the underside of the platform forever --
    // there is nothing to hear and nothing to see, which is the worst possible
    // failure for a speech-only interface. Probing first lets callers say "the
    // shaft is blocked" and stop, instead of failing mutely.
    //
    // Reports the FIRST obstruction from \p fromZ, so the reported height is the
    // one that matters to a traveller starting there.
    ShaftObstruction probeShaftObstruction(
        const MWWorld::Ptr& player, const VerticalShaft& shaft, float fromZ, float toZ);

    // Detect the shafts in \p player's current cell. This is the one engine-facing
    // entry point in this module: it walks the cell's refs (shaft pieces are
    // STATICS, which the scanner's categories deliberately skip) and feeds them to
    // detectShafts. Shared by the scanner's readout and the auto-walker's descent
    // so the two can never disagree about where the shaft is. Callers should cache
    // the result per cell -- this visits every ref in the cell.
    std::vector<VerticalShaft> collectCellShafts(const MWWorld::Ptr& player);
}

#endif
