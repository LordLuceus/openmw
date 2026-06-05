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
        // Progress tracking drives stuck-detection. We measure progress toward
        // the GOAL (smallest horizontal distance to target achieved so far),
        // not raw movement -- otherwise the recovery wiggle (jump + strafe)
        // counts as "movement" and resets the timer, producing an endless
        // hop-in-place loop. mBestDistToGoal is the closest we've gotten;
        // mTimeSinceProgress accumulates while we fail to beat it.
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
    };
}

#endif
