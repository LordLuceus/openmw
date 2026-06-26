#include "recharge.hpp"

#include <MyGUI_ScrollView.h>
#include <MyGUI_UString.h>

#include <components/widgets/box.hpp>

#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/recharge.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "accessibility/itemtext.hpp"

#include "inventoryitemmodel.hpp"
#include "itemchargeview.hpp"
#include "itemmodel.hpp"
#include "itemselection.hpp"
#include "itemwidget.hpp"
#include "sortfilteritemmodel.hpp"

namespace MWGui
{

    Recharge::Recharge()
        : WindowBase("openmw_recharge_dialog.layout")
    {
        getWidget(mBox, "Box");
        getWidget(mGemBox, "GemBox");
        getWidget(mGemIcon, "GemIcon");
        getWidget(mChargeLabel, "ChargeLabel");
        getWidget(mCancelButton, "CancelButton");

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &Recharge::onCancel);
        mBox->eventItemClicked += MyGUI::newDelegate(this, &Recharge::onItemClicked);

        mBox->setDisplayMode(ItemChargeView::DisplayMode_EnchantmentCharge);

        mGemIcon->eventMouseButtonClick += MyGUI::newDelegate(this, &Recharge::onSelectItem);

        // Screen-reader setup: invisible anchor holds key focus; the soul gem and
        // the rechargeable items are options rebuilt by buildAccessibility().
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);

        mControllerButtons.mA = "#{OMWEngine:RechargeSelect}";
        mControllerButtons.mB = "#{Interface:Cancel}";
        mControllerButtons.mY = "#{Interface:Soul}";
    }

    void Recharge::onOpen()
    {
        center();

        SortFilterItemModel* model
            = new SortFilterItemModel(std::make_unique<InventoryItemModel>(MWMechanics::getPlayer()));
        model->setFilter(SortFilterItemModel::Filter_OnlyRechargable);
        mBox->setModel(model);

        // Reset scrollbars
        mBox->resetScrollbars();

        // NB: don't build the screen-reader list here -- onOpen() runs BEFORE
        // setPtr(), so the soul gem isn't set yet. updateView() (called from
        // setPtr once the gem is in place) activates the screen on its first run
        // and rebuilds it thereafter.
    }

    void Recharge::onClose()
    {
        mA11y.deactivate();
    }

    void Recharge::onFrame(float dt)
    {
        mA11y.onFrame(dt);
    }

    void Recharge::buildAccessibility()
    {
        mA11y.clear();

        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();

        // Option 1: the soul gem. When one is set, read its name plus the soul's
        // value (the charge it can restore); Enter opens the picker to switch
        // gems. When none is set, it's a prompt to choose one.
        const MWWorld::Ptr* gemData = mGemIcon->getUserData<MWWorld::Ptr>(/*throw=*/false);
        const MWWorld::Ptr gem = gemData ? *gemData : MWWorld::Ptr();
        const bool hasGem = !gem.isEmpty() && gem.getCellRef().getCount() != 0;
        const std::string soulLabel{ winMgr->getGameSettingString("sSoul", "Soul") };
        if (hasGem)
        {
            const std::string gemName{ gem.getClass().getName(gem) };
            const ESM::RefId& soul = gem.getCellRef().getSoul();
            const ESM::Creature* creature = store.get<ESM::Creature>().search(soul);
            std::string label = gemName;
            if (creature != nullptr)
                label += ", " + soulLabel + " " + std::string{ creature->mName } + " "
                    + std::to_string(creature->mData.mSoul);

            mA11y.add({ .widget = nullptr, .label = std::move(label),
                .tooltips = [gem] { return A11y::itemTooltipLines(gem, 1); },
                .activate = [this] { onSelectItem(mGemIcon); } });
        }
        else
        {
            mA11y.add({ .widget = nullptr,
                .label = std::string(winMgr->getGameSettingString("sSoulGemsWithSouls", "Soul Gem")),
                .activate = [this] { onSelectItem(mGemIcon); } });
        }

        // Then each rechargeable enchanted item in the box. Label = name plus
        // remaining/maximum charge (matching the on-screen charge bar); the T-key
        // tooltip carries the full enchantment detail. Enter recharges it with
        // the current gem (onItemClicked no-ops when the recharge can't proceed).
        if (ItemModel* model = mBox->getModel())
        {
            const std::string chargesLabel{ winMgr->getGameSettingString("sCharges", "Charges") };
            for (size_t i = 0; i < model->getItemCount(); ++i)
            {
                const ItemStack stack = model->getItem(static_cast<ItemModel::ModelIndex>(i));
                const MWWorld::Ptr base = stack.mBase;

                std::string label = std::string(base.getClass().getName(base));
                const ESM::RefId& enchId = base.getClass().getEnchantment(base);
                if (!enchId.empty())
                {
                    if (const ESM::Enchantment* ench = store.get<ESM::Enchantment>().search(enchId))
                    {
                        const int maxCharge = MWMechanics::getEnchantmentCharge(*ench);
                        const float cur = base.getCellRef().getEnchantmentCharge();
                        const int charge = (cur == -1) ? maxCharge : static_cast<int>(cur);
                        label += ", " + chargesLabel + " " + std::to_string(charge) + " / "
                            + std::to_string(maxCharge);
                    }
                }

                mA11y.add({ .widget = nullptr, .label = std::move(label),
                    .tooltips = [base] { return A11y::itemTooltipLines(base, 1); },
                    .activate = [this, base] { onItemClicked(nullptr, base); } });
            }
        }

        mA11y.add({ .widget = mCancelButton,
            .label = std::string(winMgr->getGameSettingString("sCancel", "Cancel")),
            .activate = [this] { onCancel(mCancelButton); } });
    }

    void Recharge::a11yRebuildKeepingCursor()
    {
        const size_t cursor = mA11y.currentIndex();
        buildAccessibility();
        if (cursor == A11y::Screen::npos)
            mA11y.focusFirst(/*announce=*/true);
        else
            mA11y.selectIndex(std::min(cursor, mA11y.size() - 1), /*announce=*/true);
    }

    void Recharge::setPtr(const MWWorld::Ptr& item)
    {
        if (item.isEmpty() || !item.getClass().isItem(item))
            throw std::runtime_error("Invalid argument in Recharge::setPtr");

        mGemIcon->setItem(item);
        mGemIcon->setUserString("ToolTipType", "ItemPtr");
        mGemIcon->setUserData(MWWorld::Ptr(item));

        updateView();
    }

    void Recharge::updateView()
    {
        MWWorld::Ptr gem = *mGemIcon->getUserData<MWWorld::Ptr>();

        const ESM::RefId& soul = gem.getCellRef().getSoul();
        const ESM::Creature* creature = MWBase::Environment::get().getESMStore()->get<ESM::Creature>().find(soul);

        mChargeLabel->setCaptionWithReplacing("#{sCharges} " + MyGUI::utility::toString(creature->mData.mSoul));

        bool toolBoxVisible = gem.getCellRef().getCount() != 0;
        mGemBox->setVisible(toolBoxVisible);
        mGemBox->setUserString("Hidden", toolBoxVisible ? "false" : "true");

        if (!toolBoxVisible)
        {
            mGemIcon->setItem(MWWorld::Ptr());
            mGemIcon->clearUserStrings();
        }

        mBox->update();

        Gui::Box* box = dynamic_cast<Gui::Box*>(mMainWidget);
        if (box == nullptr)
            throw std::runtime_error("main widget must be a box");

        box->notifyChildrenSizeChanged();
        center();

        // Refresh the screen-reader list now the gem + item views are current.
        // First run (right after setPtr on open) activates the screen; later runs
        // (gem swapped, item recharged) rebuild it keeping cursor position.
        if (mA11y.isActive())
            a11yRebuildKeepingCursor();
        else
        {
            buildAccessibility();
            mA11y.activate();
        }
    }

    void Recharge::onCancel(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Recharge);
    }

    void Recharge::onSelectItem(MyGUI::Widget* /*sender*/)
    {
        mItemSelectionDialog = std::make_unique<ItemSelectionDialog>("#{sSoulGemsWithSouls}");
        mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &Recharge::onItemSelected);
        mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &Recharge::onItemCancel);
        mItemSelectionDialog->setVisible(true);
        mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
        mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyChargedSoulstones);
    }

    void Recharge::onItemSelected(MWWorld::Ptr item)
    {
        mItemSelectionDialog->setVisible(false);

        mGemIcon->setItem(item);
        mGemIcon->setUserString("ToolTipType", "ItemPtr");
        mGemIcon->setUserData(item);

        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
        updateView();
    }

    void Recharge::onItemCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void Recharge::onItemClicked(MyGUI::Widget* /*sender*/, const MWWorld::Ptr& item)
    {
        MWWorld::Ptr gem = *mGemIcon->getUserData<MWWorld::Ptr>();
        if (!MWMechanics::rechargeItem(item, gem))
            return;

        updateView();
    }

    bool Recharge::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if ((arg.button == SDL_CONTROLLER_BUTTON_A && !mGemBox->getVisible()) || arg.button == SDL_CONTROLLER_BUTTON_Y)
        {
            onSelectItem(mGemIcon);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onCancel(mCancelButton);
        else
            mBox->onControllerButton(arg.button);

        return true;
    }
}
