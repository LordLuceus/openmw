#ifndef OPENMW_MWGUI_ACCESSIBILITY_ACTIVEEFFECTS_H
#define OPENMW_MWGUI_ACCESSIBILITY_ACTIVEEFFECTS_H

#include <string>
#include <vector>

#include "../../mwworld/ptr.hpp"

namespace MWGui::A11y
{
    /// One applied magic effect belonging to a source (spell / item / ability).
    struct ActiveEffectLine
    {
        /// Display name of the source that applied this effect (the spell,
        /// enchanted item or ability name), e.g. "Ancestor Guardian".
        std::string source;

        /// The effect itself: name (with target skill/attribute), live
        /// magnitude and remaining duration, e.g. "Sanctuary 50 points for 58
        /// seconds" or "Resist Fire 50%, permanent".
        std::string effect;
    };

    /// Collect an actor's currently-applied magic effects -- the same set
    /// rendered as the on-screen effect-icon row (SpellIcons): cast spells,
    /// constant enchantments from equipped items, racial/birthsign abilities
    /// (permanent), diseases, potions, summon buffs, etc.
    ///
    /// Effects are returned grouped by source and contiguous (one source's
    /// effects appear together), so a caller can present them with the source
    /// as a section header announced before its effects. Returns empty when
    /// nothing is active.
    ///
    /// Shared so the spells window and the HUD can both voice active effects
    /// from one implementation.
    std::vector<ActiveEffectLine> activeEffects(const MWWorld::Ptr& actor);
}

#endif
