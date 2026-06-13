#ifndef MWGUI_CONSOLE_H
#define MWGUI_CONSOLE_H

#include <list>
#include <string>
#include <vector>

#include <components/compiler/errorhandler.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/output.hpp>
#include <components/files/configurationmanager.hpp>

#include "../mwbase/windowmanager.hpp"

#include "../mwscript/compilercontext.hpp"

#include "referenceinterface.hpp"
#include "windowbase.hpp"

#include "accessibility/editfield.hpp"

namespace MWGui
{
    class Console : public WindowBase, private Compiler::ErrorHandler, public ReferenceInterface
    {
    public:
        /// Set the implicit object for script execution
        void setSelectedObject(const MWWorld::Ptr& object);
        MWWorld::Ptr getSelectedObject() const { return mPtr; }

        MyGUI::EditBox* mCommandLine;
        MyGUI::EditBox* mHistory;
        MyGUI::EditBox* mSearchTerm;
        MyGUI::Button* mNextButton;
        MyGUI::Button* mPreviousButton;
        MyGUI::Button* mCaseSensitiveToggleButton;
        MyGUI::Button* mRegExSearchToggleButton;

        typedef std::list<std::string> StringList;

        // History of previous entered commands
        StringList mCommandHistory;
        StringList::iterator mCurrent;
        std::string mEditString;
        std::ofstream mCommandHistoryFile;

        Console(int w, int h, bool consoleOnlyScripts, Files::ConfigurationManager& cfgMgr);
        ~Console();

        void onOpen() override;

        // Print a message to the console, in specified color.
        void print(const std::string& msg, std::string_view color = MWBase::WindowManager::sConsoleColor_Default);

        // These are pre-colored versions that you should use.

        /// Output from successful console command
        void printOK(const std::string& msg);

        /// Error message
        void printError(const std::string& msg);

        void execute(const std::string& command);

        void executeFile(const std::filesystem::path& path);

        void updateSelectedObjectPtr(const MWWorld::Ptr& currentPtr, const MWWorld::Ptr& newPtr);

        void onFrame(float dt) override;
        void clear() override;

        void resetReference() override;

        const std::string& getConsoleMode() const { return mConsoleMode; }
        void setConsoleMode(std::string_view mode);

    protected:
        void onReferenceUnavailable() override;

    private:
        std::string mConsoleMode;

        void updateConsoleTitle();

        void commandBoxKeyPress(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char value);
        void acceptCommand(MyGUI::EditBox* sender);

        // Screen-reader click-to-target replacement. A sighted player sets the
        // console's implicit object by clicking it in the world; a blind player
        // can't aim, so instead they select the object in the accessibility
        // scanner (during gameplay) and then press the grab key (Ctrl+T) here to
        // adopt that selection as the console target. Announces the adopted
        // object's name, or that nothing is selected. No-op result is spoken so
        // the action is never silent.
        void adoptScannerTarget();

        // --- Screen-reader support for the console REPL --------------------
        // The console is a read-eval-print loop, not an option list, so it
        // doesn't use A11y::Screen. Instead: the command line is an EditField
        // (spoken typing / caret / history-recall feedback), and command OUTPUT
        // is spoken as it is printed (all output funnels through print()). A
        // small set of review keys let the player re-hear output without sight.

        // Spoken editing feedback for the command-line edit box.
        A11y::EditField mCommandField;

        // The most recent block of console output, kept so the reread key (R is
        // taken by text entry, so we use a dedicated handler) and the
        // line-review keys can repeat it. Stored as already-localized,
        // tag-stripped plain text ready to speak.
        std::vector<std::string> mA11yOutputLines;
        // Review cursor into mA11yOutputLines (npos = not reviewing).
        size_t mA11yReviewIndex = std::string::npos;

        // Speak \p msg as console output (used by print()), remembering it for
        // review. Strips MyGUI markup/colour tags first. Queued (interrupt=
        // false) so multiple lines from one command don't clobber each other.
        void a11yAnnounceOutput(const std::string& msg);
        // Read the previous / next stored output line (Ctrl+Up / Ctrl+Down).
        void a11yReviewOutput(bool previous);

        enum class SearchDirection;
        void toggleCaseSensitiveSearch(MyGUI::Widget* sender);
        void toggleRegExSearch(MyGUI::Widget* sender);
        void acceptSearchTerm(MyGUI::EditBox* sender);
        void findNextOccurrence(MyGUI::Widget* sender);
        void findPreviousOccurrence(MyGUI::Widget* sender);
        void findOccurrence(SearchDirection direction);
        void findInHistoryText(
            const std::string& historyText, SearchDirection direction, size_t firstIndex, size_t lastIndex);
        void findWithRegex(
            const std::string& historyText, SearchDirection direction, size_t firstIndex, size_t lastIndex);
        void findWithStringSearch(
            const std::string& historyText, SearchDirection direction, size_t firstIndex, size_t lastIndex);
        void markOccurrence(size_t textPosition, size_t length);
        size_t mCurrentOccurrenceIndex = std::string::npos;
        size_t mCurrentOccurrenceLength = 0;
        std::string mCurrentSearchTerm;
        bool mCaseSensitiveSearch;
        bool mRegExSearch;

        std::string complete(std::string input, std::vector<std::string>& matches);

        Compiler::Extensions mExtensions;
        MWScript::CompilerContext mCompilerContext;
        std::vector<std::string> mNames;

        bool mConsoleOnlyScripts;
        Files::ConfigurationManager& mCfgMgr;
        bool compile(const std::string& cmd, Compiler::Output& output);

        /// Report error to the user.
        void report(const std::string& message, const Compiler::TokenLoc& loc, Type type) override;

        /// Report a file related error
        void report(const std::string& message, Type type) override;

        /// Write all valid identifiers and keywords into mNames and sort them.
        /// \note If mNames is not empty, this function is a no-op.
        /// \note The list may contain duplicates (if a name is a keyword and an identifier at the same
        /// time).
        void listNames();

        void initConsoleHistory();
    };
}
#endif
