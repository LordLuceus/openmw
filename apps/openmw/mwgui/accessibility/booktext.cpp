#include "booktext.hpp"

#include "../formatting.hpp"

namespace
{
    // Trim leading/trailing ASCII whitespace from a view into \p s.
    std::string trim(const std::string& s)
    {
        size_t begin = 0;
        size_t end = s.size();
        auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        while (begin < end && isSpace(s[begin]))
            ++begin;
        while (end > begin && isSpace(s[end - 1]))
            --end;
        return s.substr(begin, end - begin);
    }
}

namespace MWGui::A11y
{
    std::vector<std::string> bookMarkupToParagraphs(
        const std::string& markup, bool shrinkTextAtLastTag, bool* outHasImage)
    {
        if (outHasImage)
            *outHasImage = false;

        // Drive the engine's own parser exactly as the visual renderer does
        // (see Formatting::BookFormatter::markupToWidget): pull text blocks,
        // skipping <BR> and attribute-less <P> events. The parser embeds '\n'
        // characters for line/paragraph breaks directly into the text it
        // accumulates, and flushes a block at every non-BR/P tag (e.g. a
        // mid-sentence <FONT> or <DIV> change). We therefore concatenate the
        // flushed blocks *verbatim* -- inserting our own separator would split
        // a sentence at a formatting boundary -- and split into paragraphs on
        // the parser's embedded newlines afterwards.
        Formatting::BookTextParser parser(markup, shrinkTextAtLastTag);

        std::string combined;
        for (;;)
        {
            Formatting::BookTextParser::Events event = parser.next();
            if (event != Formatting::BookTextParser::Event_BrTag
                && !(event == Formatting::BookTextParser::Event_PTag && parser.getAttributes().empty()))
            {
                combined += parser.getReadyText();
            }
            if (event == Formatting::BookTextParser::Event_ImgTag && outHasImage)
                *outHasImage = true;
            if (event == Formatting::BookTextParser::Event_EOF)
                break;
        }

        // Split into paragraphs on newlines, trimming each and dropping blank
        // lines so navigation isn't cluttered with empty entries.
        std::vector<std::string> paragraphs;
        size_t start = 0;
        while (start <= combined.size())
        {
            size_t nl = combined.find('\n', start);
            if (nl == std::string::npos)
                nl = combined.size();
            std::string line = trim(combined.substr(start, nl - start));
            if (!line.empty())
                paragraphs.push_back(std::move(line));
            start = nl + 1;
        }

        return paragraphs;
    }
}
