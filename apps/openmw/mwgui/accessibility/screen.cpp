#include "screen.hpp"

#include <algorithm>

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

    void Screen::add(Element element)
    {
        if (!element.widget)
            return;

        MyGUI::Widget* widget = element.widget;
        widget->setNeedKeyFocus(true);
        widget->eventKeySetFocus += MyGUI::newDelegate(this, &Screen::onKeyFocus);
        widget->eventKeyButtonPressed += MyGUI::newDelegate(this, &Screen::onKey);

        mElements.push_back(std::move(element));
    }

    void Screen::clear()
    {
        mElements.clear();
        mTooltipWidget = nullptr;
        mTooltipLines.clear();
        mHintWidget = nullptr;
    }

    const Element* Screen::find(MyGUI::Widget* widget) const
    {
        auto it = std::find_if(mElements.begin(), mElements.end(),
            [widget](const Element& e) { return e.widget == widget; });
        return it == mElements.end() ? nullptr : &*it;
    }

    const Element* Screen::focusedElement() const
    {
        return find(MyGUI::InputManager::getInstance().getKeyFocusWidget());
    }

    void Screen::activate(MyGUI::Widget* initialFocus)
    {
        UiManager::instance().setActive(this);

        if (mDisableEngineNav)
            MWBase::Environment::get().getWindowManager()->setKeyboardNavigationEnabled(false);

        MyGUI::Widget* target = initialFocus;
        if (!target && !mElements.empty())
            target = mElements.front().widget;
        if (!target)
            return;

        setFocus(target);
    }

    void Screen::deactivate()
    {
        UiManager::instance().clear(this);
        if (mDisableEngineNav)
            MWBase::Environment::get().getWindowManager()->setKeyboardNavigationEnabled(true);
        mHintWidget = nullptr;
    }

    void Screen::announce(const Element& element, bool withValue)
    {
        if (!element.label.empty())
            say(element.label);
        if (withValue && element.value)
            say(element.value());
    }

    void Screen::announceFocused()
    {
        if (const Element* element = focusedElement())
            announce(*element, /*withValue=*/true);
    }

    void Screen::setFocus(MyGUI::Widget* widget)
    {
        const Element* element = find(widget);
        if (!element)
            return;
        // Suppress the focus-event announcement we're about to trigger, then
        // announce exactly once here. This keeps a single, predictable
        // announcement whether or not setKeyFocusWidget actually fires
        // eventKeySetFocus (it won't if the widget is already focused).
        mSuppressFocusAnnounce = true;
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(widget);
        mSuppressFocusAnnounce = false;

        announce(*element, /*withValue=*/true);
        resetHint(widget);
    }

    void Screen::onKeyFocus(MyGUI::Widget* sender, MyGUI::Widget* /*oldFocus*/)
    {
        if (!isActive() || !sender || mSuppressFocusAnnounce)
            return;
        if (const Element* element = find(sender))
        {
            announce(*element, /*withValue=*/true);
            resetHint(sender);
        }
    }

    void Screen::focus(MyGUI::Widget* widget)
    {
        setFocus(widget);
    }

    void Screen::moveFocus(int delta)
    {
        if (mElements.empty())
            return;
        MyGUI::Widget* current = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        auto it = std::find_if(mElements.begin(), mElements.end(),
            [current](const Element& e) { return e.widget == current; });

        size_t index = 0;
        if (it != mElements.end())
        {
            const size_t count = mElements.size();
            const size_t cur = static_cast<size_t>(it - mElements.begin());
            index = (cur + count + delta) % count;
        }
        focus(mElements[index].widget);
    }

    void Screen::changeValue(bool next)
    {
        const Element* element = focusedElement();
        if (!element || !element->change)
            return;

        element->change(next);

        // The value changed, so any cached tooltip list is stale.
        mTooltipWidget = nullptr;

        if (element->value)
            say(element->value());
    }

    void Screen::activateFocused()
    {
        const Element* element = focusedElement();
        if (element && element->activate)
            element->activate();
    }

    void Screen::cycleTooltip(bool forward)
    {
        MyGUI::Widget* focusWidget = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        const Element* element = find(focusWidget);

        const bool rebuild = (focusWidget != mTooltipWidget || mTooltipLines.empty());
        if (rebuild)
        {
            mTooltipLines = (element && element->tooltips) ? element->tooltips() : std::vector<std::string>{};
            mTooltipWidget = focusWidget;
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

    void Screen::resetHint(MyGUI::Widget* widget)
    {
        mHintWidget = widget;
        mHintTimer = 0.f;
        mHintSpoken = false;
    }

    void Screen::onFrame(float dt)
    {
        if (!isActive() || mHintSpoken || !mHintWidget)
            return;

        MyGUI::Widget* focusWidget = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        if (focusWidget != mHintWidget)
        {
            mHintWidget = nullptr; // focus moved without an event; cancel
            return;
        }

        mHintTimer += dt;
        if (mHintTimer < sHintDelay)
            return;

        mHintSpoken = true;
        const Element* element = find(mHintWidget);
        if (!element || !element->tooltips)
            return;
        const size_t count = element->tooltips().size();
        if (count == 0)
            return;
        say("Has " + std::to_string(count) + (count == 1 ? " tooltip" : " tooltips") + ". Press T to read.");
    }

    void Screen::onKey(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char /*ch*/)
    {
        if (!isActive())
            return;

        // Let the screen's bespoke handler have first refusal.
        if (mExtraKeyHandler && mExtraKeyHandler(key))
            return;

        switch (key.getValue())
        {
            case MyGUI::KeyCode::ArrowDown:
                moveFocus(1);
                break;
            case MyGUI::KeyCode::ArrowUp:
                moveFocus(-1);
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
            case MyGUI::KeyCode::Return:
            case MyGUI::KeyCode::NumpadEnter:
            case MyGUI::KeyCode::Space:
                activateFocused();
                break;
            default:
                break;
        }
    }
}
