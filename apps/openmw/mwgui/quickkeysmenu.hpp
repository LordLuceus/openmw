#ifndef MWGUI_QUICKKEYS_H
#define MWGUI_QUICKKEYS_H

#include <memory>

#include "components/esm3/quickkeys.hpp"

#include "../mwworld/manualref.hpp"

#include "accessibility/screen.hpp"
#include "itemselection.hpp"
#include "spellmodel.hpp"
#include "windowbase.hpp"

namespace MWGui
{

    class QuickKeysMenuAssign;
    class MagicSelectionDialog;
    class ItemWidget;
    class SpellView;

    class QuickKeysMenu : public WindowBase
    {
    public:
        QuickKeysMenu();

        void onResChange(int, int) override { center(); }
        void onFrame(float dt) override;

        void onItemButtonClicked(MyGUI::Widget* sender);
        void onMagicButtonClicked(MyGUI::Widget* sender);
        void onUnassignButtonClicked(MyGUI::Widget* sender);
        void onCancelButtonClicked(MyGUI::Widget* sender);

        void onAssignItem(MWWorld::Ptr item);
        void onAssignItemCancel();
        void onAssignMagicItem(MWWorld::Ptr item);
        void onAssignMagic(const ESM::RefId& spellId);
        void onAssignMagicCancel();
        void onOpen() override;
        void onClose() override;

        void activateQuickKey(int index);
        void updateActivatedQuickKey();

        void write(ESM::ESMWriter& writer);
        void readRecord(ESM::ESMReader& reader, uint32_t type);
        void clear() override;

        std::string_view getWindowIdForLua() const override { return "QuickKeys"; }

    private:
        struct keyData
        {
            int index = -1;
            ItemWidget* button = nullptr;
            ESM::QuickKeys::Type type = ESM::QuickKeys::Type::Unassigned;
            ESM::RefId id;
            std::string name;
        };

        std::vector<keyData> mKey;
        std::vector<MWWorld::ManualRef> mTemp;
        keyData* mSelected;
        keyData* mActivated;

        MyGUI::EditBox* mInstructionLabel;
        MyGUI::Button* mOkButton;

        std::unique_ptr<QuickKeysMenuAssign> mAssignDialog;
        std::unique_ptr<ItemSelectionDialog> mItemSelectionDialog;
        std::unique_ptr<MagicSelectionDialog> mMagicSelectionDialog;

        void onQuickKeyButtonClicked(MyGUI::Widget* sender);
        void onOkButtonClicked(MyGUI::Widget* sender);
        // Check if quick key is still valid
        inline void validate(int index);
        void unassign(keyData* key);
        void assignItem(MWWorld::Ptr item);

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        size_t mControllerFocus = 0;

        // --- Accessibility -------------------------------------------------
        // Screen-reader controller. Virtual-focus: an invisible anchor holds
        // MyGUI key focus while the 10 quick-key slots are navigated as
        // widget-less options (the buttons are visual; navigation is internal).
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        // (Re)build the spoken option list from the current slot assignments.
        void buildAccessibility();
        // Spoken label for slot \p index (0-based): "Quick key N, <name>",
        // "Quick key N, empty", or the Hand-to-hand slot.
        std::string a11ySlotLabel(int index) const;
        // Enter handler for a slot: opens the assign chooser (slots 0-8) or
        // announces the fixed Hand-to-hand slot (9).
        void a11yActivateSlot(int index);

    public:
        // Let the assign / picker sub-dialogs suspend & resume this window's
        // screen while they're open (mirrors ItemSelectionDialog's handling of
        // the screen it covers), and read which slot is being assigned so they
        // can announce it.
        A11y::Screen& a11yScreen() { return mA11y; }
        // 1-based number of the slot currently being assigned, or -1 if none.
        int a11ySelectedSlot() const { return mSelected ? mSelected->index : -1; }
    };

    class QuickKeysMenuAssign : public WindowModal
    {
    public:
        QuickKeysMenuAssign(QuickKeysMenu* parent);

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;

    private:
        MyGUI::TextBox* mLabel;
        MyGUI::Button* mItemButton;
        MyGUI::Button* mMagicButton;
        MyGUI::Button* mUnassignButton;
        MyGUI::Button* mCancelButton;

        QuickKeysMenu* mParent;

        // --- Accessibility ---
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        // The parent window's screen, suspended while this modal is open and
        // resumed on close (see ItemSelectionDialog).
        A11y::Screen* mA11yPrev = nullptr;
        void buildAccessibility();

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        size_t mControllerFocus = 0;
    };

    class MagicSelectionDialog : public WindowModal
    {
    public:
        MagicSelectionDialog(QuickKeysMenu* parent);

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;
        bool exit() override;

        void setActiveControllerWindow(bool active) override;

    private:
        MyGUI::Button* mCancelButton;
        SpellView* mMagicList;

        QuickKeysMenu* mParent;

        // --- Accessibility ---
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        A11y::Screen* mA11yPrev = nullptr;
        // Activation is deferred to the first onFrame because the spell model is
        // populated in onOpen, after which buildAccessibility can enumerate it.
        bool mA11yPendingActivate = false;
        void buildAccessibility();

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onModelIndexSelected(SpellModel::ModelIndex index);

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
    };
}

#endif
