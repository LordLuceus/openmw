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
    }
}
