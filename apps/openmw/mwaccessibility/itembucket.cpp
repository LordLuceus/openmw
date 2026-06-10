#include "itembucket.hpp"

#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadweap.hpp>

namespace MWAccessibility
{
    ItemBucket classifyItemType(unsigned int recordType)
    {
        if (recordType == ESM::Weapon::sRecordId)
            return ItemBucket::Weapon;
        if (recordType == ESM::Armor::sRecordId)
            return ItemBucket::Armor;
        if (recordType == ESM::Clothing::sRecordId)
            return ItemBucket::Clothing;
        if (recordType == ESM::Potion::sRecordId)
            return ItemBucket::Potion;
        if (recordType == ESM::Ingredient::sRecordId)
            return ItemBucket::Ingredient;
        if (recordType == ESM::Book::sRecordId)
            return ItemBucket::BookOrScroll;
        // "Tools": apparatus, lockpicks, probes, repair items, and carryable
        // lights (torches) -- the usable utility odds and ends.
        if (recordType == ESM::Apparatus::sRecordId || recordType == ESM::Lockpick::sRecordId
            || recordType == ESM::Probe::sRecordId || recordType == ESM::Repair::sRecordId
            || recordType == ESM::Light::sRecordId)
            return ItemBucket::Tool;
        // Everything else carryable (papers, keys, gold, soul gems) is Misc.
        return ItemBucket::Misc;
    }
}
