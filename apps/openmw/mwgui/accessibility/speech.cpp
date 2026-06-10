#include "speech.hpp"

#include <string>

#include <MyGUI_LanguageManager.h>
#include <MyGUI_UString.h>

#include <components/accessibility/accessibilitymanager.hpp>
#include <components/debug/debuglog.hpp>

namespace MWGui::A11y
{
    namespace
    {
        // The last announcement marked rereadable (already tag-resolved). Repeated
        // verbatim by reread(). Process-wide because the reread key may fire from a
        // different screen than the one that produced the text (e.g. dialogue).
        std::string sLastRereadable;

        std::string resolveTags(std::string_view text)
        {
            MyGUI::UString resolved
                = MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(std::string(text)));
            return resolved.asUTF8();
        }
    }

    void say(std::string_view text, bool interrupt)
    {
        if (text.empty())
            return;

        // Resolve #{Group:Key} tags (GMST and L10n) to display text.
        std::string utf8 = resolveTags(text);
        if (utf8.empty())
        {
            // Non-empty input resolved to nothing -- almost always a broken or
            // renamed #{Group:Key} tag (e.g. after an upstream l10n change). The
            // user would just hear silence with no clue why; log which input
            // failed so it's diagnosable. (An all-tag string with no fallback
            // text is the classic culprit.)
            logWarn(std::string("say: input resolved to empty (broken tag?): ").append(text));
            return;
        }

        Accessibility::AccessibilityManager::instance().speak(utf8, interrupt);
    }

    void sayRereadable(std::string_view text, bool interrupt)
    {
        if (text.empty())
            return;

        std::string utf8 = resolveTags(text);
        if (utf8.empty())
        {
            logWarn(std::string("sayRereadable: input resolved to empty (broken tag?): ").append(text));
            return;
        }

        sLastRereadable = utf8;
        Accessibility::AccessibilityManager::instance().speak(utf8, interrupt);
    }

    void sayRereadable(std::string_view spoken, std::string_view rereadable, bool interrupt)
    {
        // Store the (possibly richer) reread text even if there's nothing to
        // speak right now, so R can still recall it.
        std::string rereadUtf8 = resolveTags(rereadable);
        if (!rereadUtf8.empty())
            sLastRereadable = rereadUtf8;

        std::string spokenUtf8 = resolveTags(spoken);
        if (!spokenUtf8.empty())
            Accessibility::AccessibilityManager::instance().speak(spokenUtf8, interrupt);
        else if (!spoken.empty())
        {
            // Caller had something to say but it resolved to nothing -- a broken
            // tag in the spoken half. (An intentionally-empty spoken half, used
            // to set reread-only text, is fine and not logged.)
            logWarn(std::string("sayRereadable: spoken input resolved to empty (broken tag?): ").append(spoken));
        }
    }

    void reread()
    {
        if (sLastRereadable.empty())
            return;
        // Interrupt: the user pressed R deliberately and wants it now.
        Accessibility::AccessibilityManager::instance().speak(sLastRereadable, /*interrupt=*/true);
    }

    void clearReread()
    {
        sLastRereadable.clear();
    }

    void logWarn(std::string_view message)
    {
        // Single, consistently-prefixed sink for framework diagnostics. The
        // [a11y] prefix matches the project's other accessibility log lines so
        // a tester's openmw.log can be filtered with one grep.
        Log(Debug::Warning) << "[a11y] " << message;
    }
}
