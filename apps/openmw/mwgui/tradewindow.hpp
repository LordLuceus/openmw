#ifndef MWGUI_TRADEWINDOW_H
#define MWGUI_TRADEWINDOW_H

#include "referenceinterface.hpp"
#include "windowbase.hpp"

#include "accessibility/screen.hpp"

namespace Gui
{
    class NumericEditBox;
}

namespace MyGUI
{
    class ControllerItem;
}

namespace MWGui
{
    class ItemView;
    class SortFilterItemModel;
    class TradeItemModel;
    struct ItemStack;

    class TradeWindow : public WindowBase, public ReferenceInterface
    {
    public:
        TradeWindow();

        void setPtr(const MWWorld::Ptr& actor) override;

        void onClose() override;
        void onFrame(float dt) override;
        void clear() override { resetReference(); }

        bool exit() override;

        void resetReference() override;

        void onDeleteCustomData(const MWWorld::Ptr& ptr) override;

        void updateItemView();

        void onInventoryUpdate(const MWWorld::Ptr& ptr) override;

        typedef MyGUI::delegates::MultiDelegate<> EventHandle_TradeDone;
        EventHandle_TradeDone eventTradeDone;

        std::string_view getWindowIdForLua() const override { return "Trade"; }

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

        // --- Screen-reader accessibility ---------------------------------
        // Announce/adjust the running barter balance and submit the offer.
        // These are shared by both barter panes (merchant + player inventory),
        // so the inventory window forwards its balance keys here.
        void a11yAnnounceBalance(bool interrupt);
        void a11yAdjustBalance(int delta);
        // Announce the player's gold (G) or the merchant's gold pool (Shift+G).
        void a11yAnnounceGold(bool merchant);
        void a11yOfferCountDialog();
        void a11ySubmitOffer();
        MWGui::A11y::Screen& a11yScreen() { return mA11y; }

    private:
        friend class InventoryWindow;

        // Rebuild the screen-reader option list (one option per merchant item).
        void a11yBuild();
        // Label for a merchant item: name, count, and buying price.
        std::string a11yItemLabel(const ItemStack& item);
        // Buy (borrow to the player) the merchant item at sort-model \p index.
        void a11yBuyItem(int sortIndex);
        // Common balance-key handling, shared with the inventory pane. Returns
        // true if the key was consumed.
        bool a11yHandleBalanceKey(MyGUI::KeyCode key);

        // Signature of the merchant list's current contents, used to detect when
        // the spoken list needs rebuilding (items borrowed/returned, partial
        // offers). -1 forces a rebuild on the next frame (e.g. first barter
        // frame, after mTrading/labels are valid).
        long long a11yTradeSignature() const;

        MWGui::A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        long long mA11yLastSig = -1;

        ItemView* mItemView;
        SortFilterItemModel* mSortModel;
        TradeItemModel* mTradeModel;

        static const float sBalanceChangeInitialPause; // in seconds
        static const float sBalanceChangeInterval; // in seconds

        MyGUI::Button* mFilterAll;
        MyGUI::Button* mFilterWeapon;
        MyGUI::Button* mFilterApparel;
        MyGUI::Button* mFilterMagic;
        MyGUI::Button* mFilterMisc;

        MyGUI::EditBox* mFilterEdit;

        MyGUI::Button* mIncreaseButton;
        MyGUI::Button* mDecreaseButton;
        MyGUI::TextBox* mTotalBalanceLabel;
        Gui::NumericEditBox* mTotalBalance;

        MyGUI::Widget* mBottomPane;

        MyGUI::Button* mMaxSaleButton;
        MyGUI::Button* mCancelButton;
        MyGUI::Button* mOfferButton;
        MyGUI::TextBox* mPlayerGold;
        MyGUI::TextBox* mMerchantGold;

        int mItemToSell;

        int mCurrentBalance;
        int mCurrentMerchantOffer;

        bool mUpdateNextFrame;

        void updateOffer();

        void onItemSelected(int index);
        void sellItem(MyGUI::Widget* sender, std::size_t count);

        void borrowItem(int index, size_t count);
        void returnItem(int index, size_t count);

        int getMerchantServices();

        void onFilterChanged(MyGUI::Widget* sender);
        void onNameFilterChanged(MyGUI::EditBox* sender);
        void onOfferButtonClicked(MyGUI::Widget* sender);
        void onAccept(MyGUI::EditBox* sender);
        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onMaxSaleButtonClicked(MyGUI::Widget* sender);
        void onIncreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onDecreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onBalanceButtonReleased(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onBalanceValueChanged(int value);
        void onRepeatClick(MyGUI::Widget* widget, MyGUI::ControllerItem* controller);
        void onOfferSubmitted(MyGUI::Widget* sender, size_t offerAmount);

        void addRepeatController(MyGUI::Widget* widget);

        void onIncreaseButtonTriggered();
        void onDecreaseButtonTriggered();

        void addOrRemoveGold(int gold, const MWWorld::Ptr& actor);

        void updateLabels();

        void onReferenceUnavailable() override;

        int getMerchantGold();
    };
}

#endif
