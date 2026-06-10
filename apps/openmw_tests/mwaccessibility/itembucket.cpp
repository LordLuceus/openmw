#include "apps/openmw/mwaccessibility/itembucket.hpp"

#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
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

#include <gtest/gtest.h>

namespace MWAccessibility
{
    namespace
    {
        // Each specific record type maps to its own bucket. Driven off the real
        // ESM::*::sRecordId values (not hardcoded ints) so the test stays correct
        // if the engine ever renumbers them.

        TEST(MWAccessibilityItemBucket, specificTypesMapToTheirBucket)
        {
            EXPECT_EQ(classifyItemType(ESM::Weapon::sRecordId), ItemBucket::Weapon);
            EXPECT_EQ(classifyItemType(ESM::Armor::sRecordId), ItemBucket::Armor);
            EXPECT_EQ(classifyItemType(ESM::Clothing::sRecordId), ItemBucket::Clothing);
            EXPECT_EQ(classifyItemType(ESM::Potion::sRecordId), ItemBucket::Potion);
            EXPECT_EQ(classifyItemType(ESM::Ingredient::sRecordId), ItemBucket::Ingredient);
            EXPECT_EQ(classifyItemType(ESM::Book::sRecordId), ItemBucket::BookOrScroll);
        }

        TEST(MWAccessibilityItemBucket, allToolTypesShareTheToolBucket)
        {
            EXPECT_EQ(classifyItemType(ESM::Apparatus::sRecordId), ItemBucket::Tool);
            EXPECT_EQ(classifyItemType(ESM::Lockpick::sRecordId), ItemBucket::Tool);
            EXPECT_EQ(classifyItemType(ESM::Probe::sRecordId), ItemBucket::Tool);
            EXPECT_EQ(classifyItemType(ESM::Repair::sRecordId), ItemBucket::Tool);
            EXPECT_EQ(classifyItemType(ESM::Light::sRecordId), ItemBucket::Tool);
        }

        TEST(MWAccessibilityItemBucket, miscTypeIsMisc)
        {
            // ESM::Miscellaneous (papers/keys/gold/soul gems) is the archetypal
            // member of the catch-all bucket.
            EXPECT_EQ(classifyItemType(ESM::Miscellaneous::sRecordId), ItemBucket::Misc);
        }

        TEST(MWAccessibilityItemBucket, unclaimedTypesFallThroughToMisc)
        {
            // Types the Items category would never feed in (actors, doors) and a
            // nonsense id all land in Misc rather than misclassifying: the
            // function is total, so the catch-all must absorb anything unknown.
            EXPECT_EQ(classifyItemType(ESM::NPC::sRecordId), ItemBucket::Misc);
            EXPECT_EQ(classifyItemType(ESM::Creature::sRecordId), ItemBucket::Misc);
            EXPECT_EQ(classifyItemType(ESM::Door::sRecordId), ItemBucket::Misc);
            EXPECT_EQ(classifyItemType(0u), ItemBucket::Misc);
            EXPECT_EQ(classifyItemType(0xFFFFFFFFu), ItemBucket::Misc);
        }
    }
}
