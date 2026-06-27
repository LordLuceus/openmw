#include "enchantingdialog.hpp"

#include <iomanip>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_UString.h>

#include <components/misc/strings/format.hpp>
#include <components/settings/values.hpp>
#include <components/widgets/list.hpp>

#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadgmst.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "accessibility/itemtext.hpp"
#include "accessibility/speech.hpp"
#include "itemselection.hpp"
#include "itemwidget.hpp"

#include "sortfilteritemmodel.hpp"

namespace MWGui
{

    EnchantingDialog::EnchantingDialog()
        : WindowBase("openmw_enchanting_dialog.layout")
        , EffectEditorBase(EffectEditorBase::Enchanting)
    {
        getWidget(mName, "NameEdit");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mAvailableEffectsList, "AvailableEffects");
        getWidget(mUsedEffectsView, "UsedEffects");
        getWidget(mItemBox, "ItemBox");
        getWidget(mSoulBox, "SoulBox");
        getWidget(mEnchantmentPoints, "Enchantment");
        getWidget(mCastCost, "CastCost");
        getWidget(mCharge, "Charge");
        getWidget(mSuccessChance, "SuccessChance");
        getWidget(mChanceLayout, "ChanceLayout");
        getWidget(mTypeButton, "TypeButton");
        getWidget(mBuyButton, "BuyButton");
        getWidget(mPrice, "PriceLabel");
        getWidget(mPriceText, "PriceTextLabel");

        setWidgets(mAvailableEffectsList, mUsedEffectsView);

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onCancelButtonClicked);
        mItemBox->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onSelectItem);
        mSoulBox->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onSelectSoul);
        mBuyButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onBuyButtonClicked);
        mTypeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onTypeButtonClicked);
        mName->eventEditSelectAccept += MyGUI::newDelegate(this, &EnchantingDialog::onAccept);

        // Screen-reader: virtual-focus anchor for the whole window (shared
        // EffectEditorBase infra), plus spoken editing on the name box.
        initEffectListA11y(mMainWidget);
        mNameField.attach(mName);
        mNameField.setActive(false);

        mControllerButtons.mA = "#{Interface:Select}";
        mControllerButtons.mB = "#{Interface:Cancel}";
        mControllerButtons.mY = "#{OMWEngine:EnchantType}";
        mControllerButtons.mL1 = "#{Interface:Item}";
        mControllerButtons.mR1 = "#{Interface:Soul}";
    }

    void EnchantingDialog::onOpen()
    {
        center();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mName);
    }

    void EnchantingDialog::setSoulGem(const MWWorld::Ptr& gem)
    {
        if (gem.isEmpty())
        {
            mSoulBox->setItem(MWWorld::Ptr());
            mSoulBox->clearUserStrings();
            mEnchanting.setSoulGem(MWWorld::Ptr());
        }
        else
        {
            mSoulBox->setItem(gem);
            mSoulBox->setUserString("ToolTipType", "ItemPtr");
            mSoulBox->setUserData(MWWorld::Ptr(gem));
            mEnchanting.setSoulGem(gem);
        }
    }

    void EnchantingDialog::setItem(const MWWorld::Ptr& item)
    {
        if (item.isEmpty())
        {
            mItemBox->setItem(MWWorld::Ptr());
            mItemBox->clearUserStrings();
            mEnchanting.setOldItem(MWWorld::Ptr());
        }
        else
        {
            std::string_view name = item.getClass().getName(item);
            mName->setCaption(MyGUI::UString(name));
            mItemBox->setItem(item);
            mItemBox->setUserString("ToolTipType", "ItemPtr");
            mItemBox->setUserData(MWWorld::Ptr(item));
            mEnchanting.setOldItem(item);
        }
    }

    void EnchantingDialog::updateLabels()
    {
        mEnchantmentPoints->setCaption(std::to_string(static_cast<int>(mEnchanting.getEnchantPoints(false))) + " / "
            + std::to_string(mEnchanting.getMaxEnchantValue()));
        mCharge->setCaption(std::to_string(mEnchanting.getGemCharge()));
        mSuccessChance->setCaption(std::to_string(std::clamp(mEnchanting.getEnchantChance(), 0, 100)));
        mCastCost->setCaption(std::to_string(mEnchanting.getEffectiveCastCost()));
        mPrice->setCaption(std::to_string(mEnchanting.getEnchantPrice()));

        switch (mEnchanting.getCastStyle())
        {
            case ESM::Enchantment::CastOnce:
                mTypeButton->setCaption(MyGUI::UString(
                    MWBase::Environment::get().getWindowManager()->getGameSettingString("sItemCastOnce", "Cast Once")));
                setConstantEffect(false);
                break;
            case ESM::Enchantment::WhenStrikes:
                mTypeButton->setCaption(
                    MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString(
                        "sItemCastWhenStrikes", "When Strikes")));
                setConstantEffect(false);
                break;
            case ESM::Enchantment::WhenUsed:
                mTypeButton->setCaption(
                    MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString(
                        "sItemCastWhenUsed", "When Used")));
                setConstantEffect(false);
                break;
            case ESM::Enchantment::ConstantEffect:
                mTypeButton->setCaption(
                    MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString(
                        "sItemCastConstant", "Cast Constant")));
                setConstantEffect(true);
                break;
        }
    }

    void EnchantingDialog::buildAccessibility()
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();

        // Preserve the user's place across a rebuild (e.g. after adding an
        // effect or swapping a slot) so focus doesn't jump back to the name.
        const std::string previous = mA11y.currentLabel();

        mA11y.clear();

        // Enchantment name (editable). value() reports the current contents so
        // it's spoken on focus; Enter begins editing.
        mA11y.add({ .widget = mName, .label = "#{sName}",
            .value =
                [this] {
                    const std::string text = mName->getOnlyText().asUTF8();
                    return text.empty() ? std::string("blank") : text;
                },
            .edit = &mNameField });

        // The item to enchant. Speaks its name (or "none"); Enter opens the
        // enchantable-item picker, or clears the slot if one is already set
        // (mirrors clicking the on-screen item box).
        {
            const MWWorld::Ptr item = mEnchanting.getOldItem();
            const bool hasItem = !item.isEmpty();
            A11y::Element e;
            e.widget = mItemBox;
            e.label = "#{sItem}";
            if (hasItem)
            {
                const std::string name{ item.getClass().getName(item) };
                e.value = [name] { return name; };
                e.tooltips = [item] { return A11y::itemTooltipLines(item, 1); };
            }
            else
            {
                e.value = [winMgr] {
                    return std::string{ winMgr->getGameSettingString("sNone", "None") };
                };
            }
            e.activate = [this] { onSelectItem(mItemBox); };
            mA11y.add(std::move(e));
        }

        // The soul gem. Speaks the gem name plus the trapped soul and its value
        // (like the recharge window); Enter opens the soul-gem picker or clears
        // the slot.
        {
            const MWWorld::Ptr gem = mEnchanting.getGem();
            const bool hasGem = !gem.isEmpty() && gem.getCellRef().getCount() != 0;
            A11y::Element e;
            e.widget = mSoulBox;
            e.label = "#{sSoulGem}";
            if (hasGem)
            {
                const std::string soulLabel{ winMgr->getGameSettingString("sSoul", "Soul") };
                std::string name{ gem.getClass().getName(gem) };
                const ESM::Creature* creature = store.get<ESM::Creature>().search(gem.getCellRef().getSoul());
                if (creature != nullptr)
                    name += ", " + soulLabel + " " + std::string{ creature->mName } + " "
                        + std::to_string(creature->mData.mSoul);
                e.value = [name] { return name; };
                e.tooltips = [gem] { return A11y::itemTooltipLines(gem, 1); };
            }
            else
            {
                e.value = [winMgr] {
                    return std::string{ winMgr->getGameSettingString("sNone", "None") };
                };
            }
            e.activate = [this] { onSelectSoul(mSoulBox); };
            mA11y.add(std::move(e));
        }

        // Cast type (Cast Once / When Strikes / When Used / Constant Effect).
        // Left/Right cycle to the next style, like every other value control;
        // the framework speaks the new caption afterwards. (nextCastStyle only
        // steps forward, so both directions advance -- the same one-way cycle as
        // the range button in the effect editor.) onTypeButtonClicked rebuilds
        // this whole screen (updateEffectsView -> notifyEffectsChanged ->
        // buildAccessibility -> clear()), which frees this option mid-change();
        // changeValue() copies the value reader onto its stack before invoking
        // the handler precisely so this is safe, and selection is preserved by
        // label across the rebuild so focus stays on the cast type.
        mA11y.add({ .widget = mTypeButton, .label = "#{OMWEngine:EnchantType}",
            .value = [this] { return std::string(mTypeButton->getCaption()); },
            .change = [this](bool /*next*/) { onTypeButtonClicked(mTypeButton); } });

        // The two effect lists (available to add / used in the enchantment).
        addEffectListElements();

        // Read-only result stats, mirroring the on-screen labels. Enchantment
        // points, cast cost and the gem charge are always shown. The success
        // chance is shown only when self-enchanting (and the setting is on) --
        // gated by mChanceLayout, exactly as on screen. The price is shown only
        // when buying from an enchanter.
        mA11y.add({ .widget = mEnchantmentPoints, .label = "#{sEnchantmentMenu3}",
            .value = [this] { return std::string(mEnchantmentPoints->getCaption()); } });
        mA11y.add({ .widget = mCastCost, .label = "#{sCastCost}",
            .value = [this] { return std::string(mCastCost->getCaption()); } });
        mA11y.add({ .widget = mCharge, .label = "#{sCharges}",
            .value = [this] { return std::string(mCharge->getCaption()); } });

        if (mChanceLayout->getVisible())
            mA11y.add({ .widget = mSuccessChance, .label = "#{sEnchantmentMenu6}",
                .value = [this] { return std::string(mSuccessChance->getCaption()); } });

        if (mPrice->getVisible())
            mA11y.add({ .widget = mPrice, .label = "#{sBarterDialog7}",
                .value = [this] { return std::string(mPrice->getCaption()); } });

        // Buy (from an enchanter) or Create (self-enchant); the caption already
        // reflects which, set in setPtr().
        mA11y.add({ .widget = mBuyButton,
            .label = std::string(mBuyButton->getCaption()),
            .activate = [this] { onBuyButtonClicked(mBuyButton); } });
        mA11y.add({ .widget = mCancelButton, .label = "#{Interface:Cancel}",
            .activate = [this] { onCancelButtonClicked(mCancelButton); } });

        if (!previous.empty())
            mA11y.selectByLabel(previous, /*announce=*/false);
    }

    void EnchantingDialog::setPtr(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || (ptr.getType() != ESM::REC_MISC && !ptr.getClass().isActor()))
            throw std::runtime_error("Invalid argument in EnchantingDialog::setPtr");

        mName->setCaption({});

        if (ptr.getClass().isActor())
        {
            mEnchanting.setSelfEnchanting(false);
            mEnchanting.setEnchanter(ptr);
            mBuyButton->setCaptionWithReplacing("#{sBuy}");
            mControllerButtons.mX = "#{Interface:Buy}";
            mChanceLayout->setVisible(false);
            mPtr = ptr;
            setSoulGem(MWWorld::Ptr());
            mPrice->setVisible(true);
            mPriceText->setVisible(true);
        }
        else
        {
            mEnchanting.setSelfEnchanting(true);
            mEnchanting.setEnchanter(MWMechanics::getPlayer());
            mBuyButton->setCaptionWithReplacing("#{sCreate}");
            mControllerButtons.mX = "#{Interface:Create}";
            mChanceLayout->setVisible(Settings::game().mShowEnchantChance);
            mPtr = MWMechanics::getPlayer();
            setSoulGem(ptr);
            mPrice->setVisible(false);
            mPriceText->setVisible(false);
        }

        setItem(MWWorld::Ptr());
        startEditing();
        updateLabels();

        // startEditing()/updateLabels() populated the available effects, reset
        // the used list, and set the slot/stat captions. Activate the screen
        // reader here -- NOT in onOpen(), which runs BEFORE setPtr() and would
        // capture the previous session's stale name + empty lists. deactivate()
        // first so a reopen starts clean (selection/edit reset); since
        // deactivate() clear()s the option list, REBUILD before activating or it
        // would activate empty and announce nothing.
        mA11y.deactivate();
        buildAccessibility();
        mA11y.activate();
        mA11yModalWasOpen = false;
    }

    void EnchantingDialog::onClose()
    {
        mA11y.deactivate();
        mA11yModalWasOpen = false;
    }

    void EnchantingDialog::onFrame(float dt)
    {
        checkReferenceAvailable();

        // The edit-effect dialog (a separate modal) takes over the single
        // active-screen slot while it's up, leaving our screen inactive. When it
        // closes, reclaim control and re-announce where we are. (The item/soul
        // pickers instead suspend/resume our screen themselves, so they restore
        // focus without this path -- but detecting the closing edge here is
        // harmless for them since the screen is already active again.)
        // buildAccessibility() (run by the effect change) already refreshed our
        // list, so just preserve selection by label, preferring the effect the
        // dialog just acted on.
        const bool modalOpen = MyGUI::InputManager::getInstance().isModalAny();
        if (mA11yModalWasOpen && !modalOpen && isVisible() && !mA11y.isActive())
        {
            const std::string previous = mA11y.currentLabel();
            MyGUI::Widget* target = a11yUsedEffectWidget(a11ySelectedEffect());
            mA11y.activate(target);
            if (!target && !previous.empty())
                mA11y.selectByLabel(previous, /*announce=*/true);
        }
        mA11yModalWasOpen = modalOpen;

        // The cast-type option cycled the style and rebuilt the list (which
        // freed its own activate closure), deferring the spoken feedback to
        // here. Selection was preserved by label across the rebuild, so the
        // current option is once again the cast type -- just re-announce it.
        if (mA11yAnnounceAfterRebuild)
        {
            mA11yAnnounceAfterRebuild = false;
            if (mA11y.isActive())
                mA11y.announceCurrent();
        }

        mNameField.onFrame();
        mA11y.onFrame(dt);
    }

    void EnchantingDialog::onReferenceUnavailable()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);
        resetReference();
    }

    void EnchantingDialog::resetReference()
    {
        ReferenceInterface::resetReference();
        setItem(MWWorld::Ptr());
        setSoulGem(MWWorld::Ptr());
        mPtr = MWWorld::Ptr();
        mEnchanting.setEnchanter(MWWorld::Ptr());
    }

    void EnchantingDialog::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);
    }

    void EnchantingDialog::onSelectItem(MyGUI::Widget* /*sender*/)
    {
        if (mEnchanting.getOldItem().isEmpty())
        {
            mItemSelectionDialog = std::make_unique<ItemSelectionDialog>("#{sEnchantItems}");
            mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &EnchantingDialog::onItemSelected);
            mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &EnchantingDialog::onItemCancel);
            mItemSelectionDialog->setVisible(true);
            mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
            mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyEnchantable);
        }
        else
        {
            // Enter on the already-filled item slot clears it. Rebuild the a11y
            // list so the slot's spoken value updates to "none", and announce it
            // next frame (we're inside the option's own activate closure, which
            // the rebuild frees -- so defer, as with the cast-type option).
            mA11yAnnounceAfterRebuild = true;
            setItem(MWWorld::Ptr());
            updateLabels();
            buildAccessibility();
        }
    }

    void EnchantingDialog::onItemSelected(MWWorld::Ptr item)
    {
        setItem(item);
        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
        mEnchanting.nextCastStyle();
        updateLabels();

        // Refresh the a11y list so the item slot speaks the NEW item, and do it
        // BEFORE hiding the picker: setVisible(false) synchronously fires the
        // picker's onClose(), which resumes our screen and announces the current
        // option. If we rebuilt after that, the resume would speak the stale
        // ("none") slot value. The picker's own selection sound has already
        // played; rebuilding first means the resume announces the fresh value.
        buildAccessibility();
        mItemSelectionDialog->setVisible(false);
    }

    void EnchantingDialog::onItemCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void EnchantingDialog::onSoulSelected(MWWorld::Ptr item)
    {
        mEnchanting.setSoulGem(item);
        if (mEnchanting.getGemCharge() == 0)
        {
            // Soulless gem: vanilla rejects it, leaving the slot unchanged and
            // the picker open. Hide the picker (matching the original flow) and
            // surface the rejection -- the message box speaks itself.
            mItemSelectionDialog->setVisible(false);
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage32}");
            return;
        }

        setSoulGem(item);
        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
        updateLabels();

        // Refresh the a11y list so the soul-gem slot speaks the NEW gem/soul,
        // BEFORE hiding the picker -- setVisible(false) synchronously resumes our
        // screen and announces the current option, so rebuilding first means it
        // announces the fresh value rather than the stale "none".
        buildAccessibility();
        mItemSelectionDialog->setVisible(false);
    }

    void EnchantingDialog::onSoulCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void EnchantingDialog::onSelectSoul(MyGUI::Widget* /*sender*/)
    {
        if (mEnchanting.getGem().isEmpty())
        {
            mItemSelectionDialog = std::make_unique<ItemSelectionDialog>("#{sSoulGemsWithSouls}");
            mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &EnchantingDialog::onSoulSelected);
            mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &EnchantingDialog::onSoulCancel);
            mItemSelectionDialog->setVisible(true);
            mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
            mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyChargedSoulstones);

            // MWBase::Environment::get().getWindowManager()->messageBox("#{sInventorySelectNoSoul}");
        }
        else
        {
            // Enter on the filled soul slot clears it. updateEffectsView() ->
            // notifyEffectsChanged() already rebuilds the a11y list, so just
            // defer the announce (we're inside the freed activate closure).
            mA11yAnnounceAfterRebuild = true;
            setSoulGem(MWWorld::Ptr());
            mEnchanting.nextCastStyle();
            updateLabels();
            updateEffectsView();
        }
    }

    void EnchantingDialog::notifyEffectsChanged()
    {
        mEffectList.populate(mEffects);
        mEnchanting.setEffect(mEffectList);
        updateLabels();

        // The used-effects list (and the result stats) changed, so rebuild the
        // screen-reader options. Safe before activate() (it only rebuilds the
        // list); selection is preserved by label across the rebuild. This is
        // also where the available effects first become navigable -- the build
        // in setPtr() runs after startEditing() populated them, but a rebuild
        // here keeps everything in sync after every add/edit/delete.
        buildAccessibility();
    }

    void EnchantingDialog::onTypeButtonClicked(MyGUI::Widget* /*sender*/)
    {
        mEnchanting.nextCastStyle();
        updateLabels();
        updateEffectsView();
    }

    void EnchantingDialog::onAccept(MyGUI::EditBox* sender)
    {
        onBuyButtonClicked(sender);

        // To do not spam onAccept() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void EnchantingDialog::onBuyButtonClicked(MyGUI::Widget* /*sender*/)
    {
        if (mEffects.size() <= 0)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sEnchantmentMenu11}");
            return;
        }

        if (mName->getCaption().empty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage10}");
            return;
        }

        if (mEnchanting.soulEmpty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage52}");
            return;
        }

        if (mEnchanting.itemEmpty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage11}");
            return;
        }

        if (static_cast<int>(mEnchanting.getEnchantPoints(false)) > mEnchanting.getMaxEnchantValue())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage29}");
            return;
        }

        mEnchanting.setNewItemName(mName->getCaption());
        mEnchanting.setEffect(mEffectList);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);
        if (mPtr != player && mEnchanting.getEnchantPrice() > playerGold)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage18}");
            return;
        }

        // check if the player is attempting to use a soulstone or item that was stolen from this actor
        if (mPtr != player)
        {
            for (int i = 0; i < 2; ++i)
            {
                MWWorld::Ptr item = (i == 0) ? mEnchanting.getOldItem() : mEnchanting.getGem();
                if (MWBase::Environment::get().getMechanicsManager()->isItemStolenFrom(
                        item.getCellRef().getRefId(), mPtr))
                {
                    std::string msg = MWBase::Environment::get()
                                          .getESMStore()
                                          ->get<ESM::GameSetting>()
                                          .find("sNotifyMessage49")
                                          ->mValue.getString();
                    msg = Misc::StringUtils::format(msg, item.getClass().getName(item));
                    MWBase::Environment::get().getWindowManager()->messageBox(msg);

                    MWBase::Environment::get().getMechanicsManager()->confiscateStolenItemToOwner(
                        player, item, mPtr, 1);

                    MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);
                    MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
                    return;
                }
            }
        }

        if (mEnchanting.create())
        {
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("enchant success"));
            MWBase::Environment::get().getWindowManager()->messageBox("#{sEnchantmentMenu12}");
            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);
        }
        else
        {
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("enchant fail"));
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage34}");
            if (!mEnchanting.getGem().isEmpty() && !mEnchanting.getGem().getCellRef().getCount())
            {
                setSoulGem(MWWorld::Ptr());
                mEnchanting.nextCastStyle();
                updateLabels();
                updateEffectsView();
            }
        }
    }

    bool EnchantingDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onCancelButtonClicked(mCancelButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_X)
            onBuyButtonClicked(mBuyButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_Y)
            onTypeButtonClicked(mTypeButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            onSelectItem(mItemBox);
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
            onSelectSoul(mSoulBox);
        else
            return EffectEditorBase::onControllerButtonEvent(arg);

        return true;
    }
}

