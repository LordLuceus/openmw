#ifndef GAME_MWACCESSIBILITY_AUTOWALKER_H
#define GAME_MWACCESSIBILITY_AUTOWALKER_H

#include <limits>

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

    private:
        bool rebuildPath();
        // Reset all progress / stuck / recovery state for a fresh walk.
        void resetProgress();
        // Rotate \p player to face \p targetPos horizontally, so the target is
        // lined up dead ahead. Used when auto-walk stops short of an
        // unreachable target so the player can close the last gap manually.
        void faceTarget(const MWWorld::Ptr& player, const osg::Vec3f& targetPos);
        // Face the target and announce that we stopped short of it (with the
        // remaining distance), suggesting the audio beacon to find the route.
        void announceStoppedShort(const MWWorld::Ptr& player, const osg::Vec3f& targetPos, float trueDist);

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
        MWMechanics::PathFinder mPathFinder;
        float mTimeSinceRepath = 0.0f;

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
        // 2. GOAL progress (mBestDistToGoal / mTimeSinceProgress): the closest
        //    horizontal distance to the target we've achieved, and how long
        //    we've failed to beat it. This resets the recovery-attempt counter
        //    on genuine progress and acts as a long-timeout backstop that gives
        //    up if we're moving but never actually getting closer (e.g. circling
        //    / path oscillation) -- a case the physical check alone would miss.
        osg::Vec3f mLastPos;
        float mTimeSinceMove = 0.0f;
        float mBestDistToGoal = std::numeric_limits<float>::max();
        float mTimeSinceProgress = 0.0f;

        // Stuck-recovery state. When we stop making progress we enter a short
        // recovery "wiggle" (jump + sidestep) instead of aborting outright; see
        // kRecoveryDuration / kMaxRecoveryAttempts. mRecoveryTimer counts down
        // the current wiggle (0 == not recovering); mRecoveryAttempts counts how
        // many we've tried since last making real progress; mRecoveryDir
        // alternates the sidestep direction (+1 / -1) each attempt.
        float mRecoveryTimer = 0.0f;
        int mRecoveryAttempts = 0;
        float mRecoveryDir = 1.0f;

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
    };
}

#endif
