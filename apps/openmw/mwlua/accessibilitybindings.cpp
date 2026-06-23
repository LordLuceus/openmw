#include "accessibilitybindings.hpp"

#include "context.hpp"
#include "luamanagerimp.hpp"

#include "../mwgui/accessibility/speech.hpp"

#include <components/lua/luastate.hpp>

namespace MWLua
{
    sol::table initAccessibilityPackage(const Context& context)
    {
        sol::table api(context.sol(), sol::create);

        // Lua scripts can run on a worker thread (see Settings "lua num threads"),
        // but the screen-reader backend must be driven from the main thread like
        // every other native A11y::say caller. So defer each call onto the Lua
        // manager's action queue, which is drained on the main thread during the
        // engine's synchronizedUpdate -- never call the backend straight from the
        // script thread (that silently produces no speech).

        // say(text, [interrupt]) -- speak text through the screen reader.
        // interrupt defaults to true: a discrete Lua-driven announcement (e.g. a
        // mod's menu selection changing) should replace whatever is being said,
        // not queue behind it. Pass false to queue. Empty/whitespace and broken
        // #{Group:Key} tags are handled (and logged) by MWGui::A11y::say.
        api["say"] = [context](std::string_view text, sol::optional<bool> interrupt) {
            context.mLuaManager->addAction(
                [str = std::string(text), interrupt = interrupt.value_or(true)] { MWGui::A11y::say(str, interrupt); },
                "A11y::say");
        };

        // sayRereadable(text, [interrupt]) -- speak text and remember it as the
        // last announcement repeatable with the native reread key. For
        // contextual prose the user can't re-navigate to (a line of dialogue, a
        // narrated event), NOT for per-option focus announcements.
        api["sayRereadable"] = [context](std::string_view text, sol::optional<bool> interrupt) {
            context.mLuaManager->addAction(
                [str = std::string(text), interrupt = interrupt.value_or(true)] {
                    MWGui::A11y::sayRereadable(str, interrupt);
                },
                "A11y::sayRereadable");
        };

        // reread() -- re-speak the last rereadable announcement (interrupting),
        // mirroring the native reread key. No-op if nothing is marked rereadable.
        api["reread"] = [context]() {
            context.mLuaManager->addAction([] { MWGui::A11y::reread(); }, "A11y::reread");
        };

        return LuaUtil::makeReadOnly(api);
    }
}
