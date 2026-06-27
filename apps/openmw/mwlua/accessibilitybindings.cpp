#include "accessibilitybindings.hpp"

#include "context.hpp"
#include "luamanagerimp.hpp"

#include "../mwgui/accessibility/speech.hpp"

#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>

#include <components/lua/luastate.hpp>
#include <components/lua_ui/element.hpp>
#include <components/lua_ui/util.hpp>
#include <components/lua_ui/widget.hpp>

namespace MWLua
{
    namespace
    {
        // Map the MyGUI widget class name ("LuaText") back to the friendly Lua
        // type a mod author wrote ("Text"). Falls back to the raw class name for
        // anything not in the Lua UI registry (e.g. skin-internal sub-widgets).
        std::string friendlyType(MyGUI::Widget* widget)
        {
            const std::string raw(widget->getTypeName());
            const auto& map = LuaUi::widgetTypeToName();
            auto it = map.find(raw);
            return it != map.end() ? it->second : raw;
        }

        // Copy the SCALAR entries (bool / number / string) out of a foreign
        // widget's props object into a fresh table on our own sol state. We never
        // hand back the foreign table itself: although all scripts in a context
        // share one lua_State (so copying is safe), the source belongs to another
        // sandbox and must stay read-only to us. Non-scalar props (tables,
        // colours, functions) are skipped -- a screen-reader plugin only needs
        // semantic scalars like `selected` (bool) and `index` (number).
        void copyScalarProps(sol::state_view lua, const sol::object& props, sol::table& out)
        {
            if (!props.is<sol::table>())
                return;
            for (const auto& [key, value] : props.as<sol::table>())
            {
                if (!key.is<std::string>())
                    continue;
                const std::string k = key.as<std::string>();
                if (value.is<bool>())
                    out[k] = value.as<bool>();
                else if (value.is<double>())
                    out[k] = value.as<double>();
                else if (value.is<std::string>())
                    out[k] = value.as<std::string>();
            }
        }

        // Recursively snapshot a widget subtree into plain Lua tables. The
        // snapshot is a value copy: it holds NO live widget pointers (the source
        // mod may destroy and recreate its whole tree next frame), so a plugin
        // can safely read it across frames. Reads MyGUI/Lua-UI state directly,
        // which is safe because Lua scripts run after the main thread has
        // finished all widget mutation for the frame (see engine.cpp frame loop).
        sol::table snapshot(sol::state_view lua, LuaUi::WidgetExtension* ext)
        {
            sol::table node(lua, sol::create);
            MyGUI::Widget* widget = ext->widget();

            node["type"] = friendlyType(widget);

            // Collect scalar props first: we need them to resolve the name (see
            // below), and a plugin reads semantic props like `selected`/`index`.
            sol::table props(lua, sol::create);
            copyScalarProps(lua, ext->templateProperties(), props);
            copyScalarProps(lua, ext->properties(), props); // props override template
            node["props"] = props;

            // Name resolution. The canonical source is the MyGUI widget name,
            // set from the layout's top-level `name` field. BUT many mods put
            // `name` inside `props` instead (the engine ignores it there, so the
            // real widget name comes back empty) -- e.g. Daisy's Multimark names
            // its root window "LMM_List" via props. To make plugins able to find
            // such windows, fall back to a `name`/`id` string prop when the
            // widget name is empty. We expose the resolved value as `name` and
            // also keep the raw prop in `props` for transparency.
            std::string name = widget->getName();
            if (name.empty())
            {
                sol::object propName = props["name"];
                if (!propName.valid() || !propName.is<std::string>())
                    propName = props["id"];
                if (propName.valid() && propName.is<std::string>())
                    name = propName.as<std::string>();
            }
            node["name"] = name;

            if (MyGUI::ILayer* layer = widget->getLayer())
                node["layer"] = layer->getName();

            // Displayed text, if this widget renders any (Text / TextEdit, and
            // any skinned widget exposing a caption). Generic -- no need to know
            // the concrete Lua type.
            if (auto* textBox = widget->castType<MyGUI::TextBox>(false))
                node["text"] = std::string(textBox->getCaption());

            node["visible"] = widget->getVisible();
            node["clickable"] = ext->hasEventCallback("mouseClick");

            const MyGUI::IntCoord coord = widget->getAbsoluteCoord();
            sol::table position(lua, sol::create);
            position["x"] = coord.left;
            position["y"] = coord.top;
            node["position"] = position;
            sol::table size(lua, sol::create);
            size["x"] = coord.width;
            size["y"] = coord.height;
            node["size"] = size;

            const auto& children = ext->children();
            sol::table childTable(lua, sol::create);
            int i = 1;
            for (LuaUi::WidgetExtension* child : children)
                childTable[i++] = snapshot(lua, child);
            node["children"] = childTable;

            return node;
        }
    }

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

        // readUi([layerName]) -- snapshot the live Lua UI widget tree and return
        // it as plain tables, so a sandboxed accessibility "plugin" script can
        // inspect ANOTHER mod's interface (which Lua's per-script sandbox
        // otherwise hides completely). Returns an array of root nodes, one per
        // ui.create'd Element in this context (menu or player); pass a layer name
        // to return only the roots on that layer (e.g. "Windows"). Each node is
        // { type, name, layer, text, visible, clickable, position={x,y},
        // size={x,y}, props={...scalars}, children={...} } -- a recursive value
        // copy holding no live widget handles, safe to keep across frames even
        // though the source mod may rebuild its tree every frame. Unlike say(),
        // this must RETURN data, so it reads synchronously here: that is safe
        // because Lua runs only after the main thread has finished all widget
        // mutation for the frame (see the engine frame loop).
        const bool menu = context.mType == Context::Menu;
        api["readUi"] = [menu, context](sol::optional<std::string_view> layerName) {
            sol::state_view lua = context.sol();
            sol::table roots(lua, sol::create);
            int i = 1;
            LuaUi::Element::forEach(menu, [&](LuaUi::Element* element) {
                if (element->mRoot == nullptr)
                    return;
                if (layerName.has_value() && element->mLayer != *layerName)
                    return;
                roots[i++] = snapshot(lua, element->mRoot);
            });
            return roots;
        };

        return LuaUtil::makeReadOnly(api);
    }
}
