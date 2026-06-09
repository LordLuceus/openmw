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

        /// Drop ALL cached MWWorld::Ptr state (lock-on target, per-category
        /// object lists, cell tracking). Called from StateManager::cleanup when
        /// a game is loaded or ended -- that tears down the world synchronously,
        /// freeing the cell refs our Ptrs point at, so anything we keep would
        /// dangle. We can't rely on onFrame noticing a non-Running state because
        /// a quickload completes (unload old world, load save, return to
        /// Running) entirely within one input handler, before onFrame runs
        /// again; the next updateLockOn would then dereference a freed target
        /// and crash. This deterministic hook runs at the exact teardown point.
        void clear();

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
        /// Returns an empty Ptr unless a game is actually running and a target
        /// is locked. The running-state guard (mirroring the announce* helpers)
        /// makes this UAF-proof by construction: external consumers
        /// (World::castSpell, CharacterController) can call it during a teardown
        /// frame, and a dangling-but-non-null mLockTarget would NOT be caught by
        /// their isEmpty() checks -- so we never hand one out.
        MWWorld::Ptr lockTarget() const;

        /// Announce when the player attacks the locked target but can't reach
        /// it: "Out of range", or "Target too high"/"Target too low" when it's
        /// within horizontal reach but beyond the engine's vertical reach check.
        /// A blind player has no on-screen miss/whiff cue, so this tells them to
        /// close in (or that the target is above/below reach, e.g. a cliff
        /// racer). \p reach is the attack's reach in world units, so the caller
        /// supplies the right value: getMeleeWeaponReach for a melee swing, or
        /// fCombatDistance for a touch spell. No-op when not locked, the target
        /// isn't an actor, or the target is actually in reach. Throttled
        /// internally so rapid swings/casts don't spam speech. Called from
        /// CharacterController::prepareHit (melee) and World::castSpell (touch).
        void announceOutOfReach(float reach);

        /// Announce "No clear shot." when the player casts a ranged ("target")
        /// spell at the locked actor but the bolt's straight-line path is
        /// obstructed. Unlike melee/touch, ranged magic bolts have no distance
        /// cap -- they fly in a straight line (no gravity) until they hit
        /// something -- so the real failure a blind player can't see is an
        /// intervening wall, pillar or clutter between them and the target.
        /// Raycasts torso->target-centre along the exact trajectory lock-on
        /// aims (see updateLockOn); if the first thing hit isn't the target,
        /// the shot is blocked. No-op unless a game is running and locked onto a
        /// live actor with a clear LINE the bolt would otherwise follow.
        /// Throttled via the shared reach cooldown. Called from
        /// World::castSpell for the player's ranged spells/enchantments.
        void announceNoClearShot();

        /// Announce that another actor has cast a spell (or used a scroll/magic
        /// item), so a screen-reader player -- who can't see casting animations
        /// or coloured spell flashes -- knows a threat is incoming and what it
        /// is. \p caster is the casting actor, \p sourceName the spell/scroll
        /// display name. \p targetsOutward is true if the spell has any
        /// touch/target-range effect (i.e. it's aimed at someone, not a pure
        /// self-buff). Spoken as "<Caster> casts <spell>." with " at you"
        /// appended when the caster is in combat with the player AND the spell
        /// reaches outward. No-op for the player's own casts (handled by the
        /// weapon/spell-ready announcements) and for casts that are neither
        /// nearby nor by an actor targeting the player. Called from
        /// CastSpell::cast (spell and item/scroll paths).
        void announceActorSpellCast(
            const MWWorld::Ptr& caster, const std::string& sourceName, bool targetsOutward);

        /// --- Accessible HUD (AHUD) ---------------------------------------
        /// Toggle the accessible HUD. Bound to H. While active, the world is
        /// paused (so a suddenly-attacked player has time to assess and react)
        /// yet the scanner keys and the quick-info keys keep working, letting
        /// the player find an attacker, check stats, etc. Pressing H again (or
        /// Escape) closes it and unpauses. The pause is a time-manager tag, not
        /// a GuiMode, precisely so scanner input keeps flowing while paused.
        void toggleHud();
        /// Whether the AHUD is currently active.
        bool isHudActive() const { return mHudActive; }

        /// Quick-info readouts. Player stats are spoken as "<Stat> <current> of
        /// <max>" (rounded), matching the numbers on the native bars. Enemy
        /// health is spoken as a percentage only -- the native enemy health bar
        /// exposes no numbers to sighted players, so neither do we. The enemy is
        /// the locked target if one is held, else the current scanner selection;
        /// a no-op (with a brief spoken note) if that isn't a living actor.
        /// These work both in normal gameplay and while the AHUD is open.
        void announcePlayerHealth();
        void announcePlayerMagicka();
        void announcePlayerFatigue();
        void announceEnemyHealth();

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
        // Snap the player's facing to the previous/next of the eight compass
        // points (Ctrl+A = counter-clockwise, Ctrl+D = clockwise). Levels the
        // pitch and announces the new heading. A keyboard-friendly way to aim
        // along a cardinal/intercardinal direction without a mouse.
        void snapToDirection(bool clockwise);
        // Turn the player 180 degrees (Ctrl+S), announcing the new facing.
        void turnAround();

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
            // True when this waypoint lives in the SAME worldspace as the player
            // (e.g. both in the Morrowind exterior), so mPosition is directly
            // comparable to the player's: distance, bearing, and auto-walk are
            // all meaningful. False for notes in interiors or another worldspace
            // -- we still list them (so distant towns/dungeons are discoverable)
            // but speak only a crude area label and refuse auto-walk, since the
            // raw XY can't be compared across coordinate systems.
            bool mReachable = true;
            // A short human-readable location for an unreachable waypoint (the
            // cell/region name, e.g. "Balmora" or "Ascadian Isles"). Empty for
            // reachable ones (which announce a real distance/bearing instead).
            std::string mAreaLabel;
        };

        // --- Position-based category helpers -----------------------------
        // Two categories (Waypoints and Locations) navigate bare world
        // positions rather than world objects, so they share the position-based
        // AutoWalker / ProximityCue paths and the mWaypoints list. The Ptr-based
        // action paths must defer to the waypoint equivalents for either.
        bool isWaypointCategory() const
        {
            return mCategory == Category::Waypoints || mCategory == Category::Locations;
        }
        // Size of the active category's list (objects or waypoints).
        size_t currentListSize() const;
        // The currently-selected waypoint, or nullptr if none / not in a
        // position-based category.
        const Waypoint* currentWaypoint() const;
        // Gather the player's waypoints (all map notes across the world plus the
        // Mark spell location) into \p out, reachable-first.
        void collectWaypoints(std::vector<Waypoint>& out) const;
        // Gather discovered global-map locations (visited named cells + NPC-
        // marked places, one entry per town) into \p out as waypoints, nearest
        // first. All are reachable exterior positions.
        void collectLocations(std::vector<Waypoint>& out) const;
        // Announce the currently-selected waypoint (name, distance, bearing,
        // N of M) -- the position-based analogue of announceCurrent().
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

        // --- Draw-state announcement ------------------------------------
        // Last observed player draw state (nothing / weapon drawn / spell
        // readied), polled each frame in onFrame so we can announce the
        // transition -- e.g. "Iron Dagger ready", "Fireball ready", "Weapon
        // sheathed". Sighted players see the readied weapon/spell on the HUD;
        // this gives the same feedback by ear. Stored as the raw enum value
        // (MWMechanics::DrawState) cast to int to avoid pulling the enum into
        // the header. -1 = uninitialised (no announcement on first poll).
        int mLastDrawState = -1;
        // Poll the player's draw state and announce any change (see above).
        void announceDrawStateChange();

        // --- Live refresh of the Actors list ----------------------------
        // Actors move and change combat state continuously, so a list cached at
        // selection time goes stale fast: a newly-hostile attacker won't appear
        // in the Hostile subcategory, and distances/ordering drift as actors
        // approach or flee. While the Actors category is active we silently
        // rebuild its list on this cadence (seconds) so membership, distance
        // order, and the proximity cue stay current. The rebuild re-pins the
        // cursor onto the same object by RefNum, so the player doesn't lose
        // their place. Other categories (doors, items, ...) are static, so they
        // don't need this. Refresh does NOT announce -- the next explicit
        // action speaks the up-to-date state.
        float mActorRefreshTimer = 0.f;
        // Silently rebuild the active category's list, preserving the current
        // selection by RefNum. Used by the live-refresh path.
        void refreshActiveListPreservingSelection();

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

        // --- Accessible HUD (navigable list) ----------------------------
        // Whether the AHUD is open. When true the world is paused via a
        // time-manager tag (see toggleHud) but scanner/quick-info keys still
        // work. Cleared (and the pause lifted) by toggleHud and by clear().
        bool mHudActive = false;
        // One row of the HUD list, built fresh each time the HUD opens (the
        // world is paused, so a snapshot is fine). mLabel is what's spoken when
        // the cursor lands on it. mIsEffects marks the single "Active effects"
        // row, which the player drills into with Enter to walk a sub-list.
        struct HudItem
        {
            std::string mLabel;
            bool mIsEffects = false;
            // The target row recomputes its label from the live scanner/lock
            // selection each time it's announced, so it tracks the target the
            // player cycles to with the scanner keys while the HUD is open
            // (rather than showing a stale snapshot from when the HUD opened).
            bool mIsTarget = false;
        };
        std::vector<HudItem> mHudItems; // current snapshot of HUD rows
        int mHudIndex = 0; // cursor within mHudItems
        // When true the cursor is inside the active-effects sub-list (entered
        // from the Effects row); Up/Down walk mHudEffects, Escape/Left back out.
        bool mHudInEffects = false;
        // Snapshot of the active-effect sub-list (source + effect strings),
        // taken when the player drills in. Parallel to MWGui::A11y::activeEffects.
        std::vector<std::pair<std::string, std::string>> mHudEffects;
        int mHudEffectIndex = 0;
        // Last target-row label spoken, so the per-frame poll (hudFollowTarget)
        // re-announces only when the live target actually changes -- letting the
        // target row track the actor the player cycles the scanner to while
        // parked on it, without repeating on unchanged frames.
        std::string mHudLastTargetLabel;
        // While the HUD is open and the cursor rests on the live target row,
        // re-announce that row whenever the underlying target changes. No-op
        // otherwise. Called each frame from onFrame (which runs even while the
        // HUD-paused world is frozen).
        void hudFollowTarget();

        // Build mHudItems from the current player/enemy state (called on open).
        void buildHudItems();
        // Move the HUD cursor by \p delta (handles both the main list and the
        // effects sub-list) and speak the row landed on.
        void hudMove(int delta);
        // Speak the current HUD row (or effect sub-row when drilled in).
        void announceHudCurrent();
        // Enter / leave the active-effects sub-list from the Effects row.
        void hudEnterEffects();
        void hudLeaveEffects();
        // Route a key while the AHUD is open. Returns true if consumed.
        bool handleHudKey(int scancode, bool ctrl, bool shift, bool alt);

        // Speak one player dynamic stat as "<label> <current> of <max>".
        // \p index is the DynamicStat index (0 health, 1 magicka, 2 fatigue).
        void announcePlayerStat(int index, const char* label);
        // The actor whose health the enemy-info key reports: the locked target
        // if held, otherwise the current scanner selection. Empty if neither is
        // a valid actor.
        MWWorld::Ptr enemyInfoTarget();
        // String builders shared by the quick-info keys and the HUD list rows.
        // Each returns the spoken phrase for one HUD element, or an empty string
        // when that element has nothing to report (so it can be skipped).
        std::string playerStatText(int index, const char* label) const;
        std::string readiedWeaponText() const; // "Weapon: Iron Dagger"
        std::string readiedSpellText() const; // "Spell: Fireball"
        std::string enemyHealthText(); // "<Name>, health N percent"
        std::string targetHealthLabel(); // "Target: <Name>, health N percent" / "Target: none"
        std::string locationText() const; // resolved cell name
        std::string breathText() const; // "Breath N percent" (only underwater)

        // Throttle for announceMeleeReach: a swung weapon resolves its hit
        // several times per second, so we rate-limit the "out of range" speech.
        // Counts DOWN each frame in onFrame; announceMeleeReach speaks only when
        // it has reached 0, then resets it to the cooldown interval. So the
        // first out-of-range swing speaks at once and repeats only after the
        // interval elapses.
        float mMeleeReachCooldown = 0.f;
    };
}

#endif
