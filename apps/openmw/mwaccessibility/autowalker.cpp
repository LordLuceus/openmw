#include "autowalker.hpp"

#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <components/accessibility/accessibilitymanager.hpp>
#include <components/debug/debuglog.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/flags.hpp>
#include <components/detournavigator/areatype.hpp>
#include <components/detournavigator/navigatorutils.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/pathfinding.hpp"
#include "../mwmechanics/pathgrid.hpp"

#include "../mwphysics/collisiontype.hpp"
#include "../mwphysics/raycasting.hpp"

#include <components/esm3/loadpgrd.hpp>

#include "../mwworld/cellref.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/doorstate.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"
#include "../mwworld/worldmodel.hpp"

#include "scanner.hpp"

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

    // A vertical gap (player-to-target Z) larger than this is a genuine
    // elevation difference -- a different floor / balcony -- worth calling out
    // by ear ("N metres above you") rather than the generic "stopped short".
    // ~128 units is roughly waist-to-shoulder height; a full interior storey is
    // ~256, so this clears slopes, low daises and rugs but flags real levels.
    constexpr float kVerticalGapNotable = 128.0f;

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

    // PATHGRID FALLBACK (multi-level interiors). The runtime navmesh only
    // connects surfaces within Recast's climb/slope limits (34u step, 46deg), so
    // the steep stairs in Dwemer ruins / multi-storey interiors split a cell into
    // DISCONNECTED navmesh islands: a route to a lower- or upper-tier target
    // comes back as a partial path that dies at the lip of the current level.
    // Bethesda hand-authored a pathgrid for every interior that DOES cross those
    // stairs (it's how vanilla NPCs walk them), so when the navmesh route falls
    // well short we rebuild on the pathgrid instead. Confirmed in Arkngthand:
    // navmesh ended 630-780u short while the pathgrid reached within ~60u.
    //
    // kPathgridFallbackShortfall: how far short (horizontally) the navmesh route
    // must end before we bother consulting the pathgrid. Comfortably above the
    // arrival distance and normal off-mesh snap gaps so we DON'T disturb the many
    // legit partial routes (e.g. a door up on a raised terrace we can reach the
    // foot of) that the navmesh handles fine -- those keep their navmesh route.
    constexpr float kPathgridFallbackShortfall = 160.0f;
    // kPathgridFallbackImprovement: only ADOPT the pathgrid route if it ends at
    // least this much closer to the target than the navmesh route did. Protects
    // against swapping a decent navmesh stub for an equally-short (or worse)
    // pathgrid one in cells where the pathgrid also can't reach.
    constexpr float kPathgridFallbackImprovement = 96.0f;

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

    // FALL-ARREST. Some routes (notably with collision-box-shrinking mods like
    // "Jammings off", which give the player a slimmer navmesh agent) send the
    // auto-walker over a lip the navmesh wrongly treats as walkable -- e.g. the
    // Arkngthand Dwemer crest, where a 22-wide agent's mesh leaves a fatal drop a
    // 29-wide agent's doesn't. We can't reliably PREDICT such a drop (interior
    // walkways curve around pits, so probing the straight line to the next
    // waypoint false-positives constantly). So we REACT instead: every grounded
    // frame we remember recent firmly-standing positions; the instant the player
    // pitches into a plunge while auto-walking (steep downward velocity, not on
    // ground, not levitating, and we didn't make them jump), we TELEPORT them back
    // to safe ground, kill the fall, stop, and announce honestly. Because the catch
    // undoes the fall, the message is actionable (the player is alive and standing)
    // rather than a useless call-out at the moment of death. kPlungeVel is the
    // downward speed (units/s) that counts as a real plunge, not a slope/step.
    constexpr float kPlungeVel = 250.0f; // downward units/sec => falling, not stepping
    // We keep grounded positions from the last kSafeTrailWindow seconds and snap
    // back to the HIGHEST one (the crest the player crossed before the descent),
    // not the most recent (which is partway down the killer slope). ~2.5s at run
    // speed is ~12-15m of trail -- enough to clear a crest-and-descent like the
    // Arkngthand one (~1.5s of downhill) but short enough to stay in the same area.
    constexpr float kSafeTrailWindow = 2.5f; // seconds of grounded history to keep
    // A single-frame vertical-velocity spike is NOT a fall. vertVel is derived as
    // (z - prevZ)/dt, so at 64fps a harmless 4-unit step-down reads as ~-255 u/s --
    // right at kPlungeVel -- and the faster the framerate the smaller the step that
    // trips it (the noise floor scales with 1/dt). A real plunge, by contrast, is
    // airborne and descending for many consecutive frames. So we additionally
    // require the body to have been continuously airborne-and-descending for at
    // least kMinFallTime seconds before arresting. This is frame-rate independent
    // (time-based, not frame-count) and easily satisfied by a genuine fall: under
    // gravity it takes ~0.4s to even reach kPlungeVel, by which point the timer is
    // well past this threshold -- while a one-frame step/ledge jitter never comes
    // close. This is the primary defence against false positives where the player
    // is merely walking along a ledge above a drop.
    constexpr float kMinFallTime = 0.2f; // seconds of sustained airborne descent

    // Arrest only if the predicted fall would cost at least this fraction of the
    // player's CURRENT health. Self-tuning: a sturdy warrior shrugs off a fall a
    // frail mage wouldn't, and we respect that rather than using a fixed
    // distance. 0.5 = "would take at least half your current health" -- a
    // genuinely dangerous fall worth stopping for, not a survivable scrape.
    constexpr float kFallDangerHealthFraction = 0.5f;

    // HAZARD-ARREST tuning. Auto-walk can route the player across a damaging
    // surface they can't survive -- most notably LAVA (e.g. Shushishi has a
    // walk-in lava pit guarding a levitation-only treasure room; auto-walk
    // toward a target in that room marches you straight into it). OpenMW has no
    // "lava" concept at all -- lava is just a magma-textured water surface and
    // the damage is applied by data-file scripts -- so we cannot detect the
    // hazard by type. Instead we watch the player's HEALTH: sustained damage
    // taken while NOT in combat means we walked into an environmental hazard, so
    // we snap back to safe ground and stop (the fall-arrest idea, generalised
    // from "don't fall to your death" to "don't walk into a damage field").
    //
    // We accumulate non-combat damage across a burst and reset the accumulator
    // once kHazardGraceTime passes with no further damage. This is what tells a
    // real hazard (lava ticks continuously, many HP/sec -- the accumulator
    // climbs fast and never resets) from a slow incidental tick (a weak poison/
    // disease at a point or two every few seconds -- grace expires between ticks
    // and the accumulator keeps resetting, so it never arms). kHazardDmgArm is
    // the accumulated non-combat damage that arms the catch: high enough to
    // ignore a trivial one-off tick, low enough that lava trips it within ~1s.
    constexpr float kHazardGraceTime = 0.5f; // seconds without damage that ends a burst
    constexpr float kHazardDmgArm = 10.0f; // accumulated non-combat HP loss that arms the catch

    // Oscillation ("limit cycle") detection. Distinct from the physical-wedge
    // check: here the body IS moving (full running speed) but in a loop, so we
    // make no net headway -- e.g. a coarse stair route whose next waypoint flips
    // across us and steers us alternately up then back down. We anchor a
    // reference point and, as long as we stay within kOscBubbleRadius of it while
    // commanding movement, count time; if we never break out of the bubble for
    // kOscTimeout we declare an oscillation and recover/give up. The radius is a
    // couple of body-widths (big enough to ignore normal weaving, small enough
    // that real travel escapes it within a second), and the timeout is long
    // enough that a legitimate tight switchback isn't misread. The anchor
    // re-seeds the instant we travel clear of it, so only true confinement trips.
    constexpr float kOscBubbleRadius = 160.0f; // ~2.3 m
    constexpr float kOscTimeout = 6.0f; // seconds confined => circling

    // DIAGNOSTIC TOGGLE for the verbose per-frame stair-follow log in onFrame.
    // OFF by default: it fires ~5x/sec for the entire duration of every walk and
    // would flood openmw.log during normal play. Flip to true and rebuild to
    // re-enable it when debugging steering / oscillation behaviour. (The sparse
    // per-repath "[a11y] autowalk repath:" log in rebuildPath stays on always --
    // it's low-volume and pinpoints routing/pathgrid-fallback decisions.)
    constexpr bool kLogStairDiag = false;

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

    // After opening a blocking door, walk BACKWARD for this long so the door has
    // room to swing (the engine won't rotate a door into the player's body, so a
    // door we're flush against never opens). A short reverse-step is enough to
    // clear the swing arc; normal forward steering then resumes and carries us
    // through. Suppresses stuck/recovery during the back-off (we're deliberately
    // not making forward progress).
    constexpr float kDoorBackoffDuration = 0.6f; // seconds

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

    // Predict the fall damage \p player would take from a fall of \p fallHeight
    // units, mirroring the engine's getFallDamage (mwmechanics/character.cpp):
    // acrobatics and any active Jump effect reduce the effective height, then
    // the GMST curve converts it to HP. Kept in lock-step with the engine so the
    // fall-arrest danger test reflects the damage the player would ACTUALLY take
    // -- a strong character survives a fall a weak one wouldn't, and we respect
    // that instead of arresting on a fixed distance. Returns 0 below the
    // damage-onset distance.
    float predictFallDamage(const MWWorld::Ptr& player, float fallHeight)
    {
        const MWWorld::Store<ESM::GameSetting>& store
            = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();
        const float fallDistanceMin = store.find("fFallDamageDistanceMin")->mValue.getFloat();
        if (fallHeight < fallDistanceMin)
            return 0.0f;

        const float acrobaticsSkill = player.getClass().getSkill(player, ESM::Skill::Acrobatics);
        const float jumpSpellBonus = player.getClass()
                                         .getCreatureStats(player)
                                         .getMagicEffects()
                                         .getOrDefault(ESM::MagicEffect::Jump)
                                         .getMagnitude();
        const float fallAcroBase = store.find("fFallAcroBase")->mValue.getFloat();
        const float fallAcroMult = store.find("fFallAcroMult")->mValue.getFloat();
        const float fallDistanceBase = store.find("fFallDistanceBase")->mValue.getFloat();
        const float fallDistanceMult = store.find("fFallDistanceMult")->mValue.getFloat();

        float x = fallHeight - fallDistanceMin;
        x -= (1.5f * acrobaticsSkill) + jumpSpellBonus;
        x = std::max(0.0f, x);

        const float a = fallAcroBase + fallAcroMult * (100.0f - acrobaticsSkill);
        x = fallDistanceBase + fallDistanceMult * x;
        x *= a;
        return x;
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
        mTeleportArmed = false; // fresh intent: disarm any stale escape hatch
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
        mTeleportArmed = false; // fresh intent: disarm any stale escape hatch
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
        // Line the player up with the target so it's dead ahead, then state the
        // gap plainly: horizontal distance and, when meaningful, the elevation
        // difference. We deliberately give NO advice ("find stairs", "use the
        // beacon") -- a blind player can't visually hunt for a ramp, and the
        // beacon only helps in the horizontal plane, so such tips are noise. The
        // honest facts (how far, how far up/down) are what's actionable.
        faceTarget(player, targetPos);
        const float metres = trueDist / 69.99f;
        const float vertGap = targetPos.z() - player.getRefData().getPosition().asVec3().z();
        char buf[160];
        if (std::abs(vertGap) > kVerticalGapNotable)
        {
            const char* dir = vertGap > 0.0f ? "above" : "below";
            std::snprintf(buf, sizeof(buf), "%s is %.0f metres ahead and %.0f metres %s.", mTargetName.c_str(),
                metres, std::abs(vertGap) / 69.99f, dir);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%s is %.0f metres ahead.", mTargetName.c_str(), metres);
        }
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

    AutoWalker::DoorProbe AutoWalker::tryOpenBlockingDoor(
        const MWWorld::Ptr& player, const osg::Vec3f& playerPos, float yaw)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWPhysics::RayCastingInterface* rayCasting = world ? world->getRayCasting() : nullptr;
        if (!rayCasting)
            return DoorProbe::None;

        // Cast straight ahead from chest height, hit-testing ONLY doors.
        const osg::Vec3f forward(std::sin(yaw), std::cos(yaw), 0.0f);
        const osg::Vec3f chest = playerPos + osg::Vec3f(0.0f, 0.0f, 32.0f);
        const osg::Vec3f to = chest + forward * kDoorProbeLen;
        const std::vector<MWWorld::ConstPtr> ignore{ player };
        const MWPhysics::RayCastingResult res
            = rayCasting->castRay(chest, to, ignore, {}, MWPhysics::CollisionType_Door);
        if (!res.mHit || res.mHitObject.isEmpty())
            return DoorProbe::None;

        MWWorld::Ptr door = res.mHitObject;
        if (door.getType() != ESM::Door::sRecordId)
            return DoorProbe::None;

        // Already open / opening? Then it isn't what's blocking us -- don't
        // toggle it shut, and don't treat it as an obstacle. A closed idle door
        // has its current rotation equal to its authored (cellref) rotation; any
        // divergence means it's swung open. (Checked BEFORE the lock/trap gates:
        // an already-open door isn't in our way regardless of its flags.)
        if (door.getClass().getDoorState(door) != MWWorld::DoorState::Idle)
            return DoorProbe::None;
        const float doorRot
            = door.getRefData().getPosition().rot[2] - door.getCellRef().getPosition().rot[2];
        if (doorRot != 0.0f)
            return DoorProbe::None; // open and idle: not our blocker

        const std::string doorName = std::string(door.getClass().getName(door));
        const std::string spoken = doorName.empty() ? std::string("the door") : doorName;

        // A closed door is squarely across the path. Decide whether it's safe to
        // open. The unsafe cases STOP the walk (DoorProbe::Blocked) with an honest
        // reason rather than grinding the recovery wiggle into it forever.
        //
        //  - TRAPPED: actuating the door springs its trap on the player, which can
        //    be lethal. We must never auto-open it -- the player has to disarm or
        //    knowingly trigger it themselves. Checked FIRST: a trap is the most
        //    dangerous property and a door can be both trapped and locked.
        if (!door.getCellRef().getTrap().empty())
        {
            speakQueued(spoken + " is trapped. Stopping.");
            return DoorProbe::Blocked;
        }
        //  - LOCKED: we can't pick it mid-walk. Say so once and stop, instead of
        //    spinning recovery sidesteps against a door that will never give.
        if (door.getCellRef().isLocked())
        {
            speakQueued(spoken + " is locked. Stopping.");
            return DoorProbe::Blocked;
        }
        //  - TELEPORT: leads to another cell; silently walking the player through
        //    would teleport them somewhere they didn't choose to go.
        if (door.getCellRef().getTeleport())
        {
            speakQueued(spoken + " leads elsewhere. Stopping.");
            return DoorProbe::Blocked;
        }

        // Safe: closed, idle, in-cell, unlocked, untrapped. Open it via the
        // normal activation path (Lua objectActivated -> Door::activate ->
        // ActionDoor) rather than World::activateDoor directly: that path also
        // plays the open SOUND and runs the proper door semantics (calling
        // activateDoor straight just rotates it silently).
        MWBase::Environment::get().getLuaManager()->objectActivated(door, player);
        speakQueued((doorName.empty() ? std::string("A door") : doorName) + ". Opening.");
        return DoorProbe::Opened;
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
            mBestPathRemaining = std::numeric_limits<float>::max();
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
        // The walk failed to reach a target that is presumably right there. Arm
        // the teleport escape hatch so the player can blink the gap (the scanner
        // enforces the distance cap and only acts on an armed walk).
        armTeleport();
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
        mBestPathRemaining = std::numeric_limits<float>::max();
        mTimeSinceProgress = 0.0f;
        // Seed mLastPos / the oscillation anchor from the player so the first
        // frame's speed reading is sane (otherwise a zero-init mLastPos yields a
        // huge bogus displacement) and the bubble is centred where we start.
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (world)
        {
            MWWorld::Ptr player = world->getPlayerPtr();
            if (!player.isEmpty())
                mLastPos = player.getRefData().getPosition().asVec3();
        }
        mOscAnchor = mLastPos;
        mTimeInOscBubble = 0.0f;
        mTimeSinceMove = 0.0f;
        mRecoveryTimer = 0.0f;
        mRecoveryAttempts = 0;
        mRecoveryDir = 1.0f;
        mDoorBackoffTimer = 0.0f;
        mProgressive = false;
        mTimeSinceCallout = 0.0f;
        mLastCalloutDist = std::numeric_limits<float>::max();
        mPathHadWater = false;
        mPathHadDrop = false;
        mTargetMoving = false;
        mAnnouncedTargetMoving = false;
        mSafeTrail.clear();
        mWalkClock = 0.0f;
        mHasPrevZ = false;
        mFallTime = 0.0f;
        mHasPrevHealth = false;
        mHazardDamage = 0.0f;
        mHazardGrace = 0.0f;
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

    void AutoWalker::armTeleport()
    {
        // A Ptr target that has vanished (count 0 / cell unloaded) has no valid
        // position -- don't arm a teleport to garbage. Position waypoints are
        // always fine.
        if (mHasPtrTarget && mTarget.isEmpty())
            return;
        // Snapshot the target's CURRENT position and name (the walk is about to
        // be cancelled, which clears mTarget/mTargetName). For a wandering NPC
        // this captures where it is right now -- the honest "go to it" spot.
        mTeleportPos = targetPosition();
        mTeleportName = mTargetName;
        mTeleportArmed = true;
    }

    bool AutoWalker::consumeTeleportTarget(osg::Vec3f& outPos, std::string& outName)
    {
        if (!mTeleportArmed)
            return false;
        outPos = mTeleportPos;
        outName = mTeleportName;
        mTeleportArmed = false; // one-shot: a fresh failed walk must re-arm it
        return true;
    }

    bool AutoWalker::rebuildPath()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty() || (mHasPtrTarget && mTarget.isEmpty()))
            return false;

        osg::Vec3f start = player.getRefData().getPosition().asVec3();
        const osg::Vec3f rawEnd = targetPosition();

        // Assume a normal (navmesh / straight) route until proven otherwise; only
        // an adopted pathgrid fallback below sets this, which in turn suppresses
        // the periodic re-path (see mStablePath in the header / onFrame).
        mStablePath = false;

        // Fall-arrest defaults OFF and is armed only for coarse route types that
        // can cross a lethal drop (progressive carrot/bee-line, pathgrid
        // fallback, straight-line last resort). A clean navmesh route leaves it
        // off -- Recast's slope/ledge constraints already guarantee no fatal
        // drop, and that route was the only source of fall-arrest false catches.
        mFallArrestEnabled = false;

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
                    // Progressive routing bee-lines toward a carrot along the
                    // straight line and can cross unmapped terrain / drops, so
                    // arm fall-arrest for this route.
                    mFallArrestEnabled = true;
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

        const bool navPathOk = mPathFinder.isPathConstructed();

        // How far short of the true target does the navmesh route end? A large
        // value means the route stopped well before the goal -- typically the lip
        // of a navmesh island in a multi-level interior (steep Dwemer stairs that
        // Recast won't connect), where a hand-authored pathgrid does cross.
        auto horizShortfall = [&](const osg::Vec3f& pathEnd) {
            return std::sqrt((rawEnd.x() - pathEnd.x()) * (rawEnd.x() - pathEnd.x())
                + (rawEnd.y() - pathEnd.y()) * (rawEnd.y() - pathEnd.y()));
        };
        const float navShortfall = navPathOk && !mPathFinder.getPath().empty()
            ? horizShortfall(mPathFinder.getPath().back())
            : std::numeric_limits<float>::max();

        // PATHGRID FALLBACK: when the navmesh route falls well short, try to reach
        // the target via the cell's hand-authored pathgrid instead. Built with
        // PathType::Full so a partial navmesh result is discarded and PathFinder's
        // built-in pathgrid fallback (which only runs when the navmesh path is
        // empty) actually fires. We adopt it only if it ends meaningfully closer,
        // so legit partial navmesh routes (e.g. the foot of a terrace door we can
        // reach) are left untouched.
        bool usedPathgrid = false;
        float pgShortfall = -1.0f;
        if (navShortfall > kPathgridFallbackShortfall)
        {
            MWBase::World* w = MWBase::Environment::get().getWorld();
            const ESM::Pathgrid* pathgrid = w && player.getCell() && player.getCell()->getCell()
                ? w->getStore().get<ESM::Pathgrid>().search(*player.getCell()->getCell())
                : nullptr;
            if (pathgrid && !pathgrid->mPoints.empty())
            {
                const MWMechanics::PathgridGraph graph(*pathgrid);
                MWMechanics::PathFinder pg;
                pg.buildPath(player, start, end, graph, bounds, flags, costs,
                    /*endTolerance=*/kArrivalDistance, MWMechanics::PathType::Full);
                if (pg.isPathConstructed() && !pg.getPath().empty())
                {
                    pgShortfall = horizShortfall(pg.getPath().back());
                    if (pgShortfall < navShortfall - kPathgridFallbackImprovement)
                    {
                        mPathFinder = std::move(pg);
                        usedPathgrid = true;
                        // Coarse hand-authored nodes can bridge gaps the navmesh
                        // would refuse, so arm fall-arrest for a pathgrid route.
                        mFallArrestEnabled = true;
                        // Coarse hand-authored route: follow it as-is. Rebuilding
                        // it every second re-inserts nodes we just passed and, on
                        // steep stairs, flips the "next" waypoint back across us
                        // into a backstep oscillation -- so freeze periodic
                        // re-pathing while we ride this route.
                        mStablePath = true;
                    }
                }
            }
        }

        // Last resort: a straight-line path. Won't avoid obstacles but
        // gives the user *something* to aim at -- they'll hear "Cannot
        // reach" only when even straight-line fails.
        if (!mPathFinder.isPathConstructed())
        {
            mPathFinder.buildStraightPath(end);
            // A blind bee-line ignores all geometry and can run straight off a
            // ledge, so arm fall-arrest for this route.
            mFallArrestEnabled = true;
        }

        // NO-BACKSTEP PRUNE. A freshly built route can begin with one or more
        // waypoints we have ALREADY walked past -- most visibly with the coarse
        // pathgrid fallback, where the nearest node is often just behind us on a
        // staircase. Steering to such a node turns us around and (combined with
        // the next rebuild re-adding it) produces the up-then-back-down stair
        // oscillation. Drop any leading waypoints that lie behind us relative to
        // the direction of the FOLLOWING waypoint: a node is "behind" when the
        // player has already passed the plane through it perpendicular to the
        // node->next segment, i.e. (player - node) . (next - node) > 0. We keep
        // at least one waypoint so there is always something to steer toward.
        {
            std::size_t pruned = 0;
            while (mPathFinder.getPath().size() > 1)
            {
                const std::deque<osg::Vec3f>& path = mPathFinder.getPath();
                const osg::Vec3f& node = path[0];
                const osg::Vec3f& next = path[1];
                const osg::Vec3f seg = next - node;
                if (seg.length2() < 1.0f)
                    break; // degenerate; leave it
                if ((start - node) * seg > 0.0f)
                {
                    mPathFinder.popFrontPoint();
                    ++pruned;
                }
                else
                    break;
            }
            (void)pruned;
        }

        // [a11y] DIAGNOSTIC (temporary): multi-level navmesh-island bug. Logs the
        // agent bounds, the navmesh route's shortfall, and the pathgrid-fallback
        // decision so we can confirm the fix in game (e.g. Arkngthand).
        {
            osg::Vec3f pathEnd = end;
            if (mPathFinder.isPathConstructed() && !mPathFinder.getPath().empty())
                pathEnd = mPathFinder.getPath().back();
            Log(Debug::Warning) << "[a11y] autowalk repath: agentHalfExtents=(" << bounds.mHalfExtents.x() << ","
                                << bounds.mHalfExtents.y() << "," << bounds.mHalfExtents.z()
                                << ") shapeType=" << static_cast<int>(bounds.mShapeType)
                                << " start=(" << start.x() << "," << start.y() << "," << start.z() << ")"
                                << " rawEnd=(" << rawEnd.x() << "," << rawEnd.y() << "," << rawEnd.z() << ")"
                                << " snappedOk=" << snappedOk << " navPathOk=" << navPathOk
                                << " navShortfall=" << navShortfall << " usedPathgrid=" << usedPathgrid
                                << " pgShortfall=" << pgShortfall << " pathSize=" << mPathFinder.getPathSize()
                                << " pathEnd=(" << pathEnd.x() << "," << pathEnd.y() << "," << pathEnd.z() << ")";
        }

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
        // Total remaining distance ALONG the planned route: from the player to
        // the first (current) waypoint, then summed waypoint-to-waypoint to the
        // end. This is the honest "how much walking is left" measure used for
        // no-progress detection -- unlike straight-line distance to the goal it
        // shrinks monotonically as we follow a route that legitimately winds
        // away from the target (stairs, galleries, switchbacks). Measured in 3D:
        // stair waypoints carry a large vertical component, and ignoring z would
        // understate progress while climbing/descending.
        auto remainingPathLength = [&playerPos, this]() {
            const auto& path = mPathFinder.getPath();
            if (path.empty())
                return 0.0f;
            float total = (path.front() - playerPos).length();
            for (std::size_t i = 1; i < path.size(); ++i)
                total += (path[i] - path[i - 1]).length();
            return total;
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

        const bool targetIsActor = mHasPtrTarget && !mTarget.isEmpty() && mTarget.getClass().isActor();

        // Decide arrival for a candidate point. Horizontal proximity is always
        // required. For ACTOR targets we ALSO require vertical proximity: an NPC
        // on a balcony or upper floor sits almost directly above the spot below,
        // so the snapped navmesh proxy (mEffectiveTarget) lands on her level a
        // few metres up -- horizontally ~on top of us, which the old horizontal-
        // only test mistook for "arrived" while we stood 3-4 m below her, out of
        // interaction reach (the false-arrival bug). Non-actor targets keep the
        // horizontal-only rule on purpose: you legitimately "arrive" standing
        // directly below a ceiling hatch or a door embedded in a wall that you
        // can never share a position with.
        auto arrivedAt = [&](const osg::Vec3f& p) {
            if (horizDistTo(p) > arrivalDist)
                return false;
            if (targetIsActor && std::abs(p.z() - playerPos.z()) > kVerticalGapNotable)
                return false;
            return true;
        };

        // In progressive (cross-cell) mode mEffectiveTarget is a transient
        // carrot, not the goal, so arrival must be judged against the true
        // target only -- otherwise reaching the carrot would falsely announce
        // "arrived". In normal mode we accept arrival at either the true target
        // or its snapped navmesh proxy (a door embedded in a wall).
        bool arrived = arrivedAt(targetPos) || (!mProgressive && arrivedAt(mEffectiveTarget));

        // For a NON-ACTOR object target, a horizontal arrival isn't enough when
        // the object is meaningfully above or below us: require that we can
        // actually interact with it from here. Arrival already demands we're
        // within ~48 units horizontally, so this only ever bites a target that's
        // nearly straight up/down -- e.g. a Dwemer coin on a ledge 5 m overhead,
        // whose position (and whose snapped navmesh proxy, which lands on the
        // floor directly beneath it) sits on top of us yet is plainly out of
        // reach. A ceiling hatch you open from directly below PASSES this check
        // (it's within activation distance -- that's exactly why the engine lets
        // you open it from there), so the start-of-game hatch still registers as
        // arrived; this is the precise distinction that the old horizontal-only
        // rule couldn't make. isWithinActivationReach measures 3D distance to the
        // object's nearest bounding-box surface and honours Telekinesis, matching
        // our own activate gate. Computed only once we'd otherwise declare
        // arrival, so the per-frame cost of the bounds query is avoided. Actors
        // are handled by the vertical check inside arrivedAt; waypoints have no
        // Ptr to reach-test and keep the positional rule.
        if (arrived && mHasPtrTarget && !mTarget.isEmpty() && !mTarget.getClass().isActor())
            arrived = Scanner::isWithinActivationReach(mTarget);

        if (arrived)
        {
            speakQueued("Arrived at " + mTargetName + ".");
            cancel();
            return;
        }
        const float horizDist
            = mProgressive ? horizDistTo(targetPos) : std::min(horizDistTo(targetPos), horizDistTo(mEffectiveTarget));

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

        // --- Fall-arrest -----------------------------------------------------
        // React to (don't predict) walking off a fatal drop. Track recent firmly
        // grounded positions; if we pitch into a real plunge, teleport back to the
        // safe crest, kill the fall, and stop honestly. Skipped while levitating/
        // flying (a controlled descent is fine) and during a recovery wiggle (its
        // hop briefly leaves the ground by design -- a hop is not a plunge, and its
        // small drop never reaches kPlungeVel anyway). Derives vertical velocity
        // from the height delta since last frame because the actor's fall speed
        // isn't handed to us here.
        {
            const bool onGround = world->isOnGround(player);
            const bool flying = world->isFlying(player);
            float vertVel = 0.0f;
            if (mHasPrevZ && dt > 0.0f)
                vertVel = (playerPos.z() - mPrevZ) / dt;
            mPrevZ = playerPos.z();
            mHasPrevZ = true;
            mWalkClock += dt;

            // Accumulate continuous airborne-descent time; reset the moment we're
            // grounded, rising, or not actually descending. This is the frame-rate
            // independent "is this a sustained fall?" signal that distinguishes a
            // real plunge from a one-frame step/ledge velocity spike.
            if (!onGround && !flying && vertVel < 0.0f)
                mFallTime += dt;
            else
                mFallTime = 0.0f;

            if (onGround && !flying)
            {
                // Record this grounded position in the trail and drop entries older
                // than the window. We snap to the HIGHEST trail point on a catch, so
                // walking down the killer slope (every frame "grounded", but lower
                // each time) can't poison the snap target -- the crest stays the max.
                mSafeTrail.emplace_back(mWalkClock, playerPos);
                while (!mSafeTrail.empty() && mWalkClock - mSafeTrail.front().first > kSafeTrailWindow)
                    mSafeTrail.pop_front();
            }
            else if (mFallArrestEnabled && !flying && !mSafeTrail.empty() && mRecoveryTimer <= 0.0f
                && vertVel <= -kPlungeVel && mFallTime >= kMinFallTime)
            {
                // A steep downward velocity is NECESSARY but not SUFFICIENT: running
                // DOWN A WALKABLE SLOPE descends just as fast as a free-fall (observed:
                // -1100+ u/s sustained for over a second on a Caldera hillside) yet
                // does NO damage, because the engine keeps re-grounding you the whole
                // way. A straight-down raycast can't tell the two apart -- a slope has
                // just as much "ground far below" as a cliff -- which is exactly what
                // made the old distance-based test false-catch survivable hillsides.
                //
                // The authoritative discriminator is the engine's OWN accumulated fall
                // height (CreatureStats::getFallHeight): it grows ONLY while genuinely
                // airborne and descending, and is reset to zero every grounded frame
                // (see mtphysics addToFallHeight / land). It is precisely the value the
                // engine will feed to getFallDamage on landing. On a walkable slope it
                // stays ~0 (no damage); in a real plunge it climbs to the true drop. We
                // feed it through the same acrobatics-aware formula and arrest only when
                // the fall SO FAR would already cost a dangerous fraction of THIS
                // character's current health -- which, for a tall cliff, crosses the
                // threshold mid-air (before impact) so the snap-back still saves us,
                // while a slope never crosses it at all.
                const float fallHeight = player.getClass().getCreatureStats(player).getFallHeight();
                const float predictedDamage = predictFallDamage(player, fallHeight);
                const float currentHealth
                    = player.getClass().getCreatureStats(player).getHealth().getCurrent();
                const float dangerThreshold = kFallDangerHealthFraction * std::max(1.0f, currentHealth);
                if (predictedDamage < dangerThreshold)
                {
                    // Either we're on a walkable slope (engine fall height stays ~0) or
                    // the fall accumulated so far isn't dangerous yet -- let it ride. If
                    // it IS a real plunge, fall height keeps climbing and a later frame
                    // will cross the threshold and arrest while still airborne.
                    if (kLogStairDiag)
                        Log(Debug::Warning) << "[a11y] autowalk fall-arrest: IGNORED survivable drop vertVel="
                                            << vertVel << " engineFallHeight=" << fallHeight
                                            << " predictedDamage=" << predictedDamage << " health=" << currentHealth
                                            << " (threshold=" << dangerThreshold << ") at pos=(" << playerPos.x()
                                            << "," << playerPos.y() << "," << playerPos.z() << ")";
                }
                else
                {
                // Pitched into a real plunge while auto-walking. Snap back to the
                // HIGHEST recent grounded point (the safe crest before the descent),
                // zero the fall, stop, and report honestly.
                osg::Vec3f snap = mSafeTrail.front().second;
                for (const auto& entry : mSafeTrail)
                    if (entry.second.z() > snap.z())
                        snap = entry.second;
                // Always-on (not diag-gated): a fall-arrest catch is rare and
                // user-visible, and false positives -- catching a step-down or a
                // gentle slope that wasn't actually fatal -- are the main risk. Log
                // enough to tell a real plunge from a false catch without a special
                // build: the trip velocity, where we caught it, where we snapped to,
                // how far back/up that snap reached, and the trail depth.
                const float snapBackHoriz = std::sqrt((snap.x() - playerPos.x()) * (snap.x() - playerPos.x())
                    + (snap.y() - playerPos.y()) * (snap.y() - playerPos.y()));
                Log(Debug::Warning) << "[a11y] autowalk fall-arrest: caught plunge vertVel=" << vertVel
                                    << " (kPlungeVel=" << kPlungeVel << ") fallTime=" << mFallTime
                                    << " fallHeight=" << fallHeight << " predictedDamage=" << predictedDamage
                                    << " health=" << currentHealth << " (threshold=" << dangerThreshold
                                    << ") at pos=(" << playerPos.x() << "," << playerPos.y() << "," << playerPos.z()
                                    << ") snapped to (" << snap.x() << "," << snap.y() << "," << snap.z()
                                    << ") snapBackHoriz=" << snapBackHoriz << " snapUpZ=" << (snap.z() - playerPos.z())
                                    << " trail=" << mSafeTrail.size();
                world->moveObject(player, snap, /*movePhysics=*/true);
                if (auto* controls = MWBase::Environment::get().getLuaManager()->getActorControls(player))
                {
                    controls->mMovement = 0.0f;
                    controls->mSideMovement = 0.0f;
                    controls->mYawChange = 0.0f;
                    controls->mPitchChange = 0.0f;
                    controls->mJump = false;
                    controls->mRun = false;
                    controls->mChanged = true;
                }
                world->queueMovement(player, osg::Vec3f(0.0f, 0.0f, 0.0f)); // kill fall velocity
                // Clear any accumulated fall height so the snap-back can't trigger
                // fall damage on the next grounded frame (land() resets it).
                player.getClass().getCreatureStats(player).land(/*isPlayer=*/true);
                speakQueued("Path drops off. Cannot reach " + mTargetName + " safely.");
                armTeleport(); // unreachable on foot -> offer the escape hatch
                cancel();
                return;
                }
            }
        }

        // --- Hazard-arrest: don't auto-walk the player into a damage field ---
        //
        // Fall-arrest stops us walking off a fatal drop; this stops us walking
        // INTO a damaging surface -- lava above all (e.g. Shushishi's walk-in
        // lava pit guarding a levitation-only treasure room). OpenMW has no lava
        // type to test, so we watch HEALTH: sustained damage taken while NOT in
        // combat is an environmental hazard. On a catch we snap back to the same
        // safe crest fall-arrest uses (highest recent grounded point -- the edge
        // we stepped from, not the spot inside the hazard) and stop honestly.
        {
            const float health = player.getClass().getCreatureStats(player).getHealth().getCurrent();
            const bool inCombat
                = !MWBase::Environment::get().getMechanicsManager()->getActorsFighting(player).empty();

            if (mHasPrevHealth && !inCombat && health < mPrevHealth)
            {
                // Lost health this frame and nobody is fighting us -> environmental.
                mHazardDamage += (mPrevHealth - health);
                mHazardGrace = kHazardGraceTime;
            }
            else if (mHazardGrace > 0.0f)
            {
                // Count down the grace window; if it lapses with no further
                // damage, the burst is over -- forget it (isolated ticks never arm).
                mHazardGrace -= dt;
                if (mHazardGrace <= 0.0f)
                    mHazardDamage = 0.0f;
            }
            mPrevHealth = health;
            mHasPrevHealth = true;

            if (mHazardDamage >= kHazardDmgArm && !mSafeTrail.empty() && mRecoveryTimer <= 0.0f)
            {
                osg::Vec3f snap = mSafeTrail.front().second;
                for (const auto& entry : mSafeTrail)
                    if (entry.second.z() > snap.z())
                        snap = entry.second;
                const float snapBackHoriz = std::sqrt((snap.x() - playerPos.x()) * (snap.x() - playerPos.x())
                    + (snap.y() - playerPos.y()) * (snap.y() - playerPos.y()));
                // Always-on (like fall-arrest): rare and user-visible. Log the
                // accumulated damage, where we caught it, and the snap target so a
                // false catch (e.g. a hazard we should have tolerated) is diagnosable.
                Log(Debug::Warning) << "[a11y] autowalk hazard-arrest: caught damage field hazardDamage="
                                    << mHazardDamage << " (arm=" << kHazardDmgArm << ") at pos=(" << playerPos.x()
                                    << "," << playerPos.y() << "," << playerPos.z() << ") snapped to (" << snap.x()
                                    << "," << snap.y() << "," << snap.z() << ") snapBackHoriz=" << snapBackHoriz
                                    << " trail=" << mSafeTrail.size();
                world->moveObject(player, snap, /*movePhysics=*/true);
                if (auto* controls = MWBase::Environment::get().getLuaManager()->getActorControls(player))
                {
                    controls->mMovement = 0.0f;
                    controls->mSideMovement = 0.0f;
                    controls->mYawChange = 0.0f;
                    controls->mPitchChange = 0.0f;
                    controls->mJump = false;
                    controls->mRun = false;
                    controls->mChanged = true;
                }
                speakQueued("Path crosses a hazard. Cannot reach " + mTargetName + " safely.");
                armTeleport(); // unreachable on foot -> offer the escape hatch
                cancel();
                return;
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

        // --- Door back-off ---------------------------------------------------
        // We just opened a door we were wedged against. The engine won't swing a
        // door into our body, so step backward briefly to clear its arc, then let
        // normal steering resume and walk us through. Runs before stuck detection
        // so the deliberate non-progress here never trips a give-up. Update mLastPos
        // so the first post-backoff frame doesn't see a huge displacement spike.
        if (mDoorBackoffTimer > 0.0f)
        {
            mDoorBackoffTimer -= dt;
            mLastPos = playerPos;
            auto* controls
                = MWBase::Environment::get().getLuaManager()->getActorControls(player);
            if (!controls)
            {
                cancel();
                return;
            }
            controls->mMovement = -1.0f; // walk straight back, away from the door
            controls->mSideMovement = 0.0f;
            controls->mYawChange = 0.0f;
            controls->mPitchChange = 0.0f;
            controls->mJump = false;
            controls->mRun = false; // a gentle step back, not a sprint
            controls->mChanged = true;
            return;
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
        // No-progress detection counts as progress if EITHER of two independent
        // measures improves -- because each covers the other's blind spot:
        //
        //  - REMAINING PATH LENGTH. Following a correct route up a spiralling
        //    stair increases straight-line goal-distance for many seconds while
        //    we are in fact advancing, so the route remainder is the honest
        //    signal there ("we are consuming the planned path").
        //
        //  - STRAIGHT-LINE DISTANCE TO THE TRUE TARGET. On a long OUTDOOR walk
        //    only a cell or two of navmesh is loaded ahead, so the planned route
        //    is PARTIAL and its far end RECEDES as new terrain streams in (in
        //    progressive mode the "carrot" does this explicitly; in normal mode
        //    the partial-path end does it implicitly). The route remainder then
        //    plateaus -- we run at full speed but the route end runs away just as
        //    fast -- so path-length alone never improves and the 10 s backstop
        //    fired a false "stuck"/"stopped short" even though we were plainly
        //    closing on the target. Goal-distance is the honest signal there.
        //
        // Requiring only ONE to improve cannot weaken genuine-stuck detection:
        // a truly wedged body stops moving (caught by the physical-wedge timer)
        // or loops in place (caught by the oscillation check); this backstop only
        // fires when we are neither consuming the route NOR nearing the goal.
        // CRUCIAL: each "best" is snapped down to the current value ONLY when the
        // current value beats it by a full kProgressEpsilon -- i.e. on a discrete
        // progress EVENT -- never every frame. If we instead ratcheted the best
        // to the current value every frame, the best would always sit within one
        // frame's travel of the current value, so "current < best - epsilon"
        // could never become true while moving smoothly: at running speed (~250
        // u/s) a 60 fps frame advances only ~4 units, under the 8-unit epsilon,
        // so the timer would climb to the give-up threshold even as we bee-line
        // at full speed toward the goal (the false-stuck-at-a-fixed-distance bug).
        // Holding the best still between events lets that 8-unit margin actually
        // accumulate over a couple of frames, firing a reset roughly continuously.
        const float pathRemaining = remainingPathLength();
        bool madeProgress = false;
        if (pathRemaining < mBestPathRemaining - kProgressEpsilon)
        {
            mBestPathRemaining = pathRemaining;
            madeProgress = true;
        }
        if (horizDist < mBestDistToGoal - kProgressEpsilon)
        {
            mBestDistToGoal = horizDist;
            madeProgress = true;
        }
        if (madeProgress)
        {
            mTimeSinceProgress = 0.0f;
            mRecoveryAttempts = 0; // genuine progress: fresh set of wiggles next snag
        }
        else
        {
            mTimeSinceProgress += dt;
        }

        // OSCILLATION ("limit cycle") DETECTION. The two checks above can both be
        // defeated at once by a route that makes us move fast in a LOOP: the
        // body never stops (so the physical-wedge timer stays at 0) and each lap
        // can jitter the remaining-path length by just over kProgressEpsilon (so
        // the no-progress timer keeps resetting) -- the player then circles
        // forever and the walk never honestly ends. This check is independent of
        // both: it watches our straight-line displacement from an anchor point.
        // While we stay within a small bubble of the anchor we accrue time; the
        // instant we travel clear of it we re-anchor and reset. Staying confined
        // for kOscTimeout while commanding movement means we're going in circles,
        // not progressing -- so we force the same honest recovery-then-give-up
        // path the wedge check uses. Suppressed for a moving target (chasing a
        // wandering NPC around a pillar is legitimate "confinement"). Skipped
        // during a recovery wiggle, which drives its own deliberate motion.
        if (mRecoveryTimer <= 0.0f)
        {
            if ((playerPos - mOscAnchor).length2() > kOscBubbleRadius * kOscBubbleRadius)
            {
                mOscAnchor = playerPos; // broke out of the bubble: genuine travel
                mTimeInOscBubble = 0.0f;
            }
            else
            {
                mTimeInOscBubble += dt;
            }
        }
        if (!mTargetMoving && mTimeInOscBubble >= kOscTimeout && mRecoveryTimer <= 0.0f)
        {
            // Confined too long while trying to move: treat exactly like a
            // physical wedge -- try the door/blocker/recovery escalation, and if
            // that's exhausted, give up honestly rather than circle in silence.
            const float oscYaw = player.getRefData().getPosition().rot[2];
            const DoorProbe oscDoor = tryOpenBlockingDoor(player, playerPos, oscYaw);
            if (oscDoor == DoorProbe::Blocked)
            {
                // Locked / trapped / teleport door across the path: it already
                // announced why. Stop here rather than circle against it.
                cancel();
                return;
            }
            if (oscDoor == DoorProbe::Opened)
            {
                mTimeSinceMove = 0.0f;
                mTimeSinceProgress = 0.0f;
                mBestDistToGoal = std::numeric_limits<float>::max();
                mBestPathRemaining = std::numeric_limits<float>::max();
                mRecoveryAttempts = 0;
                mTimeInOscBubble = 0.0f;
                mOscAnchor = playerPos;
                mDoorBackoffTimer = kDoorBackoffDuration;
                return;
            }
            if (mRecoveryAttempts >= kMaxRecoveryAttempts)
            {
                if (handleGiveUp(player, playerPos, targetPos))
                {
                    cancel();
                    return;
                }
                // Phased through a blocker: reset the bubble and keep walking.
                mTimeInOscBubble = 0.0f;
                mOscAnchor = playerPos;
                return;
            }
            // Kick off a recovery wiggle (and a re-path) to try to break the
            // loop; reset the bubble so we give the wiggle a fair chance.
            ++mRecoveryAttempts;
            mRecoveryTimer = kRecoveryDuration;
            mRecoveryDir = chooseRecoverySide(player, playerPos, oscYaw, -mRecoveryDir);
            mTimeInOscBubble = 0.0f;
            mOscAnchor = playerPos;
            mTimeSinceMove = 0.0f;
            rebuildPath();
            return;
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
            // too -- check for a closed door FIRST. The navmesh routes through
            // doors assuming they open, but auto-walk never actuated them, so a
            // shut door stalls progress just like a wall. If we open one, refresh
            // the budgets and let normal steering carry us through.
            const float doorYaw = player.getRefData().getPosition().rot[2];
            const DoorProbe progressDoor = tryOpenBlockingDoor(player, playerPos, doorYaw);
            if (progressDoor == DoorProbe::Blocked)
            {
                cancel(); // locked / trapped / teleport: already announced, stop
                return;
            }
            if (progressDoor == DoorProbe::Opened)
            {
                mTimeSinceMove = 0.0f;
                mTimeSinceProgress = 0.0f;
                mBestDistToGoal = std::numeric_limits<float>::max();
                mBestPathRemaining = std::numeric_limits<float>::max();
                mRecoveryAttempts = 0;
                mDoorBackoffTimer = kDoorBackoffDuration; // step back so it can swing
                return;
            }

            // handleGiveUp phases through a blocking NPC and keeps walking
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
            const DoorProbe wedgeDoor = tryOpenBlockingDoor(player, playerPos, doorYaw);
            if (wedgeDoor == DoorProbe::Blocked)
            {
                cancel(); // locked / trapped / teleport: already announced, stop
                return;
            }
            if (wedgeDoor == DoorProbe::Opened)
            {
                mTimeSinceMove = 0.0f;
                mTimeSinceProgress = 0.0f;
                mBestDistToGoal = std::numeric_limits<float>::max();
                mBestPathRemaining = std::numeric_limits<float>::max();
                mRecoveryAttempts = 0;
                mDoorBackoffTimer = kDoorBackoffDuration; // step back so it can swing
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
        // the gap. Also throttled on a stable pathgrid route (mStablePath): that
        // coarse route is correct as built, and re-running it every second
        // re-inserts the node we just passed -- on steep stairs that flips the
        // next waypoint back across us and drives a backstep oscillation. We
        // STILL rebuild the moment the route is consumed (isPathConstructed()
        // false) or for a moving target (so we keep chasing a wandering NPC);
        // recovery and arrival logic rebuild on their own when actually needed.
        if (!mFinalApproach)
        {
            mTimeSinceRepath += dt;
            const bool periodicDue = mTimeSinceRepath >= kRepathInterval;
            const bool pathGone = !mPathFinder.isPathConstructed();
            const bool suppressPeriodic = mStablePath && !mTargetMoving;
            if (pathGone || (periodicDue && !suppressPeriodic))
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
            // Horizontal proximity is necessary but NOT sufficient: a target
            // meaningfully above or below us (an overhead trapdoor, a balcony
            // hatch) can be ~on top of us horizontally yet metres out of reach.
            // This is the SAME false-vertical-arrival the primary arrival check
            // above guards against (fix c4a800441b) -- but this completed-path
            // branch was still horizontal-only, so a partial/1-node route that
            // ran out directly beneath a ceiling trapdoor (snap to navmesh
            // failed, so the route just walks under it) announced a false
            // "Arrived" while the door sat ~5 m up. Apply the identical gate:
            // for a non-actor object target require real activation reach (3D
            // distance to its nearest bounding-box surface, honouring
            // Telekinesis); for an actor require vertical proximity too; a bare
            // waypoint (no Ptr) keeps the horizontal rule. A hatch you CAN open
            // from directly below still passes (it's within activation range --
            // exactly why the engine lets you open it).
            bool reachOk = trueDist <= reachDist;
            if (reachOk && mHasPtrTarget && !mTarget.isEmpty())
            {
                if (mTarget.getClass().isActor())
                    reachOk = std::abs(targetPos.z() - playerPos.z()) <= kVerticalGapNotable;
                else
                    reachOk = Scanner::isWithinActivationReach(mTarget);
            }
            if (reachOk)
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
                mBestPathRemaining = std::numeric_limits<float>::max();
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

        const float yawErr = yawDelta; // [a11y] temporary: true steer error pre-clamp

        // Apply only a fraction per frame so we don't snap.
        const float kTurnSpeed = 6.0f; // rad/s
        float maxTurn = kTurnSpeed * dt;
        if (yawDelta > maxTurn)
            yawDelta = maxTurn;
        else if (yawDelta < -maxTurn)
            yawDelta = -maxTurn;

        // [a11y] STAIR-FOLLOW DIAGNOSTIC (debug-gated via kLogStairDiag, OFF by
        // default; throttled ~5x/sec when on). The repath log shows position over
        // time but not WHY the steer fails on stairs; this logs, per frame: the
        // actual move speed (sliding vs truly stuck), the next waypoint and its
        // height delta (steering UP the stairs or ACROSS them?), the steer angle,
        // goal/route progress, recovery state, and the stable-path / oscillation
        // state. Kept (gated) rather than removed so steering regressions can be
        // re-diagnosed instantly -- flip kLogStairDiag to true and rebuild.
        if (kLogStairDiag)
            mStairDiagTimer += dt;
        if (kLogStairDiag && mStairDiagTimer >= 0.2f)
        {
            mStairDiagTimer = 0.0f;
            osg::Vec3f wp = targetPos;
            if (!mPathFinder.getPath().empty())
                wp = mPathFinder.getPath().front();
            constexpr float kRad2Deg = 57.2957795f;
            Log(Debug::Warning) << "[a11y] autowalk stairdiag: pos=(" << playerPos.x() << "," << playerPos.y() << ","
                                << playerPos.z() << ") speed=" << speed << " horizDist=" << horizDist
                                << " bestDist=" << mBestDistToGoal << " pathRem=" << pathRemaining
                                << " bestPathRem=" << mBestPathRemaining << " wp=(" << wp.x() << "," << wp.y() << ","
                                << wp.z() << ") wpDz=" << (wp.z() - playerPos.z()) << " pathSize="
                                << mPathFinder.getPathSize() << " yawErrDeg=" << (yawErr * kRad2Deg)
                                << " tSinceMove=" << mTimeSinceMove << " tSinceProg=" << mTimeSinceProgress
                                << " recovTimer=" << mRecoveryTimer << " recovAtt=" << mRecoveryAttempts
                                << " final=" << mFinalApproach << " prog=" << mProgressive
                                << " stable=" << mStablePath << " oscT=" << mTimeInOscBubble;
        }

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
