#ifndef GAME_MWACCESSIBILITY_AUTOWALKER_H
#define GAME_MWACCESSIBILITY_AUTOWALKER_H

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

        bool mActive = false;
        MWWorld::Ptr mTarget;
        std::string mTargetName;
        MWMechanics::PathFinder mPathFinder;
        float mTimeSinceRepath = 0.0f;

        // Stuck detection: if the player's horizontal position hasn't
        // moved meaningfully within mStuckWindow seconds, declare we
        // have arrived (or are unreachable, depending on distance to
        // target).
        osg::Vec3f mLastStuckPos;
        float mTimeSinceStuckSample = 0.0f;
        float mStuckTimer = 0.0f;
    };
}

#endif
