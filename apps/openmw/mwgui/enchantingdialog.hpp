#ifndef MWGUI_ENCHANTINGDIALOG_H
#define MWGUI_ENCHANTINGDIALOG_H

#include <memory>

#include "accessibility/editfield.hpp"
#include "itemselection.hpp"
#include "spellcreationdialog.hpp"

#include "../mwmechanics/enchanting.hpp"

namespace MWGui
{

    class ItemWidget;

    class EnchantingDialog : public WindowBase, public ReferenceInterface, public EffectEditorBase
    {
    public:
        EnchantingDialog();
        virtual ~EnchantingDialog() = default;

        void onOpen() override;

        void onFrame(float dt) override;
        void clear() override { resetReference(); }

        void setSoulGem(const MWWorld::Ptr& gem);
        void setItem(const MWWorld::Ptr& item);

        /// Actor Ptr: buy enchantment from this actor
        /// Soulgem Ptr: player self-enchant
        void setPtr(const MWWorld::Ptr& ptr) override;

        void resetReference() override;

        std::string_view getWindowIdForLua() const override { return "EnchantingDialog"; }

    protected:
        void onReferenceUnavailable() override;
        void notifyEffectsChanged() override;

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onSelectItem(MyGUI::Widget* sender);
        void onSelectSoul(MyGUI::Widget* sender);

        void onItemSelected(MWWorld::Ptr item);
        void onItemCancel();
        void onSoulSelected(MWWorld::Ptr item);
        void onSoulCancel();
        void onBuyButtonClicked(MyGUI::Widget* sender);
        void updateLabels();
        void onTypeButtonClicked(MyGUI::Widget* sender);
        void onAccept(MyGUI::EditBox* sender);

        std::unique_ptr<ItemSelectionDialog> mItemSelectionDialog;

        MyGUI::Widget* mChanceLayout;

        MyGUI::Button* mCancelButton;
        ItemWidget* mItemBox;
        ItemWidget* mSoulBox;

        MyGUI::Button* mTypeButton;
        MyGUI::Button* mBuyButton;

        MyGUI::EditBox* mName;
        MyGUI::TextBox* mEnchantmentPoints;
        MyGUI::TextBox* mCastCost;
        MyGUI::TextBox* mCharge;
        MyGUI::TextBox* mSuccessChance;
        MyGUI::TextBox* mPrice;
        MyGUI::TextBox* mPriceText;

        MWMechanics::Enchanting mEnchanting;
        ESM::EffectList mEffectList;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

        void onClose() override;

        // NB: no exit() override. Like SpellCreationDialog, this is a NON-modal
        // window: an edit-mode Escape is swallowed by the A11y screen's
        // consumedKey() and never reaches exit(), so a modal-style
        // consumeEscape() latch check would leak and later bounce the player to
        // the main menu. The default WindowBase::exit() is correct here.

        // Screen-reader editing for the enchantment-name box.
        A11y::EditField mNameField;
        // (Re)build the full option list: name, item + soul-gem slots, cast
        // type, the two effect lists, the read-only result stats, then
        // Buy/Create and Cancel. Called at the end of setPtr() and after the
        // used-effects list or any slot changes.
        void buildAccessibility();
        // Tracks whether a child modal (the edit-effect dialog, or an item/soul
        // picker) was open last frame, so onFrame can detect when an effect
        // edit closes and reclaim screen-reader control. (The item/soul pickers
        // suspend/resume our screen themselves; the edit-effect dialog displaces
        // it, which is what this reclaim handles.)
        bool mA11yModalWasOpen = false;
        // Set when the user cycles the cast type via Enter. The handler triggers
        // a full a11y-list rebuild (freeing the executing closure), so it can't
        // safely announce the new style itself; onFrame announces it next frame
        // once the list is stable. See the cast-type option in buildAccessibility.
        bool mA11yAnnounceAfterRebuild = false;
    };

}

#endif
