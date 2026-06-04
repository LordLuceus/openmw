#ifndef OPENMW_MWGUI_ACCESSIBILITY_SPELLTEXT_H
#define OPENMW_MWGUI_ACCESSIBILITY_SPELLTEXT_H

#include <string>

namespace ESM
{
    struct IndexedENAMstruct;
}

namespace MWGui::Widgets
{
    struct SpellEffectParams;
}

namespace MWGui::A11y
{
    /// Build a human-readable description of a single spell effect, including
    /// magnitude, duration, area and range -- mirroring the text shown in the
    /// on-screen spell tooltip (see MWSpellEffect::updateWidgets). Returned with
    /// any \c #{...} L10n tags intact; A11y::say resolves them.
    ///
    /// \param isConstant when true the effect is a constant ability, so its
    ///        duration / area / range are omitted (they don't apply), matching
    ///        how constant effects are displayed in game.
    std::string formatSpellEffectLine(const ESM::IndexedENAMstruct& effect, bool isConstant = false);

    /// Same, for the Widgets::SpellEffectParams form used by item tooltips
    /// (potions, enchanted gear). Honours mKnown (unknown effects read as "?"),
    /// mNoMagnitude (ingredients), mNoTarget (potions) and mIsConstant exactly
    /// as MWSpellEffect::updateWidgets renders them on screen.
    std::string formatSpellEffectLine(const Widgets::SpellEffectParams& effect);
}

#endif
