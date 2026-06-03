#ifndef GAME_MWACCESSIBILITY_PROXIMITYCUE_H
#define GAME_MWACCESSIBILITY_PROXIMITYCUE_H

#include "../mwworld/ptr.hpp"

namespace MWAccessibility
{
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
        void playArrivalSound();

        MWWorld::Ptr mTarget;
        State mState = State::Idle;
    };
}

#endif
