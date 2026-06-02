#include "editfield.hpp"

#include <algorithm>

#include <MyGUI_EditBox.h>
#include <MyGUI_UString.h>

#include "speech.hpp"

namespace MWGui::A11y
{
    namespace
    {
        // Encode a single UTF-32 code point as UTF-8 onto \p out.
        void appendUtf8(std::string& out, char32_t ch)
        {
            if (ch < 0x80)
                out.push_back(static_cast<char>(ch));
            else if (ch < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
            else if (ch < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
                out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (ch >> 18)));
                out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
        }

        // Whole-run text: raw UTF-8 with real spaces (unlike the single-char
        // mapping which speaks "space" etc.).
        std::string toUtf8(const std::u32string& str)
        {
            std::string out;
            for (char32_t ch : str)
                appendUtf8(out, ch);
            return out;
        }

        // Extract the line (run between newlines) of \p text containing the
        // caret at \p caret. Used to read the current row in a multi-line box.
        std::u32string lineAt(const std::u32string& text, size_t caret)
        {
            if (text.empty())
                return {};
            const size_t pos = std::min(caret, text.size());
            size_t start = pos;
            while (start > 0 && text[start - 1] != U'\n')
                --start;
            size_t end = pos;
            while (end < text.size() && text[end] != U'\n')
                ++end;
            return text.substr(start, end - start);
        }

        // Spoken name for a single character, mapping whitespace / invisible
        // characters to words so the TTS backend doesn't silently drop them.
        std::string spokenChar(char32_t ch)
        {
            switch (ch)
            {
                case U' ':
                    return "space";
                case U'\t':
                    return "tab";
                case U'\n':
                case U'\r':
                    return "newline";
            }
            std::string out;
            appendUtf8(out, ch);
            return out;
        }
    }

    void EditField::attach(MyGUI::EditBox* edit)
    {
        mEdit = edit;
        if (!mEdit)
            return;
        mEdit->eventKeyButtonPressed += MyGUI::newDelegate(this, &EditField::onKey);
        mEdit->eventKeySetFocus += MyGUI::newDelegate(this, &EditField::onSetFocus);
        sync();
    }

    std::u32string EditField::text() const
    {
        if (!mEdit)
            return {};
        return mEdit->getOnlyText().asUTF32();
    }

    size_t EditField::caret() const
    {
        return mEdit ? mEdit->getTextCursor() : 0;
    }

    void EditField::sync()
    {
        mPrevText = text();
        mPrevCaret = caret();
        mHasPending = false;
    }

    void EditField::sayChar(char32_t ch)
    {
        say(spokenChar(ch), /*interrupt=*/true);
    }

    void EditField::sayRun(const std::u32string& run)
    {
        if (run.empty())
            return;
        if (run.size() == 1)
            sayChar(run[0]);
        else
            say(toUtf8(run), /*interrupt=*/true);
    }

    void EditField::announceContents(const std::string& label)
    {
        sync();
        const std::u32string current = text();
        std::string spoken = label;
        if (current.empty())
        {
            if (!spoken.empty())
                spoken += ": ";
            spoken += "blank";
        }
        else
        {
            if (!spoken.empty())
                spoken += ": ";
            spoken += toUtf8(current);
        }
        say(spoken, /*interrupt=*/true);
    }

    void EditField::onSetFocus(MyGUI::Widget* /*sender*/, MyGUI::Widget* /*oldFocus*/)
    {
        // Re-baseline so the first edit after (re)gaining focus diffs correctly.
        sync();
    }

    void EditField::setActive(bool active)
    {
        mActive = active;
        // NB: we deliberately do *not* toggle setEditReadOnly here. A read-only
        // MyGUI EditBox stops firing eventKeyButtonPressed, which would kill the
        // Screen's own key handler hooked on this same widget (breaking all form
        // navigation once focus rests on the field). Instead we leave the box
        // editable and silently revert any stray edits while inactive (see
        // onFrame). Re-baseline so the next edit diffs correctly.
        sync();
    }

    void EditField::onKey(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char /*ch*/)
    {
        // Record the key even while inactive: onFrame uses it to know a stray
        // native edit may have happened so it can revert it silently.
        mPendingKey = key;
        mHasPending = true;
    }

    void EditField::onFrame()
    {
        if (!mEdit || !mHasPending)
            return;
        mHasPending = false;

        // While inactive the field is only navigated past, not edited: undo any
        // text the native EditBox may have inserted/removed (keeping the caret
        // sane) and stay silent. Caret-only moves are harmless, so ignore them.
        if (!mActive)
        {
            if (text() != mPrevText)
            {
                mEdit->setOnlyText(MyGUI::UString(toUtf8(mPrevText)));
                mEdit->setTextCursor(std::min(mPrevCaret, mPrevText.size()));
            }
            return;
        }

        const std::u32string now = text();
        const size_t caretNow = caret();
        const MyGUI::KeyCode key = mPendingKey;

        const std::u32string before = mPrevText;
        const size_t caretBefore = mPrevCaret;

        // Update the snapshot up front so every return path leaves a correct
        // baseline for the next key.
        mPrevText = now;
        mPrevCaret = caretNow;

        switch (key.getValue())
        {
            case MyGUI::KeyCode::ArrowLeft:
            case MyGUI::KeyCode::ArrowRight:
            case MyGUI::KeyCode::Home:
            case MyGUI::KeyCode::End:
            {
                // Pure caret movement: read the character at the new caret. For
                // a rightward move we read the character we just passed over
                // (the one to the left of the caret); for leftward / Home we
                // read the character now under the caret. End past the last
                // character reads "blank".
                if (now.empty())
                {
                    say("blank", /*interrupt=*/true);
                    return;
                }
                const bool movedRight
                    = (key == MyGUI::KeyCode::ArrowRight || key == MyGUI::KeyCode::End);
                size_t readIndex;
                if (movedRight)
                    readIndex = caretNow > 0 ? caretNow - 1 : 0;
                else
                    readIndex = caretNow;
                if (readIndex >= now.size())
                {
                    say("blank", /*interrupt=*/true);
                    return;
                }
                sayChar(now[readIndex]);
                return;
            }
            case MyGUI::KeyCode::ArrowUp:
            case MyGUI::KeyCode::ArrowDown:
            {
                if (now.empty())
                {
                    say("blank", /*interrupt=*/true);
                    return;
                }
                // A multi-line box has rows: Up/Down move the caret between
                // them, so read the line the caret now sits on. A single-line
                // box has no rows, so Up/Down just read the whole content.
                if (mEdit->getEditMultiLine())
                {
                    const std::u32string line = lineAt(now, caretNow);
                    if (line.empty())
                        say("blank", /*interrupt=*/true);
                    else
                        say(toUtf8(line), /*interrupt=*/true);
                }
                else
                    say(toUtf8(now), /*interrupt=*/true);
                return;
            }
            case MyGUI::KeyCode::Backspace:
            case MyGUI::KeyCode::Delete:
            {
                // Announce whatever was removed by diffing old vs new around the
                // caret. Find the common prefix and suffix; the middle of the
                // old string that's gone is what was deleted.
                if (now.size() >= before.size())
                {
                    // Nothing actually removed (e.g. already empty).
                    say("blank", /*interrupt=*/true);
                    return;
                }
                size_t prefix = 0;
                const size_t minLen = now.size();
                while (prefix < minLen && now[prefix] == before[prefix])
                    ++prefix;
                size_t suffix = 0;
                while (suffix < (minLen - prefix) && now[now.size() - 1 - suffix] == before[before.size() - 1 - suffix])
                    ++suffix;
                const size_t removedCount = before.size() - now.size();
                const std::u32string removed = before.substr(prefix, removedCount);
                if (removed.empty())
                    return;
                sayRun(removed);
                return;
            }
            default:
            {
                // Any other key: if exactly one or more characters were inserted
                // just before the caret, echo them. Otherwise stay silent (e.g.
                // modifier keys, Tab, Enter handled elsewhere).
                if (now.size() > before.size())
                {
                    size_t prefix = 0;
                    const size_t minLen = before.size();
                    while (prefix < minLen && now[prefix] == before[prefix])
                        ++prefix;
                    const size_t insertedCount = now.size() - before.size();
                    const std::u32string inserted = now.substr(prefix, insertedCount);
                    sayRun(inserted);
                }
                return;
            }
        }
    }
}
