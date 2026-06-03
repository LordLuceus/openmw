#include "proximitycue.hpp"

#include <cmath>

#include <components/vfs/pathutil.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwsound/type.hpp"

#include "../mwworld/cellref.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"

namespace
{
    // Proximity-cue assets, relative to a VFS data directory (copied from
    // files/data/sounds/a11y into resources/vfs at build time). A single pair
    // is used for every scanner category -- only one target's cue ever plays
    // at a time, so distinguishing object types by ear is unnecessary, and
    // this means new scanner categories get a working cue for free. Missing
    // files fail gracefully (the sound system logs and plays nothing).
    //
    //  - approach loop: plays continuously from the object while you close in.
    //  - arrival one-shot: plays once when you reach activation range.
    //
    // NB: NormalizedView must reference a string literal with a static
    // lifetime; constexpr globals satisfy that. Sources must be MONO or
    // OpenAL won't spatialise them (stereo plays flat/centred).
    constexpr VFS::Path::NormalizedView kApproachSound("sounds/a11y/approach.wav");
    constexpr VFS::Path::NormalizedView kArrivalSound("sounds/a11y/arrival.wav");

    // Horizontal (XY) distance between player and target. Z is ignored so a
    // target on a slightly different floor height still registers as "here".
    float horizontalDistance(const MWWorld::Ptr& player, const MWWorld::Ptr& target)
    {
        osg::Vec3f p = player.getRefData().getPosition().asVec3();
        osg::Vec3f t = target.getRefData().getPosition().asVec3();
        float dx = t.x() - p.x();
        float dy = t.y() - p.y();
        return std::sqrt(dx * dx + dy * dy);
    }
}

namespace MWAccessibility
{
    ProximityCue::ProximityCue() = default;

    ProximityCue::~ProximityCue()
    {
        stop();
    }

    void ProximityCue::setTarget(const MWWorld::Ptr& target)
    {
        // Unchanged target: nothing to do (keeps the loop running smoothly
        // instead of restarting it every selection refresh).
        if (target == mTarget)
            return;

        // Switching away from the old target: silence whatever it was playing.
        stopApproachLoop();

        mTarget = target;
        mState = target.isEmpty() ? State::Idle : State::Approaching;

        // Don't start audio here -- onFrame() will start the approach loop (or
        // immediately fire the arrival sound if we're already in range). This
        // keeps all the distance logic in one place.
    }

    void ProximityCue::onFrame(float /*dt*/)
    {
        if (mTarget.isEmpty() || mState == State::Idle)
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Target gone (deleted / cell unloaded): stop cleanly.
        if (mTarget.getCellRef().getCount() <= 0)
        {
            stop();
            return;
        }

        const float activateDist = world->getMaxActivationDistance();
        // A small hysteresis band so standing right at the boundary doesn't
        // rapidly toggle between approach and arrival.
        const float reEnterApproachDist = activateDist * 1.25f;

        const float dist = horizontalDistance(player, mTarget);

        switch (mState)
        {
            case State::Approaching:
            {
                // Make sure the loop is actually playing (it may have been
                // stopped externally, e.g. cell change), then check arrival.
                MWBase::SoundManager* snd = MWBase::Environment::get().getSoundManager();
                if (!snd->getSoundPlaying(mTarget, kApproachSound))
                    startApproachLoop();

                if (dist <= activateDist)
                {
                    stopApproachLoop();
                    playArrivalSound();
                    mState = State::Arrived;
                }
                break;
            }
            case State::Arrived:
            {
                // Re-arm if the player wanders back out (past the hysteresis
                // band) so the approach loop guides them in again.
                if (dist > reEnterApproachDist)
                {
                    mState = State::Approaching;
                    startApproachLoop();
                }
                break;
            }
            case State::Idle:
                break;
        }
    }

    void ProximityCue::stop()
    {
        stopApproachLoop();
        mTarget = MWWorld::Ptr();
        mState = State::Idle;
    }

    void ProximityCue::startApproachLoop()
    {
        if (mTarget.isEmpty())
            return;
        MWBase::SoundManager* snd = MWBase::Environment::get().getSoundManager();
        // Already playing? Don't stack a second copy.
        if (snd->getSoundPlaying(mTarget, kApproachSound))
            return;
        // 3D, looping, attached to the target so it tracks the object's
        // position and is HRTF-spatialised every frame. NoEnv so reverb/water
        // filters don't muddy a navigation cue we want to stay legible.
        snd->playSound3D(mTarget, kApproachSound, /*volume=*/1.0f, /*pitch=*/1.0f,
            MWSound::Type::Sfx, MWSound::PlayMode::LoopNoEnv);
    }

    void ProximityCue::stopApproachLoop()
    {
        if (mTarget.isEmpty())
            return;
        MWBase::SoundManager* snd = MWBase::Environment::get().getSoundManager();
        snd->stopSound3D(mTarget, kApproachSound);
    }

    void ProximityCue::playArrivalSound()
    {
        if (mTarget.isEmpty())
            return;
        MWBase::SoundManager* snd = MWBase::Environment::get().getSoundManager();
        // One-shot, 3D so it still comes from the object's direction, NoEnv to
        // keep it crisp.
        snd->playSound3D(mTarget, kArrivalSound, /*volume=*/1.0f,
            /*pitch=*/1.0f, MWSound::Type::Sfx, MWSound::PlayMode::NoEnv);
    }
}
