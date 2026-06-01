#ifndef OPENMW_MWGUI_ACCESSIBILITY_SCREEN_H
#define OPENMW_MWGUI_ACCESSIBILITY_SCREEN_H

#include <functional>
#include <string>
#include <vector>

#include <MyGUI_KeyCode.h>
#include <MyGUI_Types.h>

#include "element.hpp"

namespace MyGUI
{
    class Widget;
}

namespace MWGui::A11y
{
    /// Per-window screen-reader controller. A MyGUI window owns one of these as
    /// a member, registers its options via add(), and forwards onOpen/onClose/
    /// onFrame to activate()/deactivate()/onFrame(). The Screen then implements
    /// the entire interaction model once:
    ///
    ///  - Up/Down      move focus between registered elements (in add() order)
    ///  - Left/Right   change the focused element's value
    ///  - Enter/Space  activate the focused element
    ///  - T / Shift+T  cycle the focused element's tooltips forward / backward
    ///  - a 2s linger announces how many tooltips the focused element has
    ///
    /// Navigation is driven directly (setKeyFocusWidget) and never by injecting
    /// Tab into the engine's KeyboardNavigation, so focus can't escape to other
    /// windows. While active, the Screen disables engine KeyboardNavigation.
    class Screen
    {
    public:
        /// \param disableEngineNav when true (default), engine spatial keyboard
        ///        navigation is turned off while this screen is active. Set
        ///        false for screens that still want the engine's Tab handling.
        explicit Screen(bool disableEngineNav = true);
        ~Screen();

        /// Register a navigable option. Forces the widget focusable and hooks
        /// its focus / key events. Call once per option, in navigation order.
        void add(Element element);

        /// Remove all registered elements (e.g. before rebuilding a dynamic
        /// screen). Does not change the active screen.
        void clear();

        /// Become the sole active screen: claim input, optionally disable engine
        /// nav, and focus \p initialFocus (or the first element if null),
        /// announcing it. Call from the window's onOpen().
        void activate(MyGUI::Widget* initialFocus = nullptr);

        /// Relinquish active status and restore engine nav. Call from onClose().
        void deactivate();

        /// Drive the delayed tooltip hint. Call from the window's onFrame().
        void onFrame(float dt);

        /// Move focus to \p widget (must be a registered element) and announce
        /// it. Useful after the screen changes its own contents (e.g. a tab
        /// switch) and wants to place focus explicitly.
        void focus(MyGUI::Widget* widget);

        /// Re-announce the currently focused element's label + value. Handy
        /// after a programmatic change that the framework didn't drive.
        void announceFocused();

        /// Install a handler for keys the framework doesn't consume (return
        /// true if handled). Lets a screen add bespoke shortcuts, e.g. settings'
        /// Ctrl+Left/Right tab cycling.
        void setExtraKeyHandler(std::function<bool(MyGUI::KeyCode)> handler)
        {
            mExtraKeyHandler = std::move(handler);
        }

        bool isActive() const;

    private:
        const Element* find(MyGUI::Widget* widget) const;
        const Element* focusedElement() const;
        void announce(const Element& element, bool withValue);
        void setFocus(MyGUI::Widget* widget);
        void moveFocus(int delta);
        void changeValue(bool next);
        void activateFocused();
        void cycleTooltip(bool forward);
        void resetHint(MyGUI::Widget* widget);

        // MyGUI event delegates.
        void onKeyFocus(MyGUI::Widget* sender, MyGUI::Widget* oldFocus);
        void onKey(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char ch);

        std::vector<Element> mElements;
        bool mDisableEngineNav;
        // Guards against double-announcing when setFocus() triggers
        // eventKeySetFocus: setFocus announces explicitly instead.
        bool mSuppressFocusAnnounce = false;

        // Tooltip cycling state for the currently focused element.
        std::vector<std::string> mTooltipLines;
        MyGUI::Widget* mTooltipWidget = nullptr;
        size_t mTooltipIndex = 0;

        // Delayed "has N tooltips" hint.
        MyGUI::Widget* mHintWidget = nullptr;
        float mHintTimer = 0.f;
        bool mHintSpoken = false;
        static constexpr float sHintDelay = 2.f;

        std::function<bool(MyGUI::KeyCode)> mExtraKeyHandler;
    };
}

#endif
