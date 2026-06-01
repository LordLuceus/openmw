#ifndef OPENMW_GAME_MWGUI_MAINMENU_H
#define OPENMW_GAME_MWGUI_MAINMENU_H

#include <memory>
#include <optional>
#include <thread>

#include <MyGUI_KeyCode.h>
#include <MyGUI_Types.h>

#include "savegamedialog.hpp"
#include "windowbase.hpp"

namespace Gui
{
    class ImageButton;
}

namespace VFS
{
    class Manager;
}

namespace MWGui
{

    class BackgroundImage;
    class VideoWidget;
    class MenuVideo
    {
        MyGUI::ImageBox* mVideoBackground;
        VideoWidget* mVideo;
        std::thread mThread;
        bool mRunning;

        void run();

    public:
        MenuVideo(const VFS::Manager* vfs);
        void resize(int w, int h);
        ~MenuVideo();
    };

    class MainMenu : public WindowBase
    {
        int mWidth;
        int mHeight;

        bool mHasAnimatedMenu;

        // True once the main menu has been hidden at least once (e.g. the
        // loading wallpaper covered it). Used by the accessibility hooks to
        // skip the very first setVisible(true) -- which on initial launch
        // fires *before* the wallpaper pushes, so the announcement would be
        // covered/duplicated by the time the wallpaper dismisses.
        bool mAccessibilityHasBeenCovered = false;

    public:
        MainMenu(int w, int h, const VFS::Manager* vfs, const std::string& versionDescription);

        void onResChange(int w, int h) override;
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

        void setVisible(bool visible) override;

        bool exit() override;

    private:
        const VFS::Manager* mVFS;

        MyGUI::Widget* mButtonBox;
        MyGUI::TextBox* mVersionText;

        BackgroundImage* mBackground;

        std::optional<MenuVideo> mVideo; // For animated main menus

        std::map<std::string, Gui::ImageButton*, std::less<>> mButtons;

        void onButtonClicked(MyGUI::Widget* sender);
        // MyGUI focus callbacks. We speak the button name when it gains focus
        // (either through keyboard navigation or mouse hover).
        void onButtonKeyFocus(MyGUI::Widget* sender, MyGUI::Widget* oldFocus);
        void onButtonMouseFocus(MyGUI::Widget* sender, MyGUI::Widget* oldFocus);
        // Used by the accessibility hooks to detect when the menu was
        // covered (e.g. by the intro video widget grabbing key focus) and
        // is now becoming interactive again.
        void onButtonKeyLostFocus(MyGUI::Widget* sender, MyGUI::Widget* newFocus);
        // Speak the focused button's description when the user presses T
        // (Shift+T does the same -- there's only ever one line per button).
        void onButtonKeyButtonPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char ch);
        void onNewGameConfirmed();
        void onExitConfirmed();

        void showBackground(bool show);

        void updateMenu();

        std::unique_ptr<SaveGameDialog> mSaveGameDialog;
    };

}

#endif
