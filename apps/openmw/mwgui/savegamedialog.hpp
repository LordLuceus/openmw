#ifndef OPENMW_MWGUI_SAVEGAMEDIALOG_H
#define OPENMW_MWGUI_SAVEGAMEDIALOG_H

#include <memory>

#include "windowbase.hpp"

#include "accessibility/editfield.hpp"
#include "accessibility/screen.hpp"

namespace MWState
{
    class Character;
    struct Slot;
}

namespace MWGui
{

    class SaveGameDialog : public MWGui::WindowModal
    {
    public:
        SaveGameDialog();

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;
        bool exit() override;

        void setLoadOrSave(bool load);

        ControllerButtons* getControllerButtons() override;

    private:
        void confirmDeleteSave();

        // Screen-reader support. The dialog is a WindowModal built on native
        // ListBox / ComboBox / EditBox widgets; we drive it via a virtual-focus
        // Screen (ownModal) whose options mirror those controls. buildAccessibility
        // rebuilds the option list whenever the contents change (character chosen,
        // slot list refilled, save deleted). a11ySelectSlot / a11yCycleCharacter
        // route arrow/Enter navigation back into the native handlers so selection,
        // info text and screenshots stay consistent with the mouse path.
        void buildAccessibility();
        // The save slot at \p index in the current character's list, or null.
        // Read-only: unlike a11ySelectSlot it has no native-selection side
        // effects (used to describe a slot in save mode without overwriting the
        // typed name / arming an overwrite target).
        const MWState::Slot* slotAt(size_t index) const;
        // Not const: in load mode focusing a slot drives the native selection
        // (updating the info panel + screenshot) before composing the spoken
        // description.
        std::string slotOptionText(size_t index);
        void a11ySelectSlot(size_t index);
        void a11yActivateSlot(size_t index);
        void a11yCycleCharacter(bool next);
        void announceOnOpen();

        A11y::Screen mA11y;
        A11y::EditField mNameField;
        MyGUI::Widget* mA11yAnchor = nullptr;
        // Set when cycling characters changes the slot list: the option list must
        // be rebuilt, but NOT from inside the value-change handler (the framework
        // still holds a pointer into the old element vector after change()). The
        // rebuild is deferred to the next onFrame. See a11yCycleCharacter.
        bool mA11yRebuildPending = false;
        // The a11y option index of the first save slot (slots follow the optional
        // leading option: the name field in save mode, the character selector in
        // load mode). Used to map the focused option back to a slot index for the
        // Delete-key handler.
        size_t mA11ySlotBase = 0;

        void onKeyButtonPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character);
        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onOkButtonClicked(MyGUI::Widget* sender);
        void onDeleteButtonClicked(MyGUI::Widget* sender);
        void onCharacterSelected(MyGUI::ComboBox* sender, size_t pos);
        void onCharacterAccept(MyGUI::ComboBox* sender, size_t pos);
        // Slot selected (mouse click or arrow keys)
        void onSlotSelected(MyGUI::ListBox* sender, size_t pos);
        // Slot activated (double click or enter key)
        void onSlotActivated(MyGUI::ListBox* sender, size_t pos);
        // Slot clicked with mouse
        void onSlotMouseClick(MyGUI::ListBox* sender, size_t pos);

        void onDeleteSlotConfirmed();
        void onDeleteSlotCancel();

        void onEditSelectAccept(MyGUI::EditBox* sender);
        void onSaveNameChanged(MyGUI::EditBox* sender);
        void onConfirmationGiven();
        void onConfirmationCancel();

        void accept(bool reallySure = false);

        void fillSaveList();

        std::unique_ptr<MyGUI::ITexture> mScreenshotTexture;
        MyGUI::ImageBox* mScreenshot;
        bool mSaving;

        MyGUI::ComboBox* mCharacterSelection;
        MyGUI::EditBox* mCellName;
        MyGUI::EditBox* mInfoText;
        MyGUI::Button* mOkButton;
        MyGUI::Button* mCancelButton;
        MyGUI::Button* mDeleteButton;
        MyGUI::ListBox* mSaveList;
        MyGUI::EditBox* mSaveNameEdit;

        const MWState::Character* mCurrentCharacter;
        const MWState::Slot* mCurrentSlot;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        bool mOkButtonFocus = true;
    };

}

#endif
