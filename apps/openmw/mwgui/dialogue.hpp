#ifndef MWGUI_DIALOGE_H
#define MWGUI_DIALOGE_H

#include <memory>

#include "referenceinterface.hpp"
#include "windowbase.hpp"

#include "bookpage.hpp"

#include "accessibility/screen.hpp"

#include "../mwbase/dialoguemanager.hpp"
#include "../mwdialogue/keywordsearch.hpp"

#include <MyGUI_Delegate.h>

namespace Gui
{
    class AutoSizedTextBox;
    class MWList;
}

namespace MWGui
{
    class DialogueWindow;

    class ResponseCallback : public MWBase::DialogueManager::ResponseCallback
    {
        DialogueWindow* mWindow;
        bool mNeedMargin;

    public:
        ResponseCallback(DialogueWindow* win, bool needMargin = true)
            : mWindow(win)
            , mNeedMargin(needMargin)
        {
        }

        void addResponse(std::string_view title, std::string_view text) override;

        void updateTopics() const;
    };

    class PersuasionDialog : public WindowModal
    {
    public:
        PersuasionDialog(std::unique_ptr<ResponseCallback> callback);

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;

        MyGUI::Widget* getDefaultKeyFocus() override;

        /// Invoked after the dialog closes (cancel or a persuasion attempt) so
        /// the owning DialogueWindow can reclaim screen-reader input. Set by the
        /// DialogueWindow that owns this dialog.
        std::function<void()> mOnClosed;

    protected:
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    private:
        std::unique_ptr<ResponseCallback> mCallback;

        // Screen-reader controller. Real-focus mode: the persuasion buttons are
        // ordinary focusable widgets, so we let MyGUI focus drive navigation.
        A11y::Screen mA11y;
        void buildAccessibility();

        int mInitialGoldLabelWidth;
        int mInitialMainWidgetWidth;

        MyGUI::Button* mCancelButton;
        MyGUI::Button* mAdmireButton;
        MyGUI::Button* mIntimidateButton;
        MyGUI::Button* mTauntButton;
        MyGUI::Button* mBribe10Button;
        MyGUI::Button* mBribe100Button;
        MyGUI::Button* mBribe1000Button;
        MyGUI::Widget* mActionsBox;
        Gui::AutoSizedTextBox* mGoldLabel;

        std::vector<MyGUI::Button*> mButtons;
        size_t mControllerFocus = 0;

        void adjustAction(MyGUI::Widget* action, int& totalHeight);

        void onCancel(MyGUI::Widget* sender);
        void onPersuade(MyGUI::Widget* sender);
    };

    struct Link
    {
        virtual ~Link() = default;
        virtual void activated() = 0;
    };

    struct Topic : Link
    {
        MyGUI::delegates::MultiDelegate<const std::string&> eventTopicActivated;
        Topic(const std::string& id)
            : mTopicId(id)
        {
        }
        std::string mTopicId;
        void activated() override;
    };

    struct Choice : Link
    {
        MyGUI::delegates::MultiDelegate<int> eventChoiceActivated;
        Choice(int id)
            : mChoiceId(id)
        {
        }
        int mChoiceId;
        void activated() override;
    };

    struct Goodbye : Link
    {
        MyGUI::delegates::MultiDelegate<> eventActivated;
        void activated() override;
    };

    struct DialogueText
    {
        virtual ~DialogueText() = default;
        virtual void write(std::shared_ptr<BookTypesetter> typesetter, const MWDialogue::KeywordSearch& keywordSearch,
            std::unordered_map<std::string, std::unique_ptr<Link>>& topicLinks) const = 0;
        std::string mText;
    };

    struct Response : DialogueText
    {
        Response(std::string_view text, std::string_view title = {}, bool needMargin = true);
        void write(std::shared_ptr<BookTypesetter> typesetter, const MWDialogue::KeywordSearch& keywordSearch,
            std::unordered_map<std::string, std::unique_ptr<Link>>& topicLinks) const override;
        std::string mTitle;
        bool mNeedMargin;
    };

    struct Message : DialogueText
    {
        Message(std::string_view text);
        void write(std::shared_ptr<BookTypesetter> typesetter, const MWDialogue::KeywordSearch& keywordSearch,
            std::unordered_map<std::string, std::unique_ptr<Link>>& topicLinks) const override;
    };

    class DialogueWindow : public WindowBase, public ReferenceInterface
    {
    public:
        DialogueWindow();

        void onTradeComplete();

        bool exit() override;

        void notifyLinkClicked(TypesetBook::InteractiveId link);

        void setPtr(const MWWorld::Ptr& actor) override;

        /// @return true if stale keywords were updated successfully
        bool setKeywords(const std::list<std::string>& keyWord);

        void addResponse(std::string_view title, std::string_view text, bool needMargin = true);

        void addMessageBox(std::string_view text);

        void onFrame(float dt) override;
        void clear() override { resetReference(); }

        void updateTopics();

        void onOpen() override;
        void onClose() override;

        std::string_view getWindowIdForLua() const override { return "Dialogue"; }

    protected:
        void updateTopicsPane();
        bool isCompanion(const MWWorld::Ptr& actor);
        bool isCompanion();

        void onSelectListItem(const std::string& topic, int id);
        void onByeClicked(MyGUI::Widget* sender);
        void onMouseWheel(MyGUI::Widget* sender, int rel);
        void onWindowResize(MyGUI::Window* sender);
        void onTopicActivated(const std::string& topicId);
        void onChoiceActivated(int id);
        void onGoodbyeActivated();

        void onScrollbarMoved(MyGUI::ScrollBar* sender, size_t pos);

        void updateHistory(bool scrollbar = false);

        void onReferenceUnavailable() override;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    private:
        void updateDisposition();
        void restock();
        void deleteLater();
        void redrawTopicsList();

        // Rebuild the screen-reader option list to match the window's current
        // state (topics + services + Goodbye normally, or the inline choices
        // when the dialogue manager is awaiting a choice). Called from
        // updateHistory(), the central refresh point.
        void buildAccessibility();
        // Speak the NPC's current disposition toward the player (bound to D).
        // Mirrors the on-screen disposition bar; NPC-only.
        void announceDisposition();

        // True if \p label is a learned dialogue topic keyword that is NOT
        // exhausted (i.e. still has something new to say). Used by the
        // Ctrl+Up/Down jump-to-topic shortcut to skip services, Goodbye, and
        // exhausted topics.
        bool isUnexhaustedTopic(std::string_view label) const;

        bool mIsCompanion;
        std::list<std::string> mKeywords;

        std::vector<std::unique_ptr<DialogueText>> mHistoryContents;
        std::vector<std::pair<std::string, int>> mChoices;
        std::vector<BookTypesetter::Style*> mChoiceStyles;
        bool mGoodbye;

        std::vector<std::unique_ptr<Link>> mLinks;
        std::unordered_map<std::string, std::unique_ptr<Link>> mTopicLinks;

        std::vector<std::unique_ptr<Link>> mDeleteLater;

        MWDialogue::KeywordSearch mKeywordSearch;

        BookPage* mHistory;
        Gui::MWList* mTopicsList;
        MyGUI::ScrollBar* mScrollBar;
        MyGUI::ProgressBar* mDispositionBar;
        MyGUI::TextBox* mDispositionText;
        MyGUI::Button* mGoodbyeButton;

        PersuasionDialog mPersuasionDialog;

        MyGUI::IntSize mCurrentWindowSize;

        std::unique_ptr<ResponseCallback> mCallback;
        std::unique_ptr<ResponseCallback> mGreetingCallback;

        void setControllerFocus(size_t index, bool focused);
        size_t mControllerFocus = 0;
        int mControllerChoice = -1;

        void updateTopicFormat();

        // Screen-reader controller (virtual focus via an invisible anchor; see
        // BookWindow). Rebuilt by buildAccessibility() on every history update.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor;
        // Tracks whether the last buildAccessibility() saw a choice prompt, so
        // we announce the choices only on the transition into choice mode (not
        // on every rebuild, which would talk over navigation).
        bool mA11yWasInChoice = false;
        // Set by onOpen() to re-activate the screen one frame later (in
        // onFrame), rather than immediately. When returning from a sub-mode
        // (barter/training) the window can be revealed and then torn down in
        // the SAME frame (e.g. training finishes: removeGuiMode reveals us,
        // then exitCurrentGuiMode closes us) -- an immediate activate would
        // announce the first topic during teardown. Deferring lets the
        // synchronous onClose cancel it. Fresh opens activate from setPtr and
        // clear this flag, so they still announce immediately.
        bool mA11yPendingActivate = false;
    };
}
#endif
