#include "apps/openmw/mwgui/accessibility/booktext.hpp"

#include <gtest/gtest.h>

namespace MWGui::A11y
{
    namespace
    {
        // --- bookMarkupToParagraphs ---------------------------------------
        // Extracts plain-text paragraphs from book/scroll markup using the
        // engine's own parser, and reports (via outHasImage) whether the
        // markup contained any <IMG> tag so callers can tell an image-only
        // book apart from a genuinely blank one.

        // ESM4 semantics (shrinkTextAtLastTag=false): all text is shown, so a
        // tagless string is a single paragraph.
        TEST(MWAccessibilityBookText, plainTextIsOneParagraph)
        {
            bool hasImage = true; // must be cleared even when no image present
            std::vector<std::string> paras = bookMarkupToParagraphs("Hello world.", false, &hasImage);
            ASSERT_EQ(paras.size(), 1u);
            EXPECT_EQ(paras[0], "Hello world.");
            EXPECT_FALSE(hasImage);
        }

        TEST(MWAccessibilityBookText, brSplitsParagraphs)
        {
            std::vector<std::string> paras = bookMarkupToParagraphs("First line<BR>Second line", false, nullptr);
            ASSERT_EQ(paras.size(), 2u);
            EXPECT_EQ(paras[0], "First line");
            EXPECT_EQ(paras[1], "Second line");
        }

        // ESM3 semantics (shrinkTextAtLastTag=true): vanilla hides any text
        // after the last <BR>/<P>, and with no such tag it hides everything.
        // We mirror the engine exactly so the reader hears what's on the page.
        TEST(MWAccessibilityBookText, esm3ShrinkDropsTaglessText)
        {
            std::vector<std::string> paras = bookMarkupToParagraphs("Hello world.", true, nullptr);
            EXPECT_TRUE(paras.empty());
        }

        TEST(MWAccessibilityBookText, esm3ShrinkDropsTextAfterLastBreak)
        {
            std::vector<std::string> paras = bookMarkupToParagraphs("First line<BR>Second line", true, nullptr);
            ASSERT_EQ(paras.size(), 1u);
            EXPECT_EQ(paras[0], "First line");
        }

        TEST(MWAccessibilityBookText, emptyMarkupHasNoParagraphsAndNoImage)
        {
            bool hasImage = true;
            std::vector<std::string> paras = bookMarkupToParagraphs("", true, &hasImage);
            EXPECT_TRUE(paras.empty());
            EXPECT_FALSE(hasImage);
        }

        TEST(MWAccessibilityBookText, imageOnlyBookHasNoTextButReportsImage)
        {
            // An image-only book (e.g. The Egg of Time, Divine Metaphysics):
            // no extractable text, but not blank.
            bool hasImage = false;
            std::vector<std::string> paras
                = bookMarkupToParagraphs("<IMG src=\"bookart/egg.dds\" width=\"256\" height=\"256\">", true, &hasImage);
            EXPECT_TRUE(paras.empty());
            EXPECT_TRUE(hasImage);
        }

        TEST(MWAccessibilityBookText, textWithImageReportsImageAndKeepsText)
        {
            bool hasImage = false;
            std::vector<std::string> paras = bookMarkupToParagraphs(
                "Caption below.<BR><IMG src=\"bookart/pic.dds\" width=\"64\" height=\"64\">", true, &hasImage);
            ASSERT_EQ(paras.size(), 1u);
            EXPECT_EQ(paras[0], "Caption below.");
            EXPECT_TRUE(hasImage);
        }

        TEST(MWAccessibilityBookText, nullOutHasImageIsSafe)
        {
            // Passing nullptr for outHasImage must not crash.
            std::vector<std::string> paras
                = bookMarkupToParagraphs("<IMG src=\"bookart/x.dds\" width=\"1\" height=\"1\">", true, nullptr);
            EXPECT_TRUE(paras.empty());
        }
    }
}
