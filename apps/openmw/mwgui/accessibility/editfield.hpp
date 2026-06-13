#ifndef OPENMW_MWGUI_ACCESSIBILITY_EDITFIELD_H
#define OPENMW_MWGUI_ACCESSIBILITY_EDITFIELD_H

#include <cstddef>
#include <string>

#include <MyGUI_KeyCode.h>

namespace MyGUI
{
    class EditBox;
    class Widget;
}

namespace MWGui::A11y
{
    /// Screen-reader support for a single MyGUI::EditBox, providing standard
    /// text-editing speech feedback:
    ///
    ///  - Left / Right       read the character moved over (or "blank" past the
    ///                       end of the line)
    ///  - Up / Down          read the whole line (a single-line edit box has no
    ///                       rows to move between, so this reads everything)
    ///  - Home / End         read the character at the new caret position
    ///  - Backspace / Delete announce the character(s) that were removed
    ///  - typing             echo the inserted character(s)
    ///
    /// Usage: call attach() once after creating the edit box, forward the
    /// owning window's onFrame() to onFrame(), and call sync() after any
    /// programmatic content change (setCaption/setOnlyText) so the next edit
    /// diffs against the right baseline.
    ///
    /// MyGUI's EditBox does its own editing and caret movement when a key is
    /// pressed; the exact moment it fires eventKeyButtonPressed relative to that
    /// work is version-dependent. To stay correct regardless, we only *record*
    /// the pressed key in the event handler and do the announcement on the next
    /// onFrame(), by which point the text and caret have fully settled. We diff
    /// the settled state against the previous snapshot to work out exactly what
    /// changed (which character moved over, which was deleted, etc.), which is
    /// robust to selections and word-wrap.
    class EditField
    {
    public:
        EditField() = default;
        /// Unhooks our delegates from the bound box (if it's still alive) so a
        /// destroyed EditField never leaves dangling listeners on a surviving
        /// widget -- critical for fields bound to volatile Lua-page boxes that
        /// outlive the EditField (e.g. the Scripts tab rebuilding its options).
        ~EditField();

        // Non-copyable / non-movable: the box holds delegates bound to `this`,
        // so the address must be stable for the field's whole lifetime. (Holders
        // keep these in node-stable containers such as std::deque.)
        EditField(const EditField&) = delete;
        EditField& operator=(const EditField&) = delete;
        EditField(EditField&&) = delete;
        EditField& operator=(EditField&&) = delete;

        /// Bind to \p edit and hook its key + focus events. Re-attaching detaches
        /// any previous box first.
        void attach(MyGUI::EditBox* edit);

        /// Unhook our delegates from the bound box and forget it. Safe to call
        /// when nothing is attached. After detach(), widget() returns null.
        void detach();

        /// Process any pending keystroke recorded since the last frame. Forward
        /// the owning window's onFrame() here.
        void onFrame();

        /// Refresh the cached text/caret snapshot from the live widget without
        /// speaking. Call after programmatic changes (setCaption/setOnlyText).
        void sync();

        /// Speak the full current contents (or "blank" when empty), prefixed by
        /// \p label if non-empty. Useful on open / focus.
        ///
        /// \param interrupt when true (default), cancel any in-progress speech
        ///        first -- right when this is the sole announcement on focus.
        ///        Pass false to QUEUE behind a preceding line (e.g. the "Editing,
        ///        press Escape when done" prompt) so it isn't clobbered.
        void announceContents(const std::string& label = {}, bool interrupt = true);

        /// The bound edit box (null until attach()).
        MyGUI::EditBox* widget() const { return mEdit; }

        /// Enable or disable spoken editing feedback. When inactive, keystrokes
        /// are ignored (no announcements) -- used so the field only "talks"
        /// while the user is actually in edit mode, not while merely navigating
        /// past it. Activating re-baselines the snapshot.
        void setActive(bool active);
        bool active() const { return mActive; }

    private:
        void onKey(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char ch);
        void onSetFocus(MyGUI::Widget* sender, MyGUI::Widget* oldFocus);
        // The bound box is being destroyed (MyGUI eventWidgetDestroyed): forget
        // it WITHOUT trying to unhook (the widget is already tearing down), so we
        // never dereference or -= against freed memory afterwards.
        void onWidgetDestroyed(MyGUI::Widget* sender);

        std::u32string text() const;
        size_t caret() const;
        // Speak one UTF-32 character, mapping space / invisible characters to
        // spoken words so the TTS engine doesn't silently drop them.
        void sayChar(char32_t ch);
        // Speak a run of characters: a single character is spoken via sayChar,
        // longer runs are spoken as plain text.
        void sayRun(const std::u32string& run);
        // Speak the result of a single-span edit diff (see diffSpan): a
        // replacement speaks the inserted text, a pure insertion echoes it, a
        // pure deletion announces what was removed. Keeps typing, paste, and
        // overwrite-a-selection from ever going silent.
        void announceSpan(const std::u32string& removed, const std::u32string& inserted);

        MyGUI::EditBox* mEdit = nullptr;

        // Snapshot of the text + caret as of the last announcement, used to
        // diff the next change against.
        std::u32string mPrevText;
        size_t mPrevCaret = 0;

        // A key recorded by onKey, consumed on the next onFrame(). None means
        // nothing pending.
        MyGUI::KeyCode mPendingKey = MyGUI::KeyCode::None;
        // Whether Ctrl was held when mPendingKey was recorded. A Ctrl-modified
        // navigation key is an owner shortcut, not a caret move, so onFrame
        // stays silent for it (avoids clobbering the shortcut's announcement).
        bool mPendingCtrl = false;
        bool mHasPending = false;

        // Whether spoken editing feedback is currently on (see setActive).
        bool mActive = true;
    };
}

#endif
