#include "activeeffects.hpp"

#include <iomanip>
#include <sstream>

#include <MyGUI_UString.h>

#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/windowmanager.hpp"

#include "../../mwmechanics/creaturestats.hpp"
#include "../../mwmechanics/magiceffects.hpp"

#include "../../mwworld/class.hpp"
#include "../../mwworld/esmstore.hpp"

#include "../tooltips.hpp"

namespace MWGui::A11y
{
    namespace
    {
        // Append the magnitude text for one effect, mirroring how SpellIcons
        // renders the live (already-resisted) magnitude of an active effect.
        void appendMagnitude(std::string& line, float srcMagnitude, ESM::MagicEffect::MagnitudeDisplayType displayType)
        {
            if (displayType == ESM::MagicEffect::MDT_None)
                return;

            MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
            const int magnitude = static_cast<int>(srcMagnitude);

            if (displayType == ESM::MagicEffect::MDT_TimesInt)
            {
                std::stringstream formatter;
                formatter << std::fixed << std::setprecision(1) << " " << (magnitude / 10.0f);
                line += formatter.str();
                line += wm->getGameSettingString("sXTimesINT", {});
                return;
            }

            line += " " + MyGUI::utility::toString(magnitude);
            if (displayType == ESM::MagicEffect::MDT_Percentage)
                line += wm->getGameSettingString("sPercent", {});
            else if (displayType == ESM::MagicEffect::MDT_Feet)
                line += std::string(" ") + std::string(wm->getGameSettingString("sFeet", {}));
            else if (displayType == ESM::MagicEffect::MDT_Level)
                line += std::string(" ")
                    + std::string(wm->getGameSettingString(magnitude > 1 ? "sLevels" : "sLevel", {}));
            else if (displayType == ESM::MagicEffect::MDT_Points)
                line += std::string(" ")
                    + std::string(wm->getGameSettingString(magnitude > 1 ? "sPoints" : "sPoint", {}));
        }
    }

    std::vector<ActiveEffectLine> activeEffects(const MWWorld::Ptr& actor)
    {
        std::vector<ActiveEffectLine> lines;
        if (actor.isEmpty() || !actor.getClass().isActor())
            return lines;

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);

        for (const auto& params : stats.getActiveSpells())
        {
            const std::string& sourceName = params.getDisplayName();
            for (const auto& source : params.getEffects())
            {
                // Match SpellIcons: only applied effects, and skip timed effects
                // whose time has run out. Permanent effects (mDuration == -1,
                // e.g. racial abilities, constant enchantments) are kept.
                if (!(source.mFlags & ESM::ActiveEffect::Flag_Applied))
                    continue;
                if (source.mDuration != -1.f && source.mTimeLeft <= 0.f)
                    continue;

                const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(source.mEffectId);
                if (!effect)
                    continue;

                const ESM::RefId arg = source.getSkillOrAttribute();
                const ESM::Attribute* attribute = nullptr;
                const ESM::Skill* skill = nullptr;
                if (effect->mData.mFlags & ESM::MagicEffect::TargetSkill)
                    skill = store.get<ESM::Skill>().search(arg);
                if (effect->mData.mFlags & ESM::MagicEffect::TargetAttribute)
                    attribute = store.get<ESM::Attribute>().search(arg);

                // Effect name (+ target skill/attribute), then live magnitude.
                std::string line = MWMechanics::getMagicEffectString(*effect, attribute, skill);
                appendMagnitude(line, source.mMagnitude, effect->getMagnitudeDisplayType());

                // Always speak the duration: unlike the on-screen icons (whose
                // duration is gated behind a HUD-clutter setting), an on-demand
                // spoken read benefits from it every time. A permanent effect
                // (mDuration == -1, e.g. an ability or constant enchantment)
                // reads as "permanent" rather than a countdown.
                if (source.mDuration == -1.f)
                    line += ", permanent";
                else if (source.mTimeLeft > -1.f)
                    line += MWGui::ToolTips::getDurationString(source.mTimeLeft, " #{sDuration}");

                lines.push_back({ sourceName, std::move(line) });
            }
        }

        return lines;
    }
}
