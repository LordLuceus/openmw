#include "scanner.hpp"

#include "itembucket.hpp"
#include "spokenformat.hpp"

#include <SDL_keycode.h>
#include <SDL_scancode.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>
#include <vector>

#include <osg/ComputeBoundsVisitor>

#include <components/sceneutil/positionattitudetransform.hpp>

#include <MyGUI_LanguageManager.h>

#include <components/esm/defs.hpp>
#include <components/esm/esm3exteriorcellrefid.hpp>
#include <components/esm/util.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/vfs/pathutil.hpp>

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
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadweap.hpp>

#include "../mwmechanics/aisequence.hpp"
#include "../mwmechanics/combat.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/magiceffects.hpp"
#include "../mwmechanics/drawstate.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/spells.hpp"
#include "../mwmechanics/weapontype.hpp"

#include "../mwsound/type.hpp"

#include "../mwphysics/collisiontype.hpp"
#include "../mwphysics/raycasting.hpp"

#include "../mwrender/vismask.hpp"

#include "../mwgui/tooltips.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwinput/actions.hpp"

#include "../mwworld/cell.hpp"
#include "../mwworld/cellref.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/doorstate.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"
#include "../mwworld/worldmodel.hpp"

namespace
{
    // True if \p scancode is the player's currently-bound forward / back / left
    // / right movement key. Reads the live key bindings (not hardcoded WASD) so
    // remapped and non-QWERTY layouts work, and an unbound action (UNKNOWN)
    // never spuriously matches. Used to cancel auto-walk when the player takes
    // manual control.
    bool isMovementKey(int scancode)
    {
        const MWBase::InputManager* input = MWBase::Environment::get().getInputManager();
        if (!input)
            return false;
        for (int action : { MWInput::A_MoveForward, MWInput::A_MoveBackward, MWInput::A_MoveLeft,
                 MWInput::A_MoveRight })
        {
            const SDL_Scancode bound = input->getActionKeyBinding(action);
            if (bound != SDL_SCANCODE_UNKNOWN && static_cast<int>(bound) == scancode)
                return true;
        }
        return false;
    }

    // Pi and Morrowind-units-per-metre live in spokenformat.hpp alongside the
    // pure distance/elevation/compass helpers (kept engine-free so they're
    // unit-testable); reused here for the spellcast-range and yaw maths.
    using MWAccessibility::kPi;
    using MWAccessibility::kUnitsPerMetre;

    // How often (seconds) to silently rebuild the Actors list so combat state
    // and distances stay current. Frequent enough that a newly-hostile attacker
    // shows up promptly, but not every frame (the rebuild scans all active
    // cells). See Scanner::announceDrawStateChange's sibling refresh path.
    constexpr float kActorRefreshInterval = 0.5f;

    // Minimum gap (seconds) between spoken "out of range" melee warnings, so a
    // rapidly-swinging weapon doesn't machine-gun the message. See
    // Scanner::announceMeleeReach.
    constexpr float kMeleeReachAnnounceInterval = 1.5f;

    // Contextual combat cues, copied from files/data/sounds/a11y into the VFS at
    // build time (see files/data/CMakeLists.txt). Played 2D (non-positional):
    // they're a HUD-style status cue about the player's own readiness, not a
    // sound emanating from the enemy, so spatialising them would be misleading.
    // Missing files fail gracefully (the sound system logs and plays nothing).
    // NormalizedView must reference a static-lifetime literal; constexpr globals
    // satisfy that.
    constexpr VFS::Path::NormalizedView kInRangeSound("sounds/a11y/enemy_in_range.wav");
    constexpr VFS::Path::NormalizedView kOutOfRangeSound("sounds/a11y/enemy_out_of_range.wav");
    constexpr VFS::Path::NormalizedView kEnemyDiedSound("sounds/a11y/enemy_died.wav");

    // How close (world units) another actor must be for its spellcast to be
    // announced when it is NOT targeting the player. ~28 m: roughly the audible/
    // relevant neighbourhood, so the player hears a mage in the same room or
    // courtyard buffing or fighting someone else, without narrating every cast
    // across a whole exterior cell. Casts by an actor in combat with the player
    // are announced at any distance regardless of this. See
    // Scanner::announceActorSpellCast.
    constexpr float kSpellCastNearbyRange = 28.f * kUnitsPerMetre;

    // Player position-vector "forward" is along +Y in OpenMW's coordinate
    // system, and yaw rotates around Z. A target bearing relative to the
    // player is computed as the angle between (target - player) and the
    // player's facing direction.

    const char* categoryName(MWAccessibility::Category cat)
    {
        switch (cat)
        {
            case MWAccessibility::Category::Npcs:
                // "Actors" (NPCs + creatures) -- the enum value is historically
                // named Npcs, but the spoken category is "Actors" since "NPCs"
                // is also one of its subcategories.
                return "Actors";
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
            case MWAccessibility::Category::Locations:
                return "Locations";
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

    // The Items subcategory predicates delegate to the pure, unit-tested
    // classifyItemType (itembucket.hpp): membership is a function of the record
    // type alone, so the bucketing -- including the Misc catch-all -- lives in
    // one tested place rather than being re-spelled here.
    // NB: fully qualify MWAccessibility::ItemBucket -- this anonymous namespace
    // sits at global scope alongside the engine's Misc:: namespace, so an
    // unqualified ItemBucket::Misc would be misparsed as that namespace.
    bool isWeapon(const MWWorld::Ptr& p)
    {
        return MWAccessibility::classifyItemType(p.getType()) == MWAccessibility::ItemBucket::Weapon;
    }
    bool isArmor(const MWWorld::Ptr& p)
    {
        return MWAccessibility::classifyItemType(p.getType()) == MWAccessibility::ItemBucket::Armor;
    }
    bool isClothing(const MWWorld::Ptr& p)
    {
        return MWAccessibility::classifyItemType(p.getType()) == MWAccessibility::ItemBucket::Clothing;
    }
    bool isPotion(const MWWorld::Ptr& p)
    {
        return MWAccessibility::classifyItemType(p.getType()) == MWAccessibility::ItemBucket::Potion;
    }
    bool isIngredient(const MWWorld::Ptr& p)
    {
        return MWAccessibility::classifyItemType(p.getType()) == MWAccessibility::ItemBucket::Ingredient;
    }
    bool isBookOrScroll(const MWWorld::Ptr& p)
    {
        return MWAccessibility::classifyItemType(p.getType()) == MWAccessibility::ItemBucket::BookOrScroll;
    }
    bool isTool(const MWWorld::Ptr& p)
    {
        return MWAccessibility::classifyItemType(p.getType()) == MWAccessibility::ItemBucket::Tool;
    }
    bool isMiscItem(const MWWorld::Ptr& p)
    {
        return MWAccessibility::classifyItemType(p.getType()) == MWAccessibility::ItemBucket::Misc;
    }

    bool isNpcActor(const MWWorld::Ptr& p) { return p.getType() == ESM::NPC::sRecordId; }
    bool isCreatureActor(const MWWorld::Ptr& p) { return p.getType() == ESM::Creature::sRecordId; }

    // A "plant" container is one flagged Organic in its record: harvestable
    // flora (plants, mushrooms, etc.) you pick from rather than store into. This
    // is the engine's own authoritative flag (the same one Container::canLock /
    // canBeHarvested consult), NOT a name/model guess -- so it stays correct for
    // mod-added flora. Storage (chests, barrels, sacks, urns...) is simply the
    // complement: any non-organic container. Morrowind has no data field naming
    // the *kind* of storage, so we deliberately don't try to split chest from
    // barrel etc. (that would require fragile model/name matching).
    bool isPlantContainer(const MWWorld::Ptr& p)
    {
        if (p.getType() != ESM::Container::sRecordId)
            return false;
        const MWWorld::LiveCellRef<ESM::Container>* ref = p.get<ESM::Container>();
        return ref && ref->mBase && (ref->mBase->mFlags & ESM::Container::Organic) != 0;
    }
    bool isStorageContainer(const MWWorld::Ptr& p)
    {
        return p.getType() == ESM::Container::sRecordId && !isPlantContainer(p);
    }

    // "Hostile": an actor actively in combat with the player, i.e. the player
    // is among its AI combat targets. This is the reliable "trying to kill me
    // right now" signal -- narrower (and more useful) than a generic
    // isInCombat(), which is also true when the actor is fighting someone else
    // (a guard vs a rat). Dead actors are excluded so corpses don't linger in
    // the list. Used as the "Hostile" subcategory of the Actors category.
    bool isHostileActor(const MWWorld::Ptr& p)
    {
        if (!p.getClass().isActor())
            return false;
        const MWMechanics::CreatureStats& stats = p.getClass().getCreatureStats(p);
        if (stats.isDead())
            return false;
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return false;
        std::vector<MWWorld::Ptr> targets;
        stats.getAiSequence().getCombatTargets(targets);
        for (const MWWorld::Ptr& t : targets)
            if (t == player)
                return true;
        return false;
    }

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
        { "Hostile", &isHostileActor },
        { "NPCs", &isNpcActor },
        { "Creatures", &isCreatureActor },
    };

    constexpr Subcategory kContainerSubs[] = {
        { "All", nullptr },
        { "Plants", &isPlantContainer },
        { "Storage", &isStorageContainer },
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
            case MWAccessibility::Category::Containers:
                return { kContainerSubs, std::size(kContainerSubs) };
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
        // CRITICAL: only teleporting doors actually lead somewhere. A non-
        // teleport door (one that just swings open to another part of the same
        // cell -- common inside buildings and dungeons) can still carry stale
        // destination data in its cell ref, and an empty/default destCell
        // resolves to the exterior region name. That produced bogus
        // announcements like "Door, to Ashlands Region" for an ordinary
        // interior door. Gate on getTeleport() exactly as the engine's own door
        // tooltip does (see MWClass::Door::getToolTipInfo).
        if (!ptr.getCellRef().getTeleport())
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

    // The current open/closed state of a door, as a spoken word ("open" /
    // "closed"), or empty for a non-door. A blind player has no visual cue
    // whether a door is standing open or shut -- which matters now that auto-walk
    // opens doors and activation is blocked through closed ones. A door is
    // "closed" only when it is Idle AND sitting at its authored rotation; any
    // swing (currently opening/closing, or idle but rotated open) reads as
    // "open". Mirrors the open/closed test in MWClass::Door::activate.
    std::string doorStateLabel(const MWWorld::Ptr& ptr)
    {
        if (ptr.getType() != ESM::Door::sRecordId)
            return {};
        const float doorRot
            = ptr.getRefData().getPosition().rot[2] - ptr.getCellRef().getPosition().rot[2];
        const bool closed
            = ptr.getClass().getDoorState(ptr) == MWWorld::DoorState::Idle && doorRot == 0.0f;
        return closed ? "closed" : "open";
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

    MWWorld::Ptr Scanner::lockTarget() const
    {
        // No game running => the world (and thus mLockTarget's backing object)
        // may be torn down; never return a possibly-dangling Ptr. clear() also
        // resets mLockedOn on teardown, but this guard removes the reliance on
        // call-ordering invariants for the external (combat) consumers.
        if (MWBase::Environment::get().getStateManager()->getState()
            != MWBase::StateManager::State_Running)
            return MWWorld::Ptr();
        return mLockedOn ? mLockTarget : MWWorld::Ptr();
    }

    MWWorld::Ptr Scanner::selectedObject()
    {
        // Mirror lockTarget()'s running-state guard: the console can read this
        // while a game is being torn down (the console is a GuiMode that can
        // outlive a quickload), and a dangling cursor/lock Ptr would not be
        // caught by the caller's isEmpty() check, so never hand one out.
        if (MWBase::Environment::get().getStateManager()->getState()
            != MWBase::StateManager::State_Running)
            return MWWorld::Ptr();
        // Prefer the locked-on target (what the player is actively engaged
        // with), else the current scanner cursor. currentTarget() already
        // returns empty for the position-based waypoint categories (it indexes
        // mObjects, which is empty there), so those can't leak in.
        MWWorld::Ptr target = mLockedOn ? mLockTarget : MWWorld::Ptr();
        if (target.isEmpty())
            target = currentTarget();
        return target;
    }

    void Scanner::clear()
    {
        // Release the lock-on and drop every cached MWWorld::Ptr. Called when a
        // game is loaded or ended (StateManager::cleanup), which tears down the
        // world synchronously and frees the cell refs our Ptrs point at. Keeping
        // any would leave a dangling target that updateLockOn() dereferences on
        // the next frame (mLockTarget.getCellRef()...), crashing on quickload.
        // Also reset cell tracking and the cell-name priming so the freshly
        // loaded cell is recorded silently and lists rebuild from scratch.
        mLockedOn = false;
        mLockTarget = MWWorld::Ptr();
        mLockTargetName.clear();
        for (auto& s : mLists)
        {
            s.mObjects.clear();
            s.mWaypoints.clear();
            s.mIndex = -1;
            s.mSelectedRef = ESM::RefNum{};
            s.mDirty = true;
        }
        // The auto-walker and proximity cue each cache a Ptr to the object they
        // are chasing / homing on and dereference it every frame (getCellRef,
        // getRefData). On world teardown that backing object is freed, so -- as
        // with the lock target above -- we must release them here, or the next
        // onFrame (state back to Running after a synchronous quickload) would
        // hit AutoWalker::onFrame / ProximityCue::onFrame and dereference freed
        // memory. isEmpty() can't catch a dangling-but-non-null Ptr, so the
        // per-frame "target gone" checks there are NOT sufficient on their own.
        mAutoWalker.cancel();
        mProximityCue.stop();
        mLastCellId = nullptr;
        mCellNamePrimed = false;
        mMeleeReachCooldown = 0.f;

        // Drop all AHUD state and lift our pause tag if held, so a HUD left open
        // when the world is torn down (e.g. the player loaded a save from the
        // HUD) can't strand the new game frozen.
        mHud.reset();
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
        {
            mCellNamePrimed = false;

            // Belt-and-braces: also drop cached Ptrs whenever no game is
            // running. The authoritative teardown hook is Scanner::clear(),
            // called synchronously from StateManager::cleanup (a quickload
            // unloads + reloads + returns to Running within one input handler,
            // so this onFrame branch may never see a non-Running state). This
            // remains as a cheap safety net for any path that lands here with a
            // game not running -- e.g. sitting at the main menu.
            clear();
        }

        if (!isGameplayActive())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // While the HUD is open and parked on the target row, keep that row in
        // sync with whichever actor the player cycles the scanner to.
        mHud.followTarget();

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
        announceDrawStateChange();

        // Tick down the out-of-range melee speech throttle (see
        // announceMeleeReach). Clamp at 0 so it doesn't run negative.
        if (mMeleeReachCooldown > 0.f)
            mMeleeReachCooldown = std::max(0.f, mMeleeReachCooldown - dt);

        // Keep the Actors list live ONLY while the Hostile subcategory is
        // active: in a fight, attackers move and new ones turn hostile, so a
        // list cached at selection time goes stale (a new attacker won't appear,
        // membership shifts). But for the other Actors views (All / NPCs /
        // Creatures) -- and every other category -- a live rebuild is harmful:
        // when just browsing a town, NPCs wandering around would re-sort the
        // list under the cursor, making you lose your place and skip people.
        // (Distances are recomputed on demand each time you read an item, so
        // they stay accurate without a live rebuild.) The refresh is silent and
        // preserves the cursor.
        const auto [npcSubs, npcSubCount] = subcategoriesFor(mCategory);
        const int activeSub = mLists[static_cast<size_t>(mCategory)].mSubIndex;
        const bool hostileView = mCategory == Category::Npcs && activeSub >= 0
            && activeSub < static_cast<int>(npcSubCount) && npcSubs[activeSub].mName == std::string_view("Hostile");
        if (hostileView)
        {
            mActorRefreshTimer += dt;
            if (mActorRefreshTimer >= kActorRefreshInterval)
            {
                mActorRefreshTimer = 0.f;
                refreshActiveListPreservingSelection();
            }
        }
        else
            mActorRefreshTimer = 0.f;
    }

    bool Scanner::handleKey(int scancode, int modState)
    {
        if (!isGameplayActive())
            return false;

        bool ctrl = (modState & KMOD_CTRL) != 0;
        bool shift = (modState & KMOD_SHIFT) != 0;
        bool alt = (modState & KMOD_ALT) != 0;

        // Quick-info keys (Alt modifier) work both in normal gameplay and while
        // the AHUD is open, so handle them up front before the auto-walk-cancel
        // and the main key switch. Alt+H/M/F read the player's health / magicka
        // / fatigue; Shift+Alt+H reads the current enemy's health.
        if (alt && !ctrl)
        {
            switch (scancode)
            {
                case SDL_SCANCODE_H:
                    if (shift)
                        announceEnemyHealth();
                    else
                        announcePlayerHealth();
                    return true;
                case SDL_SCANCODE_M:
                    announcePlayerMagicka();
                    return true;
                case SDL_SCANCODE_F:
                    announcePlayerFatigue();
                    return true;
                default:
                    break;
            }
        }

        // While the AHUD is open, give its navigation keys (arrows, Enter,
        // Home, Escape/Left to back out of the effects sub-list) first crack.
        // Anything it doesn't consume falls through to the scanner keys below,
        // which keep working while the HUD is up (the player can still cycle
        // scan targets, lock on, etc.). H / Escape that aren't consumed here
        // reach the main switch and close the HUD.
        if (mHud.isActive() && mHud.handleKey(scancode, ctrl, shift, alt))
            return true;

        // Pressing a movement key while auto-walk is active should cancel it
        // cleanly. We don't consume the key; the player still wants to move.
        // NOTE: Space is deliberately excluded -- it's the default Activate
        // binding, so the player auto-walks to an object and presses Space to
        // interact with it on arrival. Cancelling on Space would make that
        // impossible (and Space wouldn't reach the activate handler).
        //
        // Use the player's *real* movement bindings rather than hardcoded WASD,
        // so a remapped or non-QWERTY (AZERTY/Dvorak/Colemak) player can still
        // cancel with their own forward/left/back/right keys -- and so a key
        // that's no longer movement for them doesn't cancel unexpectedly.
        if (mAutoWalker.isActive() && isMovementKey(scancode))
        {
            speak("Auto-walk cancelled.");
            mAutoWalker.cancel();
        }

        // Ctrl+number: jump straight to a category, skipping the cycle. The
        // order matches the Category enum / the cycle order, so the muscle
        // memory is the same as Ctrl+PageDown stepping. Plain number keys are
        // the engine's quick-keys (item/spell slots), so we only claim the
        // Ctrl-modified combo and let bare numbers fall through. Conditional
        // categories (Detected/Waypoints/Locations) are entered on request even
        // when empty -- selectCategory announces "0 in range" honestly.
        if (ctrl && !alt)
        {
            int catIndex = -1;
            switch (scancode)
            {
                case SDL_SCANCODE_1: catIndex = static_cast<int>(Category::Npcs); break;
                case SDL_SCANCODE_2: catIndex = static_cast<int>(Category::Doors); break;
                case SDL_SCANCODE_3: catIndex = static_cast<int>(Category::Containers); break;
                case SDL_SCANCODE_4: catIndex = static_cast<int>(Category::Items); break;
                case SDL_SCANCODE_5: catIndex = static_cast<int>(Category::Activators); break;
                case SDL_SCANCODE_6: catIndex = static_cast<int>(Category::Detected); break;
                case SDL_SCANCODE_7: catIndex = static_cast<int>(Category::Waypoints); break;
                case SDL_SCANCODE_8: catIndex = static_cast<int>(Category::Locations); break;
                default: break;
            }
            if (catIndex >= 0)
            {
                selectCategory(static_cast<Category>(catIndex));
                return true;
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
            case SDL_SCANCODE_X:
                // X toggles combat/interaction lock-on to the selected target.
                // While locked, the player is kept aimed at it so melee,
                // spells, and lockpicks/probes connect without manual aiming.
                // X (left hand) is chosen for ergonomics: the right hand stays on
                // the mouse to swing/aim while the left switches targets.
                // Shift+X is the one-key combat opener: jump to the nearest
                // hostile (Actors / Hostile) and lock onto it in a single press.
                if (shift && !ctrl && !alt)
                    engageNearestHostile();
                else
                    toggleLockOn();
                return true;
            case SDL_SCANCODE_H:
                // Toggle the accessible HUD (pauses the world; scanner +
                // quick-info keys keep working). Plain H only -- Alt+H is the
                // quick health readout handled above.
                if (!ctrl && !shift && !alt)
                {
                    mHud.toggle();
                    return true;
                }
                return false;
            case SDL_SCANCODE_ESCAPE:
                // Escape closes the AHUD if it's open (and only then do we
                // consume it, so Escape behaves normally otherwise).
                if (mHud.isActive())
                {
                    mHud.toggle();
                    return true;
                }
                return false;
            case SDL_SCANCODE_LEFT:
                // Ctrl+Left snaps facing to the previous (counter-clockwise)
                // compass point. (Ctrl is the default sneak key, but the arrow
                // keys aren't bound to movement, so this doesn't conflict with
                // sneaking + WASD.)
                if (ctrl && !shift && !alt)
                {
                    snapToDirection(/*clockwise=*/false);
                    return true;
                }
                return false;
            case SDL_SCANCODE_RIGHT:
                // Ctrl+Right snaps facing to the next (clockwise) compass point.
                if (ctrl && !shift && !alt)
                {
                    snapToDirection(/*clockwise=*/true);
                    return true;
                }
                return false;
            case SDL_SCANCODE_DOWN:
                // Ctrl+Down turns the player 180 degrees.
                if (ctrl && !shift && !alt)
                {
                    turnAround();
                    return true;
                }
                return false;
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
            // Available only when the player has at least one waypoint anywhere
            // (a dropped map note or a Mark), so it's skipped when cycling
            // otherwise -- just like Detected.
            std::vector<Waypoint> wps;
            collectWaypoints(wps);
            return !wps.empty();
        }
        if (cat == Category::Locations)
        {
            // Available only once the player has discovered at least one named
            // location on the global map (or an NPC has marked one).
            std::vector<Waypoint> locs;
            collectLocations(locs);
            return !locs.empty();
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
        selectCategory(static_cast<Category>(cur));
    }

    void Scanner::selectCategory(Category cat)
    {
        mCategory = cat;
        // Force a rebuild of the new category's list and announce its
        // size, then auto-select the first (nearest) entry.
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
            // An unreachable waypoint (interior / other worldspace) has no
            // comparable position, so there's no meaningful direction to face.
            if (!wp->mReachable)
            {
                speak(wp->mName + " is in a different area; cannot face it.");
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
        lockOnCurrentTarget();
    }

    bool Scanner::lockOnCurrentTarget()
    {
        // Lock-on is for world objects (actors to fight, chests/doors to pick),
        // not the position-based Waypoints category.
        if (isWaypointCategory())
        {
            speak("Cannot lock onto a waypoint.");
            return false;
        }

        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
        {
            speak("No target selected.");
            return false;
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
        return true;
    }

    void Scanner::engageNearestHostile()
    {
        // Jump straight to the Actors category, Hostile subcategory. Both are
        // forced via mDirty so the list reflects who is attacking *right now*,
        // then sorted nearest-first by rebuildCurrentList -- so index 0 is the
        // closest attacker.
        mCategory = Category::Npcs;
        auto& state = mLists[static_cast<size_t>(Category::Npcs)];

        // Find the "Hostile" subcategory index by name rather than hardcoding
        // it, so reordering kNpcSubs can't silently point this at the wrong
        // filter.
        auto [subs, subCount] = subcategoriesFor(Category::Npcs);
        int hostileSub = 0;
        for (int i = 0; subs && i < static_cast<int>(subCount); ++i)
        {
            if (subs[i].mName == std::string_view("Hostile"))
            {
                hostileSub = i;
                break;
            }
        }
        state.mSubIndex = hostileSub;
        state.mDirty = true;
        rebuildCurrentList();

        if (state.mObjects.empty())
        {
            // Honest negative feedback -- nothing is in combat with the player.
            // Leave the category switched (the player asked for it) but don't
            // pretend to have locked.
            speak("No hostiles nearby.");
            updateProximityCue();
            return;
        }

        // Nearest attacker is first after the distance sort.
        state.mIndex = 0;
        // Re-aim/lock onto it. lockOnCurrentTarget announces "Locked onto X."
        lockOnCurrentTarget();
        updateProximityCue();
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
            // Reinforce the spoken "<Name> is dead." with a cue, since the
            // death of your locked target is exactly the moment combat chatter
            // is loudest and the speech is most likely to be missed.
            MWBase::Environment::get().getSoundManager()->playSound(
                kEnemyDiedSound, /*volume=*/1.0f, /*pitch=*/1.0f);
            speak(mLockTargetName + " is dead.");
            releaseLockOn(/*announce=*/false);
            return;
        }

        // Proactive in/out-of-range cue for the locked target. Done before the
        // re-aim below so the early "target on top of us" return can't skip it.
        updateRangeCue();

        // Re-aim the player at the target (yaw + pitch). The engine's combat/use
        // systems all resolve their target from the player's facing direction,
        // so holding this aim is what makes melee, spells, and lockpicks/probes
        // connect. We set absolute orientation via rotateObject; with no mouse
        // input there are no competing deltas, so the aim holds steady (same
        // mechanism as focusCamera()). WASD movement stays relative to this
        // facing, so the player can advance/strafe.
        //
        // CRITICAL: aim the pitch from the projectile/attack ORIGIN to the
        // target's CENTRE -- not foot-origin to foot-origin. The position
        // vectors above are at each object's base (feet); pitching foot-to-foot
        // badly under-aims downward targets.
        //
        // Use TORSO height (0.75) for our origin, because that is exactly where
        // the engine launches a target spell's magic bolt from
        // (ProjectileManager::launchMagicBolt uses Constants::TorsoHeight). A
        // bolt flies in a straight line PARALLEL to our facing, starting at the
        // torso. If we instead pitched from a higher point (e.g. the eye/camera
        // at 0.85), the bolt -- launched 0.10*height lower but flying parallel
        // to the eye->centre line -- arrives that same ~13 units BELOW centre,
        // dropping into the target's legs/feet and clipping low clutter (a chest
        // or the ground) on the way at range. That produced intermittent ranged
        // spell misses that worked up close (before the trajectory had dropped
        // into anything). Matching the origin to the actual launch height makes
        // the bolt pass through centre at any distance.
        //
        // This is safe for the other interactions: lockpick/probe/activate and
        // touch-on-object spells (Open) now use the locked target DIRECTLY
        // rather than a camera ray (see CharacterController / World::castSpell),
        // so they don't depend on this pitch; melee getHitContact() uses a
        // tolerant facing cone, not a thin ray. Only the projectile is angle-
        // sensitive, so we optimise the aim for it.
        const float playerTorso = world->getHalfExtents(player, /*rendering=*/true).z() * 2.f * 0.75f;
        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        playerPos.z() += playerTorso;
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

    Scanner::HitState Scanner::computeHitState(
        const MWWorld::Ptr& player, const MWWorld::Ptr& target) const
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();

        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        const MWMechanics::DrawState draw = stats.getDrawState();

        // Helper: is the straight-line path from the player's torso to the
        // target's centre clear? Mirrors announceNoClearShot's trajectory and
        // collision set exactly, so the cue and the spoken warning agree.
        auto hasClearShot = [&]() -> bool {
            const float playerTorso = world->getHalfExtents(player, /*rendering=*/true).z() * 2.f * 0.75f;
            osg::Vec3f from = player.getRefData().getPosition().asVec3();
            from.z() += playerTorso;
            osg::Vec3f to = target.getRefData().getPosition().asVec3();
            to.z() += world->getHalfExtents(target, /*rendering=*/true).z();
            const MWPhysics::RayCastingInterface* rayCasting = world->getRayCasting();
            if (!rayCasting)
                return true; // can't test -> don't claim a block
            const int mask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap
                | MWPhysics::CollisionType_Door | MWPhysics::CollisionType_Actor;
            const MWPhysics::RayCastingResult result = rayCasting->castRay(from, to, { player }, {}, mask);
            return !result.mHit || result.mHitObject == target;
        };

        // Helper: in melee/touch reach? Uses the same engine math (and the same
        // fCombatDistance fallback) as announceOutOfReach.
        auto inMeleeReach = [&](float reach) -> bool {
            return MWMechanics::isInMeleeReach(player, target, reach);
        };

        if (draw == MWMechanics::DrawState::Weapon)
        {
            // Classify the readied weapon. Hand-to-hand (no weapon in the right
            // hand) is melee. Ranged (bow/crossbow) and thrown use the clear-
            // shot test; everything else is melee reach.
            MWWorld::InventoryStore& inv = player.getClass().getInventoryStore(player);
            auto slot = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
            if (slot == inv.end() || slot->isEmpty() || slot->getType() != ESM::Weapon::sRecordId)
            {
                // Hand to hand: melee reach with the unarmed (empty) weapon.
                return inMeleeReach(MWMechanics::getMeleeWeaponReach(player, MWWorld::Ptr()))
                    ? HitState::InRange
                    : HitState::OutOfRange;
            }
            const int weaponType = slot->get<ESM::Weapon>()->mBase->mData.mType;
            const ESM::WeaponType::Class weaponClass = MWMechanics::getWeaponType(weaponType)->mWeaponClass;
            if (weaponClass == ESM::WeaponType::Ranged || weaponClass == ESM::WeaponType::Thrown)
                return hasClearShot() ? HitState::InRange : HitState::OutOfRange;
            return inMeleeReach(MWMechanics::getMeleeWeaponReach(player, *slot))
                ? HitState::InRange
                : HitState::OutOfRange;
        }

        if (draw == MWMechanics::DrawState::Spell)
        {
            // Resolve the readied spell's (or selected enchanted item's) effect
            // ranges. A target-range effect launches a bolt (use the clear-shot
            // test); a touch effect uses melee reach; a self-only spell has no
            // meaningful "range to enemy", so we give no cue.
            const ESM::EffectList* effects = nullptr;
            // CRITICAL: read the readied spell from the WindowManager, NOT from
            // CreatureStats. For the PLAYER, selecting a spell in the UI does
            // not update CreatureStats' selected spell (the engine keeps that
            // frozen during casting; see mwlua/types/actor.cpp ~line 230), so
            // stats.getSpells().getSelectedSpell() is empty here -- which made
            // the whole spell branch resolve no effects and return Unknown (no
            // cue for any readied spell). The WindowManager holds the player's
            // actually-selected spell.
            const ESM::RefId selected = MWBase::Environment::get().getWindowManager()->getSelectedSpell();
            if (!selected.empty())
            {
                if (const ESM::Spell* spell = world->getStore().get<ESM::Spell>().search(selected))
                    effects = &spell->mEffects;
            }
            else
            {
                MWWorld::InventoryStore& inv = player.getClass().getInventoryStore(player);
                if (inv.getSelectedEnchantItem() != inv.end())
                {
                    const MWWorld::Ptr item = *inv.getSelectedEnchantItem();
                    if (!item.isEmpty())
                    {
                        const ESM::Enchantment* ench
                            = world->getStore().get<ESM::Enchantment>().search(
                                item.getClass().getEnchantment(item));
                        if (ench)
                            effects = &ench->mEffects;
                    }
                }
            }
            if (!effects)
                return HitState::Unknown;

            bool hasTarget = false;
            bool hasTouch = false;
            for (const ESM::IndexedENAMstruct& e : effects->mList)
            {
                if (e.mData.mRange == ESM::RT_Target)
                    hasTarget = true;
                else if (e.mData.mRange == ESM::RT_Touch)
                    hasTouch = true;
            }
            // A ranged ("target") effect dominates: that's the bolt the lock-on
            // aims, and the relevant question is whether the path is clear.
            if (hasTarget)
                return hasClearShot() ? HitState::InRange : HitState::OutOfRange;
            if (hasTouch)
            {
                const float fCombatDistance
                    = world->getStore().get<ESM::GameSetting>().find("fCombatDistance")->mValue.getFloat();
                return inMeleeReach(fCombatDistance) ? HitState::InRange : HitState::OutOfRange;
            }
            // Self-only spell: no enemy-range concept.
            return HitState::Unknown;
        }

        // Nothing readied (DrawState::Nothing) -> no cue.
        return HitState::Unknown;
    }

    void Scanner::updateRangeCue()
    {
        // Only while locked onto a live actor. Any other situation resets the
        // edge tracker so the next lock starts clean (and the cue-on-lock fires).
        if (!mLockedOn || mLockTarget.isEmpty() || !mLockTarget.getClass().isActor()
            || mLockTarget.getClass().getCreatureStats(mLockTarget).isDead())
        {
            mLastHitState = HitState::Unknown;
            return;
        }

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        const HitState now = computeHitState(player, mLockTarget);

        // No relevant weapon/spell readied: stay silent, but DON'T collapse the
        // remembered in/out state to Unknown -- otherwise readying a weapon
        // again would re-fire the cue for a state the player already knows.
        // Only a genuine InRange<->OutOfRange transition plays a sound.
        if (now == HitState::Unknown)
            return;

        if (now == mLastHitState)
            return;
        mLastHitState = now;

        MWBase::Environment::get().getSoundManager()->playSound(
            now == HitState::InRange ? kInRangeSound : kOutOfRangeSound,
            /*volume=*/1.0f, /*pitch=*/1.0f);
    }

    void Scanner::announceOutOfReach(float reach)
    {
        // CRITICAL: bail unless a game is actually running. This is called from
        // the combat/cast path (prepareHit, castSpell), which can run during a
        // save load's teardown frame -- a queued melee swing resolves while the
        // world is being torn down. At that point mLockTarget may be dangling
        // (the lock is cleared by our own onFrame, but combat updates can fire
        // first), so touching it would dereference freed memory and crash on
        // quickload. The Running-state gate matches where onFrame clears the
        // lock; in-game menus stay Running, so this doesn't suppress legitimate
        // in-combat warnings.
        if (MWBase::Environment::get().getStateManager()->getState()
            != MWBase::StateManager::State_Running)
            return;

        // Only meaningful when locked onto a live actor: the message tells the
        // player their attack on *that enemy* won't land. Without a lock we have
        // no specific target to reason about (a sighted player would see the
        // whiff), and non-actor locks (a chest) aren't melee/touch combat
        // targets.
        if (!mLockedOn || mLockTarget.isEmpty())
            return;
        if (!mLockTarget.getClass().isActor()
            || mLockTarget.getClass().getCreatureStats(mLockTarget).isDead())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Use the engine's own reach math so our notion of "in range" matches
        // what the hit actually requires. isInMeleeReach() ANDs two conditions:
        // a vertical check (|height difference| < reach) and a horizontal
        // distance-to-bounds check. We evaluate them separately so we can tell
        // the player *why* it won't connect. (Touch spells resolve their actor
        // target through the same melee cone, so the same reach math applies.)
        if (MWMechanics::isInMeleeReach(player, mLockTarget, reach))
            return; // Actually in range -- the attack can land, so stay silent.

        // Throttle: a held swing (or repeated casts) resolves several times a
        // second.
        if (mMeleeReachCooldown > 0.f)
            return;
        mMeleeReachCooldown = kMeleeReachAnnounceInterval;

        // Distinguish the two failure modes. Mirror isInMeleeReach's tests:
        // if the horizontal distance is within reach but the height isn't, the
        // target is simply too far above or below us (a cliff racer overhead, an
        // enemy atop a ledge), which closing the horizontal gap won't fix.
        const float heightDiff = player.getRefData().getPosition().pos[2]
            - mLockTarget.getRefData().getPosition().pos[2];
        const bool horizOk = MWMechanics::getDistanceToBounds(player, mLockTarget) < reach;
        if (horizOk && std::abs(heightDiff) >= reach)
            speak(heightDiff < 0.f ? "Target too high." : "Target too low.");
        else
            speak("Out of range.");
    }

    void Scanner::announceNoClearShot()
    {
        // Same teardown guard as announceOutOfReach: the cast path can resolve
        // during a save-load frame, when mLockTarget may be dangling.
        if (MWBase::Environment::get().getStateManager()->getState()
            != MWBase::StateManager::State_Running)
            return;

        // Only meaningful when locked onto a live actor -- that's the target the
        // bolt is aimed at (updateLockOn holds the player's facing on it).
        if (!mLockedOn || mLockTarget.isEmpty())
            return;
        if (!mLockTarget.getClass().isActor()
            || mLockTarget.getClass().getCreatureStats(mLockTarget).isDead())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Reconstruct the exact bolt trajectory lock-on aims along: from the
        // player's torso (where launchMagicBolt spawns the bolt) to the
        // target's vertical centre. Keep this in sync with updateLockOn().
        const float playerTorso = world->getHalfExtents(player, /*rendering=*/true).z() * 2.f * 0.75f;
        osg::Vec3f from = player.getRefData().getPosition().asVec3();
        from.z() += playerTorso;
        osg::Vec3f to = mLockTarget.getRefData().getPosition().asVec3();
        to.z() += world->getHalfExtents(mLockTarget, /*rendering=*/true).z();

        // Cast along the trajectory using the projectile's own collision set
        // (world geometry, terrain, doors, actors), ignoring the player so we
        // don't self-block at the origin. A clear shot hits the locked target
        // first; anything else hit before it means a wall/pillar/clutter (or
        // another actor) is in the way and the bolt won't reach the target.
        const MWPhysics::RayCastingInterface* rayCasting = world->getRayCasting();
        if (!rayCasting)
            return;
        const int mask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap
            | MWPhysics::CollisionType_Door | MWPhysics::CollisionType_Actor;
        const MWPhysics::RayCastingResult result
            = rayCasting->castRay(from, to, { player }, {}, mask);

        // No hit at all (open line to the target) => clear shot, stay silent.
        // A hit ON the locked target => clear shot. A hit on anything else =>
        // blocked.
        if (!result.mHit || result.mHitObject == mLockTarget)
            return;

        // Throttle with the shared reach cooldown so a flurry of casts (or a
        // held cast) doesn't spam, and so this doesn't double up with the
        // touch/melee out-of-reach line.
        if (mMeleeReachCooldown > 0.f)
            return;
        mMeleeReachCooldown = kMeleeReachAnnounceInterval;
        speak("No clear shot.");
    }

    void Scanner::announceActorSpellCast(
        const MWWorld::Ptr& caster, const std::string& sourceName, bool targetsOutward)
    {
        // Guard against being called outside a running game (the cast path can
        // run during scripted setup / teardown). Mirrors announceOutOfReach.
        if (MWBase::Environment::get().getStateManager()->getState()
            != MWBase::StateManager::State_Running)
            return;

        if (caster.isEmpty() || !caster.getClass().isActor())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty() || caster == player)
            return; // The player's own casts are covered by ready announcements.

        // Is the caster actively in combat with the player? This is our "aimed
        // at you" signal: actors cast with an empty target (the engine resolves
        // the real target post-cast via hit contact / projectile), so we can't
        // read intent from the spell itself -- but an actor fighting the player
        // who casts an outward spell is, in practice, casting at them.
        const bool inCombatWithPlayer
            = caster.getClass().getCreatureStats(caster).getAiSequence().isInCombat(player);

        // Announce a cast if it's either near the player or by someone fighting
        // them (at any distance). A distant mage buffing allies in another part
        // of the cell isn't worth narrating; an enemy lobbing spells at the
        // player from across a room always is.
        const float distSq
            = (caster.getRefData().getPosition().asVec3() - player.getRefData().getPosition().asVec3())
                  .length2();
        const bool nearby = distSq <= kSpellCastNearbyRange * kSpellCastNearbyRange;
        if (!inCombatWithPlayer && !nearby)
            return;

        std::string text = objectDisplayName(caster) + " casts " + sourceName;
        // Only claim "at you" when the spell actually reaches outward (has a
        // touch/target effect) AND the caster is fighting the player; a self-
        // buff cast mid-fight shouldn't be reported as aimed at the player.
        if (inCombatWithPlayer && targetsOutward)
            text += " at you";
        text += ".";
        speak(text);
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
        // Reset the range-cue edge tracker so the next lock-on re-evaluates from
        // scratch and fires its initial in/out cue.
        mLastHitState = HitState::Unknown;
    }

    void Scanner::refreshActiveListPreservingSelection()
    {
        auto& state = mLists[static_cast<size_t>(mCategory)];

        // Capture the current selection's stable identity so the rebuild can
        // re-pin the cursor onto the same physical object even though the list
        // re-sorts by distance (and membership may change). If nothing is
        // selected, clear the remembered ref so the rebuild doesn't resurrect a
        // stale one.
        if (state.mIndex >= 0 && state.mIndex < static_cast<int>(state.mObjects.size()))
            state.mSelectedRef = state.mObjects[state.mIndex].getCellRef().getRefNum();
        else
            state.mSelectedRef = ESM::RefNum{};

        state.mDirty = true;
        rebuildCurrentList();

        // Keep the audio beacon homing on the (possibly moved) selection.
        updateProximityCue();
    }

    void Scanner::announceDrawStateChange()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        const MWMechanics::DrawState state
            = player.getClass().getCreatureStats(player).getDrawState();
        const int stateInt = static_cast<int>(state);
        if (stateInt == mLastDrawState)
            return;
        const int prev = mLastDrawState;
        mLastDrawState = stateInt;
        // Don't announce the initial baseline (first poll of a freshly loaded
        // game): the player hasn't just pressed anything, so it would be noise.
        if (prev < 0)
            return;

        switch (state)
        {
            case MWMechanics::DrawState::Weapon:
            {
                // Name the readied weapon, or "Hand to hand" when unarmed.
                std::string name = "Hand to hand";
                MWWorld::InventoryStore& inv = player.getClass().getInventoryStore(player);
                auto slot = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                if (slot != inv.end() && !slot->isEmpty())
                    name = slot->getClass().getName(*slot);
                speak(name + " ready.");
                break;
            }
            case MWMechanics::DrawState::Spell:
            {
                // Name the readied spell, or the selected enchanted item if a
                // magic item is what's equipped for casting.
                std::string name;
                // WindowManager, not CreatureStats: the player's UI spell
                // selection does not update CreatureStats (see computeHitState),
                // so reading CreatureStats here would always fall through to the
                // generic "Magic ready." instead of naming the spell.
                const ESM::RefId selected
                    = MWBase::Environment::get().getWindowManager()->getSelectedSpell();
                if (!selected.empty())
                {
                    const ESM::Spell* spell = world->getStore().get<ESM::Spell>().search(selected);
                    if (spell)
                        name = spell->mName;
                }
                if (name.empty())
                {
                    MWWorld::InventoryStore& inv = player.getClass().getInventoryStore(player);
                    if (inv.getSelectedEnchantItem() != inv.end())
                    {
                        const MWWorld::Ptr item = *inv.getSelectedEnchantItem();
                        if (!item.isEmpty())
                            name = item.getClass().getName(item);
                    }
                }
                if (name.empty())
                    speak("Magic ready.");
                else
                    speak(name + " ready.");
                break;
            }
            case MWMechanics::DrawState::Nothing:
            default:
                // Distinguish what was put away so the player knows which mode
                // they left (matches the two ready announcements).
                if (prev == static_cast<int>(MWMechanics::DrawState::Spell))
                    speak("Magic put away.");
                else
                    speak("Weapon sheathed.");
                break;
        }
    }

    namespace
    {
        // Distance (world units) from \p fromPos to the nearest point of \p
        // target's world-space bounding box, or to its origin as a fallback when
        // no renderable bounds are available.
        //
        // Why bounds, not the origin: a reference's origin sits at its authored
        // pivot, which for many statics (e.g. a hollow tree stump) is at the
        // base, often sunk into the terrain. Measuring origin-to-origin then
        // reports the player as several metres away even while they stand right
        // on top of the object -- and for an object below them there's no way to
        // close that vertical gap on foot. Vanilla doesn't hit this because it
        // activates via a camera ray that strikes the visible SURFACE; the
        // nearest-bounding-box point is our equivalent of that surface hit.
        float distanceToBounds(const osg::Vec3f& fromPos, const MWWorld::Ptr& target)
        {
            const osg::Vec3f origin = target.getRefData().getPosition().asVec3();
            auto* node = target.getRefData().getBaseNode();
            if (!node)
                return (origin - fromPos).length();

            osg::ComputeBoundsVisitor cb;
            cb.setTraversalMask(~(MWRender::Mask_ParticleSystem | MWRender::Mask_Effect));
            node->accept(cb);
            const osg::BoundingBox& bb = cb.getBoundingBox();
            if (!bb.valid())
                return (origin - fromPos).length();

            // Clamp the point to the box on each axis; the distance to that
            // clamped point is the distance to the nearest surface (0 if inside).
            const osg::Vec3f nearest(std::clamp(fromPos.x(), bb.xMin(), bb.xMax()),
                std::clamp(fromPos.y(), bb.yMin(), bb.yMax()),
                std::clamp(fromPos.z(), bb.zMin(), bb.zMax()));
            return (nearest - fromPos).length();
        }
    }

    bool Scanner::isWithinActivationReach(const MWWorld::Ptr& target)
    {
        if (target.isEmpty() || !isGameplayActive())
            return false;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return false;

        // Gate on the engine's activation distance, matching vanilla reach. The
        // audio beacon already guides the player into range, so this just
        // prevents interacting with things across the room. Measure to the
        // target's nearest bounding-box surface rather than its origin, so an
        // object whose pivot is sunk into the ground (a tree stump, a floor
        // hatch) still reads as close when the player stands on/over it -- see
        // distanceToBounds.
        //
        // Telekinesis extends reach: the engine (World::getFocusObject) adds
        // feetToGameUnits(telekinesisMagnitude) to the activation distance, but
        // only honours it for objects whose class allows telekinesis (items and
        // most doors yes; actors no; an unlocked, untrapped teleport door no).
        // Mirror that exactly so a sighted player casting Telekinesis and a
        // screen-reader player get identical reach -- otherwise we'd refuse
        // things the game would happily let you grab/open at range.
        float reach = world->getMaxActivationDistance();
        if (target.getClass().allowTelekinesis(target))
        {
            const float telekinesisMagnitude = player.getClass()
                                                   .getCreatureStats(player)
                                                   .getMagicEffects()
                                                   .getOrDefault(ESM::MagicEffect::Telekinesis)
                                                   .getMagnitude();
            if (telekinesisMagnitude > 0.0f)
                // feetToGameUnits: the engine rounds units-per-foot up (see
                // World::feetToGameUnits), so match with std::ceil.
                reach += telekinesisMagnitude * std::ceil(Constants::UnitsPerFoot);
        }

        const float dist = distanceToBounds(player.getRefData().getPosition().asVec3(), target);
        return dist <= reach;
    }

    void Scanner::announceTooFarAway(const MWWorld::Ptr& target)
    {
        if (target.isEmpty() || !isGameplayActive())
            return;
        speak(objectDisplayName(target) + " is too far away.");
    }

    bool Scanner::activateTarget()
    {
        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
            return false; // Nothing selected; let the default Activate run.

        if (!isWithinActivationReach(target))
        {
            announceTooFarAway(target);
            return true; // Consume: we handled it (by refusing), don't also
                         // fire the crosshair Activate.
        }

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();

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
            // Can't auto-walk to a waypoint in another worldspace: there's no
            // continuous navmesh path across a door/teleport, and its raw XY
            // isn't comparable to ours. Tell the user where it is instead.
            if (!wp->mReachable)
            {
                std::string where = wp->mAreaLabel.empty() ? std::string("a different area") : wp->mAreaLabel;
                speak(wp->mName + " is in " + where + "; cannot walk there from here.");
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

    void Scanner::snapToDirection(bool clockwise)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Eight compass points, 45 degrees apart. Snap to the next point in the
        // chosen direction.
        const float sector = 2.f * kPi / 8.f;

        // IMPORTANT: normalize the read yaw into [0, 2*PI) first. The engine
        // stores yaw normalized to roughly [-PI, PI], so a heading like 315 deg
        // comes back as -45 deg; if we didn't normalize, the index arithmetic
        // near that wrap boundary kept re-selecting the same point (the snap
        // "got stuck" after a few presses). Working in [0, 2*PI) makes the
        // sector index well-defined everywhere.
        float yaw = player.getRefData().getPosition().rot[2];
        while (yaw < 0.f)
            yaw += 2.f * kPi;
        while (yaw >= 2.f * kPi)
            yaw -= 2.f * kPi;

        // Position within the 8-sector ring, in [0, 8). The next point in a
        // direction is the next integer that way; a small tolerance means that
        // being essentially ON a point advances a full step rather than snapping
        // back to the same one.
        const float s = yaw / sector;
        const float tol = 1e-3f;
        int idx = clockwise ? static_cast<int>(std::floor(s + tol)) + 1
                            : static_cast<int>(std::ceil(s - tol)) - 1;
        idx = ((idx % 8) + 8) % 8;
        const float targetYaw = idx * sector;

        // Level the pitch (rot[0]) so snapping also looks straight ahead; keep
        // roll at 0. Absolute orientation, same mechanism as focusCamera().
        world->rotateObject(player, osg::Vec3f(0.f, 0.f, targetYaw), MWBase::RotationFlag_none);
        speak(std::string("Facing ") + compassLabel(targetYaw) + ".");
    }

    void Scanner::turnAround()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Add 180 degrees to the current yaw, preserving the current pitch/roll
        // so this is a pure about-face.
        const auto& pos = player.getRefData().getPosition();
        float yaw = pos.rot[2] + kPi;
        world->rotateObject(player, osg::Vec3f(pos.rot[0], pos.rot[1], yaw), MWBase::RotationFlag_none);
        speak(std::string("Facing ") + compassLabel(yaw) + ".");
    }

    std::string Scanner::playerStatText(int index, const char* label) const
    {
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return {};

        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        const MWMechanics::DynamicStat<float>& stat = stats.getDynamic(index);
        // Current can dip a touch below 0 / round oddly; clamp current to >= 0
        // and report the modified maximum, matching the on-screen bars.
        const int current = static_cast<int>(std::max(0.f, std::round(stat.getCurrent())));
        const int max = static_cast<int>(std::round(stat.getModified()));
        // Match the stats pane's convention: "<label>: <current> / <max>".
        return std::string(label) + ": " + std::to_string(current) + " / " + std::to_string(max);
    }

    void Scanner::announcePlayerStat(int index, const char* label)
    {
        std::string text = playerStatText(index, label);
        if (!text.empty())
            speak(text + ".");
    }

    void Scanner::announcePlayerHealth()
    {
        announcePlayerStat(0, "Health");
    }

    void Scanner::announcePlayerMagicka()
    {
        announcePlayerStat(1, "Magicka");
    }

    void Scanner::announcePlayerFatigue()
    {
        announcePlayerStat(2, "Fatigue");
    }

    std::string Scanner::readiedWeaponText() const
    {
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty() || !player.getClass().hasInventoryStore(player))
            return {};
        MWWorld::InventoryStore& inv = player.getClass().getInventoryStore(player);
        auto slot = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (slot != inv.end() && !slot->isEmpty())
            return "Weapon: " + std::string(slot->getClass().getName(*slot));
        // Empty right hand = hand-to-hand, mirroring the native HUD's H2H icon.
        return "Weapon: Hand to hand";
    }

    std::string Scanner::readiedSpellText() const
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return {};

        // Read the player's readied spell from the WindowManager, not from
        // CreatureStats: for the player the UI selection does not update the
        // CreatureStats selected spell (see computeHitState / mwlua actor.cpp),
        // so the CreatureStats value is empty and this line would never name the
        // spell.
        const ESM::RefId selected = MWBase::Environment::get().getWindowManager()->getSelectedSpell();
        if (!selected.empty())
        {
            const ESM::Spell* spell = world->getStore().get<ESM::Spell>().search(selected);
            if (spell)
                return "Spell: " + spell->mName;
        }
        if (player.getClass().hasInventoryStore(player))
        {
            MWWorld::InventoryStore& inv = player.getClass().getInventoryStore(player);
            if (inv.getSelectedEnchantItem() != inv.end())
            {
                const MWWorld::Ptr item = *inv.getSelectedEnchantItem();
                if (!item.isEmpty())
                    return "Spell: " + std::string(item.getClass().getName(item));
            }
        }
        return {}; // Nothing selected -- omit the line entirely.
    }

    std::string Scanner::breathText() const
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty() || !player.getClass().isNpc())
            return {};

        // Mirror the HUD's drowning bar: it appears only once the player is
        // underwater and has started spending breath (timeLeft < the full
        // hold-breath time). A full or uninitialised (-1) timer means the bar
        // is hidden, so we report nothing. Spoken as a percentage of capacity,
        // matching the bar (which carries no numbers).
        const MWMechanics::NpcStats& stats = player.getClass().getNpcStats(player);
        const float timeLeft = stats.getTimeToStartDrowning();
        static const float holdBreath = world->getStore()
                                            .get<ESM::GameSetting>()
                                            .find("fHoldBreathTime")
                                            ->mValue.getFloat();
        if (holdBreath <= 0.f || timeLeft < 0.f || timeLeft >= holdBreath)
            return {};
        const int percent = static_cast<int>(std::round(std::clamp(timeLeft / holdBreath, 0.f, 1.f) * 100.f));
        return "Breath: " + std::to_string(percent) + " percent";
    }

    MWWorld::Ptr Scanner::enemyInfoTarget()
    {
        // Prefer the locked target (the thing the player is actively fighting),
        // falling back to the current scanner selection.
        MWWorld::Ptr target = lockTarget();
        if (target.isEmpty())
            target = currentTarget();
        if (target.isEmpty() || !target.getClass().isActor())
            return MWWorld::Ptr();
        return target;
    }

    std::string Scanner::enemyHealthText()
    {
        MWWorld::Ptr target = enemyInfoTarget();
        if (target.isEmpty())
            return {};
        const MWMechanics::CreatureStats& stats = target.getClass().getCreatureStats(target);
        // Percentage only: the native enemy health bar shows no numbers to
        // sighted players, so we expose none either -- matching getRatio()*100,
        // exactly what the bar fills to.
        const int percent
            = static_cast<int>(std::round(std::clamp(stats.getHealth().getRatio(), 0.f, 1.f) * 100.f));
        return objectDisplayName(target) + ", health " + std::to_string(percent) + " percent";
    }

    std::string Scanner::locationText() const
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return {};
        MWWorld::CellStore* cell = player.getCell();
        if (!cell)
            return {};
        std::string_view name = world->getCellName(cell);
        if (name.empty())
            return {};
        return std::string(name);
    }

    void Scanner::announceEnemyHealth()
    {
        std::string text = enemyHealthText();
        if (text.empty())
            speak("No target.");
        else
            speak(text + ".");
    }

    std::string Scanner::targetHealthLabel()
    {
        std::string text = enemyHealthText();
        return text.empty() ? "Target: none" : "Target: " + text;
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

        // The Locations category is likewise position-based: discovered global-
        // map places (visited towns + NPC-marked spots), one entry per town.
        if (mCategory == Category::Locations)
        {
            collectLocations(state.mWaypoints);
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
                //
                // EXCEPTION for actors: hasToolTip() returns false for an NPC
                // (and creature) the moment it enters combat with you and isn't
                // fleeing -- the engine suppresses the hover-tooltip on a hostile
                // so you can't pickpocket mid-fight (see Npc::hasToolTip). If we
                // honoured that here, an attacker would vanish from the Actors
                // list at the exact instant it became dangerous -- the opposite
                // of what a blind player needs. For actors, use "has a non-empty
                // name" directly (the same test hasToolTip applies to activators)
                // so hostiles stay listed; nameless helper actors, if any, are
                // still dropped.
                if (ptr.getClass().isActor())
                {
                    if (ptr.getClass().getName(ptr).empty())
                        return true;
                }
                else if (!ptr.getClass().hasToolTip(ptr))
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

        // Speak whether a door is open or closed (empty for non-doors). A blind
        // player has no visual cue, and it matters now that activation is blocked
        // through a closed door: "Wooden Door, to Seyda Neen, closed".
        if (std::string doorState = doorStateLabel(target); !doorState.empty())
            name += ", " + doorState;

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

        const MWWorld::Cell* cell = cellStore->getCell();
        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();

        // The player's worldspace decides which notes share a comparable
        // coordinate system. A note is "reachable" (real distance/bearing,
        // auto-walkable) only when it's in the SAME worldspace the player is
        // currently standing in -- whether that's the outdoor overworld or one
        // specific interior. Notes in any other worldspace are listed but
        // flagged unreachable with a crude area label, since their XY can't be
        // compared to the player's across coordinate systems.
        const ESM::RefId playerWorldspace = cell->getWorldSpace();

        // Resolve a short location name for an unreachable note from its cell id:
        // an interior cell id is a string (the cell name); an exterior note maps
        // to a cell whose name/region we can look up. Failing that, fall back to
        // the raw id string so the user still gets *something*.
        auto areaLabelFor = [world](const ESM::RefId& noteCell) -> std::string {
            if (const auto* ext = noteCell.getIf<ESM::ESM3ExteriorCellRefId>())
            {
                const ESM::ExteriorCellLocation loc(ext->getX(), ext->getY(), ESM::Cell::sDefaultWorldspaceId);
                try
                {
                    const MWWorld::CellStore& store
                        = MWBase::Environment::get().getWorldModel()->getExterior(loc, /*forceLoad=*/false);
                    const std::string_view name = world->getCellName(&store);
                    if (!name.empty())
                        return std::string(name);
                }
                catch (const std::exception&)
                {
                }
                return std::string("Wilderness");
            }
            // Interior: the cell id IS the cell name.
            return noteCell.toString();
        };

        // ALL map notes across every cell -- distant towns and quest dungeons
        // should be discoverable, not just notes in the cell you're standing in.
        const auto notes = MWBase::Environment::get().getWindowManager()->getAllPlayerMapNotes();
        for (const auto& note : notes)
        {
            Waypoint wp;
            wp.mName = note.mText.empty() ? std::string("Note") : note.mText;

            // A note is reachable when it lives in the SAME worldspace as the
            // player (same coordinate system, continuous navmesh). A note's
            // worldspace is the default overworld if it's an exterior note
            // (exterior cell ids carry x/y grid coords, so getIf() succeeds),
            // otherwise the note's own interior cell id IS its worldspace.
            // NB: this must NOT require the player to be outdoors -- a note
            // dropped in the very interior you're standing in is reachable, even
            // though both you and it are indoors (the earlier "playerInExterior"
            // guard wrongly flagged such a note as "different area").
            const bool noteIsExterior = note.mCell.getIf<ESM::ESM3ExteriorCellRefId>() != nullptr;
            const bool reachable = noteIsExterior ? (playerWorldspace == ESM::Cell::sDefaultWorldspaceId)
                                                  : (playerWorldspace == note.mCell);

            wp.mReachable = reachable;
            if (reachable)
            {
                // Map notes only carry an XY world position; use the player's Z
                // as a reasonable height for bearing/240 audio (same floor in
                // practice, and the pathfinder snaps to navmesh height anyway).
                wp.mPosition = osg::Vec3f(note.mWorldX, note.mWorldY, playerPos.z());
            }
            else
            {
                // Keep the raw position for completeness, but it won't be used
                // for bearing/auto-walk; supply a crude area label instead.
                wp.mPosition = osg::Vec3f(note.mWorldX, note.mWorldY, playerPos.z());
                wp.mAreaLabel = areaLabelFor(note.mCell);
            }
            out.push_back(std::move(wp));
        }

        // The Mark spell location: a single global position. Reachable only when
        // it's in the player's current worldspace (an exterior mark while the
        // player is outdoors, or an interior mark in the very cell the player is
        // standing in); otherwise list it with the cell name and no bearing.
        MWWorld::CellStore* markedCell = nullptr;
        ESM::Position markedPos;
        world->getPlayer().getMarkedPosition(markedCell, markedPos);
        if (markedCell && markedCell->getCell())
        {
            Waypoint wp;
            wp.mName = "Mark";
            wp.mPosition = markedPos.asVec3();
            const bool markReachable = markedCell->getCell()->getWorldSpace() == playerWorldspace;
            wp.mReachable = markReachable;
            if (!markReachable)
                wp.mAreaLabel = std::string(world->getCellName(markedCell));
            out.push_back(std::move(wp));
        }

        // Reachable waypoints first (nearest-first within that group, matching
        // the object categories' distance sort); unreachable ones after, by
        // name, since distance is meaningless for them.
        std::sort(out.begin(), out.end(), [&playerPos](const Waypoint& a, const Waypoint& b) {
            if (a.mReachable != b.mReachable)
                return a.mReachable; // reachable sorts before unreachable
            if (a.mReachable)
                return (a.mPosition - playerPos).length2() < (b.mPosition - playerPos).length2();
            return a.mName < b.mName;
        });
    }

    void Scanner::collectLocations(std::vector<Waypoint>& out) const
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;
        const MWWorld::CellStore* cellStore = player.getCell();
        if (!cellStore || !cellStore->getCell())
            return;
        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();

        // Discovered locations are all exterior places in the default
        // (overworld) worldspace. They're "reachable" -- real bearing/distance,
        // auto-walkable -- only while the player is themselves out in that
        // worldspace. From inside an interior they're still listed (so you can
        // see what you've found) but, like a cross-worldspace waypoint, with no
        // bearing: their XY isn't comparable to an interior position.
        const bool reachable = cellStore->getCell()->getWorldSpace() == ESM::Cell::sDefaultWorldspaceId;

        const auto locations = MWBase::Environment::get().getWindowManager()->getDiscoveredLocations();
        for (const auto& loc : locations)
        {
            Waypoint wp;
            wp.mName = loc.mName;
            // Use the player's Z for bearing/elevation audio; the pathfinder
            // snaps to navmesh height once we actually walk there.
            wp.mPosition = osg::Vec3f(loc.mWorldX, loc.mWorldY, playerPos.z());
            wp.mReachable = reachable;
            if (!reachable)
                wp.mAreaLabel = "on the map";
            out.push_back(std::move(wp));
        }

        // Nearest-first when reachable; otherwise alphabetical (distance is
        // meaningless from a different worldspace).
        std::sort(out.begin(), out.end(), [&playerPos, reachable](const Waypoint& a, const Waypoint& b) {
            if (reachable)
                return (a.mPosition - playerPos).length2() < (b.mPosition - playerPos).length2();
            return a.mName < b.mName;
        });
    }

    void Scanner::announceCurrentWaypoint()
    {
        const Waypoint* wp = currentWaypoint();
        if (!wp)
            return;

        const auto& state = mLists[static_cast<size_t>(mCategory)];
        const std::string position
            = std::to_string(state.mIndex + 1) + " of " + std::to_string(state.mWaypoints.size()) + ".";

        // Unreachable waypoints (interiors, other worldspaces) have no comparable
        // position, so don't fabricate a distance/bearing -- just name the note
        // and its area so the user knows it exists and roughly where.
        if (!wp->mReachable)
        {
            std::string msg = wp->mName + ". ";
            if (!wp->mAreaLabel.empty())
                msg += wp->mAreaLabel + ", ";
            msg += "different area. " + position;
            speak(msg);
            return;
        }

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        osg::Vec3f delta = wp->mPosition - playerPos;
        float dist = delta.length();
        float targetYaw = std::atan2(delta.x(), delta.y());

        std::string elevation = formatElevation(delta.z());
        std::string msg = wp->mName + ". " + formatDistance(dist)
            + ", " + compassLabel(targetYaw) + ". "
            + (elevation.empty() ? "" : elevation + ". ")
            + position;
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
            // Only beacon toward reachable waypoints; an unreachable one (another
            // worldspace) would point the cue at a position in a different
            // coordinate system, giving a nonsensical direction.
            if (const Waypoint* wp = currentWaypoint(); wp && wp->mReachable)
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
