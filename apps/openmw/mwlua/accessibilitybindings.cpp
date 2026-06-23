#include "accessibilitybindings.hpp"

#include "context.hpp"

#include "../mwgui/accessibility/speech.hpp"

#include <components/lua/luastate.hpp>

namespace MWLua
{
    sol::table initAccessibilityPackage(const Context& context)
    {
        sol::table api(context.sol(), sol::create);

        // say(text, [interrupt]) -- speak text through the screen reader.
        // interrupt defaults to true: a discrete Lua-driven announcement (e.g. a
        // mod's menu selection changing) should replace whatever is being said,
        // not queue behind it. Pass false to queue. Empty/whitespace and broken
        // #{Group:Key} tags are handled (and logged) by MWGui::A11y::say.
        api["say"] = [](std::string_view text, sol::optional<bool> interrupt) {
            MWGui::A11y::say(text, interrupt.value_or(true));
        };

        // sayRereadable(text, [interrupt]) -- speak text and remember it as the
        // last announcement repeatable with the native reread key. For
        // contextual prose the user can't re-navigate to (a line of dialogue, a
        // narrated event), NOT for per-option focus announcements.
        api["sayRereadable"] = [](std::string_view text, sol::optional<bool> interrupt) {
            MWGui::A11y::sayRereadable(text, interrupt.value_or(true));
        };

        // reread() -- re-speak the last rereadable announcement (interrupting),
        // mirroring the native reread key. No-op if nothing is marked rereadable.
        api["reread"] = []() { MWGui::A11y::reread(); };

        return LuaUtil::makeReadOnly(api);
    }
}
