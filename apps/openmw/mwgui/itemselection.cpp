#include "itemselection.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_TextBox.h>

#include <components/settings/values.hpp>

#include "../mwworld/class.hpp"

#include "accessibility/itemtext.hpp"
#include "accessibility/screen.hpp"
#include "accessibility/speech.hpp"
#include "accessibility/uimanager.hpp"

#include "inventoryitemmodel.hpp"
#include "itemview.hpp"
#include "sortfilteritemmodel.hpp"

namespace MWGui
{

    ItemSelectionDialog::ItemSelectionDialog(const std::string& label)
        : WindowModal("openmw_itemselection_dialog.layout")
        , mSortModel(nullptr)
        , mLabel(label)
    {
        getWidget(mItemView, "ItemView");
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &ItemSelectionDialog::onSelectedItem);

        MyGUI::TextBox* l;
        getWidget(l, "Label");
        l->setCaptionWithReplacing(label);

        MyGUI::Button* cancelButton;
        getWidget(cancelButton, "CancelButton");
        cancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemSelectionDialog::onCancelButtonClicked);

        center();

        // Screen-reader setup: invisible anchor holds key focus; item stacks are
        // widget-less options rebuilt from the model by buildAccessibility().
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor, /*ownModal=*/true);

        mControllerButtons.mA = "#{Interface:Select}";
        mControllerButtons.mB = "#{Interface:Cancel}";
        mControllerButtons.mR3 = "#{Interface:Info}";
    }

    void ItemSelectionDialog::onOpen()
    {
        WindowModal::onOpen();

        // Suspend whatever screen was active underneath (e.g. the Repair window
        // that opened this tool picker) so it stops handling keys, then take
        // input ourselves. Resumed on close, like ConfirmationDialog.
        //
        // Activation is DEFERRED to the first onFrame: callers populate the
        // model AFTER setVisible(true) (which fires onOpen) -- e.g. Repair does
        // setVisible -> openContainer -> setFilter -- so the model is still
        // empty here. Those setup calls run synchronously before the next
        // frame, so by onFrame the list is final. Announce the label now (it's
        // stable) and let the first item follow on activation.
        mA11yPrev = A11y::UiManager::instance().active();
        if (mA11yPrev)
            mA11yPrev->suspend();
        A11y::say(mLabel, /*interrupt=*/true);
        mA11yPendingActivate = true;
    }

    void ItemSelectionDialog::onClose()
    {
        WindowModal::onClose();
        mA11yPendingActivate = false;
        mA11y.deactivate();
        // Resume the screen we covered (if it's still active in its window) so it
        // handles keys again, and re-announce where it left off.
        if (mA11yPrev)
        {
            mA11yPrev->resume();
            mA11yPrev->announceCurrent();
            mA11yPrev = nullptr;
        }
    }

    void ItemSelectionDialog::onFrame(float dt)
    {
        if (mA11yPendingActivate)
        {
            mA11yPendingActivate = false;
            buildAccessibility();
            mA11y.activate(); // announces the first item (queued after the label)
        }
        mA11y.onFrame(dt);
    }

    void ItemSelectionDialog::buildAccessibility()
    {
        mA11y.clear();

        // Each item stack is a widget-less option (the ItemView draws them, so
        // there's no per-item widget to focus). Label = name + count; the T-key
        // tooltip carries the on-screen detail (weight / value / effects). Enter
        // selects the stack, firing eventItemSelected just like a mouse click.
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
                    .activate = [this, index] { onSelectedItem(index); } });
            }
        }
    }

    void ItemSelectionDialog::a11yRefresh()
    {
        // Rebuild in place only if we're already taking input (e.g. a later
        // setCategory()/setFilter() after activation). During initial setup the
        // screen isn't active yet -- onFrame builds the final list on its first
        // tick -- so there's nothing to refresh.
        if (mA11y.isActive())
        {
            const size_t cursor = mA11y.currentIndex();
            buildAccessibility();
            const size_t itemCount = mSortModel ? mSortModel->getItemCount() : 0;
            if (cursor == A11y::Screen::npos || itemCount == 0)
                mA11y.focusFirst(/*announce=*/false);
            else
                mA11y.selectIndex(std::min(cursor, itemCount - 1), /*announce=*/false);
        }
    }

    bool ItemSelectionDialog::exit()
    {
        eventDialogCanceled();
        return true;
    }

    void ItemSelectionDialog::openContainer(const MWWorld::Ptr& container)
    {
        auto sortModel = std::make_unique<SortFilterItemModel>(std::make_unique<InventoryItemModel>(container));
        mSortModel = sortModel.get();
        mItemView->setModel(std::move(sortModel));
        mItemView->resetScrollBars();
        if (Settings::gui().mControllerMenus)
            mItemView->setActiveControllerWindow(true);
        a11yRefresh();
    }

    void ItemSelectionDialog::setCategory(int category)
    {
        mSortModel->setCategory(category);
        mItemView->update();
        a11yRefresh();
    }

    void ItemSelectionDialog::setFilter(int filter)
    {
        mSortModel->setFilter(filter);
        mItemView->update();
        a11yRefresh();
    }

    void ItemSelectionDialog::onSelectedItem(int index)
    {
        ItemStack item = mSortModel->getItem(index);
        eventItemSelected(item.mBase);
    }

    void ItemSelectionDialog::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        exit();
    }

    bool ItemSelectionDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onCancelButtonClicked(nullptr);
        else
            mItemView->onControllerButton(arg.button);

        return true;
    }
}
