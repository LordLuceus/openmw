#include "tradewindow.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_ControllerManager.h>
#include <MyGUI_ControllerRepeatClick.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>

#include <components/misc/rng.hpp>
#include <components/misc/strings/format.hpp>
#include <components/widgets/numericeditbox.hpp>

#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"

#include "accessibility/itemtext.hpp"
#include "accessibility/panegroup.hpp"
#include "accessibility/speech.hpp"
#include "containeritemmodel.hpp"
#include "countdialog.hpp"
#include "inventorywindow.hpp"
#include "itemview.hpp"
#include "sortfilteritemmodel.hpp"
#include "tooltips.hpp"
#include "tradeitemmodel.hpp"

namespace
{

    int getEffectiveValue(MWWorld::Ptr item, int count)
    {
        float price = static_cast<float>(item.getClass().getValue(item));
        if (item.getClass().hasItemHealth(item))
        {
            price *= item.getClass().getItemNormalizedHealth(item);
        }
        return static_cast<int>(price * count);
    }

    bool haggle(const MWWorld::Ptr& player, const MWWorld::Ptr& merchant, int playerOffer, int merchantOffer)
    {
        // accept if merchant offer is better than player offer
        if (playerOffer <= merchantOffer)
        {
            return true;
        }

        // reject if npc is a creature
        if (merchant.getType() != ESM::NPC::sRecordId)
        {
            return false;
        }

        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

        // Is the player buying?
        bool buying = (merchantOffer < 0);
        int a = std::abs(merchantOffer);
        int b = std::abs(playerOffer);
        int d = (buying) ? int(100 * (a - b) / a) : int(100 * (b - a) / b);

        int clampedDisposition = MWBase::Environment::get().getMechanicsManager()->getDerivedDisposition(merchant);

        const MWMechanics::CreatureStats& merchantStats = merchant.getClass().getCreatureStats(merchant);
        const MWMechanics::CreatureStats& playerStats = player.getClass().getCreatureStats(player);

        float a1 = static_cast<float>(player.getClass().getSkill(player, ESM::Skill::Mercantile));
        float b1 = 0.1f * playerStats.getAttribute(ESM::Attribute::Luck).getModified();
        float c1 = 0.2f * playerStats.getAttribute(ESM::Attribute::Personality).getModified();
        float d1 = static_cast<float>(merchant.getClass().getSkill(merchant, ESM::Skill::Mercantile));
        float e1 = 0.1f * merchantStats.getAttribute(ESM::Attribute::Luck).getModified();
        float f1 = 0.2f * merchantStats.getAttribute(ESM::Attribute::Personality).getModified();

        float dispositionTerm = gmst.find("fDispositionMod")->mValue.getFloat() * (clampedDisposition - 50);
        float pcTerm = (dispositionTerm + a1 + b1 + c1) * playerStats.getFatigueTerm();
        float npcTerm = (d1 + e1 + f1) * merchantStats.getFatigueTerm();
        float x = gmst.find("fBargainOfferMulti")->mValue.getFloat() * d
            + gmst.find("fBargainOfferBase")->mValue.getFloat() + int(pcTerm - npcTerm);

        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        int roll = Misc::Rng::rollDice(100, prng) + 1;

        // reject if roll fails
        // (or if player tries to buy things and get money)
        if (roll > x || (merchantOffer < 0 && 0 < playerOffer))
        {
            return false;
        }

        // apply skill gain on successful barter
        float skillGain = 0.f;
        int finalPrice = std::abs(playerOffer);
        int initialMerchantOffer = std::abs(merchantOffer);

        if (!buying && (finalPrice > initialMerchantOffer))
        {
            skillGain = std::floor(100.f * (finalPrice - initialMerchantOffer) / finalPrice);
        }
        else if (buying && (finalPrice < initialMerchantOffer))
        {
            skillGain = std::floor(100.f * (initialMerchantOffer - finalPrice) / initialMerchantOffer);
        }
        player.getClass().skillUsageSucceeded(
            player, ESM::Skill::Mercantile, ESM::Skill::Mercantile_Success, skillGain);

        return true;
    }
}

namespace MWGui
{
    TradeWindow::TradeWindow()
        : WindowBase("openmw_trade_window.layout")
        , mSortModel(nullptr)
        , mTradeModel(nullptr)
        , mItemToSell(-1)
        , mCurrentBalance(0)
        , mCurrentMerchantOffer(0)
        , mUpdateNextFrame(false)
    {
        getWidget(mFilterAll, "AllButton");
        getWidget(mFilterWeapon, "WeaponButton");
        getWidget(mFilterApparel, "ApparelButton");
        getWidget(mFilterMagic, "MagicButton");
        getWidget(mFilterMisc, "MiscButton");

        getWidget(mMaxSaleButton, "MaxSaleButton");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mOfferButton, "OfferButton");
        getWidget(mPlayerGold, "PlayerGold");
        getWidget(mMerchantGold, "MerchantGold");
        getWidget(mIncreaseButton, "IncreaseButton");
        getWidget(mDecreaseButton, "DecreaseButton");
        getWidget(mTotalBalance, "TotalBalance");
        getWidget(mTotalBalanceLabel, "TotalBalanceLabel");
        getWidget(mBottomPane, "BottomPane");
        getWidget(mFilterEdit, "FilterEdit");

        getWidget(mItemView, "ItemView");
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &TradeWindow::onItemSelected);

        // Screen reader: the merchant's items are model-backed (drawn by the
        // ItemView, not per-item widgets), so navigate them virtually by index
        // like the container window. An invisible anchor holds key focus; the
        // option list is rebuilt by a11yBuild().
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);
        mA11y.setExtraKeyHandler([this](MyGUI::KeyCode key) { return a11yHandleBalanceKey(key); });

        mFilterAll->setStateSelected(true);

        mFilterAll->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterWeapon->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterApparel->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterMagic->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterMisc->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &TradeWindow::onNameFilterChanged);

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onCancelButtonClicked);
        mOfferButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onOfferButtonClicked);
        mMaxSaleButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onMaxSaleButtonClicked);
        mIncreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &TradeWindow::onIncreaseButtonPressed);
        mIncreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &TradeWindow::onBalanceButtonReleased);
        mDecreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &TradeWindow::onDecreaseButtonPressed);
        mDecreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &TradeWindow::onBalanceButtonReleased);

        mTotalBalance->eventValueChanged += MyGUI::newDelegate(this, &TradeWindow::onBalanceValueChanged);
        mTotalBalance->eventEditSelectAccept += MyGUI::newDelegate(this, &TradeWindow::onAccept);
        mTotalBalance->setMinValue(
            std::numeric_limits<int>::min() + 1); // disallow INT_MIN since abs(INT_MIN) is undefined

        setCoord(400, 0, 400, 300);

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

            mControllerButtons.mA = "#{Interface:Buy}";
            mControllerButtons.mB = "#{Interface:Cancel}";
            mControllerButtons.mX = "#{Interface:Offer}";
            mControllerButtons.mR3 = "#{Interface:Info}";
            mControllerButtons.mL2 = "#{Interface:Inventory}";
        }
    }

    void TradeWindow::setPtr(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty() || !actor.getClass().isActor())
            throw std::runtime_error("Invalid argument in TradeWindow::setPtr");
        mPtr = actor;

        mCurrentBalance = 0;
        mCurrentMerchantOffer = 0;

        std::vector<MWWorld::Ptr> itemSources;
        // Important: actor goes first, so purchased items come out of the actor's pocket first
        itemSources.push_back(actor);
        MWBase::Environment::get().getWorld()->getContainersOwnedBy(actor, itemSources);

        std::vector<MWWorld::Ptr> worldItems;
        MWBase::Environment::get().getWorld()->getItemsOwnedBy(actor, worldItems);

        auto tradeModel
            = std::make_unique<TradeItemModel>(std::make_unique<ContainerItemModel>(itemSources, worldItems), mPtr);
        mTradeModel = tradeModel.get();
        auto sortModel = std::make_unique<SortFilterItemModel>(std::move(tradeModel));
        mSortModel = sortModel.get();
        mItemView->setModel(std::move(sortModel));
        mItemView->resetScrollBars();

        updateLabels();

        setTitle(actor.getClass().getName(actor));

        onFilterChanged(mFilterAll);
        mFilterEdit->setCaption({});

        // Cycle to the buy window if it's not active.
        if (Settings::gui().mControllerMenus && !mActiveControllerWindow)
            MWBase::Environment::get().getWindowManager()->cycleActiveControllerWindow(true);

        // Screen reader: barter shows the merchant's goods here next to the
        // player's inventory window. Enrol both as panes (merchant = 0, the
        // inventory enrols itself as 1) so Tab switches between buying and
        // selling. Build our option list now; the PaneGroup decides which pane
        // claims focus first (the lowest order, i.e. the merchant).
        mA11yLastSig = -1; // force a rebuild on the first barter frame
        a11yBuild();
        // Label the merchant pane with the trader's name (the window title), so
        // Tabbing to it announces e.g. "Arrille's goods" vs "Inventory".
        A11y::PaneGroup::instance().enrol(&mA11y, std::string(actor.getClass().getName(actor)), 0);
    }

    void TradeWindow::onFrame(float dt)
    {
        checkReferenceAvailable();

        if (isVisible() && mUpdateNextFrame)
        {
            mTradeModel->updateBorrowed();
            MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel()->updateBorrowed();
            MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
            mItemView->update();
            updateOffer();
            mUpdateNextFrame = false;
        }

        // Screen reader: rebuild the spoken list whenever the merchant's items
        // change -- an item bought (moves to the player's pane as on-offer), a
        // borrowed item returned, or a partial-stack offer. Driven off a content
        // signature rather than mUpdateNextFrame because a buy updates the model
        // synchronously without firing onInventoryUpdate, and the initial -1
        // forces a rebuild on the first barter frame (after which the labels are
        // valid). Preserve the cursor position silently.
        if (A11y::PaneGroup::instance().contains(&mA11y))
        {
            const long long sig = a11yTradeSignature();
            if (sig != mA11yLastSig)
            {
                // The very first build (seed -1) sets up labels silently; later
                // changes are user-driven, so announce the item the cursor lands
                // on -- but only when this pane is the active one (the other
                // pane shouldn't speak when the user acts here).
                const bool announce = mA11yLastSig != -1 && mA11y.isActive();
                mA11yLastSig = sig;
                const size_t cursor = mA11y.currentIndex();
                a11yBuild();
                const size_t count = mSortModel ? mSortModel->getItemCount() : 0;
                if (cursor != A11y::Screen::npos && count > 0)
                    mA11y.selectIndex(std::min(cursor, count - 1), announce);
            }

            // Let the PaneGroup claim focus for the pane the user should land on.
            A11y::PaneGroup::instance().maybeActivateInitial(&mA11y);
        }

        mA11y.onFrame(dt);
    }

    long long TradeWindow::a11yTradeSignature() const
    {
        // Fold the merchant list's size and each stack's count + barter state
        // into a cheap rolling hash. Any borrow/return/partial-offer changes it.
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

    void TradeWindow::onNameFilterChanged(MyGUI::EditBox* sender)
    {
        mSortModel->setNameFilter(sender->getCaption());
        mItemView->update();
    }

    void TradeWindow::onFilterChanged(MyGUI::Widget* sender)
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

        sender->castType<MyGUI::Button>()->setStateSelected(true);

        mItemView->update();
    }

    int TradeWindow::getMerchantServices()
    {
        return mPtr.getClass().getServices(mPtr);
    }

    bool TradeWindow::exit()
    {
        mTradeModel->abort();
        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel()->abort();
        return true;
    }

    void TradeWindow::onItemSelected(int index)
    {
        const ItemStack& item = mSortModel->getItem(index);

        MWWorld::Ptr object = item.mBase;
        size_t count = item.mCount;
        bool shift = MyGUI::InputManager::getInstance().isShiftPressed();
        if (MyGUI::InputManager::getInstance().isControlPressed())
            count = 1;

        if (count > 1 && !shift)
        {
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            std::string message = "#{sQuanityMenuMessage02}";
            std::string name{ object.getClass().getName(object) };
            name += MWGui::ToolTips::getSoulString(object.getCellRef());
            dialog->openCountDialog(name, message, static_cast<int>(count));
            dialog->eventOkClicked.clear();
            dialog->eventOkClicked += MyGUI::newDelegate(this, &TradeWindow::sellItem);
            mItemToSell = mSortModel->mapToSource(index);
        }
        else
        {
            mItemToSell = mSortModel->mapToSource(index);
            sellItem(nullptr, count);
        }
    }

    void TradeWindow::sellItem(MyGUI::Widget* /*sender*/, std::size_t count)
    {
        const ItemStack& item = mTradeModel->getItem(mItemToSell);
        const ESM::RefId& sound = item.mBase.getClass().getUpSoundId(item.mBase);
        MWBase::Environment::get().getWindowManager()->playSound(sound);

        TradeItemModel* playerTradeModel
            = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();

        if (item.mType == ItemStack::Type_Barter)
        {
            // this was an item borrowed to us by the player
            mTradeModel->returnItemBorrowedToUs(mItemToSell, count);
            playerTradeModel->returnItemBorrowedFromUs(mItemToSell, mTradeModel, count);
            updateOffer();
        }
        else
        {
            // borrow item to player
            playerTradeModel->borrowItemToUs(mItemToSell, mTradeModel, count);
            mTradeModel->borrowItemFromUs(mItemToSell, count);
            updateOffer();
        }

        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
        mItemView->update();
    }

    void TradeWindow::borrowItem(int index, size_t count)
    {
        TradeItemModel* playerTradeModel
            = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();
        mTradeModel->borrowItemToUs(index, playerTradeModel, count);
        mItemView->update();
        updateOffer();
    }

    void TradeWindow::returnItem(int index, size_t count)
    {
        TradeItemModel* playerTradeModel
            = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();
        mTradeModel->returnItemBorrowedFromUs(index, playerTradeModel, count);
        mItemView->update();
        updateOffer();
    }

    void TradeWindow::addOrRemoveGold(int amount, const MWWorld::Ptr& actor)
    {
        MWWorld::ContainerStore& store = actor.getClass().getContainerStore(actor);

        if (amount > 0)
        {
            store.add(MWWorld::ContainerStore::sGoldId, amount);
        }
        else
        {
            store.remove(MWWorld::ContainerStore::sGoldId, -amount);
        }
    }

    void TradeWindow::onOfferSubmitted(MyGUI::Widget* /*sender*/, size_t offerAmount)
    {
        mCurrentBalance = static_cast<int>(offerAmount) * (mCurrentBalance < 0 ? -1 : 1);
        updateLabels();
        onOfferButtonClicked(mOfferButton);
    }

    void TradeWindow::onOfferButtonClicked(MyGUI::Widget* /*sender*/)
    {
        TradeItemModel* playerItemModel
            = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();

        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

        if (mTotalBalance->getValue() == 0)
            mCurrentBalance = 0;

        // were there any items traded at all?
        const std::vector<ItemStack>& playerBought = playerItemModel->getItemsBorrowedToUs();
        const std::vector<ItemStack>& merchantBought = mTradeModel->getItemsBorrowedToUs();
        if (playerBought.empty() && merchantBought.empty())
        {
            // user notification
            MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog11}");
            return;
        }

        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);

        // check if the player can afford this
        if (mCurrentBalance < 0 && playerGold < std::abs(mCurrentBalance))
        {
            // user notification
            MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog1}");
            return;
        }

        // check if the merchant can afford this
        if (mCurrentBalance > 0 && getMerchantGold() < mCurrentBalance)
        {
            // user notification
            MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog2}");
            return;
        }

        // check if the player is attempting to sell back an item stolen from this actor
        for (const ItemStack& itemStack : merchantBought)
        {
            if (MWBase::Environment::get().getMechanicsManager()->isItemStolenFrom(
                    itemStack.mBase.getCellRef().getRefId(), mPtr))
            {
                std::string msg = gmst.find("sNotifyMessage49")->mValue.getString();
                msg = Misc::StringUtils::format(msg, itemStack.mBase.getClass().getName(itemStack.mBase));
                MWBase::Environment::get().getWindowManager()->messageBox(msg);

                MWBase::Environment::get().getMechanicsManager()->confiscateStolenItemToOwner(
                    player, itemStack.mBase, mPtr, static_cast<int>(itemStack.mCount));

                onCancelButtonClicked(mCancelButton);
                MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
                return;
            }
        }

        bool offerAccepted = haggle(player, mPtr, mCurrentBalance, mCurrentMerchantOffer);

        // apply disposition change if merchant is NPC
        if (mPtr.getClass().isNpc())
        {
            int dispositionDelta = offerAccepted ? gmst.find("iBarterSuccessDisposition")->mValue.getInteger()
                                                 : gmst.find("iBarterFailDisposition")->mValue.getInteger();

            MWBase::Environment::get().getDialogueManager()->applyBarterDispositionChange(dispositionDelta);
        }

        // display message on haggle failure
        if (!offerAccepted)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage9}");
            return;
        }

        // make the item transfer
        mTradeModel->transferItems();
        playerItemModel->transferItems();

        // transfer the gold
        if (mCurrentBalance != 0)
        {
            addOrRemoveGold(mCurrentBalance, player);
            mPtr.getClass().getCreatureStats(mPtr).setGoldPool(
                mPtr.getClass().getCreatureStats(mPtr).getGoldPool() - mCurrentBalance);
        }

        eventTradeDone();

        MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Item Gold Up"));
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
    }

    void TradeWindow::onAccept(MyGUI::EditBox* sender)
    {
        onOfferButtonClicked(sender);

        // To do not spam onAccept() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void TradeWindow::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        exit();
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
    }

    void TradeWindow::onMaxSaleButtonClicked(MyGUI::Widget* /*sender*/)
    {
        mCurrentBalance = getMerchantGold();
        updateLabels();
    }

    void TradeWindow::addRepeatController(MyGUI::Widget* widget)
    {
        MyGUI::ControllerItem* item
            = MyGUI::ControllerManager::getInstance().createItem(MyGUI::ControllerRepeatClick::getClassTypeName());
        MyGUI::ControllerRepeatClick* controller = static_cast<MyGUI::ControllerRepeatClick*>(item);
        controller->eventRepeatClick += newDelegate(this, &TradeWindow::onRepeatClick);
        MyGUI::ControllerManager::getInstance().addItem(widget, controller);
    }

    void TradeWindow::onIncreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        addRepeatController(sender);
        onIncreaseButtonTriggered();
    }

    void TradeWindow::onDecreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        addRepeatController(sender);
        onDecreaseButtonTriggered();
    }

    void TradeWindow::onRepeatClick(MyGUI::Widget* widget, MyGUI::ControllerItem* controller)
    {
        if (widget == mIncreaseButton)
            onIncreaseButtonTriggered();
        else if (widget == mDecreaseButton)
            onDecreaseButtonTriggered();
    }

    void TradeWindow::onBalanceButtonReleased(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        MyGUI::ControllerManager::getInstance().removeItem(sender);
    }

    void TradeWindow::onBalanceValueChanged(int value)
    {
        int previousBalance = mCurrentBalance;

        // Entering a "-" sign inverts the buying/selling state
        mCurrentBalance = (mCurrentBalance >= 0 ? 1 : -1) * value;
        updateLabels();

        if (mCurrentBalance == 0)
            mCurrentBalance = previousBalance;

        if (value != std::abs(value))
            mTotalBalance->setValue(std::abs(value));
    }

    void TradeWindow::onIncreaseButtonTriggered()
    {
        // prevent overflows, and prevent entering INT_MIN since abs(INT_MIN) is undefined
        if (mCurrentBalance == std::numeric_limits<int>::max()
            || mCurrentBalance == std::numeric_limits<int>::min() + 1)
            return;
        if (mTotalBalance->getValue() == 0)
            mCurrentBalance = 0;
        if (mCurrentBalance < 0)
            mCurrentBalance -= 1;
        else
            mCurrentBalance += 1;
        updateLabels();
    }

    void TradeWindow::onDecreaseButtonTriggered()
    {
        if (mTotalBalance->getValue() == 0)
            mCurrentBalance = 0;
        if (mCurrentBalance < 0)
            mCurrentBalance += 1;
        else
            mCurrentBalance -= 1;
        updateLabels();
    }

    void TradeWindow::updateLabels()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);
        mPlayerGold->setCaptionWithReplacing("#{sYourGold} " + MyGUI::utility::toString(playerGold));

        TradeItemModel* playerTradeModel
            = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();
        const std::vector<ItemStack>& playerBorrowed = playerTradeModel->getItemsBorrowedToUs();
        const std::vector<ItemStack>& merchantBorrowed = mTradeModel->getItemsBorrowedToUs();

        if (playerBorrowed.empty() && merchantBorrowed.empty())
        {
            mCurrentBalance = 0;
        }

        if (mCurrentBalance < 0)
        {
            mTotalBalanceLabel->setCaptionWithReplacing("#{sTotalCost}");
        }
        else
        {
            mTotalBalanceLabel->setCaptionWithReplacing("#{sTotalSold}");
        }

        mTotalBalance->setValue(std::abs(mCurrentBalance));

        mMerchantGold->setCaptionWithReplacing("#{sSellerGold} " + MyGUI::utility::toString(getMerchantGold()));
    }

    void TradeWindow::updateOffer()
    {
        TradeItemModel* playerTradeModel
            = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();

        int merchantOffer = 0;

        // The offered price must be capped at 75% of the base price to avoid exploits
        // connected to buying and selling the same item.
        // This value has been determined by researching the limitations of the vanilla formula
        // and may not be sufficient if getBarterOffer behavior has been changed.
        const std::vector<ItemStack>& playerBorrowed = playerTradeModel->getItemsBorrowedToUs();
        for (const ItemStack& itemStack : playerBorrowed)
        {
            const int basePrice = getEffectiveValue(itemStack.mBase, static_cast<int>(itemStack.mCount));
            const int cap
                = static_cast<int>(std::max(1.f, 0.75f * basePrice)); // Minimum buying price -- 75% of the base
            const int buyingPrice
                = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, basePrice, true);
            merchantOffer -= std::max(cap, buyingPrice);
        }

        const std::vector<ItemStack>& merchantBorrowed = mTradeModel->getItemsBorrowedToUs();
        for (const ItemStack& itemStack : merchantBorrowed)
        {
            const int basePrice = getEffectiveValue(itemStack.mBase, static_cast<int>(itemStack.mCount));
            const int cap
                = static_cast<int>(std::max(1.f, 0.75f * basePrice)); // Maximum selling price -- 75% of the base
            const int sellingPrice
                = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, basePrice, false);
            merchantOffer += mPtr.getClass().isNpc() ? std::min(cap, sellingPrice) : sellingPrice;
        }

        int diff = merchantOffer - mCurrentMerchantOffer;
        mCurrentMerchantOffer = merchantOffer;
        mCurrentBalance += diff;
        updateLabels();
    }

    void TradeWindow::onReferenceUnavailable()
    {
        // remove both Trade and Dialogue (since you always trade with the NPC/creature that you have previously talked
        // to)
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
        MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
    }

    int TradeWindow::getMerchantGold()
    {
        int merchantGold = mPtr.getClass().getCreatureStats(mPtr).getGoldPool();
        return merchantGold;
    }

    void TradeWindow::resetReference()
    {
        ReferenceInterface::resetReference();
        mItemView->setModel(nullptr);
        mTradeModel = nullptr;
        mSortModel = nullptr;
    }

    void TradeWindow::onClose()
    {
        // Screen reader: leave the pane group and release key focus. Done even
        // when only temporarily hidden (a sub-mode like a count dialog) -- it
        // re-enrols on the next setPtr / onFrame.
        A11y::PaneGroup::instance().withdraw(&mA11y);
        mA11y.deactivate();

        // Make sure the window was actually closed and not temporarily hidden.
        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Barter))
            return;
        A11y::PaneGroup::instance().resetMemory();
        resetReference();
    }

    void TradeWindow::onDeleteCustomData(const MWWorld::Ptr& ptr)
    {
        if (mTradeModel && mTradeModel->usesContainer(ptr))
            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
    }

    bool TradeWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            int index = mItemView->getControllerFocus();
            if (index >= 0 && index < mItemView->getItemCount())
                onItemSelected(index);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            onCancelButtonClicked(mCancelButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_X)
        {
            if (mCurrentBalance == 0)
                return true;
            // Show a count dialog to allow for bartering.
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            if (mCurrentBalance < 0)
            {
                // Buying from the merchant
                dialog->openCountDialog("#{sTotalcost}:", "#{sOffer}", -mCurrentMerchantOffer);
                dialog->setCount(-mCurrentBalance);
            }
            else
            {
                // Selling to the merchant
                dialog->openCountDialog("#{sTotalsold}:", "#{sOffer}", getMerchantGold());
                dialog->setCount(mCurrentBalance);
            }
            dialog->eventOkClicked.clear();
            dialog->eventOkClicked += MyGUI::newDelegate(this, &TradeWindow::onOfferSubmitted);
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
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK || arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP
            || arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN || arg.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT
            || arg.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
        {
            mItemView->onControllerButton(arg.button);
        }

        return true;
    }

    void TradeWindow::setActiveControllerWindow(bool active)
    {
        // Show L1 and R1 buttons next to tabs
        MyGUI::Widget* image;
        getWidget(image, "BtnL1Image");
        image->setVisible(active);

        getWidget(image, "BtnR1Image");
        image->setVisible(active);

        mItemView->setActiveControllerWindow(active);
        WindowBase::setActiveControllerWindow(active);
    }

    void TradeWindow::updateItemView()
    {
        mItemView->update();
    }

    void TradeWindow::onInventoryUpdate(const MWWorld::Ptr& ptr)
    {
        if (mTradeModel && mTradeModel->usesContainer(ptr))
            mUpdateNextFrame = true;
    }

    // --- Screen-reader accessibility -------------------------------------

    std::string TradeWindow::a11yItemLabel(const ItemStack& item)
    {
        std::string label = std::string(item.mBase.getClass().getName(item.mBase));
        if (item.mCount > 1)
            label += " (" + std::to_string(item.mCount) + ")";

        // Most rows are the merchant's goods, priced at what the player would PAY
        // to buy them. But a Type_Barter row here is one of the player's own
        // items, lent to the merchant as a pending sale -- so it carries the
        // SELLING price instead, matching its contribution to the balance.
        const bool onOffer = item.mType == ItemStack::Type_Barter;
        const int basePrice = getEffectiveValue(item.mBase, static_cast<int>(item.mCount));
        const int cap = static_cast<int>(std::max(1.f, 0.75f * basePrice));
        int price;
        if (onOffer)
        {
            const int sellingPrice
                = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, basePrice, false);
            price = mPtr.getClass().isNpc() ? std::min(cap, sellingPrice) : sellingPrice;
        }
        else
        {
            const int buyingPrice
                = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, basePrice, true);
            price = std::max(cap, buyingPrice);
        }

        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        label += ", " + std::string(winMgr->getGameSettingString("sValue", "Value")) + " " + std::to_string(price);

        // Items the player has put up for sale are lent to the merchant and show
        // here marked Type_Barter. Enter on such a row retracts the offer
        // (returns the item), so flag the state for the user. No vanilla GMST
        // means "on offer", so use a bare literal like the other a11y labels.
        if (onOffer)
            label += ", on offer";
        return label;
    }

    void TradeWindow::a11yBuild()
    {
        mA11y.clear();
        if (!mSortModel)
            return;

        for (size_t i = 0; i < mSortModel->getItemCount(); ++i)
        {
            const int index = static_cast<int>(i);
            const ItemStack item = mSortModel->getItem(index);
            mA11y.add({ .widget = nullptr,
                .label = a11yItemLabel(item),
                .tooltips = [base = item.mBase, count = item.mCount]
                { return A11y::itemTooltipLines(base, static_cast<int>(count)); },
                .activate = [this, index] { a11yBuyItem(index); } });
        }
    }

    void TradeWindow::a11yBuyItem(int sortIndex)
    {
        if (!mSortModel || sortIndex < 0 || sortIndex >= static_cast<int>(mSortModel->getItemCount()))
            return;

        // onItemSelected already does the right thing for a click: it borrows
        // the item to the player (or opens a count picker for a stack), updating
        // the offer. Reuse it so buying stays consistent with the mouse path.
        onItemSelected(sortIndex);
    }

    void TradeWindow::a11yAnnounceBalance(bool interrupt)
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        const int balance = std::abs(mCurrentBalance);
        // mCurrentBalance < 0 means the player pays (total cost); >= 0 means the
        // player receives (total sold).
        const std::string label = mCurrentBalance < 0
            ? std::string(winMgr->getGameSettingString("sTotalCost", "Total cost"))
            : std::string(winMgr->getGameSettingString("sTotalSold", "Total sold"));
        A11y::say(label + ": " + std::to_string(balance), interrupt);
    }

    void TradeWindow::a11yAdjustBalance(int delta)
    {
        // Reuse the increase/decrease button logic so clamping/sign handling
        // stays identical to the mouse path, then announce the new balance.
        if (delta > 0)
            for (int i = 0; i < delta; ++i)
                onIncreaseButtonTriggered();
        else
            for (int i = 0; i < -delta; ++i)
                onDecreaseButtonTriggered();
        a11yAnnounceBalance(/*interrupt=*/true);
    }

    void TradeWindow::a11yOfferCountDialog()
    {
        // Mirror the controller X-button path: a count dialog to type an exact
        // offer amount. No-op when nothing has been added to the trade.
        if (mCurrentBalance == 0)
        {
            a11yAnnounceBalance(/*interrupt=*/true);
            return;
        }

        CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
        if (mCurrentBalance < 0)
        {
            dialog->openCountDialog("#{sTotalcost}:", "#{sOffer}", -mCurrentMerchantOffer);
            dialog->setCount(-mCurrentBalance);
        }
        else
        {
            dialog->openCountDialog("#{sTotalsold}:", "#{sOffer}", getMerchantGold());
            dialog->setCount(mCurrentBalance);
        }
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &TradeWindow::onOfferSubmitted);
    }

    void TradeWindow::a11ySubmitOffer()
    {
        onOfferButtonClicked(mOfferButton);
    }

    bool TradeWindow::a11yHandleBalanceKey(MyGUI::KeyCode key)
    {
        switch (key.getValue())
        {
            case MyGUI::KeyCode::B:
                a11yAnnounceBalance(/*interrupt=*/true);
                return true;
            case MyGUI::KeyCode::Equals:
            case MyGUI::KeyCode::Add:
                a11yAdjustBalance(MyGUI::InputManager::getInstance().isShiftPressed() ? 100 : 1);
                return true;
            case MyGUI::KeyCode::Minus:
            case MyGUI::KeyCode::Subtract:
                a11yAdjustBalance(MyGUI::InputManager::getInstance().isShiftPressed() ? -100 : -1);
                return true;
            case MyGUI::KeyCode::C:
                a11yOfferCountDialog();
                return true;
            case MyGUI::KeyCode::O:
                a11ySubmitOffer();
                return true;
            default:
                return false;
        }
    }
}
