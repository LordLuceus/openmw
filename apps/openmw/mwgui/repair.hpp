#ifndef OPENMW_MWGUI_REPAIR_H
#define OPENMW_MWGUI_REPAIR_H

#include <memory>

#include "windowbase.hpp"

#include "accessibility/screen.hpp"

#include "../mwmechanics/repair.hpp"

namespace MWGui
{

    class ItemSelectionDialog;
    class ItemWidget;
    class ItemChargeView;

    class Repair : public WindowBase
    {
    public:
        Repair();

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;

        void setPtr(const MWWorld::Ptr& item) override;

        std::string_view getWindowIdForLua() const override { return "Repair"; }

    protected:
        ItemChargeView* mRepairBox;

        MyGUI::Widget* mToolBox;

        ItemWidget* mToolIcon;

        std::unique_ptr<ItemSelectionDialog> mItemSelectionDialog;

        MyGUI::TextBox* mUsesLabel;
        MyGUI::TextBox* mQualityLabel;

        MyGUI::Button* mCancelButton;

        MWMechanics::Repair mRepair;

        void updateRepairView();

        void onSelectItem(MyGUI::Widget* sender);

        void onItemSelected(MWWorld::Ptr item);
        void onItemCancel();

        void onRepairItem(MyGUI::Widget* sender, const MWWorld::Ptr& ptr);
        void onCancel(MyGUI::Widget* sender);

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

        // Screen-reader controller. Virtual focus via an invisible anchor. The
        // option list is: the repair tool (Enter opens the item picker), then
        // each damaged item (Enter repairs it), then Cancel. Rebuilt by
        // buildAccessibility() whenever the view changes.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        void buildAccessibility();
        // Re-read the current option after a repair, keeping cursor position.
        void a11yRebuildKeepingCursor();
    };

}

#endif
