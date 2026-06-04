#ifndef MWGUI_STATS_WINDOW_H
#define MWGUI_STATS_WINDOW_H

#include "statswatcher.hpp"
#include "windowpinnablebase.hpp"

#include "accessibility/screen.hpp"

#include <components/esm/attr.hpp>
#include <components/esm/refid.hpp>

namespace MWGui
{
    class StatsWindow : public WindowPinnableBase, public NoDrop, public StatsListener
    {
    public:
        typedef std::map<ESM::RefId, int> FactionList;

        /// It would be nice to measure these, but for now they're hardcoded.
        static int getIdealHeight() { return 750; }
        static int getIdealWidth() { return 600; }

        StatsWindow(DragAndDrop* drag);

        /// automatically updates all the data in the stats window, but only if it has changed.
        void onFrame(float dt) override;

        void setBar(const std::string& name, const std::string& tname, int val, int max);
        void setPlayerName(const std::string& playerName);

        /// Set value for the given ID.
        void setAttribute(ESM::RefId id, const MWMechanics::AttributeValue& value) override;
        void setValue(std::string_view id, const MWMechanics::DynamicStat<float>& value) override;
        void setValue(std::string_view id, const std::string& value) override;
        void setValue(std::string_view id, int value) override;
        void setValue(ESM::RefId id, const MWMechanics::SkillValue& value) override;
        void configureSkills(const std::vector<ESM::RefId>& major, const std::vector<ESM::RefId>& minor) override;

        void setReputation(int reputation)
        {
            if (reputation != mReputation)
                mChanged = true;
            this->mReputation = reputation;
        }
        void setBounty(int bounty)
        {
            if (bounty != mBounty)
                mChanged = true;
            this->mBounty = bounty;
        }
        void updateSkillArea();

        void onOpen() override;
        void onClose() override;

        std::string_view getWindowIdForLua() const override { return "Stats"; }

    protected:
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

    private:
        void addSkills(const std::vector<ESM::RefId>& skills, const std::string& titleId,
            const std::string& titleDefault, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2);
        void addSeparator(MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2);
        void addGroup(std::string_view label, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2);
        std::pair<MyGUI::TextBox*, MyGUI::TextBox*> addValueItem(std::string_view text, const std::string& value,
            const std::string& state, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2);
        MyGUI::Widget* addItem(const std::string& text, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2);

        void setFactions(const FactionList& factions);
        void setExpelled(const std::set<ESM::RefId>& expelled);
        void setBirthSign(const ESM::RefId& signId);

        void onWindowResize(MyGUI::Window* window);
        void onMouseWheel(MyGUI::Widget* sender, int rel);

        MyGUI::Widget* mLeftPane;
        MyGUI::Widget* mRightPane;

        MyGUI::ScrollView* mSkillView;

        std::vector<ESM::RefId> mMajorSkills, mMinorSkills, mMiscSkills;
        std::map<ESM::RefId, MWMechanics::SkillValue> mSkillValues;
        std::map<ESM::RefId, MyGUI::TextBox*> mAttributeWidgets;
        std::map<ESM::RefId, std::pair<MyGUI::TextBox*, MyGUI::TextBox*>> mSkillWidgetMap;
        std::map<std::string, MyGUI::Widget*> mFactionWidgetMap;
        FactionList mFactions; ///< Stores a list of factions and the current rank
        ESM::RefId mBirthSignId;
        int mReputation, mBounty;
        std::vector<MyGUI::Widget*> mSkillWidgets; //< Skills and other information
        std::set<ESM::RefId> mExpelled;

        bool mChanged;
        const int mMinFullWidth;

        // Screen-reader support. The stats window rebuilds its skill/faction
        // widgets whenever data changes (updateSkillArea destroys + recreates
        // them) and updates values live every frame, so we must NOT anchor A11y
        // options on those widgets. Instead the screen uses a single invisible
        // anchor (virtual focus) and widget-less Elements whose value callbacks
        // read straight from the player's stats each time they are spoken.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        // (Re)build the option list from the current character.
        void buildAccessibility();
        // Submenu item builders (Attributes / Skills are expandable lists).
        std::vector<A11y::SubItem> attributeItems() const;
        std::vector<A11y::SubItem> skillItems() const;
        void appendSkillItems(std::vector<A11y::SubItem>& out, const std::vector<ESM::RefId>& skills,
            const std::string& section) const;
        // Live value strings for the top-level vital / character / status items.
        std::string vitalValue(int dynamicIndex) const; // 0=health 1=magicka 2=fatigue
        std::string factionValue() const;

    protected:
        void onPinToggled() override;
        void onTitleDoubleClicked() override;
    };
}
#endif
