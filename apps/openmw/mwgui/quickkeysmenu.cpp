#include "quickkeysmenu.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_RenderManager.h>

#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/quickkeys.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/player.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "accessibility/itemtext.hpp"
#include "accessibility/screen.hpp"
#include "accessibility/speech.hpp"
#include "accessibility/spelltext.hpp"
#include "accessibility/uimanager.hpp"
#include "itemselection.hpp"
#include "itemwidget.hpp"
#include "sortfilteritemmodel.hpp"
#include "spellview.hpp"

#include "../mwmechanics/spellutil.hpp"

namespace MWGui
{
    namespace
    {
        // Spoken section/label/tooltip for one entry in the magic picker,
        // mirroring SpellWindow's list a11y (powers / spells / magic items).
        std::string magicPickerLabel(const Spell& spell)
        {
            std::string label = spell.mName;
            if (spell.mType == Spell::Type_EnchantedItem && spell.mCount > 1)
                label += " (" + std::to_string(spell.mCount) + ")";
            return label;
        }

        std::string magicPickerSection(const Spell& spell)
        {
            MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
            switch (spell.mType)
            {
                case Spell::Type_Power:
                    return std::string(winMgr->getGameSettingString("sPowers", "Powers"));
                case Spell::Type_EnchantedItem:
                    return std::string(winMgr->getGameSettingString("sMagicItem", "Magic Item"));
                default:
                    return std::string(winMgr->getGameSettingString("sSpells", "Spells"));
            }
        }

        std::vector<std::string> magicPickerTooltip(const Spell& spell)
        {
            std::vector<std::string> lines;
            MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

            if (spell.mType == Spell::Type_EnchantedItem)
            {
                if (!spell.mItem.isEmpty())
                    lines = A11y::itemTooltipLines(spell.mItem, spell.mCount);
                if (!spell.mCostColumn.empty())
                    lines.insert(lines.begin(),
                        std::string(winMgr->getGameSettingString("sCostCharge", "Cost/Charge")) + ": "
                            + spell.mCostColumn);
                return lines;
            }

            const ESM::Spell* esmSpell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(spell.mId);
            if (!esmSpell)
                return lines;

            if (spell.mType == Spell::Type_Spell && !spell.mCostColumn.empty())
                lines.push_back(std::string(winMgr->getGameSettingString("sCostChance", "Cost/Chance")) + ": "
                    + spell.mCostColumn);

            const bool isConstant = (esmSpell->mData.mType == ESM::Spell::ST_Ability);
            for (const ESM::IndexedENAMstruct& effect : esmSpell->mEffects.mList)
                lines.push_back(A11y::formatSpellEffectLine(effect, isConstant));

            return lines;
        }
    }

    QuickKeysMenu::QuickKeysMenu()
        : WindowBase("openmw_quickkeys_menu.layout")
        , mKey(std::vector<keyData>(10))
        , mSelected(nullptr)
        , mActivated(nullptr)
    {
        getWidget(mOkButton, "OKButton");
        getWidget(mInstructionLabel, "InstructionLabel");

        mMainWidget->setSize(mMainWidget->getWidth(),
            mMainWidget->getHeight() + (mInstructionLabel->getTextSize().height - mInstructionLabel->getHeight()));

        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuickKeysMenu::onOkButtonClicked);
        center();

        for (int i = 0; i < 10; ++i)
        {
            mKey[i].index = i + 1;
            getWidget(mKey[i].button, "QuickKey" + MyGUI::utility::toString(i + 1));
            mKey[i].button->eventMouseButtonClick += MyGUI::newDelegate(this, &QuickKeysMenu::onQuickKeyButtonClicked);

            unassign(&mKey[i]);
        }

        if (Settings::gui().mControllerMenus)
        {
            mControllerButtons.mA = "#{Interface:Select}";
            mControllerButtons.mB = "#{Interface:OK}";
        }

        // Screen-reader setup: an invisible anchor holds key focus; the 10 quick
        // key slots are navigated as widget-less options (the slot buttons are
        // visual only). Built fresh on each open from the live assignments.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);
    }

    void QuickKeysMenu::clear()
    {
        mActivated = nullptr;

        for (int i = 0; i < 10; ++i)
        {
            unassign(&mKey[i]);
        }

        mTemp.clear();
    }

    inline void QuickKeysMenu::validate(int index)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);
        switch (mKey[index].type)
        {
            case ESM::QuickKeys::Type::Unassigned:
            case ESM::QuickKeys::Type::HandToHand:
            case ESM::QuickKeys::Type::Magic:
                break;
            case ESM::QuickKeys::Type::Item:
            case ESM::QuickKeys::Type::MagicItem:
            {
                MWWorld::Ptr item = *mKey[index].button->getUserData<MWWorld::Ptr>();
                // Make sure the item is available
                if (item.isEmpty() || item.getCellRef().getCount() < 1)
                {
                    // Try searching for a compatible replacement
                    item = store.findReplacement(mKey[index].id);

                    if (!item.isEmpty())
                        mKey[index].button->setUserData(MWWorld::Ptr(item));

                    break;
                }
            }
        }
    }

    void QuickKeysMenu::onOpen()
    {
        WindowBase::onOpen();

        // Quick key index
        for (int index = 0; index < 10; ++index)
        {
            validate(index);
        }

        if (Settings::gui().mControllerMenus)
        {
            mControllerFocus = 0;
            for (size_t i = 0; i < mKey.size(); i++)
                mKey[i].button->setControllerFocus(i == mControllerFocus);
        }

        // Announce the window title for context, then the first slot follows on
        // activation (queued after the title).
        A11y::say("#{sQuickMenuTitle}", /*interrupt=*/true);
        buildAccessibility();
        mA11y.activate();
    }

    void QuickKeysMenu::onClose()
    {
        WindowBase::onClose();

        mA11y.deactivate();

        if (mAssignDialog)
            mAssignDialog->setVisible(false);
        if (mItemSelectionDialog)
            mItemSelectionDialog->setVisible(false);
        if (mMagicSelectionDialog)
            mMagicSelectionDialog->setVisible(false);
    }

    void QuickKeysMenu::onFrame(float dt)
    {
        mA11y.onFrame(dt);
    }

    std::string QuickKeysMenu::a11ySlotLabel(int index) const
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        const keyData& key = mKey[index];

        // "Quick key N" prefix -- the number is the physical key the player
        // presses, so it's the slot's identity (not positional N-of-M info).
        // The 10th slot uses 0 in-game, matching the keyboard's 0 key.
        const int spoken = key.index == 10 ? 0 : key.index;
        std::string label = "Quick key " + std::to_string(spoken);

        switch (key.type)
        {
            case ESM::QuickKeys::Type::Unassigned:
                // Honest "nothing here" rather than silence, so the user can tell
                // an empty slot from an assigned one on focus.
                label += ", " + std::string(winMgr->getGameSettingString("sNone", "None"));
                break;
            case ESM::QuickKeys::Type::HandToHand:
                label += ", #{sSkillHandtohand}";
                break;
            case ESM::QuickKeys::Type::Item:
            case ESM::QuickKeys::Type::MagicItem:
            case ESM::QuickKeys::Type::Magic:
                label += ", " + key.name;
                break;
        }
        return label;
    }

    void QuickKeysMenu::a11yActivateSlot(int index)
    {
        // Mirror a mouse click on the slot button: opens the assign chooser for
        // assignable slots; the Hand-to-hand slot (index 9) is fixed and does
        // nothing, exactly as onQuickKeyButtonClicked early-returns for it.
        onQuickKeyButtonClicked(mKey[index].button);
    }

    void QuickKeysMenu::buildAccessibility()
    {
        mA11y.clear();
        // Each slot's spoken text is computed live via describe() rather than a
        // cached label, so it always reflects the current assignment. This means
        // no rebuild is needed after an assign/unassign: when a picker closes and
        // re-announces the current slot, describe() recomputes the fresh name.
        for (int i = 0; i < 10; ++i)
            mA11y.add({ .widget = nullptr,
                .label = a11ySlotLabel(i),
                .describe = [this, i] { return a11ySlotLabel(i); },
                .activate = [this, i] { a11yActivateSlot(i); } });
    }

    void QuickKeysMenu::unassign(keyData* key)
    {
        key->button->clearUserStrings();
        key->button->setItem(MWWorld::Ptr());

        while (key->button->getChildCount()) // Destroy number label
            MyGUI::Gui::getInstance().destroyWidget(key->button->getChildAt(0));

        if (key->index == 10)
        {
            key->type = ESM::QuickKeys::Type::HandToHand;

            MyGUI::ImageBox* image = key->button->createWidget<MyGUI::ImageBox>(
                "ImageBox", MyGUI::IntCoord(14, 13, 32, 32), MyGUI::Align::Default);

            image->setImageTexture("icons\\k\\stealth_handtohand.dds");
            image->setNeedMouseFocus(false);
        }
        else
        {
            key->type = ESM::QuickKeys::Type::Unassigned;
            key->id = ESM::RefId();
            key->name.clear();

            MyGUI::TextBox* textBox = key->button->createWidgetReal<MyGUI::TextBox>(
                "SandText", MyGUI::FloatCoord(0, 0, 1, 1), MyGUI::Align::Default);

            textBox->setTextAlign(MyGUI::Align::Center);
            textBox->setCaption(MyGUI::utility::toString(key->index));
            textBox->setNeedMouseFocus(false);
        }
    }

    void QuickKeysMenu::onQuickKeyButtonClicked(MyGUI::Widget* sender)
    {
        int index = -1;
        for (int i = 0; i < 10; ++i)
        {
            if (sender == mKey[i].button || sender->getParent() == mKey[i].button)
            {
                index = i;
                break;
            }
        }
        assert(index != -1);
        if (index < 0)
        {
            mSelected = nullptr;
            return;
        }

        mSelected = &mKey[index];

        // prevent reallocation of zero key from ESM::QuickKeys::Type::HandToHand
        if (mSelected->index == 10)
            return;

        // open assign dialog
        if (!mAssignDialog)
            mAssignDialog = std::make_unique<QuickKeysMenuAssign>(this);

        mAssignDialog->setVisible(true);
    }

    void QuickKeysMenu::onOkButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_QuickKeysMenu);
    }

    void QuickKeysMenu::onItemButtonClicked(MyGUI::Widget* /*sender*/)
    {
        if (!mItemSelectionDialog)
        {
            mItemSelectionDialog = std::make_unique<ItemSelectionDialog>("#{sQuickMenu6}");
            mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &QuickKeysMenu::onAssignItem);
            mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &QuickKeysMenu::onAssignItemCancel);
        }

        // Hide the assign chooser BEFORE showing the picker so the screen-reader
        // suspend/resume chain stays strict LIFO: the assign dialog resumes the
        // quick-keys window first, then the picker suspends that. (Doing it the
        // other way would make the picker suspend the assign screen, after which
        // the assign's close would resume the quick-keys screen and steal input
        // back from the picker.) The modal-stack end state is unchanged.
        mAssignDialog->setVisible(false);

        mItemSelectionDialog->setVisible(true);
        mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
        mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyUsableItems);
    }

    void QuickKeysMenu::onMagicButtonClicked(MyGUI::Widget* /*sender*/)
    {
        if (!mMagicSelectionDialog)
        {
            mMagicSelectionDialog = std::make_unique<MagicSelectionDialog>(this);
        }

        // Hide the chooser before showing the picker -- see onItemButtonClicked
        // for why the screen-reader suspend/resume chain must stay LIFO.
        mAssignDialog->setVisible(false);

        mMagicSelectionDialog->setVisible(true);
        mMagicSelectionDialog->setActiveControllerWindow(true);
    }

    void QuickKeysMenu::onUnassignButtonClicked(MyGUI::Widget* /*sender*/)
    {
        unassign(mSelected);
        mAssignDialog->setVisible(false);
    }

    void QuickKeysMenu::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        mAssignDialog->setVisible(false);
    }

    void QuickKeysMenu::assignItem(MWWorld::Ptr item)
    {
        assert(mSelected);

        while (mSelected->button->getChildCount()) // Destroy number label
            MyGUI::Gui::getInstance().destroyWidget(mSelected->button->getChildAt(0));

        mSelected->type = ESM::QuickKeys::Type::Item;
        mSelected->id = item.getCellRef().getRefId();
        mSelected->name = item.getClass().getName(item);

        mSelected->button->setItem(item, ItemWidget::Barter);
        mSelected->button->setUserString("ToolTipType", "ItemPtr");
        mSelected->button->setUserData(item);

        if (mItemSelectionDialog)
            mItemSelectionDialog->setVisible(false);
    }

    void QuickKeysMenu::onAssignItem(MWWorld::Ptr item)
    {
        assignItem(item);
        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
    }

    void QuickKeysMenu::onAssignItemCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void QuickKeysMenu::onAssignMagicItem(MWWorld::Ptr item)
    {
        assert(mSelected);

        while (mSelected->button->getChildCount()) // Destroy number label
            MyGUI::Gui::getInstance().destroyWidget(mSelected->button->getChildAt(0));

        mSelected->type = ESM::QuickKeys::Type::MagicItem;
        mSelected->id = item.getCellRef().getRefId();
        mSelected->name = item.getClass().getName(item);

        float scale = 1.f;
        MyGUI::ITexture* texture
            = MyGUI::RenderManager::getInstance().getTexture("textures\\menu_icon_select_magic_magic.dds");
        if (texture)
            scale = texture->getHeight() / 64.f;

        mSelected->button->setFrame("textures\\menu_icon_select_magic_magic.dds",
            MyGUI::IntCoord(0, 0, static_cast<int>(44 * scale), static_cast<int>(44 * scale)));
        mSelected->button->setIcon(item);

        mSelected->button->setUserString("ToolTipType", "ItemPtr");
        mSelected->button->setUserData(MWWorld::Ptr(item));

        if (mMagicSelectionDialog)
            mMagicSelectionDialog->setVisible(false);
    }

    void QuickKeysMenu::onAssignMagic(const ESM::RefId& spellId)
    {
        assert(mSelected);
        while (mSelected->button->getChildCount()) // Destroy number label
            MyGUI::Gui::getInstance().destroyWidget(mSelected->button->getChildAt(0));

        const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();
        const ESM::Spell* spell = esmStore.get<ESM::Spell>().find(spellId);

        mSelected->type = ESM::QuickKeys::Type::Magic;
        mSelected->id = spellId;
        mSelected->name = spell->mName;

        mSelected->button->setItem(MWWorld::Ptr());
        mSelected->button->setUserString("ToolTipType", "Spell");
        mSelected->button->setUserString("Spell", spellId.serialize());

        // use the icon of the first effect
        const ESM::MagicEffect* effect
            = esmStore.get<ESM::MagicEffect>().find(spell->mEffects.mList.front().mData.mEffectID);

        const VFS::Path::Normalized iconPath = Misc::ResourceHelpers::correctBigIconPath(
            VFS::Path::toNormalized(effect->mIcon), *MWBase::Environment::get().getResourceSystem()->getVFS());

        float scale = 1.f;
        MyGUI::ITexture* texture
            = MyGUI::RenderManager::getInstance().getTexture("textures\\menu_icon_select_magic.dds");
        if (texture)
            scale = texture->getHeight() / 64.f;

        const int diameter = static_cast<int>(44 * scale);
        mSelected->button->setFrame("textures\\menu_icon_select_magic.dds", MyGUI::IntCoord(0, 0, diameter, diameter));
        mSelected->button->setIcon(iconPath);

        if (mMagicSelectionDialog)
            mMagicSelectionDialog->setVisible(false);
    }

    void QuickKeysMenu::onAssignMagicCancel()
    {
        mMagicSelectionDialog->setVisible(false);
    }

    void QuickKeysMenu::updateActivatedQuickKey()
    {
        // there is no delayed action, nothing to do.
        if (!mActivated)
            return;

        activateQuickKey(mActivated->index);
    }

    void QuickKeysMenu::activateQuickKey(int index)
    {
        assert(index >= 1 && index <= 10);

        keyData* key = &mKey[index - 1];

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);
        const MWMechanics::CreatureStats& playerStats = player.getClass().getCreatureStats(player);

        validate(index - 1);

        // Delay action executing,
        // if player is busy for now (casting a spell, attacking someone, etc.)
        bool isDelayNeeded = MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(player)
            || playerStats.getKnockedDown() || playerStats.getHitRecovery();

        bool isReturnNeeded = playerStats.isParalyzed() || playerStats.isDead();

        if (isReturnNeeded)
        {
            return;
        }
        else if (isDelayNeeded)
        {
            mActivated = key;
            return;
        }
        else
        {
            mActivated = nullptr;
        }

        if (key->type == ESM::QuickKeys::Type::Item || key->type == ESM::QuickKeys::Type::MagicItem)
        {
            MWWorld::Ptr item = *key->button->getUserData<MWWorld::Ptr>();

            MWWorld::ContainerStoreIterator it = store.begin();
            for (; it != store.end(); ++it)
            {
                if (*it == item)
                    break;
            }

            // Is the quickkey item not in the inventory?
            if (it == store.end())
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sQuickMenu5} " + key->name);
                return;
            }

            if (key->type == ESM::QuickKeys::Type::Item)
            {
                if (!store.isEquipped(item.getCellRef().getRefId()))
                    MWBase::Environment::get().getWindowManager()->useItem(item);
                MWWorld::ConstContainerStoreIterator rightHand
                    = store.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                // change draw state only if the item is in player's right hand
                if (rightHand != store.end() && item == *rightHand)
                {
                    MWBase::Environment::get().getWorld()->getPlayer().setDrawState(MWMechanics::DrawState::Weapon);
                }
                // Confirm what the key did -- a weapon/item activation gives no
                // other audible signal to a blind player.
                A11y::say(key->name, /*interrupt=*/true);
            }
            else if (key->type == ESM::QuickKeys::Type::MagicItem)
            {
                // equip, if it can be equipped and isn't yet equipped
                if (!item.getClass().getEquipmentSlots(item).first.empty() && !store.isEquipped(item))
                {
                    MWBase::Environment::get().getWindowManager()->useItem(item);

                    // make sure that item was successfully equipped
                    if (!store.isEquipped(item))
                        return;
                }

                store.setSelectedEnchantItem(it);
                // to reset WindowManager::mSelectedSpell immediately
                MWBase::Environment::get().getWindowManager()->setSelectedEnchantItem(*it);

                MWBase::Environment::get().getWorld()->getPlayer().setDrawState(MWMechanics::DrawState::Spell);
                // Enchanted item is now the readied magic.
                A11y::say(key->name + " ready", /*interrupt=*/true);
            }
        }
        else if (key->type == ESM::QuickKeys::Type::Magic)
        {
            const ESM::RefId& spellId = key->id;

            // Make sure the player still has this spell
            MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
            MWMechanics::Spells& spells = stats.getSpells();

            if (!spells.hasSpell(spellId))
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sQuickMenu5} " + key->name);
                return;
            }

            store.setSelectedEnchantItem(store.end());
            MWBase::Environment::get().getWindowManager()->setSelectedSpell(
                spellId, int(MWMechanics::getSpellSuccessChance(spellId, player)));
            MWBase::Environment::get().getWorld()->getPlayer().setDrawState(MWMechanics::DrawState::Spell);
            // Spell is now readied for casting.
            A11y::say(key->name + " ready", /*interrupt=*/true);
        }
        else if (key->type == ESM::QuickKeys::Type::HandToHand)
        {
            store.unequipSlot(MWWorld::InventoryStore::Slot_CarriedRight);
            MWBase::Environment::get().getWorld()->getPlayer().setDrawState(MWMechanics::DrawState::Weapon);
            A11y::say("#{sSkillHandtohand}", /*interrupt=*/true);
        }
        else if (key->type == ESM::QuickKeys::Type::Unassigned)
        {
            // Pressing an unassigned number does nothing in-game; say so rather
            // than leave the player wondering whether the key registered.
            A11y::say(std::string(MWBase::Environment::get().getWindowManager()->getGameSettingString(
                          "sNone", "None")),
                /*interrupt=*/true);
        }

        // Updates the state of equipped/not equipped (skin) in spellwindow
        MWBase::Environment::get().getWindowManager()->updateSpellWindow();
    }

    bool QuickKeysMenu::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
            onQuickKeyButtonClicked(mKey[mControllerFocus].button);
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onOkButtonClicked(mOkButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP || arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            mControllerFocus = (mControllerFocus + 5) % 10;
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT)
        {
            if (mControllerFocus == 0)
                mControllerFocus = 4;
            else if (mControllerFocus == 5)
                mControllerFocus = 9;
            else
                mControllerFocus--;
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
        {
            if (mControllerFocus == 4)
                mControllerFocus = 0;
            else if (mControllerFocus == 9)
                mControllerFocus = 5;
            else
                mControllerFocus++;
        }

        for (size_t i = 0; i < mKey.size(); i++)
            mKey[i].button->setControllerFocus(i == mControllerFocus);

        return true;
    }

    // ---------------------------------------------------------------------------------------------------------

    QuickKeysMenuAssign::QuickKeysMenuAssign(QuickKeysMenu* parent)
        : WindowModal("openmw_quickkeys_menu_assign.layout")
        , mParent(parent)
    {
        getWidget(mLabel, "Label");
        getWidget(mItemButton, "ItemButton");
        getWidget(mMagicButton, "MagicButton");
        getWidget(mUnassignButton, "UnassignButton");
        getWidget(mCancelButton, "CancelButton");

        mItemButton->eventMouseButtonClick += MyGUI::newDelegate(mParent, &QuickKeysMenu::onItemButtonClicked);
        mMagicButton->eventMouseButtonClick += MyGUI::newDelegate(mParent, &QuickKeysMenu::onMagicButtonClicked);
        mUnassignButton->eventMouseButtonClick += MyGUI::newDelegate(mParent, &QuickKeysMenu::onUnassignButtonClicked);
        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(mParent, &QuickKeysMenu::onCancelButtonClicked);

        int maxWidth = mLabel->getTextSize().width + 24;
        maxWidth = std::max(maxWidth, mItemButton->getTextSize().width + 24);
        maxWidth = std::max(maxWidth, mMagicButton->getTextSize().width + 24);
        maxWidth = std::max(maxWidth, mUnassignButton->getTextSize().width + 24);
        maxWidth = std::max(maxWidth, mCancelButton->getTextSize().width + 24);

        mMainWidget->setSize(maxWidth + 24, mMainWidget->getHeight());
        mLabel->setSize(maxWidth, mLabel->getHeight());

        mItemButton->setCoord((maxWidth - mItemButton->getTextSize().width - 24) / 2 + 8, mItemButton->getTop(),
            mItemButton->getTextSize().width + 24, mItemButton->getHeight());
        mMagicButton->setCoord((maxWidth - mMagicButton->getTextSize().width - 24) / 2 + 8, mMagicButton->getTop(),
            mMagicButton->getTextSize().width + 24, mMagicButton->getHeight());
        mUnassignButton->setCoord((maxWidth - mUnassignButton->getTextSize().width - 24) / 2 + 8,
            mUnassignButton->getTop(), mUnassignButton->getTextSize().width + 24, mUnassignButton->getHeight());
        mCancelButton->setCoord((maxWidth - mCancelButton->getTextSize().width - 24) / 2 + 8, mCancelButton->getTop(),
            mCancelButton->getTextSize().width + 24, mCancelButton->getHeight());

        if (Settings::gui().mControllerMenus)
        {
            mDisableGamepadCursor = true;
            mItemButton->setStateSelected(true);
            mControllerButtons.mA = "#{Interface:Select}";
            mControllerButtons.mB = "#{Interface:Cancel}";
        }

        center();

        // Screen-reader setup: invisible anchor holds key focus; the four
        // buttons become navigable options.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor, /*ownModal=*/true);
    }

    void QuickKeysMenuAssign::buildAccessibility()
    {
        mA11y.clear();
        mA11y.add({ .widget = nullptr, .label = "#{sQuickMenu2}",
            .activate = [this] { mParent->onItemButtonClicked(mItemButton); } });
        mA11y.add({ .widget = nullptr, .label = "#{sQuickMenu3}",
            .activate = [this] { mParent->onMagicButtonClicked(mMagicButton); } });
        mA11y.add({ .widget = nullptr, .label = "#{sQuickMenu4}",
            .activate = [this] { mParent->onUnassignButtonClicked(mUnassignButton); } });
        mA11y.add({ .widget = nullptr, .label = "#{Interface:Cancel}",
            .activate = [this] { mParent->onCancelButtonClicked(mCancelButton); } });
    }

    void QuickKeysMenuAssign::onOpen()
    {
        WindowModal::onOpen();

        // Suspend the quick-keys window's screen underneath us, then take input.
        mA11yPrev = A11y::UiManager::instance().active();
        if (mA11yPrev)
            mA11yPrev->suspend();

        // Announce which slot is being assigned (sQuickMenu1 is the dialog's own
        // instruction line, e.g. "Select an action for this key").
        const int slot = mParent->a11ySelectedSlot();
        std::string intro = "#{sQuickMenu1}";
        if (slot != -1)
            intro += ", quick key " + std::to_string(slot == 10 ? 0 : slot);
        A11y::say(intro, /*interrupt=*/true);

        buildAccessibility();
        mA11y.activate(); // first option announced after the intro
    }

    void QuickKeysMenuAssign::onClose()
    {
        WindowModal::onClose();
        mA11y.deactivate();
        if (mA11yPrev)
        {
            mA11yPrev->resume();
            mA11yPrev->announceCurrent();
            mA11yPrev = nullptr;
        }
    }

    void QuickKeysMenuAssign::onFrame(float dt)
    {
        mA11y.onFrame(dt);
    }

    bool QuickKeysMenuAssign::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            if (mControllerFocus == 0)
                mParent->onItemButtonClicked(mItemButton);
            else if (mControllerFocus == 1)
                mParent->onMagicButtonClicked(mMagicButton);
            else if (mControllerFocus == 2)
                mParent->onUnassignButtonClicked(mUnassignButton);
            else if (mControllerFocus == 3)
                mParent->onCancelButtonClicked(mCancelButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
            mParent->onCancelButtonClicked(mCancelButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
            mControllerFocus = wrap(mControllerFocus, 4, -1);
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            mControllerFocus = wrap(mControllerFocus, 4, 1);

        mItemButton->setStateSelected(mControllerFocus == 0);
        mMagicButton->setStateSelected(mControllerFocus == 1);
        mUnassignButton->setStateSelected(mControllerFocus == 2);
        mCancelButton->setStateSelected(mControllerFocus == 3);

        return true;
    }

    void QuickKeysMenu::write(ESM::ESMWriter& writer)
    {
        writer.startRecord(ESM::REC_KEYS);

        ESM::QuickKeys keys;

        // NB: The quick key with index 9 always has Hand-to-Hand type and must not be saved
        for (int i = 0; i < 9; ++i)
        {
            ItemWidget* button = mKey[i].button;

            const ESM::QuickKeys::Type type = mKey[i].type;

            ESM::QuickKeys::QuickKey key;
            key.mType = type;

            switch (type)
            {
                case ESM::QuickKeys::Type::Unassigned:
                case ESM::QuickKeys::Type::HandToHand:
                    break;
                case ESM::QuickKeys::Type::Item:
                case ESM::QuickKeys::Type::MagicItem:
                {
                    MWWorld::Ptr item = *button->getUserData<MWWorld::Ptr>();
                    key.mId = item.getCellRef().getRefId();
                    break;
                }
                case ESM::QuickKeys::Type::Magic:
                    key.mId = ESM::RefId::deserialize(button->getUserString("Spell"));
                    break;
            }

            keys.mKeys.push_back(key);
        }

        keys.save(writer);

        writer.endRecord(ESM::REC_KEYS);
    }

    void QuickKeysMenu::readRecord(ESM::ESMReader& reader, uint32_t type)
    {
        if (type != ESM::REC_KEYS)
            return;

        ESM::QuickKeys keys;
        keys.load(reader);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);

        auto assign = [this](ESM::QuickKeys::Type keyType, MWWorld::Ptr item) {
            if (keyType == ESM::QuickKeys::Type::Item)
                assignItem(item);
            else // if (quickKey.mType == ESM::QuickKeys::Type::MagicItem)
                onAssignMagicItem(item);
        };

        int i = 0;
        for (ESM::QuickKeys::QuickKey& quickKey : keys.mKeys)
        {
            // NB: The quick key with index 9 always has Hand-to-Hand type and must not be loaded
            if (i >= 9)
                return;

            mSelected = &mKey[i];

            switch (quickKey.mType)
            {
                case ESM::QuickKeys::Type::Magic:
                    if (MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(quickKey.mId))
                        onAssignMagic(quickKey.mId);
                    break;
                case ESM::QuickKeys::Type::Item:
                case ESM::QuickKeys::Type::MagicItem:
                {
                    // Find the item by id
                    MWWorld::Ptr item = store.findReplacement(quickKey.mId);
                    if (item.isEmpty())
                    {
                        unassign(mSelected);
                        if (!quickKey.mId.empty())
                        {
                            // Fallback to a temporary object for UI display purposes
                            if (MWBase::Environment::get().getESMStore()->find(quickKey.mId) != 0)
                            {
                                // Tie temporary item lifetime to this window
                                mTemp.emplace_back(*MWBase::Environment::get().getESMStore(), quickKey.mId, 0);
                                assign(quickKey.mType, mTemp.back().getPtr());
                            }
                            else
                                Log(Debug::Warning) << "Failed to load quick key " << (i + 1)
                                                    << ": could not find object " << quickKey.mId;
                        }
                    }
                    else
                        assign(quickKey.mType, item);

                    break;
                }
                case ESM::QuickKeys::Type::Unassigned:
                case ESM::QuickKeys::Type::HandToHand:
                    unassign(mSelected);
                    break;
            }

            ++i;
        }
    }

    // ---------------------------------------------------------------------------------------------------------

    MagicSelectionDialog::MagicSelectionDialog(QuickKeysMenu* parent)
        : WindowModal("openmw_magicselection_dialog.layout")
        , mParent(parent)
    {
        getWidget(mCancelButton, "CancelButton");
        getWidget(mMagicList, "MagicList");
        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &MagicSelectionDialog::onCancelButtonClicked);

        mMagicList->setShowCostColumn(false);
        mMagicList->setHighlightSelected(false);
        mMagicList->eventSpellClicked += MyGUI::newDelegate(this, &MagicSelectionDialog::onModelIndexSelected);

        if (Settings::gui().mControllerMenus)
        {
            mControllerButtons.mA = "#{Interface:Select}";
            mControllerButtons.mB = "#{Interface:Cancel}";
        }

        center();

        // Screen-reader setup: invisible anchor holds key focus; each spell /
        // enchanted item in the list becomes a navigable option, mirroring the
        // spell list in the magic window.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor, /*ownModal=*/true);
    }

    void MagicSelectionDialog::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        exit();
    }

    bool MagicSelectionDialog::exit()
    {
        mParent->onAssignMagicCancel();
        return true;
    }

    void MagicSelectionDialog::onOpen()
    {
        WindowModal::onOpen();

        mMagicList->setModel(new SpellModel(MWMechanics::getPlayer()));

        // Suspend the screen underneath (the quick-keys window), then take input.
        // Activation is deferred to the first onFrame: the SpellModel above needs
        // a frame to populate its item list before buildAccessibility can read
        // it. Announce the title now; the first spell follows on activation.
        mA11yPrev = A11y::UiManager::instance().active();
        if (mA11yPrev)
            mA11yPrev->suspend();
        A11y::say("#{sQuickMenu3}", /*interrupt=*/true);
        mA11yPendingActivate = true;
    }

    void MagicSelectionDialog::onClose()
    {
        WindowModal::onClose();
        mA11yPendingActivate = false;
        mA11y.deactivate();
        if (mA11yPrev)
        {
            mA11yPrev->resume();
            mA11yPrev->announceCurrent();
            mA11yPrev = nullptr;
        }
    }

    void MagicSelectionDialog::onFrame(float dt)
    {
        if (mA11yPendingActivate)
        {
            mA11yPendingActivate = false;
            buildAccessibility();
            mA11y.activate();
        }
        mA11y.onFrame(dt);
    }

    void MagicSelectionDialog::buildAccessibility()
    {
        mA11y.clear();
        SpellModel* model = mMagicList->getModel();
        if (!model)
            return;
        for (size_t i = 0; i < model->getItemCount(); ++i)
        {
            const int index = static_cast<int>(i);
            const Spell spell = model->getItem(index);
            mA11y.add({ .widget = nullptr,
                .label = magicPickerLabel(spell),
                .section = magicPickerSection(spell),
                .tooltips = [spell] { return magicPickerTooltip(spell); },
                .activate = [this, index] { onModelIndexSelected(index); } });
        }
    }

    void MagicSelectionDialog::onModelIndexSelected(SpellModel::ModelIndex index)
    {
        const Spell& spell = mMagicList->getModel()->getItem(index);
        if (spell.mType == Spell::Type_EnchantedItem)
            mParent->onAssignMagicItem(spell.mItem);
        else
            mParent->onAssignMagic(spell.mId);
    }

    bool MagicSelectionDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onCancelButtonClicked(mCancelButton);
        else
            mMagicList->onControllerButton(arg.button);

        return true;
    }

    void MagicSelectionDialog::setActiveControllerWindow(bool active)
    {
        if (!Settings::gui().mControllerMenus)
            return;

        mMagicList->setActiveControllerWindow(active);
        WindowBase::setActiveControllerWindow(active);
    }
}
