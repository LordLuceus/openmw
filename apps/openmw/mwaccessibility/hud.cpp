#include "hud.hpp"

#include <string>

#include <SDL_scancode.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/datetimemanager.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"

#include "../mwgui/accessibility/activeeffects.hpp"

namespace MWAccessibility
{
    namespace
    {
        // Time-manager pause tag for the accessible HUD. Pausing is additive/
        // tagged (see DateTimeManager), so our pause coexists with any other
        // pause source.
        constexpr std::string_view sHudPauseTag = "a11y_hud";
    }

    void Hud::buildItems()
    {
        mItems.clear();
        mIndex = 0;
        mInEffects = false;
        mEffects.clear();
        mEffectIndex = 0;
        mLastTargetLabel.clear();

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Build the list in the order the screen reads: where you are, your
        // three vitals, breath (only underwater), sneaking, readied weapon /
        // spell, an "Active effects" drill-in row, and the current enemy. Rows
        // with nothing to say are omitted so navigation only lands on real
        // info. The world is paused while the HUD is open, so this snapshot
        // stays accurate until close.
        if (std::string loc = mHost.locationText(); !loc.empty())
            mItems.push_back({ loc, false });

        mItems.push_back({ mHost.playerStatText(0, "Health"), false });
        mItems.push_back({ mHost.playerStatText(1, "Magicka"), false });
        mItems.push_back({ mHost.playerStatText(2, "Fatigue"), false });

        if (std::string breath = mHost.breathText(); !breath.empty())
            mItems.push_back({ breath, false });

        if (MWBase::Environment::get().getMechanicsManager()->isSneaking(player))
            mItems.push_back({ "Sneaking", false });

        if (std::string w = mHost.readiedWeaponText(); !w.empty())
            mItems.push_back({ w, false });
        if (std::string s = mHost.readiedSpellText(); !s.empty())
            mItems.push_back({ s, false });

        // Active effects: a single drill-in row reporting the count. Snapshot
        // the detailed lines (shared with the magic pane) so Enter can walk
        // them. Omitted entirely when nothing is active.
        for (const MWGui::A11y::ActiveEffectLine& line : MWGui::A11y::activeEffects(player))
            mEffects.emplace_back(line.source, line.effect);
        if (!mEffects.empty())
        {
            Item fx;
            fx.mLabel = "Active effects, " + std::to_string(mEffects.size());
            fx.mIsEffects = true;
            mItems.push_back(std::move(fx));
        }

        // Target row: always present so the player can park on it and have it
        // track whichever actor they cycle to with the scanner keys (its label
        // is recomputed live in announceCurrent). mLabel here is just the
        // initial snapshot shown on open.
        Item target;
        target.mIsTarget = true;
        target.mLabel = mHost.targetHealthLabel();
        mItems.push_back(std::move(target));
    }

    void Hud::announceCurrent()
    {
        if (mInEffects)
        {
            if (mEffects.empty())
                return;
            const auto& [source, effect] = mEffects[mEffectIndex];
            mHost.speak(source + ": " + effect + ".");
            return;
        }

        if (mItems.empty())
        {
            mHost.speak("HUD empty.");
            return;
        }
        const Item& item = mItems[mIndex];
        // The target row is recomputed live so it reflects whichever actor the
        // player has cycled the scanner to since the HUD opened.
        std::string label = item.mIsTarget ? mHost.targetHealthLabel() : item.mLabel;
        // Record the spoken target label so the per-frame follow poll only
        // re-announces on an actual change.
        if (item.mIsTarget)
            mLastTargetLabel = label;
        // Hint that the effects row is enterable.
        if (item.mIsEffects)
            label += ". Press Enter for details";
        mHost.speak(label + ".");
    }

    void Hud::followTarget()
    {
        // Only while the HUD is open, in the main list, parked on the target
        // row. Re-announce when the live target label changes (the player
        // cycled the scanner to a different actor, or its health moved -- though
        // the world is paused, so in practice this fires on target switches).
        if (!mActive || mInEffects || mItems.empty())
            return;
        if (!mItems[mIndex].mIsTarget)
            return;
        std::string label = mHost.targetHealthLabel();
        if (label != mLastTargetLabel)
            announceCurrent();
    }

    void Hud::move(int delta)
    {
        if (mInEffects)
        {
            if (mEffects.empty())
                return;
            const int n = static_cast<int>(mEffects.size());
            mEffectIndex = (mEffectIndex + delta % n + n) % n;
            announceCurrent();
            return;
        }
        if (mItems.empty())
            return;
        const int n = static_cast<int>(mItems.size());
        mIndex = (mIndex + delta % n + n) % n;
        announceCurrent();
    }

    void Hud::enterEffects()
    {
        if (mItems.empty() || !mItems[mIndex].mIsEffects || mEffects.empty())
            return;
        mInEffects = true;
        mEffectIndex = 0;
        announceCurrent();
    }

    void Hud::leaveEffects()
    {
        if (!mInEffects)
            return;
        mInEffects = false;
        // Return to the Effects row that was drilled into, and re-announce it
        // so the player knows they're back in the main list.
        announceCurrent();
    }

    void Hud::toggle()
    {
        MWWorld::DateTimeManager* timeMgr = MWBase::Environment::get().getWorld()->getTimeManager();
        if (mActive)
        {
            mActive = false;
            mInEffects = false;
            timeMgr->unpause(sHudPauseTag);
            mHost.speak("HUD closed.");
            return;
        }

        mActive = true;
        // Pause via a time-manager tag (not a GuiMode): this freezes the world
        // -- player and AI movement, time, combat -- while leaving our key
        // handler live, so the player can calmly navigate the HUD and read
        // stats mid-ambush. Unpaused on close, and defensively in reset().
        timeMgr->pause(sHudPauseTag);
        buildItems();
        // Land on the first row and read it (a brief "HUD" cue prefixes it). The
        // quick-info keys (Alt+H/M/F, Shift+Alt+H) stay available for spot
        // checks, and Up/Down walk the list.
        if (mItems.empty())
        {
            mHost.speak("HUD empty.");
            return;
        }
        mHost.speak("HUD.");
        announceCurrent();
    }

    void Hud::reset()
    {
        // If the AHUD was open when the world was torn down (e.g. the player
        // loaded a save from the HUD), drop our pause tag so the new game isn't
        // left frozen. The time manager is reset on load, but unpausing our tag
        // explicitly keeps the bookkeeping honest and harmless if already gone.
        if (mActive)
        {
            mActive = false;
            if (MWBase::World* world = MWBase::Environment::get().getWorld())
                world->getTimeManager()->unpause(sHudPauseTag);
        }
        mInEffects = false;
        mItems.clear();
        mEffects.clear();
        mIndex = 0;
        mEffectIndex = 0;
        mLastTargetLabel.clear();
    }

    bool Hud::handleKey(int scancode, bool ctrl, bool shift, bool alt)
    {
        // Only called while the HUD is open. Arrow Up/Down walk the list (or the
        // effects sub-list); Enter drills into the effects row; Escape/Left back
        // out of the sub-list, else H/Escape (handled by the caller) close the
        // HUD. Plain presses only -- modified combos fall through so the
        // quick-info keys and scanner keys keep working.
        if (ctrl || shift || alt)
            return false;

        switch (scancode)
        {
            case SDL_SCANCODE_UP:
                move(-1);
                return true;
            case SDL_SCANCODE_DOWN:
                move(+1);
                return true;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                // Drill into the effects row. In the sub-list there's nothing
                // to enter, but still consume Enter so it doesn't leak to the
                // scanner's focus-camera action while the HUD is up.
                if (!mInEffects)
                    enterEffects();
                return true;
            case SDL_SCANCODE_LEFT:
            case SDL_SCANCODE_ESCAPE:
                // Escape / Left back out of the effects sub-list (consuming the
                // key). When already in the main list, return false so the
                // caller's Escape handling closes the whole HUD.
                if (mInEffects)
                {
                    leaveEffects();
                    return true;
                }
                return false;
            case SDL_SCANCODE_HOME:
                // Re-read the current row (consistent with Home elsewhere as a
                // "repeat" key) without leaving the HUD.
                announceCurrent();
                return true;
            default:
                return false;
        }
    }
}
