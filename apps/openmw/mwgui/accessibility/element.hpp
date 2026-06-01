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

        /// Returns the current value text, spoken on focus and after a change.
        /// Leave empty for options with no value (e.g. buttons).
        std::function<std::string()> value;

        /// Left/Right handler. \c next is true for Right (increment) and false
        /// for Left (decrement). Leave empty for options with no value.
        std::function<void(bool next)> change;

        /// Returns the tooltip lines cycled by T / Shift+T. Leave empty for
        /// options with no tooltip. Recomputed on demand so it always reflects
        /// the current value.
        std::function<std::vector<std::string>()> tooltips;

        /// Enter / Space activation handler (e.g. for buttons). Optional.
        std::function<void()> activate;
    };
}

#endif
