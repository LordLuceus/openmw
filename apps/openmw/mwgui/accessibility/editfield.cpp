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

        // Diff two strings as a single changed span: the maximal common prefix
        // and (non-overlapping) common suffix are stripped, and whatever's left
        // in the middle of each is the removed / inserted run. This generalises
        // pure insertion (removed empty), pure deletion (inserted empty) AND
        // replacement (both non-empty) -- e.g. typing or pasting over a
        // selection, where the older insert-only / delete-only diffs either said
        // nothing (equal-length replace) or announced the wrong characters.
        struct SpanDiff
        {
            std::u32string removed;
            std::u32string inserted;
        };

        SpanDiff diffSpan(const std::u32string& before, const std::u32string& now)
        {
            size_t prefix = 0;
            const size_t maxPrefix = std::min(before.size(), now.size());
            while (prefix < maxPrefix && before[prefix] == now[prefix])
                ++prefix;
            // Common suffix, but never overlapping the prefix already matched in
            // either string (so a span can't be counted twice).
            size_t suffix = 0;
            const size_t maxSuffix = std::min(before.size(), now.size()) - prefix;
            while (suffix < maxSuffix && before[before.size() - 1 - suffix] == now[now.size() - 1 - suffix])
                ++suffix;
            SpanDiff diff;
            diff.removed = before.substr(prefix, before.size() - prefix - suffix);
            diff.inserted = now.substr(prefix, now.size() - prefix - suffix);
            return diff;
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

    EditField::~EditField()
    {
        detach();
    }

    void EditField::attach(MyGUI::EditBox* edit)
    {
        // Drop any previous binding first so we never leave stale delegates on a
        // box we're no longer tracking, and never double-add to a reused box.
        detach();
        mEdit = edit;
        if (!mEdit)
            return;
        mEdit->eventKeyButtonPressed += MyGUI::newDelegate(this, &EditField::onKey);
        mEdit->eventKeySetFocus += MyGUI::newDelegate(this, &EditField::onSetFocus);
        // If the box is destroyed before we are (volatile Lua-page widgets get
        // rebuilt out from under us), this fires so we forget it instead of
        // later dereferencing / unhooking freed memory.
        mEdit->eventWidgetDestroyed += MyGUI::newDelegate(this, &EditField::onWidgetDestroyed);
        sync();
    }

    void EditField::detach()
    {
        if (!mEdit)
            return;
        // The box is still alive here: cleanly remove our listeners so a
        // destroyed/re-attached EditField leaves nothing dangling on it.
        mEdit->eventKeyButtonPressed -= MyGUI::newDelegate(this, &EditField::onKey);
        mEdit->eventKeySetFocus -= MyGUI::newDelegate(this, &EditField::onSetFocus);
        mEdit->eventWidgetDestroyed -= MyGUI::newDelegate(this, &EditField::onWidgetDestroyed);
        mEdit = nullptr;
        mHasPending = false;
    }

    void EditField::onWidgetDestroyed(MyGUI::Widget* /*sender*/)
    {
        // The widget is tearing down; its event objects are about to be freed.
        // Just forget it -- do NOT -= (that would touch the dying delegates).
        mEdit = nullptr;
        mHasPending = false;
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

    void EditField::announceSpan(const std::u32string& removed, const std::u32string& inserted)
    {
        // Decide what to speak for a single changed span. Replacement (both
        // non-empty -- e.g. typing or pasting over a selection) speaks the new
        // text, since the freshest, most useful feedback is what the field now
        // holds at the caret. Pure insertion echoes what was added; pure
        // deletion announces what was removed. An empty span (no change) is a
        // no-op -- callers already filter now == before.
        if (!inserted.empty())
            sayRun(inserted);
        else if (!removed.empty())
            sayRun(removed);
    }

    void EditField::announceContents(const std::string& label, bool interrupt)
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
        say(spoken, interrupt);
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
                // Announce whatever was removed by diffing old vs new. The span
                // diff covers both a single-character backspace and deleting a
                // whole selection at once; if a selection was *replaced* (rare on
                // these keys) we fall through to the general announce below.
                if (now == before)
                {
                    // Nothing changed (e.g. Backspace in an already-empty field,
                    // or at the very start with nothing to the left).
                    say("blank", /*interrupt=*/true);
                    return;
                }
                const SpanDiff diff = diffSpan(before, now);
                // Pure deletion speaks what was removed; a replacement (selection
                // overwritten) or any other shape is handled by the shared
                // announce so we never go silent.
                announceSpan(diff.removed, diff.inserted);
                return;
            }
            default:
            {
                // Any other key (typing, paste, IME commit): diff old vs new as a
                // single changed span. This handles a plain insertion, typing or
                // pasting *over a selection* (replacement -- which the old
                // size-only check missed when the new text was the same length or
                // shorter), and a selection delete-via-typing. Pure caret moves /
                // modifier keys leave the text unchanged and stay silent.
                if (now == before)
                    return;
                const SpanDiff diff = diffSpan(before, now);
                announceSpan(diff.removed, diff.inserted);
                return;
            }
        }
    }
}
