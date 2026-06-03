#ifndef MWGUI_BOOKWINDOW_H
#define MWGUI_BOOKWINDOW_H

#include "windowbase.hpp"

#include "../mwworld/ptr.hpp"

#include <components/widgets/imagebutton.hpp>

#include "accessibility/screen.hpp"

namespace MWGui
{
    class BookWindow : public BookWindowBase
    {
    public:
        BookWindow();

        void setPtr(const MWWorld::Ptr& book) override;
        void setInventoryAllowed(bool allowed);

        void onResChange(int, int) override { center(); }
        void onClose() override;
        void onFrame(float duration) override;
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

        std::string_view getWindowIdForLua() const override { return "Book"; }
        ControllerButtons* getControllerButtons() override;

    protected:
        void onNextPageButtonClicked(MyGUI::Widget* sender);
        void onPrevPageButtonClicked(MyGUI::Widget* sender);
        void onCloseButtonClicked(MyGUI::Widget* sender);
        void onTakeButtonClicked(MyGUI::Widget* sender);
        void onMouseWheel(MyGUI::Widget* sender, int rel);
        void setTakeButtonShow(bool show);

        void onKeyButtonPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character);

        void nextPage();
        void prevPage();

        void updatePages();
        void clearPages();

        // Build the screen-reader option list (one element per paragraph, plus
        // Take / Close actions) for the current book. Called from setPtr().
        void buildAccessibility();

    private:
        typedef std::pair<int, int> Page;
        typedef std::vector<Page> Pages;

        Gui::ImageButton* mCloseButton;
        Gui::ImageButton* mTakeButton;
        Gui::ImageButton* mNextPageButton;
        Gui::ImageButton* mPrevPageButton;

        MyGUI::TextBox* mLeftPageNumber;
        MyGUI::TextBox* mRightPageNumber;
        MyGUI::Widget* mLeftPage;
        MyGUI::Widget* mRightPage;

        unsigned int mCurrentPage; // 0 is first page
        Pages mPages;

        MWWorld::Ptr mBook;

        bool mTakeButtonShow;
        bool mTakeButtonAllowed;

        // Screen-reader controller (virtual-focus mode). Navigated via an
        // invisible anchor so the page text and buttons can be read with the
        // arrow keys without the native widgets eating them.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor;
    };

}

#endif
