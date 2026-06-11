#include "inventorywindow.hpp"

#include <cmath>
#include <stdexcept>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_Window.h>

#include <osg/Texture2D>

#include <components/debug/debuglog.hpp>

#include <components/misc/strings/algorithm.hpp>

#include <components/myguiplatform/myguitexture.hpp>

#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "accessibility/itemtext.hpp"
#include "accessibility/panegroup.hpp"
#include "accessibility/speech.hpp"
#include "companionwindow.hpp"
#include "container.hpp"
#include "countdialog.hpp"
#include "itemmodel.hpp"
#include "draganddrop.hpp"
#include "hud.hpp"
#include "inventoryitemmodel.hpp"
#include "itemtransfer.hpp"
#include "itemview.hpp"
#include "settings.hpp"
#include "sortfilteritemmodel.hpp"
#include "statswindow.hpp"
#include "tooltips.hpp"
#include "tradeitemmodel.hpp"
#include "tradewindow.hpp"

namespace
{

    bool isRightHandWeapon(const MWWorld::Ptr& item)
    {
        if (item.getClass().getType() != ESM::Weapon::sRecordId)
            return false;
        std::vector<int> equipmentSlots = item.getClass().getEquipmentSlots(item).first;
        return (!equipmentSlots.empty() && equipmentSlots.front() == MWWorld::InventoryStore::Slot_CarriedRight);
    }

}

namespace MWGui
{
    namespace
    {
        WindowSettingValues getModeSettings(GuiMode mode)
        {
            switch (mode)
            {
                case GM_Container:
                    return makeInventoryContainerWindowSettingValues();
                case GM_Companion:
                    return makeInventoryCompanionWindowSettingValues();
                case GM_Barter:
                    return makeInventoryBarterWindowSettingValues();
                default:
                    return makeInventoryWindowSettingValues();
            }
        }
    }

    InventoryWindow::InventoryWindow(DragAndDrop& dragAndDrop, ItemTransfer& itemTransfer, osg::Group* parent,
        Resource::ResourceSystem* resourceSystem)
        : WindowPinnableBase("openmw_inventory_window.layout")
        , mDragAndDrop(&dragAndDrop)
        , mItemTransfer(&itemTransfer)
        , mSelectedItem(-1)
        , mSortModel(nullptr)
        , mTradeModel(nullptr)
        , mGuiMode(GM_Inventory)
        , mLastXSize(0)
        , mLastYSize(0)
        , mPreview(std::make_unique<MWRender::InventoryPreview>(parent, resourceSystem, MWMechanics::getPlayer()))
        , mTrading(false)
        , mUpdateNextFrame(false)
        , mPendingControllerAction(ControllerAction::None)
    {
        mPreviewTexture
            = std::make_unique<MyGUIPlatform::OSGTexture>(mPreview->getTexture(), mPreview->getTextureStateSet());
        mPreview->rebuild();

        mMainWidget->castType<MyGUI::Window>()->eventWindowChangeCoord
            += MyGUI::newDelegate(this, &InventoryWindow::onWindowResize);

        getWidget(mAvatar, "Avatar");
        getWidget(mAvatarImage, "AvatarImage");
        getWidget(mEncumbranceBar, "EncumbranceBar");
        getWidget(mFilterAll, "AllButton");
        getWidget(mFilterWeapon, "WeaponButton");
        getWidget(mFilterApparel, "ApparelButton");
        getWidget(mFilterMagic, "MagicButton");
        getWidget(mFilterMisc, "MiscButton");
        getWidget(mLeftPane, "LeftPane");
        getWidget(mRightPane, "RightPane");
        getWidget(mArmorRating, "ArmorRating");
        getWidget(mFilterEdit, "FilterEdit");

        mAvatarImage->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onAvatarClicked);
        mAvatarImage->setRenderItemTexture(mPreviewTexture.get());
        // The widget is Y-down, the RTT image is Y-up, so this UV is inverted
        mAvatarImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 1.f, 1.f, 0.f));

        getWidget(mItemView, "ItemView");
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &InventoryWindow::onItemSelected);
        mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &InventoryWindow::onBackgroundSelected);

        mFilterAll->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterWeapon->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterApparel->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterMagic->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterMisc->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &InventoryWindow::onNameFilterChanged);

        mFilterAll->setStateSelected(true);

        // Screen-reader setup: an invisible anchor holds key focus while the
        // item list is navigated by index (the items are drawn by the custom
        // ItemView, not as individual widgets), as in the container window.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);
        mA11yFilterEdit.attach(mFilterEdit);
        mA11yFilterEdit.setActive(false);
        // Extra keys on the item list:
        //  - Ctrl+Left/Right cycle the category filter (All/Weapon/.../Misc).
        //  - Delete drops the selected item (count picker first for a stack).
        //  - E reports encumbrance + armor rating on demand.
        mA11y.setExtraKeyHandler([this](MyGUI::KeyCode key) -> bool {
            const bool ctrl = MyGUI::InputManager::getInstance().isControlPressed();
            if (ctrl && (key == MyGUI::KeyCode::ArrowLeft || key == MyGUI::KeyCode::ArrowRight))
            {
                a11yCycleCategory(key == MyGUI::KeyCode::ArrowRight);
                return true;
            }
            if (key == MyGUI::KeyCode::Delete)
            {
                const size_t cur = mA11y.currentIndex();
                // Item rows follow the leading filter/category options; map the
                // list index onto a sort-model row.
                if (mSortModel && cur != A11y::Screen::npos && cur >= mA11yItemBase)
                {
                    const int index = static_cast<int>(cur - mA11yItemBase);
                    if (index < static_cast<int>(mSortModel->getItemCount()))
                        a11yActivateItem(index, /*drop=*/true);
                }
                return true;
            }
            if (key == MyGUI::KeyCode::E)
            {
                A11y::say(a11yEncumbranceValue(), /*interrupt=*/true);
                return true;
            }
            // S stores the selected item into the open container (the accessible
            // counterpart to Alt+clicking it across). Only meaningful while a
            // container window is open alongside; in plain inventory or barter
            // there's nowhere to store, so let the key fall through. Shift+S
            // stores the whole stack; otherwise the count picker opens for a
            // stack.
            if (key == MyGUI::KeyCode::S && mGuiMode == GM_Container)
            {
                const size_t cur = mA11y.currentIndex();
                if (mSortModel && cur != A11y::Screen::npos && cur >= mA11yItemBase)
                {
                    const int index = static_cast<int>(cur - mA11yItemBase);
                    if (index < static_cast<int>(mSortModel->getItemCount()))
                        a11yStoreItem(index, MyGUI::InputManager::getInstance().isShiftPressed());
                }
                return true;
            }
            // In barter, the balance/offer keys are shared with the merchant
            // pane: forward them so the player can read and adjust the running
            // total and submit the offer from the inventory side too.
            if (mTrading)
            {
                TradeWindow* trade = MWBase::Environment::get().getWindowManager()->getTradeWindow();
                if (trade && trade->a11yHandleBalanceKey(key))
                    return true;
            }
            return false;
        });

        setGuiMode(mGuiMode);

        if (Settings::gui().mControllerMenus)
        {
            // Show L1 and R1 buttons next to tabs
            MyGUI::ImageBox* image;
            getWidget(image, "BtnL1Image");
            image->setVisible(true);
            image->setUserString("Hidden", "false");
            image->setImageTexture(MWBase::Environment::get().getInputManager()->getControllerButtonIcon(
                SDL_CONTROLLER_BUTTON_LEFTSHOULDER));

            getWidget(image, "BtnR1Image");
            image->setVisible(true);
            image->setUserString("Hidden", "false");
            image->setImageTexture(MWBase::Environment::get().getInputManager()->getControllerButtonIcon(
                SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));

            mControllerButtons.mR3 = "#{Interface:Info}";
        }

        adjustPanes();
    }

    void InventoryWindow::adjustPanes()
    {
        const float aspect = 0.5; // fixed aspect ratio for the avatar image
        int leftPaneWidth = static_cast<int>((mMainWidget->getSize().height - 44 - mArmorRating->getHeight()) * aspect);
        mLeftPane->setSize(leftPaneWidth, mMainWidget->getSize().height - 44);
        mRightPane->setCoord(mLeftPane->getPosition().left + leftPaneWidth + 4, mRightPane->getPosition().top,
            mMainWidget->getSize().width - 12 - leftPaneWidth - 15, mMainWidget->getSize().height - 44);
    }

    void InventoryWindow::updatePlayer()
    {
        mPtr = MWBase::Environment::get().getWorld()->getPlayerPtr();
        auto tradeModel = std::make_unique<TradeItemModel>(std::make_unique<InventoryItemModel>(mPtr), MWWorld::Ptr());
        mTradeModel = tradeModel.get();

        if (mSortModel) // reuse existing SortModel when possible to keep previous category/filter settings
            mSortModel->setSourceModel(std::move(tradeModel));
        else
        {
            auto sortModel = std::make_unique<SortFilterItemModel>(std::move(tradeModel));
            mSortModel = sortModel.get();
            mItemView->setModel(std::move(sortModel));
        }

        mSortModel->setNameFilter(mFilterEdit->getCaption());

        mFilterAll->setStateSelected(true);
        mFilterWeapon->setStateSelected(false);
        mFilterApparel->setStateSelected(false);
        mFilterMagic->setStateSelected(false);
        mFilterMisc->setStateSelected(false);

        mPreview->updatePtr(mPtr);
        mPreview->rebuild();
        mPreview->update();

        dirtyPreview();

        updatePreviewSize();

        updateEncumbranceBar();
        mItemView->update();
        notifyContentChanged();
    }

    void InventoryWindow::clear()
    {
        mPtr = MWWorld::Ptr();
        mTradeModel = nullptr;
        mSortModel = nullptr;
        mItemView->setModel(nullptr);
    }

    void InventoryWindow::toggleMaximized()
    {
        const WindowSettingValues settings = getModeSettings(mGuiMode);
        const WindowRectSettingValues& rect = settings.mIsMaximized ? settings.mRegular : settings.mMaximized;

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        const int x = static_cast<int>(rect.mX * viewSize.width);
        const int y = static_cast<int>(rect.mY * viewSize.height);
        const int w = static_cast<int>(rect.mW * viewSize.width);
        const int h = static_cast<int>(rect.mH * viewSize.height);
        MyGUI::Window* window = mMainWidget->castType<MyGUI::Window>();
        window->setCoord(x, y, w, h);

        settings.mIsMaximized.set(!settings.mIsMaximized);

        adjustPanes();
        updatePreviewSize();
    }

    void InventoryWindow::setGuiMode(GuiMode mode)
    {
        if (Settings::gui().mControllerMenus && mGuiMode == mode && isVisible())
            return;

        mGuiMode = mode;
        const WindowSettingValues settings = getModeSettings(mGuiMode);
        setPinButtonVisible(
            mode != GM_Container && mode != GM_Companion && mode != GM_Barter && !Settings::gui().mControllerMenus);

        const WindowRectSettingValues& rect = settings.mIsMaximized ? settings.mMaximized : settings.mRegular;

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        MyGUI::IntPoint pos(static_cast<int>(rect.mX * viewSize.width), static_cast<int>(rect.mY * viewSize.height));
        MyGUI::IntSize size(static_cast<int>(rect.mW * viewSize.width), static_cast<int>(rect.mH * viewSize.height));

        bool needUpdate = (size.width != mMainWidget->getWidth() || size.height != mMainWidget->getHeight());

        mMainWidget->setPosition(pos);
        mMainWidget->setSize(size);

        adjustPanes();

        if (needUpdate)
            updatePreviewSize();
    }

    SortFilterItemModel* InventoryWindow::getSortFilterModel()
    {
        return mSortModel;
    }

    TradeItemModel* InventoryWindow::getTradeModel()
    {
        return mTradeModel;
    }

    ItemModel* InventoryWindow::getModel()
    {
        return mTradeModel;
    }

    void InventoryWindow::onBackgroundSelected()
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
            mDragAndDrop->drop(mTradeModel, mItemView);
    }

    std::string InventoryWindow::a11yItemLabel(const ItemStack& item) const
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        std::string label = std::string(item.mBase.getClass().getName(item.mBase));
        if (item.mCount > 1)
            label += " (" + std::to_string(item.mCount) + ")";
        // Surface the equipped state, which is the inventory's key extra signal
        // over a plain container list.
        if (item.mType == ItemStack::Type_Equipped)
            label += ", " + std::string(winMgr->getGameSettingString("sEquip", "Equipped"));
        // In barter mode, append the barter price for this stack so the player
        // can judge it without adding it to the offer first. Most rows are the
        // player's own goods, priced at what the merchant would PAY for them
        // (selling price). But a Type_Barter row here is a merchant item the
        // player is buying (lent to us, pending purchase) -- so it carries the
        // BUYING price instead, matching its contribution to the balance.
        const bool onOffer = item.mType == ItemStack::Type_Barter;
        if (mTrading && !mPtr.isEmpty())
        {
            float price = static_cast<float>(item.mBase.getClass().getValue(item.mBase));
            if (item.mBase.getClass().hasItemHealth(item.mBase))
                price *= item.mBase.getClass().getItemNormalizedHealth(item.mBase);
            const int basePrice = static_cast<int>(price * item.mCount);
            const MWWorld::Ptr merchant = winMgr->getTradeWindow()->mPtr;
            int shownPrice = basePrice;
            if (!merchant.isEmpty())
            {
                const int cap = static_cast<int>(std::max(1.f, 0.75f * basePrice));
                if (onOffer)
                {
                    const int buyingPrice
                        = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(merchant, basePrice, true);
                    shownPrice = std::max(cap, buyingPrice);
                }
                else
                {
                    const int offer
                        = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(merchant, basePrice, false);
                    shownPrice = merchant.getClass().isNpc() ? std::min(cap, offer) : offer;
                }
            }
            label += ", " + std::string(winMgr->getGameSettingString("sValue", "Value")) + " "
                + std::to_string(shownPrice);
        }
        // Items bought from the merchant (not yet paid for) are lent to the
        // player and show here marked Type_Barter. Enter retracts the purchase.
        if (mTrading && onOffer)
            label += ", on offer";
        return label;
    }

    long long InventoryWindow::a11yTradeSignature() const
    {
        // Fold the item list's size and each stack's count + barter state into a
        // cheap rolling hash. Any sale/purchase/retraction changes it. Mirrors
        // TradeWindow::a11yTradeSignature.
        if (!mSortModel)
            return 0;
        long long sig = 1469598103934665603LL; // FNV offset basis
        const auto mix = [&sig](long long v) { sig = (sig ^ v) * 1099511628211LL; };
        const size_t count = mSortModel->getItemCount();
        mix(static_cast<long long>(count));
        for (size_t i = 0; i < count; ++i)
        {
            const ItemStack item = mSortModel->getItem(static_cast<int>(i));
            mix(static_cast<long long>(item.mCount));
            mix(static_cast<long long>(item.mType));
        }
        return sig;
    }

    std::string InventoryWindow::a11yCategoryName() const
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        switch (mA11yCategoryIndex)
        {
            case 1:
                return std::string(winMgr->getGameSettingString("sWeapon", "Weapon"));
            case 2:
                return std::string(winMgr->getGameSettingString("sApparel", "Apparel"));
            case 3:
                return std::string(winMgr->getGameSettingString("sMagic", "Magic"));
            case 4:
                return std::string(winMgr->getGameSettingString("sMisc", "Misc"));
            default:
                return std::string(winMgr->getGameSettingString("sAll", "All"));
        }
    }

    void InventoryWindow::a11yCycleCategory(bool next)
    {
        mA11yCategoryIndex = (mA11yCategoryIndex + (next ? 1 : 4)) % 5; // -1 mod 5

        // Drive the same path a mouse click would: select the matching filter
        // button so the on-screen UI and model stay in sync.
        MyGUI::Button* buttons[] = { mFilterAll, mFilterWeapon, mFilterApparel, mFilterMagic, mFilterMisc };
        onFilterChanged(buttons[mA11yCategoryIndex]);

        // Rebuild the spoken list for the new filter and land back on the first
        // item; announce the category we switched to first.
        A11y::say(a11yCategoryName(), /*interrupt=*/true);
        buildAccessibility();
        mA11y.selectIndex(mA11yItemBase, /*announce=*/true);
    }

    std::string InventoryWindow::a11yEncumbranceValue() const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        const int capacity = static_cast<int>(player.getClass().getCapacity(player));
        const int encumbrance = static_cast<int>(std::ceil(player.getClass().getEncumbrance(player)));
        const int armor = static_cast<int>(player.getClass().getArmorRating(player, true));
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        std::string out = std::string(winMgr->getGameSettingString("sEncumbrance", "Encumbrance")) + ": "
            + std::to_string(encumbrance) + " / " + std::to_string(capacity) + ". "
            + std::string(winMgr->getGameSettingString("sArmor", "Armor")) + ": " + std::to_string(armor);
        return out;
    }

    void InventoryWindow::buildAccessibility()
    {
        mA11y.clear();

        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        // Leading options: name-filter field, then the category filter.
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sName", "Name")),
            .value =
                [this] {
                    const std::string text = mFilterEdit->getOnlyText().asUTF8();
                    return text.empty() ? std::string("blank") : text;
                },
            .edit = &mA11yFilterEdit });

        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sShowAll", "Category")),
            .value = [this] { return a11yCategoryName(); },
            .change = [this](bool next) { a11yCycleCategory(next); } });

        mA11yItemBase = 2;

        // One option per item stack. Label carries name/count/equipped; the
        // T-key tooltip carries weight/value/effects. Enter equips or uses the
        // item (mirroring a normal click); Delete drops it (handled centrally
        // in the extra-key handler).
        if (mSortModel)
        {
            for (size_t i = 0; i < mSortModel->getItemCount(); ++i)
            {
                const int index = static_cast<int>(i);
                const ItemStack item = mSortModel->getItem(index);
                mA11y.add({ .widget = nullptr,
                    .label = a11yItemLabel(item),
                    .tooltips = [base = item.mBase, count = item.mCount]
                    { return A11y::itemTooltipLines(base, static_cast<int>(count)); },
                    .activate = [this, index] { a11yActivateItem(index, /*drop=*/false); } });
            }
        }
    }

    void InventoryWindow::a11yActivateItem(int sortIndex, bool drop)
    {
        if (!mSortModel || mTradeModel == nullptr)
            return;
        if (sortIndex < 0 || sortIndex >= static_cast<int>(mSortModel->getItemCount()))
            return;

        const ItemStack item = mSortModel->getItem(sortIndex);
        const int sourceIndex = mSortModel->mapToSource(sortIndex);

        if (drop)
        {
            // Can't drop a conjured/bound item.
            if (item.mFlags & ItemStack::Flag_Bound)
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sContentsMessage1}");
                return;
            }

            const size_t count = item.mCount;
            if (count > 1)
            {
                // Offer the accessible count picker for a partial drop.
                std::string name{ item.mBase.getClass().getName(item.mBase) };
                name += MWGui::ToolTips::getSoulString(item.mBase.getCellRef());
                CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
                dialog->openCountDialog(name, "#{sDrop}", static_cast<int>(count));
                dialog->eventOkClicked.clear();
                dialog->eventOkClicked += MyGUI::newDelegate(this, &InventoryWindow::onA11yCountDropped);
                mSelectedItem = sourceIndex;
                return;
            }

            mSelectedItem = sourceIndex;
            dropItem(nullptr, count);
            a11yStartFollowing(item);
            return;
        }

        // In barter mode, Enter sells (borrows the item to the merchant)
        // instead of equipping. onItemSelectedFromSourceModel runs the same
        // trade path the mouse uses: it validates the item is sellable and
        // opens the accessible count picker for a stack, otherwise sells one.
        // Don't use the equip-follow mechanism here: borrowing fires no
        // onInventoryUpdate, so the spoken list is instead rebuilt off the trade
        // signature in onFrame (which also catches the deferred count-dialog
        // confirm). Keep the cursor where it is.
        if (mTrading)
        {
            onItemSelectedFromSourceModel(sourceIndex);
            return;
        }

        // Equip / use the whole interaction the same way a click does. The
        // change is applied asynchronously (drag&drop now, the Lua-driven equip
        // a few frames later) and reorders the list, so we follow the item until
        // its state settles (handled in onFrame) rather than guessing when to
        // rebuild.
        mSelectedItem = sourceIndex;
        equipItem(item.mCount);
        a11yStartFollowing(item);
    }

    void InventoryWindow::a11yStartFollowing(const ItemStack& item)
    {
        mA11yFollowItem = item.mBase;
        mA11yFollowTimer = 0.f;
        // Capture the pre-action signature so a11yUpdateFollow can tell when the
        // async equip/use/drop has actually landed (the model mutates a frame or
        // more later, and equip/unequip fires no onInventoryUpdate at all).
        mA11yFollowWasEquipped = item.mType == ItemStack::Type_Equipped;
        mA11yFollowCount = item.mCount;
        mA11yFollowItemTotal = mSortModel ? mSortModel->getItemCount() : 0;
    }

    void InventoryWindow::onA11yCountDropped(MyGUI::Widget* sender, std::size_t count)
    {
        // Grab the item identity before the drop mutates the model.
        MWWorld::Ptr dropped;
        if (mTradeModel && mSelectedItem >= 0 && mSelectedItem < static_cast<int>(mTradeModel->getItemCount()))
            dropped = mTradeModel->getItem(mSelectedItem).mBase;

        dropItem(sender, count);

        // A partial drop leaves a smaller stack of the same item behind; follow
        // it so the announced count reflects the remainder (a full drop removes
        // it, which a11yUpdateFollow handles by detecting the item is gone).
        if (mSortModel && !dropped.isEmpty())
        {
            for (size_t i = 0; i < mSortModel->getItemCount(); ++i)
            {
                const ItemStack stack = mSortModel->getItem(static_cast<int>(i));
                if (stack.mBase == dropped)
                {
                    a11yStartFollowing(stack);
                    return;
                }
            }
            // Item fully gone: follow the now-removed identity so the update
            // logic announces the row's new occupant.
            mA11yFollowItem = dropped;
            mA11yFollowTimer = 0.f;
            mA11yFollowItemTotal = mSortModel->getItemCount() + 1; // was one more before drop
        }
    }

    void InventoryWindow::a11yStoreItem(int sortIndex, bool wholeStack)
    {
        if (!mSortModel || mTradeModel == nullptr)
            return;
        if (mGuiMode != GM_Container)
            return;
        if (sortIndex < 0 || sortIndex >= static_cast<int>(mSortModel->getItemCount()))
            return;

        const ItemStack item = mSortModel->getItem(sortIndex);
        const int sourceIndex = mSortModel->mapToSource(sortIndex);

        // Bound/conjured items can't be transferred out (ItemTransfer::apply
        // would just message-box and refuse); guard here so we don't follow a
        // move that never happens.
        if (item.mFlags & ItemStack::Flag_Bound)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
            return;
        }

        mSelectedItem = sourceIndex;
        const size_t count = item.mCount;

        if (!wholeStack && count > 1)
        {
            // Open the accessible count picker; on OK it transfers the chosen
            // amount into the container/companion. Label "Take" matches the
            // wording the container side uses for the equivalent picker.
            std::string name{ item.mBase.getClass().getName(item.mBase) };
            name += MWGui::ToolTips::getSoulString(item.mBase.getCellRef());
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            dialog->openCountDialog(name, "#{sTake}", static_cast<int>(count));
            dialog->eventOkClicked.clear();
            dialog->eventOkClicked += MyGUI::newDelegate(this, &InventoryWindow::onA11yCountStored);
            return;
        }

        // Move the whole stack across, then follow the item so the spoken list
        // and cursor track the remainder (partial) or the row's new occupant
        // (full move) -- same mechanism as a drop.
        transferItem(nullptr, count);
        a11yStartFollowing(item);
    }

    void InventoryWindow::onA11yCountStored(MyGUI::Widget* sender, std::size_t count)
    {
        // Grab the item identity before the transfer mutates the model, then
        // follow it (or its now-removed row) like onA11yCountDropped does.
        MWWorld::Ptr stored;
        if (mTradeModel && mSelectedItem >= 0 && mSelectedItem < static_cast<int>(mTradeModel->getItemCount()))
            stored = mTradeModel->getItem(mSelectedItem).mBase;

        transferItem(sender, count);

        if (mSortModel && !stored.isEmpty())
        {
            for (size_t i = 0; i < mSortModel->getItemCount(); ++i)
            {
                const ItemStack stack = mSortModel->getItem(static_cast<int>(i));
                if (stack.mBase == stored)
                {
                    a11yStartFollowing(stack);
                    return;
                }
            }
            mA11yFollowItem = stored;
            mA11yFollowTimer = 0.f;
            mA11yFollowItemTotal = mSortModel->getItemCount() + 1; // was one more before the move
        }
    }

    void InventoryWindow::a11yRebuildKeepingCursor()
    {
        const size_t cursor = mA11y.currentIndex();
        buildAccessibility();
        const size_t itemCount = mSortModel ? mSortModel->getItemCount() : 0;
        if (cursor == A11y::Screen::npos)
            mA11y.focusFirst(/*announce=*/true);
        else if (cursor < mA11yItemBase)
            mA11y.selectIndex(cursor, /*announce=*/true); // stayed on a filter option
        else if (itemCount == 0)
            mA11y.selectIndex(mA11yItemBase - 1, /*announce=*/true); // no items: land on category
        else
        {
            const size_t item = std::min(cursor - mA11yItemBase, itemCount - 1);
            mA11y.selectIndex(mA11yItemBase + item, /*announce=*/true);
        }
    }

    void InventoryWindow::a11yUpdateFollow(bool contentUpdated, float dt)
    {
        // Not following: keep the spoken list in sync with the on-screen ItemView
        // on a passive content update (e.g. an item picked up while the menu is
        // open), preserving the cursor position silently.
        if (mA11yFollowItem.isEmpty())
        {
            if (contentUpdated)
            {
                const size_t cursor = mA11y.currentIndex();
                buildAccessibility();
                const size_t itemCount = mSortModel ? mSortModel->getItemCount() : 0;
                if (mA11y.isActive() && cursor != A11y::Screen::npos)
                {
                    if (cursor < mA11yItemBase)
                        mA11y.selectIndex(cursor, /*announce=*/false);
                    else if (itemCount > 0)
                        mA11y.selectIndex(
                            mA11yItemBase + std::min(cursor - mA11yItemBase, itemCount - 1), /*announce=*/false);
                }
            }
            return;
        }

        // Following an item across an asynchronous equip/use/drop. The change
        // lands a frame or more after the keypress; crucially, equip/unequip
        // only flips the item's equipped flag and fires NO onInventoryUpdate, so
        // we cannot wait on contentUpdated -- instead we POLL the model every
        // frame and detect when the followed item's signature has changed from
        // what it was at action time (equipped flag, count, or total item
        // count). Once it has (or it vanished), we rebuild, land on it, announce
        // once, and stop following.
        mA11yFollowTimer += dt;

        // Refresh the model + on-screen view so we read LIVE state. Equip/unequip
        // mutates the inventory store but fires no onInventoryUpdate, so without
        // this the cached item list (and the visible UI) would stay stale -- the
        // exact symptom of the equip not showing until the menu is reopened.
        updateItemView();

        int found = -1;
        ItemStack foundItem;
        const size_t itemCount = mSortModel ? mSortModel->getItemCount() : 0;
        if (mSortModel)
        {
            for (size_t i = 0; i < itemCount; ++i)
            {
                const ItemStack stack = mSortModel->getItem(static_cast<int>(i));
                if (stack.mBase == mA11yFollowItem)
                {
                    found = static_cast<int>(i);
                    foundItem = stack;
                    break;
                }
            }
        }

        // Has the change landed? Either the item is gone, the total item count
        // changed (use/drop), or the item's equipped/count signature flipped.
        const bool gone = (found < 0);
        const bool changed = gone || itemCount != mA11yFollowItemTotal
            || (foundItem.mType == ItemStack::Type_Equipped) != mA11yFollowWasEquipped
            || foundItem.mCount != mA11yFollowCount;

        // Wait for the change unless we've exhausted the settle window.
        if (!changed && mA11yFollowTimer <= 0.6f)
            return;

        // Settled (or timed out): refresh the list and announce the result once.
        buildAccessibility();
        if (mA11y.isActive())
        {
            if (found >= 0)
                // QUEUE this announcement (announce=true, non-interrupting)
                // rather than interrupting in-progress speech. Equipping an item
                // can itself trigger game speech -- most notably a tutorial popup
                // (the early-game "you equipped a weapon" message): interrupting
                // clobbered that popup's text, leaving the player with a modal
                // dialog they couldn't hear and arrow keys that "didn't work"
                // because focus was trapped on the unheard popup. Queuing lets
                // the equip result follow any such message instead of cutting it
                // off. We only announce once per action (mA11yFollowItem is
                // cleared just below), so there's no stale-announcement pile-up
                // to interrupt away.
                mA11y.selectIndex(mA11yItemBase + static_cast<size_t>(found), /*announce=*/true);
            else
                a11yRebuildKeepingCursor(); // item gone (fully dropped/used)
        }
        mA11yFollowItem = MWWorld::Ptr();
    }

    void InventoryWindow::onItemSelected(int index)
    {
        onItemSelectedFromSourceModel(mSortModel->mapToSource(index));
    }

    void InventoryWindow::onItemSelectedFromSourceModel(int index)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            mDragAndDrop->drop(mTradeModel, mItemView);
            return;
        }

        const ItemStack& item = mTradeModel->getItem(index);
        const ESM::RefId& sound = item.mBase.getClass().getDownSoundId(item.mBase);

        MWWorld::Ptr object = item.mBase;
        size_t count = item.mCount;
        bool shift = MyGUI::InputManager::getInstance().isShiftPressed();

        if (MyGUI::InputManager::getInstance().isControlPressed())
            count = 1;

        if (mTrading)
        {
            // Can't give conjured items to a merchant
            if (item.mFlags & ItemStack::Flag_Bound)
            {
                MWBase::Environment::get().getWindowManager()->playSound(sound);
                MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog9}");
                return;
            }

            // check if merchant accepts item
            int services = MWBase::Environment::get().getWindowManager()->getTradeWindow()->getMerchantServices();
            if (!object.getClass().canSell(object, services))
            {
                MWBase::Environment::get().getWindowManager()->playSound(sound);
                MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog4}");
                return;
            }
        }

        // If we unequip weapon during attack, it can lead to unexpected behaviour
        if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(mPtr))
        {
            MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
            MWWorld::ContainerStoreIterator weapIt = invStore.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
            bool weapActive = mPtr.getClass().getCreatureStats(mPtr).getDrawState() == MWMechanics::DrawState::Weapon;
            if (weapActive && weapIt != invStore.end() && *weapIt == item.mBase)
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sCantEquipWeapWarning}");
                return;
            }
        }

        // Show a dialog to select a count of items, but not when using an item from the inventory
        // in controller mode. In that case, we skip the dialog and just use one item immediately.
        if (count > 1 && !shift && mPendingControllerAction != ControllerAction::Use)
        {
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            std::string message = "#{sTake}";
            if (mTrading || mPendingControllerAction == ControllerAction::Sell)
                message = "#{sQuanityMenuMessage01}";
            else if (mPendingControllerAction == ControllerAction::Drop)
                message = "#{sDrop}";
            std::string name{ object.getClass().getName(object) };
            name += MWGui::ToolTips::getSoulString(object.getCellRef());
            dialog->openCountDialog(name, message, static_cast<int>(count));
            dialog->eventOkClicked.clear();
            if (mTrading || mPendingControllerAction == ControllerAction::Sell)
                dialog->eventOkClicked += MyGUI::newDelegate(this, &InventoryWindow::sellItem);
            else if (mPendingControllerAction == ControllerAction::Drop)
                dialog->eventOkClicked += MyGUI::newDelegate(this, &InventoryWindow::dropItem);
            else if (MyGUI::InputManager::getInstance().isAltPressed()
                || mPendingControllerAction == ControllerAction::Transfer)
                dialog->eventOkClicked += MyGUI::newDelegate(this, &InventoryWindow::transferItem);
            else
                dialog->eventOkClicked += MyGUI::newDelegate(this, &InventoryWindow::dragItem);

            mSelectedItem = index;
        }
        else
        {
            mSelectedItem = index;

            if (mTrading || mPendingControllerAction == ControllerAction::Sell)
                sellItem(nullptr, count);
            else if (mPendingControllerAction == ControllerAction::Use)
                equipItem(count);
            else if (mPendingControllerAction == ControllerAction::Drop)
                dropItem(nullptr, count);
            else if (MyGUI::InputManager::getInstance().isAltPressed()
                || mPendingControllerAction == ControllerAction::Transfer)
                transferItem(nullptr, count);
            else
                dragItem(nullptr, count);
        }

        mPendingControllerAction = ControllerAction::None;
    }

    void InventoryWindow::ensureSelectedItemUnequipped(int count)
    {
        const ItemStack& item = mTradeModel->getItem(mSelectedItem);
        if (item.mType == ItemStack::Type_Equipped)
        {
            MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
            MWWorld::Ptr newStack = *invStore.unequipItemQuantity(item.mBase, count);

            // The unequipped item was re-stacked. We have to update the index
            // since the item pointed does not exist anymore.
            if (item.mBase != newStack)
            {
                updateItemView(); // Unequipping can produce a new stack, not yet in the window...

                // newIndex will store the index of the ItemStack the item was stacked on
                int newIndex = -1;
                for (size_t i = 0; i < mTradeModel->getItemCount(); ++i)
                {
                    if (mTradeModel->getItem(static_cast<ItemModel::ModelIndex>(i)).mBase == newStack)
                    {
                        newIndex = static_cast<int>(i);
                        break;
                    }
                }

                if (newIndex == -1)
                    throw std::runtime_error("Can't find restacked item");

                mSelectedItem = newIndex;
            }
        }
    }

    void InventoryWindow::dragItem(MyGUI::Widget* /*sender*/, std::size_t count)
    {
        ensureSelectedItemUnequipped(static_cast<int>(count));
        mDragAndDrop->startDrag(mSelectedItem, mSortModel, mTradeModel, mItemView, count);
        notifyContentChanged();
    }

    void InventoryWindow::transferItem(MyGUI::Widget* /*sender*/, std::size_t count)
    {
        ensureSelectedItemUnequipped(static_cast<int>(count));
        mItemTransfer->apply(mTradeModel->getItem(mSelectedItem), count, *mItemView);
        notifyContentChanged();
    }

    void InventoryWindow::sellItem(MyGUI::Widget* /*sender*/, std::size_t count)
    {
        ensureSelectedItemUnequipped(static_cast<int>(count));
        const ItemStack& item = mTradeModel->getItem(mSelectedItem);
        const ESM::RefId& sound = item.mBase.getClass().getUpSoundId(item.mBase);
        MWBase::Environment::get().getWindowManager()->playSound(sound);

        if (item.mType == ItemStack::Type_Barter)
        {
            // this was an item borrowed to us by the merchant
            mTradeModel->returnItemBorrowedToUs(mSelectedItem, count);
            MWBase::Environment::get().getWindowManager()->getTradeWindow()->returnItem(mSelectedItem, count);
        }
        else
        {
            // borrow item to the merchant
            mTradeModel->borrowItemFromUs(mSelectedItem, count);
            MWBase::Environment::get().getWindowManager()->getTradeWindow()->borrowItem(mSelectedItem, count);
        }

        mItemView->update();
        notifyContentChanged();
    }

    void InventoryWindow::dropItem(MyGUI::Widget* sender, size_t count)
    {
        if (mGuiMode != MWGui::GM_Inventory)
            return;

        if (!mDragAndDrop->mIsOnDragAndDrop)
            dragItem(sender, count);

        // Drop the item into the gameworld
        if (mDragAndDrop->mIsOnDragAndDrop)
            MWBase::Environment::get().getWindowManager()->getHud()->dropDraggedItem(0.5f, 0.5f);
    }

    void InventoryWindow::equipItem(std::size_t count)
    {
        const ItemStack& item = mTradeModel->getItem(mSelectedItem);
        ensureSelectedItemUnequipped(static_cast<int>(count));
        // Disable the pick up sound as the item will be used immediately
        mDragAndDrop->startDrag(mSelectedItem, mSortModel, mTradeModel, mItemView, count, false);
        notifyContentChanged();

        const bool wasEquipped = item.mType == ItemStack::Type_Equipped;
        // Drop the item on the avatar to activate or equip it.
        if (!wasEquipped)
            onAvatarClicked(nullptr);

        // Drop the item to unequip it or drop any remaining items back in inventory.
        // This is needed when clicking on a stack of items; we only want to use the first item.
        if (mDragAndDrop->mIsOnDragAndDrop)
            mDragAndDrop->drop(mTradeModel, mItemView, wasEquipped);
    }

    void InventoryWindow::updateItemView()
    {
        MWBase::Environment::get().getWindowManager()->updateSpellWindow();

        mItemView->update();

        dirtyPreview();
    }

    void InventoryWindow::onOpen()
    {
        // Reset the filter focus when opening the window
        MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        if (focus == mFilterEdit)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);

        if (!mPtr.isEmpty())
        {
            updateEncumbranceBar();
            mItemView->update();
            notifyContentChanged();
        }
        adjustPanes();

        mItemTransfer->addTarget(*mItemView);

        // Screen reader: in the standalone inventory the window is shown next to
        // Stats/Spells/Map, so enrol in the PaneGroup (Inventory = pane 1) and
        // let Tab switch between them. In barter mode it's shown next to the
        // merchant's TradeWindow (which enrols itself as pane 0), so enrol here
        // too -- selling happens in this pane. In container mode it's shown next
        // to the loot window (which enrols itself as pane 0), so enrol here as
        // well so the player can Tab to their own inventory and store items into
        // the container with the S key. (Companion mode has no accessible pane on
        // the other side yet, so it's excluded until that window is made
        // accessible.)
        if (mGuiMode == GM_Inventory || mGuiMode == GM_Barter || mGuiMode == GM_Container)
        {
            mA11yLastTradeSig = -1; // force a barter rebuild on the first frame
            buildAccessibility();
            A11y::PaneGroup::instance().enrol(&mA11y,
                std::string(
                    MWBase::Environment::get().getWindowManager()->getGameSettingString("sInventory", "Inventory")),
                1);
        }
    }

    void InventoryWindow::onClose()
    {
        mItemTransfer->removeTarget(*mItemView);

        A11y::PaneGroup::instance().withdraw(&mA11y);
        mA11y.deactivate();

        // If the inventory mode is gone for good (not just hidden behind a
        // sub-mode like reading a book), forget which pane was active so the
        // next fresh open lands on Stats again. While a book/scroll is open the
        // mode is still on the stack, so the memory survives and we return to
        // the pane the user left.
        if (!MWBase::Environment::get().getWindowManager()->containsMode(GM_Inventory))
            A11y::PaneGroup::instance().resetMemory();
    }

    void InventoryWindow::onWindowResize(MyGUI::Window* sender)
    {
        WindowBase::clampWindowCoordinates(sender);

        adjustPanes();
        const WindowSettingValues settings = getModeSettings(mGuiMode);

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();

        settings.mRegular.mX.set(sender->getPosition().left / static_cast<float>(viewSize.width));
        settings.mRegular.mY.set(sender->getPosition().top / static_cast<float>(viewSize.height));
        settings.mRegular.mW.set(sender->getSize().width / static_cast<float>(viewSize.width));
        settings.mRegular.mH.set(sender->getSize().height / static_cast<float>(viewSize.height));
        settings.mIsMaximized.set(false);

        if (mMainWidget->getSize().width != mLastXSize || mMainWidget->getSize().height != mLastYSize)
        {
            mLastXSize = mMainWidget->getSize().width;
            mLastYSize = mMainWidget->getSize().height;

            updatePreviewSize();
            updateArmorRating();
        }
    }

    void InventoryWindow::updateArmorRating()
    {
        if (mPtr.isEmpty())
            return;

        auto rating = MyGUI::utility::toString(static_cast<int>(mPtr.getClass().getArmorRating(mPtr, true)));
        mArmorRating->setCaptionWithReplacing("#{sArmor}: " + rating);
        if (mArmorRating->getTextSize().width > mArmorRating->getSize().width)
            mArmorRating->setCaptionWithReplacing(rating);
    }

    void InventoryWindow::updatePreviewSize()
    {
        const MyGUI::IntSize viewport = getPreviewViewportSize();
        mPreview->setViewport(viewport.width, viewport.height);
        const float top = viewport.height / static_cast<float>(mPreview->getTextureHeight());
        const float right = viewport.width / static_cast<float>(mPreview->getTextureWidth());
        // The widget is Y-down, the RTT image is Y-up, so this UV is inverted
        mAvatarImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, top, right, 0.f));
    }

    void InventoryWindow::onNameFilterChanged(MyGUI::EditBox* sender)
    {
        mSortModel->setNameFilter(sender->getCaption());
        mItemView->update();
    }

    void InventoryWindow::onFilterChanged(MyGUI::Widget* sender)
    {
        if (sender == mFilterAll)
            mSortModel->setCategory(SortFilterItemModel::Category_All);
        else if (sender == mFilterWeapon)
            mSortModel->setCategory(SortFilterItemModel::Category_Weapon);
        else if (sender == mFilterApparel)
            mSortModel->setCategory(SortFilterItemModel::Category_Apparel);
        else if (sender == mFilterMagic)
            mSortModel->setCategory(SortFilterItemModel::Category_Magic);
        else if (sender == mFilterMisc)
            mSortModel->setCategory(SortFilterItemModel::Category_Misc);
        mFilterAll->setStateSelected(false);
        mFilterWeapon->setStateSelected(false);
        mFilterApparel->setStateSelected(false);
        mFilterMagic->setStateSelected(false);
        mFilterMisc->setStateSelected(false);

        mItemView->update();

        sender->castType<MyGUI::Button>()->setStateSelected(true);
    }

    void InventoryWindow::onPinToggled()
    {
        Settings::windows().mInventoryPin.set(mPinned);

        MWBase::Environment::get().getWindowManager()->setWeaponVisibility(!mPinned);
    }

    void InventoryWindow::onTitleDoubleClicked()
    {
        if (Settings::gui().mControllerMenus && mGuiMode == GM_Inventory)
            return;
        else if (MyGUI::InputManager::getInstance().isShiftPressed())
            toggleMaximized();
        else if (!mPinned)
            MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Inventory);
    }

    void InventoryWindow::useItem(const MWWorld::Ptr& ptr, bool force)
    {
        const ESM::RefId& script = ptr.getClass().getScript(ptr);
        if (!script.empty())
        {
            // Don't try to equip the item if PCSkipEquip is set to 1
            if (ptr.getRefData().getLocals().getIntVar(script, "pcskipequip") == 1)
            {
                ptr.getRefData().getLocals().setVarByInt(script, "onpcequip", 1);
                return;
            }
            ptr.getRefData().getLocals().setVarByInt(script, "onpcequip", 0);
        }

        MWWorld::Ptr player = MWMechanics::getPlayer();
        auto type = ptr.getType();
        bool isWeaponOrArmor = type == ESM::Weapon::sRecordId || type == ESM::Armor::sRecordId;
        bool isBroken = ptr.getClass().hasItemHealth(ptr) && ptr.getCellRef().getCharge() == 0;
        const bool isFromDragAndDrop = mDragAndDrop->mIsOnDragAndDrop && mDragAndDrop->mItem.mBase == ptr;
        const auto [canEquipResult, canEquipMsg] = ptr.getClass().canBeEquipped(ptr, mPtr);

        // In vanilla, broken armor or weapons cannot be equipped
        // tools with 0 charges is equippable
        if (isBroken && isWeaponOrArmor)
        {
            if (isFromDragAndDrop)
                mDragAndDrop->drop(mTradeModel, mItemView);
            MWBase::Environment::get().getWindowManager()->messageBox(canEquipMsg);
            return;
        }

        const bool willEquip = canEquipResult != 0 || force;

        // If the item has a script, set OnPCEquip or PCSkipEquip to 1
        if (!script.empty() && willEquip)
        {
            // Ingredients, books and repair hammers must not have OnPCEquip set to 1 here
            bool isBook = type == ESM::Book::sRecordId;
            if (!isBook && type != ESM::Ingredient::sRecordId && type != ESM::Repair::sRecordId)
                ptr.getRefData().getLocals().setVarByInt(script, "onpcequip", 1);
            // Books must have PCSkipEquip set to 1 instead
            else if (isBook)
                ptr.getRefData().getLocals().setVarByInt(script, "pcskipequip", 1);
        }

        std::unique_ptr<MWWorld::Action> action = ptr.getClass().use(ptr, force);

        MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
        auto [eqSlots, canStack] = ptr.getClass().getEquipmentSlots(ptr);
        int useCount = isFromDragAndDrop ? static_cast<int>(mDragAndDrop->mDraggedCount) : ptr.getCellRef().getCount();

        if (!eqSlots.empty())
        {
            MWWorld::ContainerStoreIterator it = invStore.getSlot(eqSlots.front());
            if (it != invStore.end() && it->getCellRef().getRefId() == ptr.getCellRef().getRefId())
                useCount += it->getCellRef().getCount();
        }

        action->execute(player, !willEquip);

        // Partial equipping
        int excess = ptr.getCellRef().getCount() - useCount;
        if (excess > 0 && canStack)
            invStore.unequipItemQuantity(ptr, excess);

        if (isFromDragAndDrop)
        {
            // Feature: Don't finish draganddrop if potion or ingredient was used
            if (type == ESM::Potion::sRecordId || type == ESM::Ingredient::sRecordId)
                mDragAndDrop->update();
            else if (!willEquip)
                mDragAndDrop->drop(mTradeModel, mItemView);
            else
                mDragAndDrop->finish();
        }

        if (isVisible())
        {
            mItemView->update();
            notifyContentChanged();
        }
        // else: will be updated in open()
    }

    void InventoryWindow::onAvatarClicked(MyGUI::Widget* /*sender*/)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            MWWorld::Ptr ptr = mDragAndDrop->mItem.mBase;

            if (mDragAndDrop->mSourceModel != mTradeModel)
            {
                // Move item to the player's inventory
                ptr = mDragAndDrop->mSourceModel->moveItem(
                    mDragAndDrop->mItem, mDragAndDrop->mDraggedCount, mTradeModel);
            }

            MWBase::Environment::get().getLuaManager()->useItem(ptr, MWMechanics::getPlayer(), false);
        }
        else
        {
            MyGUI::IntPoint mousePos
                = MyGUI::InputManager::getInstance().getLastPressedPosition(MyGUI::MouseButton::Left);
            MyGUI::IntPoint relPos = mousePos - mAvatarImage->getAbsolutePosition();

            MWWorld::Ptr itemSelected = getAvatarSelectedItem(relPos.left, relPos.top);
            if (itemSelected.isEmpty())
                return;

            for (size_t i = 0; i < mTradeModel->getItemCount(); ++i)
            {
                if (mTradeModel->getItem(static_cast<ItemModel::ModelIndex>(i)).mBase == itemSelected)
                {
                    onItemSelectedFromSourceModel(static_cast<int>(i));
                    return;
                }
            }
            throw std::runtime_error("Can't find clicked item");
        }
    }

    MWWorld::Ptr InventoryWindow::getAvatarSelectedItem(int x, int y)
    {
        const osg::Vec2i viewportCoords = mapPreviewWindowToViewport(x, y);
        int slot = mPreview->getSlotSelected(viewportCoords.x(), viewportCoords.y());

        if (slot == -1)
            return MWWorld::Ptr();

        MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
        if (invStore.getSlot(slot) != invStore.end())
        {
            MWWorld::Ptr item = *invStore.getSlot(slot);
            if (!item.getClass().showsInInventory(item))
                return MWWorld::Ptr();
            return item;
        }

        return MWWorld::Ptr();
    }

    void InventoryWindow::updateEncumbranceBar()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();

        int capacity = static_cast<int>(player.getClass().getCapacity(player));
        float encumbrance = player.getClass().getEncumbrance(player);
        mTradeModel->adjustEncumbrance(encumbrance);
        mEncumbranceBar->setValue(static_cast<int>(std::ceil(encumbrance)), capacity);
    }

    void InventoryWindow::onFrame(float dt)
    {
        updateEncumbranceBar();

        bool contentUpdated = false;
        if (mUpdateNextFrame)
        {
            if (mTrading)
            {
                mTradeModel->updateBorrowed();
                MWBase::Environment::get().getWindowManager()->getTradeWindow()->mTradeModel->updateBorrowed();
                MWBase::Environment::get().getWindowManager()->getTradeWindow()->updateItemView();
                MWBase::Environment::get().getWindowManager()->getTradeWindow()->updateOffer();
            }

            mDragAndDrop->update();
            mItemView->update();
            notifyContentChanged();
            mUpdateNextFrame = false;
            contentUpdated = true;
        }

        mA11yFilterEdit.onFrame();

        a11yUpdateFollow(contentUpdated, dt);

        // Barter: rebuild the spoken list when the trade contents change (a sale,
        // a retracted purchase, a partial-stack offer). Selling borrows the item
        // to the merchant, which fires no onInventoryUpdate, and the count-dialog
        // confirm lands several frames later -- so poll a content signature here
        // rather than relying on contentUpdated or the equip-follow path. The -1
        // seed forces a rebuild on the first barter frame, after setTrading() has
        // run, fixing prices/labels that were built before mTrading was set.
        if (mTrading && A11y::PaneGroup::instance().contains(&mA11y))
        {
            const long long sig = a11yTradeSignature();
            if (sig != mA11yLastTradeSig)
            {
                // First build (seed -1) is silent; later changes are user-driven,
                // so announce the item the cursor lands on -- but only when this
                // pane is the active one (a sale here also mutates the merchant
                // pane, which must stay silent).
                const bool announce = mA11yLastTradeSig != -1 && mA11y.isActive();
                mA11yLastTradeSig = sig;
                const size_t cursor = mA11y.currentIndex();
                buildAccessibility();
                const size_t itemCount = mSortModel ? mSortModel->getItemCount() : 0;
                if (cursor != A11y::Screen::npos)
                {
                    if (cursor < mA11yItemBase)
                        mA11y.selectIndex(cursor, announce);
                    else if (itemCount > 0)
                        mA11y.selectIndex(mA11yItemBase + std::min(cursor - mA11yItemBase, itemCount - 1), announce);
                    else
                        mA11y.selectIndex(mA11yItemBase - 1, announce); // no items: category row
                }
            }
        }

        // When the user finishes editing the name filter, the matching set of
        // items has changed: rebuild the spoken list (the on-screen ItemView is
        // already kept current by onNameFilterChanged). Defer until edit mode
        // ends so we don't churn the list on every keystroke.
        const bool editing = mA11y.editing();
        if (mA11yWasEditing && !editing && mA11y.isActive())
            a11yRebuildKeepingCursor();
        mA11yWasEditing = editing;

        // Let the PaneGroup activate this pane if it's the one to land on (e.g.
        // returning from a book to the Inventory pane the user was reading from).
        if (A11y::PaneGroup::instance().contains(&mA11y))
            A11y::PaneGroup::instance().maybeActivateInitial(&mA11y);

        mA11y.onFrame(dt);
    }

    void InventoryWindow::setTrading(bool trading)
    {
        mTrading = trading;
    }

    void InventoryWindow::dirtyPreview()
    {
        mPreview->update();

        updateArmorRating();
    }

    void InventoryWindow::notifyContentChanged()
    {
        // update the spell window just in case new enchanted items were added to inventory
        MWBase::Environment::get().getWindowManager()->updateSpellWindow();

        MWBase::Environment::get().getMechanicsManager()->updateMagicEffects(MWMechanics::getPlayer());

        dirtyPreview();
    }

    void InventoryWindow::pickUpObject(MWWorld::Ptr object)
    {
        // If the inventory is not yet enabled, don't pick anything up
        if (!MWBase::Environment::get().getWindowManager()->isAllowed(GW_Inventory))
            return;
        // make sure the object is of a type that can be picked up
        auto type = object.getType();
        if ((type != ESM::Apparatus::sRecordId) && (type != ESM::Armor::sRecordId) && (type != ESM::Book::sRecordId)
            && (type != ESM::Clothing::sRecordId) && (type != ESM::Ingredient::sRecordId)
            && (type != ESM::Light::sRecordId) && (type != ESM::Miscellaneous::sRecordId)
            && (type != ESM::Lockpick::sRecordId) && (type != ESM::Probe::sRecordId) && (type != ESM::Repair::sRecordId)
            && (type != ESM::Weapon::sRecordId) && (type != ESM::Potion::sRecordId))
            return;

        // An object that can be picked up must have a tooltip.
        if (!object.getClass().hasToolTip(object))
            return;

        int count = object.getCellRef().getCount();
        if (object.getClass().isGold(object))
            count *= object.getClass().getValue(object);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWBase::Environment::get().getWorld()->breakInvisibility(player);

        if (!object.getRefData().activate())
            return;

        // Player must not be paralyzed, knocked down, or dead to pick up an item.
        const MWMechanics::NpcStats& playerStats = player.getClass().getNpcStats(player);
        if (playerStats.isParalyzed() || playerStats.getKnockedDown() || playerStats.isDead())
            return;

        MWBase::Environment::get().getMechanicsManager()->itemTaken(player, object, MWWorld::Ptr(), count);

        // add to player inventory
        // can't use ActionTake here because we need an MWWorld::Ptr to the newly inserted object
        MWWorld::Ptr newObject = *player.getClass().getContainerStore(player).add(object, count);

        // remove from world
        MWBase::Environment::get().getWorld()->deleteObject(object);

        // get ModelIndex to the item
        mTradeModel->update();
        size_t i = 0;
        for (; i < mTradeModel->getItemCount(); ++i)
        {
            if (mTradeModel->getItem(static_cast<ItemModel::ModelIndex>(i)).mBase == newObject)
                break;
        }
        if (i == mTradeModel->getItemCount())
            throw std::runtime_error("Added item not found");

        if (mDragAndDrop->mIsOnDragAndDrop)
            mDragAndDrop->finish();

        if (MyGUI::InputManager::getInstance().isAltPressed())
        {
            const MWWorld::Ptr item = mTradeModel->getItem(static_cast<ItemModel::ModelIndex>(i)).mBase;
            MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
            mItemView->update();
        }
        else
        {
            mDragAndDrop->startDrag(static_cast<int>(i), mSortModel, mTradeModel, mItemView, count);
        }

        MWBase::Environment::get().getWindowManager()->updateSpellWindow();
    }

    void InventoryWindow::cycle(bool next)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();

        if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(player))
            return;

        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        if (stats.isParalyzed() || stats.getKnockedDown() || stats.isDead() || stats.getHitRecovery())
            return;

        ItemModel::ModelIndex selected = -1;
        // not using mSortFilterModel as we only need sorting, not filtering
        SortFilterItemModel model(std::make_unique<InventoryItemModel>(player));
        model.setSortByType(false);
        model.update();
        if (model.getItemCount() == 0)
            return;

        for (ItemModel::ModelIndex i = 0; i < int(model.getItemCount()); ++i)
        {
            MWWorld::Ptr item = model.getItem(i).mBase;
            if (model.getItem(i).mType & ItemStack::Type_Equipped && isRightHandWeapon(item))
                selected = i;
        }

        int incr = next ? 1 : -1;
        bool found = false;
        ESM::RefId lastId;
        if (selected != -1)
            lastId = model.getItem(selected).mBase.getCellRef().getRefId();
        ItemModel::ModelIndex cycled = selected;
        for (size_t i = 0; i < model.getItemCount(); ++i)
        {
            cycled += incr;
            cycled = static_cast<ItemModel::ModelIndex>((cycled + model.getItemCount()) % model.getItemCount());

            MWWorld::Ptr item = model.getItem(cycled).mBase;

            // skip different stacks of the same item, or we will get stuck as stacking/unstacking them may change their
            // relative ordering
            if (lastId == item.getCellRef().getRefId())
                continue;

            lastId = item.getCellRef().getRefId();

            if (item.getClass().getType() == ESM::Weapon::sRecordId && isRightHandWeapon(item)
                && item.getClass().canBeEquipped(item, player).first)
            {
                found = true;
                break;
            }
        }

        if (!found || selected == cycled)
            return;

        MWWorld::Ptr selectedWeapon = model.getItem(cycled).mBase;
        useItem(selectedWeapon);

        // Announce the newly-selected weapon for screen-reader users: this
        // cycling happens with the menu closed, so there's no visible selection
        // feedback they can read. Interrupt so rapid cycling always reflects the
        // current selection.
        std::string_view name = selectedWeapon.getClass().getName(selectedWeapon);
        if (!name.empty())
            A11y::say(name, /*interrupt=*/true);
    }

    void InventoryWindow::rebuildAvatar()
    {
        mPreview->rebuild();
    }

    void InventoryWindow::onInventoryUpdate(const MWWorld::Ptr& ptr)
    {
        if (ptr == mPtr)
            mUpdateNextFrame = true;
    }

    MyGUI::IntSize InventoryWindow::getPreviewViewportSize() const
    {
        const MyGUI::IntSize previewWindowSize = mAvatarImage->getSize();
        const float scale = MWBase::Environment::get().getWindowManager()->getScalingFactor();

        return MyGUI::IntSize(std::min(mPreview->getTextureWidth(), static_cast<int>(previewWindowSize.width * scale)),
            std::min(mPreview->getTextureHeight(), static_cast<int>(previewWindowSize.height * scale)));
    }

    osg::Vec2i InventoryWindow::mapPreviewWindowToViewport(int x, int y) const
    {
        const MyGUI::IntSize previewWindowSize = mAvatarImage->getSize();
        const float normalisedX = x / std::max(1.f, static_cast<float>(previewWindowSize.width));
        const float normalisedY = y / std::max(1.f, static_cast<float>(previewWindowSize.height));

        const MyGUI::IntSize viewport = getPreviewViewportSize();
        return osg::Vec2i(static_cast<int>(normalisedX * (viewport.width - 1)),
            static_cast<int>((1 - normalisedY) * (viewport.height - 1)));
    }

    ControllerButtons* InventoryWindow::getControllerButtons()
    {
        switch (mGuiMode)
        {
            case MWGui::GM_Companion:
                mControllerButtons.mA = "#{OMWEngine:InventorySelect}";
                mControllerButtons.mB = "#{Interface:Close}";
                mControllerButtons.mX.clear();
                mControllerButtons.mR2 = "#{Interface:Share}";
                break;
            case MWGui::GM_Container:
                mControllerButtons.mA = "#{OMWEngine:InventorySelect}";
                mControllerButtons.mB = "#{Interface:Close}";
                mControllerButtons.mX = "#{Interface:TakeAll}";
                mControllerButtons.mR2 = "#{Interface:Container}";
                break;
            case MWGui::GM_Barter:
                mControllerButtons.mA = "#{Interface:Sell}";
                mControllerButtons.mB = "#{Interface:Cancel}";
                mControllerButtons.mX = "#{Interface:Offer}";
                mControllerButtons.mR2 = "#{Interface:Barter}";
                break;
            case MWGui::GM_Inventory:
            default:
                mControllerButtons.mA = "#{Interface:Equip}";
                mControllerButtons.mB = "#{Interface:Back}";
                mControllerButtons.mX = "#{Interface:Drop}";
                mControllerButtons.mR2.clear();
                break;
        }
        return &mControllerButtons;
    }

    bool InventoryWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        mPendingControllerAction = ControllerAction::None; // Clear any pending controller actions

        if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            if (mGuiMode == MWGui::GM_Inventory)
                mPendingControllerAction = ControllerAction::Use;
            else if (mGuiMode == MWGui::GM_Companion || mGuiMode == MWGui::GM_Container)
                mPendingControllerAction = ControllerAction::Transfer;
            else if (mGuiMode == MWGui::GM_Barter)
                mPendingControllerAction = ControllerAction::Sell;

            mItemView->onControllerButton(SDL_CONTROLLER_BUTTON_A);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_X)
        {
            if (mGuiMode == MWGui::GM_Inventory)
            {
                mPendingControllerAction = ControllerAction::Drop;
                mItemView->onControllerButton(SDL_CONTROLLER_BUTTON_A);
            }
            else if (mGuiMode == MWGui::GM_Container)
            {
                // Take all. Pass the button press to the container window and let it do the
                // logic of taking all.
                MWGui::ContainerWindow* containerWindow = static_cast<MWGui::ContainerWindow*>(
                    MWBase::Environment::get().getWindowManager()->getGuiModeWindows(mGuiMode).at(0));
                containerWindow->onControllerButtonEvent(arg);
            }
            else if (mGuiMode == MWGui::GM_Barter)
            {
                // Offer. Pass the button press to the barter window and let it do the logic
                // of making an offer.
                MWGui::TradeWindow* tradeWindow = static_cast<MWGui::TradeWindow*>(
                    MWBase::Environment::get().getWindowManager()->getGuiModeWindows(mGuiMode).at(1));
                tradeWindow->onControllerButtonEvent(arg);
            }
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        {
            if (mFilterAll->getStateSelected())
                onFilterChanged(mFilterMisc);
            else if (mFilterWeapon->getStateSelected())
                onFilterChanged(mFilterAll);
            else if (mFilterApparel->getStateSelected())
                onFilterChanged(mFilterWeapon);
            else if (mFilterMagic->getStateSelected())
                onFilterChanged(mFilterApparel);
            else if (mFilterMisc->getStateSelected())
                onFilterChanged(mFilterMagic);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        {
            if (mFilterAll->getStateSelected())
                onFilterChanged(mFilterWeapon);
            else if (mFilterWeapon->getStateSelected())
                onFilterChanged(mFilterApparel);
            else if (mFilterApparel->getStateSelected())
                onFilterChanged(mFilterMagic);
            else if (mFilterMagic->getStateSelected())
                onFilterChanged(mFilterMisc);
            else if (mFilterMisc->getStateSelected())
                onFilterChanged(mFilterAll);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else
        {
            mItemView->onControllerButton(arg.button);
        }

        return true;
    }

    void InventoryWindow::setActiveControllerWindow(bool active)
    {
        if (!Settings::gui().mControllerMenus)
            return;

        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        if (winMgr->getMode() == MWGui::GM_Inventory)
        {
            // Fill the screen, or limit to a certain size on large screens. Size chosen to
            // match the size of the stats window.
            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            int width = std::min(viewSize.width, 1600);
            int height = std::min(winMgr->getControllerMenuHeight(), StatsWindow::getIdealHeight());
            int x = (viewSize.width - width) / 2;
            int y = (viewSize.height - height) / 2;

            MyGUI::Window* window = mMainWidget->castType<MyGUI::Window>();
            window->setCoord(x, active ? y : viewSize.height + 1, width, height);

            adjustPanes();
            updatePreviewSize();
        }

        // Show L1 and R1 buttons next to tabs
        MyGUI::Widget* image;
        getWidget(image, "BtnL1Image");
        image->setVisible(active);

        getWidget(image, "BtnR1Image");
        image->setVisible(active);

        mItemView->setActiveControllerWindow(active);
        WindowBase::setActiveControllerWindow(active);
    }
}
