#ifndef OPENMW_MWGUI_MERCHANTREPAIR_H
#define OPENMW_MWGUI_MERCHANTREPAIR_H

#include "../mwworld/ptr.hpp"
#include "windowbase.hpp"

#include "accessibility/screen.hpp"

namespace MWGui
{

    class MerchantRepair : public WindowBase
    {
    public:
        MerchantRepair();

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;

        void setPtr(const MWWorld::Ptr& actor) override;

        std::string_view getWindowIdForLua() const override { return "MerchantRepair"; }

    private:
        MyGUI::ScrollView* mList;
        MyGUI::Button* mOkButton;
        MyGUI::TextBox* mGoldLabel;
        /// List of enabled/repairable items and their index in the full list.
        std::vector<std::pair<MyGUI::Button*, size_t>> mButtons;

        MWWorld::Ptr mActor;

        size_t mControllerFocus = 0;

        // Screen-reader controller. Virtual focus via an invisible anchor; the
        // repair buttons are widget-backed options rebuilt each setPtr().
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        void buildAccessibility();

    protected:
        void onMouseWheel(MyGUI::Widget* sender, int rel);
        void onRepairButtonClick(MyGUI::Widget* sender);
        void onOkButtonClick(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
    };

}

#endif
