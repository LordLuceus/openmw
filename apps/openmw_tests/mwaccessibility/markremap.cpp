#include <gtest/gtest.h>

#include <apps/openmw/mwaccessibility/markremap.hpp>

namespace
{
    using namespace MWAccessibility;

    TEST(MWAccessibilityMarkRemap, FindsPluginCaseInsensitively)
    {
        const std::vector<std::string> files = { "Morrowind.esm", "Uvirith's Legacy_3.53.esp" };
        EXPECT_EQ(findContentFileIndex("Morrowind.esm", files), 0);
        EXPECT_EQ(findContentFileIndex("uvirith's legacy_3.53.ESP", files), 1);
        EXPECT_EQ(findContentFileIndex("NotLoaded.esp", files), -1);
    }

    // The real failure this fix exists for: ten mods were added ahead of
    // Uvirith's Legacy, moving it from index 43 to 54, and every strongroom
    // mark silently pointed at whatever now sat in its old slot.
    TEST(MWAccessibilityMarkRemap, RemapsPluginThatMovedLaterInLoadOrder)
    {
        const std::map<int32_t, std::string> saved = { { 43, "Uvirith's Legacy_3.53.esp" } };
        std::vector<std::string> now(55);
        now[54] = "Uvirith's Legacy_3.53.esp";
        now[43] = "Some Unrelated Mod.esp";

        int32_t out = -999;
        EXPECT_EQ(remapContentFileIndex(43, saved, now, out), RemapResult::Remapped);
        EXPECT_EQ(out, 54);
    }

    // The mirror case: removing a plugin shifts indices DOWN. This is what
    // happened the first time, when the switch plugin was uninstalled.
    TEST(MWAccessibilityMarkRemap, RemapsPluginThatMovedEarlierInLoadOrder)
    {
        const std::map<int32_t, std::string> saved = { { 55, "Uvirith's Legacy_3.53.esp" } };
        std::vector<std::string> now(55);
        now[54] = "Uvirith's Legacy_3.53.esp";

        int32_t out = -999;
        EXPECT_EQ(remapContentFileIndex(55, saved, now, out), RemapResult::Remapped);
        EXPECT_EQ(out, 54);
    }

    TEST(MWAccessibilityMarkRemap, LeavesIndexAloneWhenLoadOrderUnchanged)
    {
        const std::map<int32_t, std::string> saved = { { 1, "Tribunal.esm" } };
        const std::vector<std::string> now = { "Morrowind.esm", "Tribunal.esm" };

        int32_t out = -999;
        EXPECT_EQ(remapContentFileIndex(1, saved, now, out), RemapResult::Unchanged);
        EXPECT_EQ(out, 1);
    }

    // A mark whose plugin is gone must be dropped, NOT kept: index 43 now holds
    // an unrelated mod, so keeping it would label a random object.
    TEST(MWAccessibilityMarkRemap, OrphansMarkWhosePluginIsNoLongerLoaded)
    {
        const std::map<int32_t, std::string> saved = { { 43, "Uninstalled Mod.esp" } };
        const std::vector<std::string> now = { "Morrowind.esm", "Something Else.esp" };

        int32_t out = -999;
        EXPECT_EQ(remapContentFileIndex(43, saved, now, out), RemapResult::Orphaned);
    }

    // Sidecars written before the manifest existed have no names to match on.
    // Guessing would be worse than doing nothing, so such marks pass through.
    TEST(MWAccessibilityMarkRemap, LeavesPreManifestMarksUntouched)
    {
        const std::map<int32_t, std::string> saved; // empty: v1/v2 sidecar
        const std::vector<std::string> now = { "Morrowind.esm", "Tribunal.esm" };

        int32_t out = -999;
        EXPECT_EQ(remapContentFileIndex(7, saved, now, out), RemapResult::Unchanged);
        EXPECT_EQ(out, 7);
    }

    // Base-game refs (contentFile 0) must survive untouched -- Morrowind.esm is
    // always first, and these are the majority of marks.
    TEST(MWAccessibilityMarkRemap, KeepsBaseGameRefsStable)
    {
        const std::map<int32_t, std::string> saved = { { 0, "Morrowind.esm" } };
        std::vector<std::string> now(60);
        now[0] = "Morrowind.esm";

        int32_t out = -999;
        EXPECT_EQ(remapContentFileIndex(0, saved, now, out), RemapResult::Unchanged);
        EXPECT_EQ(out, 0);
    }

    // Two plugins swapping places must each follow their own name, not merely
    // shift by a constant offset.
    TEST(MWAccessibilityMarkRemap, FollowsEachPluginIndividuallyWhenTheySwap)
    {
        const std::map<int32_t, std::string> saved = { { 5, "A.esp" }, { 6, "B.esp" } };
        const std::vector<std::string> now = { "0", "1", "2", "3", "4", "B.esp", "A.esp" };

        int32_t outA = -999;
        EXPECT_EQ(remapContentFileIndex(5, saved, now, outA), RemapResult::Remapped);
        EXPECT_EQ(outA, 6);

        int32_t outB = -999;
        EXPECT_EQ(remapContentFileIndex(6, saved, now, outB), RemapResult::Remapped);
        EXPECT_EQ(outB, 5);
    }
}
