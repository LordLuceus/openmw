#ifndef MWGUI_RACE_H
#define MWGUI_RACE_H

#include "windowbase.hpp"
#include <components/esm/refid.hpp>
#include <memory>

#include <MyGUI_KeyCode.h>
#include <MyGUI_Types.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace MWRender
{
    class RaceSelectionPreview;
}

namespace ESM
{
    struct NPC;
}

namespace osg
{
    class Group;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWGui
{
    class RaceDialog : public WindowModal
    {
    public:
        RaceDialog(osg::Group* parent, Resource::ResourceSystem* resourceSystem);

        enum Gender
        {
            GM_Male,
            GM_Female
        };

        const ESM::NPC& getResult() const;
        const ESM::RefId& getRaceId() const { return mCurrentRaceId; }
        Gender getGender() const { return mGenderIndex == 0 ? GM_Male : GM_Female; }

        void setRaceId(const ESM::RefId& raceId);
        void setGender(Gender gender) { mGenderIndex = gender == GM_Male ? 0 : 1; }

        void setNextButtonShow(bool shown);
        void onOpen() override;
        void onClose() override;
        void onFrame(float duration) override;

        bool exit() override { return false; }

        // Events
        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;

        /** Event : Back button clicked.\n
            signature : void method()\n
        */
        EventHandle_Void eventBack;

        /** Event : Dialog finished, OK button clicked.\n
            signature : void method()\n
        */
        EventHandle_WindowBase eventDone;

    protected:
        void onPreviewScroll(MyGUI::Widget* sender, int delta);
        void onHeadRotate(MyGUI::ScrollBar* sender, size_t position);

        void onSelectPreviousGender(MyGUI::Widget* sender);
        void onSelectNextGender(MyGUI::Widget* sender);

        void onSelectPreviousFace(MyGUI::Widget* sender);
        void onSelectNextFace(MyGUI::Widget* sender);

        void onSelectPreviousHair(MyGUI::Widget* sender);
        void onSelectNextHair(MyGUI::Widget* sender);

        void onSelectRace(MyGUI::ListBox* sender, size_t index);
        void onAccept(MyGUI::ListBox* sender, size_t index);

        void onOkClicked(MyGUI::Widget* sender);
        void onBackClicked(MyGUI::Widget* sender);

    private:
        void updateRaces();
        void updateSkills();
        void updateSpellPowers();
        void updatePreview();
        void recountParts();

        // Screen-reader plumbing.
        void hookA11yFocus(MyGUI::Widget* widget, std::string_view label);
        void onA11yFocus(MyGUI::Widget* sender, MyGUI::Widget* oldFocus);
        void onA11yKey(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char ch);
        void moveA11yFocus(int delta);
        void changeFocusedValue(bool next);
        void activateFocused();
        void cycleTooltip(bool forward);
        std::vector<std::string> buildTooltipLines(MyGUI::Widget* widget);
        void rebuildTooltips(MyGUI::Widget* widget);
        size_t tooltipCountFor(MyGUI::Widget* widget);
        void announceWidget(MyGUI::Widget* widget, bool withValue);
        void announceCurrentRace();
        void announceHeadAngle();
        std::string genderLabel() const;
        std::string faceLabel() const;
        std::string hairLabel() const;

        void getBodyParts(int part, std::vector<ESM::RefId>& out);

        osg::Group* mParent;
        Resource::ResourceSystem* mResourceSystem;

        std::vector<ESM::RefId> mAvailableHeads;
        std::vector<ESM::RefId> mAvailableHairs;

        MyGUI::ImageBox* mPreviewImage;
        MyGUI::ListBox* mRaceList;
        // The "Race" header textbox doubles as the keyboard-focus proxy
        // for the race option. We focus this rather than the ListBox so
        // the list's built-in Up/Down selection handling doesn't fight
        // our own arrow-key navigation. The list still shows the
        // current selection visually.
        MyGUI::Widget* mRaceFocusProxy;
        // Row-heading widgets that act as the focusable proxy for each
        // +/- selector. Stored as pointers because findWidget by raw
        // name doesn't work through the Layout name-prefixing.
        MyGUI::Widget* mGenderChoice;
        MyGUI::Widget* mFaceChoice;
        MyGUI::Widget* mHairChoice;
        MyGUI::ScrollBar* mHeadRotate;
        MyGUI::Button* mBackButton;
        MyGUI::Button* mOkButton;

        MyGUI::Widget* mSkillList;
        std::vector<MyGUI::Widget*> mSkillItems;

        MyGUI::Widget* mSpellPowerList;
        std::vector<MyGUI::Widget*> mSpellPowerItems;

        size_t mGenderIndex, mFaceIndex, mHairIndex;

        ESM::RefId mCurrentRaceId;

        float mCurrentAngle;

        std::unique_ptr<MWRender::RaceSelectionPreview> mPreview;
        std::unique_ptr<MyGUI::ITexture> mPreviewTexture;

        bool mPreviewDirty;

        // Resolved accessibility labels per focusable widget. Looked
        // up from onA11yFocus to drive screen-reader announcements.
        std::unordered_map<MyGUI::Widget*, std::string> mA11yLabels;
        // Explicit Up/Down focus order. We drive navigation manually
        // (and disable the built-in spatial nav while open) because the
        // race ListBox natively consumes the arrow keys and the default
        // navigation can't reliably reach the +/- row headings.
        std::vector<MyGUI::Widget*> mA11yFocusOrder;
        // Tooltip lines for the currently focused option. Pressing T
        // cycles through these one at a time (e.g. for a race: the
        // description, each skill bonus, each special with its full
        // effect breakdown). Rebuilt whenever T is pressed on a
        // different widget or the option's value changes.
        std::vector<std::string> mTooltipLines;
        MyGUI::Widget* mTooltipWidget = nullptr;
        size_t mTooltipIndex = 0;

        // Delayed "has X tooltips" hint. When focus rests on a widget
        // for longer than mTooltipHintDelay, we announce how many
        // tooltips it has so the user knows T is worth pressing. Reset
        // whenever focus changes; fires once per focus.
        MyGUI::Widget* mTooltipHintWidget = nullptr;
        float mTooltipHintTimer = 0.f;
        bool mTooltipHintSpoken = false;
        static constexpr float sTooltipHintDelay = 2.f;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        bool onControllerThumbstickEvent(const SDL_ControllerAxisEvent& arg) override;
    };
}
#endif
