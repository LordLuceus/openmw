#ifndef GAME_MWACCESSIBILITY_AUTOWALKER_H
#define GAME_MWACCESSIBILITY_AUTOWALKER_H

#include <deque>
#include <limits>
#include <utility>

#include <osg/Vec3f>

#include "../mwmechanics/pathfinding.hpp"
#include "../mwworld/ptr.hpp"

namespace MWAccessibility
{
    /// Drives the player toward a chosen world object via the engine's
    /// detour navmesh pathfinder. Construction is cheap; activation
    /// happens via start(). Cancellation is automatic on user input
    /// (movement keys), on arrival, or on target loss.
    class AutoWalker
    {
    public:
        AutoWalker();
        ~AutoWalker();

        /// Begin walking toward \p target. Returns false if a path could
        /// not be built (in which case nothing changes and the caller
        /// should announce the failure).
        bool start(const MWWorld::Ptr& target);

        /// Begin walking toward a fixed world position \p target (used for
        /// scanner waypoints -- map notes and the Mark spot -- which are bare
        /// positions with no backing object). \p name is spoken in arrival/
        /// failure messages. Returns false if a path could not be built.
        bool start(const osg::Vec3f& target, const std::string& name);

        /// Stops auto-walk and zeroes the player's forward-movement
        /// setting. Safe to call when not active.
        void cancel();

        bool isActive() const { return mActive; }

        /// Per-frame: re-aims the player toward the next waypoint and
        /// drives forward movement. Calls cancel() automatically when
        /// arrived or the target is gone.
        void onFrame(float dt);

        /// The name the AutoWalker is currently chasing, for diagnostic
        /// messages.
        const std::string& targetName() const { return mTargetName; }

        /// Teleport escape hatch. Auto-walk can fail to REACH a target that is
        /// obviously there (you flew up to a ledge and now no walkable path
        /// leads back; pathfinding gets within metres but can't traverse the
        /// drop). When a walk gives up or stops short, we ARM a one-shot
        /// teleport to that target's last position. The scanner exposes this on
        /// a deliberately awkward key (Ctrl+Shift+Enter) so the player can blink
        /// the short gap. Guardrails live in the scanner (distance cap; only
        /// armed after a real failed attempt). Returns true and fills \p outPos
        /// / \p outName if a teleport is armed; consuming it disarms it.
        bool consumeTeleportTarget(osg::Vec3f& outPos, std::string& outName);
        bool teleportArmed() const { return mTeleportArmed; }

    private:
        // Outcome of probing for a closed door across the path (tryOpenBlockingDoor).
        enum class DoorProbe
        {
            None, // no closed door ahead -- caller continues its normal escalation
            Opened, // opened a safe, closed, in-cell, unlocked, untrapped door -- keep walking
            Blocked, // a door is ahead we must NOT open (locked / trapped / teleport) -- stop the walk
        };

        bool rebuildPath();
        // Reset all progress / stuck / recovery state for a fresh walk.
        void resetProgress();
        // Scan the freshly-built path for hazards (deep water crossings and
        // steep drops) and, if any are found, speak a single up-front warning so
        // the player can prepare (Levitation / Water Walking) or cancel. Called
        // once per fresh walk from start(), not on periodic re-paths, so the
        // warning isn't repeated every second. Purely advisory: auto-walk still
        // proceeds (the user chose warn-and-continue).
        void warnRouteHazards();
        // Rotate \p player to face \p targetPos horizontally, so the target is
        // lined up dead ahead. Used when auto-walk stops short of an
        // unreachable target so the player can close the last gap manually.
        void faceTarget(const MWWorld::Ptr& player, const osg::Vec3f& targetPos);
        // Face the target and announce that we stopped short of it (with the
        // remaining distance), suggesting the audio beacon to find the route.
        void announceStoppedShort(const MWWorld::Ptr& player, const osg::Vec3f& targetPos, float trueDist);
        // Arm the teleport escape hatch at the current target's position, so the
        // player can blink the gap if the walk failed to reach an obviously
        // present target. Called from the give-up / stop-short failure paths.
        void armTeleport();
        // Pick which way to sidestep during a recovery wiggle by probing both
        // sides with a short raycast and choosing the more open one. This is how
        // we squeeze around an NPC standing in a narrow doorway/aisle (actors
        // aren't baked into the navmesh, so the route runs straight through
        // them): blindly alternating left/right often picks the blocked side.
        // Returns +1 (step right) or -1 (step left). \p yaw is the player's
        // current facing. Falls back to \p fallbackDir if both sides look equal
        // or raycasting is unavailable.
        float chooseRecoverySide(const MWWorld::Ptr& player, const osg::Vec3f& playerPos, float yaw,
            float fallbackDir) const;
        // Probe straight ahead for an actor (NPC/creature) physically blocking
        // the path. Used when recovery has failed: actors aren't baked into the
        // navmesh, so a stationary NPC in a doorway plugs the route solid and no
        // amount of wiggling gets past. Returns the blocking actor's Ptr, or an
        // empty Ptr if what's ahead is static geometry (or nothing). \p yaw is
        // the player's current facing.
        MWWorld::Ptr detectBlockingActor(const MWWorld::Ptr& player, const osg::Vec3f& playerPos, float yaw) const;
        // Probe straight ahead for a CLOSED door blocking the path and, if found,
        // open it and keep walking. The navmesh routes THROUGH doors (the player
        // agent has Flag_openDoor), assuming they'll be opened -- but nothing in
        // auto-walk actuated them, so the player just wedged against a shut door.
        // We only ever OPEN a door that is closed, in-cell (non-teleport),
        // unlocked, and UNTRAPPED. The other cases are reported and stop the walk
        // rather than being forced:
        //   - teleport door: walking the player through would yank them into a
        //     cell they didn't choose to enter;
        //   - locked door: we can't pick it mid-walk, and grinding against it just
        //     spun the recovery wiggle pointlessly;
        //   - TRAPPED door: actuating it would spring the trap on the player --
        //     potentially lethal -- so we must never auto-open it.
        // Returns DoorProbe::Opened (door opened; caller refreshes budgets and
        // keeps walking), DoorProbe::Blocked (a door we must not open is in the
        // way; caller announces via the give-up path and stops), or
        // DoorProbe::None (no closed door ahead; caller continues as before).
        // \p yaw is the player's current facing.
        DoorProbe tryOpenBlockingDoor(const MWWorld::Ptr& player, const osg::Vec3f& playerPos, float yaw);
        // Handle a give-up condition. Probes ahead for a blocking NPC: if one is
        // found and we're not already phasing, disables that NPC's collision so
        // the player slips past, announces "X is blocking the way. Moving past.",
        // refreshes the progress budget, and returns FALSE (keep walking). If
        // there's no person blocking (genuine geometry / unreachable) or we
        // already phased and are still stuck, it speaks the appropriate report
        // ("X is blocking the way to Y" / stopped-short beacon hint / "Stuck.
        // Cannot reach Y") and returns TRUE (cancel the walk). Shared by both
        // give-up paths (no-progress backstop AND exhausted-recovery wedge) so
        // the behavior is identical regardless of which trips first.
        bool handleGiveUp(const MWWorld::Ptr& player, const osg::Vec3f& playerPos, const osg::Vec3f& targetPos);

        bool mActive = false;
        // A target is either a world object (mTarget) or, for scanner
        // waypoints, a fixed position (mTargetPos with mHasPtrTarget == false).
        // mTarget is empty in the position case.
        MWWorld::Ptr mTarget;
        osg::Vec3f mTargetPos;
        bool mHasPtrTarget = true;
        // The destination actually fed to the pathfinder: the requested target
        // position snapped to the nearest walkable navmesh point (see
        // kNavMeshSnapRadius). Refreshed every rebuildPath(). Falls back to the
        // raw target position when no navmesh point is found nearby. Arrival is
        // accepted at either this point or the true target, whichever the
        // player reaches first, so we still "arrive" when standing in front of
        // a door embedded in a wall.
        osg::Vec3f mEffectiveTarget;
        std::string mTargetName;
        // Teleport escape-hatch arm state. Set in the give-up / stop-short
        // failure paths to the target's last position and name; consumed (and
        // disarmed) by consumeTeleportTarget(). Deliberately NOT cleared by
        // cancel()/resetProgress() (the failure paths call cancel() right after
        // arming) -- cleared instead at the start of a new walk and on arrival.
        bool mTeleportArmed = false;
        osg::Vec3f mTeleportPos;
        std::string mTeleportName;
        MWMechanics::PathFinder mPathFinder;
        float mTimeSinceRepath = 0.0f;

        // Set when the current route came from the hand-authored pathgrid
        // fallback (rebuildPath adopted it because the navmesh route fell short
        // in a multi-level interior). Such routes are COARSE and, crucially,
        // rebuilding them every frame-second re-inserts the node we just passed,
        // which on steep stairs makes the "next" waypoint flip back and forth
        // across the player and drives a backstep oscillation. So while this is
        // set we SUPPRESS the periodic re-path and just follow the route we have
        // (we still re-path when the path is consumed, on recovery, or for a
        // moving target). Recomputed on every rebuildPath.
        bool mStablePath = false;

        // Whether FALL-ARREST should be active for the CURRENT route. A clean
        // navmesh route can't contain a lethal drop: Recast bakes slope and
        // ledge constraints into the mesh, so its polygons never span a fatal
        // fall, and following one can't walk us off a cliff. Fall-arrest only
        // earns its keep -- and only risks false positives -- on the COARSE
        // route types that ignore those constraints: the hand-authored pathgrid
        // fallback (nodes can bridge gaps), the progressive cross-cell carrot/
        // bee-line across not-yet-loaded terrain, and the last-resort straight
        // line. So we arm fall-arrest only for those and leave it OFF for a
        // normal navmesh route, where it was the sole source of false catches.
        // Recomputed every rebuildPath.
        bool mFallArrestEnabled = false;

        // [a11y] TEMPORARY: throttle for the per-frame stair-follow diagnostic so
        // we log ~5x/sec instead of every frame. Remove with the diagnostic.
        float mStairDiagTimer = 0.0f;

        // Resolve the current target's world position, whether it's a Ptr or a
        // fixed point. Returns false (via the out-param being left untouched is
        // avoided) only conceptually; callers check mActive/emptiness first.
        osg::Vec3f targetPosition() const;

        // Stuck detection: if the player's horizontal position hasn't
        // moved meaningfully within mStuckWindow seconds, declare we
        // have arrived (or are unreachable, depending on distance to
        // target).
        // Stuck-detection is split into two independent signals:
        //
        // 1. PHYSICAL movement (mLastPos / mTimeSinceMove): are we actually
        //    moving through the world, or wedged against geometry? This drives
        //    the recovery wiggle. We deliberately do NOT use distance-to-goal
        //    for this: a legitimate navmesh detour around a wall moves the
        //    player tangentially to -- or briefly away from -- the goal for a
        //    second or two, and a goal-distance check mis-reads that as "stuck"
        //    and fires a jump/strafe on otherwise clean terrain (the spurious-
        //    jumping bug). Physical motion is the honest "am I wedged?" signal.
        //    mLastPos is the previous frame's position; mTimeSinceMove counts
        //    how long we've been commanding forward motion without the body
        //    actually moving.
        // 2. ROUTE progress (mBestPathRemaining / mTimeSinceProgress): the
        //    shortest remaining-path-length along the planned route we've
        //    achieved, and how long we've failed to beat it. This resets the
        //    recovery-attempt counter on genuine progress and acts as a
        //    long-timeout backstop that gives up if we're moving but never
        //    advancing along the route (e.g. truly circling) -- a case the
        //    physical check alone would miss. We measure progress ALONG THE PATH
        //    rather than straight-line distance to the goal because a correct
        //    route through multi-level geometry legitimately winds AWAY from the
        //    goal (up a spiralling stair, around a gallery, doubling back), so
        //    straight-line goal-distance can grow for many seconds while we are
        //    in fact making perfect progress -- which used to trip a false
        //    give-up on stairs. mBestDistToGoal is retained only for diagnostics
        //    and closest-approach callouts.
        osg::Vec3f mLastPos;
        float mTimeSinceMove = 0.0f;
        float mBestDistToGoal = std::numeric_limits<float>::max();
        float mBestPathRemaining = std::numeric_limits<float>::max();
        float mTimeSinceProgress = 0.0f;

        // FALL-ARREST state. We keep a short TRAIL of recent grounded positions
        // (each tagged with a walk-clock time) rather than just the last one,
        // because walking DOWN a steep slope toward a pit reads as "on ground"
        // every frame -- so the most recent grounded point is ON the killer slope
        // and snapping to it just drops the player back onto the cliff. On a catch
        // we instead snap to the HIGHEST point in the trail within the last
        // kSafeTrailWindow seconds -- the crest the player crossed safely before the
        // descent. mWalkClock accumulates dt; mPrevZ/mHasPrevZ derive vertical
        // velocity (the engine doesn't hand us fall speed here). All reset in
        // resetProgress.
        std::deque<std::pair<float, osg::Vec3f>> mSafeTrail;
        float mWalkClock = 0.0f;
        float mPrevZ = 0.0f;
        bool mHasPrevZ = false;
        // Seconds the body has been continuously airborne AND descending. Reset to
        // 0 whenever grounded or rising. A real fall sustains this; a single-frame
        // step/ledge jitter (which can spike the per-frame velocity to plunge levels
        // at high framerates) never accumulates enough to arm the catch.
        float mFallTime = 0.0f;

        // HAZARD-ARREST state. Auto-walk can march the player into a damaging
        // surface (lava, a fire field, damaging water). We can't detect the
        // hazard by type -- OpenMW has no lava concept; it's script-applied
        // damage on a water surface -- so we watch health instead. mPrevHealth
        // is last frame's current health; mHazardDamage accumulates health lost
        // while NOT in combat; mHazardGrace counts down since the last such loss
        // and resets the accumulator when it expires (so isolated ticks never
        // arm, but continuous lava damage does). All reset in resetProgress.
        float mPrevHealth = 0.0f;
        bool mHasPrevHealth = false;
        float mHazardDamage = 0.0f;
        float mHazardGrace = 0.0f;

        // Oscillation detection. The physical-wedge check (mTimeSinceMove) only
        // catches us when the BODY stops moving; it misses a "limit cycle" where
        // we move at full speed but in a loop -- e.g. a coarse stair route whose
        // next waypoint flips back and forth across us, so we climb a few steps,
        // get told to go back down, slide down, get told to climb, forever. To
        // catch that we anchor a reference position and watch how long we stay
        // within a small radius of it while still commanding movement: if we
        // never escape that bubble for long enough, we're circling, not
        // progressing, and must recover then honestly give up (a speech-only UI
        // must never leave the player silently walking in a ring). mOscAnchor is
        // re-seeded whenever we travel clear of it.
        osg::Vec3f mOscAnchor;
        float mTimeInOscBubble = 0.0f;

        // Stuck-recovery state. When we stop making progress we enter a short
        // recovery "wiggle" (jump + sidestep) instead of aborting outright; see
        // kRecoveryDuration / kMaxRecoveryAttempts. mRecoveryTimer counts down
        // the current wiggle (0 == not recovering); mRecoveryAttempts counts how
        // many we've tried since last making real progress; mRecoveryDir
        // alternates the sidestep direction (+1 / -1) each attempt.
        float mRecoveryTimer = 0.0f;
        int mRecoveryAttempts = 0;
        float mRecoveryDir = 1.0f;

        // Door back-off state. When auto-walk opens a closed door it is usually
        // wedged flush against it, and the engine REFUSES to swing a door into an
        // actor's body (rotateDoor undoes the rotation every frame it would hit
        // us) -- so the door we just told to open never actually moves. Like a
        // sighted player, we step back to give it room: mDoorBackoffTimer counts
        // down a brief reverse-walk after opening, during which we drive movement
        // backward and suppress stuck/recovery logic (we are deliberately not
        // progressing). 0 == not backing off.
        float mDoorBackoffTimer = 0.0f;

        // True once we've switched from navmesh following to the final
        // straight-line approach at the target (after the navmesh path ran out
        // short). Guards against re-entering final approach repeatedly and
        // against the periodic re-path clobbering the straight line.
        bool mFinalApproach = false;

        // PROGRESSIVE (cross-cell) mode. When the requested target lies beyond
        // the loaded navmesh (only a 3x3 cell grid is ever loaded around the
        // player), we can't path to it directly. Instead we steer toward a
        // "carrot": the farthest walkable navmesh point along the straight line
        // toward the target (DetourNavigator::raycast). As the player advances
        // and new cells stream in, each re-path pushes the carrot further, so
        // we cross open same-worldspace terrain cell by cell until the true
        // target finally comes within the loaded mesh and normal pathing/arrival
        // takes over. In this mode mEffectiveTarget is the (transient) carrot,
        // NOT an arrival proxy, so arrival is judged only against the true
        // target. If the carrot can't advance (a wall/mountain truly blocks the
        // straight bearing), the no-progress backstop stops us with an honest
        // "stopped short" report. Set/cleared each rebuildPath().
        bool mProgressive = false;

        // Periodic progress callouts on long walks. mTimeSinceCallout counts up
        // to kCalloutInterval; mLastCalloutDist is the true-target distance at
        // the previous callout, so we only speak when we've actually gotten
        // closer (never spam a distance while stuck).
        float mTimeSinceCallout = 0.0f;
        float mLastCalloutDist = std::numeric_limits<float>::max();

        // Moving-target (wandering NPC) handling. The no-progress backstop
        // measures our ALL-TIME-closest approach to the goal; against a
        // wandering NPC that's a false-failure trap: we get close once, the NPC
        // strolls off, and now we can never beat that best again, so after
        // kNoProgressTimeout the backstop wrongly reports "Stuck. Cannot reach"
        // while we are in fact correctly chasing. (Confirmed in the Balmora
        // Temple log: both failed walks were wandering NPCs whose distance shot
        // back up after a close approach.) FIX: when the target is an actor that
        // has actually moved from where it stood when the walk began, mark it
        // moving, suppress the no-progress backstop (the physical-wedge check
        // still aborts if we genuinely can't move), and announce once that the
        // target is moving so the player knows why it's taking a while.
        // mTargetStartPos is captured at walk start (set in start(), NOT in
        // resetProgress(), which also runs from cancel() when the target is
        // already cleared).
        osg::Vec3f mTargetStartPos;
        bool mTargetMoving = false;
        bool mAnnouncedTargetMoving = false;

        // Route-hazard warning state (deep water / steep drops). warnRouteHazards()
        // runs on every rebuildPath() so hazards that only come into view as new
        // cells stream in (long progressive walks) are still caught -- but it
        // only SPEAKS on a rising edge (a hazard type newly appearing in the
        // upcoming path), so a route that stays watery doesn't re-warn every
        // second. The flags clear when that hazard leaves the path, so a later,
        // separate hazard of the same type warns again. Seeded true-less on a
        // fresh walk by resetProgress().
        bool mPathHadWater = false;
        bool mPathHadDrop = false;

        // Phase-through-blocker handling. A stationary NPC plugging a doorway
        // can't be wiggled past (actors aren't in the navmesh and physics won't
        // let two bodies share space), so when recovery is exhausted and a forward
        // probe finds a person in the way, we temporarily disable JUST that NPC's
        // collision body so the player slips through, then RESTORE it the instant
        // we're clear. mPhasingActor holds the actor whose collision we disabled
        // (empty when none). CRITICAL SAFETY INVARIANT: this must never persist --
        // restorePhasing() is called on clear-through, on every walk end (cancel),
        // and if the actor/cell goes invalid, so we never leave an NPC permanently
        // non-solid. mPhaseStartPos is where the player stood when phasing began;
        // we restore collision once we've moved kPhaseClearDistance past it.
        MWWorld::Ptr mPhasingActor;
        osg::Vec3f mPhaseStartPos;

        // Begin phasing through the given blocking actor (disable its collision).
        // No-op if already phasing through someone.
        void beginPhasing(const MWWorld::Ptr& blocker, const osg::Vec3f& playerPos);
        // Restore the phased actor's collision and clear the phasing state. Safe
        // to call when not phasing. MUST be invoked on every walk-exit path.
        void restorePhasing();
    };
}

#endif
