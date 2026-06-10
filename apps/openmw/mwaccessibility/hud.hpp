#ifndef GAME_MWACCESSIBILITY_HUD_H
#define GAME_MWACCESSIBILITY_HUD_H

#include <string>
#include <utility>
#include <vector>

namespace MWAccessibility
{
    // Callbacks the Hud needs from its owner (the Scanner) to build and speak
    // its rows. These are the spoken-phrase builders that depend on the broader
    // scanner/lock-on selection state, plus the shared speech sink -- everything
    // else (player ptr, time manager, active effects) the Hud reads from the
    // engine directly. Kept as a narrow interface so the Hud doesn't need to
    // pull in the whole Scanner header and the coupling stays explicit.
    class HudHost
    {
    public:
        virtual ~HudHost() = default;

        // Speak \p text via the screen reader (interrupting current speech).
        virtual void speak(const std::string& text) = 0;

        // "Target: <Name>, health N percent" / "Target: none" for the live
        // enemy (locked target, else current scanner selection). Recomputed each
        // time the target row is announced so it tracks the player's cycling.
        virtual std::string targetHealthLabel() = 0;

        // Spoken phrases for the static HUD rows, each empty when that row has
        // nothing to report (so it is omitted from the list).
        virtual std::string locationText() const = 0; // resolved cell name
        virtual std::string playerStatText(int index, const char* label) const = 0;
        virtual std::string breathText() const = 0; // only underwater
        virtual std::string readiedWeaponText() const = 0; // "Weapon: Iron Dagger"
        virtual std::string readiedSpellText() const = 0; // "Spell: Fireball"
    };

    // The Accessible HUD (AHUD): a navigable, speech-driven readout of the
    // player's situation (location, vitals, breath, sneak, readied weapon/spell,
    // active effects, current enemy). Opening it pauses the world via a
    // time-manager tag so an ambushed player can calmly assess, while the
    // scanner and quick-info keys keep working. The single "Active effects" row
    // drills into a sub-list with Enter; Escape/Left backs out.
    //
    // All engine-facing reads go through the host (see HudHost) or the engine
    // singletons directly; the Hud owns only its own navigation state.
    class Hud
    {
    public:
        explicit Hud(HudHost& host)
            : mHost(host)
        {
        }

        bool isActive() const { return mActive; }

        // Toggle the HUD open/closed. Opening pauses the world, snapshots the
        // rows and reads the first; closing unpauses and announces the close.
        void toggle();

        // Drop all HUD state and lift the world pause if held. Called from
        // Scanner::clear() at world teardown so a HUD left open across a load
        // can't strand the new game frozen.
        void reset();

        // Route a key while the HUD is open. Returns true if consumed. Up/Down
        // walk the list (or the effects sub-list); Enter drills into the effects
        // row; Escape/Left backs out of the sub-list (returning false in the
        // main list so the caller's Escape closes the HUD); Home re-reads the
        // current row. Modified combos fall through (false) so the quick-info
        // and scanner keys keep working.
        bool handleKey(int scancode, bool ctrl, bool shift, bool alt);

        // While the HUD is open and parked on the live target row, re-announce
        // it whenever the underlying target changes. No-op otherwise. Called
        // each frame from Scanner::onFrame (which runs even while the HUD-paused
        // world is frozen).
        void followTarget();

    private:
        // One row of the HUD list, built fresh each time the HUD opens (the
        // world is paused, so a snapshot is fine). mLabel is what's spoken when
        // the cursor lands on it. mIsEffects marks the single "Active effects"
        // row, which the player drills into with Enter. mIsTarget marks the
        // enemy row, whose label is recomputed live from the host so it tracks
        // the target the player cycles to with the scanner keys.
        struct Item
        {
            std::string mLabel;
            bool mIsEffects = false;
            bool mIsTarget = false;
        };

        void buildItems(); // snapshot the rows from current state (on open)
        void move(int delta); // move the cursor and speak the row landed on
        void announceCurrent(); // speak the current row / effect sub-row
        void enterEffects(); // drill into the active-effects sub-list
        void leaveEffects(); // back out to the main list

        HudHost& mHost;

        bool mActive = false;
        std::vector<Item> mItems;
        int mIndex = 0;
        // True when the cursor is inside the active-effects sub-list (entered
        // from the Effects row); Up/Down walk mEffects, Escape/Left back out.
        bool mInEffects = false;
        // Snapshot of the active-effect sub-list (source + effect strings),
        // taken when the player drills in.
        std::vector<std::pair<std::string, std::string>> mEffects;
        int mEffectIndex = 0;
        // Last target-row label spoken, so followTarget re-announces only when
        // the live target actually changes.
        std::string mLastTargetLabel;
    };
}

#endif
