#include "apps/openmw/mwaccessibility/spokenformat.hpp"

#include <gtest/gtest.h>

namespace MWAccessibility
{
    namespace
    {
        // --- formatDistance --------------------------------------------------
        // Under 10 m reads with one decimal; at/above 10 m rounds to a whole
        // number. Inputs are Morrowind world units (~69.99 per metre).

        TEST(MWAccessibilitySpokenFormat, formatDistanceZeroIsOneDecimal)
        {
            EXPECT_EQ(formatDistance(0.f), "0.0 metres");
        }

        TEST(MWAccessibilitySpokenFormat, formatDistanceUnderTenMetresHasOneDecimal)
        {
            // 1 metre.
            EXPECT_EQ(formatDistance(kUnitsPerMetre), "1.0 metres");
            // 4.2 metres.
            EXPECT_EQ(formatDistance(4.2f * kUnitsPerMetre), "4.2 metres");
        }

        TEST(MWAccessibilitySpokenFormat, formatDistanceAtAndAboveTenMetresRoundsToWhole)
        {
            // Just above 10 m to stay clear of float rounding right on the
            // < 10.0f branch edge; this is firmly in the whole-number branch and
            // rounds 10.2 -> 10.
            EXPECT_EQ(formatDistance(10.2f * kUnitsPerMetre), "10 metres");
        }

        TEST(MWAccessibilitySpokenFormat, formatDistanceAboveTenMetresRoundsHalfUp)
        {
            // 23.4 m rounds down to 23.
            EXPECT_EQ(formatDistance(23.4f * kUnitsPerMetre), "23 metres");
            // 23.6 m rounds up to 24.
            EXPECT_EQ(formatDistance(23.6f * kUnitsPerMetre), "24 metres");
        }

        // --- formatElevation -------------------------------------------------
        // Dead-band of ~0.75 m (52.5 units) returns empty; otherwise reads like
        // formatDistance with an "up"/"down" suffix from the sign of dz.

        TEST(MWAccessibilitySpokenFormat, formatElevationWithinDeadBandIsEmpty)
        {
            EXPECT_EQ(formatElevation(0.f), "");
            EXPECT_EQ(formatElevation(52.5f), ""); // exactly on the dead-band edge
            EXPECT_EQ(formatElevation(-52.5f), "");
        }

        TEST(MWAccessibilitySpokenFormat, formatElevationPositiveIsUp)
        {
            EXPECT_EQ(formatElevation(2.f * kUnitsPerMetre), "2.0 metres up");
        }

        TEST(MWAccessibilitySpokenFormat, formatElevationNegativeIsDown)
        {
            EXPECT_EQ(formatElevation(-3.f * kUnitsPerMetre), "3.0 metres down");
        }

        TEST(MWAccessibilitySpokenFormat, formatElevationAboveTenMetresRoundsToWhole)
        {
            EXPECT_EQ(formatElevation(12.f * kUnitsPerMetre), "12 metres up");
            EXPECT_EQ(formatElevation(-15.f * kUnitsPerMetre), "15 metres down");
        }

        // --- formatElevationDirectionFirst -----------------------------------
        // Same measurement as formatElevation, with the direction moved to the
        // front. Used where the direction is the point and must be heard first:
        // where a ladder/shaft leads, and where one just moved the player.

        TEST(MWAccessibilitySpokenFormat, formatElevationDirectionFirstPutsDirectionFirst)
        {
            EXPECT_EQ(formatElevationDirectionFirst(2.f * kUnitsPerMetre), "up 2.0 metres");
            EXPECT_EQ(formatElevationDirectionFirst(-3.f * kUnitsPerMetre), "down 3.0 metres");
        }

        TEST(MWAccessibilitySpokenFormat, formatElevationDirectionFirstRoundsLikeFormatElevation)
        {
            EXPECT_EQ(formatElevationDirectionFirst(12.f * kUnitsPerMetre), "up 12 metres");
            EXPECT_EQ(formatElevationDirectionFirst(-15.f * kUnitsPerMetre), "down 15 metres");
        }

        TEST(MWAccessibilitySpokenFormat, formatElevationDirectionFirstSharesTheDeadBand)
        {
            // Must stay silent exactly where formatElevation does, so a ladder
            // that barely rises never claims a direction it can't justify.
            EXPECT_EQ(formatElevationDirectionFirst(0.f), "");
            EXPECT_EQ(formatElevationDirectionFirst(52.5f), "");
            EXPECT_EQ(formatElevationDirectionFirst(-52.5f), "");
        }

        TEST(MWAccessibilitySpokenFormat, formatElevationDirectionFirstAgreesWithFormatElevation)
        {
            // The two phrasings must never disagree about the number: they
            // describe the same height to the same player moments apart.
            for (const float dz : { 60.f, -60.f, 200.f, -350.f, 1400.f, -2500.f })
            {
                const std::string plain = formatElevation(dz);
                const std::string swapped = formatElevationDirectionFirst(dz);
                ASSERT_FALSE(plain.empty());
                const std::size_t sep = plain.rfind(' ');
                ASSERT_NE(sep, std::string::npos);
                // "<number> metres <dir>" -> "<dir> <number> metres"
                EXPECT_EQ(swapped, plain.substr(sep + 1) + " " + plain.substr(0, sep));
            }
        }

        // --- letterForIndex --------------------------------------------------
        // Bijective base-26 (A=1): A..Z, then AA, AB, ... with no "A0" gap.

        TEST(MWAccessibilitySpokenFormat, letterForIndexFirstTwentySix)
        {
            EXPECT_EQ(letterForIndex(0), "A");
            EXPECT_EQ(letterForIndex(1), "B");
            EXPECT_EQ(letterForIndex(25), "Z");
        }

        TEST(MWAccessibilitySpokenFormat, letterForIndexWrapsToTwoLetters)
        {
            // 26 -> AA (NOT BA): bijective, so the carry has no zero digit.
            EXPECT_EQ(letterForIndex(26), "AA");
            EXPECT_EQ(letterForIndex(27), "AB");
            EXPECT_EQ(letterForIndex(51), "AZ");
            EXPECT_EQ(letterForIndex(52), "BA");
        }

        TEST(MWAccessibilitySpokenFormat, letterForIndexThreeLetterBoundary)
        {
            // 26 + 26*26 = 702 is the first three-letter suffix (AAA).
            EXPECT_EQ(letterForIndex(701), "ZZ");
            EXPECT_EQ(letterForIndex(702), "AAA");
        }

        // --- compassLabel ----------------------------------------------------
        // 0 = north (+Y), increasing toward east (+X); eight 45-degree sectors
        // centred on each point, so north spans [-22.5, +22.5) degrees. Helper
        // converts degrees to the radians the function expects.
        float deg(float d) { return d * kPi / 180.f; }

        TEST(MWAccessibilitySpokenFormat, compassLabelCardinalCentres)
        {
            EXPECT_STREQ(compassLabel(deg(0.f)), "north");
            EXPECT_STREQ(compassLabel(deg(90.f)), "east");
            EXPECT_STREQ(compassLabel(deg(180.f)), "south");
            EXPECT_STREQ(compassLabel(deg(270.f)), "west");
        }

        TEST(MWAccessibilitySpokenFormat, compassLabelIntercardinalCentres)
        {
            EXPECT_STREQ(compassLabel(deg(45.f)), "northeast");
            EXPECT_STREQ(compassLabel(deg(135.f)), "southeast");
            EXPECT_STREQ(compassLabel(deg(225.f)), "southwest");
            EXPECT_STREQ(compassLabel(deg(315.f)), "northwest");
        }

        TEST(MWAccessibilitySpokenFormat, compassLabelSectorBoundariesRoundToNextPoint)
        {
            // Just inside north's upper edge (<22.5) is still north; at/just past
            // it rolls into northeast.
            EXPECT_STREQ(compassLabel(deg(22.f)), "north");
            EXPECT_STREQ(compassLabel(deg(23.f)), "northeast");
        }

        TEST(MWAccessibilitySpokenFormat, compassLabelNormalisesNegativeAngles)
        {
            // -90 degrees == 270 == west; -45 == 315 == northwest.
            EXPECT_STREQ(compassLabel(deg(-90.f)), "west");
            EXPECT_STREQ(compassLabel(deg(-45.f)), "northwest");
        }

        TEST(MWAccessibilitySpokenFormat, compassLabelNormalisesAnglesAboveTwoPi)
        {
            // 360 wraps to north, 450 (== 90) to east.
            EXPECT_STREQ(compassLabel(deg(360.f)), "north");
            EXPECT_STREQ(compassLabel(deg(450.f)), "east");
        }

        // --- compassSector ---------------------------------------------------
        // Integer counterpart of compassLabel (0 = north .. 7 = northwest), used
        // by the scanner's direction filter to decide "same compass direction".
        TEST(MWAccessibilitySpokenFormat, compassSectorIndices)
        {
            EXPECT_EQ(compassSector(deg(0.f)), 0); // north
            EXPECT_EQ(compassSector(deg(45.f)), 1); // northeast
            EXPECT_EQ(compassSector(deg(90.f)), 2); // east
            EXPECT_EQ(compassSector(deg(135.f)), 3); // southeast
            EXPECT_EQ(compassSector(deg(180.f)), 4); // south
            EXPECT_EQ(compassSector(deg(225.f)), 5); // southwest
            EXPECT_EQ(compassSector(deg(270.f)), 6); // west
            EXPECT_EQ(compassSector(deg(315.f)), 7); // northwest
        }

        TEST(MWAccessibilitySpokenFormat, compassSectorNormalisesAndSnapsLikeLabel)
        {
            // Negative / >2pi angles fold in, same as compassLabel.
            EXPECT_EQ(compassSector(deg(-90.f)), 6); // west
            EXPECT_EQ(compassSector(deg(360.f)), 0); // north
            // Boundary parity with compassLabel: <22.5 stays north (0), at/just
            // past rolls into northeast (1).
            EXPECT_EQ(compassSector(deg(22.f)), 0);
            EXPECT_EQ(compassSector(deg(23.f)), 1);
        }

        TEST(MWAccessibilitySpokenFormat, compassSectorAgreesWithLabel)
        {
            // The two must never disagree -- the filter relies on this so the
            // kept set always matches the spoken bearing. Sweep all 360 degrees.
            static const char* kPoints[8]
                = { "north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest" };
            for (int d = 0; d < 360; ++d)
                EXPECT_STREQ(compassLabel(deg(static_cast<float>(d))), kPoints[compassSector(deg(static_cast<float>(d)))])
                    << "disagreement at " << d << " degrees";
        }

        // --- floorBand -------------------------------------------------------
        // Buckets a target's height offset into signed storey indices (0 = the
        // player's own level), rounding so half a storey either side is "level".
        TEST(MWAccessibilitySpokenFormat, floorBandOwnLevel)
        {
            EXPECT_EQ(floorBand(0.f), 0);
            EXPECT_EQ(floorBand(50.f), 0); // a few steps up is still this floor
            EXPECT_EQ(floorBand(-50.f), 0);
            // Just under half a storey stays on level; just over rolls to the next.
            EXPECT_EQ(floorBand(kFloorHeight * 0.49f), 0);
            EXPECT_EQ(floorBand(kFloorHeight * 0.51f), 1);
        }

        TEST(MWAccessibilitySpokenFormat, floorBandUpAndDown)
        {
            EXPECT_EQ(floorBand(kFloorHeight), 1);
            EXPECT_EQ(floorBand(2.f * kFloorHeight), 2);
            EXPECT_EQ(floorBand(-kFloorHeight), -1);
            EXPECT_EQ(floorBand(-2.f * kFloorHeight), -2);
        }

        // --- lessByLevelThenDistance -----------------------------------------
        // Groups by level (own first, then nearest storey outward), nearest-first
        // within a level. dz = target.z - player.z; horiz is SQUARED horizontal
        // distance.
        TEST(MWAccessibilitySpokenFormat, levelSortSameLevelIsNearestFirst)
        {
            // Both on the player's floor: the closer one wins regardless of a
            // small height wobble.
            EXPECT_TRUE(lessByLevelThenDistance(10.f, 100.f, -10.f, 400.f));
            EXPECT_FALSE(lessByLevelThenDistance(-10.f, 400.f, 10.f, 100.f));
        }

        TEST(MWAccessibilitySpokenFormat, levelSortOwnLevelBeatsCloserOtherFloor)
        {
            // A far object on the player's OWN floor still precedes a horizontally
            // closer one a storey up -- the whole point: sweep this floor first.
            const float farSameFloor = 5000.f * 5000.f;
            const float nearNextFloor = 100.f * 100.f;
            EXPECT_TRUE(lessByLevelThenDistance(0.f, farSameFloor, kFloorHeight, nearNextFloor));
            EXPECT_FALSE(lessByLevelThenDistance(kFloorHeight, nearNextFloor, 0.f, farSameFloor));
        }

        TEST(MWAccessibilitySpokenFormat, levelSortNearerFloorBeatsFartherFloor)
        {
            // One storey up precedes two storeys up (nearest level outward).
            EXPECT_TRUE(lessByLevelThenDistance(kFloorHeight, 400.f, 2.f * kFloorHeight, 100.f));
            // One down also precedes two up (|1| < |2|).
            EXPECT_TRUE(lessByLevelThenDistance(-kFloorHeight, 400.f, 2.f * kFloorHeight, 100.f));
        }

        TEST(MWAccessibilitySpokenFormat, levelSortSameDistanceUpVsDownIsDeterministic)
        {
            // One up vs one down (same rank, opposite sign): lower sorts first,
            // and the order is strict (never reports a < b AND b < a).
            const bool ab = lessByLevelThenDistance(-kFloorHeight, 100.f, kFloorHeight, 100.f);
            const bool ba = lessByLevelThenDistance(kFloorHeight, 100.f, -kFloorHeight, 100.f);
            EXPECT_TRUE(ab); // the lower (down) one comes first
            EXPECT_FALSE(ba);
        }

        // --- lessByReachThenLevelThenDistance --------------------------------
        // Reachable-first override. THE REGRESSION THIS FIXES (reported in a
        // mod-overhauled dungeon): a door raised ~2 m sits in band +1, so plain
        // level sorting pushed it behind every object on the player's own floor
        // even though the player was standing close enough to open it.

        TEST(MWAccessibilitySpokenFormat, reachSortRaisedDoorWithinReachComesFirst)
        {
            // The real case: a door 2 m up (dz 140 units => band +1) about a
            // metre away, versus a crate on the player's own floor 10 m off.
            // Plain level sorting puts the crate first; reach-aware does not.
            const float raisedDz = 140.f; // ~2 m up: past the 115-unit band flip
            const float doorHoriz2 = 70.f * 70.f; // ~1 m away, within reach
            const float crateHoriz2 = 700.f * 700.f; // ~10 m away, same floor

            // Confirm the OLD behaviour really was wrong (guards against the
            // premise silently changing if kFloorHeight is ever retuned).
            EXPECT_FALSE(lessByLevelThenDistance(raisedDz, doorHoriz2, 0.f, crateHoriz2));

            // Reach-aware: the door the player can actually touch comes first.
            EXPECT_TRUE(lessByReachThenLevelThenDistance(true, raisedDz, doorHoriz2, false, 0.f, crateHoriz2));
            EXPECT_FALSE(lessByReachThenLevelThenDistance(false, 0.f, crateHoriz2, true, raisedDz, doorHoriz2));
        }

        TEST(MWAccessibilitySpokenFormat, reachSortPreservesLevelGroupingBeyondReach)
        {
            // Neither reachable => unchanged guild-hall behaviour: a far object
            // on the player's own floor still precedes a closer one upstairs.
            const float farSameFloor = 5000.f * 5000.f;
            const float nearNextFloor = 100.f * 100.f;
            EXPECT_TRUE(lessByReachThenLevelThenDistance(false, 0.f, farSameFloor, false, kFloorHeight, nearNextFloor));
            EXPECT_FALSE(
                lessByReachThenLevelThenDistance(false, kFloorHeight, nearNextFloor, false, 0.f, farSameFloor));
        }

        TEST(MWAccessibilitySpokenFormat, reachSortBothReachableFallsBackToLevelThenDistance)
        {
            // Two reachable objects tie on the override, so the usual ordering
            // decides: same level beats another band, then nearest-first.
            EXPECT_TRUE(lessByReachThenLevelThenDistance(true, 0.f, 400.f, true, kFloorHeight, 100.f));
            EXPECT_TRUE(lessByReachThenLevelThenDistance(true, 0.f, 100.f, true, 0.f, 400.f));
            EXPECT_FALSE(lessByReachThenLevelThenDistance(true, 0.f, 400.f, true, 0.f, 100.f));
        }

        TEST(MWAccessibilitySpokenFormat, reachSortIsStrictWeakOrdering)
        {
            // Irreflexive, and never reports both a<b and b<a for any mix of
            // reachability -- required of a std::sort comparator.
            EXPECT_FALSE(lessByReachThenLevelThenDistance(true, 0.f, 100.f, true, 0.f, 100.f));
            EXPECT_FALSE(lessByReachThenLevelThenDistance(false, 0.f, 100.f, false, 0.f, 100.f));

            const bool ab = lessByReachThenLevelThenDistance(true, 140.f, 100.f, false, 0.f, 100.f);
            const bool ba = lessByReachThenLevelThenDistance(false, 0.f, 100.f, true, 140.f, 100.f);
            EXPECT_TRUE(ab);
            EXPECT_FALSE(ba);
        }
    }
}
