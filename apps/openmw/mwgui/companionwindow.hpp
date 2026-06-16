#ifndef OPENMW_MWGUI_COMPANIONWINDOW_H
#define OPENMW_MWGUI_COMPANIONWINDOW_H

#include "referenceinterface.hpp"
#include "windowbase.hpp"

#include "accessibility/editfield.hpp"
#include "accessibility/screen.hpp"

#include <components/misc/notnullptr.hpp>

namespace MWGui
{
    namespace Widgets
    {
        class MWDynamicStat;
    }

    class MessageBoxManager;
    class ItemView;
    class DragAndDrop;
    class SortFilterItemModel;
    class CompanionItemModel;
    class ItemTransfer;

    class CompanionWindow : public WindowBase, public ReferenceInterface
    {
    public:
        explicit CompanionWindow(DragAndDrop& dragAndDrop, ItemTransfer& itemTransfer, MessageBoxManager* manager);

        bool exit() override;

        void resetReference() override;

        void setPtr(const MWWorld::Ptr& actor) override;
        void onFrame(float dt) override;
        void clear() override { resetReference(); }

        void onInventoryUpdate(const MWWorld::Ptr& ptr) override;

        std::string_view getWindowIdForLua() const override { return "Companion"; }

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

        MWGui::ItemView* getItemView() { return mItemView; }
        CompanionItemModel* getModel() { return mModel; }

    private:
        ItemView* mItemView;
        SortFilterItemModel* mSortModel;
        CompanionItemModel* mModel;
        int mSelectedItem;
        bool mUpdateNextFrame;

        Misc::NotNullPtr<DragAndDrop> mDragAndDrop;
        Misc::NotNullPtr<ItemTransfer> mItemTransfer;

        MyGUI::Button* mCloseButton;
        MyGUI::EditBox* mFilterEdit;
        MyGUI::TextBox* mProfitLabel;
        Widgets::MWDynamicStat* mEncumbranceBar;
        MessageBoxManager* mMessageBoxManager;

        void onItemSelected(int index);
        void onNameFilterChanged(MyGUI::EditBox* sender);
        void onBackgroundSelected();
        void dragItem(MyGUI::Widget* sender, std::size_t count);
        void transferItem(MyGUI::Widget* sender, std::size_t count);

        void onMessageBoxButtonClicked(int button);

        void updateEncumbranceBar();

        void onCloseButtonClicked(MyGUI::Widget* sender);

        void onReferenceUnavailable() override;

        void onOpen() override;

        void onClose() override;

        // Screen-reader controller. Virtual focus: the companion's items are
        // drawn by the custom ItemView (no per-item widget), so each item is a
        // widget-less option navigated by index, as in ContainerWindow. The
        // window is shown next to the player's inventory, so it enrols in the
        // PaneGroup as pane 0 (the companion) and the inventory enrols as pane 1.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        // Spoken editing feedback for the name-filter box (first option).
        A11y::EditField mA11yFilterEdit;
        // Tracks the screen's edit-mode across frames so we can defer the spoken
        // list rebuild until the user finishes typing in the name filter (a
        // rebuild mid-edit would clear edit mode and leak keys -- see onFrame).
        bool mA11yWasEditing = false;
        void buildAccessibility();
        // Take the stack at sort-model index \p sortIndex out of the companion
        // and into the player's inventory: whole stack when \p wholeStack, else
        // open the accessible count picker. Rebuilds the list afterwards and
        // keeps the cursor near the same row.
        void a11yTakeItem(int sortIndex, bool wholeStack);
        // CountDialog OK callback for a partial take (Shift+Enter path).
        void onA11yCountTaken(MyGUI::Widget* sender, std::size_t count);
        // Rebuild the a11y item list after the model changed, pinning the cursor
        // to the same row (clamped) or the Close button if the list emptied.
        void a11yRebuildKeepingCursor();
        // Spoken on-demand (E key): the companion's encumbrance, plus profit when
        // this is a contract companion that tracks it.
        std::string a11yEncumbranceValue() const;
        // Index of the first item option (the name filter precedes the items).
        std::size_t mA11yItemBase = 0;
    };

}

#endif
