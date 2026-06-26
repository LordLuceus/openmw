#ifndef OPENMW_MWGUI_RECHARGE_H
#define OPENMW_MWGUI_RECHARGE_H

#include <memory>

#include "windowbase.hpp"

#include "accessibility/screen.hpp"

namespace MWWorld
{
    class Ptr;
}

namespace MWGui
{

    class ItemSelectionDialog;
    class ItemWidget;
    class ItemChargeView;

    class Recharge : public WindowBase
    {
    public:
        Recharge();

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;

        void setPtr(const MWWorld::Ptr& gem) override;

        std::string_view getWindowIdForLua() const override { return "Recharge"; }

    protected:
        ItemChargeView* mBox;

        MyGUI::Widget* mGemBox;

        ItemWidget* mGemIcon;

        std::unique_ptr<ItemSelectionDialog> mItemSelectionDialog;

        MyGUI::TextBox* mChargeLabel;

        MyGUI::Button* mCancelButton;

        void updateView();

        void onSelectItem(MyGUI::Widget* sender);

        void onItemSelected(MWWorld::Ptr item);
        void onItemCancel();

        void onItemClicked(MyGUI::Widget* sender, const MWWorld::Ptr& item);
        void onCancel(MyGUI::Widget* sender);
        void onMouseWheel(MyGUI::Widget* sender, int rel);

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

        // Screen-reader controller. Virtual focus via an invisible anchor. The
        // option list is: the soul gem (Enter opens the gem picker), then each
        // rechargeable enchanted item (Enter recharges it with the current gem),
        // then Cancel. Rebuilt by buildAccessibility() whenever the view changes.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        void buildAccessibility();
        // Re-read the current option after a recharge, keeping cursor position.
        void a11yRebuildKeepingCursor();
    };

}

#endif
