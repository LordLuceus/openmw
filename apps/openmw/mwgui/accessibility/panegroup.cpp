#include "panegroup.hpp"

#include <algorithm>

#include <MyGUI_InputManager.h>

#include "screen.hpp"
#include "speech.hpp"

namespace MWGui::A11y
{
    PaneGroup& PaneGroup::instance()
    {
        static PaneGroup sInstance;
        return sInstance;
    }

    std::size_t PaneGroup::indexOf(const Screen* screen) const
    {
        for (std::size_t i = 0; i < mPanes.size(); ++i)
            if (mPanes[i].screen == screen)
                return i;
        return npos;
    }

    bool PaneGroup::contains(const Screen* screen) const
    {
        return indexOf(screen) != npos;
    }

    void PaneGroup::enrol(Screen* screen, std::string label, int order)
    {
        if (!screen)
            return;

        if (const std::size_t existing = indexOf(screen); existing != npos)
        {
            mPanes[existing].label = std::move(label);
            mPanes[existing].order = order;
        }
        else
        {
            mPanes.push_back({ screen, std::move(label), order });
        }

        // Keep panes ordered so Tab walks them in a stable, meaningful sequence
        // (e.g. Stats then Inventory) no matter which window's onOpen ran first.
        std::sort(mPanes.begin(), mPanes.end(),
            [](const Pane& a, const Pane& b) { return a.order < b.order; });
    }

    void PaneGroup::withdraw(Screen* screen)
    {
        if (const std::size_t i = indexOf(screen); i != npos)
            mPanes.erase(mPanes.begin() + i);
    }

    void PaneGroup::maybeActivateInitial(Screen* screen)
    {
        if (mPanes.empty())
            return;
        // A modal dialog (e.g. a spell-delete confirmation) opened over our
        // panes owns input and has suspended the pane underneath. Don't grab
        // focus back while it's up, or we'd steal keys from the modal and the
        // pane would start re-announcing behind it. The modal resumes the pane
        // itself on close.
        if (MyGUI::InputManager::getInstance().isModalAny())
            return;
        // If some pane is already active (the user has started navigating),
        // don't steal focus.
        for (const Pane& pane : mPanes)
            if (pane.screen->isActive())
                return;

        // Work out which pane should claim focus: the one the user was last on
        // (if it's still enrolled), else the lowest-order pane.
        std::size_t target = 0;
        if (mLastActiveOrder >= 0)
        {
            for (std::size_t i = 0; i < mPanes.size(); ++i)
            {
                if (mPanes[i].order == mLastActiveOrder)
                {
                    target = i;
                    break;
                }
            }
        }

        // Only the target pane self-activates (and announces), so the call from
        // any other pane's onFrame is a no-op.
        if (mPanes[target].screen != screen)
            return;

        screen->activate();
        mLastActiveOrder = mPanes[target].order;
    }

    bool PaneGroup::cycle(int delta)
    {
        if (mPanes.size() < 2 || delta == 0)
            return false;

        // Find the currently active pane.
        std::size_t active = npos;
        for (std::size_t i = 0; i < mPanes.size(); ++i)
        {
            if (mPanes[i].screen->isActive())
            {
                active = i;
                break;
            }
        }
        if (active == npos)
            return false;

        const std::size_t count = mPanes.size();
        const std::size_t step = (delta > 0) ? 1 : count - 1; // -1 mod count
        const std::size_t next = (active + step) % count;
        if (next == active)
            return false;

        mPanes[active].screen->suspend();
        mPanes[next].screen->resume();
        mLastActiveOrder = mPanes[next].order;

        // Announce the pane we landed on, then its current option (resume()
        // re-announces the option; prefix the pane name so the user knows the
        // context switched).
        say(mPanes[next].label, /*interrupt=*/true);
        mPanes[next].screen->announceCurrent();
        return true;
    }
}
