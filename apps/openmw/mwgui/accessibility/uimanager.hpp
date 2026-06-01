#ifndef OPENMW_MWGUI_ACCESSIBILITY_UIMANAGER_H
#define OPENMW_MWGUI_ACCESSIBILITY_UIMANAGER_H

namespace MWGui::A11y
{
    class Screen;

    /// Process-wide tracker of which accessible Screen currently owns input.
    ///
    /// Acts as a minimal state machine: exactly one Screen is "active" at a
    /// time (or none). Every Screen checks this before handling a key or frame
    /// event, so a stale event from a hidden window can never reach a
    /// non-active screen -- which is what previously caused input on one
    /// screen to read out widgets belonging to another (e.g. the main menu
    /// behind the settings window).
    class UiManager
    {
    public:
        static UiManager& instance();

        /// Make \p screen the sole active screen.
        void setActive(Screen* screen) { mActive = screen; }

        /// Clear \p screen if (and only if) it is currently active. Passing the
        /// caller guards against a screen that opened after us clearing our
        /// state when it closes.
        void clear(Screen* screen)
        {
            if (mActive == screen)
                mActive = nullptr;
        }

        Screen* active() const { return mActive; }

        bool isActive(const Screen* screen) const { return mActive == screen; }

    private:
        UiManager() = default;
        UiManager(const UiManager&) = delete;
        UiManager& operator=(const UiManager&) = delete;

        Screen* mActive = nullptr;
    };
}

#endif
