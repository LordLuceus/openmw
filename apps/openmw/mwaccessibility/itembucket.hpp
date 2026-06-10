#ifndef GAME_MWACCESSIBILITY_ITEMBUCKET_H
#define GAME_MWACCESSIBILITY_ITEMBUCKET_H

namespace MWAccessibility
{
    // The item subcategory buckets the scanner offers under the Items category
    // (Shift+Page cycles them). This is a pure classification of an ESM record
    // type, split out from scanner.cpp so the bucketing -- in particular the
    // "Misc = anything not in another bucket" catch-all, which silently absorbs
    // any record type a future bucket forgets to claim -- can be unit-tested
    // without standing up the engine.
    enum class ItemBucket
    {
        Weapon,
        Armor,
        Clothing,
        Potion,
        Ingredient,
        BookOrScroll,
        // Apparatus, lockpicks, probes, repair items, carryable lights.
        Tool,
        // Papers, keys, gold, soul gems -- everything carryable not above.
        Misc,
    };

    // Classify an ESM record type id (as returned by MWWorld::Ptr::getType,
    // i.e. an ESM::REC_* value) into its item subcategory bucket. Any type not
    // matching a specific bucket falls through to Misc, so this never fails to
    // classify -- it is only meaningful for types the Items category already
    // accepts (Class::isItem), but is total over all inputs.
    ItemBucket classifyItemType(unsigned int recordType);
}

#endif
