#include "screen.hpp"

#include <MyGUI_InputManager.h>
#include <MyGUI_Widget.h>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/windowmanager.hpp"

#include "speech.hpp"
#include "uimanager.hpp"

namespace MWGui::A11y
{
    Screen::Screen(bool disableEngineNav)
        : mDisableEngineNav(disableEngineNav)
    {
    }

    Screen::~Screen()
    {
        UiManager::instance().clear(this);
    }

    bool Screen::isActive() const
    {
        return UiManager::instance().isActive(this);
    }

    void Screen::setVirtualFocus(MyGUI::Widget* anchor)
    {
        mVirtual = true;
        mAnchor = anchor;
        // Tell the engine's keyboard navigation not to consume Tab when our
        // anchor holds focus, so Tab reaches our own key handler (used for tab
        // cycling). In virtual mode we deliberately leave engine navigation
        // *enabled* -- it is inert for our keys (the anchor is not a Button, so
        // arrows/Enter/Tab all fall through to us) but must stay on so native
        // modal dialogs (confirmation / message boxes) keep working.
        if (anchor)
            anchor->setUserString("AcceptTab", "true");
    }

    void Screen::add(Element element)
    {
        if (!element.widget)
            return;

        MyGUI::Widget* widget = element.widget;
        if (mVirtual)
        {
            // The option widgets must never grab key focus themselves -- the
            // anchor owns it -- otherwise native controls (ListBox, ComboBox,
            // ScrollBar) would consume the arrow keys we use for navigation.
            widget->setNeedKeyFocus(false);
        }
        else
        {
            widget->setNeedKeyFocus(true);
            widget->eventKeySetFocus += MyGUI::newDelegate(this, &Screen::onKeyFocus);
            widget->eventKeyButtonPressed += MyGUI::newDelegate(this, &Screen::onKey);
        }

        mElements.push_back(std::move(element));
    }

    void Screen::clear()
    {
        mElements.clear();
        mCurrent = npos;
        mTooltipElement = npos;
        mTooltipLines.clear();
        mHintElement = npos;
    }

    const Element* Screen::find(MyGUI::Widget* widget) const
    {
        const size_t index = indexOf(widget);
        return index == npos ? nullptr : &mElements[index];
    }

    size_t Screen::indexOf(MyGUI::Widget* widget) const
    {
        for (size_t i = 0; i < mElements.size(); ++i)
            if (mElements[i].widget == widget)
                return i;
        return npos;
    }

    const Element* Screen::current() const
    {
        return mCurrent < mElements.size() ? &mElements[mCurrent] : nullptr;
    }

    bool Screen::isUsable(size_t index) const
    {
        if (index >= mElements.size())
            return false;
        MyGUI::Widget* widget = mElements[index].widget;
        return widget && widget->getInheritedVisible() && widget->getInheritedEnabled();
    }

    void Screen::activate(MyGUI::Widget* initialFocus)
    {
        UiManager::instance().setActive(this);

        // Only real-focus screens turn engine navigation off (so its spatial nav
        // can't fight ours). Virtual-focus screens leave it ON: it's inert for
        // our keys -- the non-Button anchor lets arrows/Enter/Tab fall through
        // to us -- but must stay enabled so native modal dialogs keep working.
        if (mDisableEngineNav && !mVirtual)
            MWBase::Environment::get().getWindowManager()->setKeyboardNavigationEnabled(false);

        // In virtual mode, pin real key focus to the anchor and keep it there.
        if (mVirtual && mAnchor)
        {
            // Remember who had focus so we can hand it back on close.
            mPreFocus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
            mAnchor->setNeedKeyFocus(true);
            // Re-bind the key delegate fresh each activation. MyGUI throws
            // "Trying to add same delegate twice" if we += an identical
            // delegate, so always remove any prior binding first (deactivate
            // also clears it, but this is belt-and-suspenders for safety).
            mAnchor->eventKeyButtonPressed -= MyGUI::newDelegate(this, &Screen::onKey);
            mAnchor->eventKeyButtonPressed += MyGUI::newDelegate(this, &Screen::onKey);
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mAnchor);
        }

        const size_t index = initialFocus ? indexOf(initialFocus) : npos;
        if (index != npos && isUsable(index))
            select(index, /*announce=*/true);
        else
            focusFirst();
    }

    void Screen::deactivate()
    {
        UiManager::instance().clear(this);
        if (mDisableEngineNav && !mVirtual)
            MWBase::Environment::get().getWindowManager()->setKeyboardNavigationEnabled(true);
        if (mVirtual && mAnchor)
        {
            // Unhook the anchor key delegate so the next activate() can re-add
            // it without MyGUI throwing "Trying to add same delegate twice".
            mAnchor->eventKeyButtonPressed -= MyGUI::newDelegate(this, &Screen::onKey);

            // Hand key focus back to whoever held it before we opened (e.g. the
            // main-menu Options button), so the opener regains keyboard control
            // and the screen reader announces it. We do this only if the anchor
            // still holds focus -- if a modal or something else grabbed it in
            // the meantime, leave that alone.
            // Hand key focus back to whoever held it before we opened (e.g. the
            // main-menu Options button), so the opener regains keyboard control
            // and the screen reader announces it. Restore when focus currently
            // rests on our anchor OR has been cleared to null -- which is what
            // happens here: hiding the settings window makes the anchor
            // invisible, so MyGUI drops key focus to null right before this
            // runs. Only skip if some *other* widget deliberately took focus.
            MyGUI::Widget* curFocus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
            const bool focusFree = (curFocus == nullptr || curFocus == mAnchor);
            if (mPreFocus && mPreFocus->getInheritedVisible() && mPreFocus->getInheritedEnabled() && focusFree)
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mPreFocus);
        }
        mPreFocus = nullptr;
        // Tear down all per-session state so reopening the screen starts fresh
        // (selection reset, hint cleared) and re-announces the first option.
        clear();
        mYieldedToModal = false;

        // Drop any rereadable announcement so R can't repeat this screen's
        // content from an unrelated screen opened later.
        clearReread();
    }

    void Screen::announce(const Element& element)
    {
        // A dynamic describe() callback fully replaces the label + value
        // announcement (used by screens whose names are computed at runtime).
        if (element.describe)
        {
            say(element.describe());
            return;
        }
        if (!element.label.empty())
            say(element.label);
        if (element.value)
            say(element.value());
    }

    void Screen::announceCurrent()
    {
        if (const Element* element = current())
            announce(*element);
    }

    MyGUI::Widget* Screen::currentWidget() const
    {
        const Element* element = current();
        return element ? element->widget : nullptr;
    }

    void Screen::select(size_t index, bool doAnnounce)
    {
        if (index >= mElements.size())
            return;
        mCurrent = index;

        if (!mVirtual)
        {
            // Drive real MyGUI focus, suppressing the focus-event announcement
            // so we announce exactly once below.
            mSuppressFocusAnnounce = true;
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mElements[index].widget);
            mSuppressFocusAnnounce = false;
        }

        if (doAnnounce)
            announce(mElements[index]);
        resetHint();
    }

    void Screen::focus(MyGUI::Widget* widget)
    {
        const size_t index = indexOf(widget);
        if (index != npos)
            select(index, /*announce=*/true);
    }

    void Screen::focusFirst(bool doAnnounce)
    {
        for (size_t i = 0; i < mElements.size(); ++i)
        {
            if (isUsable(i))
            {
                select(i, doAnnounce);
                return;
            }
        }
        // Nothing focusable right now (e.g. an empty tab).
        mCurrent = npos;
    }

    void Screen::onKeyFocus(MyGUI::Widget* sender, MyGUI::Widget* /*oldFocus*/)
    {
        if (!isActive() || !sender || mSuppressFocusAnnounce || mVirtual)
            return;
        const size_t index = indexOf(sender);
        if (index != npos)
            select(index, /*announce=*/true);
    }

    void Screen::moveSelection(int delta)
    {
        const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(mElements.size());
        if (count == 0)
            return;

        const std::ptrdiff_t start = (mCurrent == npos) ? -1 : static_cast<std::ptrdiff_t>(mCurrent);

        // Step in the requested direction, skipping hidden/disabled options
        // (e.g. options on a non-active tab). At most one full loop so we never
        // spin on an all-hidden screen.
        for (std::ptrdiff_t step = 1; step <= count; ++step)
        {
            std::ptrdiff_t index = start + delta * step;
            index = ((index % count) + count) % count;
            if (isUsable(static_cast<size_t>(index)))
            {
                select(static_cast<size_t>(index), /*announce=*/true);
                return;
            }
        }
    }

    void Screen::changeValue(bool next)
    {
        const Element* element = current();
        if (!element || !element->change)
            return;

        element->change(next);

        // The value changed, so any cached tooltip list is stale.
        mTooltipElement = npos;

        if (element->value)
            say(element->value());
    }

    void Screen::activateCurrent()
    {
        const Element* element = current();
        if (element && element->activate)
            element->activate();
    }

    void Screen::cycleTooltip(bool forward)
    {
        const bool rebuild = (mCurrent != mTooltipElement || mTooltipLines.empty());
        if (rebuild)
        {
            const Element* element = current();
            mTooltipLines = (element && element->tooltips) ? element->tooltips() : std::vector<std::string>{};
            mTooltipElement = mCurrent;
            if (!mTooltipLines.empty())
                mTooltipIndex = forward ? 0 : mTooltipLines.size() - 1;
        }

        if (mTooltipLines.empty())
        {
            say("No description available.");
            return;
        }

        if (!rebuild)
        {
            const size_t count = mTooltipLines.size();
            mTooltipIndex = forward ? (mTooltipIndex + 1) % count : (mTooltipIndex + count - 1) % count;
        }

        // Position indicator goes at the END (project convention).
        std::string line = mTooltipLines[mTooltipIndex];
        if (mTooltipLines.size() > 1)
            line += ". " + std::to_string(mTooltipIndex + 1) + " of " + std::to_string(mTooltipLines.size());
        say(line);
    }

    void Screen::resetHint()
    {
        mHintElement = mCurrent;
        mHintTimer = 0.f;
        mHintSpoken = false;
    }

    void Screen::onFrame(float dt)
    {
        if (!isActive())
            return;

        // Virtual-focus screens are non-modal windows (e.g. Settings). When a
        // native modal dialog (confirmation / interactive message box) pops up
        // *over* such a screen, get out of its way entirely: don't re-pin anchor
        // focus (the dialog's button needs focus) and don't run our hint logic.
        // Engine keyboard navigation stays enabled in virtual mode, so the
        // dialog's own Enter/arrow handling works. We reclaim focus on close.
        //
        // Real-focus screens (e.g. the Race dialog) are themselves WindowModal,
        // so isModalAny() is always true for them -- they must NOT yield, or
        // every key would be swallowed. Only virtual screens treat a modal as
        // "someone else's dialog".
        if (mVirtual && MyGUI::InputManager::getInstance().isModalAny())
        {
            mYieldedToModal = true;
            return;
        }
        if (mYieldedToModal)
        {
            // Modal just closed: reclaim anchor focus and re-announce so the
            // user knows where they are.
            mYieldedToModal = false;
            if (mVirtual && mAnchor)
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mAnchor);
            announceCurrent();
        }

        // Virtual mode: keep real key focus pinned to the anchor so native
        // controls or a stray mouse click can't take over the arrow keys.
        if (mVirtual && mAnchor)
        {
            if (MyGUI::InputManager::getInstance().getKeyFocusWidget() != mAnchor)
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mAnchor);
        }

        // Self-heal the initial selection: when the window first opens, child
        // widgets may not yet report getInheritedVisible()==true, so focusFirst
        // during onOpen can select nothing (mCurrent==npos) and stay silent
        // until the user switches tabs. Once visibility settles, pick + announce
        // the first usable option exactly once.
        if (mCurrent == npos && !mElements.empty())
        {
            for (size_t i = 0; i < mElements.size(); ++i)
            {
                if (isUsable(i))
                {
                    select(i, /*announce=*/true);
                    break;
                }
            }
        }

        if (mHintSpoken || mHintElement == npos || mHintElement != mCurrent)
            return;

        mHintTimer += dt;
        if (mHintTimer < sHintDelay)
            return;

        mHintSpoken = true;
        const Element* element = current();
        if (!element || !element->tooltips)
            return;
        const size_t count = element->tooltips().size();
        if (count == 0)
            return;
        say("Has " + std::to_string(count) + (count == 1 ? " tooltip" : " tooltips") + ". Press T to read.");
    }

    void Screen::onKey(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char /*ch*/)
    {
        onKeyValue(key);
    }

    void Screen::onKeyValue(MyGUI::KeyCode key)
    {
        if (!isActive())
            return;

        // For a virtual-focus (non-modal) screen, a live modal dialog owns the
        // keyboard -- let the engine route keys to it. Real-focus screens are
        // themselves modal, so this guard must not apply to them (it would
        // swallow every key). See the matching note in onFrame().
        if (mVirtual && MyGUI::InputManager::getInstance().isModalAny())
            return;

        // Let the screen's bespoke handler have first refusal.
        if (mExtraKeyHandler && mExtraKeyHandler(key))
            return;

        switch (key.getValue())
        {
            case MyGUI::KeyCode::ArrowDown:
                moveSelection(1);
                break;
            case MyGUI::KeyCode::ArrowUp:
                moveSelection(-1);
                break;
            case MyGUI::KeyCode::ArrowRight:
                changeValue(/*next=*/true);
                break;
            case MyGUI::KeyCode::ArrowLeft:
                changeValue(/*next=*/false);
                break;
            case MyGUI::KeyCode::T:
                cycleTooltip(/*forward=*/!MyGUI::InputManager::getInstance().isShiftPressed());
                break;
            case MyGUI::KeyCode::R:
                // Repeat the last primary/contextual announcement (e.g. a
                // class-quiz question). Per-option labels aren't rereadable --
                // the user re-reads those by arrowing back onto the option.
                reread();
                break;
            case MyGUI::KeyCode::Return:
            case MyGUI::KeyCode::NumpadEnter:
            case MyGUI::KeyCode::Space:
                activateCurrent();
                break;
            default:
                break;
        }
    }
}
