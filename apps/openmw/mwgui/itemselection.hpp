#ifndef OPENMW_GAME_MWGUI_ITEMSELECTION_H
#define OPENMW_GAME_MWGUI_ITEMSELECTION_H

#include <MyGUI_Delegate.h>

#include "windowbase.hpp"

#include "accessibility/screen.hpp"

namespace MWWorld
{
    class Ptr;
}

namespace MWGui
{
    class ItemView;
    class SortFilterItemModel;

    class ItemSelectionDialog : public WindowModal
    {
    public:
        ItemSelectionDialog(const std::string& label);

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;
        bool exit() override;

        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;
        typedef MyGUI::delegates::MultiDelegate<MWWorld::Ptr> EventHandle_Item;

        EventHandle_Item eventItemSelected;
        EventHandle_Void eventDialogCanceled;

        void openContainer(const MWWorld::Ptr& container);
        void setCategory(int category);
        void setFilter(int filter);

        SortFilterItemModel* getSortModel() { return mSortModel; }

        /// Rebuild the screen-reader option list from the current model. Called
        /// by openContainer/setCategory/setFilter so the spoken list always
        /// matches the visible one regardless of setup-call order.
        void a11yRefresh();

    private:
        ItemView* mItemView;
        SortFilterItemModel* mSortModel;

        std::string mLabel;

        // Screen-reader controller. Item stacks are widget-less options (the
        // ItemView draws them); a modal layered over another accessible screen,
        // so it suspends/resumes the screen underneath like ConfirmationDialog.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        A11y::Screen* mA11yPrev = nullptr;
        bool mA11yPendingActivate = false;
        void buildAccessibility();

        void onSelectedItem(int index);

        void onCancelButtonClicked(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
    };

}

#endif
