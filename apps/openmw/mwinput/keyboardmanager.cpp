#include "keyboardmanager.hpp"

#include <cctype>

#include <MyGUI_InputManager.h>

#include <components/sdlutil/sdlmappings.hpp>

#include "../mwaccessibility/scanner.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "actions.hpp"
#include "bindingsmanager.hpp"

namespace MWInput
{
    KeyboardManager::KeyboardManager(BindingsManager* bindingsManager)
        : mBindingsManager(bindingsManager)
    {
    }

    void KeyboardManager::textInput(const SDL_TextInputEvent& arg)
    {
        MyGUI::UString ustring(&arg.text[0]);
        MyGUI::UString::utf32string utf32string = ustring.asUTF32();
        for (MyGUI::UString::utf32string::const_iterator it = utf32string.begin(); it != utf32string.end(); ++it)
            MyGUI::InputManager::getInstance().injectKeyPress(MyGUI::KeyCode::None, *it);
    }

    void KeyboardManager::keyPressed(const SDL_KeyboardEvent& arg)
    {
        // Give the accessibility scanner first crack at the key. It only
        // consumes a small set (PgUp/PgDn, Enter, Home, End, Backspace)
        // and only during gameplay; everything else falls through to
        // normal handling.
        //
        // Auto-repeat is allowed only for the list-navigation keys. Holding
        // PgUp/PgDn to run through a long list is the whole point of a key
        // repeat -- a library with hundreds of books was otherwise one tap per
        // book (reported 2026-08-21). Every other scanner key is an ACTION
        // (activate, take, mark, teleport, toggle a mode), where a repeat means
        // firing it dozens of times from one held press, so those stay
        // edge-triggered.
        const bool repeatable = arg.keysym.scancode == SDL_SCANCODE_PAGEUP
            || arg.keysym.scancode == SDL_SCANCODE_PAGEDOWN;
        if ((!arg.repeat || repeatable)
            && MWAccessibility::Scanner::instance().handleKey(
                arg.keysym.scancode, arg.keysym.mod, arg.repeat != 0))
        {
            MWBase::Environment::get().getInputManager()->setJoystickLastUsed(false);
            return;
        }

        // HACK: to make default keybinding for the console work without printing an extra "^" upon closing
        // This assumes that SDL_TextInput events always come *after* the key event
        // (which is somewhat reasonable, and hopefully true for all SDL platforms)
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.keysym.sym);
        if (mBindingsManager->getKeyBinding(A_Console) == arg.keysym.scancode
            && MWBase::Environment::get().getWindowManager()->isConsoleMode())
            SDL_StopTextInput();

        bool consumed = SDL_IsTextInputActive() && // Little trick to check if key is printable
            (!(SDLK_SCANCODE_MASK & arg.keysym.sym) &&
                // Don't trust isprint for symbols outside the extended ASCII range
                ((kc == MyGUI::KeyCode::None && arg.keysym.sym > 0xff)
                    || (arg.keysym.sym >= 0 && arg.keysym.sym <= 255 && std::isprint(arg.keysym.sym))));
        if (kc != MyGUI::KeyCode::None && !mBindingsManager->isDetectingBindingState())
        {
            if (MWBase::Environment::get().getWindowManager()->injectKeyPress(kc, 0, arg.repeat))
                consumed = true;
            mBindingsManager->setPlayerControlsEnabled(!consumed);
        }

        if (arg.repeat)
            return;

        MWBase::InputManager* input = MWBase::Environment::get().getInputManager();
        if (!input->controlsDisabled() && !consumed)
            mBindingsManager->keyPressed(arg);

        if (!consumed)
        {
            MWBase::Environment::get().getLuaManager()->inputEvent(
                { MWBase::LuaManager::InputEvent::KeyPressed, arg.keysym });
        }

        input->setJoystickLastUsed(false);
    }

    void KeyboardManager::keyReleased(const SDL_KeyboardEvent& arg)
    {
        MWBase::Environment::get().getInputManager()->setJoystickLastUsed(false);
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.keysym.sym);

        if (!mBindingsManager->isDetectingBindingState())
            mBindingsManager->setPlayerControlsEnabled(!MyGUI::InputManager::getInstance().injectKeyRelease(kc));
        mBindingsManager->keyReleased(arg);
        MWBase::Environment::get().getLuaManager()->inputEvent(
            { MWBase::LuaManager::InputEvent::KeyReleased, arg.keysym });
    }
}
