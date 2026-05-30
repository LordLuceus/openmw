#include "autowalker.hpp"

#include <cmath>

#include <components/accessibility/accessibilitymanager.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/flags.hpp>
#include <components/detournavigator/areatype.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/pathfinding.hpp"
#include "../mwmechanics/pathgrid.hpp"

#include "../mwworld/cellref.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"

namespace
{
    // Arrival is measured as horizontal distance to the target. The
    // engine's iMaxActivateDist is ~192 units (within which the player
    // can activate an object), but for "auto-walk arrived" we want to
    // be obviously close -- about an arm's reach. 48 units (~ half a
    // metre in MW scale) means the player is essentially standing on
    // top of the target.
    constexpr float kArrivalDistance = 48.0f;
    constexpr float kWaypointTolerance = 32.0f;
    constexpr float kRepathInterval = 1.0f; // seconds

    // Stuck-detection tuning. Every kStuckSampleInterval we measure how
    // far the player has moved horizontally since the last sample; if
    // the cumulative motion stays below kStuckMoveThreshold for more
    // than kStuckTimeout seconds, we conclude we are wedged against an
    // obstacle and abort the auto-walk.
    constexpr float kStuckSampleInterval = 0.25f; // seconds
    constexpr float kStuckMoveThreshold = 8.0f; // world units per sample
    constexpr float kStuckTimeout = 1.5f; // seconds

    DetourNavigator::Flags playerNavigatorFlags()
    {
        // The player is treated as a walker that can swim and open
        // doors. Including Flag_usePathgrid lets the pathfinder fall
        // back to ESM3 pathgrid nodes in interiors where navmesh
        // coverage may be incomplete (which is exactly the Census
        // Office / Seyda Neen prison ship case).
        return DetourNavigator::Flag_walk | DetourNavigator::Flag_swim
            | DetourNavigator::Flag_openDoor | DetourNavigator::Flag_usePathgrid;
    }

    DetourNavigator::AreaCosts defaultAreaCosts()
    {
        DetourNavigator::AreaCosts costs;
        // Defaults from DetourNavigator are reasonable for the player.
        return costs;
    }

    void speakQueued(const std::string& text)
    {
        Accessibility::AccessibilityManager::instance().speak(
            text, /*interrupt=*/false);
    }
}

namespace MWAccessibility
{
    AutoWalker::AutoWalker() = default;
    AutoWalker::~AutoWalker() = default;

    bool AutoWalker::start(const MWWorld::Ptr& target)
    {
        if (target.isEmpty())
            return false;
        mTarget = target;
        mTargetName = std::string(target.getClass().getName(target));
        mActive = true;
        mTimeSinceRepath = 0.0f;
        mTimeSinceStuckSample = 0.0f;
        mStuckTimer = 0.0f;
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (!player.isEmpty())
            mLastStuckPos = player.getRefData().getPosition().asVec3();
        if (!rebuildPath())
        {
            cancel();
            return false;
        }
        return true;
    }

    void AutoWalker::cancel()
    {
        if (mActive)
        {
            MWWorld::Ptr player
                = MWBase::Environment::get().getWorld()->getPlayerPtr();
            if (!player.isEmpty())
            {
                // Stop driving the player's controls. Player input is
                // now mediated by Lua actor-controls, so write through
                // that interface to avoid being clobbered next frame.
                auto* controls
                    = MWBase::Environment::get().getLuaManager()->getActorControls(player);
                if (controls)
                {
                    controls->mMovement = 0.0f;
                    controls->mSideMovement = 0.0f;
                    controls->mYawChange = 0.0f;
                    controls->mChanged = true;
                }
            }
        }
        mActive = false;
        mTarget = MWWorld::Ptr();
        mTargetName.clear();
        mPathFinder.clearPath();
        mTimeSinceRepath = 0.0f;
    }

    bool AutoWalker::rebuildPath()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty() || mTarget.isEmpty())
            return false;

        osg::Vec3f start = player.getRefData().getPosition().asVec3();
        osg::Vec3f end = mTarget.getRefData().getPosition().asVec3();

        DetourNavigator::AgentBounds bounds = world->getPathfindingAgentBounds(player);
        DetourNavigator::Flags flags = playerNavigatorFlags();
        DetourNavigator::AreaCosts costs = defaultAreaCosts();

        mPathFinder.clearPath();
        // Try navmesh first; if it fails (common in interiors with sparse
        // navmesh coverage), buildPath internally falls back to the cell
        // pathgrid. sEmpty is fine when no pathgrid exists.
        mPathFinder.buildPath(player, start, end,
            MWMechanics::PathgridGraph::sEmpty, bounds, flags, costs,
            /*endTolerance=*/kArrivalDistance,
            MWMechanics::PathType::Full);

        // Last resort: a straight-line path. Won't avoid obstacles but
        // gives the user *something* to aim at -- they'll hear "Cannot
        // reach" only when even straight-line fails.
        if (!mPathFinder.isPathConstructed())
            mPathFinder.buildStraightPath(end);

        return mPathFinder.isPathConstructed();
    }

    void AutoWalker::onFrame(float dt)
    {
        if (!mActive)
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
        {
            cancel();
            return;
        }

        // If the target Ptr has become invalid (deleted, cell unloaded),
        // give up cleanly.
        if (mTarget.isEmpty() || mTarget.getCellRef().getCount() <= 0)
        {
            speakQueued("Lost target.");
            cancel();
            return;
        }

        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        osg::Vec3f targetPos = mTarget.getRefData().getPosition().asVec3();

        // Arrival check on horizontal distance (Z is allowed to differ
        // since players can't fly up to a ceiling hatch -- we treat
        // "directly below it" as arrived).
        float dx = targetPos.x() - playerPos.x();
        float dy = targetPos.y() - playerPos.y();
        float horizDist = std::sqrt(dx * dx + dy * dy);
        if (horizDist <= kArrivalDistance)
        {
            speakQueued("Arrived at " + mTargetName + ".");
            cancel();
            return;
        }

        // Stuck detection: sample the player's horizontal position at a
        // fixed interval; if motion stays below threshold for long
        // enough, abort. This catches the case where the path-builder
        // keeps producing a "valid" path on every re-path interval but
        // we are wedged against an obstacle in practice and not making
        // any real progress toward the target.
        mTimeSinceStuckSample += dt;
        if (mTimeSinceStuckSample >= kStuckSampleInterval)
        {
            float dxStuck = playerPos.x() - mLastStuckPos.x();
            float dyStuck = playerPos.y() - mLastStuckPos.y();
            float moved = std::sqrt(dxStuck * dxStuck + dyStuck * dyStuck);
            if (moved < kStuckMoveThreshold)
                mStuckTimer += mTimeSinceStuckSample;
            else
                mStuckTimer = 0.0f;
            mLastStuckPos = playerPos;
            mTimeSinceStuckSample = 0.0f;

            if (mStuckTimer >= kStuckTimeout)
            {
                speakQueued("Stuck. Cannot reach " + mTargetName + ".");
                cancel();
                return;
            }
        }

        // Periodic re-path so we keep up with moving NPCs and recover
        // from getting nudged off course by physics.
        mTimeSinceRepath += dt;
        if (mTimeSinceRepath >= kRepathInterval || !mPathFinder.isPathConstructed())
        {
            mTimeSinceRepath = 0.0f;
            rebuildPath();
        }

        DetourNavigator::AgentBounds bounds = world->getPathfindingAgentBounds(player);
        DetourNavigator::Flags flags = playerNavigatorFlags();
        MWMechanics::PathFinder::UpdateFlags updateFlags
            = MWMechanics::PathFinder::UpdateFlag_RemoveLoops
            | MWMechanics::PathFinder::UpdateFlag_ShortenIfAlmostStraight;
        mPathFinder.update(playerPos, kWaypointTolerance, kArrivalDistance,
            updateFlags, bounds, flags);

        // If the pathfinder reports the path is complete but the
        // horizontal arrival check above did not fire, it means the
        // remaining gap is purely vertical (e.g. an overhead hatch or
        // a ledge we cannot climb). Treat that as arrived rather than
        // spinning in circles around the last waypoint.
        if (mPathFinder.checkPathCompleted())
        {
            speakQueued("Arrived at " + mTargetName + ".");
            cancel();
            return;
        }

        if (!mPathFinder.isPathConstructed())
        {
            // Path exhausted but we did not hit the arrival threshold;
            // try one more rebuild before declaring failure.
            if (!rebuildPath())
            {
                speakQueued("Cannot reach " + mTargetName + ".");
                cancel();
                return;
            }
        }

        // Steer toward the next waypoint. Yaw is fed in as a delta to
        // mYawChange because controls.mYawChange is what the player-input
        // Lua script applies each frame; writing to MovementSettings
        // directly would be clobbered when that script next runs.
        float desiredYaw
            = mPathFinder.getZAngleToNext(playerPos.x(), playerPos.y());
        float currentYaw = player.getRefData().getPosition().rot[2];
        float yawDelta = desiredYaw - currentYaw;
        // Wrap to [-PI, PI].
        constexpr float kPi = 3.14159265358979323846f;
        while (yawDelta > kPi)
            yawDelta -= 2.0f * kPi;
        while (yawDelta < -kPi)
            yawDelta += 2.0f * kPi;

        // Apply only a fraction per frame so we don't snap.
        const float kTurnSpeed = 6.0f; // rad/s
        float maxTurn = kTurnSpeed * dt;
        if (yawDelta > maxTurn)
            yawDelta = maxTurn;
        else if (yawDelta < -maxTurn)
            yawDelta = -maxTurn;

        auto* controls
            = MWBase::Environment::get().getLuaManager()->getActorControls(player);
        if (!controls)
        {
            cancel();
            return;
        }
        controls->mMovement = 1.0f; // full forward
        controls->mSideMovement = 0.0f;
        controls->mYawChange = yawDelta;
        controls->mPitchChange = 0.0f;
        controls->mJump = false;
        controls->mRun = true; // auto-walk uses running speed
        controls->mChanged = true;
    }
}
