#include "scanner.hpp"

#include <SDL_keycode.h>
#include <SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <MyGUI_LanguageManager.h>

#include <components/accessibility/accessibilitymanager.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcrea.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/cellref.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"
#include "../mwworld/worldmodel.hpp"

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    // Morrowind world units: 64 units = 1 yard = 0.9144 metres, so
    // ~70 units per metre.
    constexpr float kUnitsPerMetre = 69.99f;

    std::string formatDistance(float units)
    {
        float metres = units / kUnitsPerMetre;
        char buf[32];
        if (metres < 10.0f)
            std::snprintf(buf, sizeof(buf), "%.1f metres", metres);
        else
            std::snprintf(buf, sizeof(buf), "%d metres", static_cast<int>(metres + 0.5f));
        return buf;
    }

    // Player position-vector "forward" is along +Y in OpenMW's coordinate
    // system, and yaw rotates around Z. A target bearing relative to the
    // player is computed as the angle between (target - player) and the
    // player's facing direction.

    const char* categoryName(MWAccessibility::Category cat)
    {
        switch (cat)
        {
            case MWAccessibility::Category::Npcs:
                return "NPCs";
            case MWAccessibility::Category::Doors:
                return "Doors";
            case MWAccessibility::Category::Containers:
                return "Containers";
            case MWAccessibility::Category::Count:
                break;
        }
        return "?";
    }

    bool matchesCategory(const MWWorld::Ptr& ptr, MWAccessibility::Category cat)
    {
        unsigned int type = ptr.getType();
        switch (cat)
        {
            case MWAccessibility::Category::Npcs:
                return type == ESM::NPC::sRecordId || type == ESM::Creature::sRecordId;
            case MWAccessibility::Category::Doors:
                return type == ESM::Door::sRecordId;
            case MWAccessibility::Category::Containers:
                return type == ESM::Container::sRecordId;
            case MWAccessibility::Category::Count:
                break;
        }
        return false;
    }

    // 8-point compass label for a relative bearing in radians ([-PI, PI]).
    // 0 = ahead, +PI/2 = right, +/-PI = behind, -PI/2 = left.
    const char* bearingLabel(float relYaw)
    {
        const float octant = kPi / 8.0f;
        if (relYaw > -octant && relYaw < octant)
            return "ahead";
        if (relYaw >= octant && relYaw < 3 * octant)
            return "ahead-right";
        if (relYaw >= 3 * octant && relYaw < 5 * octant)
            return "right";
        if (relYaw >= 5 * octant && relYaw < 7 * octant)
            return "behind-right";
        if (relYaw >= 7 * octant || relYaw <= -7 * octant)
            return "behind";
        if (relYaw <= -5 * octant)
            return "behind-left";
        if (relYaw <= -3 * octant)
            return "left";
        return "ahead-left";
    }

    std::string objectDisplayName(const MWWorld::Ptr& ptr)
    {
        std::string_view name = ptr.getClass().getName(ptr);
        if (!name.empty())
            return std::string(name);
        // Fall back to the refId for unnamed objects (rare for the
        // categories we care about, but harmless to handle).
        return ptr.getCellRef().getRefId().toDebugString();
    }

    void appendDoorDestination(const MWWorld::Ptr& ptr, std::string& out)
    {
        if (ptr.getType() != ESM::Door::sRecordId)
            return;
        ESM::RefId destCell = ptr.getCellRef().getDestCell();
        if (destCell.empty())
            return;
        // The door class formats its destination as "#{sCell=<name>}";
        // we look up the cell name directly via WorldModel.
        std::string_view dest = MWBase::Environment::get().getWorld()->getCellName(
            &MWBase::Environment::get().getWorldModel()->getCell(destCell));
        if (!dest.empty())
        {
            out += ", to ";
            out += dest;
        }
    }
}

namespace MWAccessibility
{
    Scanner::Scanner() = default;
    Scanner::~Scanner() = default;

    Scanner& Scanner::instance()
    {
        static Scanner sInstance;
        return sInstance;
    }

    bool Scanner::isGameplayActive()
    {
        if (MWBase::Environment::get().getStateManager()->getState()
            != MWBase::StateManager::State_Running)
            return false;
        if (MWBase::Environment::get().getWindowManager()->isGuiMode())
            return false;
        return true;
    }

    void Scanner::onFrame(float dt)
    {
        if (!isGameplayActive())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Invalidate caches when the player's cell changes (handles
        // both interior/exterior transitions).
        const void* cellId = static_cast<const void*>(player.getCell());
        if (cellId != mLastCellId)
        {
            mLastCellId = cellId;
            for (auto& s : mLists)
            {
                s.mObjects.clear();
                s.mIndex = -1;
                s.mDirty = true;
            }
            mAutoWalker.cancel();
        }

        mAutoWalker.onFrame(dt);
    }

    bool Scanner::handleKey(int scancode, int modState)
    {
        if (!isGameplayActive())
            return false;

        bool ctrl = (modState & KMOD_CTRL) != 0;
        bool shift = (modState & KMOD_SHIFT) != 0;

        // Pressing a movement key while auto-walk is active should cancel it
        // cleanly. We don't consume the key; the player still wants to move.
        // NOTE: Space is deliberately excluded -- it's the default Activate
        // binding, so the player auto-walks to an object and presses Space to
        // interact with it on arrival. Cancelling on Space would make that
        // impossible (and Space wouldn't reach the activate handler).
        if (mAutoWalker.isActive())
        {
            switch (scancode)
            {
                case SDL_SCANCODE_W:
                case SDL_SCANCODE_A:
                case SDL_SCANCODE_S:
                case SDL_SCANCODE_D:
                    speak("Auto-walk cancelled.");
                    mAutoWalker.cancel();
                    break;
                default:
                    break;
            }
        }

        switch (scancode)
        {
            case SDL_SCANCODE_PAGEDOWN:
                if (ctrl)
                    cycleCategory(+1);
                else
                    cycleTarget(+1);
                return true;
            case SDL_SCANCODE_PAGEUP:
                if (ctrl)
                    cycleCategory(-1);
                else
                    cycleTarget(-1);
                return true;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                if (shift)
                    walkToTarget();
                else
                    focusCamera();
                return true;
            case SDL_SCANCODE_HOME:
                repeatAnnouncement();
                return true;
            case SDL_SCANCODE_END:
                clearSelection();
                return true;
            case SDL_SCANCODE_BACKSPACE:
                resetToFirst();
                return true;
            default:
                return false;
        }
    }

    void Scanner::cycleCategory(int delta)
    {
        int n = static_cast<int>(Category::Count);
        int cur = static_cast<int>(mCategory);
        cur = ((cur + delta) % n + n) % n;
        mCategory = static_cast<Category>(cur);
        // Force a rebuild of the new category's list and announce its
        // size, then auto-select the first entry.
        auto& state = mLists[static_cast<size_t>(mCategory)];
        state.mDirty = true;
        rebuildCurrentList();
        state.mIndex = state.mObjects.empty() ? -1 : 0;
        std::string msg = std::string("Category: ") + categoryName(mCategory)
            + ". " + std::to_string(state.mObjects.size()) + " in range.";
        speak(msg);
        if (!state.mObjects.empty())
            announceCurrent();
    }

    void Scanner::cycleTarget(int delta)
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        if (state.mDirty)
            rebuildCurrentList();
        if (state.mObjects.empty())
        {
            speak(std::string("No ") + categoryName(mCategory) + " in range.");
            return;
        }
        if (state.mIndex < 0)
            state.mIndex = 0;
        else
            state.mIndex = (state.mIndex + delta) % static_cast<int>(state.mObjects.size());
        if (state.mIndex < 0)
            state.mIndex += static_cast<int>(state.mObjects.size());
        announceCurrent();
    }

    void Scanner::focusCamera()
    {
        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
        {
            speak("No target selected.");
            return;
        }
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        osg::Vec3f targetPos = target.getRefData().getPosition().asVec3();
        osg::Vec3f delta = targetPos - playerPos;
        float horiz = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
        float desiredYaw = std::atan2(delta.x(), delta.y());
        float desiredPitch = -std::atan2(delta.z(), horiz);
        // OpenMW stores Euler rotation as (pitch, roll, yaw) on positions;
        // rotateObject accepts (x, y, z) in radians.
        osg::Vec3f rot(desiredPitch, 0.0f, desiredYaw);
        world->rotateObject(player, rot, MWBase::RotationFlag_none);
        speak("Facing " + objectDisplayName(target) + ".");
    }

    void Scanner::walkToTarget()
    {
        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
        {
            speak("No target selected.");
            return;
        }
        if (mAutoWalker.start(target))
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            osg::Vec3f playerPos = world->getPlayerPtr().getRefData().getPosition().asVec3();
            osg::Vec3f targetPos = target.getRefData().getPosition().asVec3();
            float dist = (targetPos - playerPos).length();
            speak("Walking to " + objectDisplayName(target)
                + ", " + formatDistance(dist) + ".");
        }
        else
        {
            speak("Cannot reach " + objectDisplayName(target) + ".");
        }
    }

    void Scanner::repeatAnnouncement()
    {
        if (currentTarget().isEmpty())
        {
            speak("No target selected.");
            return;
        }
        announceCurrent();
    }

    void Scanner::clearSelection()
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        state.mIndex = -1;
        mAutoWalker.cancel();
        speak("Selection cleared.");
    }

    void Scanner::resetToFirst()
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        if (state.mDirty)
            rebuildCurrentList();
        if (state.mObjects.empty())
        {
            speak(std::string("No ") + categoryName(mCategory) + " in range.");
            return;
        }
        state.mIndex = 0;
        announceCurrent();
    }

    void Scanner::rebuildCurrentList()
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        state.mObjects.clear();
        state.mIndex = -1;
        state.mDirty = false;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;
        MWWorld::CellStore* cell = player.getCell();
        if (!cell)
            return;

        Category cat = mCategory;
        cell->forEach([&](const MWWorld::Ptr& ptr) {
            if (ptr == player)
                return true;
            if (!matchesCategory(ptr, cat))
                return true;
            if (ptr.getCellRef().getCount() <= 0)
                return true;
            state.mObjects.push_back(ptr);
            return true;
        });

        osg::Vec3f pp = player.getRefData().getPosition().asVec3();
        std::sort(state.mObjects.begin(), state.mObjects.end(),
            [&pp](const MWWorld::Ptr& a, const MWWorld::Ptr& b) {
                float da = (a.getRefData().getPosition().asVec3() - pp).length2();
                float db = (b.getRefData().getPosition().asVec3() - pp).length2();
                return da < db;
            });
    }

    void Scanner::announceCurrent()
    {
        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        osg::Vec3f targetPos = target.getRefData().getPosition().asVec3();
        osg::Vec3f delta = targetPos - playerPos;
        float dist = delta.length();

        // Compute bearing relative to the player's current yaw.
        float playerYaw = player.getRefData().getPosition().rot[2];
        float targetYaw = std::atan2(delta.x(), delta.y());
        float relYaw = targetYaw - playerYaw;
        while (relYaw > kPi)
            relYaw -= 2 * kPi;
        while (relYaw < -kPi)
            relYaw += 2 * kPi;

        std::string name = objectDisplayName(target);
        appendDoorDestination(target, name);

        auto& state = mLists[static_cast<size_t>(mCategory)];
        std::string msg = name + ". " + formatDistance(dist)
            + ", " + bearingLabel(relYaw) + ". "
            + std::to_string(state.mIndex + 1) + " of "
            + std::to_string(state.mObjects.size()) + ".";
        speak(msg);
    }

    void Scanner::speak(const std::string& text)
    {
        // Resolve any MyGUI #{...} tags so cell-name references in door
        // destinations are spoken as their localized strings. We queue
        // (interrupt=false) so back-to-back announcements like "Category:
        // NPCs. 2 in range." and the first NPC line both get heard.
        auto resolved = MyGUI::LanguageManager::getInstance().replaceTags(text);
        Accessibility::AccessibilityManager::instance().speak(
            resolved.asUTF8(), /*interrupt=*/false);
    }

    MWWorld::Ptr Scanner::currentTarget()
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        if (state.mIndex < 0 || state.mIndex >= static_cast<int>(state.mObjects.size()))
            return MWWorld::Ptr();
        return state.mObjects[state.mIndex];
    }
}
