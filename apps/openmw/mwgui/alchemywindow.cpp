#include "alchemywindow.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_ControllerManager.h>
#include <MyGUI_ControllerRepeatClick.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_UString.h>

#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/alchemy.hpp"
#include "../mwmechanics/magiceffects.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include <MyGUI_Macros.h>

#include "accessibility/itemtext.hpp"
#include "accessibility/speech.hpp"
#include "accessibility/spelltext.hpp"
#include "inventoryitemmodel.hpp"
#include "itemview.hpp"
#include "itemwidget.hpp"
#include "sortfilteritemmodel.hpp"
#include "widgets.hpp"

namespace MWGui
{
    AlchemyWindow::AlchemyWindow()
        : WindowBase("openmw_alchemy_window.layout")
        , mCurrentFilter(FilterType::ByName)
        , mModel(nullptr)
        , mSortModel(nullptr)
        , mAlchemy(std::make_unique<MWMechanics::Alchemy>())
        , mApparatus(4)
        , mIngredients(4)
    {
        getWidget(mCreateButton, "CreateButton");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mIngredients[0], "Ingredient1");
        getWidget(mIngredients[1], "Ingredient2");
        getWidget(mIngredients[2], "Ingredient3");
        getWidget(mIngredients[3], "Ingredient4");
        getWidget(mApparatus[0], "Apparatus1");
        getWidget(mApparatus[1], "Apparatus2");
        getWidget(mApparatus[2], "Apparatus3");
        getWidget(mApparatus[3], "Apparatus4");
        getWidget(mEffectsBox, "CreatedEffects");
        getWidget(mBrewCountEdit, "BrewCount");
        getWidget(mIncreaseButton, "IncreaseButton");
        getWidget(mDecreaseButton, "DecreaseButton");
        getWidget(mNameEdit, "NameEdit");
        getWidget(mItemView, "ItemView");
        getWidget(mFilterValue, "FilterValue");
        getWidget(mFilterType, "FilterType");

        mBrewCountEdit->eventValueChanged += MyGUI::newDelegate(this, &AlchemyWindow::onCountValueChanged);
        mBrewCountEdit->eventEditSelectAccept += MyGUI::newDelegate(this, &AlchemyWindow::onAccept);
        mBrewCountEdit->setMinValue(1);
        mBrewCountEdit->setValue(1);

        mIncreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &AlchemyWindow::onIncreaseButtonPressed);
        mIncreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &AlchemyWindow::onCountButtonReleased);
        mDecreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &AlchemyWindow::onDecreaseButtonPressed);
        mDecreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &AlchemyWindow::onCountButtonReleased);

        mItemView->eventItemClicked += MyGUI::newDelegate(this, &AlchemyWindow::onSelectedItem);

        mIngredients[0]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onIngredientSelected);
        mIngredients[1]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onIngredientSelected);
        mIngredients[2]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onIngredientSelected);
        mIngredients[3]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onIngredientSelected);

        mApparatus[0]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onApparatusSelected);
        mApparatus[1]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onApparatusSelected);
        mApparatus[2]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onApparatusSelected);
        mApparatus[3]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onApparatusSelected);

        mCreateButton->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onCreateButtonClicked);
        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onCancelButtonClicked);

        mNameEdit->eventEditSelectAccept += MyGUI::newDelegate(this, &AlchemyWindow::onAccept);
        mFilterValue->eventComboChangePosition += MyGUI::newDelegate(this, &AlchemyWindow::onFilterChanged);
        mFilterValue->eventEditTextChange += MyGUI::newDelegate(this, &AlchemyWindow::onFilterEdited);
        mFilterType->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::switchFilterType);

        if (Settings::gui().mControllerMenus)
        {
            mControllerButtons.mA = "#{Interface:Select}";
            mControllerButtons.mB = "#{Interface:Cancel}";
            mControllerButtons.mX = "#{Interface:Create}";
            mControllerButtons.mY = "#{Interface:MagicEffects}";
            mControllerButtons.mR3 = "#{Interface:Info}";
        }

        // Screen-reader setup: an invisible anchor holds key focus while the
        // window is navigated by index. The ingredient list is drawn by the
        // custom ItemView (not as individual widgets), so we navigate a flat
        // option list built in buildAccessibility() and rebuilt on change.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>({}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);
        mA11yNameEdit.attach(mNameEdit);
        mA11yNameEdit.setActive(false);
        // Extra keys on the option list:
        //  - Delete removes the selected chosen ingredient / apparatus slot.
        //  - E re-reads the current potion effects on demand.
        mA11y.setExtraKeyHandler([this](MyGUI::KeyCode key) -> bool {
            if (key == MyGUI::KeyCode::E)
            {
                a11yAnnounceEffects();
                return true;
            }
            return false;
        });

        center();
    }

    void AlchemyWindow::onAccept(MyGUI::EditBox* sender)
    {
        onCreateButtonClicked(sender);

        // To do not spam onAccept() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void AlchemyWindow::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Alchemy);
    }

    void AlchemyWindow::onCreateButtonClicked(MyGUI::Widget* /*sender*/)
    {
        mAlchemy->setPotionName(mNameEdit->getCaption());
        int count = mAlchemy->countPotionsToBrew();
        count = std::min(count, mBrewCountEdit->getValue());
        createPotions(count);
    }

    void AlchemyWindow::createPotions(int count)
    {
        MWMechanics::Alchemy::Result result = mAlchemy->create(mNameEdit->getCaption(), count);
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        switch (result)
        {
            case MWMechanics::Alchemy::Result_NoName:
                winMgr->messageBox("#{sNotifyMessage37}");
                break;
            case MWMechanics::Alchemy::Result_NoMortarAndPestle:
                winMgr->messageBox("#{sNotifyMessage45}");
                break;
            case MWMechanics::Alchemy::Result_LessThanTwoIngredients:
                winMgr->messageBox("#{sNotifyMessage6a}");
                break;
            case MWMechanics::Alchemy::Result_Success:
                winMgr->playSound(ESM::RefId::stringRefId("potion success"));
                if (count == 1)
                    winMgr->messageBox("#{sPotionSuccess}");
                else
                    winMgr->messageBox(
                        "#{sPotionSuccess} " + mNameEdit->getCaption().asUTF8() + " (" + std::to_string(count) + ")");
                break;
            case MWMechanics::Alchemy::Result_NoEffects:
            case MWMechanics::Alchemy::Result_RandomFailure:
                winMgr->messageBox("#{sNotifyMessage8}");
                winMgr->playSound(ESM::RefId::stringRefId("potion fail"));
                break;
        }

        // remove ingredient slots that have been fully used up
        for (size_t i = 0; i < mIngredients.size(); ++i)
            if (mIngredients[i]->isUserString("ToolTipType"))
            {
                MWWorld::Ptr ingred = *mIngredients[i]->getUserData<MWWorld::Ptr>();
                if (ingred.getCellRef().getCount() == 0)
                    mAlchemy->removeIngredient(i);
            }

        updateFilters();
        update();
    }

    void AlchemyWindow::initFilter()
    {
        auto const& wm = MWBase::Environment::get().getWindowManager();
        std::string_view ingredient = wm->getGameSettingString("sIngredients", "Ingredients");

        if (mFilterType->getCaption() == ingredient)
        {
            if (Settings::gui().mControllerMenus)
                switchFilterType(mFilterType);
            else
                mCurrentFilter = FilterType::ByName;
        }
        else
            mCurrentFilter = FilterType::ByEffect;
        updateFilters();
        mFilterValue->clearIndexSelected();
        updateFilters();
    }

    void AlchemyWindow::switchFilterType(MyGUI::Widget* sender)
    {
        auto const& wm = MWBase::Environment::get().getWindowManager();
        std::string_view ingredient = wm->getGameSettingString("sIngredients", "Ingredients");
        auto* button = sender->castType<MyGUI::Button>();

        if (button->getCaption() == ingredient)
        {
            button->setCaption(MyGUI::UString(wm->getGameSettingString("sMagicEffects", "Magic Effects")));
            mCurrentFilter = FilterType::ByEffect;
        }
        else
        {
            button->setCaption(MyGUI::UString(ingredient));
            mCurrentFilter = FilterType::ByName;
        }
        mSortModel->setNameFilter({});
        mSortModel->setEffectFilter({});
        mFilterValue->clearIndexSelected();
        updateFilters();
        mItemView->update();
    }

    void AlchemyWindow::updateFilters()
    {
        std::set<std::string> itemNames, itemEffects;
        for (size_t i = 0; i < mModel->getItemCount(); ++i)
        {
            MWWorld::Ptr item = mModel->getItem(static_cast<ItemModel::ModelIndex>(i)).mBase;
            if (item.getType() != ESM::Ingredient::sRecordId)
                continue;

            itemNames.emplace(item.getClass().getName(item));

            MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
            auto const alchemySkill = player.getClass().getSkill(player, ESM::Skill::Alchemy);

            auto const effects = MWMechanics::Alchemy::effectsDescription(item, alchemySkill);
            itemEffects.insert(effects.begin(), effects.end());
        }

        mFilterValue->removeAllItems();
        auto const addItems = [&](auto const& container) {
            for (auto const& item : container)
                mFilterValue->addItem(item);
        };
        switch (mCurrentFilter)
        {
            case FilterType::ByName:
                addItems(itemNames);
                break;
            case FilterType::ByEffect:
                addItems(itemEffects);
                break;
        }
    }

    void AlchemyWindow::applyFilter(const std::string& filter)
    {
        switch (mCurrentFilter)
        {
            case FilterType::ByName:
                mSortModel->setNameFilter(filter);
                break;
            case FilterType::ByEffect:
                mSortModel->setEffectFilter(filter);
                break;
        }
        mItemView->update();
    }

    void AlchemyWindow::onFilterChanged(MyGUI::ComboBox* sender, size_t index)
    {
        // ignore spurious event fired when one edit the content after selection.
        // onFilterEdited will handle it.
        if (index != MyGUI::ITEM_NONE)
            applyFilter(sender->getItemNameAt(index));
    }

    void AlchemyWindow::onFilterEdited(MyGUI::EditBox* sender)
    {
        applyFilter(sender->getCaption());
    }

    void AlchemyWindow::onOpen()
    {
        mAlchemy->clear();
        mAlchemy->setAlchemist(MWMechanics::getPlayer());

        auto model = std::make_unique<InventoryItemModel>(MWMechanics::getPlayer());
        mModel = model.get();
        auto sortModel = std::make_unique<SortFilterItemModel>(std::move(model));
        mSortModel = sortModel.get();
        mSortModel->setFilter(SortFilterItemModel::Filter_OnlyIngredients);
        mItemView->setModel(std::move(sortModel));
        mItemView->resetScrollBars();

        mNameEdit->setCaption({});
        mBrewCountEdit->setValue(1);

        size_t index = 0;
        for (auto iter = mAlchemy->beginTools(); iter != mAlchemy->endTools() && index < mApparatus.size();
             ++iter, ++index)
        {
            const auto& widget = mApparatus[index];
            widget->setItem(*iter);
            widget->clearUserStrings();
            if (!iter->isEmpty())
            {
                widget->setUserString("ToolTipType", "ItemPtr");
                widget->setUserData(MWWorld::Ptr(*iter));
            }
        }

        update();
        initFilter();

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mNameEdit);

        if (Settings::gui().mControllerMenus)
            mItemView->setActiveControllerWindow(true);

        mA11yNameEdit.sync();
        buildAccessibility();
        mA11yLastSig = a11ySignature();
        mA11y.activate();
    }

    void AlchemyWindow::onClose()
    {
        mA11y.deactivate();
    }

    void AlchemyWindow::onFrame(float dt)
    {
        mA11y.onFrame(dt);
        mA11yNameEdit.onFrame();

        // When the user finishes editing the name field, re-sync the edit
        // baseline. (The suggested-name machinery in update() also writes the
        // box, so keep the snapshot fresh.)
        const bool editing = mA11y.editing();
        if (mA11yWasEditing && !editing)
            mA11yNameEdit.sync();
        mA11yWasEditing = editing;

        // Rebuild the spoken option list when the alchemy state changes
        // (ingredient added/removed, apparatus changed, filter applied). Never
        // rebuild mid-edit (buildAccessibility() -> clear() would drop edit mode
        // and leak the next keystroke to the extra-key handler) nor while a
        // submenu is open (the filter-value submenu's activation mutates the
        // signature; a rebuild would yank the open list out from under it).
        if (!editing && !mA11y.submenuOpen())
        {
            const long long sig = a11ySignature();
            if (sig != mA11yLastSig)
            {
                const size_t cursor = mA11y.currentIndex();
                mA11yLastSig = sig;
                buildAccessibility();
                // Preserve the cursor by position; clamp to the new list size.
                if (cursor != A11y::Screen::npos && mA11y.size() > 0)
                    mA11y.selectIndex(std::min(cursor, mA11y.size() - 1), /*announce=*/false);
            }
        }
    }

    void AlchemyWindow::onIngredientSelected(MyGUI::Widget* sender)
    {
        size_t i = std::distance(mIngredients.begin(), std::find(mIngredients.begin(), mIngredients.end(), sender));
        mAlchemy->removeIngredient(i);
        update();
    }

    void AlchemyWindow::onItemSelected(MWWorld::Ptr item)
    {
        int32_t index = item.get<ESM::Apparatus>()->mBase->mData.mType;
        const auto& widget = mApparatus[index];

        widget->setItem(item);

        if (item.isEmpty())
        {
            widget->clearUserStrings();
            mItemSelectionDialog->setVisible(false);
            return;
        }

        mAlchemy->addApparatus(item);

        widget->setUserString("ToolTipType", "ItemPtr");
        widget->setUserData(MWWorld::Ptr(item));

        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
        update();

        // Hide the picker LAST: WindowBase::setVisible(false) fires the picker's
        // onClose, which resumes our screen and re-announces the current option.
        // The apparatus must already be added (above) so that announcement reads
        // the new tool, not the stale "empty" slot.
        mItemSelectionDialog->setVisible(false);
    }

    void AlchemyWindow::onItemCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void AlchemyWindow::onApparatusSelected(MyGUI::Widget* sender)
    {
        size_t i = std::distance(mApparatus.begin(), std::find(mApparatus.begin(), mApparatus.end(), sender));
        if (sender->getUserData<MWWorld::Ptr>()->isEmpty()) // if this apparatus slot is empty
        {
            std::string title;
            switch (i)
            {
                case ESM::Apparatus::AppaType::MortarPestle:
                    title = "#{sMortar}";
                    break;
                case ESM::Apparatus::AppaType::Alembic:
                    title = "#{sAlembic}";
                    break;
                case ESM::Apparatus::AppaType::Calcinator:
                    title = "#{sCalcinator}";
                    break;
                case ESM::Apparatus::AppaType::Retort:
                    title = "#{sRetort}";
                    break;
                default:
                    title = "#{sApparatus}";
            }

            mItemSelectionDialog = std::make_unique<ItemSelectionDialog>(title);
            mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &AlchemyWindow::onItemSelected);
            mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &AlchemyWindow::onItemCancel);
            mItemSelectionDialog->setVisible(true);
            mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
            mItemSelectionDialog->getSortModel()->setApparatusTypeFilter(static_cast<int32_t>(i));
            mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyAlchemyTools);
        }
        else
        {
            const auto& widget = mApparatus[i];
            mAlchemy->removeApparatus(i);

            if (widget->getChildCount())
                MyGUI::Gui::getInstance().destroyWidget(widget->getChildAt(0));

            widget->clearUserStrings();
            widget->setItem(MWWorld::Ptr());
            widget->setUserData(MWWorld::Ptr());
        }

        update();
    }

    void AlchemyWindow::onSelectedItem(int index)
    {
        MWWorld::Ptr item = mSortModel->getItem(index).mBase;
        int res = mAlchemy->addIngredient(item);

        if (res != -1)
        {
            update();

            const ESM::RefId& sound = item.getClass().getUpSoundId(item);
            MWBase::Environment::get().getWindowManager()->playSound(sound);
        }
    }

    void AlchemyWindow::update()
    {
        std::string suggestedName = mAlchemy->suggestPotionName();
        if (suggestedName != mSuggestedPotionName)
        {
            mNameEdit->setCaptionWithReplacing(suggestedName);
            mSuggestedPotionName = std::move(suggestedName);
        }

        mSortModel->clearDragItems();

        MWMechanics::Alchemy::TIngredientsIterator it = mAlchemy->beginIngredients();
        for (int i = 0; i < 4; ++i)
        {
            ItemWidget* ingredient = mIngredients[i];

            MWWorld::Ptr item;
            if (it != mAlchemy->endIngredients())
            {
                item = *it;
                ++it;
            }

            if (!item.isEmpty())
                mSortModel->addDragItem(item, item.getCellRef().getCount());

            if (ingredient->getChildCount())
                MyGUI::Gui::getInstance().destroyWidget(ingredient->getChildAt(0));

            ingredient->clearUserStrings();

            ingredient->setItem(item);

            if (item.isEmpty())
                continue;

            ingredient->setUserString("ToolTipType", "ItemPtr");
            ingredient->setUserData(MWWorld::Ptr(item));

            ingredient->setCount(item.getCellRef().getCount());
        }

        mItemView->update();

        std::vector<MWMechanics::EffectKey> effectIds = mAlchemy->listEffects();
        Widgets::SpellEffectList list;
        unsigned int effectIndex = 0;
        for (const MWMechanics::EffectKey& effectKey : effectIds)
        {
            Widgets::SpellEffectParams params;
            params.mEffectID = effectKey.mId;
            const ESM::MagicEffect* magicEffect
                = MWBase::Environment::get().getESMStore()->get<ESM::MagicEffect>().find(effectKey.mId);
            if (magicEffect->mData.mFlags & ESM::MagicEffect::TargetSkill)
                params.mSkill = effectKey.mArg;
            else if (magicEffect->mData.mFlags & ESM::MagicEffect::TargetAttribute)
                params.mAttribute = effectKey.mArg;
            params.mIsConstant = true;
            params.mNoTarget = true;
            params.mNoMagnitude = true;

            params.mKnown = mAlchemy->knownEffect(effectIndex, MWBase::Environment::get().getWorld()->getPlayerPtr());

            list.push_back(params);
            ++effectIndex;
        }

        while (mEffectsBox->getChildCount())
            MyGUI::Gui::getInstance().destroyWidget(mEffectsBox->getChildAt(0));

        MyGUI::IntCoord coord(0, 0, mEffectsBox->getWidth(), 24);
        Widgets::MWEffectListPtr effectsWidget = mEffectsBox->createWidget<Widgets::MWEffectList>(
            "MW_StatName", coord, MyGUI::Align::Left | MyGUI::Align::Top);

        effectsWidget->setEffectList(list);

        std::vector<MyGUI::Widget*> effectItems;
        effectsWidget->createEffectWidgets(effectItems, mEffectsBox, coord, false, 0);
        effectsWidget->setCoord(coord);
    }

    void AlchemyWindow::addRepeatController(MyGUI::Widget* widget)
    {
        MyGUI::ControllerItem* item
            = MyGUI::ControllerManager::getInstance().createItem(MyGUI::ControllerRepeatClick::getClassTypeName());
        MyGUI::ControllerRepeatClick* controller = static_cast<MyGUI::ControllerRepeatClick*>(item);
        controller->eventRepeatClick += newDelegate(this, &AlchemyWindow::onRepeatClick);
        MyGUI::ControllerManager::getInstance().addItem(widget, controller);
    }

    void AlchemyWindow::onIncreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        addRepeatController(sender);
        onIncreaseButtonTriggered();
    }

    void AlchemyWindow::onDecreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        addRepeatController(sender);
        onDecreaseButtonTriggered();
    }

    void AlchemyWindow::onRepeatClick(MyGUI::Widget* widget, MyGUI::ControllerItem* controller)
    {
        if (widget == mIncreaseButton)
            onIncreaseButtonTriggered();
        else if (widget == mDecreaseButton)
            onDecreaseButtonTriggered();
    }

    void AlchemyWindow::onCountButtonReleased(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        MyGUI::ControllerManager::getInstance().removeItem(sender);
    }

    void AlchemyWindow::onCountValueChanged(int value)
    {
        mBrewCountEdit->setValue(std::abs(value));
    }

    void AlchemyWindow::onIncreaseButtonTriggered()
    {
        int currentCount = mBrewCountEdit->getValue();

        // prevent overflows
        if (currentCount == std::numeric_limits<int>::max())
            return;

        mBrewCountEdit->setValue(currentCount + 1);
    }

    void AlchemyWindow::onDecreaseButtonTriggered()
    {
        int currentCount = mBrewCountEdit->getValue();
        if (currentCount > 1)
            mBrewCountEdit->setValue(currentCount - 1);
    }

    void AlchemyWindow::filterListButtonHandler(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A || arg.button == SDL_CONTROLLER_BUTTON_Y)
        {
            // Select the highlighted entry in the combo box and close it. List is closed by focusing on another
            // widget.
            size_t index = mFilterValue->getIndexSelected();
            mFilterValue->setIndexSelected(index);
            onFilterChanged(mFilterValue, index);
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mNameEdit);

            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            // Close the list without selecting anything. List is closed by focusing on another widget.
            mFilterValue->clearIndexSelected();
            onFilterEdited(mFilterValue);
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mNameEdit);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
            MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::ArrowUp, 0, false);
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::ArrowDown, 0, false);
    }

    bool AlchemyWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        bool isFilterListOpen
            = focus != nullptr && focus->getParent() != nullptr && focus->getParent()->getParent() == mFilterValue;

        if (isFilterListOpen)
        {
            // When the filter list combo box is open, send all inputs to it.
            filterListButtonHandler(arg);
            return true;
        }

        if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            // Remove active ingredients or close the window, starting with right-most slot.
            for (size_t i = mIngredients.size(); i > 0; --i)
            {
                if (mIngredients[i - 1]->isUserString("ToolTipType"))
                {
                    onIngredientSelected(mIngredients[i - 1]);
                    return true;
                }
            }
            // If the ingredients list is empty, B closes the menu.
            onCancelButtonClicked(mCancelButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_X)
            onCreateButtonClicked(mCreateButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_Y && mFilterValue->getItemCount() > 0)
        {
            // Magical effects/ingredients filter
            if (mFilterValue->getIndexSelected() != MyGUI::ITEM_NONE)
            {
                // Clear the active filter
                mFilterValue->clearIndexSelected();
                onFilterEdited(mFilterValue);
            }
            else
            {
                // Open the combo box to choose the a filter
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mFilterValue);
                MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::ArrowDown, 0, false);
            }
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            onDecreaseButtonTriggered();
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
            onIncreaseButtonTriggered();
        else
            mItemView->onControllerButton(arg.button);

        return true;
    }

    // ----------------------------------------------------------------------------
    // Accessibility
    // ----------------------------------------------------------------------------

    std::string AlchemyWindow::a11yNameValue() const
    {
        const std::string text = mNameEdit->getCaption().asUTF8();
        return text.empty() ? std::string("blank") : text;
    }

    void AlchemyWindow::a11ySyncPotionName()
    {
        // getReadyStatus() (used by countPotionsToBrew) returns Result_NoName
        // until a name is set, which the window otherwise only does at Create.
        // Keep mAlchemy's name in step with the edit box so the brewable count
        // is accurate before the first Create.
        mAlchemy->setPotionName(mNameEdit->getCaption());
    }

    std::string AlchemyWindow::a11yQuantityValue()
    {
        a11ySyncPotionName();
        // countPotionsToBrew() is the real cap (limited by the scarcest
        // ingredient); it's 0 until the mix is valid. Surface it so the count
        // field is meaningful -- on screen the max is implicit, but a blind user
        // has no other way to know how many they can actually make.
        const int max = mAlchemy->countPotionsToBrew();
        const int value = mBrewCountEdit->getValue();
        if (max <= 0)
            return std::to_string(value) + ", none brewable yet";
        return std::to_string(value) + " of " + std::to_string(max);
    }

    void AlchemyWindow::a11yChangeQuantity(bool next)
    {
        a11ySyncPotionName();
        const int max = mAlchemy->countPotionsToBrew();
        int value = mBrewCountEdit->getValue();
        if (next)
        {
            // Don't climb past what's actually brewable (the native Increase
            // button is uncapped, which is meaningless without sight).
            if (max > 0 && value >= max)
            {
                A11y::say(std::to_string(value) + " of " + std::to_string(max) + ", maximum.", /*interrupt=*/true);
                return;
            }
            mBrewCountEdit->setValue(value + 1);
        }
        else
        {
            if (value <= 1)
            {
                A11y::say("1, minimum.", /*interrupt=*/true);
                return;
            }
            mBrewCountEdit->setValue(value - 1);
        }
        A11y::say(a11yQuantityValue(), /*interrupt=*/true);
    }

    std::string AlchemyWindow::a11yFilterTypeName() const
    {
        auto const& wm = MWBase::Environment::get().getWindowManager();
        return mCurrentFilter == FilterType::ByEffect
            ? std::string(wm->getGameSettingString("sMagicEffects", "Magic Effects"))
            : std::string(wm->getGameSettingString("sIngredients", "Ingredients"));
    }

    std::string AlchemyWindow::a11yIngredientLabel(const MWWorld::Ptr& item, int count) const
    {
        std::string label = std::string(item.getClass().getName(item));
        if (count > 1)
            label += " (" + std::to_string(count) + ")";
        return label;
    }

    std::vector<std::string> AlchemyWindow::a11yCurrentEffectLines() const
    {
        std::vector<std::string> lines;
        std::vector<MWMechanics::EffectKey> effectIds = mAlchemy->listEffects();
        unsigned int effectIndex = 0;
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        for (const MWMechanics::EffectKey& effectKey : effectIds)
        {
            Widgets::SpellEffectParams params;
            params.mEffectID = effectKey.mId;
            const ESM::MagicEffect* magicEffect
                = MWBase::Environment::get().getESMStore()->get<ESM::MagicEffect>().find(effectKey.mId);
            if (magicEffect->mData.mFlags & ESM::MagicEffect::TargetSkill)
                params.mSkill = effectKey.mArg;
            else if (magicEffect->mData.mFlags & ESM::MagicEffect::TargetAttribute)
                params.mAttribute = effectKey.mArg;
            params.mIsConstant = true;
            params.mNoTarget = true;
            params.mNoMagnitude = true;
            params.mKnown = mAlchemy->knownEffect(effectIndex, player);
            lines.push_back(A11y::formatSpellEffectLine(params));
            ++effectIndex;
        }
        return lines;
    }

    void AlchemyWindow::a11yAnnounceEffects()
    {
        auto const& wm = MWBase::Environment::get().getWindowManager();
        std::vector<std::string> lines = a11yCurrentEffectLines();
        if (lines.empty())
        {
            // A potion needs at least two ingredients sharing an effect. Until
            // then there are simply no effects yet -- this is NOT a brew failure
            // (sNotifyMessage8 "potion failed"), which only happens on Create.
            A11y::say("No shared effects yet.", /*interrupt=*/true);
            return;
        }
        std::string text = std::string(wm->getGameSettingString("sEffects", "Effects")) + ": ";
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i > 0)
                text += ", ";
            text += lines[i];
        }
        A11y::say(text, /*interrupt=*/true);
    }

    void AlchemyWindow::a11yAddIngredient(int index)
    {
        if (!mSortModel || index < 0 || index >= static_cast<int>(mSortModel->getItemCount()))
            return;
        onSelectedItem(index);
        // onSelectedItem only plays a sound on success; announce the resulting
        // combined effects so the player knows whether the addition produced a
        // shared (and thus brewable) effect.
        a11yAnnounceEffects();
    }

    std::string AlchemyWindow::a11yFilterValue() const
    {
        if (mA11yActiveFilter.empty())
            return "none";
        return mA11yActiveFilter;
    }

    void AlchemyWindow::a11yApplyFilter(const std::string& value)
    {
        // Single-select toggle: re-activating the active value clears it. The
        // engine model holds only one filter string at a time (name and effect
        // filters are mutually exclusive), so multi-select isn't possible here.
        const bool clearing = value.empty() || value == mA11yActiveFilter;
        if (clearing)
        {
            mA11yActiveFilter.clear();
            mFilterValue->clearIndexSelected();
            applyFilter({});
            A11y::say("Filter cleared.", /*interrupt=*/true);
        }
        else
        {
            mA11yActiveFilter = value;
            applyFilter(value);
            A11y::say(value + ", selected.", /*interrupt=*/true);
        }
        // If toggled from the open filter submenu, re-snapshot it so the
        // selected/not-selected marks reflect the change (no re-announce: we
        // just spoke the result above). No-op when no submenu is open.
        mA11y.refreshSubmenu(/*announce=*/false);
    }

    std::vector<A11y::SubItem> AlchemyWindow::a11yFilterValues()
    {
        std::vector<A11y::SubItem> items;

        // A leading entry to clear any active filter.
        items.push_back({ .label = mA11yActiveFilter.empty() ? "Clear filter (none active)" : "Clear filter",
            .activate = [this] { a11yApplyFilter({}); } });

        for (size_t i = 0; i < mFilterValue->getItemCount(); ++i)
        {
            std::string value = mFilterValue->getItemNameAt(i);
            const bool selected = (value == mA11yActiveFilter);
            // Mark the active value so the user can tell what's filtering. The
            // state suffix goes at the end (project convention for status info).
            std::string label = value + (selected ? ", selected" : ", not selected");
            items.push_back({ .label = std::move(label), .activate = [this, value] { a11yApplyFilter(value); } });
        }
        return items;
    }

    long long AlchemyWindow::a11ySignature() const
    {
        // Fold the chosen ingredients, apparatus slots, available-ingredient set
        // and suggested name into a cheap rolling hash. Any change to the mix or
        // the filtered list shifts it, triggering a rebuild of the spoken list.
        long long sig = 1469598103934665603LL; // FNV offset basis
        auto mix = [&sig](long long v) { sig = (sig ^ v) * 1099511628211LL; };

        for (auto it = mAlchemy->beginIngredients(); it != mAlchemy->endIngredients(); ++it)
        {
            if (!it->isEmpty())
            {
                mix(static_cast<long long>(std::hash<std::string>{}(it->getCellRef().getRefId().toString())));
                mix(it->getCellRef().getCount());
            }
            else
                mix(0);
        }
        for (auto it = mAlchemy->beginTools(); it != mAlchemy->endTools(); ++it)
            mix(it->isEmpty() ? 0 : static_cast<long long>(std::hash<std::string>{}(it->getCellRef().getRefId().toString())));

        if (mSortModel)
        {
            mix(static_cast<long long>(mSortModel->getItemCount()));
            for (size_t i = 0; i < mSortModel->getItemCount(); ++i)
            {
                const ItemStack item = mSortModel->getItem(static_cast<int>(i));
                mix(static_cast<long long>(std::hash<std::string>{}(item.mBase.getCellRef().getRefId().toString())));
                mix(item.mCount);
            }
        }
        mix(static_cast<long long>(std::hash<std::string>{}(mSuggestedPotionName)));
        return sig;
    }

    void AlchemyWindow::buildAccessibility()
    {
        mA11y.clear();
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();

        const std::string ingredientsSection
            = std::string(wm->getGameSettingString("sIngredients", "Ingredients"));

        // 1. Potion name (editable text field).
        mA11y.add({ .widget = nullptr,
            .label = std::string(wm->getGameSettingString("sName", "Name")),
            .value = [this] { return a11yNameValue(); },
            .edit = &mA11yNameEdit });

        // 2. Brew count (Left/Right to adjust, clamped to what's brewable).
        //    asyncValue suppresses the framework's auto re-announce: our change
        //    handler speaks richer feedback ("N of max", "maximum", "minimum").
        mA11y.add({ .widget = nullptr,
            .label = std::string(wm->getGameSettingString("sQuantityMenuMessage02", "Quantity")),
            .value = [this] { return a11yQuantityValue(); },
            .change = [this](bool next) { a11yChangeQuantity(next); },
            .asyncValue = true });

        // 3. Filter type (by name / by effect). Switching clears any active
        //    filter value (the two filter kinds are mutually exclusive).
        mA11y.add({ .widget = nullptr,
            .label = std::string(wm->getGameSettingString("sShowAll", "Filter")),
            .value = [this] { return a11yFilterTypeName(); },
            .change =
                [this](bool) {
                    switchFilterType(mFilterType);
                    mA11yActiveFilter.clear();
                } });

        // 4. Filter value (submenu of available values for the current type).
        //    The value reports the active filter so it's audible on focus.
        mA11y.add({ .widget = nullptr,
            .label = std::string(wm->getGameSettingString("sFilter", "Filter value")),
            .value = [this] { return a11yFilterValue(); },
            .children = [this] { return a11yFilterValues(); } });

        // 5. Apparatus slots. Enter opens the picker for an empty slot, or
        //    removes the tool in a filled one. Names mirror the on-screen labels.
        //    The value is read LIVE from mAlchemy (not captured), so the picker's
        //    resume-announcement and a removal both reflect the real slot state
        //    without waiting for the next-frame rebuild.
        static const char* const appaNames[] = { "sMortar", "sAlembic", "sCalcinator", "sRetort" };
        const std::string apparatusSection = std::string(wm->getGameSettingString("sApparatus", "Apparatus"));
        for (size_t i = 0; i < mApparatus.size(); ++i)
        {
            const std::string slotName = std::string(wm->getGameSettingString(appaNames[i], appaNames[i]));
            mA11y.add({ .widget = nullptr,
                .label = slotName,
                .section = apparatusSection,
                .value =
                    [this, i] {
                        auto it = mAlchemy->beginTools();
                        std::advance(it, i);
                        if (it != mAlchemy->endTools() && !it->isEmpty())
                            return std::string(it->getClass().getName(*it));
                        return std::string("empty");
                    },
                .activate =
                    [this, i, slotName] {
                        const bool wasFilled = !mApparatus[i]->getUserData<MWWorld::Ptr>()->isEmpty();
                        onApparatusSelected(mApparatus[i]);
                        // A removal stays in-window (no picker to close, so nothing
                        // re-announces); speak the now-empty slot. An add opens the
                        // picker, which announces on its own when it closes.
                        if (wasFilled)
                            A11y::say(slotName + ", empty.", /*interrupt=*/true);
                    } });
        }

        // 6. Chosen ingredients (the current mix). Enter removes one from the mix.
        {
            size_t slot = 0;
            for (auto it = mAlchemy->beginIngredients(); it != mAlchemy->endIngredients(); ++it, ++slot)
            {
                if (it->isEmpty())
                    continue;
                const size_t i = slot;
                std::string label = a11yIngredientLabel(*it, it->getCellRef().getCount());
                mA11y.add({ .widget = nullptr,
                    .label = label,
                    .section = "Selected ingredients",
                    .activate =
                        [this, i] {
                            mAlchemy->removeIngredient(i);
                            update();
                            a11yAnnounceEffects();
                        } });
            }
        }

        // 7. Current potion effects (read-only; expand to hear each effect).
        mA11y.add({ .widget = nullptr,
            .label = std::string(wm->getGameSettingString("sEffects", "Effects")),
            .children =
                [this] {
                    std::vector<A11y::SubItem> items;
                    for (const std::string& line : a11yCurrentEffectLines())
                        items.push_back({ .label = line });
                    return items;
                } });

        // 8. Available ingredients in inventory (the primary list). Enter adds
        //    one to the mix. The T-key tooltip carries weight/value/effects.
        if (mSortModel)
        {
            for (size_t i = 0; i < mSortModel->getItemCount(); ++i)
            {
                const int index = static_cast<int>(i);
                const ItemStack item = mSortModel->getItem(index);
                mA11y.add({ .widget = nullptr,
                    .label = a11yIngredientLabel(item.mBase, static_cast<int>(item.mCount)),
                    .section = ingredientsSection,
                    .tooltips = [base = item.mBase, count = item.mCount]
                    { return A11y::itemTooltipLines(base, static_cast<int>(count)); },
                    .activate = [this, index] { a11yAddIngredient(index); } });
            }
        }

        // 9. Create button -- LAST, after the ingredient list, so navigating
        //    down through the ingredients ends on the action that consumes them.
        mA11y.add({ .widget = nullptr,
            .label = std::string(wm->getGameSettingString("sCreate", "Create")),
            .activate = [this] { onCreateButtonClicked(mCreateButton); } });
    }
}
