#ifndef OPENMW_MWGUI_ACCESSIBILITY_BOOKTEXT_H
#define OPENMW_MWGUI_ACCESSIBILITY_BOOKTEXT_H

#include <string>
#include <vector>

namespace MWGui::A11y
{
    /// Convert raw Morrowind book/scroll markup into a list of plain-text
    /// paragraphs suitable for screen-reader navigation (one navigable line
    /// each, read with Up/Down).
    ///
    /// This reuses the engine's own \c Formatting::BookTextParser -- the same
    /// parser the visual renderer uses -- so the extracted text matches what a
    /// sighted player sees, with no separate markup handling to drift out of
    /// sync. Formatting tags (\c <FONT>, \c <DIV>) and images (\c <IMG>) are
    /// dropped; \c <BR> / \c <P> / [pagebreak] become paragraph boundaries.
    ///
    /// \param markup the record text (ESM::Book::mText).
    /// \param shrinkTextAtLastTag matches the engine's per-record flag
    ///        (true for ESM3 books, false for ESM4).
    std::vector<std::string> bookMarkupToParagraphs(const std::string& markup, bool shrinkTextAtLastTag);
}

#endif
