#ifndef MWGUI_BOOKPAGE_HPP
#define MWGUI_BOOKPAGE_HPP

#include "MyGUI_Colour.h"
#include "MyGUI_IFont.h"
#include "MyGUI_Widget.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <components/settings/values.hpp>

namespace MWGui
{
    /// A formatted and paginated document to be used with
    /// the book page widget.
    struct TypesetBook
    {
        using Content = std::vector<uint8_t>;
        typedef intptr_t InteractiveId;

        /// Returns the number of pages in the document.
        virtual size_t pageCount() const = 0;

        /// Return the area covered by the document. The first
        /// integer is the maximum with of any line. This is not
        /// the largest coordinate of the right edge of any line,
        /// it is the largest distance from the left edge to the
        /// right edge. The second integer is the height of all
        /// text combined prior to pagination.
        virtual std::pair<unsigned int, unsigned int> getSize() const = 0;

        /// Used to highlight journal indices
        virtual void setColour(size_t section, size_t line, size_t run, const MyGUI::Colour& colour) const = 0;

        /// Reconstruct the plain text laid out on page \p page, for
        /// screen-reader output. Returns the visible text in reading order:
        /// runs within a line are concatenated, lines are joined with spaces,
        /// and sections (paragraphs) are separated by newlines. Formatting,
        /// colours and links are dropped. Returns an empty string for an
        /// out-of-range page.
        virtual std::string getPageText(size_t page) const = 0;

        /// The number of sections (paragraphs) in the document. In the journal
        /// each logical entry (a dated update, or a topic response) is laid out
        /// as exactly one section, so for screen-reader output this is the
        /// count of entries the reader can step through one at a time.
        virtual size_t sectionCount() const = 0;

        /// Reconstruct the plain text of a whole section (paragraph/entry) in
        /// reading order, independent of pagination -- so an entry that visually
        /// spans a page boundary still comes back as one string. Lines are
        /// joined with spaces. Returns empty for an out-of-range section.
        virtual std::string getSectionText(size_t section) const = 0;

        /// The index of the page on which \p section begins, so a caller can
        /// turn to it (and derive the two-page spread it sits in) to keep the
        /// visible pages in sync while stepping entries. Returns 0 for an
        /// out-of-range section.
        virtual size_t pageForSection(size_t section) const = 0;

        /// Inverse of pageForSection: the index of the first section that begins
        /// on \p page (or, if none begins there because a long entry spans into
        /// the page, the section that covers the top of the page). Lets a caller
        /// resync its entry cursor after the user has turned pages directly.
        /// Returns 0 for an out-of-range page.
        virtual size_t sectionForPage(size_t page) const = 0;

        virtual ~TypesetBook() = default;
    };

    /// A factory class for creating a typeset book instance.
    struct BookTypesetter
    {
        virtual ~BookTypesetter() = default;

        enum Alignment
        {
            AlignLeft = -1,
            AlignCenter = 0,
            AlignRight = +1
        };

        /// Styles are used to control the character level formatting
        /// of text added to a typeset book. Their lifetime is equal
        /// to the lifetime of the book-typesetter instance that created
        /// them.
        struct Style;

        /// A factory function for creating the default implementation of a book typesetter
        static std::shared_ptr<BookTypesetter> create(int pageWidth, int pageHeight);

        /// Create a simple text style consisting of a font and a text color.
        virtual Style* createStyle(const std::string& fontName, const MyGUI::Colour& colour, bool useBookFont = true)
            = 0;

        /// Create a hyper-link style with a user-defined identifier based on an
        /// existing style. The unique flag forces a new instance of this style
        /// to be created even if an existing instance is present.
        virtual Style* createHotStyle(Style* baseStyle, const MyGUI::Colour& normalColour,
            const MyGUI::Colour& hoverColour, const MyGUI::Colour& activeColour, TypesetBook::InteractiveId id,
            bool unique = true)
            = 0;

        /// Insert a line break into the document. Newline characters in the input
        /// text have the same affect. The margin parameter adds additional space
        /// before the next line of text.
        virtual void lineBreak(float margin = 0) = 0;

        /// Insert a section  break into the document. This causes a new section
        /// to begin when additional text is inserted. Pagination attempts to keep
        /// sections together on a single page. The margin parameter adds additional space
        /// before the next line of text.
        virtual void sectionBreak(int margin = 0) = 0;

        /// Changes the alignment for the current section of text.
        virtual void setSectionAlignment(Alignment sectionAlignment) = 0;

        // Layout a block of text with the specified style into the document.
        virtual void write(Style* style, std::string_view text) = 0;

        /// Adds a content block to the document without laying it out. An
        /// identifier is returned that can be used to refer to it. If select
        /// is true, the block is activated to be references by future writes.
        virtual const TypesetBook::Content* addContent(std::string_view text, bool select = true) = 0;

        /// Select a previously created content block for future writes.
        virtual void selectContent(const TypesetBook::Content* contentHandle) = 0;

        /// Layout a span of the selected content block into the document
        /// using the specified style.
        virtual void write(Style* style, size_t begin, size_t end) = 0;

        /// Finalize the document layout, and return a pointer to it.
        virtual std::shared_ptr<TypesetBook> complete() = 0;
    };

    /// An interface to the BookPage widget.
    class BookPage : public MyGUI::Widget
    {
        MYGUI_RTTI_DERIVED(BookPage)
    public:
        using ClickCallback = std::function<void(TypesetBook::InteractiveId)>;

        /// Make the widget display the specified page from the specified book.
        virtual void showPage(std::shared_ptr<TypesetBook> book, size_t page) = 0;

        /// Set the callback for a clicking a hyper-link in the document.
        virtual void adviseLinkClicked(ClickCallback callback) = 0;

        /// Clear the hyper-link click callback.
        virtual void unadviseLinkClicked() = 0;

        /// Register the widget and associated sub-widget with MyGUI. Should be
        /// called once near the beginning of the program.
        static void registerMyGUIComponents();

        virtual void setFocusItem(BookTypesetter::Style* itemStyle) = 0;
    };
}

#endif // MWGUI_BOOKPAGE_HPP
