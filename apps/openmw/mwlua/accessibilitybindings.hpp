#ifndef OPENMW_MWLUA_ACCESSIBILITYBINDINGS_H
#define OPENMW_MWLUA_ACCESSIBILITYBINDINGS_H

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    // openmw.accessibility -- screen-reader / TTS output for Lua scripts.
    //
    // This is the engine's public a11y output API: mods (and, by design,
    // standalone "a11y plugin" mods written for other mods) can speak text
    // through the same Prism backend the built-in screen reader uses, without
    // bundling their own TTS or touching the engine. Routed through
    // MWGui::A11y::say, so it shares tag resolution, empty-input logging and the
    // single speech sink with the native accessibility framework.
    sol::table initAccessibilityPackage(const Context& context);
}

#endif // OPENMW_MWLUA_ACCESSIBILITYBINDINGS_H
