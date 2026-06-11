#ifndef OPENMW_MWGUI_ACCESSIBILITY_ELEMENT_H
#define OPENMW_MWGUI_ACCESSIBILITY_ELEMENT_H

#include <functional>
#include <string>
#include <vector>

namespace MyGUI
{
    class Widget;
}

namespace MWGui::A11y
{
    class EditField;

    /// One item inside an expandable Element's submenu. Produced on demand by
    /// \c Element::children so it always reflects current data.
    struct SubItem
    {
        /// Spoken text for this item (e.g. "Alteration 5").
        std::string label;

        /// Optional section/group this item belongs to (e.g. "Major Skills").
        /// When focus crosses into a different section, the section name is
        /// announced before the item, e.g. "Major Skills: Alteration 5".
        /// Items sharing a section are announced without the prefix. Leave
        /// empty for a flat list with no sections.
        std::string section;

        /// Optional tooltip lines for this item, cycled with T / Shift+T while
        /// the submenu is open. Recomputed on demand.
        std::function<std::vector<std::string>()> tooltips;

        /// Enter / Space activation handler for this submenu item. Optional;
        /// leave empty for a read-only item. Lets a submenu offer per-item
        /// actions (e.g. a map note opening its edit dialog) without the
        /// framework needing to know what the item is.
        std::function<void()> activate;
    };

    /// Declarative description of one navigable option on an accessible screen.
    ///
    /// Behaviour is supplied as optional callbacks so the framework never needs
    /// to know what a given option actually is. Construct with designated
    /// initializers, e.g.:
    ///
    /// \code
    /// screen.add({ .widget = mOkButton, .label = "#{sOK}",
    ///              .activate = [this]{ onOkClicked(mOkButton); } });
    /// \endcode
    struct Element
    {
        /// Widget that receives keyboard focus. May be a *proxy* (e.g. a header
        /// TextBox) when the real control consumes the arrow keys itself.
        MyGUI::Widget* widget = nullptr;

        /// Spoken name of the option. May contain MyGUI \c #{tags}.
        std::string label;

        /// Optional section/group this option belongs to. When navigation moves
        /// onto an option whose section differs from the previously focused
        /// one's, the section name is announced before the option, e.g.
        /// "Combat: Block". Options sharing a section are announced without the
        /// prefix. Leave empty for a flat list with no sections.
        std::string section;

        /// Optional override for the *focus* announcement. When set, this is
        /// spoken when the element gains focus instead of \c label + \c value.
        /// Use for options whose name is computed dynamically (e.g. settings
        /// whose label is resolved by walking sibling widgets at runtime).
        std::function<std::string()> describe;

        /// Returns the current value text, spoken on focus (after \c label,
        /// unless \c describe is set) and after a change. Leave empty for
        /// options with no value (e.g. buttons).
        std::function<std::string()> value;

        /// Left/Right handler. \c next is true for Right (increment) and false
        /// for Left (decrement). Leave empty for options with no value.
        std::function<void(bool next)> change;

        /// Set when \c change applies the new value *asynchronously* (the value
        /// returned by \c value won't reflect the change for one or more frames,
        /// e.g. a global Lua setting that round-trips through the global script
        /// context). When true, \c changeValue does NOT speak the value
        /// immediately after \c change -- doing so would announce the stale,
        /// pre-change value. The owner is responsible for announcing the new
        /// value once it settles (typically by polling in its onFrame).
        bool asyncValue = false;

        /// Returns the tooltip lines cycled by T / Shift+T. Leave empty for
        /// options with no tooltip. Recomputed on demand so it always reflects
        /// the current value.
        std::function<std::vector<std::string>()> tooltips;

        /// Enter / Space activation handler (e.g. for buttons). Optional.
        std::function<void()> activate;

        /// Makes this option an *expandable submenu*. When set (and non-empty),
        /// pressing Enter on the option enters a child list: Up/Down move
        /// between children, T cycles a child's tooltips, and Escape returns to
        /// the main screen. A delayed hint announces "Press Enter to expand".
        /// Recomputed each time the submenu is opened so it reflects live data.
        ///
        /// Takes precedence over \c activate when both are set. Children may be
        /// grouped into sections via \c SubItem::section.
        std::function<std::vector<SubItem>()> children;

        /// Makes this option an *editable text field*. When set, pressing Enter
        /// on the option enters "edit mode": all keystrokes are routed to the
        /// edit box (text editing + spoken feedback via EditField), and Escape
        /// exits edit mode back to form navigation. While not in edit mode the
        /// option behaves like a normal navigable item (its value, spoken on
        /// focus, should report the current contents).
        ///
        /// Takes precedence over \c children and \c activate when set. The
        /// EditField must outlive the screen (typically a window member).
        EditField* edit = nullptr;
    };
}

#endif
