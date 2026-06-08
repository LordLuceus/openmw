#include "itemtext.hpp"

#include <sstream>

#include <MyGUI_UString.h>

#include <components/esm3/loadench.hpp>

#include "../tooltips.hpp"
#include "../widgets.hpp"

#include "../../mwbase/environment.hpp"
#include "../../mwbase/windowmanager.hpp"

#include "../../mwmechanics/spellutil.hpp"

#include "../../mwworld/class.hpp"
#include "../../mwworld/esmstore.hpp"

#include "spelltext.hpp"

namespace MWGui::A11y
{
    namespace
    {
        // Split text on newlines into trimmed, non-empty lines. The engine's
        // tooltip strings are newline-separated (e.g. "\nWeight: 2\nValue: 5"),
        // and each line makes a natural T-cycle entry.
        void appendLines(std::string_view text, std::vector<std::string>& out)
        {
            std::stringstream stream{ std::string(text) };
            std::string line;
            while (std::getline(stream, line))
            {
                // Trim leading/trailing whitespace (and stray carriage returns).
                const auto first = line.find_first_not_of(" \t\r\n");
                if (first == std::string::npos)
                    continue;
                const auto last = line.find_last_not_of(" \t\r\n");
                out.emplace_back(line.substr(first, last - first + 1));
            }
        }

        // Format each effect into a spoken line and append the non-empty ones.
        void appendEffects(const MWGui::Widgets::SpellEffectList& effects, std::vector<std::string>& out)
        {
            for (const MWGui::Widgets::SpellEffectParams& effect : effects)
            {
                std::string line = formatSpellEffectLine(effect);
                if (!line.empty())
                    out.push_back(std::move(line));
            }
        }
    }

    std::vector<std::string> itemTooltipLines(const MWWorld::ConstPtr& ptr, int count)
    {
        std::vector<std::string> lines;
        if (ptr.isEmpty())
            return lines;

        // getToolTipInfo already gates the owner/script "extra" block on the
        // full-help setting, exactly as the on-screen tooltip does, so we just
        // mirror whatever it produced.
        MWGui::ToolTipInfo info = ptr.getClass().getToolTipInfo(ptr, count);

        appendLines(info.text, lines);

        // Intrinsic effects (potions, ingredients). The on-screen tooltip stores
        // bare params in info.effects and only applies the display flags at
        // render time (see ToolTips::createToolTip + MWSpell::createEffectWidgets):
        // potions have no target, ingredients show neither magnitude nor
        // duration/target. Mirror that here, otherwise ingredient effects (which
        // carry no magnitude) print stray "-1" magnitudes / ranges.
        if (!info.effects.empty())
        {
            MWGui::Widgets::SpellEffectList effects = info.effects;
            for (MWGui::Widgets::SpellEffectParams& effect : effects)
            {
                if (info.isPotion)
                    effect.mNoTarget = true;
                if (info.isIngredient)
                {
                    effect.mNoMagnitude = true;
                    effect.mIsConstant = true;
                }
            }
            appendEffects(effects, lines);
        }

        // Enchantment effects (enchanted clothing/armour/weapons, e.g. a Ring of
        // Healing). These are NOT part of info.effects -- the on-screen tooltip
        // looks them up separately from info.enchant and renders them in their
        // own block. Mirror that path so enchanted gear actually speaks its
        // effects. Constant-effect enchantments omit duration/range, exactly as
        // the visual tooltip does.
        if (!info.enchant.empty())
        {
            const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
            if (const ESM::Enchantment* enchant = store.get<ESM::Enchantment>().search(info.enchant))
            {
                const bool constant = (enchant->mData.mType == ESM::Enchantment::ConstantEffect);
                MWGui::Widgets::SpellEffectList effects
                    = MWGui::Widgets::MWEffectList::effectListFromESM(&enchant->mEffects);
                for (MWGui::Widgets::SpellEffectParams& effect : effects)
                    effect.mIsConstant = constant;
                appendEffects(effects, lines);

                // How the enchantment is triggered (Cast When Used / When
                // Strikes / Once / Constant Effect). The on-screen tooltip shows
                // this as a line after the effects (see ToolTips::getMiscString /
                // the enchant block in tooltips.cpp); without it the spoken
                // tooltip can't tell e.g. a Cast-When-Used item from a constant
                // one. Resolve the same GMST labels the visual tooltip uses.
                MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
                std::string_view castType;
                switch (enchant->mData.mType)
                {
                    case ESM::Enchantment::CastOnce:
                        castType = wm->getGameSettingString("sItemCastOnce", {});
                        break;
                    case ESM::Enchantment::WhenStrikes:
                        castType = wm->getGameSettingString("sItemCastWhenStrikes", {});
                        break;
                    case ESM::Enchantment::WhenUsed:
                        castType = wm->getGameSettingString("sItemCastWhenUsed", {});
                        break;
                    case ESM::Enchantment::ConstantEffect:
                        castType = wm->getGameSettingString("sItemCastConstant", {});
                        break;
                }
                if (!castType.empty())
                    lines.emplace_back(castType);

                // Remaining / maximum enchantment charge. The on-screen tooltip
                // draws this as a charge bar, but only for the rechargeable cast
                // types (When Used / When Strikes) -- Cast Once and Constant
                // Effect items have no charge pool. This is the only place the
                // charge is shown for items that don't appear in the magic pane
                // (e.g. on-strike weapons like the Firebite Dagger), so surface
                // it here for every enchanted item, not just castable ones.
                if (enchant->mData.mType == ESM::Enchantment::WhenStrikes
                    || enchant->mData.mType == ESM::Enchantment::WhenUsed)
                {
                    const int maxCharge = MWMechanics::getEnchantmentCharge(*enchant);
                    const int charge = (info.remainingEnchantCharge == -1) ? maxCharge : info.remainingEnchantCharge;
                    std::string chargeLine{ wm->getGameSettingString("sCharges", "Charges") };
                    chargeLine += ": " + MyGUI::utility::toString(charge) + " / " + MyGUI::utility::toString(maxCharge);
                    lines.emplace_back(std::move(chargeLine));
                }
            }
        }

        appendLines(info.extra, lines);

        return lines;
    }
}
