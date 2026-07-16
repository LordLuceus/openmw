#include "statswindow.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextIterator.h>
#include <MyGUI_Window.h>

#include <components/debug/debuglog.hpp>

#include <components/esm3/loadbsgn.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadspel.hpp>

#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/player.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/magiceffects.hpp"
#include "../mwmechanics/npcstats.hpp"

#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#include "accessibility/panegroup.hpp"
#include "accessibility/spelltext.hpp"
#include "tooltips.hpp"

namespace
{
    // True when a stat carries PERMANENT damage (Damage Attribute / Damage
    // Skill -- e.g. a bonewalker's curse) that no temporary active effect
    // accounts for. Sighted players see this as a red stat number in the stats
    // window; there is no entry for it in the active-effects list (unlike Drain,
    // which is temporary and IS listed), so without this a screen-reader user
    // has no way to know a stat has been permanently lowered.
    //
    // Damage, Drain and Absorb all increment the same underlying mDamage
    // counter on the stat. Drain and Absorb are temporary and reverse
    // themselves when the effect ends, and while active they appear in the
    // magic-effects magnitudes (and the active-effects list). So we subtract the
    // live Drain + Absorb magnitudes from mDamage to isolate the permanent,
    // otherwise-invisible portion.
    bool statPermanentlyDamaged(const MWWorld::Ptr& player, float damage, const ESM::RefId& drainEffect,
        const ESM::RefId& absorbEffect, const ESM::RefId& arg)
    {
        if (damage <= 0.f)
            return false;
        const auto& effects = player.getClass().getCreatureStats(player).getMagicEffects();
        const float drain = effects.getOrDefault(MWMechanics::EffectKey(drainEffect, arg)).getMagnitude();
        const float absorb = effects.getOrDefault(MWMechanics::EffectKey(absorbEffect, arg)).getMagnitude();
        // Guard against float rounding: only report clearly-positive residue.
        return damage - drain - absorb > 0.5f;
    }
}

namespace MWGui
{
    StatsWindow::StatsWindow(DragAndDrop* drag)
        : WindowPinnableBase("openmw_stats_window.layout")
        , NoDrop(drag, mMainWidget)
        , mSkillView(nullptr)
        , mReputation(0)
        , mBounty(0)
        , mChanged(true)
        , mMinFullWidth(mMainWidget->getSize().width)
    {

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        MyGUI::Widget* attributeView = getWidget("AttributeView");
        MyGUI::IntCoord coord{ 0, 0, 204, 18 };
        const MyGUI::Align alignment = MyGUI::Align::Left | MyGUI::Align::Top | MyGUI::Align::HStretch;
        for (const ESM::Attribute& attribute : store.get<ESM::Attribute>())
        {
            auto* box = attributeView->createWidget<MyGUI::Button>({}, coord, alignment);
            box->setUserString("ToolTipType", "Layout");
            box->setUserString("ToolTipLayout", "AttributeToolTip");
            box->setUserString("Caption_AttributeName", attribute.mName);
            box->setUserString("Caption_AttributeDescription", attribute.mDescription);
            box->setUserString("ImageTexture_AttributeImage", attribute.mIcon);
            coord.top += coord.height;
            auto* name = box->createWidget<MyGUI::TextBox>("SandText", { 0, 0, 160, 18 }, alignment);
            name->setNeedMouseFocus(false);
            name->setCaption(attribute.mName);
            auto* value = box->createWidget<MyGUI::TextBox>(
                "SandTextRight", { 160, 0, 44, 18 }, MyGUI::Align::Right | MyGUI::Align::Top);
            value->setNeedMouseFocus(false);
            mAttributeWidgets.emplace(attribute.mId, value);
        }

        getWidget(mSkillView, "SkillView");
        getWidget(mLeftPane, "LeftPane");
        getWidget(mRightPane, "RightPane");

        for (const ESM::Skill& skill : store.get<ESM::Skill>())
        {
            mSkillValues.emplace(skill.mId, MWMechanics::SkillValue());
            mSkillWidgetMap.emplace(skill.mId, std::make_pair<MyGUI::TextBox*, MyGUI::TextBox*>(nullptr, nullptr));
        }

        MyGUI::Window* t = mMainWidget->castType<MyGUI::Window>();
        t->eventWindowChangeCoord += MyGUI::newDelegate(this, &StatsWindow::onWindowResize);

        // Accessibility: an invisible anchor holds key focus while the A11y
        // screen navigates widget-less options internally (virtual focus). The
        // option list is (re)built in buildAccessibility() and activated when
        // the window becomes visible.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>({}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);

        if (Settings::gui().mControllerMenus)
        {
            setPinButtonVisible(false);
            mControllerButtons.mLStick = "#{Interface:Mouse}";
            mControllerButtons.mRStick = "#{Interface:ScrollDown}";
            mControllerButtons.mB = "#{Interface:Back}";
        }

        onWindowResize(t);
    }

    void StatsWindow::onMouseWheel(MyGUI::Widget* /*sender*/, int rel)
    {
        if (mSkillView->getViewOffset().top + rel * 0.3 > 0)
            mSkillView->setViewOffset(MyGUI::IntPoint(0, 0));
        else
            mSkillView->setViewOffset(
                MyGUI::IntPoint(0, static_cast<int>(mSkillView->getViewOffset().top + rel * 0.3)));
    }

    void StatsWindow::onWindowResize(MyGUI::Window* window)
    {
        int windowWidth = window->getSize().width;
        int windowHeight = window->getSize().height;

        // initial values defined in openmw_stats_window.layout, if custom options are not present in .layout, a default
        // is loaded
        float leftPaneRatio = 0.44f;
        if (mLeftPane->isUserString("LeftPaneRatio"))
            leftPaneRatio = MyGUI::utility::parseFloat(mLeftPane->getUserString("LeftPaneRatio"));

        int leftOffsetWidth = 24;
        if (mLeftPane->isUserString("LeftOffsetWidth"))
            leftOffsetWidth = MyGUI::utility::parseInt(mLeftPane->getUserString("LeftOffsetWidth"));

        float rightPaneRatio = 1.f - leftPaneRatio;
        int minLeftWidth = static_cast<int>(mMinFullWidth * leftPaneRatio);
        int minLeftOffsetWidth = minLeftWidth + leftOffsetWidth;

        // if there's no space for right pane
        mRightPane->setVisible(windowWidth >= minLeftOffsetWidth);
        if (!mRightPane->getVisible())
        {
            mLeftPane->setCoord(MyGUI::IntCoord(0, 0, windowWidth - leftOffsetWidth, windowHeight));
        }
        // if there's some space for right pane
        else if (windowWidth < mMinFullWidth)
        {
            mLeftPane->setCoord(MyGUI::IntCoord(0, 0, minLeftWidth, windowHeight));
            mRightPane->setCoord(MyGUI::IntCoord(minLeftWidth, 0, windowWidth - minLeftWidth, windowHeight));
        }
        // if there's enough space for both panes
        else
        {
            mLeftPane->setCoord(MyGUI::IntCoord(0, 0, static_cast<int>(leftPaneRatio * windowWidth), windowHeight));
            mRightPane->setCoord(MyGUI::IntCoord(static_cast<int>(leftPaneRatio * windowWidth), 0,
                static_cast<int>(rightPaneRatio * windowWidth), windowHeight));
        }

        // Canvas size must be expressed with VScroll disabled, otherwise MyGUI would expand the scroll area when the
        // scrollbar is hidden
        mSkillView->setVisibleVScroll(false);
        mSkillView->setCanvasSize(mSkillView->getWidth(), mSkillView->getCanvasSize().height);
        mSkillView->setVisibleVScroll(true);
    }

    void StatsWindow::setBar(const std::string& name, const std::string& tname, int val, int max)
    {
        MyGUI::ProgressBar* pt;
        getWidget(pt, name);

        std::stringstream out;
        out << val << "/" << max;
        setText(tname, out.str());

        pt->setProgressRange(std::max(0, max));
        pt->setProgressPosition(std::max(0, val));
    }

    void StatsWindow::setPlayerName(const std::string& playerName)
    {
        mMainWidget->castType<MyGUI::Window>()->setCaption(playerName);
    }

    void StatsWindow::setAttribute(ESM::RefId id, const MWMechanics::AttributeValue& value)
    {
        auto it = mAttributeWidgets.find(id);
        if (it != mAttributeWidgets.end())
        {
            MyGUI::TextBox* box = it->second;
            box->setCaption(std::to_string(static_cast<int>(value.getModified())));
            if (value.getModified() > value.getBase())
                box->_setWidgetState("increased");
            else if (value.getModified() < value.getBase())
                box->_setWidgetState("decreased");
            else
                box->_setWidgetState("normal");
        }
    }

    void StatsWindow::setValue(std::string_view id, const MWMechanics::DynamicStat<float>& value)
    {
        int current = static_cast<int>(value.getCurrent());
        int modified = static_cast<int>(value.getModified(false));

        // Fatigue can be negative
        if (id != "FBar")
            current = std::max(0, current);

        setBar(std::string(id), std::string(id) + "T", current, modified);

        // health, magicka, fatigue tooltip
        MyGUI::Widget* w;
        std::string valStr = MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
        if (id == "HBar")
        {
            getWidget(w, "Health");
            w->setUserString("Caption_HealthDescription", "#{sHealthDesc}\n" + valStr);
        }
        else if (id == "MBar")
        {
            getWidget(w, "Magicka");
            w->setUserString("Caption_HealthDescription", "#{sMagDesc}\n" + valStr);
        }
        else if (id == "FBar")
        {
            getWidget(w, "Fatigue");
            w->setUserString("Caption_HealthDescription", "#{sFatDesc}\n" + valStr);
        }
    }

    void StatsWindow::setValue(std::string_view id, const std::string& value)
    {
        if (id == "name")
            setPlayerName(value);
        else if (id == "race")
            setText("RaceText", value);
        else if (id == "class")
            setText("ClassText", value);
    }

    void StatsWindow::setValue(std::string_view id, int value)
    {
        if (id == "level")
        {
            std::ostringstream text;
            text << value;
            setText("LevelText", text.str());
        }
    }

    void setSkillProgress(MyGUI::Widget* w, float progress, ESM::RefId skillId)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();

        float progressRequirement = player.getClass().getNpcStats(player).getSkillProgressRequirement(
            skillId, *esmStore.get<ESM::Class>().find(player.get<ESM::NPC>()->mBase->mClass));

        // This is how vanilla MW displays the progress bar (I think). Note it's slightly inaccurate,
        // due to the int casting in the skill levelup logic. Also the progress label could in rare cases
        // reach 100% without the skill levelling up.
        // Leaving the original display logic for now, for consistency with ess-imported savegames.
        int progressPercent = int(float(progress) / float(progressRequirement) * 100.f + 0.5f);

        w->setUserString("Caption_SkillProgressText", MyGUI::utility::toString(progressPercent) + "/100");
        w->setUserString("RangePosition_SkillProgress", MyGUI::utility::toString(progressPercent));
    }

    void StatsWindow::setValue(ESM::RefId id, const MWMechanics::SkillValue& value)
    {
        mSkillValues[id] = value;
        std::pair<MyGUI::TextBox*, MyGUI::TextBox*> widgets = mSkillWidgetMap[id];
        MyGUI::TextBox* valueWidget = widgets.second;
        MyGUI::TextBox* nameWidget = widgets.first;
        if (valueWidget && nameWidget)
        {
            float modified = value.getModified(), base = value.getBase();
            std::string text = MyGUI::utility::toString(static_cast<int>(modified));
            std::string state = "normal";
            if (modified > base)
                state = "increased";
            else if (modified < base)
                state = "decreased";

            int widthBefore = valueWidget->getTextSize().width;

            valueWidget->setCaption(text);
            valueWidget->_setWidgetState(state);

            int widthAfter = valueWidget->getTextSize().width;
            if (widthBefore != widthAfter)
            {
                valueWidget->setCoord(valueWidget->getLeft() - (widthAfter - widthBefore), valueWidget->getTop(),
                    valueWidget->getWidth() + (widthAfter - widthBefore), valueWidget->getHeight());
                nameWidget->setSize(nameWidget->getWidth() - (widthAfter - widthBefore), nameWidget->getHeight());
            }

            if (value.getBase() < 100)
            {
                nameWidget->setUserString("Visible_SkillMaxed", "false");
                nameWidget->setUserString("UserData^Hidden_SkillMaxed", "true");
                nameWidget->setUserString("Visible_SkillProgressVBox", "true");
                nameWidget->setUserString("UserData^Hidden_SkillProgressVBox", "false");

                valueWidget->setUserString("Visible_SkillMaxed", "false");
                valueWidget->setUserString("UserData^Hidden_SkillMaxed", "true");
                valueWidget->setUserString("Visible_SkillProgressVBox", "true");
                valueWidget->setUserString("UserData^Hidden_SkillProgressVBox", "false");

                setSkillProgress(nameWidget, value.getProgress(), id);
                setSkillProgress(valueWidget, value.getProgress(), id);
            }
            else
            {
                nameWidget->setUserString("Visible_SkillMaxed", "true");
                nameWidget->setUserString("UserData^Hidden_SkillMaxed", "false");
                nameWidget->setUserString("Visible_SkillProgressVBox", "false");
                nameWidget->setUserString("UserData^Hidden_SkillProgressVBox", "true");

                valueWidget->setUserString("Visible_SkillMaxed", "true");
                valueWidget->setUserString("UserData^Hidden_SkillMaxed", "false");
                valueWidget->setUserString("Visible_SkillProgressVBox", "false");
                valueWidget->setUserString("UserData^Hidden_SkillProgressVBox", "true");
            }
        }
    }

    void StatsWindow::configureSkills(const std::vector<ESM::RefId>& major, const std::vector<ESM::RefId>& minor)
    {
        mMajorSkills = major;
        mMinorSkills = minor;

        // Update misc skills with the remaining skills not in major or minor
        std::set<ESM::RefId> skillSet;
        std::copy(major.begin(), major.end(), std::inserter(skillSet, skillSet.begin()));
        std::copy(minor.begin(), minor.end(), std::inserter(skillSet, skillSet.begin()));
        mMiscSkills.clear();
        const auto& store = MWBase::Environment::get().getWorld()->getStore().get<ESM::Skill>();
        for (const auto& skill : store)
        {
            if (!skillSet.contains(skill.mId))
                mMiscSkills.push_back(skill.mId);
        }

        updateSkillArea();
    }

    void StatsWindow::onFrame(float dt)
    {
        NoDrop::onFrame(dt);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::NpcStats& playerStats = player.getClass().getNpcStats(player);
        const auto& store = MWBase::Environment::get().getESMStore();

        std::stringstream detail;
        bool first = true;
        for (const auto& attribute : store->get<ESM::Attribute>())
        {
            int mult = playerStats.getLevelupAttributeMultiplier(attribute.mId);
            mult = std::min(mult, static_cast<int>(100 - playerStats.getAttribute(attribute.mId).getBase()));
            if (mult > 1)
            {
                if (!first)
                    detail << '\n';
                detail << attribute.mName << " x" << MyGUI::utility::toString(mult);
                first = false;
            }
        }
        std::string detailText = detail.str();

        // level progress
        MyGUI::Widget* levelWidget;
        for (int i = 0; i < 2; ++i)
        {
            int max = store->get<ESM::GameSetting>().find("iLevelUpTotal")->mValue.getInteger();
            getWidget(levelWidget, i == 0 ? "Level_str" : "LevelText");

            levelWidget->setUserString(
                "RangePosition_LevelProgress", MyGUI::utility::toString(playerStats.getLevelProgress()));
            levelWidget->setUserString("Range_LevelProgress", MyGUI::utility::toString(max));
            levelWidget->setUserString("Caption_LevelProgressText",
                MyGUI::utility::toString(playerStats.getLevelProgress()) + "/" + MyGUI::utility::toString(max));
            levelWidget->setUserString("Caption_LevelDetailText", detailText);
        }

        setFactions(playerStats.getFactionRanks());
        setExpelled(playerStats.getExpelled());

        const auto& signId = MWBase::Environment::get().getWorld()->getPlayer().getBirthSign();

        setBirthSign(signId);
        setReputation(playerStats.getReputation());
        setBounty(playerStats.getBounty());

        // Note mChanged before updateSkillArea() clears it: it signals that the
        // character data behind the option list may have changed shape, not
        // just its values. Conditionally-present options -- Faction and
        // Birthsign -- are only added when their data is non-empty, and that
        // data arrives via the StatsWatcher *after* the initial onOpen build
        // (the very first time the sheet is opened in a session, factions
        // hadn't been populated yet, so the Faction line was silently missing).
        // Rebuild the a11y list on a real data change so those options appear /
        // disappear correctly; buildAccessibility() preserves the selection.
        const bool dataChanged = mChanged;
        if (mChanged)
            updateSkillArea();

        // Rebuild when the screen is in play: either the sole active screen, or
        // enrolled in the PaneGroup (where it may be suspended behind another
        // pane right when the faction/birthsign data arrives -- we must still
        // pick it up so it's present when the user Tabs back).
        if (dataChanged && (mA11y.isActive() || A11y::PaneGroup::instance().contains(&mA11y)))
            buildAccessibility();

        // In Inventory mode, let the PaneGroup pick the initial pane (Stats)
        // once the windows have settled, rather than racing in onOpen.
        if (A11y::PaneGroup::instance().contains(&mA11y))
            A11y::PaneGroup::instance().maybeActivateInitial(&mA11y);

        mA11y.onFrame(dt);
    }

    void StatsWindow::onOpen()
    {
        onWindowResize(mMainWidget->castType<MyGUI::Window>());

        // Build the option list from the current character. The stats arrive
        // via the StatsWatcher (which may push updates after this point); the
        // widget-less value callbacks always read the latest values, so an early
        // build is fine.
        buildAccessibility();

        // In Inventory mode the Stats window is shown alongside Inventory (and
        // later Spells/Map). Enrol in the PaneGroup so Tab/Shift+Tab switch
        // between those panes; Stats is the first pane (order 0) and claims
        // focus initially. In any other context (e.g. pinned during gameplay)
        // there's no sibling pane, so just claim screen-reader input directly.
        if (MWBase::Environment::get().getWindowManager()->getMode() == GM_Inventory)
        {
            A11y::PaneGroup::instance().enrol(&mA11y,
                std::string(MWBase::Environment::get().getWindowManager()->getGameSettingString("sStats", "Stats")), 0);
        }
        else
        {
            mA11y.activate();
        }
    }

    void StatsWindow::onClose()
    {
        A11y::PaneGroup::instance().withdraw(&mA11y);
        mA11y.deactivate();
    }

    void StatsWindow::setFactions(const FactionList& factions)
    {
        if (mFactions != factions)
        {
            mFactions = factions;
            mChanged = true;
        }
    }

    void StatsWindow::setExpelled(const std::set<ESM::RefId>& expelled)
    {
        if (mExpelled != expelled)
        {
            mExpelled = expelled;
            mChanged = true;
        }
    }

    void StatsWindow::setBirthSign(const ESM::RefId& signId)
    {
        if (signId != mBirthSignId)
        {
            mBirthSignId = signId;
            mChanged = true;
        }
    }

    void StatsWindow::addSeparator(MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        MyGUI::ImageBox* separator = mSkillView->createWidget<MyGUI::ImageBox>("MW_HLine",
            MyGUI::IntCoord(10, coord1.top, coord1.width + coord2.width - 4, 18),
            MyGUI::Align::Left | MyGUI::Align::Top | MyGUI::Align::HStretch);
        separator->eventMouseWheel += MyGUI::newDelegate(this, &StatsWindow::onMouseWheel);
        mSkillWidgets.push_back(separator);

        coord1.top += separator->getHeight();
        coord2.top += separator->getHeight();
    }

    void StatsWindow::addGroup(std::string_view label, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        MyGUI::TextBox* groupWidget = mSkillView->createWidget<MyGUI::TextBox>("SandBrightText",
            MyGUI::IntCoord(0, coord1.top, coord1.width + coord2.width, coord1.height),
            MyGUI::Align::Left | MyGUI::Align::Top | MyGUI::Align::HStretch);
        groupWidget->setCaption(MyGUI::UString(label));
        groupWidget->eventMouseWheel += MyGUI::newDelegate(this, &StatsWindow::onMouseWheel);
        mSkillWidgets.push_back(groupWidget);

        const int lineHeight = Settings::gui().mFontSize + 2;
        coord1.top += lineHeight;
        coord2.top += lineHeight;
    }

    std::pair<MyGUI::TextBox*, MyGUI::TextBox*> StatsWindow::addValueItem(std::string_view text,
        const std::string& value, const std::string& state, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        MyGUI::TextBox *skillNameWidget, *skillValueWidget;

        skillNameWidget = mSkillView->createWidget<MyGUI::TextBox>(
            "SandText", coord1, MyGUI::Align::Left | MyGUI::Align::Top | MyGUI::Align::HStretch);
        skillNameWidget->setCaption(MyGUI::UString(text));
        skillNameWidget->eventMouseWheel += MyGUI::newDelegate(this, &StatsWindow::onMouseWheel);

        skillValueWidget = mSkillView->createWidget<MyGUI::TextBox>(
            "SandTextRight", coord2, MyGUI::Align::Right | MyGUI::Align::Top);
        skillValueWidget->setCaption(value);
        skillValueWidget->_setWidgetState(state);
        skillValueWidget->eventMouseWheel += MyGUI::newDelegate(this, &StatsWindow::onMouseWheel);

        // resize dynamically according to text size
        int textWidthPlusMargin = skillValueWidget->getTextSize().width + 12;
        skillValueWidget->setCoord(
            coord2.left + coord2.width - textWidthPlusMargin, coord2.top, textWidthPlusMargin, coord2.height);
        skillNameWidget->setSize(skillNameWidget->getSize() + MyGUI::IntSize(coord2.width - textWidthPlusMargin, 0));

        mSkillWidgets.push_back(skillNameWidget);
        mSkillWidgets.push_back(skillValueWidget);

        const int lineHeight = Settings::gui().mFontSize + 2;
        coord1.top += lineHeight;
        coord2.top += lineHeight;

        return std::make_pair(skillNameWidget, skillValueWidget);
    }

    MyGUI::Widget* StatsWindow::addItem(const std::string& text, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        MyGUI::TextBox* skillNameWidget;

        skillNameWidget = mSkillView->createWidget<MyGUI::TextBox>("SandText", coord1, MyGUI::Align::Default);

        skillNameWidget->setCaption(text);
        skillNameWidget->eventMouseWheel += MyGUI::newDelegate(this, &StatsWindow::onMouseWheel);

        int textWidth = skillNameWidget->getTextSize().width;
        skillNameWidget->setSize(textWidth, skillNameWidget->getHeight());

        mSkillWidgets.push_back(skillNameWidget);

        const int lineHeight = Settings::gui().mFontSize + 2;
        coord1.top += lineHeight;
        coord2.top += lineHeight;

        return skillNameWidget;
    }

    void StatsWindow::addSkills(const std::vector<ESM::RefId>& skills, const std::string& titleId,
        const std::string& titleDefault, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        // Add a line separator if there are items above
        if (!mSkillWidgets.empty())
        {
            addSeparator(coord1, coord2);
        }

        addGroup(
            MWBase::Environment::get().getWindowManager()->getGameSettingString(titleId, titleDefault), coord1, coord2);

        const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();
        for (const ESM::RefId& skillId : skills)
        {
            const ESM::Skill* skill = esmStore.get<ESM::Skill>().search(skillId);
            if (!skill) // Skip unknown skills
                continue;

            auto skillValue = mSkillValues.find(skill->mId);
            if (skillValue == mSkillValues.end())
            {
                Log(Debug::Error) << "Failed to update stats window: can not find value for skill " << skill->mId;
                continue;
            }

            const ESM::Attribute* attr
                = esmStore.get<ESM::Attribute>().find(ESM::Attribute::indexToRefId(skill->mData.mAttribute));

            std::pair<MyGUI::TextBox*, MyGUI::TextBox*> widgets
                = addValueItem(skill->mName, {}, "normal", coord1, coord2);
            mSkillWidgetMap[skill->mId] = std::move(widgets);

            for (int i = 0; i < 2; ++i)
            {
                mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("ToolTipType", "Layout");
                mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("ToolTipLayout", "SkillToolTip");
                mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString(
                    "Caption_SkillName", MyGUI::TextIterator::toTagsString(skill->mName));
                mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString(
                    "Caption_SkillDescription", skill->mDescription);
                mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("Caption_SkillAttribute",
                    "#{sGoverningAttribute}: " + MyGUI::TextIterator::toTagsString(attr->mName));
                mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("ImageTexture_SkillImage", skill->mIcon);
                mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("Range_SkillProgress", "100");
            }

            setValue(skill->mId, skillValue->second);
        }
    }

    void StatsWindow::updateSkillArea()
    {
        mChanged = false;

        for (MyGUI::Widget* widget : mSkillWidgets)
        {
            MyGUI::Gui::getInstance().destroyWidget(widget);
        }
        mSkillWidgets.clear();

        const int valueSize = 40;
        MyGUI::IntCoord coord1(10, 0, mSkillView->getWidth() - (10 + valueSize) - 24, 18);
        MyGUI::IntCoord coord2(coord1.left + coord1.width, coord1.top, valueSize, coord1.height);

        if (!mMajorSkills.empty())
            addSkills(mMajorSkills, "sSkillClassMajor", "Major Skills", coord1, coord2);

        if (!mMinorSkills.empty())
            addSkills(mMinorSkills, "sSkillClassMinor", "Minor Skills", coord1, coord2);

        if (!mMiscSkills.empty())
            addSkills(mMiscSkills, "sSkillClassMisc", "Misc Skills", coord1, coord2);

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWWorld::ESMStore& store = world->getStore();
        const ESM::NPC* player = world->getPlayerPtr().get<ESM::NPC>()->mBase;

        // race tooltip
        const ESM::Race* playerRace = store.get<ESM::Race>().find(player->mRace);

        MyGUI::Widget* raceWidget;
        getWidget(raceWidget, "RaceText");
        ToolTips::createRaceToolTip(raceWidget, playerRace);
        getWidget(raceWidget, "Race_str");
        ToolTips::createRaceToolTip(raceWidget, playerRace);

        // class tooltip
        MyGUI::Widget* classWidget;

        const ESM::Class* playerClass = store.get<ESM::Class>().find(player->mClass);

        getWidget(classWidget, "ClassText");
        ToolTips::createClassToolTip(classWidget, *playerClass);
        getWidget(classWidget, "Class_str");
        ToolTips::createClassToolTip(classWidget, *playerClass);

        if (!mFactions.empty())
        {
            MWWorld::Ptr playerPtr = MWMechanics::getPlayer();
            const MWMechanics::NpcStats& playerStats = playerPtr.getClass().getNpcStats(playerPtr);
            const std::set<ESM::RefId>& expelled = playerStats.getExpelled();

            bool firstFaction = true;
            for (const auto& [factionId, factionRank] : mFactions)
            {
                const ESM::Faction* faction = store.get<ESM::Faction>().find(factionId);
                if (faction->mData.mIsHidden == 1)
                    continue;

                if (firstFaction)
                {
                    // Add a line separator if there are items above
                    if (!mSkillWidgets.empty())
                        addSeparator(coord1, coord2);

                    addGroup(MWBase::Environment::get().getWindowManager()->getGameSettingString("sFaction", "Faction"),
                        coord1, coord2);

                    firstFaction = false;
                }

                MyGUI::Widget* w = addItem(faction->mName, coord1, coord2);

                std::string text;

                text += std::string("#{fontcolourhtml=header}") + faction->mName;

                if (expelled.find(factionId) != expelled.end())
                    text += "\n#{fontcolourhtml=normal}#{sExpelled}";
                else
                {
                    const auto rank = static_cast<size_t>(std::max(0, factionRank));
                    if (rank < faction->mRanks.size())
                        text += std::string("\n#{fontcolourhtml=normal}") + faction->mRanks[rank];
                    if (rank + 1 < faction->mRanks.size() && !faction->mRanks[rank + 1].empty())
                    {
                        // player doesn't have max rank yet
                        text += std::string("\n\n#{fontcolourhtml=header}#{sNextRank} ") + faction->mRanks[rank + 1];

                        const ESM::RankData& rankData = faction->mData.mRankData[rank + 1];
                        const ESM::Attribute* attr1 = store.get<ESM::Attribute>().find(
                            ESM::Attribute::indexToRefId(faction->mData.mAttribute[0]));
                        const ESM::Attribute* attr2 = store.get<ESM::Attribute>().find(
                            ESM::Attribute::indexToRefId(faction->mData.mAttribute[1]));

                        text += "\n#{fontcolourhtml=normal}" + MyGUI::TextIterator::toTagsString(attr1->mName) + ": "
                            + MyGUI::utility::toString(rankData.mAttribute1) + ", "
                            + MyGUI::TextIterator::toTagsString(attr2->mName) + ": "
                            + MyGUI::utility::toString(rankData.mAttribute2);

                        text += "\n\n#{fontcolourhtml=header}#{sFavoriteSkills}";
                        text += "\n#{fontcolourhtml=normal}";
                        bool firstSkill = true;
                        for (int id : faction->mData.mSkills)
                        {
                            const ESM::Skill* skill = store.get<ESM::Skill>().search(ESM::Skill::indexToRefId(id));
                            if (skill)
                            {
                                if (!firstSkill)
                                    text += ", ";

                                firstSkill = false;
                                text += MyGUI::TextIterator::toTagsString(skill->mName);
                            }
                        }

                        text += "\n";

                        if (rankData.mPrimarySkill > 0)
                            text += "\n#{sNeedOneSkill} " + MyGUI::utility::toString(rankData.mPrimarySkill);
                        if (rankData.mFavouredSkill > 0)
                            text += " #{sand} #{sNeedTwoSkills} " + MyGUI::utility::toString(rankData.mFavouredSkill);
                    }
                }

                w->setUserString("ToolTipType", "Layout");
                w->setUserString("ToolTipLayout", "FactionToolTip");
                w->setUserString("Caption_FactionText", text);
            }
        }

        if (!mBirthSignId.empty())
        {
            // Add a line separator if there are items above
            if (!mSkillWidgets.empty())
                addSeparator(coord1, coord2);

            addGroup(MWBase::Environment::get().getWindowManager()->getGameSettingString("sBirthSign", "Sign"), coord1,
                coord2);
            const ESM::BirthSign* sign = store.get<ESM::BirthSign>().find(mBirthSignId);
            MyGUI::Widget* w = addItem(sign->mName, coord1, coord2);

            ToolTips::createBirthsignToolTip(w, mBirthSignId);
        }

        // Add a line separator if there are items above
        if (!mSkillWidgets.empty())
            addSeparator(coord1, coord2);

        addValueItem(MWBase::Environment::get().getWindowManager()->getGameSettingString("sReputation", "Reputation"),
            MyGUI::utility::toString(static_cast<int>(mReputation)), "normal", coord1, coord2);

        for (int i = 0; i < 2; ++i)
        {
            mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("ToolTipType", "Layout");
            mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("ToolTipLayout", "TextToolTip");
            mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("Caption_Text", "#{sSkillsMenuReputationHelp}");
        }

        addValueItem(MWBase::Environment::get().getWindowManager()->getGameSettingString("sBounty", "Bounty"),
            MyGUI::utility::toString(static_cast<int>(mBounty)), "normal", coord1, coord2);

        for (int i = 0; i < 2; ++i)
        {
            mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("ToolTipType", "Layout");
            mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("ToolTipLayout", "TextToolTip");
            mSkillWidgets[mSkillWidgets.size() - 1 - i]->setUserString("Caption_Text", "#{sCrimeHelp}");
        }

        // Canvas size must be expressed with VScroll disabled, otherwise MyGUI would expand the scroll area when the
        // scrollbar is hidden
        mSkillView->setVisibleVScroll(false);
        mSkillView->setCanvasSize(mSkillView->getWidth(), std::max(mSkillView->getHeight(), coord1.top));
        mSkillView->setVisibleVScroll(true);
    }

    std::string StatsWindow::vitalValue(int dynamicIndex) const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        const MWMechanics::DynamicStat<float>& value = stats.getDynamic(dynamicIndex);
        int current = static_cast<int>(value.getCurrent());
        int modified = static_cast<int>(value.getModified(false));
        if (dynamicIndex != 2) // fatigue can be negative
            current = std::max(0, current);
        return MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
    }

    std::vector<A11y::SubItem> StatsWindow::attributeItems() const
    {
        std::vector<A11y::SubItem> items;
        MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        const auto& store = MWBase::Environment::get().getESMStore()->get<ESM::Attribute>();
        for (const ESM::Attribute& attribute : store)
        {
            const std::string name = attribute.mName;
            const std::string description = attribute.mDescription;
            const MWMechanics::AttributeValue attr = stats.getAttribute(attribute.mId);
            const int value = static_cast<int>(attr.getModified());
            // Flag permanent Damage Attribute (e.g. a bonewalker's curse), which
            // -- unlike temporary Drain -- has no active-effects entry to reveal
            // it. Mirrors the red stat number a sighted player sees.
            const bool damaged = statPermanentlyDamaged(player, attr.getDamage(), ESM::MagicEffect::DrainAttribute,
                ESM::MagicEffect::AbsorbAttribute, attribute.mId);
            const std::string suffix = damaged ? ", damaged" : std::string();
            A11y::SubItem item;
            item.label = name + " " + MyGUI::utility::toString(value) + suffix;
            item.tooltips = [name, description, value, suffix] {
                std::string line = name + " " + MyGUI::utility::toString(value) + suffix;
                if (!description.empty())
                    line += ". " + description;
                return std::vector<std::string>{ line };
            };
            items.push_back(std::move(item));
        }
        return items;
    }

    void StatsWindow::appendSkillItems(
        std::vector<A11y::SubItem>& out, const std::vector<ESM::RefId>& skills, const std::string& section) const
    {
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        for (const ESM::RefId& skillId : skills)
        {
            const ESM::Skill* skill = store.get<ESM::Skill>().search(skillId);
            if (!skill)
                continue;
            auto valueIt = mSkillValues.find(skillId);
            const int modified = (valueIt != mSkillValues.end()) ? static_cast<int>(valueIt->second.getModified()) : 0;
            const float damage = (valueIt != mSkillValues.end()) ? valueIt->second.getDamage() : 0.f;

            const std::string name = skill->mName;
            const std::string description = skill->mDescription;
            const ESM::RefId governingId = ESM::Attribute::indexToRefId(skill->mData.mAttribute);

            // Flag permanent Damage Skill (no active-effects entry, unlike Drain).
            const bool damaged = statPermanentlyDamaged(
                MWMechanics::getPlayer(), damage, ESM::MagicEffect::DrainSkill, ESM::MagicEffect::AbsorbSkill, skillId);
            const std::string suffix = damaged ? ", damaged" : std::string();

            A11y::SubItem item;
            item.label = name + " " + MyGUI::utility::toString(modified) + suffix;
            item.section = section;
            item.tooltips = [name, description, governingId, modified, suffix] {
                std::string line = name + " " + MyGUI::utility::toString(modified) + suffix;
                const ESM::Attribute* attr
                    = MWBase::Environment::get().getESMStore()->get<ESM::Attribute>().search(governingId);
                if (attr)
                    line += ". #{sGoverningAttribute}: " + attr->mName;
                if (!description.empty())
                    line += ". " + description;
                return std::vector<std::string>{ line };
            };
            out.push_back(std::move(item));
        }
    }

    std::vector<A11y::SubItem> StatsWindow::skillItems() const
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        std::vector<A11y::SubItem> items;
        appendSkillItems(
            items, mMajorSkills, std::string(winMgr->getGameSettingString("sSkillClassMajor", "Major Skills")));
        appendSkillItems(
            items, mMinorSkills, std::string(winMgr->getGameSettingString("sSkillClassMinor", "Minor Skills")));
        appendSkillItems(
            items, mMiscSkills, std::string(winMgr->getGameSettingString("sSkillClassMisc", "Misc Skills")));
        return items;
    }

    std::string StatsWindow::factionValue() const
    {
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::NpcStats& playerStats = player.getClass().getNpcStats(player);
        const std::set<ESM::RefId>& expelled = playerStats.getExpelled();

        std::string result;
        bool first = true;
        for (const auto& [factionId, factionRank] : mFactions)
        {
            const ESM::Faction* faction = store.get<ESM::Faction>().search(factionId);
            if (!faction || faction->mData.mIsHidden == 1)
                continue;

            std::string entry = faction->mName + ": ";
            if (expelled.find(factionId) != expelled.end())
                entry += "#{sExpelled}";
            else
            {
                const auto rank = static_cast<size_t>(std::max(0, factionRank));
                if (rank < faction->mRanks.size() && !faction->mRanks[rank].empty())
                    entry += faction->mRanks[rank];
            }

            if (!first)
                result += ". ";
            result += entry;
            first = false;
        }
        return result;
    }

    std::vector<std::string> StatsWindow::raceTooltip() const
    {
        std::vector<std::string> lines;
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        MWWorld::Ptr player = MWMechanics::getPlayer();
        const ESM::Race* race = store.get<ESM::Race>().search(player.get<ESM::NPC>()->mBase->mRace);
        if (!race)
            return lines;
        std::string line = race->mName;
        if (!race->mDescription.empty())
            line += ". " + race->mDescription;
        lines.push_back(std::move(line));
        return lines;
    }

    std::vector<std::string> StatsWindow::classTooltip() const
    {
        std::vector<std::string> lines;
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        MWWorld::Ptr player = MWMechanics::getPlayer();
        const ESM::Class* playerClass = store.get<ESM::Class>().search(player.get<ESM::NPC>()->mBase->mClass);
        if (!playerClass || playerClass->mName.empty())
            return lines;
        // Name + specialisation, then the flavour description, mirroring the
        // visual ClassToolTip (which shows "Specialization: <spec>").
        std::string specTag = "#{";
        specTag += ESM::Class::sGmstSpecializationIds[playerClass->mData.mSpecialization];
        specTag += "}";
        std::string line = playerClass->mName + ". #{sSpecialization}: " + specTag;
        if (!playerClass->mDescription.empty())
            line += ". " + playerClass->mDescription;
        lines.push_back(std::move(line));
        return lines;
    }

    std::vector<std::string> StatsWindow::factionTooltip() const
    {
        std::vector<std::string> lines;
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::NpcStats& playerStats = player.getClass().getNpcStats(player);
        const std::set<ESM::RefId>& expelled = playerStats.getExpelled();

        // One line per faction: current rank, plus the next rank and its
        // requirements (attributes, favoured skills), mirroring the visual
        // FactionToolTip.
        for (const auto& [factionId, factionRank] : mFactions)
        {
            const ESM::Faction* faction = store.get<ESM::Faction>().search(factionId);
            if (!faction || faction->mData.mIsHidden == 1)
                continue;

            std::string line = faction->mName;
            if (expelled.find(factionId) != expelled.end())
            {
                line += ". #{sExpelled}";
                lines.push_back(std::move(line));
                continue;
            }

            const auto rank = static_cast<size_t>(std::max(0, factionRank));
            if (rank < faction->mRanks.size() && !faction->mRanks[rank].empty())
                line += ". " + faction->mRanks[rank];

            if (rank + 1 < faction->mRanks.size() && !faction->mRanks[rank + 1].empty())
            {
                line += ". #{sNextRank} " + faction->mRanks[rank + 1];

                const ESM::RankData& rankData = faction->mData.mRankData[rank + 1];
                const ESM::Attribute* attr1
                    = store.get<ESM::Attribute>().search(ESM::Attribute::indexToRefId(faction->mData.mAttribute[0]));
                const ESM::Attribute* attr2
                    = store.get<ESM::Attribute>().search(ESM::Attribute::indexToRefId(faction->mData.mAttribute[1]));
                if (attr1 && attr2)
                    line += ". " + attr1->mName + ": " + MyGUI::utility::toString(rankData.mAttribute1) + ", "
                        + attr2->mName + ": " + MyGUI::utility::toString(rankData.mAttribute2);

                std::string skills;
                for (int id : faction->mData.mSkills)
                {
                    const ESM::Skill* skill = store.get<ESM::Skill>().search(ESM::Skill::indexToRefId(id));
                    if (skill)
                    {
                        if (!skills.empty())
                            skills += ", ";
                        skills += skill->mName;
                    }
                }
                if (!skills.empty())
                    line += ". #{sFavoriteSkills}: " + skills;

                if (rankData.mPrimarySkill > 0)
                    line += ". #{sNeedOneSkill} " + MyGUI::utility::toString(rankData.mPrimarySkill);
                if (rankData.mFavouredSkill > 0)
                    line += " #{sand} #{sNeedTwoSkills} " + MyGUI::utility::toString(rankData.mFavouredSkill);
            }

            lines.push_back(std::move(line));
        }
        return lines;
    }

    std::vector<std::string> StatsWindow::birthSignTooltip() const
    {
        std::vector<std::string> lines;
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::BirthSign* sign = store.get<ESM::BirthSign>().search(mBirthSignId);
        if (!sign)
            return lines;

        std::string intro = sign->mName;
        if (!sign->mDescription.empty())
            intro += ". " + sign->mDescription;
        lines.push_back(std::move(intro));

        // One line per granted power / ability / spell with its full effect
        // breakdown, mirroring the visual BirthSignToolTip categories.
        for (const ESM::RefId& spellId : sign->mPowers.mList)
        {
            const ESM::Spell* spell = store.get<ESM::Spell>().search(spellId);
            if (!spell)
                continue;
            const auto type = static_cast<ESM::Spell::SpellType>(spell->mData.mType);
            if (type != ESM::Spell::ST_Spell && type != ESM::Spell::ST_Ability && type != ESM::Spell::ST_Power)
                continue;
            std::string line = spell->mName;
            for (const ESM::IndexedENAMstruct& effect : spell->mEffects.mList)
            {
                std::string effLine = A11y::formatSpellEffectLine(effect);
                if (!effLine.empty())
                    line += ". " + effLine;
            }
            lines.push_back(std::move(line));
        }
        return lines;
    }

    void StatsWindow::buildAccessibility()
    {
        // Preserve the current selection across the rebuild (the options are
        // widget-less, so remember it by label and restore it silently -- a
        // routine data-change rebuild must not move focus or talk over the user).
        const std::string previousLabel = mA11y.currentLabel();

        mA11y.clear();

        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        // The left pane (name/level/race/class + health/magicka/fatigue) has no
        // native section headings in vanilla, so these are flat top-level items.

        // Player name (window title).
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sName", "Name")),
            .value = [this] { return mMainWidget->castType<MyGUI::Window>()->getCaption().asUTF8(); } });

        // Level, race, class (read the on-screen captions, which the watcher keeps current).
        // The Level item also carries the native level-up tooltip (the
        // "hover the level to see your progress" info): the progress bar value
        // toward the next level plus the per-attribute level-up multipliers,
        // mirroring the visual LevelToolTip (sLevelProgress header + progress /
        // total + the "Attribute xN" detail lines built in onFrame).
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sLevel", "Level")),
            .value = [this] {
                MyGUI::TextBox* w = nullptr;
                getWidget(w, "LevelText");
                return w ? w->getCaption().asUTF8() : std::string();
            },
            .tooltips = [this] {
                MWWorld::Ptr player = MWMechanics::getPlayer();
                const MWMechanics::NpcStats& playerStats = player.getClass().getNpcStats(player);
                const auto& store = MWBase::Environment::get().getESMStore();

                std::vector<std::string> lines;
                const int max = store->get<ESM::GameSetting>().find("iLevelUpTotal")->mValue.getInteger();
                lines.push_back("#{sLevelProgress}: " + MyGUI::utility::toString(playerStats.getLevelProgress()) + " / "
                    + MyGUI::utility::toString(max));

                // The attributes that will gain a level-up bonus, with their
                // multiplier (only those above x1, as the visual tooltip shows).
                for (const auto& attribute : store->get<ESM::Attribute>())
                {
                    int mult = playerStats.getLevelupAttributeMultiplier(attribute.mId);
                    mult = std::min(mult, static_cast<int>(100 - playerStats.getAttribute(attribute.mId).getBase()));
                    if (mult > 1)
                        lines.push_back(attribute.mName + " x" + MyGUI::utility::toString(mult));
                }
                return lines;
            } });
        // Race: name (value) + flavour description (tooltip), mirroring the
        // visual RaceToolTip.
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sRace", "Race")),
            .value = [this] {
                MyGUI::TextBox* w = nullptr;
                getWidget(w, "RaceText");
                return w ? w->getCaption().asUTF8() : std::string();
            },
            .tooltips = [this] { return raceTooltip(); } });
        // Class: name (value) + specialisation and description (tooltip),
        // mirroring the visual ClassToolTip.
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sClass", "Class")),
            .value = [this] {
                MyGUI::TextBox* w = nullptr;
                getWidget(w, "ClassText");
                return w ? w->getCaption().asUTF8() : std::string();
            },
            .tooltips = [this] { return classTooltip(); } });

        // Health, magicka, fatigue (current / max). The native bars carry a
        // one-line description tooltip (sHealthDesc / sMagDesc / sFatDesc).
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sHealth", "Health")),
            .value = [this] { return vitalValue(0); },
            .tooltips = [] { return std::vector<std::string>{ "#{sHealthDesc}" }; } });
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sMagic", "Magicka")),
            .value = [this] { return vitalValue(1); },
            .tooltips = [] { return std::vector<std::string>{ "#{sMagDesc}" }; } });
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sFatigue", "Fatigue")),
            .value = [this] { return vitalValue(2); },
            .tooltips = [] { return std::vector<std::string>{ "#{sFatDesc}" }; } });

        // Attributes and skills as expandable submenus (Enter to enter the list).
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sAttributes", "Attributes")),
            .children = [this] { return attributeItems(); } });
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sSkills", "Skills")),
            .children = [this] { return skillItems(); } });

        // Factions (combined into one spoken line; empty if none). The tooltip
        // gives each faction's rank and what's needed for the next rank,
        // mirroring the visual FactionToolTip.
        if (!mFactions.empty())
        {
            mA11y.add({ .widget = nullptr,
                .label = std::string(winMgr->getGameSettingString("sFaction", "Faction")),
                .value = [this] { return factionValue(); },
                .tooltips = [this] { return factionTooltip(); } });
        }

        // Birthsign: name (value) + description and granted powers/abilities/
        // spells (tooltip), mirroring the visual BirthSignToolTip.
        if (!mBirthSignId.empty())
        {
            mA11y.add({ .widget = nullptr,
                .label = std::string(winMgr->getGameSettingString("sBirthSign", "Birthsign")),
                .value = [this] {
                    const ESM::BirthSign* sign
                        = MWBase::Environment::get().getESMStore()->get<ESM::BirthSign>().search(mBirthSignId);
                    return sign ? sign->mName : std::string();
                },
                .tooltips = [this] { return birthSignTooltip(); } });
        }

        // Reputation and bounty carry the native help-text tooltips.
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sReputation", "Reputation")),
            .value = [this] { return MyGUI::utility::toString(mReputation); },
            .tooltips = [] { return std::vector<std::string>{ "#{sSkillsMenuReputationHelp}" }; } });
        mA11y.add({ .widget = nullptr,
            .label = std::string(winMgr->getGameSettingString("sBounty", "Bounty")),
            .value = [this] { return MyGUI::utility::toString(mBounty); },
            .tooltips = [] { return std::vector<std::string>{ "#{sCrimeHelp}" }; } });

        // Restore the prior selection silently if this was a rebuild of a
        // screen that already had one (active, or suspended behind another
        // pane). onOpen() activates separately (which focuses the first option),
        // so on a fresh open previousLabel is empty and we leave selection alone
        // for activate() to set.
        if (!previousLabel.empty())
            mA11y.selectByLabel(previousLabel, /*announce=*/false);
    }

    void StatsWindow::onPinToggled()
    {
        Settings::windows().mStatsPin.set(mPinned);

        MWBase::Environment::get().getWindowManager()->setHMSVisibility(!mPinned);
    }

    void StatsWindow::onTitleDoubleClicked()
    {
        if (Settings::gui().mControllerMenus)
            return;
        else if (MyGUI::InputManager::getInstance().isShiftPressed())
        {
            MWBase::Environment::get().getWindowManager()->toggleMaximized(this);
            MyGUI::Window* t = mMainWidget->castType<MyGUI::Window>();
            onWindowResize(t);
        }
        else if (!mPinned)
            MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Stats);
    }

    bool StatsWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
            MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();

        return true;
    }

    void StatsWindow::setActiveControllerWindow(bool active)
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        if (winMgr->getMode() == MWGui::GM_Inventory)
        {
            // Fill the screen, or limit to a certain size on large screens. Size chosen to
            // show all stats.
            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            int width = std::min(viewSize.width, getIdealWidth());
            int height = std::min(winMgr->getControllerMenuHeight(), getIdealHeight());
            int x = (viewSize.width - width) / 2;
            int y = (viewSize.height - height) / 2;

            MyGUI::Window* window = mMainWidget->castType<MyGUI::Window>();
            window->setCoord(x, active ? y : viewSize.height + 1, width, height);

            if (active)
                onWindowResize(window);
        }

        WindowBase::setActiveControllerWindow(active);
    }
}
