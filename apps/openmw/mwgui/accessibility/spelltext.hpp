#ifndef OPENMW_MWGUI_ACCESSIBILITY_SPELLTEXT_H
#define OPENMW_MWGUI_ACCESSIBILITY_SPELLTEXT_H

#include <string>
#include <vector>

namespace ESM
{
    struct IndexedENAMstruct;
    struct Spell;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWGui
{
    struct Spell;

    namespace Widgets
    {
        struct SpellEffectParams;
    }
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

    /// The single "#{sSchool}: <name>" tooltip line for a spell, mirroring the
    /// on-screen Spell tooltip (ToolTips::createToolTip "Spell" branch). Returns
    /// an empty string for spells that don't contribute to skill progress
    /// (powers / abilities / diseases) or whose school can't be resolved -- in
    /// those cases the on-screen tooltip shows no school either. \p caster
    /// selects the dominant school (getSpellSchool is caster-relative).
    std::string spellSchoolLine(const ESM::Spell& spell, const MWWorld::Ptr& caster);

    /// The full spoken tooltip (detail lines cycled with T) for one entry in a
    /// SpellModel-backed list -- the powers/spells/enchanted-items lists shown
    /// by the spell window and the quick-keys magic picker. Mirrors the on-screen
    /// tooltip: for enchanted items it defers to itemTooltipLines plus the
    /// cost/charge column; for powers and spells it lists cost/chance, the spell
    /// school (see spellSchoolLine) and each magic effect. The entry's name is
    /// NOT included (the caller announces that as the option label).
    std::vector<std::string> spellModelTooltipLines(const MWGui::Spell& spell);
}

#endif
