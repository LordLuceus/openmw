#ifndef GAME_MWACCESSIBILITY_HAZARD_H
#define GAME_MWACCESSIBILITY_HAZARD_H

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
    // Detection of DAMAGING TERRAIN (lava, acid, fire fields) before the player
    // walks into it.
    //
    // A sighted player sees a lava pool and walks around it. A blind player
    // discovers it by standing in it, and lava deals 20 HP/sec -- often lethal
    // before the "you are taking damage" cue can be acted on, and it damages
    // followers too. The auto-walker already has a REACTIVE net (it watches for
    // sustained non-combat damage and snaps the player back), but that net by
    // definition only fires after the burn has started, and it does nothing at
    // all when the player is walking manually.
    //
    // The key insight is that this hazard IS in the data, ahead of time.
    // OpenMW has no "lava" type -- lava is a magma-textured mesh like any other
    // -- but the DAMAGE is not magic: it comes from a script on the object
    // calling HurtStandingActor / HurtCollidingActor. Vanilla's "lava" script is
    // simply:
    //
    //     HurtStandingActor, 20.0     ;20 pts of damage a sec
    //
    // attached to six activators (in_lava_1024, in_lava_oval, ...). So rather
    // than recognising lava meshes -- which would cover exactly the tilesets we
    // thought to list, and miss every mod's own acid pit -- we look for the
    // damage VERB in the object's script. Anything that hurts you for standing
    // on or touching it is a hazard, whoever authored it and whatever it is
    // made of. This is the level author's own statement of "this will hurt",
    // which is why it generalises to Tamriel Rebuilt and beyond for free.
    //
    // This module is deliberately pure: it takes script text, refIds and
    // positions and returns plain data, with no MWWorld/MWBase dependencies, so
    // the fiddly parts (parsing the damage rate, naming a nameless object,
    // grouping a tiled pool into one announcement) are unit-testable rather than
    // only checkable in game. See apps/openmw_tests/mwaccessibility/hazard.cpp.

    // How an object's script inflicts damage.
    enum class HazardContact
    {
        None, // script does not damage the player
        Standing, // HurtStandingActor: hurts you for standing on it (lava, acid pools)
        Colliding, // HurtCollidingActor: hurts you for touching it (fire jets, blades)
    };

    // What a script does to whoever stands on / touches the object it is on.
    struct HazardEffect
    {
        HazardContact mContact = HazardContact::None;
        // Damage per second as the script declares it. Note this is the
        // author's number, not a prediction of what the player will actually
        // lose (resistances, armour and script conditions all apply), so it is
        // used only to RANK severity, never spoken as a health figure.
        float mDamagePerSecond = 0.f;

        bool isHazard() const { return mContact != HazardContact::None; }
    };

    // Parse \p scriptText (a Morrowind script's source) for a damage verb.
    //
    // Deliberately tolerant of real-world script formatting: comments, tabs,
    // arbitrary spacing, optional comma, any capitalisation, and the verb
    // appearing anywhere in the body. When a script has several damage calls the
    // LARGEST rate wins, so a hazard that escalates is ranked by its worst case.
    //
    // A damage verb inside a COMMENT is ignored: commented-out code is common in
    // mod scripts, and treating it as live would invent hazards that cannot hurt
    // anyone -- exactly the "confident wrong information" a speech-only
    // interface must never produce.
    HazardEffect parseHazardScript(std::string_view scriptText);

    // A hazard-bearing object found in the world.
    struct HazardObject
    {
        std::string mName; // spoken name, already resolved (see hazardDisplayName)
        osg::Vec3f mPos;
        float mDamagePerSecond = 0.f;
        HazardContact mContact = HazardContact::None;
    };

    // A group of adjacent hazard objects, announced as one thing.
    //
    // Large pools are built by tiling several meshes edge to edge (a big lava
    // lake in a Dwemer ruin can be a dozen in_lava_1024 pieces). Announcing each
    // separately would bury the player in "Lava, 3 metres north. Lava, 5 metres
    // north. Lava, 6 metres north" and obscure the one fact that matters: there
    // is lava over there and it is wide. So touching pieces are merged and
    // reported by their NEAREST edge -- the nearest point is what you are about
    // to step in.
    struct HazardGroup
    {
        std::string mName;
        // Position of the group's nearest piece to the reference point it was
        // grouped against (i.e. the closest place the player can be burned).
        // CHANGES as the reference point moves, so it must NOT be used to
        // identify a group between frames -- use mIdentityPos for that.
        osg::Vec3f mNearestPos;
        float mNearestDistance = 0.f; // world units, from that reference point
        // A STABLE identity for this group: the position of its lowest-indexed
        // piece, which does not depend on where the player is standing. Callers
        // that must recognise "this same pool again" on a later frame (e.g. to
        // avoid re-announcing it) key on this. Grouping is deterministic for a
        // given cell, so the same pool yields the same identity every time.
        osg::Vec3f mIdentityPos;
        float mDamagePerSecond = 0.f; // worst piece in the group
        HazardContact mContact = HazardContact::None;
        int mPieceCount = 1;
        // Positions of every piece in the group, so a caller can test whether a
        // remembered identity belongs to this group even if grouping shifted.
        std::vector<osg::Vec3f> mPieces;
    };

    // Is \p pos one of \p group's pieces? Used to match a remembered hazard
    // against the current frame's grouping without depending on which piece is
    // nearest right now.
    bool hazardGroupContains(const HazardGroup& group, const osg::Vec3f& pos);

    // Merge \p objects into groups of touching/adjacent pieces, measured from
    // \p from (normally the player).
    //
    // \p mergeRadius is how close two pieces must be to count as one pool. It
    // wants to be a little over one tile of the biggest common lava mesh
    // (in_lava_1024 is 1024 units across) so a tiled lake merges, while two
    // genuinely separate pools in the same room stay separate.
    //
    // Results are sorted nearest-first, which is the order a player needs: the
    // thing about to burn you comes before the thing across the room.
    std::vector<HazardGroup> groupHazards(
        const std::vector<HazardObject>& objects, const osg::Vec3f& from, float mergeRadius = 1200.f);

    // Spoken name for a hazard object.
    //
    // Hazard objects are usually NAMELESS in the data -- vanilla's lava
    // activators have an empty Name field, so the scanner would fall back to
    // speaking a record id ("in_lava_1024"), which tells a player nothing. We
    // infer a friendly substance name from the refId/model where we recognise
    // it, and otherwise say "hazard": honest, and still actionable, since the
    // announcement always carries direction and distance.
    //
    // \p objectName is the object's own display name and always wins when
    // non-empty -- if an author named it, that is the word the player should
    // hear. \p refId and \p model are used only to guess an unnamed one.
    std::string hazardDisplayName(std::string_view objectName, std::string_view refId, std::string_view model);

    // Collect the hazard objects in \p player's current cell.
    //
    // The one engine-facing entry point in this module: it walks the cell's refs,
    // reads each one's script, and keeps those whose script damages the player.
    // Callers should cache the result per cell -- this visits every ref in the
    // cell and compiles-checks each script's text once.
    std::vector<HazardObject> collectCellHazards(const MWWorld::Ptr& player);
}

#endif
