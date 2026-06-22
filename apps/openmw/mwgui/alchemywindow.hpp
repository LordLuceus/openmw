#ifndef MWGUI_ALCHEMY_H
#define MWGUI_ALCHEMY_H

#include <memory>
#include <vector>

#include <MyGUI_ComboBox.h>
#include <MyGUI_ControllerItem.h>

#include <components/widgets/box.hpp>
#include <components/widgets/numericeditbox.hpp>

#include "accessibility/editfield.hpp"
#include "accessibility/screen.hpp"
#include "itemselection.hpp"
#include "windowbase.hpp"

#include "../mwmechanics/alchemy.hpp"

namespace MWGui
{
    class ItemView;
    class ItemWidget;
    class InventoryItemModel;
    class SortFilterItemModel;

    class AlchemyWindow : public WindowBase
    {
    public:
        AlchemyWindow();

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;

        void onResChange(int, int) override { center(); }

        std::string_view getWindowIdForLua() const override { return "Alchemy"; }

    private:
        static const float sCountChangeInitialPause; // in seconds
        static const float sCountChangeInterval; // in seconds

        std::string mSuggestedPotionName;
        enum class FilterType
        {
            ByName,
            ByEffect
        };
        FilterType mCurrentFilter;

        std::unique_ptr<ItemSelectionDialog> mItemSelectionDialog;

        ItemView* mItemView;
        InventoryItemModel* mModel;
        SortFilterItemModel* mSortModel;

        MyGUI::Button* mCreateButton;
        MyGUI::Button* mCancelButton;

        MyGUI::Widget* mEffectsBox;

        MyGUI::Button* mIncreaseButton;
        MyGUI::Button* mDecreaseButton;
        Gui::AutoSizedButton* mFilterType;
        MyGUI::ComboBox* mFilterValue;
        MyGUI::EditBox* mNameEdit;
        Gui::NumericEditBox* mBrewCountEdit;

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onCreateButtonClicked(MyGUI::Widget* sender);
        void onIngredientSelected(MyGUI::Widget* sender);
        void onApparatusSelected(MyGUI::Widget* sender);
        void onAccept(MyGUI::EditBox*);
        void onIncreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onDecreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onCountButtonReleased(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onCountValueChanged(int value);
        void onRepeatClick(MyGUI::Widget* widget, MyGUI::ControllerItem* controller);

        void applyFilter(const std::string& filter);
        void initFilter();
        void onFilterChanged(MyGUI::ComboBox* sender, size_t index);
        void onFilterEdited(MyGUI::EditBox* sender);
        void switchFilterType(MyGUI::Widget* sender);
        void updateFilters();

        void addRepeatController(MyGUI::Widget* widget);

        void onIncreaseButtonTriggered();
        void onDecreaseButtonTriggered();

        void onSelectedItem(int index);

        void onItemSelected(MWWorld::Ptr item);
        void onItemCancel();

        void createPotions(int count);

        void update();

        std::unique_ptr<MWMechanics::Alchemy> mAlchemy;

        std::vector<ItemWidget*> mApparatus;
        std::vector<ItemWidget*> mIngredients;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void filterListButtonHandler(const SDL_ControllerButtonEvent& arg);

        // --- Accessibility -----------------------------------------------------
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        A11y::EditField mA11yNameEdit;
        // Cheap rolling hash of the alchemy state (chosen ingredients, apparatus,
        // suggested name, available ingredient set). When it changes between
        // frames we rebuild the spoken option list.
        long long mA11yLastSig = -1;
        bool mA11yWasEditing = false;
        // The currently applied filter value (empty = no filter). Tracked so the
        // filter submenu can mark the active entry and toggle it off. The engine
        // model supports only ONE filter string at a time (name and effect
        // filters are mutually exclusive), so this is single-select.
        std::string mA11yActiveFilter;

        void buildAccessibility();
        long long a11ySignature() const;
        // Spoken label for one available ingredient (name + count).
        std::string a11yIngredientLabel(const MWWorld::Ptr& item, int count) const;
        // Effects shared by the currently-selected ingredients, as spoken lines.
        std::vector<std::string> a11yCurrentEffectLines() const;
        // The current potion's combined effects, spoken (interrupting), e.g.
        // after adding/removing an ingredient.
        void a11yAnnounceEffects();
        // Add the available-ingredient at sort-model \p index to the mix.
        void a11yAddIngredient(int index);
        // Spoken value for the name field (current contents or "blank").
        std::string a11yNameValue() const;
        // Spoken value for the brew-count field: "<n> of <max>" where max is the
        // number actually brewable from the current ingredients (0 if none).
        std::string a11yQuantityValue();
        // Push the name-edit caption into mAlchemy so countPotionsToBrew()'s
        // ready-status check (which requires a non-empty name) reflects what the
        // user would actually brew. The name is otherwise only synced at Create.
        void a11ySyncPotionName();
        // Adjust the brew count by one, clamped to [1, max brewable], announcing.
        void a11yChangeQuantity(bool next);
        // Filter type name ("by name" / "by effect").
        std::string a11yFilterTypeName() const;
        // Spoken value for the filter-value option: the active filter, or "none".
        std::string a11yFilterValue() const;
        // Apply (or, if already active, clear) a filter value, announcing the
        // result. Empty string clears the filter.
        void a11yApplyFilter(const std::string& value);
        // The available filter values for the current filter type, as sub-items
        // that toggle the filter when activated, each marked selected/not.
        std::vector<A11y::SubItem> a11yFilterValues();
    };
}

#endif
