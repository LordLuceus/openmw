#include "scanner.hpp"

#include "itembucket.hpp"
#include "markremap.hpp"
#include "spokenformat.hpp"

#include <SDL_keycode.h>
#include <SDL_scancode.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include <osg/ComputeBoundsVisitor>

#include <components/sceneutil/positionattitudetransform.hpp>

#include <MyGUI_LanguageManager.h>

#include <components/esm/attr.hpp>
#include <components/esm/defs.hpp>
#include <components/esm/esm3exteriorcellrefid.hpp>
#include <components/esm/util.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/format.hpp>
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
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/settings/values.hpp>

#include "../mwmechanics/activespells.hpp"
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
#include "../mwbase/scriptmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwinput/actions.hpp"

#include "../mwworld/actionteleport.hpp"
#include "../mwworld/cell.hpp"
#include "../mwworld/cellref.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/datetimemanager.hpp"
#include "../mwworld/doorstate.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/globals.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"
#include "../mwworld/worldmodel.hpp"

#include "../mwscript/locals.hpp"

#include <components/compiler/locals.hpp>
#include <components/esm3/loadscpt.hpp>

namespace
{
    // Teleport escape-hatch distance cap (~4096u, ~58m). The teleport only fires
    // after auto-walk has actually FAILED to reach an obviously-present target
    // (e.g. a ledge you flew up to that has no walkable path back), and only over
    // a gap no larger than this -- enough for a room/cave that pathfinding can't
    // route, but far short of letting it be abused as in-cell fast travel.
    constexpr float kTeleportMaxDist = 4096.0f;

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
    using MWAccessibility::kHalfPi;
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

    // Status cues. Played 2D (non-positional): these are HUD-style notifications
    // about the player's own state, not sounds emanating from a world location.
    constexpr VFS::Path::NormalizedView kMagicExpiringSound("sounds/a11y/magic_expiring.wav");
    constexpr VFS::Path::NormalizedView kQuestUpdateSound("sounds/a11y/quest_update.wav");
    constexpr VFS::Path::NormalizedView kQuestCompleteSound("sounds/a11y/quest_complete.wav");

    // Sneak-detection cues (see Scanner::updateSneakDetection). 2D status cues:
    // they report the player's OWN stealth state (mirroring the HUD sneak eye),
    // not a sound from any observer, so they're deliberately non-positional.
    constexpr VFS::Path::NormalizedView kSneakDetectedSound("sounds/a11y/sneak_detected.wav");
    constexpr VFS::Path::NormalizedView kSneakHiddenSound("sounds/a11y/sneak_hidden.wav");

    // How many seconds before a timed magic effect ends we warn the player.
    // A single one-shot cue at this point (not a per-second tick), and effects
    // whose entire duration is no longer than this never warn at all.
    constexpr float kExpiryWarnSeconds = 5.f;

    // How long to keep watching for the effect of an activated mechanism (see
    // Scanner::updateActivationWatch). Scripts often act over several frames or
    // on a timer -- the dwarven crank in Arvesa's Dagoth Ur facility runs for
    // about six seconds of turning before it opens its grate -- so a one-frame
    // check would miss the outcome entirely. Long enough to catch those, short
    // enough that an unrelated later change isn't blamed on the player's pull.
    constexpr float kActivationWatchSeconds = 10.f;

    // The allowlist of magic effects worth an expiry warning: survival- and
    // navigation-critical ones whose sudden loss can strand, drown, or drop a
    // blind player, or blow their cover. Routine stat buffs (Fortify/Restore/
    // Shield) are deliberately excluded -- their expiry is harmless, so warning
    // on every potion would just be noise. Extend here if more prove useful.
    bool isExpiryWarnEffect(const ESM::RefId& id)
    {
        return id == ESM::MagicEffect::Levitate || id == ESM::MagicEffect::WaterWalking
            || id == ESM::MagicEffect::WaterBreathing || id == ESM::MagicEffect::SlowFall
            || id == ESM::MagicEffect::Invisibility || id == ESM::MagicEffect::Chameleon
            || id == ESM::MagicEffect::Sanctuary;
    }

    // True for the invisible helper objects Morrowind and its mods scatter
    // through the world: ambient sound emitters, weather and script triggers,
    // teleport anchors. They are implemented as activators whose model is the
    // editor-only "EditorMarker.NIF" mesh, which the engine hides in-game, and
    // they are almost always nameless. A sighted player never knows they are
    // there, so neither should we: they must not appear in a list, and their
    // movement must never be announced. (Morrowind Acoustic Overhaul's
    // "AO_Weather_act_Wood" is one of these -- it shifted when the player
    // rested and was reported as a mechanism operating.)
    bool isEditorMarker(const MWWorld::Ptr& ptr)
    {
        return Misc::StringUtils::ciEndsWith(ptr.getClass().getModel(ptr), "editormarker.nif");
    }

    // How close (world units) another actor must be for its spellcast to be
    // announced when it is NOT targeting the player. ~28 m: roughly the audible/
    // relevant neighbourhood, so the player hears a mage in the same room or
    // courtyard buffing or fighting someone else, without narrating every cast
    // across a whole exterior cell. Casts by an actor in combat with the player
    // are announced at any distance regardless of this. See
    // Scanner::announceActorSpellCast.
    constexpr float kSpellCastNearbyRange = 28.f * kUnitsPerMetre;

    // HAZARD PROXIMITY tuning (damaging terrain: lava, acid, fire fields).
    //
    // How near damaging terrain has to be before it is announced unprompted.
    // ~4 m is far enough to stop before stepping in at a walk, and near enough
    // that a room with a pool along one wall doesn't warn the moment you enter.
    // Lava deals 20 HP/sec, so being late here is measured in dead characters,
    // but being early makes the warning noise the player learns to ignore.
    constexpr float kHazardWarnRadius = 4.f * kUnitsPerMetre;
    // Extra distance the player must retreat before the same hazard can warn
    // again. Prevents a warning stutter while standing at the boundary.
    constexpr float kHazardRearmMargin = 2.f * kUnitsPerMetre;
    // Height difference beyond which a hazard is treated as "not on my floor"
    // and left unannounced. One storey (kFloorHeight) is the natural unit: a
    // lava cave below a walkway cannot burn someone on the walkway, and warning
    // about it would be both wrong and constant.
    constexpr float kHazardWarnVertical = MWAccessibility::kFloorHeight;
    // Below this, a bearing is meaningless and the honest phrasing is "at your
    // feet". Matches the shaft readout's kShaftHereRadius (~0.75 m).
    constexpr float kHazardHereRadius = 52.5f;

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
            case MWAccessibility::Category::Terrain:
                return "Terrain";
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
                return !isEditorMarker(ptr);
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

    // Subcategories for the Terrain category. Like kDetectedSubs the predicates
    // are null: Terrain's members are bare positions (not Ptrs), so the
    // filtering happens in rebuildCurrentList's Terrain branch keyed on the
    // subcategory index, in this order: 0 = All, 1 = Hazards, 2 = Shafts.
    constexpr Subcategory kTerrainSubs[] = {
        { "All", nullptr },
        { "Hazards", nullptr },
        { "Shafts", nullptr },
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
            case MWAccessibility::Category::Terrain:
                return { kTerrainSubs, std::size(kTerrainSubs) };
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

    // Resolve a place name (e.g. a road signpost's "Balmora", "Vos") to a world
    // position, by matching it against the names of the game's exterior cells.
    // This reads the STATIC cell records, so it works for any named place in the
    // world -- crucially including towns the player has never visited, which is
    // the whole value of a signpost (a sighted player reads the direction off the
    // sign without having been there). A multi-cell town (Balmora spans several
    // named cells) resolves to the barycentre of its cells' centres, mirroring
    // how the global-map markers aggregate discovered locations. Returns nullopt
    // when the name matches no exterior cell (e.g. a sign to a landmark that is
    // not itself a named cell), so the caller can fall back to name-only rather
    // than fabricate a bearing.
    std::optional<osg::Vec2f> resolveExteriorPlacePosition(std::string_view placeName)
    {
        if (placeName.empty())
            return std::nullopt;
        const auto& cells = MWBase::Environment::get().getESMStore()->get<ESM::Cell>();
        osg::Vec2f sum(0.0f, 0.0f);
        int count = 0;
        for (auto it = cells.extBegin(); it != cells.extEnd(); ++it)
        {
            if (it->mName.empty())
                continue;
            // Match on the display name BEFORE any comma ("Balmora" from
            // "Balmora, Fred's House"), exactly as the map's location aggregation
            // does, so every named sub-cell of a town folds into one place.
            std::string_view cellName = it->mName;
            if (const auto comma = cellName.find(','); comma != std::string_view::npos)
                cellName = cellName.substr(0, comma);
            if (!Misc::StringUtils::ciEqual(cellName, placeName))
                continue;
            sum += osg::Vec2f((it->getGridX() + 0.5f) * Constants::CellSizeInUnits,
                (it->getGridY() + 0.5f) * Constants::CellSizeInUnits);
            ++count;
        }
        if (count == 0)
            return std::nullopt;
        return sum / static_cast<float>(count);
    }

    // True if \p ptr is a teleport door whose destination is the cell the door
    // itself sits in -- an INTERNAL teleport. Mods use these heavily as ladders,
    // shafts and hatches connecting parts of one big cell (OAAB's "Dwemer
    // Ladder", placed by Arvesa's Dagoth Ur facility, is exactly this). Naming
    // the destination cell is actively misleading for these: it speaks the cell
    // the player is already standing in.
    bool isInternalTeleport(const MWWorld::Ptr& ptr)
    {
        if (ptr.getType() != ESM::Door::sRecordId || !ptr.getCellRef().getTeleport())
            return false;
        const ESM::RefId destCell = ptr.getCellRef().getDestCell();
        if (destCell.empty() || !ptr.isInCell())
            return false;
        const MWWorld::CellStore* here = ptr.getCell();
        return here && here->getCell()->getId() == destCell;
    }

    // Where an internal teleport door actually puts you, phrased relative to the
    // door: e.g. "leads up 4.5 metres, north" (no leading separator). Speaking
    // the destination CELL (the normal behaviour) is useless here because it is
    // the current cell, so instead we describe the move itself, computed from
    // the door's authored destination -- the same data the engine uses to place
    // the player. Height change comes first because these are overwhelmingly
    // ladders/shafts and "up" or "down" is the part the player needs; the
    // compass bearing follows for the ones that also move you sideways. Both
    // parts are omitted when the move is negligible in that axis, so we never
    // speak a fabricated direction. Empty for anything that isn't an internal
    // teleport.
    std::string internalTeleportLeadsLabel(const MWWorld::Ptr& ptr)
    {
        if (!isInternalTeleport(ptr))
            return {};

        const osg::Vec3f doorPos = ptr.getRefData().getPosition().asVec3();
        const osg::Vec3f destPos = ptr.getCellRef().getDoorDest().asVec3();
        const osg::Vec3f delta = destPos - doorPos;

        // Reuse formatElevation's own dead-band so "up"/"down" here means the
        // same thing it does everywhere else in the scanner.
        const std::string climb = MWAccessibility::formatElevationDirectionFirst(delta.z());
        const float horiz = osg::Vec2f(delta.x(), delta.y()).length();

        // Goes essentially nowhere we can describe honestly. Say only that it
        // stays in this area rather than inventing a direction.
        // Capitalised because this fragment starts its own sentence at the end
        // of the readout ("... 3 metres, north. Leads up 4 metres, north.").
        if (climb.empty() && horiz <= kUnitsPerMetre)
            return "Leads elsewhere in this area";

        std::string out = "Leads ";
        out += climb.empty() ? "across" : climb;

        if (horiz > kUnitsPerMetre)
        {
            // atan2(x, y): 0 = north, +X = east -- the absolute-bearing
            // convention used everywhere in the scanner.
            out += ", ";
            out += MWAccessibility::compassLabel(std::atan2(delta.x(), delta.y()));
        }
        return out;
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
        // An internal teleport leads back into this same cell, so naming the
        // cell tells the player where they already are. Its "leads up 4 metres"
        // description is appended at the END of the spoken line instead (see
        // announceCurrent), after the distance -- so nothing is added here.
        if (isInternalTeleport(ptr))
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

    // For a road signpost (and any other named activator that points at a place),
    // append the compass direction and distance to the place it names, computed
    // from where the player stands. Signposts are activators whose name is a
    // place ("Balmora", "Vos") but carry no script, so vanilla activation does
    // nothing and the scanner previously spoke only the bare name -- useless,
    // since a signpost's entire purpose is telling you WHICH WAY somewhere is.
    // We resolve the name against the static exterior cells (so it works for
    // places never visited -- exactly the case a signpost helps with), then speak
    // the true bearing/distance, e.g. "Vos, northwest, 1800 metres". If the name
    // matches no exterior cell (a landmark that is not a named cell, or the
    // player is in an interior/other worldspace where the exterior XY is not
    // comparable), we append NOTHING and the caller keeps the plain name -- never
    // a fabricated direction. \p ptr must be the selected object; \p playerPos is
    // the player's world position.
    void appendSignpostDirection(const MWWorld::Ptr& ptr, const osg::Vec3f& playerPos, std::string& out)
    {
        if (!ptr.getClass().isActivator())
            return;
        // CRITICAL: only treat SCRIPTLESS activators as signposts. A road sign is
        // a plain activator whose name is a place and which carries no script, so
        // vanilla activation does nothing -- that is exactly why we repurpose it.
        // But some scriptABLE activators share a name with a cell: the Sanctus
        // Shrine pilgrimage object is named "Sanctus Shrine" (same as its cell)
        // and runs a script on activation to complete the pilgrimage. Without this
        // guard we hijacked its Activate and spoke a direction instead of letting
        // the shrine script fire, breaking the quest. A present script means the
        // object DOES something when activated, so it is never a signpost.
        if (!ptr.getClass().getScript(ptr).empty())
            return;
        // Only meaningful when we ourselves are in the exterior worldspace: the
        // bearing is computed in exterior world XY, which is not comparable to an
        // interior's local coordinates.
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty() || !player.getCell() || !player.getCell()->getCell())
            return;
        if (player.getCell()->getCell()->getWorldSpace() != ESM::Cell::sDefaultWorldspaceId)
            return;

        const std::optional<osg::Vec2f> place = resolveExteriorPlacePosition(ptr.getClass().getName(ptr));
        if (!place)
            return;

        const osg::Vec2f delta = *place - osg::Vec2f(playerPos.x(), playerPos.y());
        // atan2(x, y): 0 = north, +X = east -- the absolute-bearing convention
        // used everywhere in the scanner (compassLabel, target readout).
        const float bearing = std::atan2(delta.x(), delta.y());
        const float dist = delta.length();
        out += ", ";
        out += MWAccessibility::compassLabel(bearing);
        out += ", ";
        out += MWAccessibility::formatDistance(dist);
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

    // Spoken lock / trap state for a container or door, matching EXACTLY what the
    // vanilla hover tooltip shows a sighted player (see MWClass::Container and
    // MWClass::Door getToolTipInfo): the numeric lock level when locked,
    // "Unlocked" for a lockable-but-currently-open object, and "Trapped" when a
    // trap is armed. Returns a localised fragment with a leading ", " so it can
    // be appended to the spoken identity, or empty when there's nothing to say
    // (no lock mechanism and no trap -- the common case for ordinary clutter).
    //
    // Two deliberate parity choices, both faithful to vanilla:
    //  - We expose ONLY the binary "Trapped" fact, never the trap's type/effect:
    //    the tooltip hides that too, so the trap is still a gamble to trigger.
    //  - We use the same #{sLockLevel}/#{sUnlocked}/#{sTrapped} GMST tokens the
    //    tooltip uses, resolved through the localisation layer, so non-English
    //    users hear their own strings (design principle 9).
    // Read live off the cell ref at speech time -- lock/trap state changes as the
    // player unlocks a chest, springs a trap, or a key auto-disarms one.
    std::string lockTrapLabel(const MWWorld::Ptr& ptr)
    {
        const unsigned int type = ptr.getType();
        if (type != ESM::Container::sRecordId && type != ESM::Door::sRecordId)
            return {};
        std::string text;
        const int lockLevel = ptr.getCellRef().getLockLevel();
        if (lockLevel)
        {
            if (ptr.getCellRef().isLocked())
                text += ", #{sLockLevel}: " + std::to_string(lockLevel);
            else
                text += ", #{sUnlocked}";
        }
        if (!ptr.getCellRef().getTrap().empty())
            text += ", #{sTrapped}";
        if (text.empty())
            return {};
        return MyGUI::LanguageManager::getInstance().replaceTags(text).asUTF8();
    }

    // Build a spoken spell name from its effects, e.g. "Cure Common Disease" or
    // "Fire Damage and Drain Strength". Used as a fallback when a spell has no
    // authored name (mName empty) -- common for scripted helper spells a mod
    // fires via mwscript (e.g. a companion's "cure me" spell), which vanilla
    // never announces so the blank name went unnoticed. Without this the cast
    // read as "<Caster> casts ." (empty effect). Mirrors how the engine derives
    // a readable effect string for tooltips (getMagicEffectString), and routes
    // the joining words through localisation (principle 9). Returns empty only
    // if not a single effect resolves (then the caller omits the cast entirely
    // rather than speak a bare "casts").
    std::string spellNameFromEffects(const ESM::EffectList& effects)
    {
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        std::vector<std::string> names;
        for (const ESM::IndexedENAMstruct& e : effects.mList)
        {
            const ESM::MagicEffect* mgef = store.get<ESM::MagicEffect>().search(e.mData.mEffectID);
            if (!mgef)
                continue;
            const ESM::Attribute* attribute = store.get<ESM::Attribute>().search(e.mData.mAttribute);
            const ESM::Skill* skill = store.get<ESM::Skill>().search(e.mData.mSkill);
            std::string name = MWMechanics::getMagicEffectString(*mgef, attribute, skill);
            if (!name.empty())
                names.push_back(std::move(name));
        }
        if (names.empty())
            return {};
        std::string out = names.front();
        const std::string andWord
            = " " + std::string(MWBase::Environment::get().getWindowManager()->getGameSettingString("sand", "and"))
            + " ";
        for (size_t i = 1; i < names.size(); ++i)
            out += andWord + names[i];
        return out;
    }

    // True if the spell has at least one effect that reaches OUT of the caster --
    // i.e. a Touch or Target ranged effect. A purely self-ranged spell (RT_Self,
    // e.g. Levitation or Water Walking a companion casts on herself to follow you)
    // can never be aimed "at" anyone. We need this because the engine's cast path
    // still runs a hit-contact/aim raycast for a non-scripted self-buff and can
    // resolve a spurious "target" -- whoever happens to stand in front of the
    // caster (often the player) -- even though the spell only ever affects the
    // caster. Gating the "at you" suffix on a non-self effect stops that false
    // "casts X at you" (design principle: never speak confident wrong info).
    bool spellHasNonSelfEffect(const ESM::EffectList& effects)
    {
        for (const ESM::IndexedENAMstruct& e : effects.mList)
            if (e.mData.mRange != ESM::RT_Self)
                return true;
        return false;
    }

    // The text the search filter matches against: the same enriched, spoken
    // identity the user hears -- the display name plus a door's destination
    // (e.g. "Door, to Balmora, Guild of Mages"). Without this, doors (which are
    // all just named "Door") would be unsearchable; the useful, distinguishing
    // text is the destination. Any #{...} localisation tags are resolved so the
    // match works against the form the user actually hears.
    //
    // \p marks is the current category's custom mark notes (Ctrl+K), included
    // so a note the player wrote themselves is searchable. This turns marks into
    // a general tagging system: label a shipmaster "silt strider" and a search
    // for "silt" finds them, even though nothing in the game calls them that.
    // It is the only search text the player controls, so it is the only way to
    // find objects the game names unhelpfully (every guard is "Guard").
    std::string objectSearchText(
        const MWWorld::Ptr& ptr, const std::unordered_map<ESM::RefNum, std::string>& marks)
    {
        std::string text = objectDisplayName(ptr);
        appendDoorDestination(ptr, text);
        // In-cell ladders/shafts say where they lead at the end of the spoken
        // line rather than in their identity, but that text is still the only
        // thing distinguishing several identically named ones, so keep it
        // searchable ("up", "down", a bearing).
        if (const std::string leads = internalTeleportLeadsLabel(ptr); !leads.empty())
            text += ", " + leads;
        if (const auto it = marks.find(ptr.getCellRef().getRefNum()); it != marks.end() && !it->second.empty())
            text += ", " + it->second;
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
            // Marks are durable per-save, but this teardown wipes them for a
            // clean slate: on LOAD, StateManager calls loadMarks() right after
            // this cleanup to repopulate them from the save's sidecar; on NEW
            // GAME (or quit) there is no sidecar, so an empty set is exactly
            // right. This keeps one save's marks from leaking into another.
            s.mMarked.clear();
            s.mDirty = true;
        }
        // The marked-object view, like the direction filter, is a transient
        // global mode -- not a saved preference -- so drop it on world teardown.
        mMarkedView = MarkedView::All;
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
        mLastCellExterior = -1;
        // The direction filter is a transient global mode tied to live facing,
        // not a saved preference, so drop it on world teardown -- a heading set
        // in the old game must not silently constrain the freshly loaded one.
        mDirectionFilterActive = false;
        mDirectionSector = -1;
        mCellNamePrimed = false;
        mMeleeReachCooldown = 0.f;
        mPendingJournalCue = 0;
        mExpiryWarned.clear();
        // A teleport requested in the old world must not fire into the new one:
        // the target it referred to no longer exists.
        mTeleportRequested = false;
        // The hazard cache holds positions from the old world, and its cell tag
        // is a freed pointer -- which a new cell could coincidentally reuse,
        // leaving us confident about hazards that aren't there. Drop both.
        mCellHazards.clear();
        mHazardCellId = nullptr;
        mHazardWarned.clear();

        // Drop all AHUD state and lift our pause tag if held, so a HUD left open
        // when the world is torn down (e.g. the player loaded a save from the
        // HUD) can't strand the new game frozen.
        mHud.reset();
    }

    std::filesystem::path Scanner::marksSidecarPath(const std::filesystem::path& saveFile)
    {
        // The marks sidecar is the save's own path with this suffix appended, so
        // "Quicksave.omwsave" pairs with "Quicksave.omwsave.a11ymarks". Appending
        // (rather than replacing the extension) guarantees a 1:1 mapping even for
        // saves whose names contain dots, and keeps the file adjacent so copying
        // or deleting a save can carry or drop its marks alongside it.
        std::filesystem::path p = saveFile;
        p += ".a11ymarks";
        return p;
    }

    void Scanner::saveMarks(const std::filesystem::path& saveFile) const
    {
        const std::filesystem::path sidecar = marksSidecarPath(saveFile);

        // Count marks across all categories first: if there are none, we write no
        // file and delete any stale sidecar from a previous save into this slot,
        // so an empty mark set never masquerades as "file missing" vs "file with
        // zero entries" -- both mean the same thing (no marks) on load.
        size_t total = 0;
        for (const auto& s : mLists)
            total += s.mMarked.size();

        if (total == 0)
        {
            std::error_code ec;
            std::filesystem::remove(sidecar, ec); // best-effort; ignore if absent
            return;
        }

        std::ofstream out(sidecar, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            Log(Debug::Warning) << "[a11y] Could not write marks sidecar: " << sidecar;
            return;
        }

        // Plain-text, line-per-record, with a leading version line so format
        // changes are detectable. Lines are tagged by kind:
        //   "F <index> <pluginName>"                  -- load-order manifest
        //   "M <category> <index> <contentFile> <note>" -- one mark
        //
        // A RefNum identifies an object as (index, contentFile), where
        // contentFile is a position in the CURRENT load order -- so it is only
        // meaningful relative to the plugin list that was active when the mark
        // was made. Adding or removing any mod ahead of a plugin shifts its
        // index, which silently repointed every mark past that point at the
        // wrong plugin and made marks look deleted. So we record the plugin
        // NAMES alongside the marks and translate indices back on load (see
        // loadMarks), exactly as the engine does for saved refs in
        // StateManager::buildContentFileIndexMap.
        //
        // The note is the REST OF THE LINE after the fourth field (may contain
        // spaces; may be empty for a plain mark).
        out << "a11ymarks 3\n";

        // Only the plugins actually referenced by a mark need recording, which
        // keeps the manifest small and means an unrelated mod being added or
        // removed can't affect the marks at all.
        std::set<int32_t> used;
        for (const auto& s : mLists)
            for (const auto& [ref, note] : s.mMarked)
                if (ref.hasContentFile())
                    used.insert(ref.mContentFile);

        const std::vector<std::string>& contentFiles
            = MWBase::Environment::get().getWorld()->getContentFiles();
        for (const int32_t index : used)
        {
            // A mark whose index is outside the current list can't be named; it
            // is written out below regardless, so nothing is lost.
            if (index >= 0 && static_cast<size_t>(index) < contentFiles.size())
                out << "F " << index << ' ' << contentFiles[index] << '\n';
        }

        for (size_t c = 0; c < mLists.size(); ++c)
        {
            for (const auto& [ref, note] : mLists[c].mMarked)
            {
                out << "M " << c << ' ' << ref.mIndex << ' ' << ref.mContentFile;
                if (!note.empty())
                    out << ' ' << note; // rest-of-line; newlines can't occur (single-line prompt)
                out << '\n';
            }
        }
    }

    void Scanner::loadMarks(const std::filesystem::path& saveFile)
    {
        // Always start from a clean slate: loading a save with no sidecar (or an
        // unreadable one) must yield no marks, never leftovers from a prior game.
        for (auto& s : mLists)
            s.mMarked.clear();

        const std::filesystem::path sidecar = marksSidecarPath(saveFile);
        std::error_code ec;
        if (!std::filesystem::exists(sidecar, ec))
            return;

        std::ifstream in(sidecar, std::ios::binary);
        if (!in)
        {
            Log(Debug::Warning) << "[a11y] Could not read marks sidecar: " << sidecar;
            return;
        }

        std::string header;
        int version = 0;
        {
            std::string line;
            if (!std::getline(in, line))
                return;
            std::istringstream hs(line);
            hs >> header >> version;
            // v1 = mark lines without notes; v2 adds an optional rest-of-line
            // note; v3 tags lines by kind and adds the load-order manifest.
            // Accept all three so existing saves keep their marks.
            if (header != "a11ymarks" || (version != 1 && version != 2 && version != 3))
            {
                Log(Debug::Warning) << "[a11y] Unrecognised marks sidecar header, ignoring: " << sidecar;
                return;
            }
        }

        // Plugin name as recorded when the marks were written, keyed by the
        // content-file index it had back then. Empty for v1/v2 sidecars, which
        // predate the manifest and therefore cannot be remapped.
        std::map<int32_t, std::string> savedPlugins;

        std::string line;
        size_t restored = 0;
        size_t remapped = 0;
        size_t orphaned = 0;
        while (std::getline(in, line))
        {
            if (line.empty())
                continue;
            std::istringstream ls(line);

            if (version >= 3)
            {
                std::string kind;
                if (!(ls >> kind))
                    continue;
                if (kind == "F")
                {
                    int32_t index = 0;
                    std::string name;
                    if ((ls >> index) && std::getline(ls, name))
                    {
                        if (!name.empty() && name.front() == ' ')
                            name.erase(name.begin());
                        if (!name.empty())
                            savedPlugins.emplace(index, std::move(name));
                    }
                    continue;
                }
                if (kind != "M")
                    continue; // unknown line kind (e.g. from a future build)
            }

            size_t cat = 0;
            uint32_t index = 0;
            int32_t contentFile = 0;
            if (!(ls >> cat >> index >> contentFile))
                continue; // skip malformed line rather than abort the whole load
            if (cat >= mLists.size())
                continue; // category out of range (e.g. from a future build)
            // The note (v2+) is the rest of the line after the last numeric
            // field and a single separating space. It may be empty (plain mark)
            // and may contain spaces. v1 lines have no note, so this is empty.
            std::string note;
            if (std::getline(ls, note) && !note.empty() && note.front() == ' ')
                note.erase(note.begin()); // drop the one separator space

            // Translate the stored index to wherever that same plugin sits in
            // the CURRENT load order. Without this, adding or removing any mod
            // ahead of it silently points the mark at a different plugin.
            {
                int32_t nowAt = contentFile;
                switch (remapContentFileIndex(contentFile, savedPlugins,
                    MWBase::Environment::get().getWorld()->getContentFiles(), nowAt))
                {
                    case RemapResult::Orphaned:
                        // The plugin is no longer loaded, so the object it
                        // marked does not exist in this game. Drop the mark
                        // rather than let the index point at an unrelated
                        // mod's object.
                        ++orphaned;
                        continue;
                    case RemapResult::Remapped:
                        contentFile = nowAt;
                        ++remapped;
                        break;
                    case RemapResult::Unchanged:
                        break;
                }
            }

            ESM::RefNum ref;
            ref.mIndex = index;
            ref.mContentFile = contentFile;
            mLists[cat].mMarked.emplace(ref, std::move(note));
            ++restored;
        }

        // Any currently-built lists must be recomputed so the hide-marked view
        // and the "marked" suffix reflect the just-restored set immediately.
        for (auto& s : mLists)
            s.mDirty = true;

        Log(Debug::Info) << "[a11y] Restored " << restored << " object mark(s) from " << sidecar
                         << " (remapped " << remapped << " for load-order changes, dropped " << orphaned
                         << " whose plugin is no longer loaded)";
    }

    void Scanner::notifyJournalEntry(bool completed)
    {
        // Record the strongest cue pending for this frame; the actual sound is
        // played in flushJournalCue (called from onFrame) so that several
        // entries added by one dialogue line collapse into a single cue. A
        // completion (2) outranks a plain update (1).
        const int cue = completed ? 2 : 1;
        if (cue > mPendingJournalCue)
            mPendingJournalCue = cue;
    }

    void Scanner::flushJournalCue()
    {
        if (mPendingJournalCue == 0)
            return;
        const VFS::Path::NormalizedView sound
            = mPendingJournalCue == 2 ? kQuestCompleteSound : kQuestUpdateSound;
        mPendingJournalCue = 0;
        MWBase::Environment::get().getSoundManager()->playSound(
            sound, /*volume=*/1.0f, /*pitch=*/1.0f, MWSound::Type::A11y);
    }

    void Scanner::updateMagicExpiry()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);

        // Collect the instance keys still present this frame, so we can prune the
        // "already warned" set down to them afterwards -- otherwise it would
        // accumulate stale keys for every effect that ever expired.
        std::set<std::pair<ESM::RefId, int>> present;
        bool fire = false;

        for (const auto& params : stats.getActiveSpells())
        {
            for (const auto& effect : params.getEffects())
            {
                if (!(effect.mFlags & ESM::ActiveEffect::Flag_Applied))
                    continue;
                // Permanent effects (mDuration == -1) never expire; nothing to warn.
                if (effect.mDuration < 0.f)
                    continue;
                if (!isExpiryWarnEffect(effect.mEffectId))
                    continue;

                const std::pair<ESM::RefId, int> key{ params.getActiveSpellId(), effect.mEffectIndex };
                present.insert(key);

                // Skip effects that never last longer than the warning lead time:
                // the cue would fire the instant they're applied, which is noise,
                // not a warning. (Strictly greater, so a 5.0s effect is skipped.)
                if (effect.mDuration <= kExpiryWarnSeconds)
                    continue;

                const bool inWindow = effect.mTimeLeft > 0.f && effect.mTimeLeft <= kExpiryWarnSeconds;
                if (inWindow && mExpiryWarned.find(key) == mExpiryWarned.end())
                {
                    mExpiryWarned.insert(key);
                    fire = true;
                }
            }
        }

        // Prune warned-keys to those still active, so a re-cast (a brand-new
        // active-spell instance, hence a new id) can warn again next time.
        for (auto it = mExpiryWarned.begin(); it != mExpiryWarned.end();)
        {
            if (present.find(*it) == present.end())
                it = mExpiryWarned.erase(it);
            else
                ++it;
        }

        // One cue even if several tracked effects cross the threshold together.
        if (fire)
            MWBase::Environment::get().getSoundManager()->playSound(
                kMagicExpiringSound, /*volume=*/1.0f, /*pitch=*/1.0f, MWSound::Type::A11y);
    }

    void Scanner::updateSneakDetection(bool sneaking, bool detected)
    {
        // Only meaningful during live gameplay. Guard here (rather than trusting
        // the caller) so a menu/dialogue frame can't fire a stealth cue, and so
        // the state doesn't drift while paused. Mirrors updateMagicExpiry's gate.
        if (!isGameplayActive())
        {
            // Don't reset mLastSneakState: a menu opened mid-sneak should not
            // re-fire the cue when gameplay resumes at the same state. The state
            // only changes on a real gameplay transition below.
            return;
        }

        const SneakState now
            = !sneaking ? SneakState::NotSneaking : (detected ? SneakState::Detected : SneakState::Hidden);
        if (now == mLastSneakState)
            return;

        const SneakState prev = mLastSneakState;
        mLastSneakState = now;

        // Fire a cue only on the transitions that carry information:
        //  - Detected: your cover is blown (someone now sees you). Always worth
        //    a cue, whether you were Hidden or just started sneaking already-seen.
        //  - Hidden after having been Detected: you've slipped out of sight, safe
        //    to move again. NOT when first crouching from standing (NotSneaking ->
        //    Hidden), which is the ordinary "start sneaking, nobody around" case
        //    and would beep on every crouch.
        MWBase::SoundManager* snd = MWBase::Environment::get().getSoundManager();
        if (now == SneakState::Detected)
            snd->playSound(kSneakDetectedSound, /*volume=*/1.0f, /*pitch=*/1.0f, MWSound::Type::A11y);
        else if (now == SneakState::Hidden && prev == SneakState::Detected)
            snd->playSound(kSneakHiddenSound, /*volume=*/1.0f, /*pitch=*/1.0f, MWSound::Type::A11y);
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

        // Flush any pending journal cue BEFORE the gameplay gate: quest entries
        // are added while the dialogue window is open (GUI mode), where
        // isGameplayActive() is false. Only acts when a game is running.
        if (MWBase::Environment::get().getStateManager()->getState()
            == MWBase::StateManager::State_Running)
            flushJournalCue();

        if (!isGameplayActive())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        // Service a teleport requested from the key handler. Done here, outside
        // SDL's event callback, because the confirmation is a blocking modal
        // that pumps the event loop itself (see handleKey).
        if (mTeleportRequested)
        {
            mTeleportRequested = false;
            teleportToTarget();
            // The teleport (and its modal) can move the player and invalidate
            // the Ptrs gathered above, so don't keep using them this frame.
            return;
        }

        // Warn about damaging terrain the player is walking toward. Done every
        // frame regardless of how the player is moving (walking, auto-walk or
        // being pushed), because the hazard doesn't care how you arrived.
        updateHazardProximity();

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

            // Detect an interior<->exterior transition (e.g. stepping out of a
            // house into the street, or into a cave). When this happens, clear
            // every category's name/subcategory filter: a filter set indoors
            // ("Storage" containers, a name search for an NPC) almost never
            // makes sense outdoors and vice versa, and a forgotten filter is a
            // common "why can't I see this door / person / item?" trap -- even
            // for experienced players. We do NOT clear on exterior-to-exterior
            // walking (crossing the cell grid), where a filter should persist.
            const bool nowExterior = player.getCell()->isExterior();
            const bool crossedInOut = mLastCellExterior != -1 && (mLastCellExterior == 1) != nowExterior;
            mLastCellExterior = nowExterior ? 1 : 0;
            bool clearedAnyFilter = false;

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

                // Marks are DURABLE, not scoped to the current room: they record
                // what the player has already looted/checked ("I've emptied these
                // crates") and must survive leaving and returning to a cell (and
                // save/reload, via the sidecar file). So we deliberately do NOT
                // clear s.mMarked here. A RefNum is unique within the save, so a
                // mark stays meaningful anywhere; the player unmarks manually (K).
                // (Contrast mLabels, the disambiguation letters, which ARE
                // per-cell and get reassigned by rebuildCurrentList.)

                // EXCEPTION: the Locations list is global (every discovered place
                // on the map), not a scan of the room you're in, so a filter on
                // it is just as valid outdoors as indoors. Clearing it here would
                // wipe the search at the worst moment -- you look up a distant
                // town while inside, then step out to travel there and lose it.
                const bool filterIsGlobal = (&s == &mLists[static_cast<size_t>(Category::Locations)]);

                if (crossedInOut && !filterIsGlobal)
                {
                    if (!s.mFilter.empty())
                    {
                        s.mFilter.clear();
                        clearedAnyFilter = true;
                    }
                    // Reset any secondary (subcategory) filter back to "All" too,
                    // so e.g. the Actors "Hostile" or Containers "Storage" filter
                    // doesn't silently carry across the threshold.
                    if (s.mSubIndex != 0)
                    {
                        s.mSubIndex = 0;
                        clearedAnyFilter = true;
                    }
                }
            }

            // Speak the new cell's name on entry, but only when it differs from
            // the last announced one -- cities span many same-named cells, so
            // we don't want "Balmora" repeated as the player walks across it.
            announceCellChange();

            // The global direction filter (Ctrl+Up) also clears on an indoor<->
            // outdoor crossing, same rationale as the name/subcategory filters:
            // a heading you set in one space rarely makes sense in the next, and
            // a forgotten one is the same "why can't I see this?" trap.
            if (crossedInOut && mDirectionFilterActive)
            {
                mDirectionFilterActive = false;
                mDirectionSector = -1;
                clearedAnyFilter = true;
            }

            // Let the player know a filter was dropped, so the change in what's
            // listed isn't mysterious. Only when something was actually cleared.
            if (clearedAnyFilter)
                speak("Scanner filters cleared.");

            // Leaving the cell invalidates the remembered departure point: its
            // coordinates are no longer comparable to the player's, and the
            // ladder that produced it is behind us.
            mHaveInternalTeleportOrigin = false;
            mHavePlayerPos = false;

            // Abandon any in-flight activation watch. Its snapshot describes the
            // cell we just left, so every ref would read as "vanished", and the
            // remembered target Ptr may no longer be valid. If the player walked
            // out, they are no longer waiting on that mechanism anyway.
            mWatchingActivation = false;
            mActivationSnapshot = CellSnapshot();
            mActivationWatchName.clear();
            mActivationTarget = MWWorld::Ptr();
            mActivationLocalsBefore.clear();
        }

        // Detect an INTERNAL teleport: a jump within the same cell, produced by
        // a teleport door whose destination is this cell (a ladder, shaft or
        // hatch). No cell change fires for these, so without this the player is
        // moved a long way in silence. See mLastPlayerPos.
        detectInternalTeleport(player);

        // Prune objects that have left the world (e.g. an item the player just
        // picked up) from the active category's cached list, so they stop being
        // announced and the beacon stops homing on them. Skip when the list is
        // dirty -- it'll be rebuilt from scratch on next access anyway.
        if (!mLists[static_cast<size_t>(mCategory)].mDirty)
            pruneDeadObjects();

        // Direction filter follows live facing: if it's engaged and the player
        // has turned into a new compass sector (via mouselook, Ctrl+Left/Right,
        // Ctrl+Down, or any other rotation), re-key the kept wedge to the new
        // heading and invalidate the cached lists so the next read reflects it.
        // We do NOT speak here -- this is passive tracking, and announcing on
        // every turn would be noise; the engaged direction was announced on the
        // Ctrl+Up press, and turn keys already speak the new facing themselves.
        if (mDirectionFilterActive)
        {
            const int sector = compassSector(player.getRefData().getPosition().rot[2]);
            if (sector != mDirectionSector)
            {
                mDirectionSector = sector;
                for (auto& s : mLists)
                    s.mDirty = true;
            }
        }

        // Report what a just-activated mechanism actually did (see
        // updateActivationWatch). Runs before the other per-frame speech so a
        // mechanism's outcome isn't queued behind routine chatter.
        updateActivationWatch(dt);

        mAutoWalker.onFrame(dt);
        mProximityCue.onFrame(dt);
        updateLockOn();
        announceDrawStateChange();
        updateMagicExpiry();

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
                case SDL_SCANCODE_9:
                    catIndex = static_cast<int>(Category::Terrain);
                    break;
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
                // toggles the audio beacon, Ctrl+Shift+Enter teleports to it
                // (the escape hatch -- warns first, see teleportToTarget).
                // Check the two-modifier combo FIRST so it isn't shadowed by
                // the single-modifier cases.
                if (ctrl && shift)
                    // Deferred to onFrame: the teleport can raise a modal
                    // confirmation, and we are currently inside SDL's key-event
                    // callback. A blocking modal here would pump the event loop
                    // re-entrantly from within an event handler.
                    mTeleportRequested = true;
                else if (ctrl)
                    toggleBeacon();
                else if (shift)
                    walkToTarget();
                else
                    focusCamera();
                return true;
            case SDL_SCANCODE_HOME:
                // Shift+Home snaps the view back to level (horizontal) from any
                // pitch -- a quick reset after flying/diving. Plain Home repeats
                // the last announcement.
                if (shift && !ctrl && !alt)
                    levelPitch();
                else
                    repeatAnnouncement();
                return true;
            case SDL_SCANCODE_L:
                // L announces the player's location (cell name). The modified
                // variants mirror the Ctrl=horizontal / Shift=vertical split of
                // the arrow keys: Ctrl+L announces which way you're facing
                // (compass point -- a horizontal heading), Shift+L announces your
                // height above the ground / depth below the water (vertical).
                // Levitation shafts and damaging terrain used to hang off this
                // key too (Alt+L / Ctrl+Alt+L / Shift+Alt+L). They are now the
                // Terrain scanner category instead, where they can be cycled,
                // faced and auto-walked to with the same keys as every other
                // target -- which also means "walk me into the shaft" needs no
                // binding of its own. Alt+L, freed by that move, now reports
                // the time of day: the fourth orientation question (when),
                // alongside where / which way / how high.
                if (ctrl && !shift && !alt)
                    announceFacing();
                else if (shift && !ctrl && !alt)
                    announceHeight();
                else if (alt && !ctrl && !shift)
                    announceTimeOfDay();
                else if (!ctrl && !shift && !alt)
                    announceLocation();
                else
                    return false;
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
            case SDL_SCANCODE_K:
                // Mark tracking: K toggles the selected object's "already looked
                // at" mark (solves losing your place among many identical
                // crates/urns); Shift+K cycles the marked-object view (show all
                // -> unmarked only -> marked only) so you can hide what you've
                // checked or focus on just what you've flagged; Ctrl+K opens a
                // text prompt to attach/edit a custom NOTE on the selected object
                // (e.g. labelling a silt-strider caravaner), marking it if needed.
                // Marks are durable per-save (persisted to the .a11ymarks sidecar).
                if (ctrl && !shift && !alt)
                    addNoteToCurrent();
                else if (shift && !ctrl && !alt)
                    cycleMarkedView();
                else if (!ctrl && !alt)
                    toggleMarkedCurrent();
                return true;
            case SDL_SCANCODE_I:
                // Inspect: read the selected object's live script state (its
                // local variables). Gives blind players a way to see mechanism
                // state that is otherwise purely visual/animated -- e.g. whether
                // each lever in a Dwemer door puzzle is currently on or off.
                if (!ctrl && !shift && !alt)
                    announceObjectState();
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
            case SDL_SCANCODE_UP:
                // Ctrl+Up toggles the direction filter: restrict every category
                // to objects lying the way the player currently faces (and keep
                // following their facing). Completes the Ctrl+arrow facing
                // cluster (Left/Right snap compass, Down turns around).
                if (ctrl && !shift && !alt)
                {
                    toggleDirectionFilter();
                    return true;
                }
                // Shift+Up aims the view higher (snap pitch to the next stop up),
                // for flying up with Levitation or surfacing while swimming.
                if (shift && !ctrl && !alt)
                {
                    aimPitch(/*up=*/true);
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
                // Shift+Down aims the view lower (snap pitch to the next stop
                // down), for descending with Levitation or diving while swimming.
                if (shift && !ctrl && !alt)
                {
                    aimPitch(/*up=*/false);
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
        if (cat == Category::Terrain)
        {
            // Available only in a room that actually has damaging terrain or a
            // shaft, so it's skipped when cycling through an ordinary room --
            // most rooms have neither. Queried with subIndex 0 (All) rather than
            // the category's current subcategory, so the category doesn't vanish
            // just because the player left it filtered to "Shafts".
            std::vector<Waypoint> features;
            collectTerrain(0, features);
            return !features.empty();
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
        // Counted via currentListSize(), NOT state.mObjects: the position-based
        // Terrain category keeps its entries in mWaypoints, so counting mObjects
        // would announce "0 in range" for a room full of lava.
        const size_t listSize = currentListSize();
        state.mIndex = listSize == 0 ? -1 : 0;

        std::string msg = std::string(subs[state.mSubIndex].mName) + ". " + std::to_string(listSize) + " in range.";
        speak(msg);
        if (listSize > 0)
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

    void Scanner::toggleMarkedCurrent()
    {
        // Marking is for the RefNum-identified world objects, not the position-
        // based Waypoints/Locations categories (which have no CellRef identity
        // and whose "marks" would be meaningless).
        if (isWaypointCategory())
        {
            speak("Cannot mark a waypoint.");
            return;
        }

        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
        {
            speak("No target selected.");
            return;
        }

        auto& state = mLists[static_cast<size_t>(mCategory)];
        const ESM::RefNum ref = target.getCellRef().getRefNum();
        const std::string name = objectDisplayName(target);

        // Flip the mark, and remember both what we did (for the verb) and the
        // object's new marked-state (for the visibility check below).
        bool nowMarked;
        if (state.mMarked.count(ref))
        {
            state.mMarked.erase(ref);
            nowMarked = false;
        }
        else
        {
            state.mMarked.emplace(ref, std::string()); // plain mark, no note
            nowMarked = true;
        }
        const std::string verb = nowMarked ? " marked." : " unmarked.";

        // If the active marked-object view now hides this object (marked it while
        // hiding marked, or unmarked it while hiding unmarked), it's about to
        // vanish from the list. Rebuild so the cursor lands on a still-visible
        // neighbour, and announce it -- so the player hears what's next rather
        // than silence on the now-hidden object. Otherwise just confirm in place.
        if (isHiddenUnderMarkedView(nowMarked))
        {
            state.mDirty = true;
            rebuildCurrentList();
            if (currentListSize() == 0)
            {
                speak(name + verb + " No more in range.");
                clearSelection();
                updateProximityCue();
                return;
            }
            // The toggled object was at mIndex and is now hidden, so the object
            // that shifted up into that slot is the next one -- exactly where the
            // player wants to continue. rebuildCurrentList can't re-pin the
            // (now-hidden) selection by RefNum, so mIndex keeps its old value;
            // just clamp it into range for the last-row case.
            if (state.mIndex < 0 || state.mIndex >= static_cast<int>(state.mObjects.size()))
                state.mIndex = static_cast<int>(state.mObjects.size()) - 1;
            // Refresh the remembered identity to the landed object so a later
            // rebuild (cell shift, live refresh) re-pins here, not on the removed
            // object.
            state.mSelectedRef = state.mObjects[state.mIndex].getCellRef().getRefNum();
            speak(name + verb);
            announceCurrent();
            updateProximityCue();
            return;
        }

        speak(name + verb);
    }

    void Scanner::addNoteToCurrent()
    {
        // Notes attach to the RefNum-identified world objects, exactly like marks
        // -- not the position-based Waypoints/Locations categories.
        if (isWaypointCategory())
        {
            speak("Cannot add a note to a waypoint.");
            return;
        }

        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
        {
            speak("No target selected.");
            return;
        }

        // Capture which object (and category) the prompt is for: the modal text
        // entry returns asynchronously, so onMarkNoteEntered must re-find THIS
        // object rather than whatever is selected when the prompt closes.
        mPendingNoteCategory = mCategory;
        mPendingNoteRef = target.getCellRef().getRefNum();

        // Pre-fill with the existing note (if any) so Ctrl+K edits in place.
        std::string existing;
        if (auto it = mLists[static_cast<size_t>(mCategory)].mMarked.find(mPendingNoteRef);
            it != mLists[static_cast<size_t>(mCategory)].mMarked.end())
            existing = it->second;

        MWBase::Environment::get().getWindowManager()->openMarkNote(existing);
    }

    void Scanner::onMarkNoteEntered(const std::string& text)
    {
        if (mPendingNoteCategory == Category::Count)
            return; // no pending prompt (shouldn't happen)
        auto& state = mLists[static_cast<size_t>(mPendingNoteCategory)];
        const ESM::RefNum ref = mPendingNoteRef;
        mPendingNoteCategory = Category::Count; // consume the pending state

        // Resolve a spoken name for the object if it's still in range; fall back to
        // a neutral word if it has since despawned/left the list.
        std::string name = "Object";
        for (const MWWorld::Ptr& p : state.mObjects)
        {
            if (p.getCellRef().getRefNum() == ref)
            {
                name = objectDisplayName(p);
                break;
            }
        }

        // The prompt already treats an empty submission as cancel, but guard
        // anyway: empty text clears the note (keeping the mark) rather than
        // storing a blank. Setting a note also MARKS the object if it wasn't.
        state.mMarked[ref] = text; // inserts (marking it) or overwrites the note

        // A note change can affect marked-view list membership (adding a note
        // marks the object, so it should vanish under hide-marked, or appear
        // under hide-marked-only); refresh so the view and suffix are
        // immediately consistent.
        if (mMarkedView != MarkedView::All)
        {
            refreshActiveListPreservingSelection();
        }

        if (text.empty())
            speak(name + " note cleared.");
        else
            speak(name + " note: " + text + ".");
    }

    void Scanner::onMarkNoteCancelled()
    {
        mPendingNoteCategory = Category::Count; // discard pending state
        speak("Cancelled.");
    }

    void Scanner::cycleMarkedView()
    {
        // Three-way cycle: All -> HideMarked -> HideUnmarked -> All.
        switch (mMarkedView)
        {
            case MarkedView::All:
                mMarkedView = MarkedView::HideMarked;
                break;
            case MarkedView::HideMarked:
                mMarkedView = MarkedView::HideUnmarked;
                break;
            case MarkedView::HideUnmarked:
                mMarkedView = MarkedView::All;
                break;
        }

        // A view change alters what every category lists, so invalidate them all.
        for (auto& s : mLists)
            s.mDirty = true;
        rebuildCurrentList();
        // Re-pin selection onto a valid row (the previously selected object may
        // now be hidden) before speaking, so the follow-up announce is honest.
        auto& state = mLists[static_cast<size_t>(mCategory)];
        if (state.mIndex >= static_cast<int>(state.mObjects.size()))
            state.mIndex = static_cast<int>(state.mObjects.size()) - 1;
        // Sync the remembered identity to wherever the cursor actually landed.
        if (state.mIndex >= 0 && state.mIndex < static_cast<int>(state.mObjects.size()))
            state.mSelectedRef = state.mObjects[state.mIndex].getCellRef().getRefNum();
        else
            state.mSelectedRef = ESM::RefNum{};

        switch (mMarkedView)
        {
            case MarkedView::All:
                speak("Showing all objects.");
                break;
            case MarkedView::HideMarked:
                speak("Showing unmarked objects only.");
                break;
            case MarkedView::HideUnmarked:
                speak("Showing marked objects only.");
                break;
        }
        // Read the current object so the player hears where the cursor landed
        // in the newly filtered list (or nothing if the list is now empty).
        if (currentListSize() > 0)
            announceCurrent();
        updateProximityCue();
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

        // Shift+X is the one-key combat opener: it must find the nearest
        // attacker no matter what the scanner was set to before. So clear every
        // filter that could hide a hostile, otherwise the rebuild below silently
        // drops it and we wrongly announce "No hostiles nearby":
        //   * the global direction filter (Ctrl+Up) excludes anything outside
        //     the faced compass sector -- an attacker behind or beside you would
        //     vanish exactly when you need it most;
        //   * a stale name/search filter on the Actors list excludes any
        //     attacker whose spoken identity doesn't contain the search text;
        //   * the marked-object view (Shift+K) would drop an attacker depending
        //     on whether it happened to be marked (hide-marked) or unmarked
        //     (hide-unmarked).
        // These are all transient view state, so resetting them here is
        // consistent with how they already clear on a cell transition. The
        // lock-on / "no hostiles" speech below is the feedback, so no separate
        // filter-cleared announcement (which would just be combat-time noise).
        mDirectionFilterActive = false;
        mDirectionSector = -1;
        mMarkedView = MarkedView::All;
        state.mFilter.clear();

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
                kEnemyDiedSound, /*volume=*/1.0f, /*pitch=*/1.0f, MWSound::Type::A11y);
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
            /*volume=*/1.0f, /*pitch=*/1.0f, MWSound::Type::A11y);
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

    void Scanner::announceActorSpellCast(const MWWorld::Ptr& caster, const std::string& sourceName,
        const ESM::EffectList& effects, const MWWorld::Ptr& target)
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

        // Is the caster actively in combat with the player? Used only to decide
        // whether a cast is worth narrating at all (see below) -- NOT to claim
        // "at you". Whether the spell is actually aimed at the player is taken
        // from the engine-resolved target, not guessed from combat state.
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

        // Prefer the authored spell/scroll name; fall back to a name built from
        // the effects when it's blank (scripted spells often have no name). If
        // even that yields nothing (no effect resolves), say nothing rather than
        // announce a bare "<Caster> casts ." -- never speak an empty effect.
        std::string spellName = sourceName;
        if (spellName.empty())
            spellName = spellNameFromEffects(effects);
        if (spellName.empty())
            return;

        std::string text = objectDisplayName(caster) + " casts " + spellName;
        // Claim "at you" ONLY when the engine actually resolved the cast onto
        // the player. The target comes from hit-contact / aim raycast (combat
        // casts) or the AI cast package (scripted casts), so a spell aimed at a
        // companion, a summon, or anyone else resolves to that actor -- not the
        // player -- and is announced plainly. This replaces the old heuristic
        // (in-combat + outward-reaching) which wrongly said "at you" whenever a
        // hostile cast anything, even a self-buff or a spell at the player's ally.
        // Also require the spell to actually reach out of the caster: a purely
        // self-ranged spell (e.g. a companion levitating/water-walking herself to
        // follow you) can never be "at" anyone, yet the engine's cast path still
        // resolves a spurious hit-contact target -- often the player standing in
        // front -- so target==player alone wrongly said "at you" (the reported bug).
        if (!target.isEmpty() && target == player && spellHasNonSelfEffect(effects))
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
                // Name the readied weapon, or the native unarmed skill name
                // (#{sSkillHandtohand}) when unarmed -- resolved by speak()'s tag
                // replacement, so it stays correct under localisation.
                std::string name = "#{sSkillHandtohand}";
                MWWorld::InventoryStore& inv = player.getClass().getInventoryStore(player);
                auto slot = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                if (slot != inv.end() && !slot->isEmpty())
                    name = slot->getClass().getName(*slot);
                speak(name + " ready");
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
                // generic "Magic ready" instead of naming the spell.
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
                    speak("Magic ready");
                else
                    speak(name + " ready");
                break;
            }
            case MWMechanics::DrawState::Nothing:
            default:
                // Distinguish what was put away so the player knows which mode
                // they left (matches the two ready announcements).
                if (prev == static_cast<int>(MWMechanics::DrawState::Spell))
                    speak("Magic put away");
                else
                    speak("Weapon sheathed");
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
        // The point on target's visual bounding box nearest to fromPos (clamped
        // per-axis; the point itself if inside). Returns the reference origin when
        // no renderable bounds exist. Shared by distanceToBounds (reach test) and
        // Scanner::nearestBoundsPoint (routing target).
        osg::Vec3f nearestBoundsPointImpl(const osg::Vec3f& fromPos, const MWWorld::Ptr& target)
        {
            const osg::Vec3f origin = target.getRefData().getPosition().asVec3();
            auto* node = target.getRefData().getBaseNode();
            if (!node)
                return origin;

            osg::ComputeBoundsVisitor cb;
            cb.setTraversalMask(~(MWRender::Mask_ParticleSystem | MWRender::Mask_Effect));
            node->accept(cb);
            const osg::BoundingBox& bb = cb.getBoundingBox();
            if (!bb.valid())
                return origin;

            // Clamp the point to the box on each axis; the clamped point is the
            // nearest surface point (equal to fromPos when inside the box).
            return osg::Vec3f(std::clamp(fromPos.x(), bb.xMin(), bb.xMax()),
                std::clamp(fromPos.y(), bb.yMin(), bb.yMax()),
                std::clamp(fromPos.z(), bb.zMin(), bb.zMax()));
        }

        float distanceToBounds(const osg::Vec3f& fromPos, const MWWorld::Ptr& target)
        {
            return (nearestBoundsPointImpl(fromPos, target) - fromPos).length();
        }
    }

    osg::Vec3f Scanner::nearestBoundsPoint(const osg::Vec3f& fromPos, const MWWorld::Ptr& target)
    {
        if (target.isEmpty())
            return fromPos;
        return nearestBoundsPointImpl(fromPos, target);
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

        // Road signposts are scriptless activators, so the engine's activation
        // does nothing -- pressing Activate on one appears dead. Repurpose it to
        // re-speak the sign's direction (the useful thing it conveys), so the
        // player can re-hear "Vos, northwest, 1800 metres" without re-walking the
        // scanner list. Info-only, so it is NOT gated on activation reach -- you
        // read a sign from a distance. Only triggers when the name resolves to a
        // place (true signposts); other activators fall through to normal
        // activation below. Skip when the direction would be empty (interior /
        // unknown place) so we never consume the key without saying anything.
        if (target.getClass().isActivator())
        {
            const MWWorld::Ptr p = MWBase::Environment::get().getWorld()->getPlayerPtr();
            if (!p.isEmpty())
            {
                std::string dir;
                appendSignpostDirection(target, p.getRefData().getPosition().asVec3(), dir);
                if (!dir.empty())
                {
                    // dir carries its own leading ", ", so this reads e.g.
                    // "Vos, northwest, 1800 metres."
                    speak(objectDisplayName(target) + dir + ".");
                    return true; // handled; don't also fire the crosshair Activate
                }
            }
        }

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

        // If this MECHANISM runs a script, watch for what its activation
        // actually DOES. A sighted player sees the grate slide open or the
        // bridge extend; without this the blind player gets only the sound, and
        // for a spent mechanism not even that. Snapshot BEFORE dispatching so
        // the comparison captures the script's own effects.
        //
        // Restricted to activators deliberately. Plenty of ordinary things carry
        // scripts -- quest items, artefacts, some containers and NPCs -- but
        // their activation has an obvious, already-announced outcome (you picked
        // it up, it opened, they started talking). Watching those produced two
        // real misreports: picking up a scripted item announced "No visible
        // effect" on top of the pickup, and a watch still running from an
        // earlier activation blamed the player's own dropped item on a
        // mechanism ("Dwemer Coherer moved, 0.2 metres away"). Only activators
        // are opaque enough to need this.
        //
        // Editor markers are excluded too: they are invisible helpers (sound
        // emitters, weather and script triggers) that the player cannot see or
        // meaningfully "operate", so an activation landing on one is never a
        // mechanism being worked.
        const bool mechanism = target.getClass().isActivator() && !target.getClass().getScript(target).empty()
            && !isEditorMarker(target);
        if (mechanism)
        {
            mActivationSnapshot = snapshotCell(player);
            mWatchingActivation = true;
            mActivationReported = false;
            mActivationWatchName = objectDisplayName(target);
            mActivationTarget = target;
            mActivationLocalsBefore = snapshotScriptLocals(target);
            // Baseline for the time-skip check, so the first tick compares
            // against the moment of activation rather than against zero.
            mActivationWatchGameTime = MWBase::Environment::get().getWorld()->getTimeManager()->getGameTime();
            // Long enough to cover slow mechanisms: the Dagoth Ur crank needs
            // roughly six seconds of turning before its grate opens.
            mActivationWatchTimer = kActivationWatchSeconds;
        }

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
            if (mAutoWalker.start(wp->mPosition, wp->mName, wp->mExactArrival))
            {
                // Horizontal distance, matching the scanner/auto-walk convention.
                osg::Vec3f d = wp->mPosition - playerPos;
                float dist = std::sqrt(d.x() * d.x() + d.y() * d.y());
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
            // Horizontal distance, matching the scanner/auto-walk convention.
            osg::Vec3f d = targetPos - playerPos;
            float dist = std::sqrt(d.x() * d.x() + d.y() * d.y());
            speak("Walking to " + objectDisplayName(target)
                + ", " + formatDistance(dist) + ".");
        }
        else
        {
            speak("Cannot reach " + objectDisplayName(target) + ".");
        }
    }

    bool Scanner::confirmTeleportRisk(const std::string& name)
    {
        // A modal, keyboard-navigable prompt: the interactive message box is
        // already screen-reader aware (it announces the prompt, then each option
        // as "<label>, button. N of M" as you arrow through it), so this needs
        // no accessibility work of its own. Blocking means the answer comes back
        // inline and the teleport stays a single, readable code path.
        //
        // The wording has to do real work. It is the only thing standing between
        // a player who does not yet know the game's layout and a broken save, so
        // it says what goes wrong (skipping the intended route), what to do
        // instead (explore first), and when this is legitimate (nothing else
        // works). It names the destination so it is never ambiguous which
        // teleport is being confirmed.
        const std::vector<std::string> buttons = {
            "Cancel",
            "Teleport",
            "Teleport and stop warning me",
        };
        const std::string message = "Warning: teleporting to " + name
            + " is a last resort.\n\n"
              "It moves you straight there, ignoring whatever stands in the way -- "
              "locked doors, walls, and routes the game expects you to find for yourself. "
              "Used early, it can drop you somewhere you were never meant to reach yet "
              "and break quests or scripted events, sometimes without any obvious sign.\n\n"
              "Explore properly first and let auto-walk try. Only teleport when nothing else works.";

        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        // defaultFocus 0 = Cancel: the safe option is the one selected when the
        // prompt opens, so a reflexive Enter does nothing destructive.
        wm->interactiveMessageBox(message, buttons, /*block=*/true, /*defaultFocus=*/0);
        const int pressed = wm->readPressedButton();

        if (pressed == 2)
        {
            // "Stop warning me" is a considered choice, so persist it rather
            // than keeping it for this session only. The engine writes
            // settings.cfg on exit, so this survives a restart.
            Settings::game().mAccessibilityTeleportWarned.set(true);
            return true;
        }
        // Anything else (Cancel, Escape, or the box being dismissed) does not
        // teleport. Only an explicit "Teleport" proceeds.
        return pressed == 1;
    }

    void Scanner::teleportToTarget()
    {
        // The escape hatch for when auto-walk physically can't route to a target
        // that is obviously there (you levitated up to a ledge; no walkable path
        // leads back), or when you already KNOW from experience that a given
        // route fails every time and would rather not sit through the attempt.
        //
        // It deliberately does NOT require a failed walk first. That gate was
        // meant to stop casual use, but it did not: reaching a door auto-walk
        // can't route to is exactly what a scripted, partly-blocked tutorial
        // route looks like, so it armed there readily -- while still forcing an
        // experienced player to trigger a pointless failure first. The real
        // protection is (1) a hard block during character generation, where
        // skipping ahead breaks the game's own scripting, (2) the distance cap,
        // and (3) an explicit warning the player must confirm.
        //
        // Followers within range come along (ActionTeleport handles them).
        MWBase::World* world = MWBase::Environment::get().getWorld();

        // Character generation is scripted and order-dependent: the census
        // office expects you to walk out through the door that Sellus Gravius
        // unlocks, and vanilla's own CharGenDoorExitCaptain script only ends
        // chargen once he has handed over the package. A tester teleported to
        // that exit door early and broke the sequence. Blocking here matches
        // what the engine already does with quick keys (see
        // MWInput::ActionManager::quickKey), and the flag covers the whole
        // opening sequence rather than a cell list we would have to maintain.
        if (world->getGlobalFloat(MWWorld::Globals::sCharGenState) != -1)
        {
            speak("Teleport is not available during character creation.");
            return;
        }

        MWWorld::Ptr player = world->getPlayerPtr();
        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();

        // Resolve the destination from the current selection, the same way
        // walkToTarget does, so "teleport to it" always means the thing the
        // scanner just read out.
        osg::Vec3f dest;
        std::string name;
        if (isWaypointCategory())
        {
            const Waypoint* wp = currentWaypoint();
            if (!wp)
            {
                speak("No target selected.");
                return;
            }
            // A waypoint in another worldspace has no comparable position, so
            // there is nothing meaningful to teleport to.
            if (!wp->mReachable)
            {
                std::string where = wp->mAreaLabel.empty() ? std::string("a different area") : wp->mAreaLabel;
                speak(wp->mName + " is in " + where + "; cannot teleport there from here.");
                return;
            }
            dest = wp->mPosition;
            name = wp->mName;
        }
        else
        {
            MWWorld::Ptr target = currentTarget();
            if (target.isEmpty())
            {
                speak("No target selected.");
                return;
            }
            dest = target.getRefData().getPosition().asVec3();
            name = objectDisplayName(target);
        }

        // 3D straight-line gap: the headline use case is vertical (a ledge above
        // us), so a horizontal-only distance would understate it and let through
        // teleports the cap is meant to block.
        const osg::Vec3f d = dest - playerPos;
        const float dist = d.length();
        if (dist > kTeleportMaxDist)
        {
            // Too far to be a "pathfinding couldn't manage a short gap" case.
            // Refuse rather than become fast travel: this is a hard stop.
            speak(name + " is too far to teleport to.");
            return;
        }

        // Warn before the first teleport of the session-or-ever, and keep
        // warning until the player says they have understood. Teleporting
        // straight to a target skips whatever stood between you and it --
        // locked doors, scripted triggers, an intended route -- so the honest
        // framing is "last resort", not "convenient shortcut". A blind player
        // has no way to notice they have ended up somewhere impossible until
        // the game is already broken, which is exactly why this is a modal
        // prompt rather than a spoken line they might talk over.
        if (!Settings::game().mAccessibilityTeleportWarned)
        {
            if (!confirmTeleportRisk(name))
                return;
        }

        // Keep the player's current facing/pitch; only the position changes.
        const ESM::Position curPos = player.getRefData().getPosition();
        ESM::Position destPos;
        destPos.pos[0] = dest.x();
        destPos.pos[1] = dest.y();
        destPos.pos[2] = dest.z();
        destPos.rot[0] = curPos.rot[0];
        destPos.rot[1] = curPos.rot[1];
        destPos.rot[2] = curPos.rot[2];

        // Teleport via the engine's own ActionTeleport, which brings nearby
        // followers and handles landing / water-walking / Lua notification
        // correctly. The player's own cell id is the right destination: the
        // scanner only ever lists objects in the current worldspace, and for an
        // exterior the engine picks the actual cell from the position, so a
        // target a cell boundary away still resolves correctly.
        const ESM::RefId cellId = player.getCell()->getCell()->getId();
        MWWorld::ActionTeleport action(cellId, destPos, /*teleportFollowers=*/true);
        action.execute(world->getPlayerPtr());

        speak("Teleported to " + name + ".");
    }

    void Scanner::openSearch()
    {
        // Waypoints are a handful of map notes / Mark in the current cell, so a
        // filter would be more work than just cycling them.
        //
        // Locations are the opposite case and MUST be filterable: late in a game
        // the discovered-places list runs to hundreds of entries, and without a
        // filter the only way to reach one is to cycle past every other. That is
        // exactly the situation a search box exists for.
        if (mCategory == Category::Waypoints)
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
        // Count via currentListSize(), NOT state.mObjects: position-based
        // categories (Locations) keep their entries in mWaypoints, so counting
        // mObjects would report "no matches" for every successful search.
        const size_t matches = currentListSize();
        state.mIndex = matches == 0 ? -1 : 0;

        if (trimmed.empty())
        {
            speak(std::string("Filter cleared. ") + std::to_string(matches) + " " + categoryName(mCategory)
                + " in range.");
        }
        else if (matches == 0)
        {
            speak(std::string("No ") + categoryName(mCategory) + " matching " + trimmed + ".");
        }
        else
        {
            speak(std::string("Filter: ") + trimmed + ". " + std::to_string(matches) + " matching.");
        }

        if (matches > 0)
            announceCurrent();
        updateProximityCue();
    }

    void Scanner::onSearchCancelled()
    {
        // The prompt closed without applying a change. Re-announce the current
        // selection (if any) so the user knows focus is back on the scanner.
        // Checked via currentListSize() rather than currentTarget(), which is
        // always empty in position-based categories (their selection lives in
        // mWaypoints) and would leave a cancelled Locations search silent.
        if (currentListSize() > 0)
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

    void Scanner::announceTimeOfDay()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world)
            return;
        const MWWorld::DateTimeManager* timeMgr = world->getTimeManager();
        if (!timeMgr)
            return;

        // Mirror WaitDialog::onOpen's conversion EXACTLY -- same truncation to
        // whole hours, same 12-hour wrap, same localized tokens. The Rest
        // dialog is what the player hears today via the escape-back-out
        // workaround, so any divergence here would read as a bug. In
        // particular getHour() is fractional (9.97 is still "9 a.m."), and
        // static_cast truncates rather than rounds, which is what we want.
        int hour = static_cast<int>(timeMgr->getTimeStamp().getHour());
        const bool pm = hour >= 12;
        if (hour >= 13)
            hour -= 12;
        if (hour == 0)
            hour = 12; // midnight and noon both read as 12, not 0
        const std::string_view meridiem(pm ? "#{Calendar:pm}" : "#{Calendar:am}");

        // Date: day-of-month plus the localized month name. We drop the
        // dialog's "(Day N)" days-since-start counter -- it's bookkeeping
        // rather than orientation, and the question being answered here is
        // "are the shops open / is it dark out", not "how long have I played".
        const ESM::EpochTimeStamp date = timeMgr->getEpochTimeStamp();
        const std::string_view month = timeMgr->getMonthName();

        speak(Misc::StringUtils::format(
            "%i %s, %i %s.", hour, meridiem, date.mDay, month));
    }

    Scanner::CellSnapshot Scanner::snapshotCell(const MWWorld::Ptr& player)
    {
        CellSnapshot snap;
        MWWorld::CellStore* cellStore = player.getCell();
        if (!cellStore)
            return snap;

        // forEach visits only ENABLED refs, which is exactly what we want: an
        // object vanishing from this set means it was disabled (a grate removed,
        // a wall slid away), and one appearing means it was enabled or moved in.
        cellStore->forEach([&](const MWWorld::Ptr& ptr) {
            const ESM::RefNum ref = ptr.getCellRef().getRefNum();
            if (!ref.isSet())
                return true;
            snap.mEnabled.insert(ref);
            snap.mPositions.emplace(ref, ptr.getRefData().getPosition().asVec3());
            // Structural = the things a mechanism actually operates on, and that
            // the player could go and use afterwards: doors, other activators,
            // containers. Loose items and actors are excluded so that pocketing
            // a potion or a rat wandering off is never blamed on the lever.
            //
            // Statics are deliberately NOT structural. They are the bulk of a
            // cell's scenery, and mods animate them constantly for reasons that
            // have nothing to do with the player -- weather props, ambient
            // machinery, lifts. Including them meant that resting in a bed, or
            // riding the Tel Uvirith elevator, reported whatever piece of decor
            // happened to shift. Nearly all are also nameless, so they could
            // only ever be announced by their record id.
            const auto type = ptr.getType();
            if ((type == ESM::Door::sRecordId || type == ESM::Activator::sRecordId
                    || type == ESM::Container::sRecordId)
                && !isEditorMarker(ptr))
                snap.mStructural.insert(ref);
            return true;
        });
        return snap;
    }

    bool Scanner::reportCellChanges(const CellSnapshot& before)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return false;
        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();

        const CellSnapshot now = snapshotCell(player);

        // What disappeared (disabled): the classic "a grate//wall/portcullis
        // opened" outcome -- scripts almost always Disable the blocking object.
        struct Change
        {
            std::string mName;
            float mDist2;
        };
        std::vector<Change> vanished;
        std::vector<Change> appeared;
        std::vector<Change> moved;

        MWWorld::CellStore* cellStore = player.getCell();
        if (!cellStore)
            return false;

        // Appeared / moved: walk the current cell once.
        cellStore->forEach([&](const MWWorld::Ptr& ptr) {
            const ESM::RefNum ref = ptr.getCellRef().getRefNum();
            if (!ref.isSet())
                return true;
            const osg::Vec3f pos = ptr.getRefData().getPosition().asVec3();
            // NEVER speak a raw record id. objectDisplayName falls back to one
            // for nameless objects, which is right for the scanner list but
            // wrong here: resting in Tel Uvirith announced
            // "AO_Weather_act_Wood moved, 2.1 metres away". A record id is
            // developer text -- meaningless to a player and gibberish through a
            // synthesiser. Nameless objects are still worth reporting (the
            // Dagoth Ur grate has no name either), just anonymously, as
            // "Something moved" -- what matters is that the way ahead changed,
            // not what the modder called the mesh.
            const std::string name(ptr.getClass().getName(ptr));
            const float d2 = (pos - playerPos).length2();

            if (!before.mEnabled.count(ref))
            {
                // Something newly enabled, or moved into the cell. Report
                // actors (the Dagoth Ur crank releases a dwarven specter right
                // next to you -- exactly the kind of thing a blind player must
                // not learn about by being attacked) and structural pieces
                // (a bridge or stair appearing). Skip loose items, which are
                // usually just the player dropping something mid-watch.
                const auto t = ptr.getType();
                const bool structural = t == ESM::Door::sRecordId || t == ESM::Activator::sRecordId
                    || t == ESM::Container::sRecordId;
                if (!structural && !ptr.getClass().isActor())
                    return true;
                if (isEditorMarker(ptr))
                    return true;
                appeared.push_back({ name, d2 });
                return true;
            }
            // Only STRUCTURAL things count as a mechanism moving (a door
            // sliding, a bridge extending). Actors wander constantly, and loose
            // items move because the PLAYER moved them -- dropping a scripted
            // item during a watch was reported as "Dwemer Coherer moved, 0.2
            // metres away", blaming the mechanism for the player's own action.
            if (!before.mStructural.count(ref))
                return true;
            const auto it = before.mPositions.find(ref);
            if (it != before.mPositions.end())
            {
                // Ignore jitter: only report a move big enough to be real.
                constexpr float kMovedThreshold = 40.0f;
                if ((pos - it->second).length2() > kMovedThreshold * kMovedThreshold)
                    moved.push_back({ name, d2 });
            }
            return true;
        });

        // Vanished: refs present before that forEach no longer visits. Only
        // STRUCTURAL ones count -- an item disappearing because the player
        // picked it up, or an actor leaving, is not the mechanism's doing.
        for (const ESM::RefNum& ref : before.mStructural)
        {
            if (now.mEnabled.count(ref))
                continue;
            // We only kept positions, not names, for the "before" state, so we
            // can still give a direction even though the object is gone.
            const auto it = before.mPositions.find(ref);
            if (it == before.mPositions.end())
                continue;
            vanished.push_back({ std::string(), (it->second - playerPos).length2() });
        }

        auto nearestFirst = [](std::vector<Change>& v) {
            std::sort(v.begin(), v.end(), [](const Change& a, const Change& b) { return a.mDist2 < b.mDist2; });
        };
        nearestFirst(appeared);
        nearestFirst(moved);
        nearestFirst(vanished);

        std::string line;
        auto append = [&](const std::string& text) {
            if (!line.empty())
                line += " ";
            line += text;
        };

        // Phrase each outcome the way it matters to a player trying to proceed:
        // something opening up is the point; the object's own name is secondary.
        if (!vanished.empty())
        {
            const float dist = std::sqrt(vanished.front().mDist2);
            append("Something opened, " + MWAccessibility::formatDistance(dist) + " away.");
        }
        // An unnamed object is announced as "Something", never by its record id.
        auto subject = [](const std::string& name) -> std::string {
            return name.empty() ? std::string("Something") : name;
        };
        if (!moved.empty())
        {
            append(subject(moved.front().mName) + " moved, "
                + MWAccessibility::formatDistance(std::sqrt(moved.front().mDist2)) + " away.");
        }
        if (!appeared.empty())
        {
            append(subject(appeared.front().mName) + " appeared, "
                + MWAccessibility::formatDistance(std::sqrt(appeared.front().mDist2)) + " away.");
        }

        if (line.empty())
            return false;

        speak(line);
        // The world changed: cached lists no longer reflect it.
        for (auto& s : mLists)
            s.mDirty = true;
        return true;
    }

    std::vector<int> Scanner::snapshotScriptLocals(const MWWorld::Ptr& ptr)
    {
        std::vector<int> out;
        // A mechanism's own script can disable or delete it as part of firing,
        // so the remembered Ptr may have gone stale mid-watch.
        if (ptr.isEmpty() || !ptr.getRefData().isEnabled() || ptr.getClass().getScript(ptr).empty())
            return out;

        const MWScript::Locals& locals = ptr.getRefData().getLocals();
        out.reserve(locals.mShorts.size() + locals.mLongs.size() + locals.mFloats.size());
        for (const auto v : locals.mShorts)
            out.push_back(static_cast<int>(v));
        for (const auto v : locals.mLongs)
            out.push_back(static_cast<int>(v));
        // Scale before truncating so a slow timer (the Dagoth Ur crank counts
        // up in fractions of a second) still shows movement between frames.
        for (const auto v : locals.mFloats)
            out.push_back(static_cast<int>(v * 100.f));
        return out;
    }

    void Scanner::updateActivationWatch(float dt)
    {
        if (!mWatchingActivation)
            return;

        // Resting, waiting and travelling jump the clock by hours. Every
        // ambient script in the cell runs during that skip, so whatever moved
        // afterwards had nothing to do with the thing the player activated --
        // and in a bed's case the player activated the bed precisely IN ORDER
        // to skip time. Abandon the watch rather than attribute the world's
        // background churn to it.
        //
        // getGameTime() is in-game SECONDS. The whole watch lasts ten real
        // seconds, so even at a fast timescale normal play advances only a few
        // game-minutes; a jump of a game-hour in a single frame can only be a
        // rest, wait or travel.
        const double gameTime = MWBase::Environment::get().getWorld()->getTimeManager()->getGameTime();
        const double elapsedGameSeconds = gameTime - mActivationWatchGameTime;
        mActivationWatchGameTime = gameTime;
        constexpr double kTimeSkipSeconds = 3600.0; // one in-game hour in one frame
        if (elapsedGameSeconds > kTimeSkipSeconds || elapsedGameSeconds < 0.0)
        {
            mWatchingActivation = false;
            mActivationSnapshot = CellSnapshot();
            mActivationWatchName.clear();
            mActivationTarget = MWWorld::Ptr();
            mActivationLocalsBefore.clear();
            return;
        }

        mActivationWatchTimer -= dt;

        // Re-check every frame rather than only at the end: a fast mechanism is
        // reported promptly, while a slow one (the Dagoth Ur crank takes about
        // six seconds of turning) is still caught before the window closes.
        // Stop at the first reported change so one pull isn't narrated twice.
        if (!mActivationReported && reportCellChanges(mActivationSnapshot))
            mActivationReported = true;

        // Decide "it did nothing" as soon as we can PROVE it, instead of always
        // waiting out the full window. A mechanism that is going to act sets its
        // own state first (a doOnce latch, a counter starting), so if its script
        // variables are still untouched after a moment AND the world has not
        // changed, it genuinely ignored the player. Reporting that promptly
        // matters: a verdict arriving ten seconds later lands while the player
        // is doing something else and reads as a random remark.
        //
        // If the variables DID move, the activation was accepted and an effect
        // may still be pending, so we keep waiting for the world to change.
        // Only usable when the mechanism actually HAS locals to compare: a
        // scriptless-in-effect object gives us no early signal, and a mechanism
        // that disabled itself as part of firing also reads as empty -- calling
        // either "no effect" would be a confident lie, so we let those wait out
        // the full window and be judged on world changes alone.
        constexpr float kEarlyVerdictAfter = 0.75f;
        const float elapsed = kActivationWatchSeconds - mActivationWatchTimer;
        const std::vector<int> localsNow = snapshotScriptLocals(mActivationTarget);
        if (!mActivationReported && elapsed >= kEarlyVerdictAfter && !mActivationLocalsBefore.empty()
            && !localsNow.empty() && localsNow == mActivationLocalsBefore)
        {
            speak("No visible effect.");
            mActivationReported = true;
            mActivationWatchTimer = 0.f;
        }

        if (mActivationWatchTimer <= 0.f)
        {
            // Nothing observable happened at all. Say so rather than leaving the
            // player wondering whether the mechanism worked -- silence here is
            // indistinguishable from "the game ignored you".
            if (!mActivationReported)
                speak("No visible effect.");
            mWatchingActivation = false;
            mActivationSnapshot = CellSnapshot();
            mActivationWatchName.clear();
            mActivationTarget = MWWorld::Ptr();
            mActivationLocalsBefore.clear();
        }
    }

    void Scanner::detectInternalTeleport(const MWWorld::Ptr& player)
    {
        const osg::Vec3f pos = player.getRefData().getPosition().asVec3();

        // First sample after a load or a cell change: seed the baseline only.
        if (!mHavePlayerPos)
        {
            mLastPlayerPos = pos;
            mHavePlayerPos = true;
            return;
        }

        const osg::Vec3f delta = pos - mLastPlayerPos;
        const osg::Vec3f origin = mLastPlayerPos;
        mLastPlayerPos = pos;

        // A teleport is an instantaneous jump far beyond anything a single frame
        // of running, falling or levitating could cover. Morrowind's fastest
        // legitimate movement is a few hundred units per second, i.e. a handful
        // of units per frame; 400 units (~5.7 m) in one frame is unambiguous
        // while staying well clear of a fast fall or a lag spike.
        constexpr float kTeleportJump = 400.0f;
        if (delta.length2() < kTeleportJump * kTeleportJump)
            return;

        // Only speak for jumps we can attribute to an internal teleport door.
        // Other things move the player abruptly too -- Divine/Almsivi
        // Intervention, Recall, a scripted PositionCell -- but those either
        // change cell (handled by the cell-change path) or are deliberate acts
        // whose destination the player already knows. Requiring a matching door
        // nearby keeps us from narrating every scripted nudge.
        if (!isNearInternalTeleportDoor(player, origin) && !isNearInternalTeleportDoor(player, pos))
            return;

        mInternalTeleportOrigin = origin;
        mHaveInternalTeleportOrigin = true;

        // Describe the move as the player experienced it: which way, how far,
        // measured from where they were standing. This is the announcement that
        // replaces the silence -- without it a ladder simply relocates you.
        std::string line = "Moved ";
        const std::string climb = MWAccessibility::formatElevationDirectionFirst(delta.z());
        const float horiz = osg::Vec2f(delta.x(), delta.y()).length();
        line += climb.empty() ? "across" : climb;
        if (horiz > kUnitsPerMetre)
        {
            line += ", ";
            line += MWAccessibility::compassLabel(std::atan2(delta.x(), delta.y()));
        }
        // Point back the way they came, so returning doesn't require guesswork.
        // The departure point is also listed in Waypoints as "Back" (see
        // collectWaypoints), which can be auto-walked to.
        const osg::Vec3f back = origin - pos;
        line += ". Back is ";
        line += MWAccessibility::compassLabel(std::atan2(back.x(), back.y()));
        line += ", ";
        line += MWAccessibility::formatDistance(osg::Vec2f(back.x(), back.y()).length());
        line += ".";
        speak(line);

        // The player is somewhere entirely new: every cached list is stale.
        for (auto& s : mLists)
            s.mDirty = true;
    }

    bool Scanner::isNearInternalTeleportDoor(const MWWorld::Ptr& player, const osg::Vec3f& pos)
    {
        const MWWorld::CellStore* cellStore = player.getCell();
        if (!cellStore)
            return false;

        // Activating a door leaves the player standing right at it, and the
        // destination end of the pair likewise sits close to where you land.
        // Keep this generous enough for a large ladder mesh but tight enough
        // that an unrelated door across the room can't excuse a jump.
        constexpr float kDoorProximity = 400.0f;
        bool found = false;
        const_cast<MWWorld::CellStore*>(cellStore)->forEach([&](const MWWorld::Ptr& ptr) {
            if (found || ptr.getType() != ESM::Door::sRecordId)
                return true;
            if (!isInternalTeleport(ptr))
                return true;
            const osg::Vec3f d = ptr.getRefData().getPosition().asVec3() - pos;
            if (d.length2() <= kDoorProximity * kDoorProximity)
                found = true;
            return true;
        });
        return found;
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

    void Scanner::refreshCellHazards(const MWWorld::Ptr& player)
    {
        // Collecting hazards walks every ref in the cell and parses each distinct
        // script, so it is done once per cell rather than per frame.
        const void* cellId = static_cast<const void*>(player.getCell());
        if (cellId == mHazardCellId)
            return;
        mHazardCellId = cellId;
        mCellHazards = collectCellHazards(player);
        // A new cell is a new set of hazards, so nothing has been warned about
        // yet. (Without this, walking back into a cell would stay silent about a
        // pool the player has since forgotten.)
        mHazardWarned.clear();
    }

    void Scanner::updateHazardProximity()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        refreshCellHazards(player);
        if (mCellHazards.empty())
            return;

        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        const std::vector<HazardGroup> groups = groupHazards(mCellHazards, playerPos);

        // Drop any warning latch whose hazard the player has walked away from, so
        // a genuine second approach warns again. Re-arming uses a WIDER radius
        // than the warning itself, or a player standing right on the boundary
        // would re-trigger it every time they shifted a few units.
        //
        // NOTE the latch is keyed on the hazard's own nearest PIECE position,
        // which is stable as the player moves. Keying it on HazardGroup's
        // mNearestPos would be a bug: that field is "nearest piece to the
        // player", so it changes as you walk along a big pool, and every change
        // would read as a brand-new hazard and warn again.
        for (auto it = mHazardWarned.begin(); it != mHazardWarned.end();)
        {
            const bool stillNear = std::any_of(groups.begin(), groups.end(), [&](const HazardGroup& g) {
                return hazardGroupContains(g, *it) && g.mNearestDistance <= kHazardWarnRadius + kHazardRearmMargin;
            });
            it = stillNear ? it + 1 : mHazardWarned.erase(it);
        }

        for (const HazardGroup& g : groups)
        {
            if (g.mNearestDistance > kHazardWarnRadius)
                continue;
            // A hazard on another floor is not a threat to someone walking on
            // this one. Without this, standing above a lava cave warns about
            // terrain you cannot touch.
            if (std::abs(g.mNearestPos.z() - playerPos.z()) > kHazardWarnVertical)
                continue;
            const bool warned = std::any_of(mHazardWarned.begin(), mHazardWarned.end(),
                [&](const osg::Vec3f& p) { return hazardGroupContains(g, p); });
            if (warned)
                continue;
            // Latch on the group's IDENTITY (its first piece), not on whichever
            // piece happens to be nearest right now -- see the note above.
            mHazardWarned.push_back(g.mIdentityPos);

            const osg::Vec3f delta = g.mNearestPos - playerPos;
            std::string line = g.mName;
            if (g.mNearestDistance > kHazardHereRadius)
                line
                    += ", " + formatDistance(g.mNearestDistance) + " " + compassLabel(std::atan2(delta.x(), delta.y()));
            else
                line += ", at your feet";
            speak(line + ".");
        }
    }

    void Scanner::announceHeight()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        const osg::Vec3f feet = player.getRefData().getPosition().asVec3();

        // UNDERWATER first: while diving, the meaningful vertical reference is the
        // water surface, not the seabed. Report how far below it we are -- the
        // companion to the "above ground" readout when flying. isSwimming is true
        // only when actually submerged enough to swim (not mere wading), which is
        // exactly when depth matters. hasWater guards interiors with no water.
        MWWorld::CellStore* cell = player.getCell();
        if (cell && cell->getCell()->hasWater() && world->isSwimming(player))
        {
            const float surface = cell->getWaterLevel();
            const float depth = surface - feet.z();
            if (depth > 0.f)
            {
                speak(formatDistance(depth) + " underwater.");
                return;
            }
        }

        // Otherwise report height above the ground directly beneath us. A
        // downward ray from the feet finds the first solid surface below -- the
        // terrain heightmap outdoors, a floor or rooftop/bridge indoors or when
        // levitating over a structure (Door included so a closed trapdoor counts
        // as ground, not a gap). Actors are deliberately NOT in the mask: we want
        // the static ground, not an NPC we happen to be standing over.
        const MWPhysics::RayCastingInterface* rayCasting = world->getRayCasting();
        if (!rayCasting)
            return;
        const int mask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap
            | MWPhysics::CollisionType_Door;
        // Start a little above the feet so we don't begin already embedded in the
        // floor we're standing on (which would miss it and report a false drop).
        // Probe a long way down to catch genuine altitude while levitating.
        constexpr float kProbeUp = 8.f;
        constexpr float kProbeDown = 1.0e6f;
        const osg::Vec3f from = feet + osg::Vec3f(0.f, 0.f, kProbeUp);
        const osg::Vec3f to = feet - osg::Vec3f(0.f, 0.f, kProbeDown);
        const MWPhysics::RayCastingResult res = rayCasting->castRay(from, to, { player }, {}, mask);
        if (!res.mHit)
        {
            // Nothing below within the probe (e.g. a bottomless void or unloaded
            // ground): be honest rather than invent a number.
            speak("Ground not found below.");
            return;
        }

        const float clearance = feet.z() - res.mHitPos.z();
        // Dead-band ~0.75 m (one stair step), matching formatElevation, so normal
        // standing/walking reads as grounded rather than a jittery "0.2 metres".
        constexpr float kGroundDeadBand = 52.5f;
        if (clearance <= kGroundDeadBand)
            speak("On the ground.");
        else
            speak(formatDistance(clearance) + " above ground.");
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

    // The five fixed pitch stops, in ascending rot[0] order (engine convention:
    // rot[0] is pitch, NEGATIVE = looking up, POSITIVE = looking down, clamped by
    // the engine to +/- PI/2). Listed lowest-angle-value (straight up) to highest
    // (straight down) so a simple index +/- 1 walks them in screen-space order:
    // Shift+Up (aim higher) moves toward index 0, Shift+Down toward index 4.
    namespace
    {
        struct PitchStop
        {
            float angle;
            const char* label;
        };
        // Spacing is PI/4 (45 deg). Keep in this exact order/!count; aimPitch and
        // levelPitch index into it.
        constexpr std::array<PitchStop, 5> kPitchStops = { {
            { -kHalfPi, "Straight up" },
            { -kHalfPi / 2.f, "Up" },
            { 0.f, "Level" },
            { kHalfPi / 2.f, "Down" },
            { kHalfPi, "Straight down" },
        } };
    }

    void Scanner::aimPitch(bool up)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        const auto& pos = player.getRefData().getPosition();
        const float pitch = pos.rot[0];

        // Find the next stop in the requested direction. "Up" means a more
        // negative pitch (lower index); "down" a more positive one (higher
        // index). A small tolerance means that being essentially ON a stop
        // advances a full step rather than re-selecting the same one (mirrors the
        // snapToDirection ring logic). When already at the extreme, hold there and
        // still announce it, so the player gets feedback rather than silence.
        constexpr float tol = 1e-3f;
        int idx;
        if (up)
        {
            // Largest stop strictly below the current pitch (minus tolerance).
            idx = 0;
            for (int i = static_cast<int>(kPitchStops.size()) - 1; i >= 0; --i)
            {
                if (kPitchStops[i].angle < pitch - tol)
                {
                    idx = i;
                    break;
                }
            }
        }
        else
        {
            // Smallest stop strictly above the current pitch (plus tolerance).
            idx = static_cast<int>(kPitchStops.size()) - 1;
            for (int i = 0; i < static_cast<int>(kPitchStops.size()); ++i)
            {
                if (kPitchStops[i].angle > pitch + tol)
                {
                    idx = i;
                    break;
                }
            }
        }

        world->rotateObject(
            player, osg::Vec3f(kPitchStops[idx].angle, pos.rot[1], pos.rot[2]), MWBase::RotationFlag_none);
        speak(std::string(kPitchStops[idx].label) + ".");
    }

    void Scanner::levelPitch()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        const auto& pos = player.getRefData().getPosition();
        world->rotateObject(player, osg::Vec3f(0.f, pos.rot[1], pos.rot[2]), MWBase::RotationFlag_none);
        speak("Level.");
    }

    void Scanner::toggleDirectionFilter()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;

        if (mDirectionFilterActive)
        {
            // Disengage: drop the kept sector and show the full lists again.
            mDirectionFilterActive = false;
            mDirectionSector = -1;
            for (auto& s : mLists)
                s.mDirty = true;
            speak("Direction filter off.");
            return;
        }

        // Engage on the player's current facing. mDirectionSector is the 8-way
        // compass sector of their yaw; onFrame keeps it tracking as they turn.
        mDirectionFilterActive = true;
        mDirectionSector = compassSector(player.getRefData().getPosition().rot[2]);
        for (auto& s : mLists)
            s.mDirty = true;
        speak(std::string("Direction filter, ") + compassLabel(player.getRefData().getPosition().rot[2]) + ".");
    }

    bool Scanner::passesDirectionFilter(const osg::Vec3f& worldPos) const
    {
        if (!mDirectionFilterActive || mDirectionSector < 0)
            return true;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return true;

        // Keep objects whose absolute bearing snaps to the same compass sector
        // the player faces. atan2(x, y) matches the bearing convention used
        // everywhere in the scanner (0 = north, +X = east); compassSector snaps
        // it with the EXACT partition compassLabel speaks, so the kept set always
        // agrees with what the player would hear as the object's direction.
        const osg::Vec3f delta = worldPos - player.getRefData().getPosition().asVec3();
        // A target essentially on top of the player has no meaningful bearing;
        // keep it rather than let float noise decide a direction for it.
        if (delta.x() * delta.x() + delta.y() * delta.y() < 1.0f)
            return true;
        return compassSector(std::atan2(delta.x(), delta.y())) == mDirectionSector;
    }

    void Scanner::filterWaypointsByDirection(std::vector<Waypoint>& waypoints) const
    {
        if (!mDirectionFilterActive)
            return;
        std::erase_if(waypoints, [&](const Waypoint& wp) {
            // An unreachable note (different worldspace) has no comparable
            // bearing, so it can't belong to "this direction" -- drop it while
            // filtering. A reachable one is kept only if it's in the sector.
            return !wp.mReachable || !passesDirectionFilter(wp.mPosition);
        });
    }

    void Scanner::filterWaypointsByName(std::vector<Waypoint>& waypoints) const
    {
        const auto& state = mLists[static_cast<size_t>(mCategory)];
        if (state.mFilter.empty())
            return;
        // Substring, case-insensitive, on the spoken name only -- the same rule
        // the object search uses, so "balm" finds "Balmora" and the behaviour is
        // predictable across categories.
        std::erase_if(waypoints, [&](const Waypoint& wp) {
            return Misc::StringUtils::ciFind(wp.mName, state.mFilter) == std::string::npos;
        });
    }

    std::string Scanner::playerStatText(int index, const char* label) const
    {
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return {};

        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        const MWMechanics::DynamicStat<float>& stat = stats.getDynamic(index);
        // Truncate (static_cast<int>) rather than round, and use getModified(false)
        // for the max -- EXACTLY matching StatsWindow::vitalValue and the HUD bars
        // (hud.cpp / statswindow.cpp). Rounding here caused an off-by-one vs the
        // stats pane (e.g. 45.6 read as 46 here but 45 there). Fatigue (index 2)
        // can be negative, so only clamp current to >= 0 for health/magicka.
        int current = static_cast<int>(stat.getCurrent());
        const int max = static_cast<int>(stat.getModified(false));
        if (index != 2)
            current = std::max(0, current);
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

    void Scanner::announceObjectState()
    {
        // Read the selected object's LIVE local script variables and speak them.
        // A sighted player learns a mechanism's state from its animation pose (a
        // lever thrown up vs down, a valve open vs shut); a blind player has no
        // such cue. But the mod's script drives that pose from a local variable
        // -- AFFresh's Dwemer door puzzle, for instance, keys each of six levers
        // on a "short active" (1 = engaged) and opens the doors only when all six
        // read 1. Exposing those variables turns an invisible, guess-and-check
        // puzzle into a readable one, generally, without hard-coding any specific
        // mod: whatever state the script itself tracks, the player can hear.
        if (isWaypointCategory())
        {
            speak("No object selected.");
            return;
        }
        MWWorld::Ptr target = currentTarget();
        if (target.isEmpty())
        {
            speak("No object selected.");
            return;
        }

        const std::string name = objectDisplayName(target);

        // The script attached to this specific object (its BALT/base-record
        // script). Empty for the vast majority of world objects, which simply
        // have no scripted state to report.
        const ESM::RefId scriptId = target.getClass().getScript(target);
        if (scriptId.empty())
        {
            speak(name + " has no readable state.");
            return;
        }

        // Names live in the COMPILER's Locals (declaration order, by type);
        // values live in the object's runtime MWScript::Locals in the SAME
        // per-type index order. Walk them in parallel to pair name<->value.
        const Compiler::Locals& decls = MWBase::Environment::get().getScriptManager()->getLocals(scriptId);
        MWScript::Locals& locals = target.getRefData().getLocals();
        // Make sure the runtime store is populated for this script before we read
        // it (a freshly loaded object may not have been configured yet).
        locals.getSize(scriptId);

        // Heuristic: a variable is a "switch" we can phrase as on/off when its
        // name hints at a boolean state AND its value is exactly 0 or 1. Anything
        // else is read as a raw "name: value" so we never speak a confident wrong
        // interpretation (see the a11y honesty principle).
        auto looksBoolean = [](std::string_view n) {
            for (std::string_view key :
                { "active", "state", "open", "on", "enabled", "activated", "toggle", "switch", "set", "done" })
                if (Misc::StringUtils::ciFind(n, key) != std::string_view::npos)
                    return true;
            return false;
        };

        // A "do once" latch is the single most common way a Morrowind script
        // records that a one-shot mechanism has already fired: the script guards
        // its payload on the variable and bumps it when done. Reading it out
        // literally ("doOnce: 2") is honest but tells the player nothing; what
        // they actually need to know is whether pulling this thing again will do
        // anything. Non-zero means the script has moved past its initial state.
        //
        // We report this as a LEADING summary and still read the raw variables
        // afterwards, so nothing is hidden and a script using the name
        // differently can't make us speak a confident falsehood.
        auto isDoOnceLatch = [](std::string_view n) {
            return Misc::StringUtils::ciFind(n, "doonce") != std::string_view::npos
                || Misc::StringUtils::ciFind(n, "done") != std::string_view::npos;
        };
        std::string latchSummary;

        std::vector<std::string> parts;

        auto emitShortLong = [&](char type, auto readValue) {
            const std::vector<std::string>& names = decls.get(type);
            for (size_t i = 0; i < names.size(); ++i)
            {
                const std::string& vn = names[i];
                const int v = readValue(i);
                if (latchSummary.empty() && isDoOnceLatch(vn))
                    latchSummary = v != 0 ? "Already used." : "Not used yet.";
                if (looksBoolean(vn) && (v == 0 || v == 1))
                    parts.push_back(vn + ": " + (v ? "on" : "off"));
                else
                    parts.push_back(vn + ": " + std::to_string(v));
            }
        };

        // Shorts and longs are both integer-valued at runtime.
        emitShortLong('s', [&](size_t i) { return i < locals.mShorts.size() ? locals.mShorts[i] : 0; });
        emitShortLong('l', [&](size_t i) { return i < locals.mLongs.size() ? locals.mLongs[i] : 0; });

        // Floats: report with light formatting (trim trailing zeros); never
        // treated as on/off since a fractional value isn't a switch.
        {
            const std::vector<std::string>& names = decls.get('f');
            for (size_t i = 0; i < names.size(); ++i)
            {
                const float v = i < locals.mFloats.size() ? locals.mFloats[i] : 0.f;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.2f", v);
                // Trim trailing zeros / dot so 3.00 -> 3, 2.50 -> 2.5.
                std::string s(buf);
                if (s.find('.') != std::string::npos)
                {
                    s.erase(s.find_last_not_of('0') + 1);
                    if (!s.empty() && s.back() == '.')
                        s.pop_back();
                }
                parts.push_back(names[i] + ": " + s);
            }
        }

        if (parts.empty())
        {
            speak(name + " has no readable state.");
            return;
        }

        // Lead with the plain-language verdict when we have one, so the player
        // hears the answer to "will this still do anything?" before the raw
        // variable dump they may not need.
        std::string out = name + ". ";
        if (!latchSummary.empty())
            out += latchSummary + " ";
        for (size_t i = 0; i < parts.size(); ++i)
        {
            out += parts[i];
            out += (i + 1 < parts.size()) ? ". " : ".";
        }
        speak(out);
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
        // Use the native skill name (resolved by speak()'s tag replacement) so it
        // matches the rest of the game and stays correct under localisation.
        return "Weapon: #{sSkillHandtohand}";
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

    void Scanner::sortObjectsByLevelThenDistance(
        std::vector<MWWorld::Ptr>& objects, const osg::Vec3f& playerPos, bool levelGrouped)
    {
        // Reachability is a per-object property, not a pairwise one, and
        // isWithinActivationReach() does real work (bounds maths, telekinesis
        // lookup). Compute it ONCE per object here rather than inside the
        // comparator, which std::sort calls O(n log n) times -- and which would
        // also risk an inconsistent comparator if any input shifted mid-sort.
        std::vector<std::pair<MWWorld::Ptr, bool>> keyed;
        keyed.reserve(objects.size());
        for (const MWWorld::Ptr& ptr : objects)
            keyed.emplace_back(ptr, levelGrouped && isWithinActivationReach(ptr));

        std::sort(keyed.begin(), keyed.end(),
            [&playerPos, levelGrouped](
                const std::pair<MWWorld::Ptr, bool>& a, const std::pair<MWWorld::Ptr, bool>& b) {
                const osg::Vec3f pa = a.first.getRefData().getPosition().asVec3();
                const osg::Vec3f pb = b.first.getRefData().getPosition().asVec3();
                // Outside (exterior worldspace), floor-banding is meaningless --
                // terrain height varies continuously, so there are no discrete
                // storeys to group by and banding just scrambles the honest
                // nearest-first order (e.g. something slightly uphill jumps the
                // queue). Fall back to plain 3D distance there; only interiors,
                // where the player asked for it, get the level grouping.
                if (!levelGrouped)
                    return (pa - playerPos).length2() < (pb - playerPos).length2();
                // Horizontal (x,y) squared distance -- vertical is handled by the
                // level grouping, not folded into the in-level distance.
                const float aHoriz2 = (pa.x() - playerPos.x()) * (pa.x() - playerPos.x())
                    + (pa.y() - playerPos.y()) * (pa.y() - playerPos.y());
                const float bHoriz2 = (pb.x() - playerPos.x()) * (pb.x() - playerPos.x())
                    + (pb.y() - playerPos.y()) * (pb.y() - playerPos.y());
                // Reachable-first: something the player can touch from where they
                // stand outranks anything they cannot, whichever band it lands
                // in. Without this a door raised barely over half a storey (~1.6 m
                // -- routine in ramped/interlocking dungeon geometry) sorted
                // behind the entire current floor despite being openable from
                // right here.
                return lessByReachThenLevelThenDistance(
                    a.second, pa.z() - playerPos.z(), aHoriz2, b.second, pb.z() - playerPos.z(), bHoriz2);
            });

        for (size_t i = 0; i < keyed.size(); ++i)
            objects[i] = keyed[i].first;
    }

    bool Scanner::lessWaypointByLevelThenDistance(
        const Waypoint& a, const Waypoint& b, const osg::Vec3f& playerPos)
    {
        const float aHoriz2 = (a.mPosition.x() - playerPos.x()) * (a.mPosition.x() - playerPos.x())
            + (a.mPosition.y() - playerPos.y()) * (a.mPosition.y() - playerPos.y());
        const float bHoriz2 = (b.mPosition.x() - playerPos.x()) * (b.mPosition.x() - playerPos.x())
            + (b.mPosition.y() - playerPos.y()) * (b.mPosition.y() - playerPos.y());
        return lessByLevelThenDistance(
            a.mPosition.z() - playerPos.z(), aHoriz2, b.mPosition.z() - playerPos.z(), bHoriz2);
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
            filterWaypointsByDirection(state.mWaypoints);
            return;
        }

        // The Locations category is likewise position-based: discovered global-
        // map places (visited towns + NPC-marked spots), one entry per town.
        if (mCategory == Category::Locations)
        {
            collectLocations(state.mWaypoints);
            filterWaypointsByDirection(state.mWaypoints);
            // Late in a game this list runs to hundreds of entries, so the name
            // filter is what makes a specific place reachable without cycling
            // past everything else.
            filterWaypointsByName(state.mWaypoints);
            return;
        }

        // Terrain is likewise position-based: room features (hazards, shafts)
        // presented as ordinary scanner entries so they can be cycled, faced and
        // auto-walked to with the standard keys.
        if (mCategory == Category::Terrain)
        {
            collectTerrain(state.mSubIndex, state.mWaypoints);
            filterWaypointsByDirection(state.mWaypoints);
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
                    return Misc::StringUtils::ciFind(objectSearchText(ptr, state.mMarked), state.mFilter)
                        == std::string_view::npos;
                });
            }

            // Global direction filter (Ctrl+Up), same as the cell-scan path.
            if (mDirectionFilterActive)
            {
                std::erase_if(state.mObjects, [&](const MWWorld::Ptr& ptr) {
                    return !passesDirectionFilter(ptr.getRefData().getPosition().asVec3());
                });
            }

            // Marked-object view (Shift+K): hide marked or unmarked objects
            // depending on the mode, same as the cell-scan path.
            if (mMarkedView != MarkedView::All)
            {
                std::erase_if(state.mObjects, [&](const MWWorld::Ptr& ptr) {
                    return isHiddenUnderMarkedView(state.mMarked.count(ptr.getCellRef().getRefNum()) != 0);
                });
            }

            osg::Vec3f pp = player.getRefData().getPosition().asVec3();
            // Floor-grouping only indoors (see sortObjectsByLevelThenDistance).
            const bool levelGrouped = player.getCell() && !player.getCell()->isExterior();
            sortObjectsByLevelThenDistance(state.mObjects, pp, levelGrouped);

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
                // Mages", plus any custom mark note the player wrote). Empty
                // filter matches everything.
                if (!state.mFilter.empty()
                    && Misc::StringUtils::ciFind(objectSearchText(ptr, state.mMarked), state.mFilter)
                        == std::string_view::npos)
                    return true;
                // Global direction filter (Ctrl+Up): drop anything not lying in
                // the compass sector the player faces. No-op when disengaged.
                if (!passesDirectionFilter(ptr.getRefData().getPosition().asVec3()))
                    return true;
                // Marked-object view (Shift+K): hide marked or unmarked objects
                // depending on the mode. No-op in the default "All" mode.
                if (isHiddenUnderMarkedView(state.mMarked.count(ptr.getCellRef().getRefNum()) != 0))
                    return true;
                state.mObjects.push_back(ptr);
                return true;
            });
        }

        osg::Vec3f pp = player.getRefData().getPosition().asVec3();
        // Floor-grouping only indoors (see sortObjectsByLevelThenDistance).
        const bool levelGrouped = player.getCell() && !player.getCell()->isExterior();
        sortObjectsByLevelThenDistance(state.mObjects, pp, levelGrouped);

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

        // How many entries BEFORE the selection are being removed. Subtracting
        // this from the old index gives the slot the next surviving object
        // shifts up into -- i.e. the player's place in the list, preserved even
        // though the object that held it is gone.
        int deadBeforeSelection = 0;
        bool removedSelection = false;
        std::vector<MWWorld::Ptr> kept;
        kept.reserve(state.mObjects.size());
        for (int i = 0; i < static_cast<int>(state.mObjects.size()); ++i)
        {
            MWWorld::Ptr& ptr = state.mObjects[i];
            if (isDead(ptr))
            {
                if (!selected.isEmpty() && ptr == selected)
                    removedSelection = true;
                else if (i < state.mIndex)
                    ++deadBeforeSelection;
                continue;
            }
            kept.push_back(ptr);
        }

        if (kept.size() == state.mObjects.size())
            return; // Nothing changed.

        state.mObjects = std::move(kept);

        // Re-pin the cursor. If the selected object survived, follow it to its
        // new index.
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
        else if (removedSelection)
        {
            // The selected object is the one that vanished -- typically the
            // player just picked it up. Keep their PLACE in the list rather
            // than the (now meaningless) object identity: the next surviving
            // entry shifts up into the vacated slot, which is exactly where
            // they want to continue. Previously the selection was dropped
            // entirely, so taking item 135 of 300 in a library sent the cursor
            // back to the start and everything already searched had to be
            // paged through again -- one keypress per item, since the cycle
            // keys don't repeat when held. (Reported 2026-08-21.)
            //
            // This mirrors what marking an object already does (see
            // toggleMarkCurrent), so taking and marking now behave alike.
            if (state.mObjects.empty())
            {
                state.mIndex = -1;
            }
            else
            {
                const int landed = std::clamp(state.mIndex - deadBeforeSelection, 0,
                    static_cast<int>(state.mObjects.size()) - 1);
                state.mIndex = landed;
                // Refresh the remembered identity, or the next full rebuild
                // would try to re-pin onto the object we just lost.
                state.mSelectedRef = state.mObjects[landed].getCellRef().getRefNum();
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
        // Headline distance is HORIZONTAL (ground) distance, not 3D straight-line.
        // The vertical component is reported separately via formatElevation below,
        // so a target almost directly overhead reads "4 metres, north. 21 metres
        // up." instead of a 3D "21 metres" that double-counts the height and
        // contradicts auto-walk's horizontal "stopped N metres short". See
        // formatElevation for the dead-band that keeps same-level targets quiet.
        float dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());

        // targetYaw is an absolute world bearing (0 = north, +X = east), a
        // fixed compass reference that doesn't change as the player turns.
        float targetYaw = std::atan2(delta.x(), delta.y());

        std::string name = objectDisplayName(target);
        appendDoorDestination(target, name);
        // NOTE: a signpost's destination direction is deliberately NOT added here.
        // The list readout already states the bearing/distance to the SIGN itself,
        // and adding the bearing to the place it points at on the same line is
        // confusing (two different directions back to back). Destination direction
        // is spoken on demand when the player activates the sign instead -- see
        // activateTarget / appendSignpostDirection.

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

        // Lock / trap state for containers and doors, at parity with the vanilla
        // hover tooltip (lock level when locked, "Unlocked", "Trapped"). The
        // fragment already carries its leading ", " and is localised, e.g.
        // "Chest, Lock Level: 50, Trapped".
        name += lockTrapLabel(target);

        // If the player has marked this object as already-looked-at (K key),
        // say so at the end of its identity. Read live from mMarked so it
        // reflects the current state (a mark/unmark takes effect immediately),
        // and keyed by the same stable RefNum as the disambiguation letters. A
        // custom note (Ctrl+K) is spoken just before the ", marked" cue, so a
        // labelled object reads e.g. "Gjalund, Khuul shipmaster, marked".
        if (auto it = state.mMarked.find(target.getCellRef().getRefNum()); it != state.mMarked.end())
        {
            if (!it->second.empty())
                name += ", " + it->second;
            name += ", marked";
        }

        // Direction is the absolute compass heading -- a fixed frame the
        // player can use to remember where a thing is regardless of which way
        // they're facing. Elevation (if the target is meaningfully above or
        // below us) follows the bearing; positional "N of M" stays at the very
        // end (project convention).
        std::string elevation = formatElevation(delta.z());

        // Where an in-cell ladder/shaft/hatch leads goes at the END, after the
        // distance to the door itself: the player wants to know how far away the
        // thing is before hearing where it would take them. (Cross-cell doors
        // keep naming their destination as part of the object's identity, up
        // front -- that name is how you tell two doors apart, whereas these are
        // all called the same thing and are distinguished by where they go.)
        const std::string leads = internalTeleportLeadsLabel(target);

        std::string msg = name + ". " + formatDistance(dist)
            + ", " + compassLabel(targetYaw) + ". "
            + (elevation.empty() ? "" : elevation + ". ")
            + (leads.empty() ? "" : leads + ". ")
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

        // Where the last in-cell teleport (ladder/shaft/hatch) brought the
        // player FROM. These doors relocate you within one cell with no cell
        // change to announce, and mods often place several identically named
        // ones, so without a way back the player is simply lost. Always in the
        // current cell and worldspace by construction, hence reachable and
        // auto-walkable. Cleared on any cell change (see onFrame).
        if (mHaveInternalTeleportOrigin)
        {
            Waypoint wp;
            wp.mName = "Back";
            wp.mPosition = mInternalTeleportOrigin;
            wp.mReachable = true;
            out.push_back(std::move(wp));
        }

        // Reachable waypoints first (nearest-first within that group, matching
        // the object categories' distance sort); unreachable ones after, by
        // name, since distance is meaningless for them.
        // Floor-grouping only indoors: a reachable note is in the player's own
        // worldspace, so if that's an interior its notes share its storeys;
        // outdoors fall back to plain nearest-first (no discrete floors).
        const bool levelGrouped = !cellStore->isExterior();
        std::sort(out.begin(), out.end(), [&playerPos, levelGrouped](const Waypoint& a, const Waypoint& b) {
            if (a.mReachable != b.mReachable)
                return a.mReachable; // reachable sorts before unreachable
            if (a.mReachable)
            {
                if (levelGrouped)
                    return lessWaypointByLevelThenDistance(a, b, playerPos);
                return (a.mPosition - playerPos).length2() < (b.mPosition - playerPos).length2();
            }
            return a.mName < b.mName;
        });
    }

    void Scanner::collectTerrain(int subIndex, std::vector<Waypoint>& out) const
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty())
            return;
        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();

        // Subcategory indices follow kTerrainSubs: 0 = All, 1 = Hazards,
        // 2 = Shafts.
        const bool wantHazards = (subIndex == 0 || subIndex == 1);
        const bool wantShafts = (subIndex == 0 || subIndex == 2);

        if (wantHazards)
        {
            // Reuse the same grouping the proximity warning uses, so a pool is
            // one entry here exactly as it is one announcement there -- the
            // player never sees two different notions of "a hazard".
            const std::vector<HazardObject> objects = collectCellHazards(player);
            for (const HazardGroup& g : groupHazards(objects, playerPos))
            {
                Waypoint wp;
                wp.mName = g.mName;
                // The nearest EDGE, not the pool's centre: that is the point the
                // player must not cross, and the one worth facing or measuring.
                wp.mPosition = g.mNearestPos;
                wp.mReachable = true;
                out.push_back(std::move(wp));
            }
        }

        if (wantShafts)
        {
            for (const VerticalShaft& s : collectCellShafts(player))
            {
                Waypoint wp;
                // Carry the floor-opening count in the name. It's the one fact
                // about a shaft the generic waypoint readout (name, distance,
                // bearing, elevation) can't express, and it tells the player
                // whether this column actually serves other floors.
                wp.mName = "Shaft";
                if (!s.mOpenings.empty())
                {
                    wp.mName += ", " + std::to_string(s.mOpenings.size())
                        + (s.mOpenings.size() == 1 ? " floor opening" : " floor openings");
                }
                // Target the shaft's column at the player's OWN height, which is
                // what makes auto-walking to this entry equivalent to the old
                // dedicated "walk into the shaft" key: you end up standing in the
                // column, ready to levitate, rather than trying to walk to a
                // point above or below you.
                wp.mPosition = osg::Vec3f(s.mX, s.mY, playerPos.z());
                wp.mReachable = true;
                // Standing at the rim is not good enough here: levitating from
                // beside the shaft just presses the player into the ceiling.
                wp.mExactArrival = true;
                out.push_back(std::move(wp));
            }
        }

        // Nearest-first, matching every other category's ordering. All terrain
        // entries are in the player's own cell, so they are always reachable and
        // there is no unreachable group to sort after.
        std::sort(out.begin(), out.end(), [&playerPos](const Waypoint& a, const Waypoint& b) {
            return (a.mPosition - playerPos).length2() < (b.mPosition - playerPos).length2();
        });

        // Disambiguate same-named entries ("Lava", "Lava", ...) the way the
        // object lists do, so the player can tell which one is being announced.
        if (out.size() > 1)
        {
            std::map<std::string, int> counts;
            for (const Waypoint& wp : out)
                ++counts[wp.mName];
            std::map<std::string, int> seen;
            for (Waypoint& wp : out)
            {
                // Look both counters up under the ORIGINAL name and only then
                // rename, or the suffixed name ("Lava A") would be treated as a
                // different, unique name on the next lookup and the numbering
                // would restart.
                const std::string base = wp.mName;
                if (counts[base] > 1)
                    wp.mName = base + " " + letterForIndex(static_cast<std::size_t>(seen[base]++));
            }
        }
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
        // Horizontal (ground) distance; the height goes in the elevation phrase
        // below, matching the target readout and auto-walk's horizontal measure.
        float dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
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
