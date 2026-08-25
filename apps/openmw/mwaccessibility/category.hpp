#ifndef GAME_MWACCESSIBILITY_CATEGORY_H
#define GAME_MWACCESSIBILITY_CATEGORY_H

namespace MWAccessibility
{
    /// Categories the screen-reader scanner can cycle through. Shared by the
    /// Scanner and the ProximityCue so neither has to include the other.
    enum class Category
    {
        Npcs = 0,
        Doors,
        Containers,
        Items,
        Activators,
        // Objects revealed by the player's active Detect Creature/Key/
        // Enchantment effects. Unlike the others this isn't a record-type
        // bucket scanned from the loaded cells: its membership comes from the
        // engine's detection query and is empty unless a Detect effect is
        // active, so the scanner skips it when cycling while it's empty.
        Detected,

        // Player-defined waypoints: map notes (custom markers) the player has
        // dropped, plus the Mark spell's location. Unlike every other category
        // these are bare world positions, not objects, so they're navigated via
        // the position-based AutoWalker / ProximityCue paths. Skipped when
        // cycling while empty (no notes in the current cell and no Mark set).
        Waypoints,

        // Discovered global-map locations: named exterior places the player has
        // visited, plus any an NPC marked via ShowMap/FillMap, aggregated one
        // entry per town. Like Waypoints these are bare world positions (not
        // objects), so they share the same position-based AutoWalker /
        // ProximityCue paths. Skipped when cycling while empty (nothing
        // discovered yet).
        Locations,

        // Features of the room itself rather than objects in it: damaging
        // terrain (lava and the like) and levitation shafts, split by
        // subcategory. Like Waypoints/Locations these are bare world positions,
        // so they reuse the position-based AutoWalker / ProximityCue paths --
        // which is the point: "walk me to the shaft" is then just auto-walking
        // to the selected entry, exactly as for any other target, instead of a
        // bespoke key. Skipped when cycling while the current cell has neither.
        Terrain,

        // Everything interactable in one list: the union of Npcs, Doors,
        // Containers, Items and Activators, distance-sorted together. Not
        // Detected/Waypoints/Locations/Terrain -- those aren't cell-scanned
        // record types (the last three are bare positions, and Detected is a
        // query result), so folding them in would mix incompatible entry kinds.
        //
        // Deliberately LAST in the enum despite being first in the key order
        // (Ctrl+0) and the startup default: the marks sidecar stores the
        // category as a raw integer, so inserting a value anywhere earlier
        // would silently repoint every existing save's marks at the wrong
        // category. Cycle order is decoupled from enum order (see kCycleOrder)
        // so this costs nothing at the keyboard.
        All,

        Count
    };
}

#endif
