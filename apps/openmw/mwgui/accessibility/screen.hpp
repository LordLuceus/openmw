#ifndef OPENMW_MWGUI_ACCESSIBILITY_SCREEN_H
#define OPENMW_MWGUI_ACCESSIBILITY_SCREEN_H

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
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
    ///  - Up/Down      move between registered options (skipping hidden ones)
    ///  - Left/Right   change the current option's value
    ///  - Enter/Space  activate the current option, or expand it if it is an
    ///                 expandable submenu (Element::children)
    ///  - T / Shift+T  cycle the current option's tooltips forward / backward
    ///  - Escape       collapse an open submenu back to the main screen
    ///  - a 2s linger announces how many tooltips the current option has, or
    ///                 "Press Enter to expand" for an expandable submenu
    ///
    /// Expandable submenus: an option whose Element::children is set becomes a
    /// nested list. Enter opens it; Up/Down then move between child items (the
    /// section name is spoken when focus crosses into a new section, e.g.
    /// "Major Skills: Alteration"); Ctrl+Up/Down jump between sections; Home/End
    /// jump to the first/last item; T cycles a child's tooltips; Escape returns
    /// to the main screen on the option you expanded.
    ///
    /// The "current option" is tracked internally by index, never by querying
    /// MyGUI's key-focus widget, so it is immune to focus being stolen by
    /// hidden windows or native controls.
    ///
    /// Two focus modes:
    ///  - Real focus (default): each option widget actually receives MyGUI key
    ///    focus as you navigate. Good for screens whose widgets are passive
    ///    focus proxies (e.g. text headings).
    ///  - Virtual focus (setVirtualFocus): real key focus is pinned to a single
    ///    anchor widget and never moves; option widgets are made non-focusable
    ///    so native controls (ListBox/ComboBox/ScrollBar) can't grab the arrow
    ///    keys. Navigation is purely internal. Good for screens built from
    ///    native widgets.
    ///
    /// While active, the Screen disables engine KeyboardNavigation so its
    /// spatial nav can't fight ours.
    class Screen
    {
    public:
        /// \param disableEngineNav when true (default), engine spatial keyboard
        ///        navigation is turned off while this screen is active.
        explicit Screen(bool disableEngineNav = true);
        ~Screen();

        /// Switch this screen to virtual-focus mode using \p anchor as the
        /// permanently-focused widget. Call before add()/activate(). In this
        /// mode option widgets are not focused and are made non-focusable.
        void setVirtualFocus(MyGUI::Widget* anchor);

        /// Register a navigable option. In real-focus mode this forces the
        /// widget focusable and hooks its focus / key events; in virtual-focus
        /// mode it makes the widget non-focusable (the anchor handles keys).
        /// Call once per option, in navigation order.
        void add(Element element);

        /// Remove all registered elements (e.g. before rebuilding a dynamic
        /// screen). Does not change the active screen.
        void clear();

        /// Become the sole active screen: claim input, optionally disable engine
        /// nav, and select \p initialFocus (or the first visible element if
        /// null), announcing it. Call from the window's onOpen().
        void activate(MyGUI::Widget* initialFocus = nullptr);

        /// Relinquish active status and restore engine nav. Call from onClose().
        void deactivate();

        /// Drive the delayed tooltip hint. Call from the window's onFrame().
        void onFrame(float dt);

        /// Select \p widget (must be a registered element) and announce it.
        void focus(MyGUI::Widget* widget);

        /// Select the first visible/enabled element. Announces it unless
        /// \p announce is false. Useful after the screen swaps its own
        /// contents (e.g. a tab change); pass false when the new selection was
        /// already spoken (e.g. by a value change that triggered the rebuild).
        void focusFirst(bool announce = true);

        /// Re-announce the current option's label + value.
        void announceCurrent();

        /// The current option's label, or empty if nothing is selected. Useful
        /// for preserving selection across a clear()/rebuild of a dynamic
        /// screen whose options have no backing widget to refocus.
        std::string currentLabel() const;

        /// Select the first usable element whose label matches \p label,
        /// announcing it unless \p announce is false. Returns false if no such
        /// element exists (selection unchanged). Companion to currentLabel()
        /// for restoring selection after a rebuild.
        bool selectByLabel(std::string_view label, bool announce);

        /// The widget backing the current option, or null if none selected.
        /// Lets an owner implement option-type-specific extra keys.
        MyGUI::Widget* currentWidget() const;

        /// Install a handler for keys the framework doesn't consume (return
        /// true if handled). Lets a screen add bespoke shortcuts, e.g. settings'
        /// Ctrl+Left/Right tab cycling.
        void setExtraKeyHandler(std::function<bool(MyGUI::KeyCode)> handler)
        {
            mExtraKeyHandler = std::move(handler);
        }

        /// Feed a key to the controller directly. Screens in virtual-focus mode
        /// whose anchor doesn't reliably deliver key events can call this from
        /// their own key hook. Safe to call only while active.
        void handleKey(MyGUI::KeyCode key) { onKeyValue(key); }

        bool isActive() const;

        /// One-shot: returns true (and resets the latch) if the most recent
        /// Escape was consumed to leave edit mode. A WindowModal whose exit()
        /// defaults to closing on Escape should call this from exit() and return
        /// false when it's true, so the Escape that ends text editing doesn't
        /// also close the dialog. (The engine's Escape action runs *after* our
        /// key handler within the same keystroke, so the latch is still set when
        /// exit() is queried.)
        bool consumeEditModeEscape();

        /// True while the current option's text field is being edited.
        bool inEditMode() const { return mEditMode; }

        /// Begin editing the current option's text field immediately (if it is
        /// editable). For dialogs whose sole purpose is text entry (e.g. the
        /// class description), so the user can type the moment it opens instead
        /// of having to press Enter first. Call after activate().
        void beginEditing() { enterEditMode(); }

    private:
        static constexpr size_t npos = static_cast<size_t>(-1);

        const Element* find(MyGUI::Widget* widget) const;
        size_t indexOf(MyGUI::Widget* widget) const;
        const Element* current() const;
        bool isUsable(size_t index) const;
        void select(size_t index, bool announce);
        void announce(const Element& element, bool withSection = false);
        void moveSelection(int delta);
        void jumpSection(int delta);  // Ctrl+Up/Down: jump between top-level sections
        void changeValue(bool next);
        void activateCurrent();
        void cycleTooltip(bool forward);
        void resetHint();

        // Editable-text-field helpers.
        void enterEditMode();  // begin editing the current option (if editable)
        void exitEditMode();   // stop editing, return to form navigation

        // Expandable-submenu helpers.
        bool inSubmenu() const { return mSubOpen; }
        void openSubmenu();          // expand current option (if it has children)
        void closeSubmenu(bool announceParent = true); // collapse back to main
        void moveSubSelection(int delta);
        // Jump to the first item of the previous/next section (Ctrl+Up/Down).
        void jumpSubSection(int delta);
        // Jump to the first / last item in the submenu (Home / End).
        void jumpSubEdge(bool last);
        void announceSubItem(size_t index, bool withSection);
        void cycleSubTooltip(bool forward);

        // MyGUI event delegates.
        void onKeyFocus(MyGUI::Widget* sender, MyGUI::Widget* oldFocus);
        void onKey(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char ch);
        void onKeyValue(MyGUI::KeyCode key);

        std::vector<Element> mElements;
        bool mDisableEngineNav;

        size_t mCurrent = npos;

        bool mVirtual = false;
        MyGUI::Widget* mAnchor = nullptr;

        // In virtual mode, the widget that held key focus *before* we pinned
        // the anchor (e.g. the main-menu Options button). Restored on
        // deactivate() so focus -- and the screen-reader announcement -- return
        // to the opener. This also repairs the engine's saved-focus memory,
        // which a blocking modal dialog (language/resolution confirm) corrupts
        // by overwriting it with our anchor.
        MyGUI::Widget* mPreFocus = nullptr;

        // True while we've handed control to a native modal dialog
        // (confirmation / interactive message box). We re-enable engine
        // keyboard navigation and stop pinning anchor focus until it closes.
        bool mYieldedToModal = false;

        // Guards against double-announcing when we set real key focus and the
        // resulting eventKeySetFocus would announce again.
        bool mSuppressFocusAnnounce = false;

        // Tooltip cycling state for the current option.
        std::vector<std::string> mTooltipLines;
        size_t mTooltipElement = npos;
        size_t mTooltipIndex = 0;

        // Delayed hint, keyed by element index (and sub-item index while a
        // submenu is open). Announces "Press Enter to expand" for an expandable
        // option, otherwise "Has N tooltips" for an option / sub-item that has
        // tooltips.
        size_t mHintElement = npos;
        size_t mHintSubItem = npos;
        float mHintTimer = 0.f;
        bool mHintSpoken = false;
        static constexpr float sHintDelay = 2.f;

        // Expandable-submenu state. While mSubOpen, navigation operates on
        // mSubItems (a snapshot of the parent option's children, taken when the
        // submenu was opened) instead of the top-level element list.
        bool mSubOpen = false;
        std::vector<SubItem> mSubItems;
        size_t mSubCurrent = npos;

        // Editable-text-field state. While mEditMode, all keys (except the
        // Escape that exits) are left for the focused EditField to handle, so
        // the framework's arrow/Enter navigation is suspended.
        bool mEditMode = false;
        // Latch set when an Escape was used to leave edit mode, read+cleared by
        // consumeEditModeEscape() so the same Escape doesn't also close a modal.
        bool mEscapeConsumed = false;
        // Tooltip cycling state for the focused sub-item (mirrors the top-level
        // tooltip state but keyed by sub-item index).
        std::vector<std::string> mSubTooltipLines;
        size_t mSubTooltipItem = npos;
        size_t mSubTooltipIndex = 0;

        std::function<bool(MyGUI::KeyCode)> mExtraKeyHandler;
    };
}

#endif
