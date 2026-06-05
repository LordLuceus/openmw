#include "autowalker.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

#include <components/accessibility/accessibilitymanager.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/flags.hpp>
#include <components/detournavigator/areatype.hpp>
#include <components/detournavigator/navigatorutils.hpp>

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

    // How far from a requested destination we'll look for a walkable navmesh
    // point. Many useful targets (doors, levers, wall-mounted activators, items
    // on tables) sit slightly off the navmesh -- flush against a wall or up on
    // furniture -- so pathing to their exact position fails and we fall back to
    // a straight line that marches into geometry. Snapping the destination to
    // the nearest walkable point first (within this radius) gives the
    // pathfinder a reachable goal right in front of the target. ~3 metres is
    // generous enough to bridge a doorway/wall gap without grabbing a point in
    // a different room.
    constexpr float kNavMeshSnapRadius = 210.0f;

    // Stuck-detection tuning. If we fail to get measurably closer to the goal
    // for more than kStuckTimeout seconds, we conclude we're wedged and begin
    // recovery (and eventually abort). We measure progress toward the goal, not
    // raw movement -- see the detailed note at the stuck-detection site.
    constexpr float kStuckTimeout = 1.5f; // seconds

    // Stuck-recovery tuning. Rather than abort the moment we detect we're
    // wedged, we try a few short "wiggle" manoeuvres first: jump while
    // sidestepping (alternating left/right each attempt), which frees the
    // collision capsule from the small lips, door frames, and stair edges that
    // cause most snags. Only after kMaxRecoveryAttempts fail do we give up.
    constexpr float kRecoveryDuration = 0.6f; // seconds per wiggle
    constexpr int kMaxRecoveryAttempts = 3;

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
        mHasPtrTarget = true;
        mTargetName = std::string(target.getClass().getName(target));
        mActive = true;
        resetProgress();
        if (!rebuildPath())
        {
            cancel();
            return false;
        }
        return true;
    }

    bool AutoWalker::start(const osg::Vec3f& target, const std::string& name)
    {
        mTarget = MWWorld::Ptr();
        mTargetPos = target;
        mHasPtrTarget = false;
        mTargetName = name;
        mActive = true;
        resetProgress();
        if (!rebuildPath())
        {
            cancel();
            return false;
        }
        return true;
    }

    osg::Vec3f AutoWalker::targetPosition() const
    {
        if (mHasPtrTarget)
            return mTarget.getRefData().getPosition().asVec3();
        return mTargetPos;
    }

    void AutoWalker::faceTarget(const MWWorld::Ptr& player, const osg::Vec3f& targetPos)
    {
        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        const osg::Vec3f delta = targetPos - playerPos;
        const float horiz = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
        // Yaw 0 = +Y = north, increasing toward +X = east (engine convention);
        // pitch tilts toward the target's height so it's centred, not just
        // level. Matches the scanner's "face" behaviour.
        const float yaw = std::atan2(delta.x(), delta.y());
        const float pitch = -std::atan2(delta.z(), horiz);
        MWBase::Environment::get().getWorld()->rotateObject(
            player, osg::Vec3f(pitch, 0.0f, yaw), MWBase::RotationFlag_none);
    }

    void AutoWalker::announceStoppedShort(const MWWorld::Ptr& player, const osg::Vec3f& targetPos, float trueDist)
    {
        // Line the player up with the target so it's dead ahead, then report
        // honestly. We suggest the beacon because for genuinely unreachable
        // spots (a door up stairs the navmesh doesn't model) the audio cue is
        // the practical way to find the real approach by ear.
        faceTarget(player, targetPos);
        const float metres = trueDist / 69.99f;
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "Stopped %.0f metres short of %s, now ahead of you. Use the beacon to find a route.",
            metres, mTargetName.c_str());
        speakQueued(buf);
    }

    void AutoWalker::resetProgress()
    {
        mFinalApproach = false;
        mTimeSinceRepath = 0.0f;
        mBestDistToGoal = std::numeric_limits<float>::max();
        mTimeSinceProgress = 0.0f;
        mRecoveryTimer = 0.0f;
        mRecoveryAttempts = 0;
        mRecoveryDir = 1.0f;
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
        mHasPtrTarget = true;
        mTargetName.clear();
        mPathFinder.clearPath();
        resetProgress();
    }

    bool AutoWalker::rebuildPath()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty() || (mHasPtrTarget && mTarget.isEmpty()))
            return false;

        osg::Vec3f start = player.getRefData().getPosition().asVec3();
        const osg::Vec3f rawEnd = targetPosition();

        DetourNavigator::AgentBounds bounds = world->getPathfindingAgentBounds(player);
        DetourNavigator::Flags flags = playerNavigatorFlags();
        DetourNavigator::AreaCosts costs = defaultAreaCosts();

        // Snap the destination to the nearest walkable navmesh point. Many
        // targets (doors flush against walls, levers, items on tables) sit just
        // off the navmesh, so pathing to their exact position fails and we'd
        // fall back to a straight line into the geometry. Snapping gives the
        // pathfinder a reachable goal right in front of the target. If nothing
        // walkable is found nearby (e.g. interiors with no navmesh, where we
        // rely on the pathgrid), keep the raw position.
        osg::Vec3f end = rawEnd;
        bool snappedOk = false;
        if (DetourNavigator::Navigator* navigator = world->getNavigator())
        {
            const osg::Vec3f searchHalfExtents(kNavMeshSnapRadius, kNavMeshSnapRadius, kNavMeshSnapRadius);
            const std::optional<osg::Vec3f> snapped = DetourNavigator::findNearestNavMeshPosition(
                *navigator, bounds, rawEnd, searchHalfExtents, flags);
            if (snapped.has_value())
            {
                end = *snapped;
                snappedOk = true;
            }
        }
        mEffectiveTarget = end;

        mPathFinder.clearPath();
        // Build the navmesh path with PathType::Partial. This is crucial for
        // targets that aren't fully reachable -- e.g. a door up on a raised
        // terrace, where Detour can route you up the stairs to the building but
        // not onto the door's exact (often off-mesh, elevated) position. With
        // PathType::Full the engine DISCARDS such a partial route entirely and
        // we fall back to a straight line that just walks the player into the
        // wall below the target. Partial accepts the route as far as the
        // navmesh reaches; we then close the final gap via the arrival /
        // vertical-gap checks in onFrame. buildPath still falls back to the
        // pathgrid, then to a straight line, if no navmesh path exists at all.
        mPathFinder.buildPath(player, start, end,
            MWMechanics::PathgridGraph::sEmpty, bounds, flags, costs,
            /*endTolerance=*/kArrivalDistance,
            MWMechanics::PathType::Partial);

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

        // If a Ptr target has become invalid (deleted, cell unloaded), give up
        // cleanly. Position targets (waypoints) never "go away", so skip this.
        if (mHasPtrTarget && (mTarget.isEmpty() || mTarget.getCellRef().getCount() <= 0))
        {
            speakQueued("Lost target.");
            cancel();
            return;
        }

        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        osg::Vec3f targetPos = targetPosition();

        // Arrival check on horizontal distance (Z is allowed to differ
        // since players can't fly up to a ceiling hatch -- we treat
        // "directly below it" as arrived). We accept arrival at EITHER the true
        // target or the snapped navmesh goal (mEffectiveTarget), whichever the
        // player reaches first: when a door is embedded in a wall the true
        // position is unreachable, so "arrived" really means "standing on the
        // walkable spot in front of it".
        auto horizDistTo = [&playerPos](const osg::Vec3f& p) {
            const float dx = p.x() - playerPos.x();
            const float dy = p.y() - playerPos.y();
            return std::sqrt(dx * dx + dy * dy);
        };
        const float horizDist = std::min(horizDistTo(targetPos), horizDistTo(mEffectiveTarget));
        if (horizDist <= kArrivalDistance)
        {
            speakQueued("Arrived at " + mTargetName + ".");
            cancel();
            return;
        }

        // Stuck detection based on PROGRESS TOWARD THE GOAL, not raw movement.
        // We track the closest horizontal distance to the target we've ever
        // achieved (mBestDistToGoal). Each frame that we beat it (by a small
        // margin) we reset the timer and the recovery attempts; otherwise the
        // timer accumulates. This is essential because the recovery wiggle
        // (jump + strafe) IS movement -- a raw-movement check would see the
        // hopping as "not stuck" and loop forever (the endless-acrobatics bug).
        constexpr float kProgressEpsilon = 8.0f; // units of improvement that "counts"
        if (horizDist < mBestDistToGoal - kProgressEpsilon)
        {
            mBestDistToGoal = horizDist;
            mTimeSinceProgress = 0.0f;
            mRecoveryAttempts = 0; // genuine progress: fresh set of wiggles next snag
        }
        else
        {
            mTimeSinceProgress += dt;
        }

        // No progress for long enough: try a recovery wiggle, then (after a
        // capped number of failed wiggles) give up. Don't start a new wiggle
        // while one is already running.
        if (mTimeSinceProgress >= kStuckTimeout && mRecoveryTimer <= 0.0f)
        {
            if (mRecoveryAttempts >= kMaxRecoveryAttempts)
            {
                // If we wedged during the final straight-line approach, the
                // target is genuinely unreachable on foot (e.g. up a level the
                // navmesh doesn't model): face it and give the honest
                // stopped-short report rather than a bare "stuck".
                if (mFinalApproach)
                {
                    announceStoppedShort(player, targetPos, horizDistTo(targetPos));
                }
                else
                {
                    speakQueued("Stuck. Cannot reach " + mTargetName + ".");
                }
                cancel();
                return;
            }
            ++mRecoveryAttempts;
            mRecoveryTimer = kRecoveryDuration;
            mRecoveryDir = -mRecoveryDir; // alternate sidestep each attempt
            mTimeSinceProgress = 0.0f; // give the wiggle a chance before re-counting
            // Re-path too: the snag may have shifted us enough that a new
            // route opens up.
            rebuildPath();
        }

        // Recovery wiggle in progress: jump while sidestepping to pop the
        // collision capsule free of whatever it's caught on, still creeping
        // forward toward the goal. We drive the controls here and return early
        // so the normal steering below doesn't overwrite them. NOTE: the
        // progress timer keeps running during the wiggle, so a wiggle that
        // doesn't actually get us closer still counts toward the next attempt /
        // eventual give-up -- this is what prevents the infinite hop loop.
        if (mRecoveryTimer > 0.0f)
        {
            mRecoveryTimer -= dt;
            auto* controls
                = MWBase::Environment::get().getLuaManager()->getActorControls(player);
            if (!controls)
            {
                cancel();
                return;
            }
            controls->mMovement = 0.5f; // ease forward, don't ram back in
            controls->mSideMovement = mRecoveryDir; // strafe to clear the edge
            controls->mYawChange = 0.0f;
            controls->mPitchChange = 0.0f;
            controls->mJump = true; // hop over the lip / step
            controls->mRun = true;
            controls->mChanged = true;
            return;
        }

        // Periodic re-path so we keep up with moving NPCs and recover
        // from getting nudged off course by physics. Skipped during the final
        // straight-line approach: rebuildPath() would replace our deliberate
        // straight line with another short navmesh path and we'd never close
        // the gap.
        if (!mFinalApproach)
        {
            mTimeSinceRepath += dt;
            if (mTimeSinceRepath >= kRepathInterval || !mPathFinder.isPathConstructed())
            {
                mTimeSinceRepath = 0.0f;
                rebuildPath();
            }
        }

        DetourNavigator::AgentBounds bounds = world->getPathfindingAgentBounds(player);
        DetourNavigator::Flags flags = playerNavigatorFlags();
        MWMechanics::PathFinder::UpdateFlags updateFlags
            = MWMechanics::PathFinder::UpdateFlag_RemoveLoops
            | MWMechanics::PathFinder::UpdateFlag_ShortenIfAlmostStraight;
        mPathFinder.update(playerPos, kWaypointTolerance, kArrivalDistance,
            updateFlags, bounds, flags);

        // The pathfinder reports the path is complete but the tight arrival
        // check above did not fire. This happens in two distinct cases:
        //   1. The remaining gap is essentially vertical (an overhead hatch or
        //      a ledge we can't climb) -- we're horizontally on top of it, so
        //      that's a genuine arrival.
        //   2. We followed a PARTIAL path that simply ran out at the navmesh
        //      boundary, several metres short of the target (e.g. a door up on
        //      a terrace the navmesh doesn't quite reach). Announcing "arrived"
        //      here would be a lie -- the player then can't interact because
        //      the door is still out of activation range.
        // Distinguish them by the true horizontal distance to the target: if
        // we're within interaction range it's a real arrival; otherwise we've
        // gotten as close as the navmesh allows, so face the target and report
        // the honest remaining distance instead of a false arrival.
        if (mPathFinder.checkPathCompleted())
        {
            const float reachDist = world->getMaxActivationDistance();
            const float trueDist = horizDistTo(targetPos);
            if (trueDist <= reachDist)
            {
                speakQueued("Arrived at " + mTargetName + ".");
                cancel();
                return;
            }

            // The navmesh route ran out short of the target. Before giving up,
            // try a FINAL STRAIGHT-LINE APPROACH straight at the target: many
            // targets sit just off the navmesh on an otherwise walkable floor
            // (doors flush in a wall, items on the ground past the mesh edge),
            // and a short straight walk closes that gap. If the target is
            // genuinely unreachable (e.g. up on a level the navmesh doesn't
            // model), this approach will wedge and stuck-detection will trigger
            // the honest "couldn't reach" handling below. We only enter final
            // approach once per walk (mFinalApproach guards re-entry) and only
            // when the gap is small enough to be plausibly walkable.
            constexpr float kMaxFinalApproach = 700.0f; // ~10 m
            if (!mFinalApproach && trueDist <= kMaxFinalApproach)
            {
                mFinalApproach = true;
                mPathFinder.clearPath();
                mPathFinder.buildStraightPath(targetPos);
                // Fresh progress budget for the straight-line leg.
                mBestDistToGoal = std::numeric_limits<float>::max();
                mTimeSinceProgress = 0.0f;
                mRecoveryAttempts = 0;
                // Fall through to steering this frame.
            }
            else
            {
                announceStoppedShort(player, targetPos, trueDist);
                cancel();
                return;
            }
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
