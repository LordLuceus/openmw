#ifndef MWGUI_BIRTH_H
#define MWGUI_BIRTH_H

#include "accessibility/screen.hpp"
#include "windowbase.hpp"
#include <components/esm/refid.hpp>

namespace MWGui
{
    class BirthDialog : public WindowModal
    {
    public:
        BirthDialog();

        enum Gender
        {
            GM_Male,
            GM_Female
        };

        const ESM::RefId& getBirthId() const { return mCurrentBirthId; }
        void setBirthId(const ESM::RefId& raceId);

        void setNextButtonShow(bool shown);
        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;

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
        void onSelectBirth(MyGUI::ListBox* sender, size_t index);

        void onAccept(MyGUI::ListBox* sender, size_t index);
        void onOkClicked(MyGUI::Widget* sender);
        void onBackClicked(MyGUI::Widget* sender);

    private:
        void updateBirths();
        void updateSpells();

        void setupAccessibility();
        // Spoken summary of the selected birthsign (name + its abilities,
        // powers and spells), announced as the list option's value.
        std::string birthValue() const;
        // Detailed native-data tooltip lines: the birthsign description plus
        // each ability / power / spell with its full effect breakdown.
        std::vector<std::string> birthTooltips() const;
        // Move the list selection by keyboard (the ListBox is a focus proxy
        // and never receives the arrow keys).
        void changeBirth(bool next);

        MyGUI::ListBox* mBirthList;
        MyGUI::ScrollView* mSpellArea;
        MyGUI::ImageBox* mBirthImage;
        std::vector<MyGUI::Widget*> mSpellItems;
        MyGUI::Button* mBackButton;
        MyGUI::Button* mOkButton;

        ESM::RefId mCurrentBirthId;

        A11y::Screen mA11y;
        // Invisible widget that holds key focus for the birthsign list option,
        // so the native ListBox never receives our navigation arrow keys.
        MyGUI::Widget* mBirthListProxy = nullptr;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
    };
}
#endif
