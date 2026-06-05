#include "screen.hpp"

#include <MyGUI_EditBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_Widget.h>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/windowmanager.hpp"

#include "editfield.hpp"
#include "panegroup.hpp"
#include "speech.hpp"
#include "uimanager.hpp"

namespace
{
    // Append a "N of M" position indicator to a tooltip line, avoiding a double
    // period when the text already ends in sentence punctuation.
    std::string withPosition(std::string line, size_t index, size_t count)
    {
        if (count > 1)
        {
            const char last = line.empty() ? '\0' : line.back();
            const bool endsWithPunct = (last == '.' || last == '!' || last == '?');
            line += (endsWithPunct ? " " : ". ") + std::to_string(index + 1) + " of " + std::to_string(count);
        }
        return line;
    }
}

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
        MyGUI::Widget* widget = element.widget;

        // Widget-less elements are allowed only in virtual-focus mode, where
        // navigation is tracked purely by index and never drives MyGUI focus.
        // They model pure-text options that have no backing control -- e.g. a
        // paragraph of book text, a line of dialogue, or a message-box prompt.
        // In real-focus mode an element must have a widget to receive focus.
        if (!widget)
        {
            if (mVirtual)
                mElements.push_back(std::move(element));
            return;
        }

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
        // In real-focus mode, add() bound key/focus delegates to each option
        // widget. Unbind them before dropping the elements, otherwise a later
        // rebuild that re-adds the same widget would try to += an identical
        // delegate -- MyGUI throws "Trying to add same delegate twice", which
        // aborts the rebuild mid-way (empty/partial list -> dead navigation and
        // no speech). Virtual-focus options carry no per-widget delegates.
        if (!mVirtual)
        {
            for (const Element& element : mElements)
            {
                if (element.widget)
                {
                    element.widget->eventKeySetFocus -= MyGUI::newDelegate(this, &Screen::onKeyFocus);
                    element.widget->eventKeyButtonPressed -= MyGUI::newDelegate(this, &Screen::onKey);
                }
            }
        }

        mElements.clear();
        mCurrent = npos;
        mTooltipElement = npos;
        mTooltipLines.clear();
        mHintElement = npos;
        mHintSubItem = npos;
        mSubOpen = false;
        mSubItems.clear();
        mSubCurrent = npos;
        mSubTooltipItem = npos;
        mSubTooltipLines.clear();
        mEditMode = false;
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
        // A widget-less element (virtual mode only) is a pure-text option with
        // no control to gate on; it is always navigable.
        if (!widget)
            return true;
        return widget->getInheritedVisible() && widget->getInheritedEnabled();
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

    void Screen::suspend()
    {
        if (!isActive())
            return;

        // Step down as the active screen but keep our elements + selection so
        // resume() can return the user exactly where they were.
        UiManager::instance().clear(this);

        if (mDisableEngineNav && !mVirtual)
            MWBase::Environment::get().getWindowManager()->setKeyboardNavigationEnabled(true);

        // Drop the anchor key delegate (virtual) so the pane we're switching to
        // can pin focus to its own anchor without us fighting it in onFrame().
        if (mVirtual && mAnchor)
            mAnchor->eventKeyButtonPressed -= MyGUI::newDelegate(this, &Screen::onKey);
    }

    void Screen::resume()
    {
        if (isActive())
            return;

        UiManager::instance().setActive(this);

        if (mDisableEngineNav && !mVirtual)
            MWBase::Environment::get().getWindowManager()->setKeyboardNavigationEnabled(false);

        if (mVirtual && mAnchor)
        {
            mAnchor->setNeedKeyFocus(true);
            mAnchor->eventKeyButtonPressed -= MyGUI::newDelegate(this, &Screen::onKey);
            mAnchor->eventKeyButtonPressed += MyGUI::newDelegate(this, &Screen::onKey);
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mAnchor);
        }

        // If we never had a selection (suspended before the list settled), pick
        // the first usable option now; otherwise keep the one we left on. The
        // caller announces, so don't announce here.
        if (mCurrent == npos)
            focusFirst(/*announce=*/false);
    }

    void Screen::announce(const Element& element, bool withSection)
    {
        // The first say() of this announcement may interrupt prior speech (so a
        // rapid re-announcement replaces a stale one rather than queueing behind
        // it); subsequent parts always queue so the whole announcement stays
        // together. The flag is one-shot.
        const bool interrupt = mAnnounceInterrupt;
        mAnnounceInterrupt = false;
        bool first = true;
        const auto speak = [&](std::string_view text) {
            say(text, /*interrupt=*/first && interrupt);
            first = false;
        };

        // Announce the section name when focus has crossed into a new one
        // (computed by the caller). A dynamic describe() option is announced
        // verbatim, so prefix the section separately.
        if (withSection && !element.section.empty())
            speak(element.section + ":");

        // A dynamic describe() callback fully replaces the label + value
        // announcement (used by screens whose names are computed at runtime).
        if (element.describe)
        {
            speak(element.describe());
            return;
        }
        if (!element.label.empty())
            speak(element.label);
        if (element.value)
            speak(element.value());
    }

    void Screen::selectIndexInterrupting(size_t index)
    {
        if (index < mElements.size() && isUsable(index))
        {
            mAnnounceInterrupt = true;
            select(index, /*announce=*/true);
            mAnnounceInterrupt = false; // belt-and-suspenders if select didn't announce
        }
    }

    void Screen::announceCurrent()
    {
        if (const Element* element = current())
            announce(*element);
    }

    std::string Screen::currentLabel() const
    {
        const Element* element = current();
        return element ? element->label : std::string();
    }

    bool Screen::selectByLabel(std::string_view label, bool doAnnounce)
    {
        for (size_t i = 0; i < mElements.size(); ++i)
        {
            if (mElements[i].label == label && isUsable(i))
            {
                select(i, doAnnounce);
                return true;
            }
        }
        return false;
    }

    void Screen::selectIndex(size_t index, bool doAnnounce)
    {
        if (index < mElements.size() && isUsable(index))
            select(index, doAnnounce);
    }

    bool Screen::selectMatchingLabel(
        int delta, const std::function<bool(std::string_view)>& pred, bool doAnnounce)
    {
        if (delta == 0 || !pred)
            return false;

        const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(mElements.size());
        if (count == 0)
            return false;

        // Start from the current selection (or one before the start, so a
        // forward search from "no selection" can land on index 0). Search
        // outward without wrapping -- the list is ordered, and wrapping past the
        // ends would surprise the user mid-navigation.
        const std::ptrdiff_t start = (mCurrent == npos) ? -1 : static_cast<std::ptrdiff_t>(mCurrent);
        for (std::ptrdiff_t index = start + delta; index >= 0 && index < count; index += delta)
        {
            const size_t i = static_cast<size_t>(index);
            if (isUsable(i) && pred(mElements[i].label))
            {
                select(i, doAnnounce);
                return true;
            }
        }
        return false;
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
        // Selecting a (possibly different) top-level option collapses any open
        // submenu without re-announcing the parent (we're about to announce the
        // new selection ourselves).
        if (mSubOpen)
        {
            mSubOpen = false;
            mSubItems.clear();
            mSubCurrent = npos;
            mSubTooltipItem = npos;
            mSubTooltipLines.clear();
        }
        // Determine whether we've crossed into a new section, comparing against
        // the previously focused option (so moving back up within a section
        // doesn't re-announce it). Sections only matter when the new option
        // actually has one.
        const size_t previous = mCurrent;
        const bool sectionChanged = !mElements[index].section.empty()
            && (previous >= mElements.size() || mElements[previous].section != mElements[index].section);

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
            announce(mElements[index], sectionChanged);
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

    void Screen::jumpSection(int delta)
    {
        const size_t count = mElements.size();
        if (count == 0 || mCurrent >= count)
        {
            // No current selection: fall back to a normal move.
            moveSelection(delta);
            return;
        }

        const std::string& cur = mElements[mCurrent].section;
        if (delta > 0)
        {
            // First usable item, scanning forward, whose section differs from
            // the current one -- the start of the next section.
            for (size_t i = mCurrent + 1; i < count; ++i)
            {
                if (isUsable(i) && mElements[i].section != cur)
                {
                    select(i, /*announce=*/true);
                    return;
                }
            }
        }
        else
        {
            // Walk back to the first item of the current section; if already
            // there, continue to the first item of the previous section.
            size_t start = mCurrent;
            while (start > 0 && mElements[start - 1].section == cur)
                --start;
            if (start > 0)
            {
                const std::string& prevSection = mElements[start - 1].section;
                size_t target = start - 1;
                while (target > 0 && mElements[target - 1].section == prevSection)
                    --target;
                // Skip to the first usable item of that section.
                while (target < start && !isUsable(target))
                    ++target;
                select(target, /*announce=*/true);
                return;
            }
            if (start != mCurrent)
                select(start, /*announce=*/true);
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

        // Restart the delayed "has N tooltips" hint: the new value (e.g. a
        // different race or birthsign) may have a different tooltip count, so
        // re-announce it after the linger just as a fresh selection would.
        resetHint();
    }

    void Screen::activateCurrent()
    {
        const Element* element = current();
        if (!element)
            return;
        // An editable text field takes precedence: Enter begins editing.
        if (element->edit)
        {
            enterEditMode();
            return;
        }
        // An expandable submenu takes precedence over a plain activate handler.
        if (element->children)
        {
            openSubmenu();
            return;
        }
        if (element->activate)
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
        say(withPosition(mTooltipLines[mTooltipIndex], mTooltipIndex, mTooltipLines.size()));
    }

    void Screen::openSubmenu()
    {
        const Element* element = current();
        if (!element || !element->children)
            return;

        mSubItems = element->children();
        if (mSubItems.empty())
        {
            say("Empty.");
            return;
        }

        mSubOpen = true;
        mSubTooltipItem = npos;
        mSubTooltipLines.clear();
        // Announce the first item with its section prefix.
        announceSubItem(0, /*withSection=*/true);
    }

    void Screen::closeSubmenu(bool announceParent)
    {
        if (!mSubOpen)
            return;
        mSubOpen = false;
        mSubItems.clear();
        mSubCurrent = npos;
        mSubTooltipItem = npos;
        mSubTooltipLines.clear();
        // Re-arm the parent option's hint so the "Press Enter to expand"
        // affordance is offered again after the linger.
        resetHint();
        if (announceParent)
            announceCurrent();
    }

    void Screen::enterEditMode()
    {
        const Element* element = current();
        if (!element || !element->edit)
            return;
        mEditMode = true;
        // Cancel the delayed hint -- it's irrelevant while editing.
        mHintElement = npos;
        // In virtual-focus mode our key delegate is bound only to the anchor,
        // but edit mode pins real focus to the edit box (so it can receive typed
        // characters) -- which means key events now arrive at the box, not the
        // anchor. Bind onKey to the box too so Escape (to leave edit mode) still
        // reaches us. Real-focus mode already has the delegate on every option
        // widget, so this is virtual-only.
        if (mVirtual && element->edit->widget())
        {
            MyGUI::Widget* box = element->edit->widget();
            box->eventKeyButtonPressed -= MyGUI::newDelegate(this, &Screen::onKey);
            box->eventKeyButtonPressed += MyGUI::newDelegate(this, &Screen::onKey);
        }
        // Activate spoken editing feedback and announce the current contents so
        // the user knows what they're editing.
        element->edit->setActive(true);
        say("Editing. Press Escape when done.");
        element->edit->announceContents();
    }

    void Screen::exitEditMode()
    {
        if (!mEditMode)
            return;
        mEditMode = false;
        const Element* element = current();
        if (element && element->edit)
        {
            element->edit->setActive(false);
            // Undo the virtual-mode edit-box key binding added in enterEditMode
            // so a later rebuild/activate doesn't double-add the delegate.
            if (mVirtual && element->edit->widget())
                element->edit->widget()->eventKeyButtonPressed -= MyGUI::newDelegate(this, &Screen::onKey);
        }
        // CRITICAL: a MyGUI EditBox releases key focus when it sees Escape (it
        // treats it as cancel). In real-focus mode the Screen receives keys only
        // via the focused widget, so with focus dropped to null all navigation
        // would die. Re-pin focus to the current option's widget so arrows keep
        // reaching us. (Virtual mode keeps focus on the anchor, so skip it.)
        if (!mVirtual && element && element->widget)
        {
            mSuppressFocusAnnounce = true;
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(element->widget);
            mSuppressFocusAnnounce = false;
        }
        // Re-announce the option (label + updated value) and re-arm its hint.
        announceCurrent();
        resetHint();
    }

    bool Screen::consumeEscape()
    {
        const bool consumed = mEscapeConsumed;
        mEscapeConsumed = false;
        return consumed;
    }

    void Screen::announceSubItem(size_t index, bool withSection)
    {
        if (index >= mSubItems.size())
            return;
        // Remember where we came from so we can tell whether we've crossed into
        // a new section. Compare against the *previously focused* item, not the
        // item physically above -- otherwise moving back up within a section
        // would wrongly re-announce it, and moving up across a boundary would
        // fail to announce the section we just entered.
        const size_t previous = mSubCurrent;
        mSubCurrent = index;
        // Tooltip state is per-item; invalidate so the next T rebuilds it.
        mSubTooltipItem = npos;

        const SubItem& item = mSubItems[index];
        // Speak the section name when the submenu first opens (withSection), or
        // whenever the new item's section differs from the one we just left.
        const bool sectionChanged
            = withSection || previous >= mSubItems.size() || mSubItems[previous].section != item.section;

        if (sectionChanged && !item.section.empty())
            say(item.section + ": " + item.label);
        else
            say(item.label);

        // Arm the delayed "has N tooltips" hint for this sub-item.
        resetHint();
    }

    void Screen::moveSubSelection(int delta)
    {
        const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(mSubItems.size());
        if (count == 0)
            return;
        const std::ptrdiff_t start = (mSubCurrent == npos) ? -1 : static_cast<std::ptrdiff_t>(mSubCurrent);
        std::ptrdiff_t index = start + delta;
        // Clamp at the ends rather than wrapping: a flat read-only list reads
        // more naturally when Up at the top / Down at the bottom does nothing.
        if (index < 0 || index >= count)
            return;
        announceSubItem(static_cast<size_t>(index), /*withSection=*/false);
    }

    void Screen::jumpSubSection(int delta)
    {
        const size_t count = mSubItems.size();
        if (count == 0 || mSubCurrent >= count)
            return;

        const std::string& cur = mSubItems[mSubCurrent].section;
        if (delta > 0)
        {
            // Find the first item whose section differs from the current one,
            // scanning forward. That's the start of the next section.
            for (size_t i = mSubCurrent + 1; i < count; ++i)
            {
                if (mSubItems[i].section != cur)
                {
                    announceSubItem(i, /*withSection=*/true);
                    return;
                }
            }
        }
        else
        {
            // Walk back to the first item of the current section. If we're
            // already there, keep going to the first item of the previous one.
            size_t start = mSubCurrent;
            while (start > 0 && mSubItems[start - 1].section == cur)
                --start;
            if (start > 0)
            {
                const std::string& prevSection = mSubItems[start - 1].section;
                size_t target = start - 1;
                while (target > 0 && mSubItems[target - 1].section == prevSection)
                    --target;
                announceSubItem(target, /*withSection=*/true);
                return;
            }
            // Already in the first section: jump to its first item if we're not
            // there yet.
            if (start != mSubCurrent)
            {
                announceSubItem(start, /*withSection=*/true);
                return;
            }
        }
    }

    void Screen::jumpSubEdge(bool last)
    {
        if (mSubItems.empty())
            return;
        const size_t index = last ? mSubItems.size() - 1 : 0;
        announceSubItem(index, /*withSection=*/true);
    }

    void Screen::cycleSubTooltip(bool forward)
    {
        if (mSubCurrent >= mSubItems.size())
            return;
        const bool rebuild = (mSubCurrent != mSubTooltipItem || mSubTooltipLines.empty());
        if (rebuild)
        {
            const SubItem& item = mSubItems[mSubCurrent];
            mSubTooltipLines = item.tooltips ? item.tooltips() : std::vector<std::string>{};
            mSubTooltipItem = mSubCurrent;
            if (!mSubTooltipLines.empty())
                mSubTooltipIndex = forward ? 0 : mSubTooltipLines.size() - 1;
        }

        if (mSubTooltipLines.empty())
        {
            say("No description available.");
            return;
        }

        if (!rebuild)
        {
            const size_t count = mSubTooltipLines.size();
            mSubTooltipIndex = forward ? (mSubTooltipIndex + 1) % count : (mSubTooltipIndex + count - 1) % count;
        }

        say(withPosition(mSubTooltipLines[mSubTooltipIndex], mSubTooltipIndex, mSubTooltipLines.size()));
    }

    void Screen::resetHint()
    {
        mHintElement = mCurrent;
        mHintSubItem = mSubCurrent;
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
            // While editing a text field, key focus must rest on the edit box
            // so typed characters reach it -- pinning to the anchor would
            // swallow them. Otherwise keep focus on the anchor so navigation
            // keys reach us and a stray click can't steal the arrows.
            MyGUI::Widget* want = mAnchor;
            if (mEditMode)
            {
                if (const Element* element = current(); element && element->edit && element->edit->widget())
                    want = element->edit->widget();
            }
            if (MyGUI::InputManager::getInstance().getKeyFocusWidget() != want)
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(want);
        }

        // Real mode: self-heal dropped key focus. A MyGUI EditBox releases key
        // focus to null when it processes Escape (it treats Escape as cancel).
        // Since real-focus navigation relies on the focused option widget
        // delivering key events to us, a null focus would silently kill all
        // navigation. This check runs *after* the EditBox has finished
        // swallowing the keystroke (re-pinning inside the key handler doesn't
        // work -- the box clobbers it right after), so it reliably recovers.
        if (!mVirtual)
        {
            MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
            MyGUI::Widget* want = currentWidget();
            if (want && focus != want)
            {
                // Focus drifted off our current option. If we were editing, that
                // Escape (which the box ate) also ended editing -- leave edit
                // mode cleanly, which re-pins focus and re-announces. Otherwise
                // just restore focus silently so navigation keeps working.
                if (mEditMode)
                    exitEditMode();
                else
                {
                    mSuppressFocusAnnounce = true;
                    MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(want);
                    mSuppressFocusAnnounce = false;
                }
            }
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

        // The hint is valid only while focus hasn't moved since it was armed --
        // both at the top level (mCurrent) and, in a submenu, the sub-item.
        if (mHintSpoken || mHintElement == npos || mHintElement != mCurrent || mHintSubItem != mSubCurrent)
            return;

        mHintTimer += dt;
        if (mHintTimer < sHintDelay)
            return;

        mHintSpoken = true;

        if (mSubOpen)
        {
            // Hint the focused sub-item's tooltip count.
            if (mSubCurrent >= mSubItems.size())
                return;
            const SubItem& item = mSubItems[mSubCurrent];
            const size_t count = item.tooltips ? item.tooltips().size() : 0;
            if (count == 0)
                return;
            say("Has " + std::to_string(count) + (count == 1 ? " tooltip" : " tooltips") + ". Press T to read.");
            return;
        }

        const Element* element = current();
        if (!element)
            return;
        // An expandable option advertises how to open it; otherwise hint the
        // tooltip count.
        if (element->children)
        {
            say("Press Enter to expand.");
            return;
        }
        if (!element->tooltips)
            return;
        const size_t count = element->tooltips().size();
        if (count == 0)
            return;
        say("Has " + std::to_string(count) + (count == 1 ? " tooltip" : " tooltips") + ". Press T to read.");
    }

    bool Screen::consumedKey() const
    {
        // When a modal dialog (e.g. the count picker) is open over a virtual
        // screen, our anchor never received the key -- mKeyConsumed is stale from
        // the last key we genuinely handled. Reporting that stale value would
        // tell the engine the key was consumed and swallow the modal's own
        // Escape/Enter handling (so the dialog couldn't be cancelled). While
        // yielding to a modal we never consume keys.
        if (mVirtual && MyGUI::InputManager::getInstance().isModalAny())
            return false;
        return mKeyConsumed;
    }

    void Screen::onKey(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char /*ch*/)
    {
        onKeyValue(key);
    }

    void Screen::onKeyValue(MyGUI::KeyCode key)
    {
        // Assume the key is ours; individual branches that deliberately let a
        // key through to the engine (e.g. a top-level Escape that should close
        // the window) clear this. Read by consumedKey().
        mKeyConsumed = true;

        if (!isActive())
        {
            mKeyConsumed = false;
            return;
        }

        // For a virtual-focus (non-modal) screen, a live modal dialog owns the
        // keyboard -- let the engine route keys to it. Real-focus screens are
        // themselves modal, so this guard must not apply to them (it would
        // swallow every key). See the matching note in onFrame().
        if (mVirtual && MyGUI::InputManager::getInstance().isModalAny())
        {
            mKeyConsumed = false;
            return;
        }

        // While editing a text field, the EditField (the focused widget) owns
        // every key for text editing; we only intercept Escape to leave edit
        // mode. Note: onKey arrives from the focused edit box itself in
        // real-focus mode, so returning here simply lets MyGUI's own edit
        // handling run for that same keystroke.
        if (mEditMode)
        {
            if (key == MyGUI::KeyCode::Escape)
            {
                // Latch so the owning modal's exit() can tell this Escape was
                // for leaving edit mode (don't close the dialog).
                mEscapeConsumed = true;
                exitEditMode();
            }
            return;
        }

        // Tab / Shift+Tab switch between sibling panes shown together in one GUI
        // mode (e.g. Stats <-> Inventory). PaneGroup::cycle() is a no-op unless
        // this screen is part of a multi-pane group, so this is inert for
        // ordinary single-window screens -- except Settings, which uses Tab for
        // its own tab cycling and is never enrolled in a PaneGroup, so its
        // extra-key handler (below) still wins there. Skipped while editing a
        // text field (Tab should reach the field / be ignored, not switch panes).
        if (!mEditMode && key == MyGUI::KeyCode::Tab && PaneGroup::instance().contains(this))
        {
            const int delta = MyGUI::InputManager::getInstance().isShiftPressed() ? -1 : 1;
            if (PaneGroup::instance().cycle(delta))
                return;
        }

        // Let the screen's bespoke handler have first refusal.
        if (mExtraKeyHandler && mExtraKeyHandler(key))
            return;

        // While an expandable submenu is open, navigation operates on its child
        // items. Up/Down move between children, T cycles a child's tooltips, and
        // Escape collapses back to the parent option on the main screen.
        if (mSubOpen)
        {
            const bool ctrl = MyGUI::InputManager::getInstance().isControlPressed();
            switch (key.getValue())
            {
                case MyGUI::KeyCode::ArrowDown:
                    // Ctrl+Down jumps to the next section; Down moves one item.
                    if (ctrl)
                        jumpSubSection(1);
                    else
                        moveSubSelection(1);
                    break;
                case MyGUI::KeyCode::ArrowUp:
                    if (ctrl)
                        jumpSubSection(-1);
                    else
                        moveSubSelection(-1);
                    break;
                case MyGUI::KeyCode::Home:
                    jumpSubEdge(/*last=*/false);
                    break;
                case MyGUI::KeyCode::End:
                    jumpSubEdge(/*last=*/true);
                    break;
                case MyGUI::KeyCode::T:
                    cycleSubTooltip(/*forward=*/!MyGUI::InputManager::getInstance().isShiftPressed());
                    break;
                case MyGUI::KeyCode::Escape:
                    // Also latch for the modal exit() path (consumeEscape()).
                    // For a non-modal host, consumedKey() additionally stops the
                    // engine turning this Escape into A_GameMenu, which would
                    // close the whole window instead of just collapsing here.
                    mEscapeConsumed = true;
                    closeSubmenu();
                    break;
                case MyGUI::KeyCode::ArrowLeft:
                    closeSubmenu();
                    break;
                default:
                    mKeyConsumed = false;
                    break;
            }
            return;
        }

        const bool ctrl = MyGUI::InputManager::getInstance().isControlPressed();
        switch (key.getValue())
        {
            case MyGUI::KeyCode::ArrowDown:
                // Ctrl+Down jumps to the next section; Down moves one item.
                if (ctrl)
                    jumpSection(1);
                else
                    moveSelection(1);
                break;
            case MyGUI::KeyCode::ArrowUp:
                if (ctrl)
                    jumpSection(-1);
                else
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
                // Unhandled at the top level (notably Escape): let the engine
                // have it so e.g. Escape closes the window as usual.
                mKeyConsumed = false;
                break;
        }
    }
}
