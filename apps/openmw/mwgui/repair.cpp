#include "repair.hpp"

#include <iomanip>

#include <MyGUI_ScrollView.h>

#include <components/esm3/loadrepa.hpp>
#include <components/widgets/box.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwworld/class.hpp"

#include "accessibility/itemtext.hpp"
#include "accessibility/speech.hpp"

#include "inventoryitemmodel.hpp"
#include "itemchargeview.hpp"
#include "itemmodel.hpp"
#include "itemselection.hpp"
#include "itemwidget.hpp"
#include "sortfilteritemmodel.hpp"

namespace MWGui
{

    Repair::Repair()
        : WindowBase("openmw_repair.layout")
    {
        getWidget(mRepairBox, "RepairBox");
        getWidget(mToolBox, "ToolBox");
        getWidget(mToolIcon, "ToolIcon");
        getWidget(mUsesLabel, "UsesLabel");
        getWidget(mQualityLabel, "QualityLabel");
        getWidget(mCancelButton, "CancelButton");

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &Repair::onCancel);

        mRepairBox->eventItemClicked += MyGUI::newDelegate(this, &Repair::onRepairItem);
        mRepairBox->setDisplayMode(ItemChargeView::DisplayMode_Health);

        mToolIcon->eventMouseButtonClick += MyGUI::newDelegate(this, &Repair::onSelectItem);

        // Screen-reader setup: invisible anchor holds key focus; the tool and
        // the damaged items are options rebuilt by buildAccessibility().
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);

        mControllerButtons.mA = "#{Interface:Repair}";
        mControllerButtons.mB = "#{Interface:Cancel}";
        mControllerButtons.mY = "#{OMWEngine:RepairTool}";
    }

    void Repair::onOpen()
    {
        center();

        SortFilterItemModel* model
            = new SortFilterItemModel(std::make_unique<InventoryItemModel>(MWMechanics::getPlayer()));
        model->setFilter(SortFilterItemModel::Filter_OnlyRepairable);
        mRepairBox->setModel(model);
        mRepairBox->update();
        // Reset scrollbars
        mRepairBox->resetScrollbars();

        // NB: don't build the screen-reader list here -- onOpen() runs BEFORE
        // setPtr(), so the repair tool isn't set yet. updateRepairView() (called
        // from setPtr once the tool is in place) activates the screen on its
        // first run and rebuilds it thereafter.
    }

    void Repair::onClose()
    {
        mA11y.deactivate();
    }

    void Repair::onFrame(float dt)
    {
        mA11y.onFrame(dt);
    }

    void Repair::buildAccessibility()
    {
        mA11y.clear();

        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        // Option 1: the repair tool. When a tool is equipped, read its name plus
        // remaining uses and quality; Enter opens the item picker to switch
        // tools. When none is set, it's a prompt to choose one.
        const MWWorld::Ptr tool = mRepair.getTool();
        const bool hasTool = !tool.isEmpty() && tool.getCellRef().getCount() != 0;
        if (hasTool)
        {
            MWWorld::LiveCellRef<ESM::Repair>* ref = tool.get<ESM::Repair>();
            const int uses = tool.getClass().getItemHealth(tool);
            const float quality = ref->mBase->mData.mQuality;
            std::stringstream qualityStr;
            qualityStr << std::setprecision(3) << quality;
            const std::string toolName{ tool.getClass().getName(tool) };
            const std::string usesLabel{ winMgr->getGameSettingString("sUses", "Uses") };
            const std::string qualityLabel{ winMgr->getGameSettingString("sQuality", "Quality") };

            mA11y.add({ .widget = nullptr,
                .label = std::string(winMgr->getGameSettingString("sRepair", "Repair")) + ": " + toolName,
                .value = [toolName, usesLabel, uses, qualityLabel, q = qualityStr.str()] {
                    return usesLabel + " " + std::to_string(uses) + ", " + qualityLabel + " " + q;
                },
                .activate = [this] { onSelectItem(mToolIcon); } });
        }
        else
        {
            mA11y.add({ .widget = nullptr,
                .label = std::string(winMgr->getGameSettingString("sRepair", "Repair")),
                .activate = [this] { onSelectItem(mToolIcon); } });
        }

        // Then each damaged item in the box. Label = name; the T-key tooltip
        // carries weight/value/condition detail. Enter repairs it with the
        // current tool (onRepairItem no-ops when no tool is set).
        if (ItemModel* model = mRepairBox->getModel())
        {
            for (size_t i = 0; i < model->getItemCount(); ++i)
            {
                const ItemStack item = model->getItem(static_cast<ItemModel::ModelIndex>(i));
                const MWWorld::Ptr base = item.mBase;

                std::string label = std::string(base.getClass().getName(base));
                // Condition as current/max, matching the on-screen charge bar.
                if (base.getClass().hasItemHealth(base))
                    label += ", " + std::string(winMgr->getGameSettingString("sCondition", "Condition")) + " "
                        + std::to_string(base.getClass().getItemHealth(base)) + "/"
                        + std::to_string(base.getClass().getItemMaxHealth(base));

                mA11y.add({ .widget = nullptr, .label = std::move(label),
                    .tooltips = [base] { return A11y::itemTooltipLines(base, 1); },
                    .activate = [this, base] { onRepairItem(nullptr, base); } });
            }
        }

        mA11y.add({ .widget = mCancelButton,
            .label = std::string(winMgr->getGameSettingString("sCancel", "Cancel")),
            .activate = [this] { onCancel(mCancelButton); } });
    }

    void Repair::a11yRebuildKeepingCursor()
    {
        const size_t cursor = mA11y.currentIndex();
        buildAccessibility();
        if (cursor == A11y::Screen::npos)
            mA11y.focusFirst(/*announce=*/true);
        else
            mA11y.selectIndex(std::min(cursor, mA11y.size() - 1), /*announce=*/true);
    }

    void Repair::setPtr(const MWWorld::Ptr& item)
    {
        if (item.isEmpty() || !item.getClass().isItem(item))
            throw std::runtime_error("Invalid argument in Repair::setPtr");

        MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Item Repair Up"));

        mRepair.setTool(item);

        mToolIcon->setItem(item);
        mToolIcon->setUserString("ToolTipType", "ItemPtr");
        mToolIcon->setUserData(MWWorld::Ptr(item));

        updateRepairView();
    }

    void Repair::updateRepairView()
    {
        MWWorld::LiveCellRef<ESM::Repair>* ref = mRepair.getTool().get<ESM::Repair>();

        int uses = mRepair.getTool().getClass().getItemHealth(mRepair.getTool());

        float quality = ref->mBase->mData.mQuality;

        mToolIcon->setUserData(mRepair.getTool());

        std::stringstream qualityStr;
        qualityStr << std::setprecision(3) << quality;

        mUsesLabel->setCaptionWithReplacing("#{sUses} " + MyGUI::utility::toString(uses));
        mQualityLabel->setCaptionWithReplacing("#{sQuality} " + qualityStr.str());

        bool toolBoxVisible = (mRepair.getTool().getCellRef().getCount() != 0);
        mToolBox->setVisible(toolBoxVisible);
        mToolBox->setUserString("Hidden", toolBoxVisible ? "false" : "true");

        if (!toolBoxVisible)
        {
            mToolIcon->setItem(MWWorld::Ptr());
            mToolIcon->clearUserStrings();
        }

        mRepairBox->update();

        Gui::Box* box = dynamic_cast<Gui::Box*>(mMainWidget);
        if (box == nullptr)
            throw std::runtime_error("main widget must be a box");

        box->notifyChildrenSizeChanged();
        center();

        // Refresh the screen-reader list now the tool + item views are current.
        // First run (right after setPtr on open) activates the screen; later
        // runs (tool swapped, item repaired) rebuild it keeping cursor position.
        if (mA11y.isActive())
            a11yRebuildKeepingCursor();
        else
        {
            buildAccessibility();
            mA11y.activate();
        }
    }

    void Repair::onSelectItem(MyGUI::Widget* /*sender*/)
    {
        mItemSelectionDialog = std::make_unique<ItemSelectionDialog>("#{sRepair}");
        mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &Repair::onItemSelected);
        mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &Repair::onItemCancel);
        mItemSelectionDialog->setVisible(true);
        mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
        mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyRepairTools);
    }

    void Repair::onItemSelected(MWWorld::Ptr item)
    {
        mItemSelectionDialog->setVisible(false);

        mToolIcon->setItem(item);
        mToolIcon->setUserString("ToolTipType", "ItemPtr");
        mToolIcon->setUserData(item);

        mRepair.setTool(item);

        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
        // updateRepairView() rebuilds and re-reads the screen-reader list (the
        // tool option now reflects the newly-chosen tool).
        updateRepairView();
    }

    void Repair::onItemCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void Repair::onCancel(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Repair);
    }

    void Repair::onRepairItem(MyGUI::Widget* /*sender*/, const MWWorld::Ptr& ptr)
    {
        if (!mRepair.getTool().getCellRef().getCount())
            return;

        mRepair.repair(ptr);

        // updateRepairView() rebuilds and re-reads the list, so the repaired
        // item's new condition and the tool's decremented uses are spoken.
        updateRepairView();
    }

    bool Repair::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if ((arg.button == SDL_CONTROLLER_BUTTON_A && !mToolBox->getVisible()) || arg.button == SDL_CONTROLLER_BUTTON_Y)
        {
            onSelectItem(mToolIcon);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onCancel(mCancelButton);
        else
            mRepairBox->onControllerButton(arg.button);

        return true;
    }
}
