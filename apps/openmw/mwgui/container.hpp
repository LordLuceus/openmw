#ifndef MGUI_CONTAINER_H
#define MGUI_CONTAINER_H

#include "itemmodel.hpp"
#include "referenceinterface.hpp"
#include "windowbase.hpp"

#include "accessibility/screen.hpp"

#include <components/misc/notnullptr.hpp>

namespace MyGUI
{
    class Gui;
    class Widget;
}

namespace MWGui
{
    class ContainerWindow;
    class ItemView;
    class SortFilterItemModel;
    class ItemTransfer;

    class ContainerWindow : public WindowBase, public ReferenceInterface
    {
    public:
        explicit ContainerWindow(DragAndDrop& dragAndDrop, ItemTransfer& itemTransfer);

        void setPtr(const MWWorld::Ptr& container) override;

        void onOpen() override;

        void onClose() override;

        void clear() override { resetReference(); }

        void onFrame(float dt) override;

        void resetReference() override;

        void onDeleteCustomData(const MWWorld::Ptr& ptr) override;

        void treatNextOpenAsLoot() { mTreatNextOpenAsLoot = true; }

        void onInventoryUpdate(const MWWorld::Ptr& ptr) override;

        std::string_view getWindowIdForLua() const override { return "Container"; }

        ControllerButtons* getControllerButtons() override;
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

        MWGui::ItemView* getItemView() { return mItemView; }
        ItemModel* getModel() { return mModel; }

    private:
        Misc::NotNullPtr<DragAndDrop> mDragAndDrop;
        Misc::NotNullPtr<ItemTransfer> mItemTransfer;

        MWGui::ItemView* mItemView;
        SortFilterItemModel* mSortModel;
        ItemModel* mModel;
        int mSelectedItem;
        bool mUpdateNextFrame;
        bool mTreatNextOpenAsLoot;
        MyGUI::Button* mDisposeCorpseButton;
        MyGUI::Button* mTakeButton;
        MyGUI::Button* mCloseButton;

        void onItemSelected(int index);
        void onBackgroundSelected();
        void dragItem(MyGUI::Widget* sender, std::size_t count);
        void transferItem(MyGUI::Widget* sender, std::size_t count);
        void dropItem();
        void onCloseButtonClicked(MyGUI::Widget* sender);
        void onTakeAllButtonClicked(MyGUI::Widget* sender);
        void onDisposeCorpseButtonClicked(MyGUI::Widget* sender);

        void onReferenceUnavailable() override;

        // Screen-reader controller. Virtual focus (the items are drawn by a
        // custom ItemView, not individual widgets, so each item is a
        // widget-less option navigated by index, as in BookWindow). The
        // action buttons (Take All / Close / Dispose) are widget-backed.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        void buildAccessibility();
        // Take the stack at sort-model index \p sortIndex: whole stack when
        // \p wholeStack, else open the (accessible) count picker. Rebuilds the
        // a11y list afterwards and keeps the cursor near the same row.
        void a11yTakeItem(int sortIndex, bool wholeStack);
        // CountDialog OK callback for a partial take (Shift+Enter path).
        void onA11yCountTaken(MyGUI::Widget* sender, std::size_t count);
        // Rebuild the a11y item list after the model changed, pinning the
        // cursor to the same row (clamped) or the buttons if the list emptied.
        void a11yRebuildKeepingCursor();
    };
}
#endif // CONTAINER_H
