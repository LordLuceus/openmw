#ifndef GAME_MWACCESSIBILITY_PROXIMITYCUE_H
#define GAME_MWACCESSIBILITY_PROXIMITYCUE_H

#include <osg/Vec3f>

#include "../mwworld/ptr.hpp"

namespace MWSound
{
    class Sound;
}

namespace MWAccessibility
{
    using MWSound::Sound;

    /// Plays an audio cue for the scanner's currently-selected target so a
    /// blind player can home in on it by ear.
    ///
    /// Two states, edge-triggered so we never spam the sound system:
    ///  - Approaching: a looping, 3D-positional cue plays *from the object's
    ///    position*. OpenAL's HRTF spatialisation means the player hears which
    ///    direction (and roughly how far) the target is, and can walk until it
    ///    is centred ahead. The loop is attached to the object Ptr, so it
    ///    tracks the target if it moves and is spatialised every frame.
    ///  - Arrived (within the engine's activation distance): the loop stops
    ///    and a single non-looping "you can interact now" sound plays once.
    ///    If the player wanders back out of range, we re-arm and the approach
    ///    loop resumes.
    ///
    /// A single approach/arrival sound pair is used for every object type:
    /// only one target's cue plays at a time, so there's no need to
    /// distinguish types by ear, and new scanner categories get a working cue
    /// for free. Missing assets fail gracefully (the cue is simply inaudible).
    class ProximityCue
    {
    public:
        ProximityCue();
        ~ProximityCue();

        /// Point the cue at \p target (the scanner's selected object). Passing
        /// an empty Ptr (or a different target) stops any cue currently
        /// playing. Cheap to call every selection change; a no-op when the
        /// target is unchanged.
        void setTarget(const MWWorld::Ptr& target);

        /// Point the cue at a fixed world position (used for scanner waypoints,
        /// which have no backing object). Unlike the Ptr cue the sound can't be
        /// attached to an object, so it's played as a static-position 3D sound
        /// and re-issued as the player crosses the approach/arrival threshold.
        void setTarget(const osg::Vec3f& position);

        /// Per-frame: evaluate distance to the target and start/stop the
        /// approach loop or fire the arrival sound as the player crosses the
        /// activation threshold. Safe to call when no target is set.
        void onFrame(float dt);

        /// Stop all cue audio and forget the target. Safe to call when idle.
        void stop();

    private:
        enum class State
        {
            Idle, // no target, nothing playing
            Approaching, // approach loop playing
            Arrived, // within activation range; arrival sound already fired
        };

        void startApproachLoop();
        void stopApproachLoop();
        // \p targetPos is where the one-shot is played; for a Ptr target it's
        // the object's current position, for a waypoint it's the fixed point.
        void playArrivalSound(const osg::Vec3f& targetPos);

        // A target is either a world object (mTarget) or, for waypoints, a fixed
        // position (mPosTarget with mHasPtrTarget == false).
        MWWorld::Ptr mTarget;
        osg::Vec3f mPosTarget;
        bool mHasPtrTarget = true;
        // The currently-playing static-position approach sound handle (position
        // targets only), so we can stop/replace it. Null when not playing or
        // when using a Ptr-attached loop.
        Sound* mPosSound = nullptr;
        State mState = State::Idle;
    };
}

#endif
