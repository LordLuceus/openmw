#include "spelltext.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

#include <MyGUI_UString.h>

#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadspel.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/windowmanager.hpp"

#include "../../mwmechanics/actorutil.hpp"
#include "../../mwmechanics/magiceffects.hpp"
#include "../../mwmechanics/spellutil.hpp"

#include "../../mwworld/esmstore.hpp"

#include "../spellmodel.hpp"
#include "../widgets.hpp"

#include "itemtext.hpp"
#include "speech.hpp"

namespace MWGui::A11y
{
    std::string formatSpellEffectLine(const ESM::IndexedENAMstruct& effect, bool isConstant)
    {
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::MagicEffect* magicEffect = store.get<ESM::MagicEffect>().search(effect.mData.mEffectID);
        if (!magicEffect)
        {
            // Effect ID not in the store -- the effect silently vanishes from
            // the spoken description and the spell/item reads as if complete.
            // Log so a data/store change doesn't go unnoticed.
            logWarn("formatSpellEffectLine: unknown magic effect ID " + effect.mData.mEffectID.toDebugString()
                + "; effect omitted from spoken description");
            return {};
        }
        const ESM::Attribute* attribute = store.get<ESM::Attribute>().search(effect.mData.mAttribute);
        const ESM::Skill* skill = store.get<ESM::Skill>().search(effect.mData.mSkill);

        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();

        const std::string_view pt = wm->getGameSettingString("spoint", {});
        const std::string_view pts = wm->getGameSettingString("spoints", {});
        const std::string_view pct = wm->getGameSettingString("spercent", {});
        const std::string_view ft = wm->getGameSettingString("sfeet", {});
        const std::string_view lvl = wm->getGameSettingString("sLevel", {});
        const std::string_view lvls = wm->getGameSettingString("sLevels", {});
        const std::string to = " " + std::string{ wm->getGameSettingString("sTo", {}) } + " ";
        const std::string sec = " " + std::string{ wm->getGameSettingString("ssecond", {}) };
        const std::string secs = " " + std::string{ wm->getGameSettingString("sseconds", {}) };

        std::string line = MWMechanics::getMagicEffectString(*magicEffect, attribute, skill);

        const int magnMin = effect.mData.mMagnMin;
        const int magnMax = effect.mData.mMagnMax;
        if (magnMin || magnMax)
        {
            ESM::MagicEffect::MagnitudeDisplayType displayType = magicEffect->getMagnitudeDisplayType();
            if (displayType == ESM::MagicEffect::MDT_TimesInt)
            {
                std::string_view timesInt = wm->getGameSettingString("sXTimesINT", {});
                std::stringstream formatter;
                formatter << std::fixed << std::setprecision(1) << " " << (magnMin / 10.0f);
                if (magnMin != magnMax)
                    formatter << to << (magnMax / 10.0f);
                formatter << timesInt;
                line += formatter.str();
            }
            else if (displayType != ESM::MagicEffect::MDT_None)
            {
                line += " " + MyGUI::utility::toString(magnMin);
                if (magnMin != magnMax)
                    line += to + MyGUI::utility::toString(magnMax);

                if (displayType == ESM::MagicEffect::MDT_Percentage)
                    line += pct;
                else if (displayType == ESM::MagicEffect::MDT_Feet)
                    line += std::string(" ") + std::string(ft);
                else if (displayType == ESM::MagicEffect::MDT_Level)
                    line += std::string(" ") + std::string((magnMin == magnMax && std::abs(magnMin) == 1) ? lvl : lvls);
                else // MDT_Points
                    line += std::string(" ") + std::string((magnMin == magnMax && std::abs(magnMin) == 1) ? pt : pts);
            }
        }

        // Constant abilities have no meaningful duration / area / range, so omit
        // them (matching how the game displays constant effects).
        if (!isConstant)
        {
            int duration = effect.mData.mDuration;
            if (!(magicEffect->mData.mFlags & ESM::MagicEffect::AppliedOnce))
                duration = std::max(1, duration);
            if (duration > 0 && !(magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration))
            {
                line += ' ';
                line += wm->getGameSettingString("sfor", {});
                line += ' ' + MyGUI::utility::toString(duration) + ((duration == 1) ? sec : secs);
            }
            if (effect.mData.mArea > 0)
                line += " #{sin} " + MyGUI::utility::toString(effect.mData.mArea) + " #{sfootarea}";

            line += ' ';
            line += wm->getGameSettingString("sonword", {});
            line += ' ';
            if (effect.mData.mRange == ESM::RT_Self)
                line += wm->getGameSettingString("sRangeSelf", {});
            else if (effect.mData.mRange == ESM::RT_Touch)
                line += wm->getGameSettingString("sRangeTouch", {});
            else if (effect.mData.mRange == ESM::RT_Target)
                line += wm->getGameSettingString("sRangeTarget", {});
        }

        return line;
    }

    std::string formatSpellEffectLine(const Widgets::SpellEffectParams& effect)
    {
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();

        // Unknown effects (e.g. an ingredient effect the player's Alchemy skill
        // isn't high enough to identify) render as a bare "?" on screen, which a
        // screen reader would voice as silence -- confusing. Speak a real phrase
        // so the player knows there's an as-yet-unidentified effect there.
        if (!effect.mKnown)
            return "Unknown effect";

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::MagicEffect* magicEffect = store.get<ESM::MagicEffect>().search(effect.mEffectID);
        if (!magicEffect)
        {
            logWarn("formatSpellEffectLine: unknown magic effect ID " + effect.mEffectID.toDebugString()
                + "; effect omitted from spoken description");
            return {};
        }
        const ESM::Attribute* attribute = store.get<ESM::Attribute>().search(effect.mAttribute);
        const ESM::Skill* skill = store.get<ESM::Skill>().search(effect.mSkill);

        const std::string_view pt = wm->getGameSettingString("spoint", {});
        const std::string_view pts = wm->getGameSettingString("spoints", {});
        const std::string_view pct = wm->getGameSettingString("spercent", {});
        const std::string_view ft = wm->getGameSettingString("sfeet", {});
        const std::string_view lvl = wm->getGameSettingString("sLevel", {});
        const std::string_view lvls = wm->getGameSettingString("sLevels", {});
        const std::string to = " " + std::string{ wm->getGameSettingString("sTo", {}) } + " ";
        const std::string sec = " " + std::string{ wm->getGameSettingString("ssecond", {}) };
        const std::string secs = " " + std::string{ wm->getGameSettingString("sseconds", {}) };

        std::string line = MWMechanics::getMagicEffectString(*magicEffect, attribute, skill);

        if ((effect.mMagnMin || effect.mMagnMax) && !effect.mNoMagnitude)
        {
            ESM::MagicEffect::MagnitudeDisplayType displayType = magicEffect->getMagnitudeDisplayType();
            if (displayType == ESM::MagicEffect::MDT_TimesInt)
            {
                std::string_view timesInt = wm->getGameSettingString("sXTimesINT", {});
                std::stringstream formatter;
                formatter << std::fixed << std::setprecision(1) << " " << (effect.mMagnMin / 10.0f);
                if (effect.mMagnMin != effect.mMagnMax)
                    formatter << to << (effect.mMagnMax / 10.0f);
                formatter << timesInt;
                line += formatter.str();
            }
            else if (displayType != ESM::MagicEffect::MDT_None)
            {
                line += " " + MyGUI::utility::toString(effect.mMagnMin);
                if (effect.mMagnMin != effect.mMagnMax)
                    line += to + MyGUI::utility::toString(effect.mMagnMax);

                if (displayType == ESM::MagicEffect::MDT_Percentage)
                    line += pct;
                else if (displayType == ESM::MagicEffect::MDT_Feet)
                    line += std::string(" ") + std::string(ft);
                else if (displayType == ESM::MagicEffect::MDT_Level)
                    line += std::string(" ")
                        + std::string((effect.mMagnMin == effect.mMagnMax && std::abs(effect.mMagnMin) == 1) ? lvl
                                                                                                             : lvls);
                else // MDT_Points
                    line += std::string(" ")
                        + std::string(
                            (effect.mMagnMin == effect.mMagnMax && std::abs(effect.mMagnMin) == 1) ? pt : pts);
            }
        }

        if (!effect.mIsConstant)
        {
            int duration = effect.mDuration;
            if (!(magicEffect->mData.mFlags & ESM::MagicEffect::AppliedOnce))
                duration = std::max(1, duration);
            if (duration > 0 && !(magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration))
            {
                line += ' ';
                line += wm->getGameSettingString("sfor", {});
                line += ' ' + MyGUI::utility::toString(duration) + ((duration == 1) ? sec : secs);
            }
            if (effect.mArea > 0)
                line += " #{sin} " + MyGUI::utility::toString(effect.mArea) + " #{sfootarea}";

            // Potions etc. have no target.
            if (!effect.mNoTarget)
            {
                line += ' ';
                line += wm->getGameSettingString("sonword", {});
                line += ' ';
                if (effect.mRange == ESM::RT_Self)
                    line += wm->getGameSettingString("sRangeSelf", {});
                else if (effect.mRange == ESM::RT_Touch)
                    line += wm->getGameSettingString("sRangeTouch", {});
                else if (effect.mRange == ESM::RT_Target)
                    line += wm->getGameSettingString("sRangeTarget", {});
            }
        }

        return line;
    }

    std::string spellSchoolLine(const ESM::Spell& spell, const MWWorld::Ptr& caster)
    {
        // Only spells that contribute to skill progress (regular castable spells,
        // not powers/abilities/diseases) show a school, matching the on-screen
        // tooltip; the school is the dominant one for this caster.
        if (!MWMechanics::spellIncreasesSkill(&spell))
            return {};
        const ESM::RefId schoolSkill = MWMechanics::getSpellSchool(&spell, caster);
        if (schoolSkill.empty())
            return {};
        const ESM::Skill* skill = MWBase::Environment::get().getESMStore()->get<ESM::Skill>().search(schoolSkill);
        if (!skill || !skill->mSchool)
            return {};
        return "#{sSchool}: " + skill->mSchool->mName;
    }

    std::vector<std::string> spellModelTooltipLines(const MWGui::Spell& spell)
    {
        std::vector<std::string> lines;
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();

        // Enchanted items: defer to the shared item tooltip helper (weight,
        // value, enchantment effects), same as the inventory/container lists.
        // The cost/charge column is item-specific extra info shown in the list;
        // surface it up front.
        if (spell.mType == MWGui::Spell::Type_EnchantedItem)
        {
            if (!spell.mItem.isEmpty())
                lines = itemTooltipLines(spell.mItem, spell.mCount);
            if (!spell.mCostColumn.empty())
                lines.insert(lines.begin(),
                    std::string(wm->getGameSettingString("sCostCharge", "Cost/Charge")) + ": " + spell.mCostColumn);
            return lines;
        }

        // Powers and spells: cost/chance, then the school, then each magic
        // effect -- mirroring the on-screen Spell tooltip (ToolTips::createToolTip
        // "Spell" branch).
        const ESM::Spell* esmSpell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(spell.mId);
        if (!esmSpell)
            return lines;

        if (spell.mType == MWGui::Spell::Type_Spell && !spell.mCostColumn.empty())
            lines.push_back(
                std::string(wm->getGameSettingString("sCostChance", "Cost/Chance")) + ": " + spell.mCostColumn);

        std::string school = spellSchoolLine(*esmSpell, MWMechanics::getPlayer());
        if (!school.empty())
            lines.push_back(std::move(school));

        const bool isConstant = (esmSpell->mData.mType == ESM::Spell::ST_Ability);
        for (const ESM::IndexedENAMstruct& effect : esmSpell->mEffects.mList)
            lines.push_back(formatSpellEffectLine(effect, isConstant));

        return lines;
    }
}
