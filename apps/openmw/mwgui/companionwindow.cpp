#include "companionwindow.hpp"

#include <cmath>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_InputManager.h>

#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"

#include "companionitemmodel.hpp"
#include "countdialog.hpp"
#include "draganddrop.hpp"
#include "itemtransfer.hpp"
#include "itemview.hpp"
#include "messagebox.hpp"
#include "sortfilteritemmodel.hpp"
#include "tooltips.hpp"
#include "widgets.hpp"

#include "accessibility/itemtext.hpp"
#include "accessibility/panegroup.hpp"
#include "accessibility/speech.hpp"

namespace
{

    int getProfit(const MWWorld::Ptr& actor)
    {
        const ESM::RefId& script = actor.getClass().getScript(actor);
        if (!script.empty())
        {
            return actor.getRefData().getLocals().getIntVar(script, "minimumprofit");
        }
        return 0;
    }

}

namespace MWGui
{

    CompanionWindow::CompanionWindow(DragAndDrop& dragAndDrop, ItemTransfer& itemTransfer, MessageBoxManager* manager)
        : WindowBase("openmw_companion_window.layout")
        , mSortModel(nullptr)
        , mModel(nullptr)
        , mSelectedItem(-1)
        , mUpdateNextFrame(false)
        , mDragAndDrop(&dragAndDrop)
        , mItemTransfer(&itemTransfer)
        , mMessageBoxManager(manager)
    {
        getWidget(mCloseButton, "CloseButton");
        getWidget(mProfitLabel, "ProfitLabel");
        getWidget(mEncumbranceBar, "EncumbranceBar");
        getWidget(mFilterEdit, "FilterEdit");
        getWidget(mItemView, "ItemView");
        mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &CompanionWindow::onBackgroundSelected);
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &CompanionWindow::onItemSelected);
        mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &CompanionWindow::onNameFilterChanged);

        mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onCloseButtonClicked);

        setCoord(200, 0, 600, 300);

        // Screen-reader setup: an invisible anchor holds key focus while the
        // item list is navigated by index (the items are drawn by the custom
        // ItemView, not as individual widgets), as in ContainerWindow.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);
        mA11yFilterEdit.attach(mFilterEdit);
        mA11yFilterEdit.setActive(false);
        // Extra key on the item list: E reports the companion's encumbrance (and
        // profit, for contract companions) on demand, mirroring the inventory.
        mA11y.setExtraKeyHandler([this](MyGUI::KeyCode key) -> bool {
            if (key == MyGUI::KeyCode::E)
            {
                A11y::say(a11yEncumbranceValue(), /*interrupt=*/true);
                return true;
            }
            return false;
        });

        mControllerButtons.mA = "#{Interface:Take}";
        mControllerButtons.mB = "#{Interface:Close}";
        mControllerButtons.mR3 = "#{Interface:Info}";
        mControllerButtons.mL2 = "#{Interface:Inventory}";
    }

    void CompanionWindow::onItemSelected(int index)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            mDragAndDrop->drop(mModel, mItemView);
            updateEncumbranceBar();
            return;
        }

        const ItemStack& item = mSortModel->getItem(index);

        // We can't take conjured items from a companion actor
        if (item.mFlags & ItemStack::Flag_Bound)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
            return;
        }

        MWWorld::Ptr object = item.mBase;
        size_t count = item.mCount;
        bool shift = MyGUI::InputManager::getInstance().isShiftPressed();
        if (MyGUI::InputManager::getInstance().isControlPressed())
            count = 1;

        mSelectedItem = mSortModel->mapToSource(index);

        if (count > 1 && !shift)
        {
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            std::string name{ object.getClass().getName(object) };
            name += MWGui::ToolTips::getSoulString(object.getCellRef());
            dialog->openCountDialog(name, "#{sTake}", static_cast<int>(count));
            dialog->eventOkClicked.clear();
            if (Settings::gui().mControllerMenus || MyGUI::InputManager::getInstance().isAltPressed())
                dialog->eventOkClicked += MyGUI::newDelegate(this, &CompanionWindow::transferItem);
            else
                dialog->eventOkClicked += MyGUI::newDelegate(this, &CompanionWindow::dragItem);
        }
        else if (Settings::gui().mControllerMenus || MyGUI::InputManager::getInstance().isAltPressed())
            transferItem(nullptr, count);
        else
            dragItem(nullptr, count);
    }

    void CompanionWindow::onNameFilterChanged(MyGUI::EditBox* sender)
    {
        mSortModel->setNameFilter(sender->getCaption());
        mItemView->update();
        // Keep the spoken item list in sync with the filtered view, pinning the
        // cursor to the filter option so typing doesn't yank focus onto an item.
        //
        // CRITICAL: do NOT rebuild while the user is editing the filter field.
        // buildAccessibility() -> mA11y.clear() resets the screen's edit-mode
        // flag, dropping us out of edit mode mid-typing -- so the next keystroke
        // leaks past the edit guard to navigation/activation. The onFrame
        // mA11yWasEditing block does one clean rebuild once editing ends.
        if (A11y::PaneGroup::instance().contains(&mA11y) && !mA11y.editing())
        {
            const size_t cursor = mA11y.currentIndex();
            buildAccessibility();
            if (cursor != A11y::Screen::npos && cursor < mA11y.size())
                mA11y.selectIndex(cursor, /*announce=*/false);
        }
    }

    void CompanionWindow::dragItem(MyGUI::Widget* /*sender*/, std::size_t count)
    {
        mDragAndDrop->startDrag(mSelectedItem, mSortModel, mModel, mItemView, count);
    }

    void CompanionWindow::transferItem(MyGUI::Widget* /*sender*/, std::size_t count)
    {
        mItemTransfer->apply(mModel->getItem(mSelectedItem), count, *mItemView);
    }

    void CompanionWindow::onBackgroundSelected()
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            mDragAndDrop->drop(mModel, mItemView);
            updateEncumbranceBar();
        }
    }

    void CompanionWindow::setPtr(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty() || !actor.getClass().isActor())
            throw std::runtime_error("Invalid argument in CompanionWindow::setPtr");
        mPtr = actor;
        updateEncumbranceBar();
        auto model = std::make_unique<CompanionItemModel>(actor);
        mModel = model.get();
        auto sortModel = std::make_unique<SortFilterItemModel>(std::move(model));
        mSortModel = sortModel.get();
        mFilterEdit->setCaption({});
        mItemView->setModel(std::move(sortModel));
        mItemView->resetScrollBars();

        setTitle(actor.getClass().getName(actor));

        // Screen reader: the companion window is shown next to the player's
        // inventory. Build our item list and enrol as pane 0; the inventory
        // enrols itself as pane 1, so Tab switches between taking (here) and
        // storing (there). The PaneGroup claims focus for the lowest order first
        // (this pane), matching the old behaviour of landing in the companion.
        A11y::say(actor.getClass().getName(actor));
        buildAccessibility();
        A11y::PaneGroup::instance().enrol(&mA11y, std::string(actor.getClass().getName(actor)), 0);
    }

    void CompanionWindow::buildAccessibility()
    {
        mA11y.clear();

        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        // Leading option: the name-filter field (matches the inventory/loot
        // panes so the player can narrow a long companion inventory).
        mA11y.add({ .widget = nullptr, .label = std::string(winMgr->getGameSettingString("sName", "Name")),
            .value =
                [this] {
                    const std::string text = mFilterEdit->getOnlyText().asUTF8();
                    return text.empty() ? std::string("blank") : text;
                },
            .edit = &mA11yFilterEdit });

        mA11yItemBase = 1;

        // Each item stack is a widget-less option (the ItemView draws them, so
        // there's no per-item widget to focus). Label = name + count; the T-key
        // tooltip carries the on-screen detail (weight / value / effects). Enter
        // takes the whole stack into the player's inventory; Shift+Enter opens
        // the accessible count picker for a partial amount.
        if (mSortModel)
        {
            for (size_t i = 0; i < mSortModel->getItemCount(); ++i)
            {
                const int index = static_cast<int>(i);
                const ItemStack item = mSortModel->getItem(index);

                std::string label = std::string(item.mBase.getClass().getName(item.mBase));
                if (item.mCount > 1)
                    label += " (" + std::to_string(item.mCount) + ")";

                mA11y.add({ .widget = nullptr, .label = std::move(label),
                    .tooltips = [base = item.mBase, count = item.mCount]
                    { return A11y::itemTooltipLines(base, static_cast<int>(count)); },
                    .activate = [this, index]
                    { a11yTakeItem(index, !MyGUI::InputManager::getInstance().isShiftPressed()); } });
            }
        }

        mA11y.add({ .widget = mCloseButton, .label = "#{sClose}",
            .activate = [this] { onCloseButtonClicked(mCloseButton); } });
    }

    std::string CompanionWindow::a11yEncumbranceValue() const
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        if (mPtr.isEmpty())
            return std::string(winMgr->getGameSettingString("sEncumbrance", "Encumbrance"));

        const int capacity = static_cast<int>(mPtr.getClass().getCapacity(mPtr));
        const int encumbrance = static_cast<int>(std::ceil(mPtr.getClass().getEncumbrance(mPtr)));
        std::string out = std::string(winMgr->getGameSettingString("sEncumbrance", "Encumbrance")) + ": "
            + std::to_string(encumbrance) + " / " + std::to_string(capacity);

        // Contract companions (e.g. Calvus) track a running profit/loss that
        // gates closing the window; report it too so the player can tell whether
        // they have given the companion their fair share.
        if (mModel && mModel->hasProfit(mPtr))
            out += ". " + std::string(winMgr->getGameSettingString("sProfitValue", "Profit"))
                + ": " + std::to_string(getProfit(mPtr));
        return out;
    }

    void CompanionWindow::a11yTakeItem(int sortIndex, bool wholeStack)
    {
        if (!mSortModel || mModel == nullptr)
            return;
        if (sortIndex < 0 || sortIndex >= static_cast<int>(mSortModel->getItemCount()))
            return;

        const ItemStack item = mSortModel->getItem(sortIndex);

        // Conjured/bound items can't be taken from a companion (same guard as
        // onItemSelected).
        if (item.mFlags & ItemStack::Flag_Bound)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
            return;
        }

        mSelectedItem = mSortModel->mapToSource(sortIndex);
        const size_t count = item.mCount;

        if (!wholeStack && count > 1)
        {
            // Open the accessible count picker; on OK it transfers the chosen
            // amount into the player's inventory.
            std::string name{ item.mBase.getClass().getName(item.mBase) };
            name += MWGui::ToolTips::getSoulString(item.mBase.getCellRef());
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            dialog->openCountDialog(name, "#{sTake}", static_cast<int>(count));
            dialog->eventOkClicked.clear();
            dialog->eventOkClicked += MyGUI::newDelegate(this, &CompanionWindow::onA11yCountTaken);
            return;
        }

        // Take the whole stack straight into the player's inventory, then rebuild
        // the list and keep the cursor near the same row.
        transferItem(nullptr, count);
        a11yRebuildKeepingCursor();
    }

    void CompanionWindow::onA11yCountTaken(MyGUI::Widget* sender, std::size_t count)
    {
        // The count picker confirmed a partial take: perform it (mSelectedItem
        // was set before the dialog opened), then refresh the spoken list.
        transferItem(sender, count);
        a11yRebuildKeepingCursor();
    }

    void CompanionWindow::a11yRebuildKeepingCursor()
    {
        updateEncumbranceBar();
        const size_t cursor = mA11y.currentIndex();
        buildAccessibility();
        const size_t itemCount = mSortModel ? mSortModel->getItemCount() : 0;
        if (cursor == A11y::Screen::npos || cursor < mA11yItemBase || itemCount == 0)
            // No items left (or cursor was on the filter): land on the last
            // option, which is Close.
            mA11y.selectIndex(mA11y.size() - 1, /*announce=*/true);
        else
        {
            const size_t item = std::min(cursor - mA11yItemBase, itemCount - 1);
            mA11y.selectIndex(mA11yItemBase + item, /*announce=*/true);
        }
    }

    void CompanionWindow::onFrame(float dt)
    {
        checkReferenceAvailable();

        if (mUpdateNextFrame)
        {
            updateEncumbranceBar();
            mItemView->update();
            mUpdateNextFrame = false;
            // The companion's contents changed (an item taken out here, or stored
            // in from the inventory pane). Rebuild the spoken list so it stays in
            // sync, keeping the cursor near its old row. Announce only when this
            // pane is the active one -- a store happens from the inventory pane,
            // which should be the one to speak.
            if (A11y::PaneGroup::instance().contains(&mA11y))
            {
                const size_t cursor = mA11y.currentIndex();
                buildAccessibility();
                const size_t itemCount = mSortModel ? mSortModel->getItemCount() : 0;
                if (mA11y.isActive() && cursor != A11y::Screen::npos && cursor >= mA11yItemBase && itemCount > 0)
                    mA11y.selectIndex(
                        mA11yItemBase + std::min(cursor - mA11yItemBase, itemCount - 1), /*announce=*/false);
            }
        }

        // Drive spoken editing feedback for the name-filter box.
        mA11yFilterEdit.onFrame();

        // When the user finishes editing the name filter, the matching set of
        // items has changed: rebuild the spoken list now (deferred from
        // onNameFilterChanged, which must not rebuild mid-edit -- that would
        // clear edit mode and leak keys).
        const bool editing = mA11y.editing();
        if (mA11yWasEditing && !editing && mA11y.isActive())
            a11yRebuildKeepingCursor();
        mA11yWasEditing = editing;

        // Let the PaneGroup claim focus for the pane the user should land on.
        if (A11y::PaneGroup::instance().contains(&mA11y))
            A11y::PaneGroup::instance().maybeActivateInitial(&mA11y);

        mA11y.onFrame(dt);
    }

    void CompanionWindow::updateEncumbranceBar()
    {
        if (mPtr.isEmpty())
            return;
        int capacity = static_cast<int>(mPtr.getClass().getCapacity(mPtr));
        float encumbrance = std::ceil(mPtr.getClass().getEncumbrance(mPtr));
        mEncumbranceBar->setValue(static_cast<int>(encumbrance), capacity);

        if (mModel && mModel->hasProfit(mPtr))
        {
            mProfitLabel->setCaptionWithReplacing("#{sProfitValue} " + MyGUI::utility::toString(getProfit(mPtr)));
        }
        else
            mProfitLabel->setCaption({});
    }

    void CompanionWindow::onCloseButtonClicked(MyGUI::Widget* /*sender*/)
    {
        if (exit())
            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Companion);
    }

    bool CompanionWindow::exit()
    {
        if (mModel && mModel->hasProfit(mPtr) && getProfit(mPtr) < 0)
        {
            std::vector<std::string> buttons;
            buttons.emplace_back("#{sCompanionWarningButtonOne}");
            buttons.emplace_back("#{sCompanionWarningButtonTwo}");
            mMessageBoxManager->createInteractiveMessageBox("#{sCompanionWarningMessage}", buttons);
            mMessageBoxManager->eventButtonPressed
                += MyGUI::newDelegate(this, &CompanionWindow::onMessageBoxButtonClicked);
            return false;
        }
        return true;
    }

    void CompanionWindow::onMessageBoxButtonClicked(int button)
    {
        if (button == 0)
        {
            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Companion);
            // Important for Calvus' contract script to work properly
            MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
        }
    }

    void CompanionWindow::onReferenceUnavailable()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Companion);
    }

    void CompanionWindow::resetReference()
    {
        ReferenceInterface::resetReference();
        mItemView->setModel(nullptr);
        mModel = nullptr;
        mSortModel = nullptr;
    }

    void CompanionWindow::onInventoryUpdate(const MWWorld::Ptr& ptr)
    {
        if (ptr == mPtr)
            mUpdateNextFrame = true;
    }

    void CompanionWindow::onOpen()
    {
        mItemTransfer->addTarget(*mItemView);
    }

    void CompanionWindow::onClose()
    {
        mItemTransfer->removeTarget(*mItemView);

        // Make sure the window was actually closed and not temporarily hidden
        // (e.g. the close-warning message box is up); only then tear down the
        // screen-reader pane.
        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Companion))
            return;

        A11y::PaneGroup::instance().withdraw(&mA11y);
        mA11y.deactivate();
    }

    bool CompanionWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            int index = mItemView->getControllerFocus();
            if (index >= 0 && index < mItemView->getItemCount())
                onItemSelected(index);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            onCloseButtonClicked(mCloseButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK || arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP
            || arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN || arg.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT
            || arg.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
        {
            mItemView->onControllerButton(arg.button);
        }

        return true;
    }

    void CompanionWindow::setActiveControllerWindow(bool active)
    {
        mItemView->setActiveControllerWindow(active);
        WindowBase::setActiveControllerWindow(active);
    }
}
