#include "spellwindow.hpp"

#include <MyGUI_EditBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_Window.h>

#include <components/esm3/loadbsgn.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/misc/strings/format.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/datetimemanager.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/player.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/spells.hpp"
#include "../mwmechanics/spellutil.hpp"

#include <components/esm3/loadspel.hpp>

#include "accessibility/activeeffects.hpp"
#include "accessibility/panegroup.hpp"
#include "accessibility/speech.hpp"
#include "accessibility/spelltext.hpp"

#include "confirmationdialog.hpp"
#include "spellicons.hpp"
#include "spellview.hpp"
#include "statswindow.hpp"

namespace MWGui
{

    SpellWindow::SpellWindow(DragAndDrop* drag)
        : WindowPinnableBase("openmw_spell_window.layout")
        , NoDrop(drag, mMainWidget)
        , mSpellView(nullptr)
        , mUpdateTimer(0.0f)
    {
        mSpellIcons = std::make_unique<SpellIcons>();

        MyGUI::Widget* deleteButton;
        getWidget(deleteButton, "DeleteSpellButton");

        getWidget(mSpellView, "SpellView");
        getWidget(mEffectBox, "EffectsBox");
        getWidget(mFilterEdit, "FilterEdit");

        mSpellView->eventSpellClicked += MyGUI::newDelegate(this, &SpellWindow::onModelIndexSelected);
        mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &SpellWindow::onFilterChanged);
        deleteButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SpellWindow::onDeleteClicked);

        setCoord(498, 300, 302, 300);

        // Adjust the spell filtering widget size because of MyGUI limitations.
        int filterWidth = mSpellView->getSize().width - deleteButton->getSize().width - 3;
        mFilterEdit->setSize(filterWidth, mFilterEdit->getSize().height);

        // Screen-reader setup: an invisible anchor holds key focus while the
        // spell list is navigated by index (the rows are drawn by the custom
        // SpellView, not as individual widgets), as in the inventory window.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);
        mA11yFilterEdit.attach(mFilterEdit);
        mA11yFilterEdit.setActive(false);
        // Extra key on the spell list: Delete asks to delete the selected spell
        // (the native Shift+click delete; powers/racial/sign spells are
        // protected, matching vanilla rules).
        mA11y.setExtraKeyHandler([this](MyGUI::KeyCode key) -> bool {
            if (key == MyGUI::KeyCode::Delete)
            {
                const size_t cur = mA11y.currentIndex();
                if (cur != A11y::Screen::npos && cur >= mA11yItemBase && mSpellView->getModel())
                {
                    const int index = static_cast<int>(cur - mA11yItemBase);
                    if (index < static_cast<int>(mSpellView->getModel()->getItemCount()))
                        a11yDeleteSpell(index);
                }
                return true;
            }
            return false;
        });

        if (Settings::gui().mControllerMenus)
        {
            setPinButtonVisible(false);
            mControllerButtons.mA = "#{Interface:Select}";
            mControllerButtons.mB = "#{Interface:Back}";
            mControllerButtons.mR3 = "#{Interface:Info}";
        }
    }

    void SpellWindow::onPinToggled()
    {
        Settings::windows().mSpellsPin.set(mPinned);

        MWBase::Environment::get().getWindowManager()->setSpellVisibility(!mPinned);
    }

    void SpellWindow::onTitleDoubleClicked()
    {
        if (Settings::gui().mControllerMenus)
            return;
        else if (MyGUI::InputManager::getInstance().isShiftPressed())
            MWBase::Environment::get().getWindowManager()->toggleMaximized(this);
        else if (!mPinned)
            MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Magic);
    }

    void SpellWindow::onOpen()
    {
        // Reset the filter focus when opening the window
        MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        if (focus == mFilterEdit)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);

        updateSpells();

        // Screen reader: in the standalone inventory the Magic window is shown
        // next to Stats/Inventory/Map, so enrol in the PaneGroup (Magic = pane
        // 2) and let Tab switch between them. updateSpells() has rebuilt the
        // model, so build the spoken list from it now.
        buildAccessibility();
        A11y::PaneGroup::instance().enrol(&mA11y,
            std::string(MWBase::Environment::get().getWindowManager()->getGameSettingString("sMagic", "Magic")), 2);
    }

    void SpellWindow::onClose()
    {
        A11y::PaneGroup::instance().withdraw(&mA11y);
        mA11y.deactivate();
    }

    void SpellWindow::onFrame(float dt)
    {
        NoDrop::onFrame(dt);
        mUpdateTimer += dt;
        if (0.5f < mUpdateTimer)
        {
            mUpdateTimer = 0;
            mSpellView->incrementalUpdate();
        }

        // Update effects if the time is unpaused for any reason (e.g. the window is pinned)
        if (!MWBase::Environment::get().getWorld()->getTimeManager()->isPaused())
            mSpellIcons->updateWidgets(mEffectBox, false);

        mA11yFilterEdit.onFrame();

        // When the user finishes editing the name filter, the matching set of
        // spells has changed: rebuild the spoken list (the on-screen SpellView
        // is already kept current by onFilterChanged). Defer until edit mode
        // ends so we don't churn the list on every keystroke.
        const bool editing = mA11y.editing();
        if (mA11yWasEditing && !editing && mA11y.isActive())
            a11yRebuildKeepingCursor();
        mA11yWasEditing = editing;

        // Let the PaneGroup activate this pane if it's the one to land on.
        if (A11y::PaneGroup::instance().contains(&mA11y))
            A11y::PaneGroup::instance().maybeActivateInitial(&mA11y);

        mA11y.onFrame(dt);
    }

    void SpellWindow::updateSpells()
    {
        mSpellIcons->updateWidgets(mEffectBox, false);

        mSpellView->setModel(new SpellModel(MWMechanics::getPlayer(), mFilterEdit->getCaption()));
    }

    void SpellWindow::onEnchantedItemSelected(MWWorld::Ptr item, bool alreadyEquipped)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);

        // retrieve ContainerStoreIterator to the item
        MWWorld::ContainerStoreIterator it = store.begin();
        for (; it != store.end(); ++it)
        {
            if (*it == item)
            {
                break;
            }
        }
        if (it == store.end())
            throw std::runtime_error("can't find selected item");

        // equip, if it can be equipped and is not already equipped
        if (!alreadyEquipped && !item.getClass().getEquipmentSlots(item).first.empty())
        {
            MWBase::Environment::get().getWindowManager()->useItem(item);
            // make sure that item was successfully equipped
            if (!store.isEquipped(item))
                return;
        }

        store.setSelectedEnchantItem(it);
        // to reset WindowManager::mSelectedSpell immediately
        MWBase::Environment::get().getWindowManager()->setSelectedEnchantItem(*it);

        updateSpells();
    }

    void SpellWindow::askDeleteSpell(const ESM::RefId& spellId)
    {
        // delete spell, if allowed
        const ESM::Spell* spell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().find(spellId);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        const ESM::RefId& raceId = player.get<ESM::NPC>()->mBase->mRace;
        const ESM::Race* race = MWBase::Environment::get().getESMStore()->get<ESM::Race>().find(raceId);
        // can't delete racial spells, birthsign spells or powers
        bool isInherent = race->mPowers.exists(spell->mId) || spell->mData.mType == ESM::Spell::ST_Power;
        const ESM::RefId& signId = MWBase::Environment::get().getWorld()->getPlayer().getBirthSign();
        if (!isInherent && !signId.empty())
        {
            const ESM::BirthSign* sign = MWBase::Environment::get().getESMStore()->get<ESM::BirthSign>().find(signId);
            isInherent = sign->mPowers.exists(spell->mId);
        }

        const auto windowManager = MWBase::Environment::get().getWindowManager();
        if (isInherent)
        {
            windowManager->messageBox("#{sDeleteSpellError}");
        }
        else
        {
            // ask for confirmation
            mSpellToDelete = spellId;
            ConfirmationDialog* dialog = windowManager->getConfirmationDialog();
            std::string question{ windowManager->getGameSettingString("sQuestionDeleteSpell", "Delete %s?") };
            question = Misc::StringUtils::format(question, spell->mName);
            dialog->askForConfirmation(question);
            dialog->eventOkClicked.clear();
            dialog->eventOkClicked += MyGUI::newDelegate(this, &SpellWindow::onDeleteSpellAccept);
            dialog->eventCancelClicked.clear();
        }
    }

    void SpellWindow::onModelIndexSelected(SpellModel::ModelIndex index)
    {
        const Spell& spell = mSpellView->getModel()->getItem(index);
        if (spell.mType == Spell::Type_EnchantedItem)
        {
            onEnchantedItemSelected(spell.mItem, spell.mActive);
        }
        else
        {
            if (MyGUI::InputManager::getInstance().isShiftPressed())
                askDeleteSpell(spell.mId);
            else
                onSpellSelected(spell.mId);
        }
    }

    void SpellWindow::onFilterChanged(MyGUI::EditBox* sender)
    {
        mSpellView->setModel(new SpellModel(MWMechanics::getPlayer(), sender->getCaption()));
    }

    void SpellWindow::onDeleteClicked(MyGUI::Widget* widget)
    {
        SpellModel::ModelIndex selected = mSpellView->getModel()->getSelectedIndex();
        if (selected < 0)
            return;

        const Spell& spell = mSpellView->getModel()->getItem(selected);
        if (spell.mType != Spell::Type_EnchantedItem)
            askDeleteSpell(spell.mId);
    }

    void SpellWindow::onSpellSelected(const ESM::RefId& spellId)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);
        store.setSelectedEnchantItem(store.end());
        MWBase::Environment::get().getWindowManager()->setSelectedSpell(
            spellId, int(MWMechanics::getSpellSuccessChance(spellId, player)));

        updateSpells();
    }

    void SpellWindow::onDeleteSpellAccept()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        MWMechanics::Spells& spells = stats.getSpells();

        if (MWBase::Environment::get().getWindowManager()->getSelectedSpell() == mSpellToDelete)
            MWBase::Environment::get().getWindowManager()->unsetSelectedSpell();

        spells.remove(mSpellToDelete);

        updateSpells();

        // Refresh the spoken list now the spell is gone, keeping the cursor on
        // the same row (which now holds the next spell, or the name filter if
        // the list emptied). The confirmation dialog was modal over our virtual
        // screen, which stays active, so no re-activation is needed.
        if (mA11y.isActive())
            a11yRebuildKeepingCursor();
    }

    std::string SpellWindow::a11ySpellLabel(const Spell& spell) const
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        std::string label = spell.mName;
        if (spell.mType == Spell::Type_EnchantedItem && spell.mCount > 1)
            label += " (" + std::to_string(spell.mCount) + ")";

        // Surface the key extra signal for this row: which spell is readied, and
        // (for enchanted items) whether the item is equipped.
        if (spell.mSelected)
            label += ", " + std::string(winMgr->getGameSettingString("sSelect", "Selected"));
        if (spell.mType == Spell::Type_EnchantedItem && spell.mActive)
            label += ", " + std::string(winMgr->getGameSettingString("sEquip", "Equipped"));
        return label;
    }

    std::string SpellWindow::a11ySpellSection(const Spell& spell) const
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

    void SpellWindow::buildAccessibility()
    {
        mA11y.clear();

        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        // Leading option: the name-filter field.
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sName", "Name")),
            .value =
                [this] {
                    const std::string text = mFilterEdit->getOnlyText().asUTF8();
                    return text.empty() ? std::string("blank") : text;
                },
            .edit = &mA11yFilterEdit });

        // Leading option: the player's currently-active magic effects, as an
        // expandable submenu (Enter to expand, Up/Down through the effects).
        // Each effect's source is its section, announced before the effect.
        // This is the same set shown as the on-screen effect-icon row: cast
        // spells, constant enchantments, racial/birthsign abilities, potions,
        // diseases, summon buffs, etc. Recomputed each time it's opened so it
        // reflects live durations.
        mA11y.add({ .widget = nullptr,
            .label = "Active effects",
            .children = [this] { return a11yActiveEffectItems(); } });

        mA11yItemBase = 2;

        // One option per power/spell/enchanted item, grouped into sections
        // (Powers / Spells / Magic Items) exactly as the visual list groups
        // them. Enter selects/equips; Delete (extra-key handler) deletes.
        SpellModel* model = mSpellView->getModel();
        if (model)
        {
            for (size_t i = 0; i < model->getItemCount(); ++i)
            {
                const int index = static_cast<int>(i);
                const Spell spell = model->getItem(index);
                mA11y.add({ .widget = nullptr,
                    .label = a11ySpellLabel(spell),
                    .section = a11ySpellSection(spell),
                    .tooltips = [spell] { return A11y::spellModelTooltipLines(spell); },
                    .activate = [this, index] { a11yActivateSpell(index); } });
            }
        }
    }

    void SpellWindow::a11yRebuildKeepingCursor()
    {
        const size_t cursor = mA11y.currentIndex();
        buildAccessibility();
        SpellModel* model = mSpellView->getModel();
        const size_t itemCount = model ? model->getItemCount() : 0;
        if (cursor == A11y::Screen::npos)
            mA11y.focusFirst(/*announce=*/true);
        else if (cursor < mA11yItemBase)
            mA11y.selectIndex(cursor, /*announce=*/true); // stayed on the name filter
        else if (itemCount == 0)
            mA11y.selectIndex(mA11yItemBase - 1, /*announce=*/true); // no spells: land on name filter
        else
        {
            const size_t item = std::min(cursor - mA11yItemBase, itemCount - 1);
            mA11y.selectIndex(mA11yItemBase + item, /*announce=*/true);
        }
    }

    void SpellWindow::a11yActivateSpell(int modelIndex)
    {
        SpellModel* model = mSpellView->getModel();
        if (!model || modelIndex < 0 || modelIndex >= static_cast<int>(model->getItemCount()))
            return;

        // Drive the same path a click would (select spell / equip-select an
        // enchanted item). This re-readies the spell but does NOT reorder the
        // list, so we just rebuild in place and keep the cursor on the row,
        // which now reports the new "Selected"/"Equipped" state.
        const Spell spell = model->getItem(modelIndex);
        if (spell.mType == Spell::Type_EnchantedItem)
            onEnchantedItemSelected(spell.mItem, spell.mActive);
        else
            onSpellSelected(spell.mId);

        a11yRebuildKeepingCursor();
    }

    void SpellWindow::a11yDeleteSpell(int modelIndex)
    {
        SpellModel* model = mSpellView->getModel();
        if (!model || modelIndex < 0 || modelIndex >= static_cast<int>(model->getItemCount()))
            return;

        const Spell spell = model->getItem(modelIndex);
        // Enchanted items aren't spells and can't be "deleted" here (matches the
        // visual delete button, which ignores them).
        if (spell.mType == Spell::Type_EnchantedItem)
        {
            A11y::say(MWBase::Environment::get().getWindowManager()->getGameSettingString(
                          "sDeleteSpellError", "You cannot delete this."),
                /*interrupt=*/true);
            return;
        }

        // Routes through the native confirmation dialog (already accessible):
        // protected powers/racial/sign spells show an error instead. After the
        // dialog's OK runs onDeleteSpellAccept -> updateSpells, the spoken list
        // is rebuilt on the next edit/selection; rebuild here too so a deletion
        // confirmed via the dialog leaves the cursor sensible.
        askDeleteSpell(spell.mId);
    }

    std::vector<A11y::SubItem> SpellWindow::a11yActiveEffectItems() const
    {
        std::vector<A11y::SubItem> items;
        for (const A11y::ActiveEffectLine& line : A11y::activeEffects(MWMechanics::getPlayer()))
        {
            A11y::SubItem item;
            // The source (spell / item / ability) is the section, announced
            // before its effects when focus crosses into a new group -- so the
            // user hears "Ancestor Guardian: Sanctuary 50 points for 58 seconds"
            // rather than the source trailing the effect.
            item.section = line.source;
            item.label = line.effect;
            items.push_back(std::move(item));
        }
        return items;
    }

    void SpellWindow::cycle(bool next)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();

        if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(player))
            return;

        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        if (stats.isParalyzed() || stats.getKnockedDown() || stats.isDead() || stats.getHitRecovery())
            return;

        mSpellView->setModel(new SpellModel(MWMechanics::getPlayer()));
        int itemCount = static_cast<int>(mSpellView->getModel()->getItemCount());
        if (itemCount == 0)
            return;

        SpellModel::ModelIndex nextIndex;
        SpellModel::ModelIndex currentIndex = mSpellView->getModel()->getSelectedIndex();

        // If we have a selected index, search for a valid selection in the target direction
        if (currentIndex >= 0)
        {
            MWWorld::ContainerStore store;
            const Spell& currentSpell = mSpellView->getModel()->getItem(currentIndex);

            nextIndex = currentIndex;
            for (int i = 0; i < itemCount; i++)
            {
                nextIndex += next ? 1 : -1;
                nextIndex = (nextIndex + itemCount) % itemCount;

                // We can keep this selection if:
                //   * we're not switching off of an enchanted item
                //   * we're not switching to an enchanted item
                //   * the next item wouldn't stack with the current item
                if (currentSpell.mType != Spell::Type_EnchantedItem)
                    break;

                const Spell& nextSpell = mSpellView->getModel()->getItem(nextIndex);
                if (nextSpell.mType != Spell::Type_EnchantedItem || !store.stacks(currentSpell.mItem, nextSpell.mItem))
                    break;
            }
        }
        // Otherwise, the first selection is always index 0
        else
            nextIndex = 0;

        // Only trigger the selection event if the selection is actually changing.
        // The itemCount check earlier ensures we have at least one spell to select.
        if (nextIndex != currentIndex)
        {
            const Spell& selectedSpell = mSpellView->getModel()->getItem(nextIndex);
            if (selectedSpell.mType == Spell::Type_EnchantedItem)
                onEnchantedItemSelected(selectedSpell.mItem, selectedSpell.mActive);
            else
                onSpellSelected(selectedSpell.mId);

            // Announce the newly-selected spell/item for screen-reader users:
            // this cycling happens with the menu closed, so there's no visible
            // selection feedback they can read. Interrupt so rapid cycling
            // always reflects the current selection.
            if (!selectedSpell.mName.empty())
                A11y::say(selectedSpell.mName, /*interrupt=*/true);
        }
    }

    bool SpellWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
            MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
        else
            mSpellView->onControllerButton(arg.button);

        return true;
    }

    void SpellWindow::setActiveControllerWindow(bool active)
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        if (winMgr->getMode() == MWGui::GM_Inventory)
        {
            // Fill the screen, or limit to a certain size on large screens. Size chosen to
            // match the size of the stats window.
            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            int width = std::min(viewSize.width, StatsWindow::getIdealWidth());
            int height = std::min(winMgr->getControllerMenuHeight(), StatsWindow::getIdealHeight());
            int x = (viewSize.width - width) / 2;
            int y = (viewSize.height - height) / 2;

            MyGUI::Window* window = mMainWidget->castType<MyGUI::Window>();
            window->setCoord(x, active ? y : viewSize.height + 1, width, height);

            MWBase::Environment::get().getWindowManager()->setControllerTooltipVisible(
                active && Settings::gui().mControllerTooltips);
        }

        mSpellView->setActiveControllerWindow(active);

        WindowBase::setActiveControllerWindow(active);
    }
}
