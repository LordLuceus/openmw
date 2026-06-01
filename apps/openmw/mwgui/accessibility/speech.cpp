#include "speech.hpp"

#include <string>

#include <MyGUI_LanguageManager.h>
#include <MyGUI_UString.h>

#include <components/accessibility/accessibilitymanager.hpp>

namespace MWGui::A11y
{
    void say(std::string_view text, bool interrupt)
    {
        if (text.empty())
            return;

        // Resolve #{Group:Key} tags (GMST and L10n) to display text.
        MyGUI::UString resolved
            = MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(std::string(text)));
        std::string utf8 = resolved.asUTF8();
        if (utf8.empty())
            return;

        Accessibility::AccessibilityManager::instance().speak(utf8, interrupt);
    }
}
