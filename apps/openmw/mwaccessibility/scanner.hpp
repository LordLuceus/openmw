#ifndef GAME_MWACCESSIBILITY_SCANNER_H
#define GAME_MWACCESSIBILITY_SCANNER_H

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "../mwworld/ptr.hpp"

#include "autowalker.hpp"
#include "category.hpp"
#include "proximitycue.hpp"

namespace MWAccessibility
{
    /// Scanner: maintains, per-category, a distance-sorted list of nearby
    /// world objects and lets the user cycle a "cursor" through them, with
    /// the currently-selected target announced via the screen-reader.
    ///
    /// Lifetime is owned by Engine. Lookups are lazy: the list for the
    /// active category is rebuilt only when the player crosses a cell
    /// boundary or when the user pages past one end, so cycling is cheap.
    class Scanner
    {
    public:
        Scanner();
        ~Scanner();

        /// Process-wide singleton. Engine owns the lifecycle; other
        /// subsystems (e.g. KeyboardManager) consult it via this.
        static Scanner& instance();

        /// Per-frame tick. Drives AutoWalker and invalidates cached
        /// lists when the player's cell changes.
        void onFrame(float dt);

        /// Called from KeyboardManager. \p scancode is an SDL_Scancode,
        /// \p modState is the raw SDL_GetModState() bitmask. Returns true
        /// if the scanner consumed the keypress.
        bool handleKey(int scancode, int modState);

        /// True when no game world is in a usable state (no save loaded,
        /// in a menu, in dialogue, etc.). Scanner keys are no-ops then.
        static bool isGameplayActive();

        AutoWalker& autoWalker() { return mAutoWalker; }

    private:
        // Update the proximity audio cue to follow the current selection.
        // Call whenever the selected target changes (cycle, clear, reset).
        void updateProximityCue();

        void cycleCategory(int delta);
        void cycleTarget(int delta);
        void cycleSubcategory(int delta);
        // Directly activate the selected target via the normal engine
        // activation path, bypassing the camera crosshair (which a blind
        // player cannot aim at small items). Returns true if it handled the
        // request (a target was selected), so the caller can consume the key
        // and suppress the default crosshair-based Activate.
        bool activateTarget();
        void focusCamera();
        void walkToTarget();
        void repeatAnnouncement();
        void clearSelection();
        void resetToFirst();

        void rebuildCurrentList();
        // Drop objects that have left the world (taken, deleted, or disabled)
        // from the active category's cached list, keeping the current
        // selection pinned to the same object where possible. Cheap; called
        // every frame so a picked-up item stops being announced immediately
        // without a full rebuild.
        void pruneDeadObjects();
        void announceCurrent();
        void speak(const std::string& text);

        // Toggle the audio beacon (proximity cue) on/off. Off by default so it
        // isn't constantly sounding; the player enables it only when actively
        // homing in on something.
        void toggleBeacon();

        // Returns empty Ptr when nothing is selected (or the list is empty).
        MWWorld::Ptr currentTarget();

        Category mCategory = Category::Npcs;

        struct CategoryState
        {
            std::vector<MWWorld::Ptr> mObjects;
            int mIndex = -1; // -1 = nothing selected yet
            int mSubIndex = 0; // 0 = "All"; secondary filter within category
            bool mDirty = true;
        };

        std::array<CategoryState, static_cast<size_t>(Category::Count)> mLists;

        // Cell tracking so we can invalidate the cache when the player
        // moves to a new cell.
        const void* mLastCellId = nullptr;

        AutoWalker mAutoWalker;
        ProximityCue mProximityCue;

        // Whether the audio beacon is currently enabled. Off by default.
        bool mBeaconEnabled = false;
    };
}

#endif
