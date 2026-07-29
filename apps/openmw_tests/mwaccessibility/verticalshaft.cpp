#include <gtest/gtest.h>

#include <apps/openmw/mwaccessibility/verticalshaft.hpp>

namespace
{
    using namespace MWAccessibility;

    TEST(MWAccessibilityVerticalShaft, ClassifiesTelvanniKitPieces)
    {
        EXPECT_EQ(classifyShaftPiece("in_t_s_shaft_6way"), ShaftPieceKind::Opening);
        EXPECT_EQ(classifyShaftPiece("in_t_s_shaft_vconnect"), ShaftPieceKind::Opening);
        EXPECT_EQ(classifyShaftPiece("in_t_s_shaft_01"), ShaftPieceKind::Segment);
        EXPECT_EQ(classifyShaftPiece("in_t_s_shaft_elbow_01"), ShaftPieceKind::Segment);
        // A cap closes the shaft; it must not be mistaken for a way through even
        // though its name also contains "hallshaft".
        EXPECT_EQ(classifyShaftPiece("in_t_s_hallshaft_cap"), ShaftPieceKind::Cap);
    }

    TEST(MWAccessibilityVerticalShaft, IgnoresUnrelatedArchitecture)
    {
        EXPECT_EQ(classifyShaftPiece("in_t_stairs_wiz_02"), ShaftPieceKind::NotShaft);
        EXPECT_EQ(classifyShaftPiece("in_t_edge_01"), ShaftPieceKind::NotShaft);
        EXPECT_EQ(classifyShaftPiece("furn_de_rope_05"), ShaftPieceKind::NotShaft);
        EXPECT_EQ(classifyShaftPiece(""), ShaftPieceKind::NotShaft);
    }

    TEST(MWAccessibilityVerticalShaft, ClassificationIsCaseInsensitive)
    {
        EXPECT_EQ(classifyShaftPiece("IN_T_S_Shaft_6Way"), ShaftPieceKind::Opening);
        EXPECT_EQ(classifyShaftPiece("In_T_S_HallShaft_Cap"), ShaftPieceKind::Cap);
    }

    // The real architecture of "Tel Uvirith, Tower Upper" (Uvirith's Legacy
    // 3.53), read from the plugin. This is the cell where auto-walk failed to
    // cross floors, so it is the case worth pinning down: the shaft runs up
    // x=256, y=0 with floor openings at z = 0, 512 and 1024, capped at 1280.
    std::vector<ShaftPiece> telUvirithUpperTower()
    {
        return {
            { "in_t_s_shaft_vconnect", { 256.f, 0.f, 0.f } },
            { "in_t_s_shaft_6way", { 256.f, 0.f, 0.f } },
            { "in_t_s_shaft_01", { 256.f, 0.f, 256.f } },
            { "in_t_s_shaft_6way", { 256.f, 0.f, 512.f } },
            { "in_t_s_shaft_vconnect", { 256.f, 0.f, 512.f } },
            { "in_t_s_shaft_6way", { 256.f, 0.f, 1024.f } },
            { "in_t_s_hallshaft_cap", { 256.f, 0.f, 1280.f } },
            // Decoys from the same cell that must not join the column.
            { "in_t_stairs_wiz_01", { 748.f, -24.f, 240.f } },
            { "in_t_manor_stairs_01", { 1645.f, -23.f, 1023.f } },
        };
    }

    TEST(MWAccessibilityVerticalShaft, FindsTheTelUvirithLevitationShaft)
    {
        const std::vector<VerticalShaft> shafts = detectShafts(telUvirithUpperTower());
        ASSERT_EQ(shafts.size(), 1u);

        const VerticalShaft& s = shafts.front();
        EXPECT_NEAR(s.mX, 256.f, 1.f);
        EXPECT_NEAR(s.mY, 0.f, 1.f);
        EXPECT_NEAR(s.mBottom, 0.f, 1.f);
        EXPECT_NEAR(s.mTop, 1280.f, 1.f);

        // Floor openings at 0, 512, 1024 -- co-located kit pieces collapse to one
        // opening per floor, and the cap at 1280 is not an opening.
        ASSERT_EQ(s.mOpenings.size(), 3u);
        EXPECT_NEAR(s.mOpenings[0], 0.f, 1.f);
        EXPECT_NEAR(s.mOpenings[1], 512.f, 1.f);
        EXPECT_NEAR(s.mOpenings[2], 1024.f, 1.f);
    }

    TEST(MWAccessibilityVerticalShaft, SeparatesDistinctColumns)
    {
        // Tel Uvirith has a second run of shaft pieces on the x=0 axis; those must
        // not be merged into the x=256 column.
        std::vector<ShaftPiece> pieces = telUvirithUpperTower();
        pieces.push_back({ "in_t_s_shaft_6way", { 0.f, 0.f, 0.f } });
        pieces.push_back({ "in_t_s_hallshaft_cap", { 0.f, 0.f, 0.f } });
        pieces.push_back({ "in_t_s_shaft_elbow_01", { 0.f, 0.f, 0.f } });

        const std::vector<VerticalShaft> shafts = detectShafts(pieces);
        ASSERT_EQ(shafts.size(), 2u);
        // Sorted by piece count, so the main 7-piece column comes first.
        EXPECT_NEAR(shafts[0].mX, 256.f, 1.f);
        EXPECT_NEAR(shafts[1].mX, 0.f, 1.f);
    }

    TEST(MWAccessibilityVerticalShaft, ToleratesSlightlyOffAxisPlacement)
    {
        // Mod-placed pieces are often nudged a few units; they should still form
        // one column rather than several one-piece fragments.
        const std::vector<ShaftPiece> pieces = {
            { "in_t_s_shaft_6way", { 254.9f, 12.1f, 0.f } },
            { "in_t_s_shaft_01", { 257.2f, -3.4f, 256.f } },
            { "in_t_s_shaft_6way", { 255.1f, 2.8f, 512.f } },
        };
        const std::vector<VerticalShaft> shafts = detectShafts(pieces);
        ASSERT_EQ(shafts.size(), 1u);
        EXPECT_EQ(shafts.front().mPieceCount, 3);
    }

    TEST(MWAccessibilityVerticalShaft, IgnoresLoneIncidentalPieces)
    {
        // A single shaft piece somewhere is not a usable shaft.
        const std::vector<ShaftPiece> pieces = { { "in_t_s_shaft_01", { 900.f, 900.f, 100.f } } };
        EXPECT_TRUE(detectShafts(pieces).empty());
    }

    TEST(MWAccessibilityVerticalShaft, PicksShaftSpanningTheJourney)
    {
        const std::vector<VerticalShaft> shafts = detectShafts(telUvirithUpperTower());

        // The failing case: descending from the throne room (z~1290) to the
        // lower-tower door (z~197). The pathgrid had no route here at all; the
        // architecture does.
        const VerticalShaft* s = bestShaftForTravel(shafts, -122.f, -136.f, 1289.f, 197.f);
        ASSERT_NE(s, nullptr);
        EXPECT_NEAR(s->mX, 256.f, 1.f);

        // Leaving the shaft for a target at z=197 should use the z=0 opening
        // rather than the 512 one.
        EXPECT_NEAR(nearestOpening(*s, 197.f), 0.f, 1.f);
        EXPECT_NEAR(nearestOpening(*s, 900.f), 1024.f, 1.f);
    }

    TEST(MWAccessibilityVerticalShaft, RejectsShaftThatDoesNotReachTheDestination)
    {
        const std::vector<VerticalShaft> shafts = detectShafts(telUvirithUpperTower());
        // Far above the capped top of the shaft (1280) and beyond the slack.
        EXPECT_EQ(bestShaftForTravel(shafts, 0.f, 0.f, 1900.f, 1850.f), nullptr);
        // Far below the bottom.
        EXPECT_EQ(bestShaftForTravel(shafts, 0.f, 0.f, -900.f, -800.f), nullptr);
    }

    TEST(MWAccessibilityVerticalShaft, NearestOpeningFallsBackToRequestedHeight)
    {
        VerticalShaft s;
        s.mX = 0.f;
        s.mY = 0.f;
        s.mBottom = 0.f;
        s.mTop = 500.f;
        // No openings recorded: be honest and return the asked-for height rather
        // than inventing a floor.
        EXPECT_NEAR(nearestOpening(s, 321.f), 321.f, 0.01f);
    }

    // Tower of Tel Fyr, Hall of Fyr -- the REAL refs from Morrowind.esm.
    //
    // Unlike Tel Uvirith this is a vanilla tower, and its shaft is short: the
    // column spans z 14320..14832, only 512 units (~7 m). Note also that four of
    // the five hallshaft_caps sit 256 units OFF the axis, on the four compass
    // points -- they cap the horizontal halls that meet the shaft, not the column.
    static std::vector<ShaftPiece> telFyrHallOfFyr()
    {
        return {
            { "in_t_s_hallshaft_cap", { 3904.f, 3920.f, 14832.f } }, // hall cap, off-axis
            { "in_t_s_shaft_01", { 4160.f, 3920.f, 14576.f } },
            { "in_t_s_shaft_vconnect", { 4160.f, 3920.f, 14320.f } },
            { "in_t_s_shaft_6way", { 4160.f, 3920.f, 14832.f } },
            { "in_t_s_hallshaft_cap", { 4416.f, 3920.f, 14832.f } }, // hall cap, off-axis
            { "in_t_s_hallshaft_cap", { 4160.f, 3664.f, 14832.f } }, // hall cap, off-axis
            { "in_t_s_hallshaft_cap", { 4160.f, 4176.f, 14832.f } }, // hall cap, off-axis
            { "in_t_s_hallshaft_cap", { 4160.f, 3920.f, 14832.f } }, // on-axis cap
        };
    }

    TEST(MWAccessibilityVerticalShaft, TelFyrShaftIsFoundOnItsTrueAxis)
    {
        const std::vector<VerticalShaft> shafts = detectShafts(telFyrHallOfFyr());
        ASSERT_FALSE(shafts.empty());
        const VerticalShaft& s = shafts.front();
        // The off-axis hall caps must NOT drag the axis away from the real column,
        // or the descent steers at a spot beside the shaft and lands on the floor.
        EXPECT_NEAR(s.mX, 4160.f, 1.f);
        EXPECT_NEAR(s.mY, 3920.f, 1.f);
    }

    TEST(MWAccessibilityVerticalShaft, TelFyrShaftDoesNotClaimTheWholeTower)
    {
        // The detected span is only what the pieces cover (512u). A walk from the
        // Hall down to a target far below is NOT served by this shaft, and saying
        // otherwise would send the descent to the column and then fail there --
        // worse than never having claimed it, because the old ring probe (which
        // would have found the actual nearby opening) never gets a turn.
        const std::vector<VerticalShaft> shafts = detectShafts(telFyrHallOfFyr());
        EXPECT_EQ(bestShaftForTravel(shafts, 4160.f, 3920.f, 14832.f, 12000.f), nullptr);
    }

    // --- Obstructions in the column (e.g. the stronghold's elevator platform) ---

    TEST(MWAccessibilityVerticalShaft, PlatformParkedMidShaftBlocksDescent)
    {
        // Descending 1289 -> 197 with the elevator plug parked at its "second
        // floor" rest height of 1000: squarely in the way.
        EXPECT_TRUE(obstructionBlocksJourney(1000.f, 1289.f, 197.f));
    }

    TEST(MWAccessibilityVerticalShaft, FloorAtTheDestinationIsNotABlockage)
    {
        // The ray is bound to stop on the ground we intend to land on. That is
        // arrival, not obstruction, or every descent would report itself blocked.
        EXPECT_FALSE(obstructionBlocksJourney(197.f, 1289.f, 197.f));
        EXPECT_FALSE(obstructionBlocksJourney(180.f, 1289.f, 197.f));
    }

    TEST(MWAccessibilityVerticalShaft, PlatformUnderfootBlocksRatherThanCountingAsArrival)
    {
        // Standing ON the plug at 490 and trying to reach the basement: the thing
        // under our feet is the obstruction. The starting end gets no slack, or
        // this case -- the one he can actually reproduce with the elevator -- would
        // be silently excused.
        EXPECT_TRUE(obstructionBlocksJourney(488.f, 490.f, -20.f));
    }

    TEST(MWAccessibilityVerticalShaft, ObstructionOutsideTheJourneyIsIgnored)
    {
        // The shaft cap far above us has no bearing on a descent.
        EXPECT_FALSE(obstructionBlocksJourney(1280.f, 600.f, 197.f));
        // Nor does something below the destination.
        EXPECT_FALSE(obstructionBlocksJourney(-20.f, 600.f, 197.f));
    }

    TEST(MWAccessibilityVerticalShaft, BlockageDetectionWorksWhenAscending)
    {
        // Same reasoning mirrored: a platform above us blocks a climb, while the
        // floor of the level we're rising into is the destination, not a blockage.
        EXPECT_TRUE(obstructionBlocksJourney(1000.f, 197.f, 1289.f));
        EXPECT_FALSE(obstructionBlocksJourney(1289.f, 197.f, 1289.f));
    }
}
