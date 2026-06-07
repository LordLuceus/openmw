#include "scanner.hpp"

#include <SDL_keycode.h>
#include <SDL_scancode.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>

#include <MyGUI_LanguageManager.h>

#include <components/misc/constants.hpp>
#include <components/misc/strings/algorithm.hpp>

#include <components/accessibility/accessibilitymanager.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadweap.hpp>

#include "../mwmechanics/creaturestats.hpp"

#include "../mwgui/tooltips.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/cell.hpp"
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

    // Describe vertical offset of a target relative to the player, e.g.
    // "2 metres up" / "3 metres down". Returns "" when within roughly one
    // floor-step of level, so we don't clutter announcements for things on the
    // same level. \p dzUnits is target.z - player.z in world units (positive =
    // target is higher).
    std::string formatElevation(float dzUnits)
    {
        // ~0.75 m dead-band: a single stair step is well under this, so minor
        // height differences on the "same" level stay silent.
        constexpr float kLevelDeadBand = 52.5f; // ~0.75 m
        if (std::abs(dzUnits) <= kLevelDeadBand)
            return std::string();
        const float metres = std::abs(dzUnits) / kUnitsPerMetre;
        char buf[32];
        if (metres < 10.0f)
            std::snprintf(buf, sizeof(buf), "%.1f metres %s", metres, dzUnits > 0.0f ? "up" : "down");
        else
            std::snprintf(buf, sizeof(buf), "%d metres %s", static_cast<int>(metres + 0.5f),
                dzUnits > 0.0f ? "up" : "down");
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
            case MWAccessibility::Category::Items:
                return "Items";
            case MWAccessibility::Category::Activators:
                return "Activators";
            case MWAccessibility::Category::Detected:
                return "Detected";
            case MWAccessibility::Category::Waypoints:
                return "Waypoints";
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
            // Items and Activators use the engine's capability predicates
            // rather than enumerating record types: isItem() covers every
            // carryable (misc/papers/keys, books, weapons, armour, clothing,
            // potions, ingredients, apparatus, lockpicks, probes, repair
            // tools, soul gems), and isActivator() covers levers/buttons/
            // scripted quest objects.
            case MWAccessibility::Category::Items:
                return ptr.getClass().isItem(ptr);
            case MWAccessibility::Category::Activators:
            {
                if (!ptr.getClass().isActivator())
                    return false;
                // Exclude ambient sound emitters and similar invisible
                // helpers. Morrowind implements these as activators whose
                // model is the editor-only "EditorMarker.NIF" mesh (which the
                // engine hides in-game) plus a script that loops a sound, e.g.
                // "Sound_Boat_Creak". Real interactables (silt strider, signs,
                // levers) use genuine visible meshes, so filtering out the
                // editor-marker model drops the decoration without a fragile
                // name match.
                std::string_view model = ptr.getClass().getModel(ptr);
                return !Misc::StringUtils::ciEndsWith(model, "editormarker.nif");
            }
            case MWAccessibility::Category::Count:
                break;
        }
        return false;
    }

    // --- Subcategories ----------------------------------------------------
    //
    // Some top-level categories can be narrowed by a secondary filter cycled
    // with Shift+PageUp/PageDown. Each subcategory is just a label plus a
    // predicate over a Ptr already known to match the parent category. Index 0
    // is always "All" (no extra filtering). Categories without subcategories
    // expose only "All", so Shift+Page is a harmless no-op there.

    struct Subcategory
    {
        const char* mName;
        // nullptr predicate == match everything ("All").
        bool (*mMatch)(const MWWorld::Ptr&);
    };

    bool isWeapon(const MWWorld::Ptr& p) { return p.getType() == ESM::Weapon::sRecordId; }
    bool isArmor(const MWWorld::Ptr& p) { return p.getType() == ESM::Armor::sRecordId; }
    bool isClothing(const MWWorld::Ptr& p) { return p.getType() == ESM::Clothing::sRecordId; }
    bool isPotion(const MWWorld::Ptr& p) { return p.getType() == ESM::Potion::sRecordId; }
    bool isIngredient(const MWWorld::Ptr& p) { return p.getType() == ESM::Ingredient::sRecordId; }

    bool isBookOrScroll(const MWWorld::Ptr& p) { return p.getType() == ESM::Book::sRecordId; }

    // "Tools": apparatus, lockpicks, probes, repair items, and carryable
    // lights (torches) -- the usable utility odds and ends.
    bool isTool(const MWWorld::Ptr& p)
    {
        unsigned int t = p.getType();
        return t == ESM::Apparatus::sRecordId || t == ESM::Lockpick::sRecordId
            || t == ESM::Probe::sRecordId || t == ESM::Repair::sRecordId
            || t == ESM::Light::sRecordId;
    }

    // "Misc": papers, keys, gold, soul gems -- everything carryable that is
    // not covered by the buckets above.
    bool isMiscItem(const MWWorld::Ptr& p)
    {
        return !isWeapon(p) && !isArmor(p) && !isClothing(p) && !isPotion(p)
            && !isIngredient(p) && !isBookOrScroll(p) && !isTool(p);
    }

    bool isNpcActor(const MWWorld::Ptr& p) { return p.getType() == ESM::NPC::sRecordId; }
    bool isCreatureActor(const MWWorld::Ptr& p) { return p.getType() == ESM::Creature::sRecordId; }

    constexpr Subcategory kItemSubs[] = {
        { "All", nullptr },
        { "Weapons", &isWeapon },
        { "Armor", &isArmor },
        { "Clothing", &isClothing },
        { "Potions", &isPotion },
        { "Ingredients", &isIngredient },
        { "Books and scrolls", &isBookOrScroll },
        { "Tools", &isTool },
        { "Miscellaneous", &isMiscItem },
    };

    constexpr Subcategory kNpcSubs[] = {
        { "All", nullptr },
        { "NPCs", &isNpcActor },
        { "Creatures", &isCreatureActor },
    };

    // Subcategories for the Detected category mirror the three Detect effects.
    // Their predicates are null because membership comes from the engine's
    // detection query (per type), not a Ptr test -- the filtering is done in
    // rebuildCurrentList's Detected branch keyed on the subcategory index, in
    // this order: 0 = All, 1 = Creatures, 2 = Keys, 3 = Enchantments.
    constexpr Subcategory kDetectedSubs[] = {
        { "All", nullptr },
        { "Creatures", nullptr },
        { "Keys", nullptr },
        { "Enchantments", nullptr },
    };

    // Returns the subcategory table for a category. Empty span (size 0) means
    // the category has no subcategories beyond the implicit "All".
    std::pair<const Subcategory*, size_t> subcategoriesFor(MWAccessibility::Category cat)
    {
        switch (cat)
        {
            case MWAccessibility::Category::Items:
                return { kItemSubs, std::size(kItemSubs) };
            case MWAccessibility::Category::Npcs:
                return { kNpcSubs, std::size(kNpcSubs) };
            case MWAccessibility::Category::Detected:
                return { kDetectedSubs, std::size(kDetectedSubs) };
            default:
                return { nullptr, 0 };
        }
    }

    // True if \p ptr (already matching \p cat) also matches subcategory index
    // \p subIndex. Index 0 / out-of-range / no-table all mean "match all".
    bool matchesSubcategory(const MWWorld::Ptr& ptr, MWAccessibility::Category cat, int subIndex)
    {
        auto [subs, count] = subcategoriesFor(cat);
        if (!subs || subIndex <= 0 || subIndex >= static_cast<int>(count))
            return true;
        const Subcategory& s = subs[subIndex];
        return s.mMatch == nullptr || s.mMatch(ptr);
    }

    // 8-point absolute compass label for a world-space bearing in radians,
    // where 0 = +Y = north and angle increases toward +X = east (matching the
    // engine's own atan2(x, y) convention; see camera north handling). This is
    // a fixed reference frame: a given door is always "to the north" regardless
    // of which way the player looks.
    const char* compassLabel(float absYaw)
    {
        // Normalize to [0, 2*PI).
        while (absYaw < 0)
            absYaw += 2 * kPi;
        while (absYaw >= 2 * kPi)
            absYaw -= 2 * kPi;
        // Each 45-degree sector centered on a compass point; offset by half a
        // sector so e.g. north covers [-22.5, +22.5) degrees.
        const float sector = 2 * kPi / 8.0f;
        int idx = static_cast<int>((absYaw + sector / 2) / sector) % 8;
        static const char* kPoints[8]
            = { "north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest" };
        return kPoints[idx];
    }

    // Spoken disambiguation suffix for the i-th (0-based) duplicate: A, B, ...
    // Z, then AA, AB, ... for the (rare) case of more than 26 same-named
    // objects in one cell.
    std::string letterForIndex(size_t i)
    {
        std::string out;
        ++i; // 1-based for bijective base-26 (A=1).
        while (i > 0)
        {
            size_t rem = (i - 1) % 26;
            out.insert(out.begin(), static_cast<char>('A' + rem));
            i = (i - 1) / 26;
        }
        return out;
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

    // The text the search filter matches against: the same enriched, spoken
    // identity the user hears -- the display name plus a door's destination
    // (e.g. "Door, to Balmora, Guild of Mages"). Without this, doors (which are
    // all just named "Door") would be unsearchable; the useful, distinguishing
    // text is the destination. Any #{...} localisation tags are resolved so the
    // match works against the form the user actually hears.
    std::string objectSearchText(const MWWorld::Ptr& ptr)
    {
        std::string text = objectDisplayName(ptr);
        appendDoorDestination(ptr, text);
        return MyGUI::LanguageManager::getInstance().replaceTags(text).asUTF8();
    }

    // Collect the objects revealed by the player's active Detect effects into
    // \p out. \p subIndex selects which Detect type(s) to include, matching the
    // kDetectedSubs order (0 = All, 1 = Creatures, 2 = Keys, 3 = Enchantments).
    // Each detection type is queried independently by the engine and is empty
    // unless the corresponding effect is active, so with nothing detected this
    // leaves \p out empty.
    void collectDetectedObjects(int subIndex, std::vector<MWWorld::Ptr>& out)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        const std::pair<int, MWBase::World::DetectionType> types[] = {
            { 1, MWBase::World::Detect_Creature },
            { 2, MWBase::World::Detect_Key },
            { 3, MWBase::World::Detect_Enchantment },
        };

        for (const auto& [sub, type] : types)
        {
            if (subIndex != 0 && subIndex != sub)
                continue;
            std::vector<MWWorld::Ptr> refs;
            world->listDetectedReferences(player, refs, type);
            out.insert(out.end(), refs.begin(), refs.end());
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
        // Re-arm the cell-name baseline whenever no game is loaded, so the
        // first cell of a freshly-loaded save / new game is recorded silently
        // (the player already knows where they start). This keys on the running
        // state -- NOT isGameplayActive(), which is also false during menus and
        // dialogue; we must stay primed across those so closing a menu doesn't
        // re-announce the current cell.
        if (MWBase::Environment::get().getStateManager()->getState()
            != MWBase::StateManager::State_Running)
            mCellNamePrimed = false;

        if (!isGameplayActive())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Invalidate caches when the player's cell changes (handles both
        // interior/exterior transitions). In an exterior the active cell grid
        // shifts as the player walks, so the cached object lists must be
        // rebuilt -- but the player's selection and any in-progress auto-walk
        // should survive: the target object is typically still loaded one cell
        // over. We remember the selected object's stable RefNum so the lazy
        // rebuild can re-pin the cursor onto it (see rebuildCurrentList), and
        // we deliberately do NOT cancel the auto-walker or proximity cue here
        // -- the auto-walker re-paths every second and self-cancels if its
        // target genuinely unloads, so it now walks seamlessly across cell
        // boundaries instead of stopping at every one.
        const void* cellId = static_cast<const void*>(player.getCell());
        if (cellId != mLastCellId)
        {
            mLastCellId = cellId;
            for (auto& s : mLists)
            {
                // Capture the current selection's identity before discarding
                // the (now stale) Ptr list, so rebuildCurrentList can restore
                // it once the new cell grid is scanned. If nothing is selected,
                // clear the remembered ref so a rebuild doesn't resurrect a
                // stale selection.
                if (s.mIndex >= 0 && s.mIndex < static_cast<int>(s.mObjects.size()))
                    s.mSelectedRef = s.mObjects[s.mIndex].getCellRef().getRefNum();
                else
                    s.mSelectedRef = ESM::RefNum{};
                s.mObjects.clear();
                s.mIndex = -1;
                s.mDirty = true;
            }

            // Speak the new cell's name on entry, but only when it differs from
            // the last announced one -- cities span many same-named cells, so
            // we don't want "Balmora" repeated as the player walks across it.
            announceCellChange();
        }

        // Prune objects that have left the world (e.g. an item the player just
        // picked up) from the active category's cached list, so they stop being
        // announced and the beacon stops homing on them. Skip when the list is
        // dirty -- it'll be rebuilt from scratch on next access anyway.
        if (!mLists[static_cast<size_t>(mCategory)].mDirty)
            pruneDeadObjects();

        mAutoWalker.onFrame(dt);
        mProximityCue.onFrame(dt);
        updateLockOn();
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
                // Ctrl cycles top-level category, Shift cycles the
                // subcategory filter, plain cycles the target.
                if (ctrl)
                    cycleCategory(+1);
                else if (shift)
                    cycleSubcategory(+1);
                else
                    cycleTarget(+1);
                return true;
            case SDL_SCANCODE_PAGEUP:
                if (ctrl)
                    cycleCategory(-1);
                else if (shift)
                    cycleSubcategory(-1);
                else
                    cycleTarget(-1);
                return true;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                // Enter faces the target, Shift+Enter walks to it, Ctrl+Enter
                // toggles the audio beacon. (Ctrl takes precedence so it works
                // regardless of whether Shift is also held.)
                if (ctrl)
                    toggleBeacon();
                else if (shift)
                    walkToTarget();
                else
                    focusCamera();
                return true;
            case SDL_SCANCODE_HOME:
                repeatAnnouncement();
                return true;
            case SDL_SCANCODE_L:
                // L announces the player's location (cell name); Shift+L
                // announces which way they're facing (compass point).
                if (shift)
                    announceFacing();
                else
                    announceLocation();
                return true;
            case SDL_SCANCODE_SLASH:
                // Open the search prompt to filter the current category by
                // name. Ctrl+/ clears any active filter outright (a quick way
                // back to the full list without opening the prompt).
                if (ctrl)
                    applySearchFilter(std::string());
                else
                    openSearch();
                return true;
            case SDL_SCANCODE_N:
                // Drop a map note (waypoint) at the player's current position.
                // Opens a text prompt to name it; the marker is placed on
                // confirm (see onWaypointNoteEntered).
                openDropNote();
                return true;
            case SDL_SCANCODE_K:
                // Toggle combat/interaction lock-on to the selected target.
                // While locked, the player is kept aimed at it so melee,
                // spells, and lockpicks/probes connect without manual aiming.
                toggleLockOn();
                return true;
            case SDL_SCANCODE_END:
                clearSelection();
                return true;
            case SDL_SCANCODE_BACKSPACE:
                resetToFirst();
                return true;
            case SDL_SCANCODE_SPACE:
                // Space is the default Activate binding. When the scanner has
                // a target selected, redirect activation to that target
                // (bypassing the crosshair the player can't aim). If nothing
                // is selected, activateTarget() returns false and we fall
                // through so normal crosshair Activate still works. Only plain
                // Space is intercepted -- modified combos pass through.
                if (!ctrl && !shift && activateTarget())
                    return true;
                return false;
            default:
                return false;
        }
    }

    bool Scanner::isCategoryAvailable(Category cat) const
    {
        if (cat == Category::Detected)
        {
            // Available only while something is actually detected. Query
            // directly (subIndex 0 = all types) rather than trusting the
            // possibly-stale cached list, so the category appears/disappears the
            // moment a Detect effect starts or ends.
            std::vector<MWWorld::Ptr> refs;
            collectDetectedObjects(0, refs);
            return !refs.empty();
        }
        if (cat == Category::Waypoints)
        {
            // Available only when the player has at least one waypoint in the
            // current cell (a dropped map note or a Mark set here), so it's
            // skipped when cycling otherwise -- just like Detected.
            std::vector<Waypoint> wps;
            collectWaypoints(wps);
            return !wps.empty();
        }
        return true;
    }

    void Scanner::cycleCategory(int delta)
    {
        int n = static_cast<int>(Category::Count);
        int cur = static_cast<int>(mCategory);
        // Step in the requested direction, skipping any category that isn't
        // currently available (today only Detected, which is hidden unless a
        // Detect effect is revealing something). Bounded to one full loop so we
        // can't spin forever if nothing is available.
        for (int steps = 0; steps < n; ++steps)
        {
            cur = ((cur + delta) % n + n) % n;
            if (isCategoryAvailable(static_cast<Category>(cur)))
                break;
        }
        mCategory = static_cast<Category>(cur);
        // Force a rebuild of the new category's list and announce its
        // size, then auto-select the first entry.
        auto& state = mLists[static_cast<size_t>(mCategory)];
        state.mDirty = true;
        rebuildCurrentList();
        const size_t count = currentListSize();
        state.mIndex = count == 0 ? -1 : 0;
        std::string msg = std::string("Category: ") + categoryName(mCategory)
            + ". " + std::to_string(count) + " in range.";
        speak(msg);
        if (count > 0)
            announceCurrent();
        updateProximityCue();
    }

    void Scanner::cycleSubcategory(int delta)
    {
        auto [subs, count] = subcategoriesFor(mCategory);
        if (!subs || count <= 1)
        {
            // No secondary filter for this category.
            speak(std::string(categoryName(mCategory)) + " has no subcategories.");
            return;
        }

        auto& state = mLists[static_cast<size_t>(mCategory)];
        int n = static_cast<int>(count);
        state.mSubIndex = ((state.mSubIndex + delta) % n + n) % n;

        // The active filter changed, so the cached list is stale.
        state.mDirty = true;
        rebuildCurrentList();
        state.mIndex = state.mObjects.empty() ? -1 : 0;

        std::string msg = std::string(subs[state.mSubIndex].mName) + ". "
            + std::to_string(state.mObjects.size()) + " in range.";
        speak(msg);
        if (!state.mObjects.empty())
            announceCurrent();
        updateProximityCue();
    }

    void Scanner::cycleTarget(int delta)
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        if (state.mDirty)
            rebuildCurrentList();
        const int count = static_cast<int>(currentListSize());
        if (count == 0)
        {
            speak(std::string("No ") + categoryName(mCategory) + " in range.");
            return;
        }
        if (state.mIndex < 0)
            state.mIndex = 0;
        else
            state.mIndex = (state.mIndex + delta) % count;
        if (state.mIndex < 0)
            state.mIndex += count;
        announceCurrent();
        updateProximityCue();
    }

    void Scanner::focusCamera()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();

        // Waypoints: face the fixed position (no object to name).
        if (isWaypointCategory())
        {
            const Waypoint* wp = currentWaypoint();
            if (!wp)
            {
                speak("No target selected.");
                return;
            }
            osg::Vec3f d = wp->mPosition - playerPos;
            float horizW = std::sqrt(d.x() * d.x() + d.y() * d.y());
            osg::Vec3f rotW(-std::atan2(d.z(), horizW), 0.0f, std::atan2(d.x(), d.y()));
            world->rotateObject(player, rotW, MWBase::RotationFlag_none);
            speak("Facing " + wp->mName + ".");
            return;
        }

        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
        {
            speak("No target selected.");
            return;
        }
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

    void Scanner::toggleLockOn()
    {
        // Already locked: pressing the key again releases.
        if (mLockedOn)
        {
            releaseLockOn(/*announce=*/true);
            return;
        }

        // Lock-on is for world objects (actors to fight, chests/doors to pick),
        // not the position-based Waypoints category.
        if (isWaypointCategory())
        {
            speak("Cannot lock onto a waypoint.");
            return;
        }

        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
        {
            speak("No target selected.");
            return;
        }

        // An auto-walk would fight the lock for control of the player's facing,
        // so cancel it first.
        if (mAutoWalker.isActive())
            mAutoWalker.cancel();

        mLockTarget = target;
        mLockTargetName = objectDisplayName(target);
        mLockedOn = true;
        speak("Locked onto " + mLockTargetName + ".");
        // Aim immediately so the first attack/use this frame already connects,
        // rather than waiting for the next updateLockOn tick.
        updateLockOn();
    }

    void Scanner::updateLockOn()
    {
        if (!mLockedOn)
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Release if the target has left the world (picked up, unloaded) or
        // died -- a corpse is no longer a meaningful combat lock, and the
        // player will want to re-acquire.
        if (mLockTarget.isEmpty() || mLockTarget.getCellRef().getCount() <= 0)
        {
            speak("Lost lock on " + mLockTargetName + ".");
            releaseLockOn(/*announce=*/false);
            return;
        }
        if (mLockTarget.getClass().isActor()
            && mLockTarget.getClass().getCreatureStats(mLockTarget).isDead())
        {
            speak(mLockTargetName + " is dead.");
            releaseLockOn(/*announce=*/false);
            return;
        }

        // Re-aim the player at the target (yaw + pitch). The engine's combat/use
        // systems all resolve their target from the player's facing direction,
        // so holding this aim is what makes melee, spells, and lockpicks/probes
        // connect. We set absolute orientation via rotateObject; with no mouse
        // input there are no competing deltas, so the aim holds steady (same
        // mechanism as focusCamera()). WASD movement stays relative to this
        // facing, so the player can advance/strafe.
        //
        // CRITICAL: aim from the player's EYE, and at the target's CENTRE -- not
        // foot-origin to foot-origin. The position vectors returned above are at
        // each object's base (feet). But the lockpick/probe and activate code
        // raycasts from the CAMERA (eye level, ~chest-to-head height up), and
        // melee getHitContact() likewise measures from the actor's eye. If we
        // pitch using foot-to-foot we badly under-aim downward targets: a chest
        // on the floor a couple of metres away comes out almost level, so the
        // camera ray sails over it and hits the wall/shelf behind (the "aimed at
        // the plates above the chest" bug). Raising the origin to eye level and
        // the aim point to the target's vertical centre makes the pitch match
        // what the raycast actually needs.
        const float playerEye = world->getHalfExtents(player, /*rendering=*/true).z() * 2.f * 0.85f;
        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        playerPos.z() += playerEye;
        osg::Vec3f targetPos = mLockTarget.getRefData().getPosition().asVec3();
        // Aim at the target's vertical centre (half its height up from its
        // base), so we don't aim at an actor's feet or a container's floor edge.
        targetPos.z() += world->getHalfExtents(mLockTarget, /*rendering=*/true).z();
        const osg::Vec3f delta = targetPos - playerPos;
        const float horiz = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
        // Degenerate case: target practically on top of us -- keep current yaw.
        if (horiz < 1.0f && std::abs(delta.z()) < 1.0f)
            return;
        const float desiredYaw = std::atan2(delta.x(), delta.y());
        const float desiredPitch = -std::atan2(delta.z(), horiz);
        world->rotateObject(player, osg::Vec3f(desiredPitch, 0.0f, desiredYaw),
            MWBase::RotationFlag_none);
    }

    void Scanner::releaseLockOn(bool announce)
    {
        if (!mLockedOn)
            return;
        mLockedOn = false;
        mLockTarget = MWWorld::Ptr();
        if (announce)
            speak("Lock released.");
        mLockTargetName.clear();
    }

    bool Scanner::activateTarget()
    {
        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
            return false; // Nothing selected; let the default Activate run.

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();

        // Gate on the engine's activation distance, matching vanilla reach.
        // The audio beacon already guides the player into range, so this just
        // prevents grabbing things across the room.
        osg::Vec3f delta = target.getRefData().getPosition().asVec3()
            - player.getRefData().getPosition().asVec3();
        float dist = delta.length();
        if (dist > world->getMaxActivationDistance())
        {
            speak(objectDisplayName(target) + " is too far away.");
            return true; // Consume: we handled it (by refusing), don't also
                         // fire the crosshair Activate.
        }

        // Mirror Player::activate(): only activate things that would show a
        // tooltip, then dispatch through the normal Lua activation path. This
        // bypasses the camera crosshair entirely -- the whole point, since a
        // blind player can't aim at a small item on a table.
        if (!target.getClass().hasToolTip(target))
            return false;

        MWBase::Environment::get().getLuaManager()->objectActivated(target, player);
        return true;
    }

    void Scanner::walkToTarget()
    {
        // Auto-walk drives the player's facing toward path waypoints, which
        // would fight a lock-on for control. Release the lock so the walk runs
        // cleanly.
        releaseLockOn(/*announce=*/false);

        MWBase::World* world = MWBase::Environment::get().getWorld();
        osg::Vec3f playerPos = world->getPlayerPtr().getRefData().getPosition().asVec3();

        // Waypoints: walk to the fixed position via the position-based path.
        if (isWaypointCategory())
        {
            const Waypoint* wp = currentWaypoint();
            if (!wp)
            {
                speak("No target selected.");
                return;
            }
            if (mAutoWalker.start(wp->mPosition, wp->mName))
            {
                float dist = (wp->mPosition - playerPos).length();
                speak("Walking to " + wp->mName + ", " + formatDistance(dist) + ".");
            }
            else
                speak("Cannot reach " + wp->mName + ".");
            return;
        }

        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
        {
            speak("No target selected.");
            return;
        }
        if (mAutoWalker.start(target))
        {
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

    void Scanner::openSearch()
    {
        // The Waypoints category isn't name-filterable (its members are the
        // handful of map notes / Mark in the current cell), so don't open the
        // filter prompt there.
        if (isWaypointCategory())
        {
            speak("Waypoints cannot be filtered.");
            return;
        }
        // Hand off to the WindowManager, which shows the text-input prompt
        // (GM_ScannerSearch) seeded with the current filter and calls back into
        // applySearchFilter() / onSearchCancelled().
        auto& state = mLists[static_cast<size_t>(mCategory)];
        MWBase::Environment::get().getWindowManager()->openScannerSearch(state.mFilter);
    }

    void Scanner::applySearchFilter(const std::string& query)
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];

        // Trim surrounding whitespace so a stray space doesn't filter to zero
        // results; an all-whitespace (or empty) query clears the filter.
        std::string trimmed = query;
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), notSpace));
        trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), notSpace).base(), trimmed.end());

        state.mFilter = trimmed;
        // Re-scan the current cells with the new filter and select the first
        // match. Don't try to preserve the old selection -- the user just chose
        // a new filter, so jumping to the nearest match is what they want.
        state.mDirty = true;
        state.mSelectedRef = ESM::RefNum{};
        rebuildCurrentList();
        state.mIndex = state.mObjects.empty() ? -1 : 0;

        if (trimmed.empty())
        {
            speak(std::string("Filter cleared. ") + std::to_string(state.mObjects.size())
                + " " + categoryName(mCategory) + " in range.");
        }
        else if (state.mObjects.empty())
        {
            speak(std::string("No ") + categoryName(mCategory) + " matching " + trimmed + ".");
        }
        else
        {
            speak(std::string("Filter: ") + trimmed + ". " + std::to_string(state.mObjects.size())
                + " matching.");
        }

        if (!state.mObjects.empty())
            announceCurrent();
        updateProximityCue();
    }

    void Scanner::onSearchCancelled()
    {
        // The prompt closed without applying a change. Re-announce the current
        // selection (if any) so the user knows focus is back on the scanner.
        if (!currentTarget().isEmpty())
            announceCurrent();
    }

    void Scanner::openDropNote()
    {
        // Hand off to the WindowManager, which shows the text-input prompt
        // (GM_WaypointNote) and calls back into onWaypointNoteEntered() /
        // onWaypointNoteCancelled().
        MWBase::Environment::get().getWindowManager()->openWaypointNote();
    }

    void Scanner::onWaypointNoteEntered(const std::string& text)
    {
        // The MapWindow owns the custom-marker collection and the cell/grid
        // know-how, so it does the actual placement at the player's position.
        const bool placed = MWBase::Environment::get().getWindowManager()->dropPlayerMapNote(text);
        if (placed)
        {
            // Invalidate the Waypoints list so the new note appears next time
            // the player cycles to / through that category.
            mLists[static_cast<size_t>(Category::Waypoints)].mDirty = true;
            speak(std::string("Note placed: ") + text + ".");
        }
        else
            speak("Could not place note.");
    }

    void Scanner::onWaypointNoteCancelled()
    {
        speak("Cancelled.");
    }

    void Scanner::repeatAnnouncement()
    {
        const bool hasSelection = isWaypointCategory() ? (currentWaypoint() != nullptr) : !currentTarget().isEmpty();
        if (!hasSelection)
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
        state.mSelectedRef = ESM::RefNum{};
        mAutoWalker.cancel();
        mProximityCue.stop();
        speak("Selection cleared.");
    }

    void Scanner::resetToFirst()
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        if (state.mDirty)
            rebuildCurrentList();
        if (currentListSize() == 0)
        {
            speak(std::string("No ") + categoryName(mCategory) + " in range.");
            return;
        }
        state.mIndex = 0;
        announceCurrent();
        updateProximityCue();
    }

    void Scanner::announceLocation()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;
        MWWorld::CellStore* cell = player.getCell();
        if (!cell)
            return;

        // getCellName resolves to the interior name (e.g. "Census and Excise
        // Office") or, for exteriors, the region/named-cell string. It may
        // contain a #{...} tag, which speak() resolves.
        std::string_view name = world->getCellName(cell);
        if (name.empty())
            speak("Unknown location.");
        else
            speak(std::string(name));
    }

    void Scanner::announceCellChange()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;
        MWWorld::CellStore* cell = player.getCell();
        if (!cell)
            return;

        // getCellName resolves to the interior name or the exterior
        // region/named-cell string; it may contain a #{...} tag. Resolve tags
        // first so the comparison (and the spoken text) uses the final display
        // string -- two differently-tagged names that resolve identically
        // shouldn't be re-announced.
        std::string_view raw = world->getCellName(cell);
        if (raw.empty())
        {
            // An unnamed exterior wilderness cell: don't announce, and don't
            // disturb the last-named location, so leaving and re-entering a
            // named place still re-announces it correctly.
            return;
        }

        std::string name = MyGUI::LanguageManager::getInstance().replaceTags(std::string(raw)).asUTF8();
        if (name.empty() || name == mLastAnnouncedCellName)
            return;

        // First cell of this game session: record it silently. The player knows
        // where they just loaded in; only announce subsequent transitions.
        if (!mCellNamePrimed)
        {
            mCellNamePrimed = true;
            mLastAnnouncedCellName = name;
            return;
        }

        mLastAnnouncedCellName = name;
        speak(name);
    }

    void Scanner::announceFacing()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // rot[2] is the player's yaw about the vertical axis, in the same
        // absolute frame compassLabel expects (0 = facing +Y = north, angle
        // increasing toward +X = east), so it maps directly to a compass point
        // -- the same vocabulary used for target bearings.
        const float yaw = player.getRefData().getPosition().rot[2];
        speak(std::string("Facing ") + compassLabel(yaw) + ".");
    }

    void Scanner::rebuildCurrentList()
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        state.mObjects.clear();
        state.mWaypoints.clear();
        state.mIndex = -1;
        state.mDirty = false;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // The Waypoints category is position-based (map notes + Mark), not a
        // cell scan of world objects: build its parallel list and stop here.
        // collectWaypoints already sorts nearest-first.
        if (mCategory == Category::Waypoints)
        {
            collectWaypoints(state.mWaypoints);
            return;
        }

        // The Detected category doesn't scan loaded cells: its membership is
        // whatever the player's active Detect Creature/Key/Enchantment effects
        // currently reveal (which can be outside the loaded grid). Build it from
        // the engine's detection query and skip the cell-scan path below.
        if (mCategory == Category::Detected)
        {
            collectDetectedObjects(state.mSubIndex, state.mObjects);

            // The same object can be reported by more than one Detect type (an
            // enchanted creature shows under both Creatures and Enchantments),
            // so drop duplicates by stable RefNum before sorting/announcing.
            std::sort(state.mObjects.begin(), state.mObjects.end(),
                [](const MWWorld::Ptr& a, const MWWorld::Ptr& b) {
                    return a.getCellRef().getRefNum() < b.getCellRef().getRefNum();
                });
            state.mObjects.erase(std::unique(state.mObjects.begin(), state.mObjects.end(),
                                     [](const MWWorld::Ptr& a, const MWWorld::Ptr& b) {
                                         return a.getCellRef().getRefNum() == b.getCellRef().getRefNum();
                                     }),
                state.mObjects.end());

            // Apply the active name filter, same as the cell-scan path.
            if (!state.mFilter.empty())
            {
                std::erase_if(state.mObjects, [&](const MWWorld::Ptr& ptr) {
                    return Misc::StringUtils::ciFind(objectSearchText(ptr), state.mFilter)
                        == std::string_view::npos;
                });
            }

            osg::Vec3f pp = player.getRefData().getPosition().asVec3();
            std::sort(state.mObjects.begin(), state.mObjects.end(),
                [&pp](const MWWorld::Ptr& a, const MWWorld::Ptr& b) {
                    float da = (a.getRefData().getPosition().asVec3() - pp).length2();
                    float db = (b.getRefData().getPosition().asVec3() - pp).length2();
                    return da < db;
                });

            if (state.mSelectedRef.isSet())
            {
                for (size_t i = 0; i < state.mObjects.size(); ++i)
                {
                    if (state.mObjects[i].getCellRef().getRefNum() == state.mSelectedRef)
                    {
                        state.mIndex = static_cast<int>(i);
                        break;
                    }
                }
            }

            assignDisambiguationLabels();
            return;
        }

        // Scan every active cell, not just the player's own. In an exterior
        // (e.g. a town like Balmora) OpenMW keeps a whole grid of cells loaded
        // around the player; scanning only player.getCell() hid everything in
        // the neighbouring cells -- doors, NPCs, etc. just a short walk away.
        // In an interior getActiveCells() returns the single cell, so this is
        // a no-op there. The distance sort below orders results across cells.
        std::vector<MWWorld::CellStore*> cells;
        world->getActiveCells(cells);
        if (cells.empty())
        {
            if (MWWorld::CellStore* cell = player.getCell())
                cells.push_back(cell);
        }

        Category cat = mCategory;
        int subIndex = state.mSubIndex;
        for (MWWorld::CellStore* cell : cells)
        {
            if (!cell)
                continue;
            cell->forEach([&](const MWWorld::Ptr& ptr) {
                if (ptr == player)
                    return true;
                if (!matchesCategory(ptr, cat))
                    return true;
                if (!matchesSubcategory(ptr, cat, subIndex))
                    return true;
                if (ptr.getCellRef().getCount() <= 0)
                    return true;
                // Skip objects scripted out of the world (disabled refs).
                if (!ptr.getRefData().isEnabled())
                    return true;
                // Skip internal/helper objects the player can never interact
                // with -- e.g. invisible collision activators like
                // "CharGenCollision". hasToolTip() is the engine's own "would
                // this ever show a tooltip" predicate (for activators it is
                // literally !getName().empty()), so this filters nameless
                // engine plumbing without hiding any real interactable object.
                if (!ptr.getClass().hasToolTip(ptr))
                    return true;
                // Apply the active name filter (case-insensitive substring of
                // the object's spoken identity -- name plus, for doors, their
                // destination, so "guild" matches "Door, to ... Guild of
                // Mages"). Empty filter matches everything.
                if (!state.mFilter.empty()
                    && Misc::StringUtils::ciFind(objectSearchText(ptr), state.mFilter)
                        == std::string_view::npos)
                    return true;
                state.mObjects.push_back(ptr);
                return true;
            });
        }

        osg::Vec3f pp = player.getRefData().getPosition().asVec3();
        std::sort(state.mObjects.begin(), state.mObjects.end(),
            [&pp](const MWWorld::Ptr& a, const MWWorld::Ptr& b) {
                float da = (a.getRefData().getPosition().asVec3() - pp).length2();
                float db = (b.getRefData().getPosition().asVec3() - pp).length2();
                return da < db;
            });

        // Re-pin the selection onto the same physical object it was on before
        // this rebuild (matched by stable RefNum), so crossing a cell boundary
        // doesn't lose the player's place in the list. If that object is no
        // longer present (truly unloaded / removed), the selection stays
        // cleared.
        if (state.mSelectedRef.isSet())
        {
            for (size_t i = 0; i < state.mObjects.size(); ++i)
            {
                if (state.mObjects[i].getCellRef().getRefNum() == state.mSelectedRef)
                {
                    state.mIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        assignDisambiguationLabels();
    }

    void Scanner::assignDisambiguationLabels()
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        state.mLabels.clear();

        // Group the objects by display name so we can detect duplicates. We
        // keep one bucket of RefNums per name. (Names are cheap to recompute;
        // the lists are small -- everything in the current cell.)
        std::unordered_map<std::string, std::vector<ESM::RefNum>> byName;
        for (const MWWorld::Ptr& ptr : state.mObjects)
            byName[objectDisplayName(ptr)].push_back(ptr.getCellRef().getRefNum());

        for (auto& [name, refs] : byName)
        {
            if (refs.size() < 2)
                continue; // Unique name: no suffix needed.

            // Sort the duplicates by their stable RefNum so the letter
            // assignment is deterministic and independent of the distance sort
            // above. This is what keeps a given door's letter fixed as the
            // player moves and the list re-orders.
            std::sort(refs.begin(), refs.end());

            for (size_t i = 0; i < refs.size(); ++i)
                state.mLabels[refs[i]] = letterForIndex(i);
        }
    }

    void Scanner::pruneDeadObjects()
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];
        if (state.mObjects.empty())
            return;

        // Remember the selected object so we can keep the cursor on it (or
        // clear the selection if it was the thing that disappeared).
        MWWorld::Ptr selected;
        if (state.mIndex >= 0 && state.mIndex < static_cast<int>(state.mObjects.size()))
            selected = state.mObjects[state.mIndex];

        // An object is "dead" once taken/deleted (count drops to 0) or disabled
        // (scripted out of the world). This mirrors the filters in
        // rebuildCurrentList(), so a pruned list matches what a rebuild would
        // produce -- without the cost of re-scanning the cell every frame.
        auto isDead = [](const MWWorld::Ptr& ptr) {
            return ptr.isEmpty() || ptr.getCellRef().getCount() <= 0 || !ptr.getRefData().isEnabled();
        };

        bool removedSelection = false;
        std::vector<MWWorld::Ptr> kept;
        kept.reserve(state.mObjects.size());
        for (MWWorld::Ptr& ptr : state.mObjects)
        {
            if (isDead(ptr))
            {
                if (!selected.isEmpty() && ptr == selected)
                    removedSelection = true;
                continue;
            }
            kept.push_back(ptr);
        }

        if (kept.size() == state.mObjects.size())
            return; // Nothing changed.

        state.mObjects = std::move(kept);

        // Re-pin the cursor. If the selected object survived, follow it to its
        // new index; if it was the one removed (or there's no selection left),
        // drop the selection so nothing stale is announced or cued.
        if (!removedSelection && !selected.isEmpty())
        {
            state.mIndex = -1;
            for (size_t i = 0; i < state.mObjects.size(); ++i)
            {
                if (state.mObjects[i] == selected)
                {
                    state.mIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        else
        {
            state.mIndex = -1;
        }

        // The selection may have been cleared or moved; keep the audio beacon
        // in sync so it stops homing on a now-gone object.
        updateProximityCue();
    }

    void Scanner::announceCurrent()
    {
        // Waypoints are position-based, so they have their own announcer.
        if (isWaypointCategory())
        {
            announceCurrentWaypoint();
            return;
        }

        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        osg::Vec3f targetPos = target.getRefData().getPosition().asVec3();
        osg::Vec3f delta = targetPos - playerPos;
        float dist = delta.length();

        // targetYaw is an absolute world bearing (0 = north, +X = east), a
        // fixed compass reference that doesn't change as the player turns.
        float targetYaw = std::atan2(delta.x(), delta.y());

        std::string name = objectDisplayName(target);
        appendDoorDestination(target, name);

        auto& state = mLists[static_cast<size_t>(mCategory)];

        // Append the stable disambiguation letter (if this object shares its
        // name with others in range), right after the name so it reads as part
        // of the object's identity, e.g. "Wooden Door, to Seyda Neen, A".
        if (auto it = state.mLabels.find(target.getCellRef().getRefNum()); it != state.mLabels.end())
            name += ", " + it->second;

        // Direction is the absolute compass heading -- a fixed frame the
        // player can use to remember where a thing is regardless of which way
        // they're facing. Elevation (if the target is meaningfully above or
        // below us) follows the bearing; positional "N of M" stays at the very
        // end (project convention).
        std::string elevation = formatElevation(delta.z());
        std::string msg = name + ". " + formatDistance(dist)
            + ", " + compassLabel(targetYaw) + ". "
            + (elevation.empty() ? "" : elevation + ". ")
            + std::to_string(state.mIndex + 1) + " of "
            + std::to_string(state.mObjects.size()) + ".";

        // Ownership / "stolen from" info is sensitive: vanilla does NOT show it
        // to sighted players during normal play -- only in "full help" mode
        // (the ToggleFullHelp console command). Mirror that exactly, so we
        // expose it only when the player has enabled full help, at parity with
        // what a sighted player would then see. getCellRefString() is the same
        // function the tooltips use; it returns newline-separated fields
        // ("\nOwner: X", "\nStolen N from Y", faction/rank), which we flatten
        // into the spoken line.
        if (MWBase::Environment::get().getWindowManager()->getFullHelp())
        {
            std::string ownerInfo = MWGui::ToolTips::getCellRefString(target.getCellRef());
            if (!ownerInfo.empty())
            {
                for (char& c : ownerInfo)
                {
                    if (c == '\n')
                        c = ' ';
                }
                msg += ownerInfo + ".";
            }
        }

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

    size_t Scanner::currentListSize() const
    {
        const auto& state = mLists[static_cast<size_t>(mCategory)];
        return isWaypointCategory() ? state.mWaypoints.size() : state.mObjects.size();
    }

    const Scanner::Waypoint* Scanner::currentWaypoint() const
    {
        if (!isWaypointCategory())
            return nullptr;
        const auto& state = mLists[static_cast<size_t>(mCategory)];
        if (state.mIndex < 0 || state.mIndex >= static_cast<int>(state.mWaypoints.size()))
            return nullptr;
        return &state.mWaypoints[state.mIndex];
    }

    void Scanner::collectWaypoints(std::vector<Waypoint>& out) const
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;
        const MWWorld::CellStore* cellStore = player.getCell();
        if (!cellStore || !cellStore->getCell())
            return;

        // Player map notes live in the same cell-keyed collection the map
        // window draws. Scope to the player's current cell so distances and
        // auto-walk stay meaningful (you can't path across a cell you're not
        // in). The cell id is derived the same way a dropped note's is.
        const MWWorld::Cell* cell = cellStore->getCell();
        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();

        ESM::RefId cellId;
        if (cell->isExterior())
        {
            const int cellX = static_cast<int>(std::floor(playerPos.x() / Constants::CellSizeInUnits));
            const int cellY = static_cast<int>(std::floor(playerPos.y() / Constants::CellSizeInUnits));
            cellId = ESM::Cell::generateIdForCell(true, {}, cellX, cellY);
        }
        else
            cellId = cell->getId();

        const auto notes = MWBase::Environment::get().getWindowManager()->getPlayerMapNotes(cellId);
        for (const auto& note : notes)
        {
            // Map notes only carry an XY world position; use the player's Z as a
            // reasonable height for bearing/240 audio (they're on the same floor
            // in practice, and the pathfinder snaps to navmesh height anyway).
            Waypoint wp;
            wp.mName = note.mText.empty() ? std::string("Note") : note.mText;
            wp.mPosition = osg::Vec3f(note.mWorldX, note.mWorldY, playerPos.z());
            out.push_back(std::move(wp));
        }

        // The Mark spell location, but only when it's in the player's current
        // cell (a single global location; including a cross-cell mark would give
        // a misleading bearing and an unreachable auto-walk target).
        MWWorld::CellStore* markedCell = nullptr;
        ESM::Position markedPos;
        world->getPlayer().getMarkedPosition(markedCell, markedPos);
        if (markedCell && markedCell == cellStore)
        {
            Waypoint wp;
            wp.mName = "Mark";
            wp.mPosition = markedPos.asVec3();
            out.push_back(std::move(wp));
        }

        // Nearest first, matching the object categories' distance sort.
        std::sort(out.begin(), out.end(), [&playerPos](const Waypoint& a, const Waypoint& b) {
            return (a.mPosition - playerPos).length2() < (b.mPosition - playerPos).length2();
        });
    }

    void Scanner::announceCurrentWaypoint()
    {
        const Waypoint* wp = currentWaypoint();
        if (!wp)
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        osg::Vec3f delta = wp->mPosition - playerPos;
        float dist = delta.length();
        float targetYaw = std::atan2(delta.x(), delta.y());

        const auto& state = mLists[static_cast<size_t>(mCategory)];
        std::string elevation = formatElevation(delta.z());
        std::string msg = wp->mName + ". " + formatDistance(dist)
            + ", " + compassLabel(targetYaw) + ". "
            + (elevation.empty() ? "" : elevation + ". ")
            + std::to_string(state.mIndex + 1) + " of "
            + std::to_string(state.mWaypoints.size()) + ".";
        speak(msg);
    }

    void Scanner::updateProximityCue()
    {
        // Point the proximity cue at whatever is currently selected (an empty
        // target silences it). onFrame() then handles approach-loop vs arrival
        // audio. A single sound pair is used for all categories.
        //
        // The beacon is opt-in: when disabled, keep the cue silenced
        // regardless of selection so it never sounds unbidden.
        if (!mBeaconEnabled)
        {
            mProximityCue.stop();
            return;
        }
        if (isWaypointCategory())
        {
            if (const Waypoint* wp = currentWaypoint())
                mProximityCue.setTarget(wp->mPosition);
            else
                mProximityCue.stop();
            return;
        }
        mProximityCue.setTarget(currentTarget());
    }

    void Scanner::toggleBeacon()
    {
        mBeaconEnabled = !mBeaconEnabled;
        if (mBeaconEnabled)
        {
            speak("Audio beacon on.");
            // Begin guiding toward the current selection immediately.
            updateProximityCue();
        }
        else
        {
            speak("Audio beacon off.");
            mProximityCue.stop();
        }
    }
}
