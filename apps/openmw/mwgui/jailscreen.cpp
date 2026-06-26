#include <MyGUI_ScrollBar.h>

#include <components/misc/rng.hpp>
#include <components/misc/strings/format.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/store.hpp"

#include <unicode/fmtable.h>
#include <unicode/unistr.h>

#include <components/l10n/manager.hpp>

#include "accessibility/speech.hpp"

#include "jailscreen.hpp"

namespace MWGui
{
    JailScreen::JailScreen()
        : WindowBase("openmw_jail_screen.layout")
        , mDays(1)
        , mFadeTimeRemaining(0)
    {
        getWidget(mProgressBar, "ProgressBar");

        mTimeAdvancer.eventProgressChanged += MyGUI::newDelegate(this, &JailScreen::onJailProgressChanged);
        mTimeAdvancer.eventFinished += MyGUI::newDelegate(this, &JailScreen::onJailFinished);

        center();
    }

    void JailScreen::goToJail(int days)
    {
        mDays = days;

        MWBase::Environment::get().getWindowManager()->fadeScreenOut(0.5);
        mFadeTimeRemaining = 0.5;

        setVisible(false);
        mProgressBar->setScrollRange(100 + 1);
        mProgressBar->setScrollPosition(0);
        mProgressBar->setTrackSize(0);

        // Screen reader: the jail screen is a non-interactive, purely visual
        // progress bar (the player is fading out, being teleported to a prison
        // marker, then time advances). Without sight there is no cue that any of
        // this is happening, nor how long the sentence is, until the engine's
        // own "released from prison" message at the very end (which is already
        // spoken via the interactive message box). Announce the sentence up
        // front so the wait isn't silent and unexplained. Routed entirely
        // through localization: sInPrisonTitle is the on-screen caption, and the
        // pluralized day count comes from the OMWEngine JailSentenceA11y string
        // (a speech-natural full sentence, not the abbreviated "5 d" duration).
        const std::string title(
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sInPrisonTitle", "Prison"));
        auto l10n = MWBase::Environment::get().getL10nManager()->getContext("OMWEngine");
        const std::string announcement = l10n->formatMessage("JailSentenceA11y", { "title", "days" },
            { icu::Formattable(icu::UnicodeString::fromUTF8(title)), icu::Formattable(mDays) });
        A11y::say(announcement);
    }

    void JailScreen::onFrame(float dt)
    {
        mTimeAdvancer.onFrame(dt);

        if (mFadeTimeRemaining <= 0)
            return;

        mFadeTimeRemaining -= dt;

        if (mFadeTimeRemaining <= 0)
        {
            MWWorld::Ptr player = MWMechanics::getPlayer();
            MWBase::Environment::get().getWorld()->teleportToClosestMarker(
                player, ESM::RefId::stringRefId("prisonmarker"));
            MWBase::Environment::get().getWindowManager()->fadeScreenOut(
                0.f); // override fade-in caused by cell transition

            setVisible(true);
            mTimeAdvancer.run(100);
        }
    }

    void JailScreen::onJailProgressChanged(int cur, int /*total*/)
    {
        mProgressBar->setScrollPosition(0);
        mProgressBar->setTrackSize(
            static_cast<int>(cur / (float)(mProgressBar->getScrollRange()) * mProgressBar->getLineSize()));
    }

    void JailScreen::onJailFinished()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Jail);
        MWBase::Environment::get().getWindowManager()->fadeScreenIn(0.5);

        MWWorld::Ptr player = MWMechanics::getPlayer();

        MWBase::Environment::get().getMechanicsManager()->rest(mDays * 24, true);
        MWBase::Environment::get().getWorld()->advanceTime(mDays * 24);

        // We should not worsen corprus when in prison
        player.getClass().getCreatureStats(player).getActiveSpells().skipWorsenings(mDays * 24);
        MWBase::Environment::get().getLuaManager()->jailTimeServed(player, mDays);
    }
}
