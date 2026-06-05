#ifndef OPENMW_MWGUI_ACCESSIBILITY_PANEGROUP_H
#define OPENMW_MWGUI_ACCESSIBILITY_PANEGROUP_H

#include <string>
#include <vector>

namespace MWGui::A11y
{
    class Screen;

    /// Coordinates Tab / Shift+Tab switching between sibling accessible windows
    /// that the engine shows together in a single GUI mode -- most importantly
    /// the Inventory mode, which displays the Stats, Inventory, Spell and Map
    /// windows simultaneously. The A11y framework keeps exactly one Screen
    /// active at a time (see UiManager), so without a coordinator only one of
    /// those panes could ever be reached.
    ///
    /// Each participating window enrols its Screen on open (with a display name
    /// spoken when the pane is switched to, and an order that fixes its place in
    /// the Tab cycle, e.g. Stats=0, Inventory=1) and withdraws it on close.
    /// Tab / Shift+Tab then move between the enrolled panes, wrapping around.
    ///
    /// Switching uses Screen::suspend()/resume() rather than deactivate()/
    /// activate(): the windows all stay visible, so each pane keeps its built
    /// option list and cursor position, and returning to a pane lands exactly
    /// where the user left it.
    class PaneGroup
    {
    public:
        static PaneGroup& instance();

        /// Record \p screen as an available pane. \p label is spoken (followed
        /// by the pane's current option) when the user switches to it; \p order
        /// fixes its position in the Tab cycle (lower comes first). Idempotent:
        /// re-enrolling an already-present screen just updates its label/order.
        /// Does not itself change which pane is active -- the lowest-order pane
        /// claims focus via maybeActivateInitial() once the windows are shown.
        void enrol(Screen* screen, std::string label, int order);

        /// Remove \p screen from the group (call from the window's onClose).
        /// Does not activate another pane: withdrawal happens during mode
        /// teardown, when every pane is closing.
        void withdraw(Screen* screen);

        /// Called from Screen::onFrame for any enrolled pane. If no pane is
        /// active yet, the pane that should claim focus does so and announces its
        /// first option. The target is the last pane the user was on (remembered
        /// across a temporary sub-mode such as reading a book), or the
        /// lowest-order pane (e.g. Stats) on a fresh open. Only the target pane
        /// self-activates, so there's no transient announcement from another.
        void maybeActivateInitial(Screen* screen);

        /// Forget the remembered last-active pane so the next fresh open lands on
        /// the lowest-order pane again. Call when the multi-pane GUI mode is
        /// fully exited (not merely hidden behind a sub-mode).
        void resetMemory() { mLastActiveOrder = -1; }

        /// Tab (\p delta = +1) / Shift+Tab (\p delta = -1): suspend the active
        /// pane and resume the next/previous one, wrapping around. Announces the
        /// target pane's name then its current option. No-op (returns false)
        /// when fewer than two panes are enrolled.
        bool cycle(int delta);

        /// True if \p screen is currently enrolled.
        bool contains(const Screen* screen) const;

        /// Number of enrolled panes.
        std::size_t size() const { return mPanes.size(); }

    private:
        PaneGroup() = default;
        PaneGroup(const PaneGroup&) = delete;
        PaneGroup& operator=(const PaneGroup&) = delete;

        struct Pane
        {
            Screen* screen;
            std::string label;
            int order;
        };

        // Index of the enrolled pane backing \p screen, or npos.
        std::size_t indexOf(const Screen* screen) const;

        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        // Kept sorted by ascending order (front() is the lowest-order pane).
        std::vector<Pane> mPanes;

        // Order value of the pane the user was last on, remembered so a brief
        // sub-mode (e.g. reading a book, which hides + reshows these windows)
        // returns the user to the same pane instead of resetting to the first.
        // -1 means "no memory" -> land on the lowest-order pane.
        int mLastActiveOrder = -1;
    };
}

#endif
