#ifndef MWGUI_SCROLLWINDOW_H
#define MWGUI_SCROLLWINDOW_H

#include "windowbase.hpp"

#include "../mwworld/ptr.hpp"

#include "accessibility/screen.hpp"

namespace Gui
{
    class ImageButton;
}

namespace MWGui
{
    class ScrollWindow : public BookWindowBase
    {
    public:
        ScrollWindow();

        void setPtr(const MWWorld::Ptr& scroll) override;
        void setInventoryAllowed(bool allowed);

        void onClose() override;
        void onFrame(float duration) override;
        void onResChange(int, int) override { center(); }

        std::string_view getWindowIdForLua() const override { return "Scroll"; }

        ControllerButtons* getControllerButtons() override;

    protected:
        void onCloseButtonClicked(MyGUI::Widget* sender);
        void onTakeButtonClicked(MyGUI::Widget* sender);
        void setTakeButtonShow(bool show);
        void onKeyButtonPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

        // Build the screen-reader option list (one element per paragraph, plus
        // Take / Close actions) for the current scroll. Called from setPtr().
        void buildAccessibility();

    private:
        Gui::ImageButton* mCloseButton;
        Gui::ImageButton* mTakeButton;
        MyGUI::ScrollView* mTextView;

        MWWorld::Ptr mScroll;

        bool mTakeButtonShow;
        bool mTakeButtonAllowed;

        // Screen-reader controller (virtual-focus mode); see BookWindow.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor;
    };

}

#endif
