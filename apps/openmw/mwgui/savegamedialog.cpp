#include "savegamedialog.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <MyGUI_ComboBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_UString.h>

#include <osg/Texture2D>
#include <osgDB/ReadFile>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/files/conversion.hpp>
#include <components/files/memorystream.hpp>
#include <components/l10n/manager.hpp>
#include <components/misc/strings/format.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/misc/timeconvert.hpp>
#include <components/myguiplatform/myguitexture.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/datetimemanager.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/timestamp.hpp"

#include "../mwstate/character.hpp"

#include "accessibility/speech.hpp"
#include "confirmationdialog.hpp"

namespace MWGui
{
    std::string formatTimeplayed(const double timeInSeconds);

    SaveGameDialog::SaveGameDialog()
        : WindowModal("openmw_savegame_dialog.layout")
        , mSaving(true)
        , mCurrentCharacter(nullptr)
        , mCurrentSlot(nullptr)
    {
        getWidget(mScreenshot, "Screenshot");
        getWidget(mCharacterSelection, "SelectCharacter");
        getWidget(mCellName, "CellName");
        getWidget(mInfoText, "InfoText");
        getWidget(mOkButton, "OkButton");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mDeleteButton, "DeleteButton");
        getWidget(mSaveList, "SaveList");
        getWidget(mSaveNameEdit, "SaveNameEdit");
        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SaveGameDialog::onOkButtonClicked);
        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SaveGameDialog::onCancelButtonClicked);
        mDeleteButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SaveGameDialog::onDeleteButtonClicked);
        mCharacterSelection->eventComboChangePosition += MyGUI::newDelegate(this, &SaveGameDialog::onCharacterSelected);
        mCharacterSelection->eventComboAccept += MyGUI::newDelegate(this, &SaveGameDialog::onCharacterAccept);
        mSaveList->eventListChangePosition += MyGUI::newDelegate(this, &SaveGameDialog::onSlotSelected);
        mSaveList->eventListMouseItemActivate += MyGUI::newDelegate(this, &SaveGameDialog::onSlotMouseClick);
        mSaveList->eventListSelectAccept += MyGUI::newDelegate(this, &SaveGameDialog::onSlotActivated);
        mSaveList->eventKeyButtonPressed += MyGUI::newDelegate(this, &SaveGameDialog::onKeyButtonPressed);
        mSaveNameEdit->eventEditSelectAccept += MyGUI::newDelegate(this, &SaveGameDialog::onEditSelectAccept);
        mSaveNameEdit->eventEditTextChange += MyGUI::newDelegate(this, &SaveGameDialog::onSaveNameChanged);

        // To avoid accidental deletions
        mDeleteButton->setNeedKeyFocus(false);

        mControllerButtons.mA = "#{Interface:Select}";
        mControllerButtons.mB = "#{Interface:Cancel}";

        // Screen-reader setup: an invisible anchor owns key focus so the native
        // ListBox / ComboBox / EditBox can't grab the arrow keys. ownModal=true
        // because this dialog is itself a WindowModal (a virtual screen would
        // otherwise yield to its own modal and go deaf to the arrows). Child
        // confirmation dialogs (overwrite / load / delete) suspend us on open and
        // resume us on close, so they get the keys while shown. Options are
        // rebuilt by buildAccessibility() each time the contents change.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor, /*ownModal=*/true);
        mNameField.attach(mSaveNameEdit);
        mNameField.setActive(false);

        // Delete key removes the focused save (mirrors the native list's Delete
        // shortcut). This is the only way to delete an arbitrary slot: the Delete
        // button always targets whatever slot navigation last dragged the native
        // selection onto, and Enter on a slot loads/overwrites it.
        mA11y.setExtraKeyHandler([this](MyGUI::KeyCode key) -> bool {
            if (key != MyGUI::KeyCode::Delete)
                return false;
            const size_t cur = mA11y.currentIndex();
            if (cur == A11y::Screen::npos || cur < mA11ySlotBase)
                return false; // not on a slot (e.g. the name field or a button)
            const size_t slotIndex = cur - mA11ySlotBase;
            if (slotIndex >= mSaveList->getItemCount())
                return false;
            // Point the native selection + mCurrentSlot at the focused slot so
            // confirmDeleteSave() targets and names the right one, then confirm.
            a11ySelectSlot(slotIndex);
            if (mCurrentSlot)
                confirmDeleteSave();
            return true;
        });
    }

    void SaveGameDialog::onSlotActivated(MyGUI::ListBox* sender, size_t pos)
    {
        onSlotSelected(sender, pos);
        accept();
    }

    void SaveGameDialog::onSlotMouseClick(MyGUI::ListBox* sender, size_t pos)
    {
        onSlotSelected(sender, pos);

        if (pos != MyGUI::ITEM_NONE && MyGUI::InputManager::getInstance().isShiftPressed())
            confirmDeleteSave();
    }

    void SaveGameDialog::confirmDeleteSave()
    {
        ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
        // Name the save in the prompt so a screen-reader user knows exactly which
        // one they're about to delete (the bare "delete this game?" gives no clue
        // which slot is targeted). mCurrentSlot is the slot focus last landed on.
        std::string message = "#{OMWEngine:DeleteGameConfirmation}";
        if (mCurrentSlot)
        {
            const std::string name = mCurrentSlot->mProfile.mDescription;
            if (!name.empty())
                message = "#{OMWEngine:DeleteGameConfirmation} (" + name + ")";
        }
        dialog->askForConfirmation(message);
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &SaveGameDialog::onDeleteSlotConfirmed);
        dialog->eventCancelClicked.clear();
        dialog->eventCancelClicked += MyGUI::newDelegate(this, &SaveGameDialog::onDeleteSlotCancel);
    }

    void SaveGameDialog::onDeleteSlotConfirmed()
    {
        MWBase::Environment::get().getStateManager()->deleteGame(mCurrentCharacter, mCurrentSlot);
        mSaveList->removeItemAt(mSaveList->getIndexSelected());
        onSlotSelected(mSaveList, mSaveList->getIndexSelected());
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mSaveList);

        if (mSaveList->getItemCount() == 0)
        {
            size_t previousIndex = mCharacterSelection->getIndexSelected();
            mCurrentCharacter = nullptr;
            mCharacterSelection->removeItemAt(previousIndex);
            if (mCharacterSelection->getItemCount())
            {
                size_t nextCharacter = std::min(previousIndex, mCharacterSelection->getItemCount() - 1);
                mCharacterSelection->setIndexSelected(nextCharacter);
                onCharacterSelected(mCharacterSelection, nextCharacter);
            }
            else
                mCharacterSelection->setIndexSelected(MyGUI::ITEM_NONE);
        }

        // The slot list (and possibly the character list) changed under us. The
        // ConfirmationDialog has already resumed our screen; rebuild the options
        // and announce the deletion plus wherever focus now lands.
        const size_t remaining = mSaveList->getItemCount();
        buildAccessibility();
        A11y::say(remaining == 0 ? "Save deleted. No saves remaining."
                                 : "Save deleted.",
            /*interrupt=*/true);
        mA11y.focusFirst(/*announce=*/true);
    }

    void SaveGameDialog::onDeleteSlotCancel()
    {
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mSaveList);
    }

    void SaveGameDialog::onSaveNameChanged(MyGUI::EditBox* sender)
    {
        // This might have previously been a save slot from the list. If so, that is no longer the case
        mSaveList->setIndexSelected(MyGUI::ITEM_NONE);
        onSlotSelected(mSaveList, MyGUI::ITEM_NONE);
    }

    void SaveGameDialog::onEditSelectAccept(MyGUI::EditBox* sender)
    {
        accept();

        // To do not spam onEditSelectAccept() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void SaveGameDialog::onClose()
    {
        mA11y.deactivate();
        mSaveList->setIndexSelected(MyGUI::ITEM_NONE);

        WindowModal::onClose();
    }

    void SaveGameDialog::onOpen()
    {
        WindowModal::onOpen();

        mSaveNameEdit->setCaption({});
        if (Settings::gui().mControllerMenus && mSaving)
        {
            // For controller mode, set a default save file name. The format is
            // "Day 24 - Last Steed 7 p.m."
            const MWWorld::DateTimeManager& timeManager = *MWBase::Environment::get().getWorld()->getTimeManager();
            std::string_view month = timeManager.getMonthName();
            int hour = static_cast<int>(timeManager.getTimeStamp().getHour());
            bool pm = hour >= 12;
            if (hour >= 13)
                hour -= 12;
            if (hour == 0)
                hour = 12;

            ESM::EpochTimeStamp currentDate = timeManager.getEpochTimeStamp();
            std::string daysPassed
                = Misc::StringUtils::format("#{Calendar:day} %i", timeManager.getTimeStamp().getDay());
            std::string_view formattedHour(pm ? "#{Calendar:pm}" : "#{Calendar:am}");
            std::string autoFilename = Misc::StringUtils::format(
                "%s - %i %s %i %s", daysPassed, currentDate.mDay, month, hour, formattedHour);

            mSaveNameEdit->setCaptionWithReplacing(autoFilename);
        }
        if (mSaving)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mSaveNameEdit);
        else
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mSaveList);

        center();

        mCharacterSelection->setCaption({});
        mCharacterSelection->removeAllItems();
        mCurrentCharacter = nullptr;
        mCurrentSlot = nullptr;
        mSaveList->removeAllItems();
        onSlotSelected(mSaveList, MyGUI::ITEM_NONE);

        if (Settings::gui().mControllerMenus)
        {
            mOkButtonFocus = true;
            mOkButton->setStateSelected(true);
            mCancelButton->setStateSelected(false);
        }

        MWBase::StateManager* mgr = MWBase::Environment::get().getStateManager();
        if (mgr->characterBegin() == mgr->characterEnd())
            return;

        mCurrentCharacter = mgr->getCurrentCharacter();

        const std::string& directory = Settings::saves().mCharacter;

        size_t selectedIndex = MyGUI::ITEM_NONE;

        for (MWBase::StateManager::CharacterIterator it = mgr->characterBegin(); it != mgr->characterEnd(); ++it)
        {
            if (it->begin() != it->end())
            {
                const ESM::SavedGame& signature = it->getSignature();

                std::stringstream title;
                title << signature.mPlayerName;

                // For a custom class, we will not find it in the store (unless we loaded the savegame first).
                // Fall back to name stored in savegame header in that case.
                std::string_view className;
                if (signature.mPlayerClassId.empty())
                    className = signature.mPlayerClassName;
                else
                {
                    // Find the localised name for this class from the store
                    const ESM::Class* playerClass
                        = MWBase::Environment::get().getESMStore()->get<ESM::Class>().search(signature.mPlayerClassId);
                    if (playerClass)
                        className = playerClass->mName;
                    else
                        className = "?"; // From an older savegame format that did not support custom classes properly.
                }

                title << " (#{OMWEngine:Level} " << signature.mPlayerLevel << " "
                      << MyGUI::TextIterator::toTagsString(MyGUI::UString(className)) << ")";

                const MyGUI::UString playerDesc = MyGUI::LanguageManager::getInstance().replaceTags(title.str());
                mCharacterSelection->addItem(playerDesc, &*it);

                if (mCurrentCharacter == &*it
                    || (!mCurrentCharacter && !mSaving
                        && Misc::StringUtils::ciEqual(directory, Files::pathToUnicodeString(it->getPath().filename()))))
                {
                    mCurrentCharacter = &*it;
                    selectedIndex = mCharacterSelection->getItemCount() - 1;
                }
            }
        }

        if (selectedIndex == MyGUI::ITEM_NONE && !mSaving && mCharacterSelection->getItemCount() != 0)
        {
            selectedIndex = 0;
            mCurrentCharacter = *mCharacterSelection->getItemDataAt<const MWState::Character*>(0);
        }
        mCharacterSelection->setIndexSelected(selectedIndex);
        if (selectedIndex == MyGUI::ITEM_NONE)
            mCharacterSelection->setCaptionWithReplacing("#{OMWEngine:SelectCharacter}");

        fillSaveList();

        // Screen reader: build the option list (mirrors the native controls),
        // announce the dialog's purpose, then activate -- activate() lands on and
        // announces the first option (the save-name field when saving, the first
        // save when loading), so the lead-in must be said first.
        buildAccessibility();
        announceOnOpen();
        mA11y.activate();
    }

    void SaveGameDialog::announceOnOpen()
    {
        // No native title widget exists, so compose the dialog's purpose here.
        // Said before activate() so it precedes the first option's announcement.
        std::string intro = mSaving ? "Save game." : "Load game.";
        // When loading with nothing to load, say so -- otherwise focus just lands
        // silently on Cancel with no hint that the list is empty.
        if (!mSaving && mSaveList->getItemCount() == 0)
            intro += " No saved games.";
        A11y::say(intro, /*interrupt=*/true);
    }

    void SaveGameDialog::onFrame(float dt)
    {
        if (mA11yRebuildPending)
        {
            mA11yRebuildPending = false;
            // Rebuild after a character change refilled the slot list. Keep focus
            // on the character selector (the value the user is cycling) without
            // re-announcing -- changeValue already spoke the new character name.
            buildAccessibility();
            mA11y.selectByLabel("#{OMWEngine:LoadingSelectCharacter}", /*announce=*/false);
        }

        mNameField.onFrame();
        mA11y.onFrame(dt);
    }

    const MWState::Slot* SaveGameDialog::slotAt(size_t index) const
    {
        if (!mCurrentCharacter)
            return nullptr;
        size_t i = 0;
        for (MWState::Character::SlotIterator it = mCurrentCharacter->begin(); it != mCurrentCharacter->end();
             ++it, ++i)
        {
            if (i == index)
                return &*it;
        }
        return nullptr;
    }

    std::string SaveGameDialog::slotOptionText(size_t index)
    {
        // In LOAD mode, focusing a slot drives the native selection so the
        // on-screen info panel + screenshot follow and mCurrentSlot becomes the
        // load target (the framework only calls describe() on focus, with no
        // separate onFocus hook). In SAVE mode we must NOT do this: onSlotSelected
        // overwrites the save-name edit box with the slot's name, so just arrowing
        // past slots would clobber a name the user typed. There we describe the
        // slot read-only and only commit it as an overwrite target on Enter.
        const MWState::Slot* slot = mSaving ? slotAt(index) : (a11ySelectSlot(index), mCurrentSlot);

        // Compose a clean spoken description directly from the slot profile rather
        // than scraping the on-screen info panel -- the panel runs everything
        // together and reads poorly aloud. Same facts as on screen, as short
        // clauses: save name, then (when it differs) the character, level, cell,
        // health, in-game day, time played, real-world save time, in-game time.
        std::string text = mSaveList->getItemNameAt(index);
        if (!slot)
            return text;

        const ESM::SavedGame& p = slot->mProfile;
        std::vector<std::string> parts;

        // The character's own name, shown (as on screen) only when it differs
        // from the one selected in the character list -- i.e. a save belonging to
        // a renamed/other character surfaced under this heading.
        const size_t profileIndex = mCharacterSelection->getIndexSelected();
        if (profileIndex != MyGUI::ITEM_NONE)
        {
            const ESM::SavedGame& heading
                = (*mCharacterSelection->getItemDataAt<const MWState::Character*>(profileIndex))->getSignature();
            if (p.mPlayerName != heading.mPlayerName && !p.mPlayerName.empty())
                parts.push_back(p.mPlayerName);
        }

        parts.push_back("#{OMWEngine:Level} " + std::to_string(p.mPlayerLevel));

        if (!p.mPlayerCellName.empty())
            parts.push_back("#{sCell=" + p.mPlayerCellName + "}");

        if (p.mMaximumHealth > 0)
            parts.push_back("#{OMWEngine:Health} " + std::to_string(static_cast<int>(p.mCurrentHealth)) + " of "
                + std::to_string(static_cast<int>(p.mMaximumHealth)));

        if (p.mCurrentDay > 0)
            parts.push_back("#{Calendar:day} " + std::to_string(p.mCurrentDay));

        if (p.mTimePlayed > 0)
            parts.push_back("#{OMWEngine:TimePlayed}: " + formatTimeplayed(p.mTimePlayed));

        // Real-world time the save was written (labelled so it isn't confused
        // with the in-game date), e.g. "Saved 2026.06.09 19:18:09".
        parts.push_back("Saved " + Misc::fileTimeToString(slot->mTimeStamp, "%Y.%m.%d %T"));

        // In-game date and time, e.g. "21 Last Seed, 7 p.m." -- ordered LAST
        // because the a.m./p.m. tag resolves to a string ending in a period, and
        // it's the only clause that does. Keeping it last means no following ". "
        // separator produces a double period (the tag is still unresolved here,
        // so a back()=='.' check can't catch it -- the raw text ends in '}').
        int hour = static_cast<int>(p.mInGameTime.mGameHour);
        const bool pm = hour >= 12;
        if (hour >= 13)
            hour -= 12;
        if (hour == 0)
            hour = 12;
        std::string when = std::to_string(p.mInGameTime.mDay) + " "
            + std::string(MWBase::Environment::get().getWorld()->getTimeManager()->getMonthName(p.mInGameTime.mMonth))
            + ", " + std::to_string(hour) + " " + (pm ? "#{Calendar:pm}" : "#{Calendar:am}");
        parts.push_back(std::move(when));

        for (const std::string& part : parts)
            text += ". " + part;
        return text;
    }

    void SaveGameDialog::a11ySelectSlot(size_t index)
    {
        // Drive the native list selection so the info panel + screenshot update,
        // exactly as a mouse click would, before the option is announced.
        mSaveList->setIndexSelected(index);
        onSlotSelected(mSaveList, index);
    }

    void SaveGameDialog::a11yActivateSlot(size_t index)
    {
        a11ySelectSlot(index);
        accept();
    }

    void SaveGameDialog::a11yCycleCharacter(bool next)
    {
        const size_t count = mCharacterSelection->getItemCount();
        if (count == 0)
            return;
        size_t index = mCharacterSelection->getIndexSelected();
        if (index == MyGUI::ITEM_NONE)
            index = 0;
        else
            index = wrap(index, count, next ? 1 : -1);
        mCharacterSelection->setIndexSelected(index);
        onCharacterSelected(mCharacterSelection, index);
        // The framework's changeValue() speaks the option's value() (the new
        // character name) right after this returns, so DON'T announce here -- that
        // would double up. The slot list was refilled, so the option list is now
        // stale; defer its rebuild to the next frame (the framework still holds a
        // pointer into the current element vector, so rebuilding now is UB).
        mA11yRebuildPending = true;
    }

    void SaveGameDialog::buildAccessibility()
    {
        mA11y.clear();

        if (mSaving)
        {
            // Save mode: type a name (Enter edits the box; Up/Down leave it), or
            // pick an existing slot to overwrite, then OK / Cancel.
            mA11y.add({ .widget = mSaveNameEdit, .label = "Save name",
                .value =
                    [this] {
                        const std::string text = mSaveNameEdit->getOnlyText().asUTF8();
                        return text.empty() ? std::string("blank") : text;
                    },
                .edit = &mNameField });
        }
        else if (mCharacterSelection->getItemCount() > 0)
        {
            // Load mode: a character selector (Left/Right cycles characters,
            // refilling the slot list) precedes the slots.
            mA11y.add({ .widget = mCharacterSelection,
                .label = "#{OMWEngine:LoadingSelectCharacter}",
                .value = [this] { return mCharacterSelection->getCaption().asUTF8(); },
                .change = [this](bool next) { a11yCycleCharacter(next); } });
        }

        // The save slots. Each is a widget-less option (the native ListBox draws
        // them); selecting one drives the native selection so the on-screen info
        // panel + screenshot follow. Enter loads (load mode) or selects-to-
        // overwrite then confirms (save mode). The Delete KEY (handled below)
        // removes the focused slot -- needed because navigating to the Delete
        // button drags the native selection to the last slot, so the button alone
        // can't target an arbitrary slot. mA11ySlotBase records where slots start
        // so the key handler can map the focused option onto a slot index.
        mA11ySlotBase = mA11y.size();
        for (size_t i = 0; i < mSaveList->getItemCount(); ++i)
        {
            mA11y.add({ .widget = nullptr,
                .describe = [this, i] { return slotOptionText(i); },
                .activate = [this, i] { a11yActivateSlot(i); } });
        }

        if (mDeleteButton->getVisible())
            mA11y.add({ .widget = mDeleteButton, .label = "#{OMWEngine:DeleteGame}",
                .activate =
                    [this] {
                        // Guard against deleting with no slot selected: rather than
                        // silently doing nothing (onDeleteButtonClicked's guard) or
                        // prompting with no named target, say there's nothing to
                        // delete. confirmDeleteSave names the slot in its prompt.
                        if (mCurrentSlot)
                            confirmDeleteSave();
                        else
                            A11y::say("No save selected to delete.", /*interrupt=*/true);
                    } });

        mA11y.add({ .widget = mOkButton, .label = "#{Interface:OK}",
            .activate = [this] { onOkButtonClicked(mOkButton); } });
        mA11y.add({ .widget = mCancelButton, .label = "#{Interface:Cancel}",
            .activate = [this] { onCancelButtonClicked(mCancelButton); } });
    }

    void SaveGameDialog::setLoadOrSave(bool load)
    {
        mSaving = !load;
        mSaveNameEdit->setVisible(!load);
        mCharacterSelection->setUserString("Hidden", load ? "false" : "true");
        mCharacterSelection->setVisible(load);

        mDeleteButton->setUserString("Hidden", load ? "false" : "true");
        mDeleteButton->setVisible(load);

        if (!load)
        {
            mCurrentCharacter = MWBase::Environment::get().getStateManager()->getCurrentCharacter();
        }

        center();
    }

    bool SaveGameDialog::exit()
    {
        // The Escape that leaves text-edit mode on the save-name field must not
        // also close the dialog. Swallow exactly that Escape; an Escape pressed
        // while merely navigating still cancels the dialog as usual.
        if (mA11y.inEditMode() || mA11y.consumeEditModeEscape())
            return false;
        return true;
    }

    void SaveGameDialog::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        setVisible(false);
    }

    void SaveGameDialog::onDeleteButtonClicked(MyGUI::Widget* /*sender*/)
    {
        if (mCurrentSlot)
            confirmDeleteSave();
    }

    void SaveGameDialog::onConfirmationGiven()
    {
        accept(true);
    }

    void SaveGameDialog::onConfirmationCancel()
    {
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mSaveList);
    }

    void SaveGameDialog::accept(bool reallySure)
    {
        if (mSaving)
        {
            // If overwriting an existing slot, ask for confirmation first
            if (mCurrentSlot != nullptr && !reallySure)
            {
                ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
                dialog->askForConfirmation("#{OMWEngine:OverwriteGameConfirmation}");
                dialog->eventOkClicked.clear();
                dialog->eventOkClicked += MyGUI::newDelegate(this, &SaveGameDialog::onConfirmationGiven);
                dialog->eventCancelClicked.clear();
                dialog->eventCancelClicked += MyGUI::newDelegate(this, &SaveGameDialog::onConfirmationCancel);
                return;
            }
            if (mSaveNameEdit->getCaption().empty())
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{OMWEngine:EmptySaveNameError}");
                return;
            }
        }
        else
        {
            MWBase::StateManager::State state = MWBase::Environment::get().getStateManager()->getState();

            // If game is running, ask for confirmation first
            if (state == MWBase::StateManager::State_Running && !reallySure)
            {
                ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
                dialog->askForConfirmation("#{OMWEngine:LoadGameConfirmation}");
                dialog->eventOkClicked.clear();
                dialog->eventOkClicked += MyGUI::newDelegate(this, &SaveGameDialog::onConfirmationGiven);
                dialog->eventCancelClicked.clear();
                dialog->eventCancelClicked += MyGUI::newDelegate(this, &SaveGameDialog::onConfirmationCancel);
                return;
            }
        }

        setVisible(false);
        MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_MainMenu);

        if (mSaving)
        {
            MWBase::Environment::get().getStateManager()->saveGame(mSaveNameEdit->getCaption(), mCurrentSlot);
        }
        else
        {
            assert(mCurrentCharacter && mCurrentSlot);
            MWBase::Environment::get().getStateManager()->loadGame(mCurrentCharacter, mCurrentSlot->mPath);
        }
    }

    void SaveGameDialog::onKeyButtonPressed(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char character)
    {
        if (key == MyGUI::KeyCode::Delete && mCurrentSlot)
            confirmDeleteSave();
    }

    void SaveGameDialog::onOkButtonClicked(MyGUI::Widget* /*sender*/)
    {
        accept();
    }

    void SaveGameDialog::onCharacterSelected(MyGUI::ComboBox* sender, size_t pos)
    {
        const MWState::Character* character = *mCharacterSelection->getItemDataAt<const MWState::Character*>(pos);

        mCurrentCharacter = character;
        mCurrentSlot = nullptr;
        fillSaveList();
    }

    void SaveGameDialog::onCharacterAccept(MyGUI::ComboBox* sender, size_t pos)
    {
        // Give key focus to save list so we can confirm the selection with Enter
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mSaveList);
    }

    void SaveGameDialog::fillSaveList()
    {
        mSaveList->removeAllItems();
        if (!mCurrentCharacter)
            return;
        for (MWState::Character::SlotIterator it = mCurrentCharacter->begin(); it != mCurrentCharacter->end(); ++it)
        {
            mSaveList->addItem(it->mProfile.mDescription);
        }
        // When loading, Auto-select the first save, if there is one
        if (mSaveList->getItemCount() && !mSaving)
        {
            mSaveList->setIndexSelected(0);
            onSlotSelected(mSaveList, 0);
        }
        else
            onSlotSelected(mSaveList, MyGUI::ITEM_NONE);
    }

    std::string formatTimeplayed(const double timeInSeconds)
    {
        auto l10n = MWBase::Environment::get().getL10nManager()->getContext("Interface");
        int duration = static_cast<int>(timeInSeconds);
        if (duration <= 0)
            return l10n->formatMessage("DurationSecond", { "seconds" }, { 0 });

        std::string result;
        int hours = duration / 3600;
        int minutes = (duration / 60) % 60;
        int seconds = duration % 60;
        if (hours)
            result += l10n->formatMessage("DurationHour", { "hours" }, { hours });
        if (minutes)
            result += l10n->formatMessage("DurationMinute", { "minutes" }, { minutes });
        if (seconds)
            result += l10n->formatMessage("DurationSecond", { "seconds" }, { seconds });

        return result;
    }

    void SaveGameDialog::onSlotSelected(MyGUI::ListBox* sender, size_t pos)
    {
        mOkButton->setEnabled(pos != MyGUI::ITEM_NONE || mSaving);
        mDeleteButton->setEnabled(pos != MyGUI::ITEM_NONE);

        if (pos == MyGUI::ITEM_NONE || !mCurrentCharacter)
        {
            mCurrentSlot = nullptr;
            mCellName->setCaption({});
            mInfoText->setCaption({});
            mScreenshot->setImageTexture({});
            return;
        }

        if (mSaving)
            mSaveNameEdit->setCaption(sender->getItemNameAt(pos));

        mCurrentSlot = nullptr;
        size_t i = 0;
        for (MWState::Character::SlotIterator it = mCurrentCharacter->begin(); it != mCurrentCharacter->end();
             ++it, ++i)
        {
            if (i == pos)
                mCurrentSlot = &*it;
        }
        if (!mCurrentSlot)
            throw std::runtime_error("Can't find selected slot");

        std::stringstream text;

        const size_t profileIndex = mCharacterSelection->getIndexSelected();
        const std::string& slotPlayerName = mCurrentSlot->mProfile.mPlayerName;
        const ESM::SavedGame& profileSavedGame
            = (*mCharacterSelection->getItemDataAt<const MWState::Character*>(profileIndex))->getSignature();
        if (slotPlayerName != profileSavedGame.mPlayerName)
            text << slotPlayerName << "\n";

        text << "#{OMWEngine:Level} " << mCurrentSlot->mProfile.mPlayerLevel << "\n";

        if (mCurrentSlot->mProfile.mCurrentDay > 0)
            text << "#{Calendar:day} " << mCurrentSlot->mProfile.mCurrentDay << "\n";

        if (mCurrentSlot->mProfile.mMaximumHealth > 0)
            text << "#{OMWEngine:Health} " << static_cast<int>(mCurrentSlot->mProfile.mCurrentHealth) << "/"
                 << static_cast<int>(mCurrentSlot->mProfile.mMaximumHealth) << "\n";

        int hour = int(mCurrentSlot->mProfile.mInGameTime.mGameHour);
        bool pm = hour >= 12;
        if (hour >= 13)
            hour -= 12;
        if (hour == 0)
            hour = 12;

        text << mCurrentSlot->mProfile.mInGameTime.mDay << " "
             << MWBase::Environment::get().getWorld()->getTimeManager()->getMonthName(
                    mCurrentSlot->mProfile.mInGameTime.mMonth)
             << " " << hour << " " << (pm ? "#{Calendar:pm}" : "#{Calendar:am}") << "\n";

        if (mCurrentSlot->mProfile.mTimePlayed > 0)
        {
            text << "#{OMWEngine:TimePlayed}: " << formatTimeplayed(mCurrentSlot->mProfile.mTimePlayed) << "\n";
        }

        text << Misc::fileTimeToString(mCurrentSlot->mTimeStamp, "%Y.%m.%d %T") << "\n";

        mCellName->setCaptionWithReplacing("#{sCell=" + mCurrentSlot->mProfile.mPlayerCellName + "}");
        mInfoText->setCaptionWithReplacing(text.str());

        // Reset the image for the case we're unable to recover a screenshot
        mScreenshotTexture.reset();
        mScreenshot->setRenderItemTexture(nullptr);
        // The widget is Y-down, the screenshot is Y-up, so this UV is inverted
        mScreenshot->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 1.f, 1.f, 0.f));

        // Decode screenshot
        const std::vector<char>& data = mCurrentSlot->mProfile.mScreenshot;
        if (!data.size())
        {
            Log(Debug::Warning) << "Selected save file '" << Files::pathToUnicodeString(mCurrentSlot->mPath.filename())
                                << "' has no savegame screenshot";
            return;
        }

        Files::IMemStream instream(data.data(), data.size());

        osgDB::ReaderWriter* readerwriter = osgDB::Registry::instance()->getReaderWriterForExtension("jpg");
        if (!readerwriter)
        {
            Log(Debug::Error) << "Can't open savegame screenshot, no jpg readerwriter found";
            return;
        }

        osgDB::ReaderWriter::ReadResult result = readerwriter->readImage(instream);
        if (!result.success())
        {
            Log(Debug::Error) << "Failed to read savegame screenshot: " << result.message() << " code "
                              << result.status();
            return;
        }

        osg::ref_ptr<osg::Texture2D> texture(new osg::Texture2D);
        texture->setImage(result.getImage());
        texture->setInternalFormat(GL_RGB);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setResizeNonPowerOfTwoHint(false);
        texture->setUnRefImageDataAfterApply(true);

        mScreenshotTexture = std::make_unique<MyGUIPlatform::OSGTexture>(texture);
        mScreenshot->setRenderItemTexture(mScreenshotTexture.get());
    }

    ControllerButtons* SaveGameDialog::getControllerButtons()
    {
        mControllerButtons.mY = mSaving ? "" : "#{OMWEngine:LoadingSelectCharacter}";
        return &mControllerButtons;
    }

    bool SaveGameDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            if (mOkButtonFocus)
                onOkButtonClicked(mOkButton);
            else
                onCancelButtonClicked(mCancelButton);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            onCancelButtonClicked(mCancelButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_Y)
        {
            size_t index = mCharacterSelection->getIndexSelected();
            index = wrap(index, mCharacterSelection->getItemCount(), 1);
            mCharacterSelection->setIndexSelected(index);
            onCharacterSelected(mCharacterSelection, index);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
        {
            MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
            winMgr->setKeyFocusWidget(mSaveList);
            winMgr->injectKeyPress(MyGUI::KeyCode::ArrowUp, 0, false);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
        {
            MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
            winMgr->setKeyFocusWidget(mSaveList);
            winMgr->injectKeyPress(MyGUI::KeyCode::ArrowDown, 0, false);
        }
        else if ((arg.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && !mOkButtonFocus)
            || (arg.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && mOkButtonFocus))
        {
            mOkButtonFocus = !mOkButtonFocus;
            mOkButton->setStateSelected(mOkButtonFocus);
            mCancelButton->setStateSelected(!mOkButtonFocus);
        }

        return true;
    }
}
