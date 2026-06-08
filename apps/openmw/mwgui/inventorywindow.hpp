#ifndef MGUI_Inventory_H
#define MGUI_Inventory_H

#include "mode.hpp"
#include "windowpinnablebase.hpp"

#include "accessibility/editfield.hpp"
#include "accessibility/screen.hpp"

#include "../mwrender/characterpreview.hpp"
#include "../mwworld/ptr.hpp"

#include <components/misc/notnullptr.hpp>

namespace osg
{
    class Group;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWGui
{
    namespace Widgets
    {
        class MWDynamicStat;
    }

    class ItemView;
    class SortFilterItemModel;
    class TradeItemModel;
    class DragAndDrop;
    class ItemModel;
    class ItemTransfer;
    struct ItemStack;

    class InventoryWindow : public WindowPinnableBase
    {
    public:
        explicit InventoryWindow(DragAndDrop& dragAndDrop, ItemTransfer& itemTransfer, osg::Group* parent,
            Resource::ResourceSystem* resourceSystem);

        void onOpen() override;

        void onClose() override;

        /// start trading, disables item drag&drop
        void setTrading(bool trading);

        void onFrame(float dt) override;

        void pickUpObject(MWWorld::Ptr object);

        MWWorld::Ptr getAvatarSelectedItem(int x, int y);

        void rebuildAvatar();

        SortFilterItemModel* getSortFilterModel();
        TradeItemModel* getTradeModel();
        ItemModel* getModel();

        void updateItemView();

        void updatePlayer();

        void clear() override;

        void useItem(const MWWorld::Ptr& ptr, bool force = false);

        void setGuiMode(GuiMode mode);

        void onInventoryUpdate(const MWWorld::Ptr& ptr) override;

        /// Cycle to previous/next weapon
        void cycle(bool next);

        std::string_view getWindowIdForLua() const override { return "Inventory"; }

        ControllerButtons* getControllerButtons() override;

    protected:
        void onTitleDoubleClicked() override;
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

    private:
        Misc::NotNullPtr<DragAndDrop> mDragAndDrop;
        Misc::NotNullPtr<ItemTransfer> mItemTransfer;

        int mSelectedItem;

        MWWorld::Ptr mPtr;

        MWGui::ItemView* mItemView;
        SortFilterItemModel* mSortModel;
        TradeItemModel* mTradeModel;

        MyGUI::Widget* mAvatar;
        MyGUI::ImageBox* mAvatarImage;
        MyGUI::TextBox* mArmorRating;
        Widgets::MWDynamicStat* mEncumbranceBar;

        MyGUI::Widget* mLeftPane;
        MyGUI::Widget* mRightPane;

        MyGUI::Button* mFilterAll;
        MyGUI::Button* mFilterWeapon;
        MyGUI::Button* mFilterApparel;
        MyGUI::Button* mFilterMagic;
        MyGUI::Button* mFilterMisc;

        MyGUI::EditBox* mFilterEdit;

        GuiMode mGuiMode;

        int mLastXSize;
        int mLastYSize;

        std::unique_ptr<MyGUI::ITexture> mPreviewTexture;
        std::unique_ptr<MWRender::InventoryPreview> mPreview;

        bool mTrading;
        bool mUpdateNextFrame;

        void toggleMaximized();

        void onItemSelected(int index);
        void onItemSelectedFromSourceModel(int index);

        void onBackgroundSelected();

        enum class ControllerAction
        {
            None,
            Use,
            Transfer,
            Sell,
            Drop,
        };
        ControllerAction mPendingControllerAction;

        void sellItem(MyGUI::Widget* sender, std::size_t count);
        void dragItem(MyGUI::Widget* sender, std::size_t count);
        void transferItem(MyGUI::Widget* sender, std::size_t count);
        void dropItem(MyGUI::Widget* sender, std::size_t count);
        void equipItem(std::size_t count);

        void onWindowResize(MyGUI::Window* sender);
        void onFilterChanged(MyGUI::Widget* sender);
        void onNameFilterChanged(MyGUI::EditBox* sender);
        void onAvatarClicked(MyGUI::Widget* sender);
        void onPinToggled() override;

        void updateEncumbranceBar();
        void notifyContentChanged();
        void dirtyPreview();
        void updatePreviewSize();
        void updateArmorRating();

        MyGUI::IntSize getPreviewViewportSize() const;
        osg::Vec2i mapPreviewWindowToViewport(int x, int y) const;

        void adjustPanes();

        /// Unequips count items from mSelectedItem, if it is equipped, and then updates mSelectedItem in case the items
        /// were re-stacked
        void ensureSelectedItemUnequipped(int count);

        // --- Screen-reader accessibility ---------------------------------
        // Virtual-focus controller (the items are drawn by the custom ItemView,
        // not as per-item widgets, so navigation is by index like the container
        // window). Shown alongside Stats/Spells/Map in Inventory mode; switched
        // to via Tab through the A11y::PaneGroup.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor;
        // Screen-reader editing for the native name-filter box (mFilterEdit).
        A11y::EditField mA11yFilterEdit;

        // (Re)build the spoken option list: name filter field, category filter,
        // then one entry per item stack, then encumbrance/armor info. Called on
        // open and after any action that changes the item list.
        void buildAccessibility();
        // Rebuild after an item action, keeping the cursor near its old row.
        void a11yRebuildKeepingCursor();
        // Activate the currently-selected item (Enter): equip/use it, or open
        // the count picker first for a partial drop. \p drop selects the
        // drop-into-world action instead of equip/use.
        void a11yActivateItem(int sortIndex, bool drop);
        // Store the currently-selected item into the open container (the S key)
        // -- the screen-reader equivalent of Alt+clicking it across. No-op
        // outside GM_Container. Opens the count picker for a stack unless
        // \p wholeStack (Shift+S). Mirrors a11yActivateItem's flow.
        void a11yStoreItem(int sortIndex, bool wholeStack);
        // Count-picker OK callback for an a11y-initiated partial store.
        void onA11yCountStored(MyGUI::Widget* sender, std::size_t count);
        // Count-picker OK callback for an a11y-initiated partial drop.
        void onA11yCountDropped(MyGUI::Widget* sender, std::size_t count);
        // Begin following \p item across asynchronous inventory updates so the
        // spoken list + cursor track it as it reorders. The current
        // (equipped, count) signature is captured so a11yUpdateFollow can detect
        // when the async change lands.
        void a11yStartFollowing(const ItemStack& item);
        // Per-frame handling of the follow window: poll for the followed item's
        // change to land, then rebuild the list, move the cursor onto it and
        // announce its settled state. Also keeps the list synced on a passive
        // content update when not following.
        void a11yUpdateFollow(bool contentUpdated, float dt);
        // Spoken description of one item stack (name, count, equipped state).
        std::string a11yItemLabel(const ItemStack& item) const;
        // Cycle the category filter (All/Weapon/Apparel/Magic/Misc).
        void a11yCycleCategory(bool next);
        // Spoken name of the active category filter.
        std::string a11yCategoryName() const;
        // Encumbrance + armor rating, spoken on demand.
        std::string a11yEncumbranceValue() const;
        // The sort-model index a11y is currently on, or -1 if not on an item.
        int mA11yPendingDropIndex = -1;
        // Number of leading non-item options (name filter + category) in the
        // a11y list, so a list index can be mapped to a sort-model item row.
        size_t mA11yItemBase = 0;
        // Index (0..4) of the active category filter button, tracked so the
        // a11y category option can cycle and report it (SortFilterItemModel has
        // no public getter). Maps to All/Weapon/Apparel/Magic/Misc.
        int mA11yCategoryIndex = 0;
        // Whether a11y text-edit mode was active last frame, so we can rebuild
        // the item list once the user finishes editing the name filter.
        bool mA11yWasEditing = false;
        // After an a11y equip/use/drop, the inventory changes asynchronously and
        // the list reorders, so we "follow" the acted-on item to land the cursor
        // on it and announce its settled state. Equip/unequip only flips an
        // item's equipped flag (no item added/removed), so it does NOT fire
        // onInventoryUpdate -- we therefore can't rely on a content-update
        // signal. Instead, while following we POLL the model every frame and
        // detect when the followed item's (equipped, count, list size) signature
        // settles to a changed value, then rebuild + announce once. mA11yFollowItem
        // is the item's base Ptr (empty = not following); the timer bounds the
        // window so a consumed item doesn't follow forever.
        MWWorld::Ptr mA11yFollowItem;
        float mA11yFollowTimer = 0.f;
        // Signature of the followed item captured at action time, compared each
        // frame to detect when the async change has landed.
        bool mA11yFollowWasEquipped = false;
        size_t mA11yFollowCount = 0;
        size_t mA11yFollowItemTotal = 0;

        // In barter mode, selling borrows an item to the merchant, which fires
        // no onInventoryUpdate; the follow poll above is also the wrong tool
        // (the count-dialog confirm lands much later). Instead rebuild the
        // spoken list whenever this content signature changes. -1 forces a
        // rebuild on the first barter frame (after mTrading is set).
        long long a11yTradeSignature() const;
        long long mA11yLastTradeSig = -1;
    };
}

#endif // Inventory_H
