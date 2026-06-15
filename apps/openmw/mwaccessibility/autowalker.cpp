#include "autowalker.hpp"

#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <components/accessibility/accessibilitymanager.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/flags.hpp>
#include <components/detournavigator/areatype.hpp>
#include <components/detournavigator/navigatorutils.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loaddoor.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/pathfinding.hpp"
#include "../mwmechanics/pathgrid.hpp"

#include "../mwphysics/collisiontype.hpp"
#include "../mwphysics/raycasting.hpp"

#include "../mwworld/cellref.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/doorstate.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"
#include "../mwworld/worldmodel.hpp"

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

    // Stuck-detection tuning.
    //
    // kStuckTimeout: how long the body may stay physically wedged (commanding
    // forward motion but not actually moving) before we trigger a recovery
    // wiggle. This is a PHYSICAL-movement check, so a normal navmesh detour
    // around a wall -- which still moves the body -- never trips it.
    //
    // kNoProgressTimeout: a much longer backstop on goal progress. If we're
    // moving but never actually getting closer to the target (circling, path
    // oscillation, a moving NPC we can't catch), give up after this. It's
    // generous so that legitimate detours, which can spend several seconds not
    // reducing goal distance, are never mistaken for being stuck.
    constexpr float kStuckTimeout = 1.0f; // seconds wedged in place
    constexpr float kNoProgressTimeout = 10.0f; // seconds without nearing goal

    // How far a target actor must have moved from where it stood when the walk
    // began before we treat it as a "moving target" (a wandering NPC). Past this
    // we suppress the no-progress backstop -- which measures all-time-closest
    // approach and would otherwise falsely give up the moment the NPC wanders
    // away after we'd gotten close -- and announce once that the target is
    // moving. ~150u (~2 m) is well beyond idle-animation jitter but trips as
    // soon as an NPC actually walks.
    constexpr float kTargetMovedThreshold = 150.0f;

    // Below this per-frame horizontal speed (units/sec) we consider the body
    // "not moving". Running speed is many hundreds of units/sec, so this only
    // catches a genuine wedge, not slow turning. ~30 u/s is well under a walk.
    constexpr float kMinMoveSpeed = 30.0f;

    // Stuck-recovery tuning. Rather than abort the moment we detect we're
    // wedged, we try a few short "wiggle" manoeuvres first: jump while
    // sidestepping (alternating left/right each attempt), which frees the
    // collision capsule from the small lips, door frames, and stair edges that
    // cause most snags. Only after kMaxRecoveryAttempts fail do we give up.
    constexpr float kRecoveryDuration = 0.6f; // seconds per wiggle
    // Raised from 3: a single NPC blocking a narrow doorway often takes several
    // sidesteps to get around (and the NPC may itself be shuffling), so we give
    // the stronger, direction-aware wiggle more chances before declaring defeat.
    constexpr int kMaxRecoveryAttempts = 6;

    // Recovery-wiggle squeeze tuning. When wedged we now probe both sides and
    // sidestep toward open space (chooseRecoverySide) at full strength rather
    // than the old half-hearted alternating strafe -- this is what gets us
    // around an NPC standing in a chokepoint (actors aren't in the navmesh).
    // kRecoveryProbeLen: how far to the side we raycast to judge "open"; about a
    // body-and-a-half so we detect a wall/NPC right beside us but not distant
    // scenery. kRecoverySideStrength: full-strength strafe (was 1.0 nominal but
    // paired with forward 0.5; we now bias harder sideways to clear the body).
    constexpr float kRecoveryProbeLen = 120.0f;
    constexpr float kRecoverySideStrength = 1.0f;

    // How far ahead to probe for an actor blocking the path once recovery has
    // failed. Short -- we only care about someone right in our face (a doorway
    // plug), not an NPC across the room. ~100u is about one body-depth ahead.
    constexpr float kBlockerProbeLen = 100.0f;

    // How far ahead to probe for a closed door blocking the path. A touch longer
    // than the actor probe: the player wedges against the door's flat collision
    // plane with the capsule's radius between body centre and the surface, so we
    // need a little extra reach to register the hit. ~150u (~2 m).
    constexpr float kDoorProbeLen = 150.0f;

    // Once we phase through a blocking NPC (temporarily disabling its collision),
    // restore that collision as soon as the player has moved this far from where
    // phasing began -- i.e. we've cleared the doorway and are past the body.
    // ~150u (~2 m) is comfortably more than a body-depth, so we don't re-collide
    // with the NPC the moment its collision comes back.
    constexpr float kPhaseClearDistance = 150.0f;

    // Progressive (cross-cell) mode. A target farther than this from the player
    // is assumed to be off the loaded navmesh, so we use the carrot approach
    // (raycast toward it) rather than expecting a direct path. Roughly one cell
    // (8192 units); within that the normal navmesh snap/path handles it.
    constexpr float kProgressiveDistance = 7000.0f;

    // How often (seconds) to announce remaining distance on a long progressive
    // walk, and the minimum improvement that makes a callout worth speaking.
    constexpr float kCalloutInterval = 25.0f;
    constexpr float kCalloutMinProgress = 200.0f;

    // Route-hazard scanning (warn-and-continue). We sample the planned path's
    // waypoints and flag two danger classes the auto-walk would otherwise march
    // the player into without warning.
    //
    // kWaterDepthWarn: how far a path point must sit BELOW the local water
    // surface before we call it a real "deep water" crossing worth warning
    // about, rather than harmless ankle/knee-deep wading. Morrowind units are
    // ~70/metre; ~90u puts the water well above the waist (near the swim point),
    // i.e. you'd actually be swimming and at drowning/slaughterfish risk.
    constexpr float kWaterDepthWarn = 90.0f;
    // kDropWarn: a descent (in units) between consecutive path points big enough
    // to be a dangerous fall rather than a step/slope. ~350u (~5 m) is well past
    // a safe step-down and into fall-damage territory. Normal navmesh routes
    // don't include drops like this; this mainly catches the straight-line and
    // progressive "carrot" fallbacks, which bee-line regardless of cliffs --
    // exactly where auto-walk fall deaths come from.
    constexpr float kDropWarn = 350.0f;

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
        // Capture where the target stood at walk start, so onFrame can tell if
        // it's a wandering actor that has since moved (see mTargetMoving).
        mTargetStartPos = targetPosition();
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
        // A fixed waypoint never moves; record it anyway for symmetry.
        mTargetStartPos = target;
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

    float AutoWalker::chooseRecoverySide(const MWWorld::Ptr& player, const osg::Vec3f& playerPos, float yaw,
        float fallbackDir) const
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWPhysics::RayCastingInterface* rayCasting = world ? world->getRayCasting() : nullptr;
        if (!rayCasting)
            return fallbackDir;

        // Cast a short ray to the player's left and right, from roughly chest
        // height, to see which side has more open space to sidestep into.
        // Engine yaw: 0 = +Y (north), increasing toward +X (east). The forward
        // unit is (sin yaw, cos yaw); the rightward unit is (cos yaw, -sin yaw).
        const osg::Vec3f right(std::cos(yaw), -std::sin(yaw), 0.0f);
        const osg::Vec3f chest = playerPos + osg::Vec3f(0.0f, 0.0f, 32.0f);

        // We hit-test against everything physical EXCEPT the player; an NPC
        // (CollisionType_Actor) blocking one side counts as "blocked" just like
        // a wall, which is exactly the doorway case. A clear ray (no hit) means
        // that side is open.
        const int mask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_Door
            | MWPhysics::CollisionType_Actor | MWPhysics::CollisionType_HeightMap;
        const std::vector<MWWorld::ConstPtr> ignore{ player };

        auto sideClearance = [&](const osg::Vec3f& dir) -> float {
            const osg::Vec3f to = chest + dir * kRecoveryProbeLen;
            const MWPhysics::RayCastingResult res = rayCasting->castRay(chest, to, ignore, {}, mask);
            if (!res.mHit)
                return kRecoveryProbeLen; // fully open
            return (res.mHitPos - chest).length();
        };

        const float rightClear = sideClearance(right);
        const float leftClear = sideClearance(-right);

        // Prefer the noticeably more open side; if they're close, keep the
        // caller's alternating fallback so we still explore both over attempts.
        constexpr float kMeaningfulDiff = 24.0f;
        if (rightClear > leftClear + kMeaningfulDiff)
            return 1.0f;
        if (leftClear > rightClear + kMeaningfulDiff)
            return -1.0f;
        return fallbackDir;
    }

    MWWorld::Ptr AutoWalker::detectBlockingActor(
        const MWWorld::Ptr& player, const osg::Vec3f& playerPos, float yaw) const
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWPhysics::RayCastingInterface* rayCasting = world ? world->getRayCasting() : nullptr;
        if (!rayCasting)
            return MWWorld::Ptr();

        // Cast straight ahead from chest height. Forward unit for engine yaw
        // (0 = +Y north, increasing toward +X east) is (sin yaw, cos yaw).
        const osg::Vec3f forward(std::sin(yaw), std::cos(yaw), 0.0f);
        const osg::Vec3f chest = playerPos + osg::Vec3f(0.0f, 0.0f, 32.0f);
        const osg::Vec3f to = chest + forward * kBlockerProbeLen;

        // Hit-test ONLY actors here: we specifically want to know whether a
        // person is the blocker (vs a wall we genuinely can't pass), since only
        // then is "ask them to move" the right message.
        const std::vector<MWWorld::ConstPtr> ignore{ player };
        const MWPhysics::RayCastingResult res
            = rayCasting->castRay(chest, to, ignore, {}, MWPhysics::CollisionType_Actor);
        if (res.mHit && !res.mHitObject.isEmpty() && res.mHitObject.getClass().isActor())
            return res.mHitObject;
        return MWWorld::Ptr();
    }

    bool AutoWalker::tryOpenBlockingDoor(
        const MWWorld::Ptr& player, const osg::Vec3f& playerPos, float yaw)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWPhysics::RayCastingInterface* rayCasting = world ? world->getRayCasting() : nullptr;
        if (!rayCasting)
            return false;

        // Cast straight ahead from chest height, hit-testing ONLY doors.
        const osg::Vec3f forward(std::sin(yaw), std::cos(yaw), 0.0f);
        const osg::Vec3f chest = playerPos + osg::Vec3f(0.0f, 0.0f, 32.0f);
        const osg::Vec3f to = chest + forward * kDoorProbeLen;
        const std::vector<MWWorld::ConstPtr> ignore{ player };
        const MWPhysics::RayCastingResult res
            = rayCasting->castRay(chest, to, ignore, {}, MWPhysics::CollisionType_Door);
        if (!res.mHit || res.mHitObject.isEmpty())
            return false;

        MWWorld::Ptr door = res.mHitObject;
        if (door.getType() != ESM::Door::sRecordId)
            return false;

        // Only in-cell doors: a teleport door would yank the player into another
        // cell they didn't choose to enter. Locked doors stay shut (we can't
        // pick them here). Both cases fall through to the normal give-up report.
        if (door.getCellRef().getTeleport() || door.getCellRef().isLocked())
            return false;

        // Already open / opening? Then it isn't what's blocking us -- don't
        // toggle it shut. A closed idle door has its current rotation equal to
        // its authored (cellref) rotation; any divergence means it's swung open.
        if (door.getClass().getDoorState(door) != MWWorld::DoorState::Idle)
            return false;
        const float doorRot
            = door.getRefData().getPosition().rot[2] - door.getCellRef().getPosition().rot[2];
        if (doorRot != 0.0f)
            return false; // open and idle: not our blocker

        // Open it (engine animates + plays sound), announce, and keep walking.
        world->activateDoor(door, MWWorld::DoorState::Opening);
        const std::string doorName = std::string(door.getClass().getName(door));
        speakQueued((doorName.empty() ? std::string("A door") : doorName) + ". Opening.");
        return true;
    }

    bool AutoWalker::handleGiveUp(
        const MWWorld::Ptr& player, const osg::Vec3f& playerPos, const osg::Vec3f& targetPos)
    {
        // Probe straight ahead: is a person plugging the way? (A stationary NPC
        // in a doorway -- actors aren't in the navmesh and physics won't let two
        // bodies share space, so wiggling can never pass.)
        const float yaw = player.getRefData().getPosition().rot[2];
        const MWWorld::Ptr blocker = detectBlockingActor(player, playerPos, yaw);

        // A blocker is in the way and we're not already phasing through someone:
        // temporarily disable JUST that NPC's collision so the player slips past,
        // announce it, refresh the progress/recovery budget, and KEEP WALKING
        // (return false = don't cancel). Collision is restored once we've cleared
        // them (onFrame) or on any walk end (cancel -> restorePhasing).
        if (!blocker.isEmpty() && mPhasingActor.isEmpty())
        {
            const std::string blockerName = std::string(blocker.getClass().getName(blocker));
            beginPhasing(blocker, playerPos);
            speakQueued((blockerName.empty() ? std::string("Someone") : blockerName)
                + " is blocking the way. Moving past.");
            // Fresh budget so the timers that just expired don't instantly fire
            // again before we've had a chance to walk through the now-open gap.
            mBestDistToGoal = std::numeric_limits<float>::max();
            mTimeSinceProgress = 0.0f;
            mTimeSinceMove = 0.0f;
            mRecoveryAttempts = 0;
            mRecoveryTimer = 0.0f;
            return false; // keep walking
        }

        // Either there's no person blocking (genuine geometry/unreachable) or we
        // ALREADY phased through someone and are still stuck -- give up honestly.
        if (!blocker.isEmpty())
        {
            // Phasing didn't get us through (e.g. a second blocker, or a true
            // dead-end past the first). Name them and stop.
            const std::string blockerName = std::string(blocker.getClass().getName(blocker));
            speakQueued((blockerName.empty() ? std::string("Something") : blockerName)
                + " is blocking the way to " + mTargetName + ".");
        }
        else if (mFinalApproach || mProgressive)
        {
            // Stopped short on a final/progressive approach: target not reachable
            // on foot from here -- give the honest beacon hint.
            const float dx = targetPos.x() - playerPos.x();
            const float dy = targetPos.y() - playerPos.y();
            announceStoppedShort(player, targetPos, std::sqrt(dx * dx + dy * dy));
        }
        else
        {
            speakQueued("Stuck. Cannot reach " + mTargetName + ".");
        }
        return true; // cancel the walk
    }

    void AutoWalker::beginPhasing(const MWWorld::Ptr& blocker, const osg::Vec3f& playerPos)
    {
        if (!mPhasingActor.isEmpty() || blocker.isEmpty())
            return;
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world)
            return;
        world->enableActorCollision(blocker, false);
        mPhasingActor = blocker;
        mPhaseStartPos = playerPos;
    }

    void AutoWalker::restorePhasing()
    {
        if (mPhasingActor.isEmpty())
            return;
        MWBase::World* world = MWBase::Environment::get().getWorld();
        // Only re-enable if the actor is still valid (alive, cell loaded);
        // re-enabling a stale Ptr would be a no-op at best. We clear our handle
        // either way so we never try to touch it again.
        if (world && !mPhasingActor.isEmpty() && mPhasingActor.getCellRef().getCount() > 0)
            world->enableActorCollision(mPhasingActor, true);
        mPhasingActor = MWWorld::Ptr();
    }

    void AutoWalker::resetProgress()
    {
        mFinalApproach = false;
        mTimeSinceRepath = 0.0f;
        mBestDistToGoal = std::numeric_limits<float>::max();
        mTimeSinceProgress = 0.0f;
        // Seed mLastPos from the player so the first frame's speed reading is
        // sane (otherwise a zero-init mLastPos yields a huge bogus displacement).
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (world)
        {
            MWWorld::Ptr player = world->getPlayerPtr();
            if (!player.isEmpty())
                mLastPos = player.getRefData().getPosition().asVec3();
        }
        mTimeSinceMove = 0.0f;
        mRecoveryTimer = 0.0f;
        mRecoveryAttempts = 0;
        mRecoveryDir = 1.0f;
        mProgressive = false;
        mTimeSinceCallout = 0.0f;
        mLastCalloutDist = std::numeric_limits<float>::max();
        mPathHadWater = false;
        mPathHadDrop = false;
        mTargetMoving = false;
        mAnnouncedTargetMoving = false;
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
        // Safety: never leave a phased-through NPC permanently non-solid. Any
        // walk end routes through cancel(), so restoring here covers every exit.
        restorePhasing();
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
        DetourNavigator::Navigator* navigator = world->getNavigator();

        // PROGRESSIVE (cross-cell) mode. Only a 3x3 cell grid is loaded around
        // the player, so a target farther than ~one cell is off the navmesh and
        // can't be pathed to directly. Rather than blindly bee-lining into
        // whatever scenery lies between (the old straight-line fallback), steer
        // toward a "carrot": the farthest walkable navmesh point along the
        // straight line to the target (raycast). Each re-path advances the
        // carrot as new cells stream in, so we cross open terrain cell by cell.
        // We only do this once we're sure the target really is beyond the mesh
        // (a near target keeps the precise snap-and-path behaviour below).
        const float horizToTarget = std::sqrt(
            (rawEnd.x() - start.x()) * (rawEnd.x() - start.x()) + (rawEnd.y() - start.y()) * (rawEnd.y() - start.y()));
        mProgressive = false;
        if (navigator && horizToTarget > kProgressiveDistance)
        {
            const std::optional<osg::Vec3f> carrot
                = DetourNavigator::raycast(*navigator, bounds, start, rawEnd, flags);
            // Use the carrot only if it actually advances us a worthwhile
            // distance toward the goal; a carrot right on top of the player
            // means a wall is immediately in the way, so let the no-progress
            // backstop handle it via the normal (non-progressive) path below.
            if (carrot.has_value())
            {
                const float carrotHoriz = std::sqrt((carrot->x() - start.x()) * (carrot->x() - start.x())
                    + (carrot->y() - start.y()) * (carrot->y() - start.y()));
                if (carrotHoriz > kArrivalDistance * 2.0f)
                {
                    mProgressive = true;
                    mEffectiveTarget = *carrot;
                    mPathFinder.clearPath();
                    mPathFinder.buildPath(player, start, *carrot, MWMechanics::PathgridGraph::sEmpty, bounds, flags,
                        costs, /*endTolerance=*/kArrivalDistance, MWMechanics::PathType::Partial);
                    if (!mPathFinder.isPathConstructed())
                        mPathFinder.buildStraightPath(*carrot);
                    warnRouteHazards();
                    return mPathFinder.isPathConstructed();
                }
            }
        }

        // Snap the destination to the nearest walkable navmesh point. Many
        // targets (doors flush against walls, levers, items on tables) sit just
        // off the navmesh, so pathing to their exact position fails and we'd
        // fall back to a straight line into the geometry. Snapping gives the
        // pathfinder a reachable goal right in front of the target. If nothing
        // walkable is found nearby (e.g. interiors with no navmesh, where we
        // rely on the pathgrid), keep the raw position.
        osg::Vec3f end = rawEnd;
        bool snappedOk = false;
        if (navigator)
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

        warnRouteHazards();
        return mPathFinder.isPathConstructed();
    }

    void AutoWalker::warnRouteHazards()
    {
        // Only meaningful for the long progressive (cross-cell, carrot-driven)
        // walk. On a normal in-cell navmesh route the warning is useless or
        // misleading: the pathfinder already routes AROUND cliffs (so the drop
        // warning never legitimately fires), and a short water crossing is
        // announced at the same instant the player hears themselves splash in --
        // far too late to act on. The hazards only matter on the bee-line/
        // partial progressive route, which interpolates Z smoothly and can sail
        // the player over a cliff or into open water before the navmesh catches
        // up. mProgressive is set by rebuildPath right before it calls us.
        if (!mProgressive)
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world || !mPathFinder.isPathConstructed())
            return;
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        const std::deque<osg::Vec3f>& path = mPathFinder.getPath();
        if (path.empty())
            return;

        const MWWorld::Cell* playerCell = player.getCell()->getCell();
        const bool exterior = playerCell->isExterior();
        const ESM::RefId worldspace = playerCell->getWorldSpace();

        // Water surface height at a world point. Interiors use the player's
        // current cell (a route on the loaded mesh won't leave it); exteriors
        // resolve the cell the point falls in, since a long route crosses
        // several cells with different water levels (the sea vs an inland pool).
        // nullopt when that cell has no water at all.
        auto waterLevelAt = [&](const osg::Vec3f& p) -> std::optional<float> {
            if (!exterior)
            {
                if (!playerCell->hasWater())
                    return std::nullopt;
                return player.getCell()->getWaterLevel();
            }
            const ESM::ExteriorCellLocation loc
                = ESM::positionToExteriorCellLocation(p.x(), p.y(), worldspace);
            try
            {
                const MWWorld::CellStore& store
                    = MWBase::Environment::get().getWorldModel()->getExterior(loc, /*forceLoad=*/false);
                if (!store.getCell()->hasWater())
                    return std::nullopt;
                return store.getWaterLevel();
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        };

        // Ground (terrain heightmap) height at a world point -- exteriors only;
        // interiors have no heightmap. This is the HONEST reference for both
        // hazards: a swim route's waypoints sit AT the water surface (the
        // navmesh treats water as walkable), so comparing waypoint-Z to the
        // surface shows ~zero depth; the seabed height vs the surface is what
        // actually reveals deep water. Likewise a straight-line/progressive
        // bee-line interpolates Z smoothly and hides the cliff it crosses, while
        // the terrain height beneath that line still drops away.
        auto groundAt = [&](const osg::Vec3f& p) -> std::optional<float> {
            if (!exterior)
                return std::nullopt;
            return world->getTerrainHeightAt(p, worldspace);
        };

        // Build the full point sequence we traverse: the player's current
        // position first, then every path waypoint. Seeding with the player
        // matters because the straight-line fallback stores ONLY the destination
        // (a single point), so without the start there'd be no segment to test
        // -- and that bee-line is the main fall-death vector we're guarding.
        std::vector<osg::Vec3f> points;
        points.reserve(path.size() + 1);
        points.push_back(player.getRefData().getPosition().asVec3());
        for (const osg::Vec3f& wp : path)
            points.push_back(wp);

        // Sample each segment ~150u apart (waypoints can be far apart, with a
        // lake or cliff edge between two of them that point-only testing skips).
        // At each sample compare the terrain beneath against the water surface
        // and against the previous sample's terrain.
        constexpr float kSampleStep = 150.0f;
        // How far the path itself (the deck you'd actually walk) may sit above
        // the water surface before we treat the crossing as a safe bridge/jetty
        // rather than a swim. Without this guard, routing over the Vivec bridges
        // -- deep water far below, but a solid walkway above -- would false-warn.
        constexpr float kBridgeClearance = 60.0f;
        bool water = false;
        bool drop = false;
        float maxDrop = 0.0f;
        std::optional<float> prevGround;
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const osg::Vec3f a = points[i - 1];
            const osg::Vec3f b = points[i];
            const osg::Vec3f seg = b - a;
            const float segLen = seg.length();
            const int steps = std::max(1, static_cast<int>(segLen / kSampleStep));
            for (int s = 1; s <= steps; ++s)
            {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                const osg::Vec3f sample = a + seg * t;
                const std::optional<float> ground = groundAt(sample);

                // Deep water: the surface stands well above the terrain (seabed)
                // here, AND the path we'd walk isn't elevated above that surface
                // on a bridge/jetty. The seabed reference is what catches a swim
                // whose waypoints ride at the surface. Interiors have no terrain,
                // so fall back to the sample Z for the rare interior pool.
                if (const std::optional<float> level = waterLevelAt(sample))
                {
                    const float seabed = ground.value_or(sample.z());
                    const bool deep = (*level - seabed) >= kWaterDepthWarn;
                    const bool onDeck = sample.z() > *level + kBridgeClearance;
                    if (deep && !onDeck)
                        water = true;
                }

                // Drop: the terrain beneath the route falls away sharply between
                // consecutive samples. A real navmesh route follows connected
                // walkable ground and won't sheer-drop; this fires on the
                // bee-line/progressive fallbacks crossing a ledge -- the fall
                // case. Exteriors only (needs the heightmap).
                if (ground && prevGround)
                {
                    const float descent = *prevGround - *ground;
                    if (descent >= kDropWarn)
                    {
                        drop = true;
                        maxDrop = std::max(maxDrop, descent);
                    }
                }
                prevGround = ground;
            }
        }

        // Speak only on a RISING edge per hazard type: warn when a hazard newly
        // appears in the path, stay silent while it persists across re-paths,
        // and re-arm once it clears so a later, separate hazard warns again.
        const bool newWater = water && !mPathHadWater;
        const bool newDrop = drop && !mPathHadDrop;
        mPathHadWater = water;
        mPathHadDrop = drop;

        if (!newWater && !newDrop)
            return;

        // One consolidated, queued line so it doesn't stomp other speech. The
        // suggestion names the relevant escape so the player knows what to cast.
        std::string msg = "Warning: route ";
        if (newWater && newDrop)
        {
            const float metres = maxDrop / 69.99f;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.0f metre", metres);
            msg += "crosses deep water and has a " + std::string(buf)
                + " drop. Consider Water Walking or Levitation.";
        }
        else if (newWater)
        {
            msg += "crosses deep water. Consider Water Walking or Levitation.";
        }
        else
        {
            const float metres = maxDrop / 69.99f;
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                "has a %.0f metre drop. Consider Levitation or Slow Fall.", metres);
            msg = "Warning: route " + std::string(buf);
        }
        speakQueued(msg);
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

        // If we're phasing through a blocking NPC, restore its collision as soon
        // as we've moved clear of where phasing began (we're past the body now),
        // or if the actor went invalid. This is the normal end of a phase; the
        // cancel() path is the safety net for every other exit.
        if (!mPhasingActor.isEmpty())
        {
            const float dx = playerPos.x() - mPhaseStartPos.x();
            const float dy = playerPos.y() - mPhaseStartPos.y();
            const bool movedClear = std::sqrt(dx * dx + dy * dy) >= kPhaseClearDistance;
            if (movedClear || mPhasingActor.getCellRef().getCount() <= 0)
                restorePhasing();
        }

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
        // When the target is another actor (an NPC/creature), we can never
        // reach its CENTRE: its collision capsule plus the player's keep us a
        // body-width apart, so the player wedges ~60-90 units away and the tight
        // 48-unit arrival never fires -- the walk then "sticks" right next to
        // someone we're plainly standing beside. Widen the arrival threshold by
        // both collision radii (plus a small margin) so "arrived" means "as
        // close as two bodies can stand". For non-actor targets (doors, items,
        // waypoints) keep the tight threshold.
        float arrivalDist = kArrivalDistance;
        if (mHasPtrTarget && !mTarget.isEmpty() && mTarget.getClass().isActor())
        {
            const osg::Vec3f playerHalf = world->getHalfExtents(player);
            const osg::Vec3f targetHalf = world->getHalfExtents(mTarget);
            // Horizontal radius = the larger of the capsule's x/y half-extents.
            const float playerR = std::max(playerHalf.x(), playerHalf.y());
            const float targetR = std::max(targetHalf.x(), targetHalf.y());
            constexpr float kBodyMargin = 16.0f; // a little slack on top of the radii
            arrivalDist = std::max(kArrivalDistance, playerR + targetR + kBodyMargin);
        }

        // In progressive (cross-cell) mode mEffectiveTarget is a transient
        // carrot, not the goal, so arrival must be judged against the true
        // target only -- otherwise reaching the carrot would falsely announce
        // "arrived". In normal mode we accept arrival at either the true target
        // or its snapped navmesh proxy (a door embedded in a wall).
        const float horizDist
            = mProgressive ? horizDistTo(targetPos) : std::min(horizDistTo(targetPos), horizDistTo(mEffectiveTarget));
        if (horizDist <= arrivalDist)
        {
            speakQueued("Arrived at " + mTargetName + ".");
            cancel();
            return;
        }

        // Periodic progress callout on a long progressive walk: every so often,
        // if we've actually closed distance since the last callout, announce
        // how far remains so the user can judge progress (and cancel if it's
        // heading the wrong way). Suppressed when not progressing, so it never
        // spams while stuck.
        if (mProgressive)
        {
            mTimeSinceCallout += dt;
            if (mTimeSinceCallout >= kCalloutInterval)
            {
                mTimeSinceCallout = 0.0f;
                if (mLastCalloutDist - horizDist >= kCalloutMinProgress)
                {
                    const float metres = horizDist / 69.99f;
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "%.0f metres to %s.", metres, mTargetName.c_str());
                    speakQueued(buf);
                    mLastCalloutDist = horizDist;
                }
            }
        }

        // --- Moving-target (wandering NPC) detection -------------------------
        //
        // If the target is an actor that has wandered meaningfully from where it
        // stood when the walk began, flag it moving. This suppresses the
        // no-progress backstop below (which would otherwise falsely give up once
        // the NPC strolls away after we'd gotten close -- the Balmora Temple
        // failure). We still rely on the physical-wedge check to abort if we're
        // genuinely stuck. Announce the moving state once so the player knows
        // why the walk is taking a while. Latched true: once an NPC has moved we
        // treat the rest of the chase as a moving-target chase even if it pauses.
        if (mHasPtrTarget && !mTarget.isEmpty() && mTarget.getClass().isActor())
        {
            const float targetDrift = (targetPos - mTargetStartPos).length();
            if (!mTargetMoving && targetDrift >= kTargetMovedThreshold)
                mTargetMoving = true;
        }
        if (mTargetMoving && !mAnnouncedTargetMoving)
        {
            mAnnouncedTargetMoving = true;
            speakQueued(mTargetName + " is moving.");
        }

        // --- Stuck detection -------------------------------------------------
        //
        // Two independent signals (see the header for the full rationale):
        //
        // (a) PHYSICAL movement drives the recovery wiggle. We measure how far
        //     the body actually moved horizontally this frame. While we're
        //     commanding forward motion (i.e. not mid-wiggle) yet the body is
        //     barely moving, we're wedged against geometry -- THAT is when a
        //     jump/strafe is warranted. A normal navmesh detour around a wall
        //     still moves the body at full speed, so it never trips this, which
        //     is what kills the spurious jumping on clean ground.
        //
        // (b) GOAL progress resets the recovery-attempt counter when we get
        //     genuinely closer, and acts as a long backstop to abort if we're
        //     moving but never nearing the target.
        const float moved = horizDistTo(mLastPos); // horizontal displacement this frame
        mLastPos = playerPos;
        const float speed = (dt > 0.0f) ? moved / dt : 0.0f;
        // Only count "not moving" while we're actually trying to walk forward
        // (not during a recovery wiggle, which drives its own movement).
        if (mRecoveryTimer <= 0.0f && speed < kMinMoveSpeed)
            mTimeSinceMove += dt;
        else
            mTimeSinceMove = 0.0f;

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

        // Backstop: moving but never getting closer for a long time -> give up.
        // SUPPRESSED for a moving target: a wandering NPC routinely pushes our
        // distance back up after a close approach, which is not a failure to
        // pathfind -- it's the NPC walking away. We keep chasing (the physical-
        // wedge check still aborts if we genuinely can't move), and only the
        // user cancelling or actually reaching the NPC ends the walk.
        if (!mTargetMoving && mTimeSinceProgress >= kNoProgressTimeout)
        {
            // A blocked doorway routinely trips THIS no-progress path rather than
            // the wedge path below (the stronger wiggle jostles us enough to keep
            // the no-move timer reset), so the blocker handling must live here
            // too. handleGiveUp phases through a blocking NPC and keeps walking
            // (returns false); only cancel if it says the walk is truly over.
            if (handleGiveUp(player, playerPos, targetPos))
            {
                cancel();
                return;
            }
        }

        // Physically wedged for long enough: try a recovery wiggle, then (after
        // a capped number of failed wiggles) give up. Don't start a new wiggle
        // while one is already running.
        if (mTimeSinceMove >= kStuckTimeout && mRecoveryTimer <= 0.0f)
        {
            // First, check whether a closed (in-cell, unlocked) door is what
            // we're wedged against. The navmesh routes THROUGH doors assuming
            // they'll be opened, but auto-walk never actuated them, so a shut
            // door looks exactly like a wall to the wiggle. If we open one, give
            // the walk a fresh budget and let normal steering carry us through
            // the now-swinging doorway -- no wiggle needed.
            const float doorYaw = player.getRefData().getPosition().rot[2];
            if (tryOpenBlockingDoor(player, playerPos, doorYaw))
            {
                mTimeSinceMove = 0.0f;
                mTimeSinceProgress = 0.0f;
                mBestDistToGoal = std::numeric_limits<float>::max();
                mRecoveryAttempts = 0;
                return;
            }

            if (mRecoveryAttempts >= kMaxRecoveryAttempts)
            {
                // handleGiveUp phases through a blocking NPC and keeps walking
                // (returns false); only cancel if the walk is truly over.
                if (handleGiveUp(player, playerPos, targetPos))
                {
                    cancel();
                    return;
                }
                // Phased through a blocker: skip the rest of recovery this frame
                // and let normal steering carry us through the now-open gap.
                return;
            }
            ++mRecoveryAttempts;
            mRecoveryTimer = kRecoveryDuration;
            // Choose the sidestep direction by probing both sides for open
            // space rather than blindly alternating: this is what lets us
            // squeeze around an NPC standing in a chokepoint (actors aren't in
            // the navmesh, so the route runs straight through them). We pass the
            // alternating value as the fallback so that when both sides look
            // equally (un)blocked we still explore left then right over attempts.
            const float playerYaw = player.getRefData().getPosition().rot[2];
            mRecoveryDir = chooseRecoverySide(player, playerPos, playerYaw, -mRecoveryDir);
            mTimeSinceMove = 0.0f; // give the wiggle a chance before re-counting
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
            // Bias HARD sideways toward the open side picked by the probe, with
            // only a little forward, so we slide around a body/edge instead of
            // grinding into it. (Old code rammed forward 0.5 + strafe; that
            // tended to keep us pinned against an NPC in a doorway.)
            controls->mMovement = 0.2f; // minimal forward; the sidestep does the work
            controls->mSideMovement = mRecoveryDir * kRecoverySideStrength;
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
            // Progressive mode: completing the path just means we reached the
            // current carrot, not the goal. Push the carrot further (a re-path
            // will raycast again from our new position, and by now more cells
            // have streamed in). If the target has finally come within reach,
            // rebuildPath() will drop out of progressive mode on its own and the
            // arrival checks above will fire next frame. Only give up if even a
            // fresh path can't be built at all.
            if (mProgressive)
            {
                mTimeSinceRepath = 0.0f;
                if (!rebuildPath())
                {
                    announceStoppedShort(player, targetPos, horizDistTo(targetPos));
                    cancel();
                    return;
                }
                return;
            }

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
                mTimeSinceMove = 0.0f;
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

        // --- Flying: follow the SAME navmesh route as walking, but in 3D -----
        //
        // While airborne (Levitation, a flying creature form), the plain ground
        // walker fails two ways: it only steers yaw (so it flies dead-level into
        // arches and never climbs to a raised target), and a naive straight
        // bee-line can't get through tight interior geometry -- the Vivec Puzzle
        // Canal is a 3D maze, and no amount of climbing solves a maze; you must
        // route AROUND the walls. The navmesh pathfinder already does that
        // routing, so we reuse it wholesale and only change HOW we follow it: aim
        // the player's absolute orientation (yaw AND pitch) at the next waypoint
        // and push forward. The engine's flight velocity = orientation * forward
        // then carries us up/down along the route's elevation profile (waypoints
        // rise onto a platform / shrine dais, so pitch rises with them). Absolute
        // aim (rotateObject, as lock-on uses) holds steady since there's no
        // competing mouse input during auto-walk. All the maze-solving,
        // stuck-detection, recovery, door-opening, repath and arrival logic above
        // is shared with walking.
        if (world->isFlying(player))
        {
            const float flyYaw = mPathFinder.getZAngleToNext(playerPos.x(), playerPos.y());
            const float flyPitch = mPathFinder.getXAngleToNext(playerPos.x(), playerPos.y(), playerPos.z());
            world->rotateObject(player, osg::Vec3f(flyPitch, 0.0f, flyYaw), MWBase::RotationFlag_none);

            controls->mMovement = 1.0f; // full forward, along the new facing
            controls->mSideMovement = 0.0f;
            controls->mYawChange = 0.0f; // orientation set absolutely above
            controls->mPitchChange = 0.0f;
            controls->mJump = false;
            controls->mRun = true;
            controls->mChanged = true;
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
