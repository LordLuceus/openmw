#ifndef MWGUI_SPELLWINDOW_H
#define MWGUI_SPELLWINDOW_H

#include <memory>

#include "accessibility/editfield.hpp"
#include "accessibility/screen.hpp"

#include "spellicons.hpp"
#include "spellmodel.hpp"
#include "windowpinnablebase.hpp"

namespace MWGui
{
    class SpellView;

    class SpellWindow : public WindowPinnableBase, public NoDrop
    {
    public:
        SpellWindow(DragAndDrop* drag);

        void updateSpells();

        void onFrame(float dt) override;

        /// Cycle to next/previous spell
        void cycle(bool next);

        std::string_view getWindowIdForLua() const override { return "Magic"; }

    protected:
        MyGUI::Widget* mEffectBox;

        ESM::RefId mSpellToDelete;

        void onEnchantedItemSelected(MWWorld::Ptr item, bool alreadyEquipped);
        void onSpellSelected(const ESM::RefId& spellId);
        void onModelIndexSelected(SpellModel::ModelIndex index);
        void onFilterChanged(MyGUI::EditBox* sender);
        void onDeleteClicked(MyGUI::Widget* widget);
        void onDeleteSpellAccept();
        void askDeleteSpell(const ESM::RefId& spellId);

        void onPinToggled() override;
        void onTitleDoubleClicked() override;
        void onOpen() override;
        void onClose() override;
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

        SpellView* mSpellView;
        std::unique_ptr<SpellIcons> mSpellIcons;
        MyGUI::EditBox* mFilterEdit;

    private:
        float mUpdateTimer;

        // --- Screen-reader accessibility ---------------------------------
        // Virtual-focus controller. The spells/powers/enchanted items are drawn
        // by the custom SpellView (not per-item widgets), so navigation is by
        // index like the inventory and container windows. Shown alongside
        // Stats/Inventory/Map in Inventory mode; switched to via Tab through the
        // A11y::PaneGroup (Magic = pane 2).
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        // Screen-reader editing for the native name-filter box (mFilterEdit).
        A11y::EditField mA11yFilterEdit;
        // Number of leading non-spell options (just the name filter) in the a11y
        // list, so a list index maps to a spell-model row.
        size_t mA11yItemBase = 0;
        // Whether a11y text-edit mode was active last frame, so we can rebuild
        // the spell list once the user finishes editing the name filter.
        bool mA11yWasEditing = false;

        // (Re)build the spoken option list: name filter field, then one entry
        // per power/spell/enchanted item grouped into sections. Called on open
        // and after any action that changes the list (selection, delete).
        void buildAccessibility();
        // Rebuild after an action, keeping the cursor near its old row.
        void a11yRebuildKeepingCursor();
        // Activate the spell/item at \p modelIndex (Enter): select it as the
        // active spell, or equip/select an enchanted item.
        void a11yActivateSpell(int modelIndex);
        // Ask to delete the spell at \p modelIndex (Delete): routes through the
        // native confirmation dialog (powers/racial/sign spells can't be
        // deleted, matching the vanilla rules).
        void a11yDeleteSpell(int modelIndex);
        // Spoken label for one spell/item row (name, count, selected/equipped).
        std::string a11ySpellLabel(const Spell& spell) const;
        // Section name for a spell row (Powers / Spells / Magic Items).
        std::string a11ySpellSection(const Spell& spell) const;
        // Tooltip lines (cost/chance or cost/charge + magic effects) cycled with
        // T, mirroring the on-screen spell/item tooltip.
        std::vector<std::string> a11ySpellTooltip(const Spell& spell) const;
    };
}

#endif
