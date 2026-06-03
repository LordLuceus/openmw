#ifndef MWGUI_TravelWINDOW_H
#define MWGUI_TravelWINDOW_H

#include "referenceinterface.hpp"
#include "windowbase.hpp"

#include "accessibility/screen.hpp"

namespace MyGUI
{
    class Gui;
    class Widget;
}

namespace MWGui
{
    class TravelWindow : public ReferenceInterface, public WindowBase
    {
    public:
        TravelWindow();

        void setPtr(const MWWorld::Ptr& actor) override;

        void onClose() override;
        void onFrame(float dt) override;

        std::string_view getWindowIdForLua() const override { return "Travel"; }

    protected:
        MyGUI::Button* mCancelButton;
        MyGUI::TextBox* mPlayerGold;

        std::vector<MyGUI::Button*> mDestinationButtons;

        MyGUI::ScrollView* mDestinationsView;

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onTravelButtonClick(MyGUI::Widget* sender);
        void onMouseWheel(MyGUI::Widget* sender, int rel);
        void addDestination(const ESM::RefId& name, const ESM::Position& pos, bool interior);
        void clearDestinations();
        int mCurrentY;

        void updateLabels();

        void onReferenceUnavailable() override;

    private:
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        size_t mControllerFocus = 0;

        // Screen-reader controller. Virtual focus via an invisible anchor; the
        // destination buttons are widget-backed options rebuilt each setPtr().
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        void buildAccessibility();
    };
}

#endif
