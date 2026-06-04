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

#include "../../mwmechanics/magiceffects.hpp"

#include "../../mwworld/esmstore.hpp"

#include "../widgets.hpp"

namespace MWGui::A11y
{
    std::string formatSpellEffectLine(const ESM::IndexedENAMstruct& effect, bool isConstant)
    {
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::MagicEffect* magicEffect = store.get<ESM::MagicEffect>().search(effect.mData.mEffectID);
        if (!magicEffect)
            return {};
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
            return {};
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
}
