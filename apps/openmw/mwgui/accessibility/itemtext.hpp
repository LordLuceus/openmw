#ifndef OPENMW_MWGUI_ACCESSIBILITY_ITEMTEXT_H
#define OPENMW_MWGUI_ACCESSIBILITY_ITEMTEXT_H

#include <string>
#include <vector>

#include "../../mwworld/ptr.hpp"

namespace MWGui::A11y
{
    /// Build the spoken tooltip lines for an item stack, mirroring the on-screen
    /// item tooltip (ToolTipInfo) exactly: weight / value text, each magic
    /// effect (for potions, ingredients, enchanted gear), and -- only when the
    /// player has full-help enabled (ToggleFullHelp) -- the owner / "stolen
    /// from" / script lines. The item's name + count is deliberately NOT
    /// included (the caller already announces that as the option label); this
    /// returns only the detail lines cycled with the T key.
    ///
    /// \param ptr   the item.
    /// \param count the stack size (affects gold value display).
    std::vector<std::string> itemTooltipLines(const MWWorld::ConstPtr& ptr, int count);
}

#endif
