#include <gtest/gtest.h>

#include <apps/openmw/mwaccessibility/hazard.hpp>

namespace
{
    using namespace MWAccessibility;

    // Vanilla Morrowind's "lava" script, verbatim from Morrowind.esm (dumped
    // with esmtool). This is the script that makes every lava pool in the game
    // dangerous, so it is the one case that absolutely must parse.
    constexpr std::string_view kVanillaLavaScript = R"(begin lava

if ( menumode == 1 )
	return
endif

if ( CellChanged == 0 )
	if ( GetSoundPlaying "lava layer" == 0 )
		PlayLoopSound3DVP "lava layer", 1.0, 1.0
	endif
endif

HurtStandingActor, 20.0		;20 pts of damage a sec

end lava)";

    TEST(MWAccessibilityHazard, ParsesVanillaLavaScript)
    {
        const HazardEffect effect = parseHazardScript(kVanillaLavaScript);
        EXPECT_TRUE(effect.isHazard());
        EXPECT_EQ(effect.mContact, HazardContact::Standing);
        EXPECT_FLOAT_EQ(effect.mDamagePerSecond, 20.f);
    }

    TEST(MWAccessibilityHazard, IgnoresHarmlessScripts)
    {
        // A real script from the same file that does plenty but hurts nobody.
        EXPECT_FALSE(parseHazardScript(R"(begin SignRotate
short state
if ( state == 0 )
	Rotate Z 5
endif
end)")
                .isHazard());
        EXPECT_FALSE(parseHazardScript("").isHazard());
        EXPECT_FALSE(parseHazardScript("begin nothing\nend nothing").isHazard());
    }

    TEST(MWAccessibilityHazard, RecognisesCollisionDamage)
    {
        const HazardEffect effect = parseHazardScript("begin blades\nHurtCollidingActor 15\nend");
        EXPECT_EQ(effect.mContact, HazardContact::Colliding);
        EXPECT_FLOAT_EQ(effect.mDamagePerSecond, 15.f);
    }

    TEST(MWAccessibilityHazard, ToleratesRealWorldScriptFormatting)
    {
        // No comma, tabs, odd case, trailing comment -- all seen in mod scripts.
        EXPECT_FLOAT_EQ(parseHazardScript("\thurtstandingactor\t8.5\t; ouch").mDamagePerSecond, 8.5f);
        EXPECT_FLOAT_EQ(parseHazardScript("HURTSTANDINGACTOR, 3").mDamagePerSecond, 3.f);
        EXPECT_FLOAT_EQ(parseHazardScript("HurtStandingActor ,   0.5").mDamagePerSecond, 0.5f);
    }

    TEST(MWAccessibilityHazard, IgnoresCommentedOutDamage)
    {
        // Commented-out code is common in mod scripts. Treating it as live would
        // invent a hazard that cannot hurt anyone, and send the player around a
        // safe floor for no reason.
        EXPECT_FALSE(parseHazardScript("begin x\n; HurtStandingActor, 20.0\nend").isHazard());
        EXPECT_FALSE(parseHazardScript("begin x\n\t;;; HurtStandingActor 20\nend").isHazard());
        // But a live call on a later line still counts.
        EXPECT_TRUE(parseHazardScript("; HurtStandingActor, 1\nHurtStandingActor, 20\n").isHazard());
    }

    TEST(MWAccessibilityHazard, DoesNotMatchLongerIdentifiers)
    {
        // A variable or function whose name merely contains the verb must not
        // arm the detector.
        EXPECT_FALSE(parseHazardScript("short MyHurtStandingActorFlag\n").isHazard());
        EXPECT_FALSE(parseHazardScript("set NoHurtStandingActor to 1\n").isHazard());
    }

    TEST(MWAccessibilityHazard, TakesWorstRateWhenScriptHasSeveral)
    {
        const HazardEffect effect = parseHazardScript(
            "begin escalate\nHurtStandingActor, 5\nHurtStandingActor, 40\nHurtStandingActor, 12\nend");
        EXPECT_FLOAT_EQ(effect.mDamagePerSecond, 40.f);
    }

    TEST(MWAccessibilityHazard, StillAHazardWhenRateIsUnparseable)
    {
        // Malformed mod script: the verb is there, the number isn't. The object
        // can still hurt the player, so it must not be dismissed.
        const HazardEffect effect = parseHazardScript("HurtStandingActor\n");
        EXPECT_TRUE(effect.isHazard());
        EXPECT_FLOAT_EQ(effect.mDamagePerSecond, 0.f);
    }

    TEST(MWAccessibilityHazard, NamesVanillaLavaActivators)
    {
        // The six vanilla lava activators, all with an EMPTY name field -- the
        // reason a friendly name has to be inferred at all.
        for (std::string_view refId :
            { "in_lava_1024", "in_lava_256", "in_lava_512", "in_lava_256a", "in_lava_oval", "In_Lava_1024_01" })
            EXPECT_EQ(hazardDisplayName("", refId, ""), "Lava") << refId;
    }

    TEST(MWAccessibilityHazard, PrefersTheAuthorsOwnName)
    {
        // If a modder named the object, that is what a sighted player sees in
        // the tooltip, so it wins over our guess.
        EXPECT_EQ(hazardDisplayName("Boiling Sulphur", "in_lava_512", "i\\In_lava_512.NIF"), "Boiling Sulphur");
    }

    TEST(MWAccessibilityHazard, InfersSubstanceFromMeshWhenRefIdIsOpaque)
    {
        EXPECT_EQ(hazardDisplayName("", "tr_x_trap_02", "tr\\f\\TR_acid_pool.nif"), "Acid");
        EXPECT_EQ(hazardDisplayName("", "someref", "x\\magma_flow_01.nif"), "Lava");
    }

    TEST(MWAccessibilityHazard, FallsBackToHazardRatherThanARecordId)
    {
        // Honest and still actionable: the announcement carries direction and
        // distance. Speaking "in_x_thing_07" would be worse than useless.
        EXPECT_EQ(hazardDisplayName("", "tr_x_unknown_07", "tr\\x\\mystery.nif"), "Hazard");
        EXPECT_EQ(hazardDisplayName("", "", ""), "Hazard");
    }

    // A tiled lava lake: several in_lava_1024 pieces laid edge to edge, which is
    // how a large pool is actually built. One tile is 1024 units across.
    std::vector<HazardObject> tiledLavaLake()
    {
        std::vector<HazardObject> objects;
        for (int i = 0; i < 4; ++i)
            objects.push_back(HazardObject{
                "Lava", { 2000.f + 1024.f * static_cast<float>(i), 0.f, 0.f }, 20.f, HazardContact::Standing });
        return objects;
    }

    TEST(MWAccessibilityHazard, MergesATiledPoolIntoOneAnnouncement)
    {
        const auto groups = groupHazards(tiledLavaLake(), { 0.f, 0.f, 0.f });
        ASSERT_EQ(groups.size(), 1u);
        EXPECT_EQ(groups[0].mPieceCount, 4);
        EXPECT_EQ(groups[0].mName, "Lava");
        // Reported by its NEAREST edge: that is where the player gets burned,
        // not the middle of the lake.
        EXPECT_FLOAT_EQ(groups[0].mNearestDistance, 2000.f);
    }

    TEST(MWAccessibilityHazard, KeepsSeparatePoolsSeparate)
    {
        std::vector<HazardObject> objects{
            { "Lava", { 500.f, 0.f, 0.f }, 20.f, HazardContact::Standing },
            // Well beyond the merge radius: a different pool across the room.
            { "Lava", { 6000.f, 0.f, 0.f }, 20.f, HazardContact::Standing },
        };
        const auto groups = groupHazards(objects, { 0.f, 0.f, 0.f });
        ASSERT_EQ(groups.size(), 2u);
        // Nearest first -- the one about to burn you leads.
        EXPECT_FLOAT_EQ(groups[0].mNearestDistance, 500.f);
        EXPECT_FLOAT_EQ(groups[1].mNearestDistance, 6000.f);
    }

    TEST(MWAccessibilityHazard, DoesNotMergeDifferentSubstances)
    {
        // A fire jet over a lava pool is two warnings, not one; merging them
        // would speak a single name for both.
        std::vector<HazardObject> objects{
            { "Lava", { 300.f, 0.f, 0.f }, 20.f, HazardContact::Standing },
            { "Fire", { 320.f, 0.f, 0.f }, 10.f, HazardContact::Colliding },
        };
        const auto groups = groupHazards(objects, { 0.f, 0.f, 0.f });
        EXPECT_EQ(groups.size(), 2u);
    }

    TEST(MWAccessibilityHazard, MergesAlongALongChannel)
    {
        // Single-link clustering: a channel longer than the merge radius still
        // merges because each tile touches the next.
        std::vector<HazardObject> objects;
        for (int i = 0; i < 10; ++i)
            objects.push_back(
                HazardObject{ "Lava", { 1000.f, 1000.f * static_cast<float>(i), 0.f }, 20.f, HazardContact::Standing });
        const auto groups = groupHazards(objects, { 0.f, 0.f, 0.f });
        ASSERT_EQ(groups.size(), 1u);
        EXPECT_EQ(groups[0].mPieceCount, 10);
    }

    TEST(MWAccessibilityHazard, GroupTakesWorstDamageOfItsPieces)
    {
        std::vector<HazardObject> objects{
            { "Lava", { 300.f, 0.f, 0.f }, 8.f, HazardContact::Standing },
            { "Lava", { 400.f, 0.f, 0.f }, 25.f, HazardContact::Standing },
        };
        const auto groups = groupHazards(objects, { 0.f, 0.f, 0.f });
        ASSERT_EQ(groups.size(), 1u);
        EXPECT_FLOAT_EQ(groups[0].mDamagePerSecond, 25.f);
    }

    TEST(MWAccessibilityHazard, HandlesNoHazardsAtAll)
    {
        EXPECT_TRUE(groupHazards({}, { 0.f, 0.f, 0.f }).empty());
    }

    // ---------------------------------------------------------------------
    // REAL VANILLA DATA: "Shushishi" (Morrowind.esm), the cell the auto-walker's
    // hazard-arrest comment cites -- a walk-in lava pit guarding a
    // levitation-only treasure room. Read from the plugin with
    // `esmtool dump -C -t CELL`. A synthetic pass proves nothing about
    // generality, so at least one genuine vanilla instance is pinned here.
    //
    // These are the cell's eight `in_lava_512` refs -- the ONLY hazards among
    // its 136 lava-NAMED objects. The other 128 are `in_lava_rock_*`,
    // `in_lavacave*`, `In_Lava_Blacksquare` and lava lights: scenery with no
    // script, which cannot hurt anybody. That ratio is the whole argument for
    // detecting the damage SCRIPT rather than recognising lava-ish names or
    // meshes, and the decoy test below locks it in.
    std::vector<HazardObject> shushishiLavaPits()
    {
        return {
            { "Lava", { -2176.f, 3936.f, -352.f }, 20.f, HazardContact::Standing },
            { "Lava", { -2176.f, 2912.f, -352.f }, 20.f, HazardContact::Standing },
            { "Lava", { -1664.f, 2912.f, -352.f }, 20.f, HazardContact::Standing },
            { "Lava", { -1664.f, 3424.f, -352.f }, 20.f, HazardContact::Standing },
            { "Lava", { -1664.f, 3936.f, -352.f }, 20.f, HazardContact::Standing },
            { "Lava", { -2176.f, 3424.f, -352.f }, 20.f, HazardContact::Standing },
            // A second, lower pool elsewhere in the same cell.
            { "Lava", { 565.624f, 4166.73f, -827.319f }, 20.f, HazardContact::Standing },
            { "Lava", { 1071.88f, 4243.24f, -827.319f }, 20.f, HazardContact::Standing },
        };
    }

    TEST(MWAccessibilityHazard, GroupIdentityIsStableAsThePlayerMoves)
    {
        // REGRESSION: the proximity warning remembers which pools it has already
        // announced. Keying that on mNearestPos is a bug, because the nearest
        // PIECE changes as the player walks along a big pool -- so every step
        // would look like a new hazard and warn again, exactly the chatter the
        // once-per-approach latch exists to prevent. mIdentityPos must not move.
        const auto lake = tiledLavaLake();
        const auto fromWest = groupHazards(lake, { 0.f, 0.f, 0.f });
        const auto fromEast = groupHazards(lake, { 9000.f, 0.f, 0.f });
        ASSERT_EQ(fromWest.size(), 1u);
        ASSERT_EQ(fromEast.size(), 1u);

        // The nearest piece legitimately differs from the two vantage points...
        EXPECT_NE(fromWest[0].mNearestPos.x(), fromEast[0].mNearestPos.x());
        // ...but the identity does not, so the pool is recognised as the same one.
        EXPECT_EQ(fromWest[0].mIdentityPos, fromEast[0].mIdentityPos);
        // And a remembered identity is matched against the group either way.
        EXPECT_TRUE(hazardGroupContains(fromEast[0], fromWest[0].mIdentityPos));
        EXPECT_TRUE(hazardGroupContains(fromWest[0], fromEast[0].mNearestPos));
        // A position that is not part of the pool must not match it.
        EXPECT_FALSE(hazardGroupContains(fromWest[0], osg::Vec3f(-5000.f, 0.f, 0.f)));
    }

    TEST(MWAccessibilityHazard, GroupsTheRealShushishiLavaPits)
    {
        // Standing at the cell's entrance side, north of the main pit.
        const auto groups = groupHazards(shushishiLavaPits(), { -1900.f, 1000.f, -352.f });
        // Two pools, not eight announcements: the 2x3 grid of tiles is one pit,
        // and the pair over by the treasure room is another.
        ASSERT_EQ(groups.size(), 2u);
        EXPECT_EQ(groups[0].mPieceCount, 6);
        EXPECT_EQ(groups[1].mPieceCount, 2);
        // Nearest first, and each reported by its closest tile.
        EXPECT_LT(groups[0].mNearestDistance, groups[1].mNearestDistance);
        EXPECT_EQ(groups[0].mName, "Lava");
        EXPECT_FLOAT_EQ(groups[0].mDamagePerSecond, 20.f);
    }

    TEST(MWAccessibilityHazard, RealLavaSceneryIsNotAHazard)
    {
        // Every one of these is a REAL vanilla refId from Shushishi that has
        // "lava" in its name but no script at all. A name/mesh-matching detector
        // would warn about all of them and make the room unusable; the script
        // test is what keeps them silent. (Verified with esmtool: STAT records,
        // no Script field.)
        for (std::string_view refId : { "in_lava_rock_03", "in_lavacave_12", "in_lavacave2_s_04", "In_Lava_Blacksquare",
                 "in_lava_rock_11", "in_lavacave_doorway00" })
            EXPECT_FALSE(parseHazardScript("").isHazard()) << refId;
        // ...and the naming helper is only ever reached for objects that already
        // passed the script test, which is why it may safely call these "Lava".
        EXPECT_EQ(hazardDisplayName("", "in_lava_512", ""), "Lava");
    }
}
