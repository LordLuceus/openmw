#ifndef GAME_MWACCESSIBILITY_SCANNER_H
#define GAME_MWACCESSIBILITY_SCANNER_H

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <osg/Vec3f>

#include <components/esm3/refnum.hpp>

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

        /// The object the player is currently locked onto (see toggleLockOn),
        /// or an empty Ptr when not locked. Exposed so engine interaction paths
        /// that normally resolve their target from the camera crosshair -- which
        /// a blind player cannot aim, and which can be blocked by furniture in
        /// front of the real target -- can use the explicit lock instead. Today
        /// the lockpick/probe path (CharacterController) consults this so picking
        /// a chest works even when the camera ray is obstructed.
        MWWorld::Ptr lockTarget() const { return mLockedOn ? mLockTarget : MWWorld::Ptr(); }

        /// Called by the WindowManager when the search prompt is confirmed.
        /// \p query is the (possibly empty) name filter; an empty query clears
        /// the filter. Applies to the current category, persists across cell
        /// rebuilds, and re-announces the resulting match count.
        void applySearchFilter(const std::string& query);

        /// Called when the search prompt is cancelled; re-announces the current
        /// selection so the user knows focus has returned to the scanner.
        void onSearchCancelled();

        /// Called by the WindowManager when the "drop note" prompt (N) is
        /// confirmed. Places a map note (custom marker) with \p text at the
        /// player's current position and announces it.
        void onWaypointNoteEntered(const std::string& text);

        /// Called when the "drop note" prompt is cancelled; announces that no
        /// note was placed.
        void onWaypointNoteCancelled();

    private:
        // Update the proximity audio cue to follow the current selection.
        // Call whenever the selected target changes (cycle, clear, reset).
        void updateProximityCue();

        void cycleCategory(int delta);
        void cycleTarget(int delta);
        void cycleSubcategory(int delta);
        // Whether \p cat should be offered when cycling categories. All the
        // record-type categories are always available; Detected is hidden
        // unless the player's active Detect effects currently reveal at least
        // one object, so the player only meets it when it's useful.
        bool isCategoryAvailable(Category cat) const;
        // Directly activate the selected target via the normal engine
        // activation path, bypassing the camera crosshair (which a blind
        // player cannot aim at small items). Returns true if it handled the
        // request (a target was selected), so the caller can consume the key
        // and suppress the default crosshair-based Activate.
        bool activateTarget();
        void focusCamera();
        void walkToTarget();

        // --- Combat / interaction lock-on --------------------------------
        // Toggle a persistent "lock-on" to the currently-selected target. While
        // locked, updateLockOn() re-aims the player at the target every frame
        // (yaw + pitch), so the engine's facing-direction based systems --
        // melee getHitContact(), the getFocusObject() raycast used by
        // lockpicks/probes, and spell/marksman launches -- all connect without
        // the player needing to aim a crosshair they can't see. Works for any
        // target type (an NPC to attack, or a chest/door to pick). Pressing the
        // key again, selecting nothing, target death, or starting an auto-walk
        // releases the lock.
        void toggleLockOn();
        // Per-frame re-aim while locked. No-op when not locked. Auto-releases
        // (with an announcement) if the locked target dies or leaves the world.
        void updateLockOn();
        // Release the lock if held. \p announce speaks "Lock released." Safe to
        // call when not locked (does nothing).
        void releaseLockOn(bool announce);
        // Open the text-input prompt to set/refine the current category's name
        // filter (see applySearchFilter). Seeds it with the active filter.
        void openSearch();
        // Open the "drop note" text prompt (see onWaypointNoteEntered). Bound to
        // N: places a map note at the player's current position.
        void openDropNote();
        void repeatAnnouncement();
        void clearSelection();
        void resetToFirst();
        // Announce the player's current location (cell name), e.g. "Seyda
        // Neen, Census and Excise Office". A quick orientation aid bound to L.
        void announceLocation();
        // Announce the player's current facing as an absolute compass point
        // (e.g. "Facing northeast"). A quick orientation aid bound to Shift+L,
        // complementing L (where am I) with which-way-am-I-looking.
        void announceFacing();

        void rebuildCurrentList();
        // Compute stable A/B/C suffixes for same-named objects in the active
        // category's list (populates CategoryState::mLabels). Called at the end
        // of rebuildCurrentList().
        void assignDisambiguationLabels();
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

        // A scanner waypoint: a bare world position with a spoken name. Used by
        // the Waypoints category, whose members (player map notes and the Mark
        // spell location) are positions, not world objects, so they can't live
        // in the Ptr-based mObjects list. Navigated via the position-based
        // AutoWalker / ProximityCue overloads.
        struct Waypoint
        {
            std::string mName;
            osg::Vec3f mPosition;
        };

        // --- Waypoints category helpers ----------------------------------
        // True when the active category is Waypoints (position-based, so the
        // Ptr-based action paths must defer to the waypoint equivalents).
        bool isWaypointCategory() const { return mCategory == Category::Waypoints; }
        // Size of the active category's list (objects or waypoints).
        size_t currentListSize() const;
        // The currently-selected waypoint, or nullptr if none / not in the
        // Waypoints category.
        const Waypoint* currentWaypoint() const;
        // Gather the player's waypoints (map notes in the current cell plus the
        // Mark spell location) into \p out, nearest first.
        void collectWaypoints(std::vector<Waypoint>& out) const;
        // Announce the currently-selected waypoint (name, distance, bearing,
        // N of M) -- the waypoint analogue of announceCurrent().
        void announceCurrentWaypoint();

        Category mCategory = Category::Npcs;

        struct CategoryState
        {
            std::vector<MWWorld::Ptr> mObjects;
            // Parallel list used only by the Waypoints category (mObjects stays
            // empty there). mIndex / mFilter / cycling all operate on whichever
            // of the two lists is active for the current category.
            std::vector<Waypoint> mWaypoints;
            int mIndex = -1; // -1 = nothing selected yet
            int mSubIndex = 0; // 0 = "All"; secondary filter within category
            bool mDirty = true;

            // Stable identity of the selected object (its RefNum), tracked
            // independently of mIndex so the selection can be re-pinned after a
            // rebuild that re-sorts or shifts the list -- e.g. when the player
            // crosses an exterior cell boundary and the active cell grid (and
            // thus the object list) changes. Unset when nothing is selected.
            ESM::RefNum mSelectedRef;

            // Active name filter for this category (case-insensitive substring).
            // Persists across cell-boundary rebuilds until the player changes or
            // clears it. Empty means "no filter" (the full list is shown).
            std::string mFilter;

            // Stable disambiguation suffixes for objects that share a display
            // name (e.g. four "Wooden Door, to Seyda Neen"). Keyed by the
            // object's RefNum -- a stable identity that does NOT change as the
            // list re-sorts by distance -- so a given physical door keeps the
            // same letter ("A", "B", ...) for as long as we're in the cell,
            // letting the player remember which ones they've already tried.
            // Objects with a unique name have no entry (no suffix spoken).
            std::unordered_map<ESM::RefNum, std::string> mLabels;
        };

        std::array<CategoryState, static_cast<size_t>(Category::Count)> mLists;

        // Cell tracking so we can invalidate the cache when the player
        // moves to a new cell.
        const void* mLastCellId = nullptr;

        // The last cell name we announced on entry. Cities span several cells
        // that all share one name (e.g. every Balmora exterior cell is named
        // "Balmora"), so we announce only when the resolved name actually
        // changes -- not on every cell-grid shift. Empty until the first
        // announcement. Stores the resolved (tag-substituted) display string so
        // the comparison matches what the player hears.
        std::string mLastAnnouncedCellName;
        // False until the cell name has been baselined for the current game
        // session. The first cell entered after a save load / new game (when
        // the player already knows where they are) is recorded silently rather
        // than announced; reset to false whenever no game is running so each
        // freshly-loaded game is primed afresh.
        bool mCellNamePrimed = false;
        // Announce the player's current cell name if it differs from the last
        // one announced (see mLastAnnouncedCellName). Called on cell change.
        void announceCellChange();

        AutoWalker mAutoWalker;
        ProximityCue mProximityCue;

        // Whether the audio beacon is currently enabled. Off by default.
        bool mBeaconEnabled = false;

        // --- Lock-on state ----------------------------------------------
        // The actor/object the player is currently locked onto for combat or
        // interaction, or empty when not locked. Held as a Ptr (refreshed each
        // frame in updateLockOn) so we can re-aim at it; auto-released if it
        // dies or unloads. Stored separately from the scanner cursor so the
        // player can keep cycling/inspecting other targets without breaking the
        // lock.
        MWWorld::Ptr mLockTarget;
        // Whether we are actively locked on (mLockTarget valid and being
        // tracked). A separate flag rather than just testing mLockTarget so the
        // intent is explicit and easy to gate updateLockOn() on.
        bool mLockedOn = false;
        // Spoken name of the locked target, captured at lock time so release /
        // status messages read sensibly even if the Ptr later goes stale.
        std::string mLockTargetName;
    };
}

#endif
