#include "review.hpp"

#include <cmath>

#include <MyGUI_Button.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_UString.h>

#include <components/esm3/loadbsgn.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/autocalcspell.hpp"
#include "../mwworld/esmstore.hpp"

#include "accessibility/spelltext.hpp"
#include "tooltips.hpp"

namespace
{
    void adjustButtonSize(MyGUI::Button* button)
    {
        // adjust size of button to fit its text
        MyGUI::IntSize size = button->getTextSize();
        button->setSize(size.width + 24, button->getSize().height);
    }
}

namespace MWGui
{
    ReviewDialog::ReviewDialog()
        : WindowModal("openmw_chargen_review.layout")
        , mUpdateSkillArea(false)
        , mControllerFocus(5)
    {
        // Centre dialog
        center();

        // Setup static stats
        MyGUI::Button* button;
        getWidget(mNameWidget, "NameText");
        getWidget(button, "NameButton");
        adjustButtonSize(button);
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &ReviewDialog::onNameClicked);
        mButtons.push_back(button);

        getWidget(mRaceWidget, "RaceText");
        getWidget(button, "RaceButton");
        adjustButtonSize(button);
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &ReviewDialog::onRaceClicked);
        mButtons.push_back(button);

        getWidget(mClassWidget, "ClassText");
        getWidget(button, "ClassButton");
        adjustButtonSize(button);
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &ReviewDialog::onClassClicked);
        mButtons.push_back(button);

        getWidget(mBirthSignWidget, "SignText");
        getWidget(button, "SignButton");
        adjustButtonSize(button);
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &ReviewDialog::onBirthSignClicked);
        mButtons.push_back(button);

        // Setup dynamic stats
        getWidget(mHealth, "Health");
        mHealth->setTitle(MWBase::Environment::get().getWindowManager()->getGameSettingString("sHealth", {}));
        mHealth->setValue(45, 45);

        getWidget(mMagicka, "Magicka");
        mMagicka->setTitle(MWBase::Environment::get().getWindowManager()->getGameSettingString("sMagic", {}));
        mMagicka->setValue(50, 50);

        getWidget(mFatigue, "Fatigue");
        mFatigue->setTitle(MWBase::Environment::get().getWindowManager()->getGameSettingString("sFatigue", {}));
        mFatigue->setValue(160, 160);

        // Setup attributes

        MyGUI::Widget* attributes = getWidget("Attributes");
        const auto& store = MWBase::Environment::get().getWorld()->getStore().get<ESM::Attribute>();
        MyGUI::IntCoord coord{ 8, 4, 250, 18 };
        for (const ESM::Attribute& attribute : store)
        {
            auto* widget
                = attributes->createWidget<Widgets::MWAttribute>("MW_StatNameValue", coord, MyGUI::Align::Default);
            mAttributeWidgets.emplace(attribute.mId, widget);
            widget->setUserString("ToolTipType", "Layout");
            widget->setUserString("ToolTipLayout", "AttributeToolTip");
            widget->setUserString("Caption_AttributeName", attribute.mName);
            widget->setUserString("Caption_AttributeDescription", attribute.mDescription);
            widget->setUserString("ImageTexture_AttributeImage", attribute.mIcon);
            widget->setAttributeId(attribute.mId);
            widget->setAttributeValue(Widgets::MWAttribute::AttributeValue());
            coord.top += coord.height;
        }

        // Setup skills
        getWidget(mSkillView, "SkillView");
        mSkillView->eventMouseWheel += MyGUI::newDelegate(this, &ReviewDialog::onMouseWheel);

        for (const ESM::Skill& skill : MWBase::Environment::get().getESMStore()->get<ESM::Skill>())
        {
            mSkillValues.emplace(skill.mId, MWMechanics::SkillValue());
            mSkillWidgetMap.emplace(skill.mId, static_cast<MyGUI::TextBox*>(nullptr));
        }

        MyGUI::Button* backButton;
        getWidget(backButton, "BackButton");
        backButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ReviewDialog::onBackClicked);
        mButtons.push_back(backButton);

        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");
        okButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ReviewDialog::onOkClicked);
        mButtons.push_back(okButton);

        if (Settings::gui().mControllerMenus)
        {
            setControllerFocus(mButtons, mControllerFocus, true);
            mControllerButtons.mA = "#{Interface:Select}";
            mControllerButtons.mB = "#{Interface:Back}";
            mControllerButtons.mX = "#{Interface:Done}";
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sDone", {})));
        }

        setupAccessibility();
    }

    void ReviewDialog::setupAccessibility()
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        // Header items are real buttons: announce label + chosen value, and
        // activate them to jump back and edit that creation step.
        mA11y.add({ .widget = mButtons[0],
            .describe = [this, winMgr] {
                return std::string(winMgr->getGameSettingString("sName", "Name")) + ": " + mNameWidget->getCaption().asUTF8();
            },
            .activate = [this] { onNameClicked(mButtons[0]); } });
        mA11y.add({ .widget = mButtons[1],
            .describe = [this, winMgr] {
                return std::string(winMgr->getGameSettingString("sRace", "Race")) + ": " + mRaceWidget->getCaption().asUTF8();
            },
            .activate = [this] { onRaceClicked(mButtons[1]); } });
        mA11y.add({ .widget = mButtons[2],
            .describe = [this, winMgr] {
                return std::string(winMgr->getGameSettingString("sClass", "Class")) + ": " + mClassWidget->getCaption().asUTF8();
            },
            .activate = [this] { onClassClicked(mButtons[2]); } });
        mA11y.add({ .widget = mButtons[3],
            .describe = [this, winMgr] {
                return std::string(winMgr->getGameSettingString("sBirthSign", "Birthsign")) + ": "
                    + mBirthSignWidget->getCaption().asUTF8();
            },
            .activate = [this] { onBirthSignClicked(mButtons[3]); } });

        // Read-only derived stats: navigate via invisible proxies that read the
        // live value (current / maximum). The proxies live on mMainWidget.
        auto makeProxy = [this] {
            return mMainWidget->createWidget<MyGUI::Widget>({}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        };
        mHealthProxy = makeProxy();
        mMagickaProxy = makeProxy();
        mFatigueProxy = makeProxy();
        mAttributesProxy = makeProxy();
        mSkillsProxy = makeProxy();

        mA11y.add({ .widget = mHealthProxy,
            .label = std::string(winMgr->getGameSettingString("sHealth", "Health")),
            .value = [this] { return dynamicStatValue(mHealth); } });
        mA11y.add({ .widget = mMagickaProxy,
            .label = std::string(winMgr->getGameSettingString("sMagic", "Magicka")),
            .value = [this] { return dynamicStatValue(mMagicka); } });
        mA11y.add({ .widget = mFatigueProxy,
            .label = std::string(winMgr->getGameSettingString("sFatigue", "Fatigue")),
            .value = [this] { return dynamicStatValue(mFatigue); } });

        // Attributes and skills are expandable submenus: Enter opens them,
        // Up/Down read each entry, T reads its native tooltip, Escape returns.
        mA11y.add({ .widget = mAttributesProxy,
            .label = std::string(winMgr->getGameSettingString("sAttributes", "Attributes")),
            .children = [this] { return attributeItems(); } });

        mA11y.add({ .widget = mSkillsProxy,
            .label = std::string(winMgr->getGameSettingString("sSkills", "Skills")),
            .children = [this] { return skillItems(); } });

        // Trailing navigation buttons.
        mA11y.add({ .widget = mButtons[4],
            .label = std::string(winMgr->getGameSettingString("sBack", "Back")),
            .activate = [this] { onBackClicked(mButtons[4]); } });
        mA11y.add({ .widget = mButtons[5],
            .describe = [this] { return mButtons[5]->getCaption().asUTF8(); },
            .activate = [this] { onOkClicked(mButtons[5]); } });
    }

    void ReviewDialog::onOpen()
    {
        WindowModal::onOpen();
        mUpdateSkillArea = true;
        mA11y.activate(mButtons[0]);
    }

    void ReviewDialog::onClose()
    {
        mA11y.deactivate();
        WindowModal::onClose();
    }

    void ReviewDialog::onFrame(float duration)
    {
        if (mUpdateSkillArea)
        {
            updateSkillArea();
            mUpdateSkillArea = false;
        }
        mA11y.onFrame(duration);
    }

    void ReviewDialog::setPlayerName(const std::string& name)
    {
        mNameWidget->setCaption(name);
    }

    void ReviewDialog::setRace(const ESM::RefId& raceId)
    {
        mRaceId = raceId;

        const ESM::Race* race = MWBase::Environment::get().getESMStore()->get<ESM::Race>().search(mRaceId);
        if (race)
        {
            ToolTips::createRaceToolTip(mRaceWidget, race);
            mRaceWidget->setCaption(race->mName);
        }

        mUpdateSkillArea = true;
    }

    void ReviewDialog::setClass(const ESM::Class& playerClass)
    {
        mClass = playerClass;
        mClassWidget->setCaption(mClass.mName);
        ToolTips::createClassToolTip(mClassWidget, mClass);
    }

    void ReviewDialog::setBirthSign(const ESM::RefId& signId)
    {
        mBirthSignId = signId;

        const ESM::BirthSign* sign
            = MWBase::Environment::get().getESMStore()->get<ESM::BirthSign>().search(mBirthSignId);
        if (sign)
        {
            mBirthSignWidget->setCaption(sign->mName);
            ToolTips::createBirthsignToolTip(mBirthSignWidget, mBirthSignId);
        }

        mUpdateSkillArea = true;
    }

    void ReviewDialog::setHealth(const MWMechanics::DynamicStat<float>& value)
    {
        int current = std::max(0, static_cast<int>(value.getCurrent()));
        int modified = static_cast<int>(value.getModified());

        mHealth->setValue(current, modified);
        std::string valStr = MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
        mHealth->setUserString("Caption_HealthDescription", "#{sHealthDesc}\n" + valStr);
    }

    void ReviewDialog::setMagicka(const MWMechanics::DynamicStat<float>& value)
    {
        int current = std::max(0, static_cast<int>(value.getCurrent()));
        int modified = static_cast<int>(value.getModified());

        mMagicka->setValue(current, modified);
        std::string valStr = MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
        mMagicka->setUserString("Caption_HealthDescription", "#{sMagDesc}\n" + valStr);
    }

    void ReviewDialog::setFatigue(const MWMechanics::DynamicStat<float>& value)
    {
        int current = static_cast<int>(value.getCurrent());
        int modified = static_cast<int>(value.getModified());

        mFatigue->setValue(current, modified);
        std::string valStr = MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
        mFatigue->setUserString("Caption_HealthDescription", "#{sFatDesc}\n" + valStr);
    }

    void ReviewDialog::setAttribute(ESM::RefId attributeId, const MWMechanics::AttributeValue& value)
    {
        auto attr = mAttributeWidgets.find(attributeId);
        if (attr == mAttributeWidgets.end())
            return;

        if (attr->second->getAttributeValue() != value)
        {
            attr->second->setAttributeValue(value);
            mUpdateSkillArea = true;
        }
    }

    void ReviewDialog::setSkillValue(ESM::RefId id, const MWMechanics::SkillValue& value)
    {
        mSkillValues[id] = value;
        MyGUI::TextBox* widget = mSkillWidgetMap[id];
        if (widget)
        {
            float modified = value.getModified();
            float base = value.getBase();
            std::string text = MyGUI::utility::toString(std::floor(modified));
            std::string state = "normal";
            if (modified > base)
                state = "increased";
            else if (modified < base)
                state = "decreased";

            widget->setCaption(text);
            widget->_setWidgetState(state);
        }

        mUpdateSkillArea = true;
    }

    void ReviewDialog::configureSkills(const std::vector<ESM::RefId>& major, const std::vector<ESM::RefId>& minor)
    {
        mMajorSkills = major;
        mMinorSkills = minor;

        // Update misc skills with the remaining skills not in major or minor
        std::set<ESM::RefId> skillSet;
        std::copy(major.begin(), major.end(), std::inserter(skillSet, skillSet.begin()));
        std::copy(minor.begin(), minor.end(), std::inserter(skillSet, skillSet.begin()));
        mMiscSkills.clear();
        const auto& store = MWBase::Environment::get().getWorld()->getStore().get<ESM::Skill>();
        for (const ESM::Skill& skill : store)
        {
            if (!skillSet.contains(skill.mId))
                mMiscSkills.push_back(skill.mId);
        }

        mUpdateSkillArea = true;
    }

    void ReviewDialog::addSeparator(MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        MyGUI::ImageBox* separator = mSkillView->createWidget<MyGUI::ImageBox>(
            "MW_HLine", MyGUI::IntCoord(10, coord1.top, coord1.width + coord2.width - 4, 18), MyGUI::Align::Default);
        separator->eventMouseWheel += MyGUI::newDelegate(this, &ReviewDialog::onMouseWheel);

        mSkillWidgets.push_back(separator);

        coord1.top += separator->getHeight();
        coord2.top += separator->getHeight();
    }

    void ReviewDialog::addGroup(std::string_view label, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        MyGUI::TextBox* groupWidget = mSkillView->createWidget<MyGUI::TextBox>("SandBrightText",
            MyGUI::IntCoord(0, coord1.top, coord1.width + coord2.width, coord1.height), MyGUI::Align::Default);
        groupWidget->eventMouseWheel += MyGUI::newDelegate(this, &ReviewDialog::onMouseWheel);
        groupWidget->setCaption(MyGUI::UString(label));
        mSkillWidgets.push_back(groupWidget);

        const int lineHeight = Settings::gui().mFontSize + 2;
        coord1.top += lineHeight;
        coord2.top += lineHeight;
    }

    MyGUI::TextBox* ReviewDialog::addValueItem(std::string_view text, const std::string& value,
        const std::string& state, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        MyGUI::TextBox* skillNameWidget;
        MyGUI::TextBox* skillValueWidget;

        skillNameWidget = mSkillView->createWidget<MyGUI::TextBox>("SandText", coord1, MyGUI::Align::Default);
        skillNameWidget->setCaption(MyGUI::UString(text));
        skillNameWidget->eventMouseWheel += MyGUI::newDelegate(this, &ReviewDialog::onMouseWheel);

        skillValueWidget = mSkillView->createWidget<MyGUI::TextBox>("SandTextRight", coord2, MyGUI::Align::Default);
        skillValueWidget->setCaption(value);
        skillValueWidget->_setWidgetState(state);
        skillValueWidget->eventMouseWheel += MyGUI::newDelegate(this, &ReviewDialog::onMouseWheel);

        mSkillWidgets.push_back(skillNameWidget);
        mSkillWidgets.push_back(skillValueWidget);

        const int lineHeight = Settings::gui().mFontSize + 2;
        coord1.top += lineHeight;
        coord2.top += lineHeight;

        return skillValueWidget;
    }

    void ReviewDialog::addItem(const std::string& text, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        MyGUI::TextBox* skillNameWidget;

        skillNameWidget = mSkillView->createWidget<MyGUI::TextBox>(
            "SandText", coord1 + MyGUI::IntSize(coord2.width, 0), MyGUI::Align::Default);
        skillNameWidget->setCaption(text);
        skillNameWidget->eventMouseWheel += MyGUI::newDelegate(this, &ReviewDialog::onMouseWheel);

        mSkillWidgets.push_back(skillNameWidget);

        const int lineHeight = Settings::gui().mFontSize + 2;
        coord1.top += lineHeight;
        coord2.top += lineHeight;
    }

    void ReviewDialog::addItem(const ESM::Spell* spell, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        Widgets::MWSpellPtr widget = mSkillView->createWidget<Widgets::MWSpell>(
            "MW_StatName", coord1 + MyGUI::IntSize(coord2.width, 0), MyGUI::Align::Default);
        widget->setSpellId(spell->mId);
        widget->setUserString("ToolTipType", "Spell");
        widget->setUserString("Spell", spell->mId.serialize());
        widget->eventMouseWheel += MyGUI::newDelegate(this, &ReviewDialog::onMouseWheel);

        mSkillWidgets.push_back(widget);

        const int lineHeight = Settings::gui().mFontSize + 2;
        coord1.top += lineHeight;
        coord2.top += lineHeight;
    }

    void ReviewDialog::addSkills(const std::vector<ESM::RefId>& skills, const std::string& titleId,
        const std::string& titleDefault, MyGUI::IntCoord& coord1, MyGUI::IntCoord& coord2)
    {
        // Add a line separator if there are items above
        if (!mSkillWidgets.empty())
        {
            addSeparator(coord1, coord2);
        }

        addGroup(
            MWBase::Environment::get().getWindowManager()->getGameSettingString(titleId, titleDefault), coord1, coord2);

        for (const ESM::RefId& skillId : skills)
        {
            const ESM::Skill* skill = MWBase::Environment::get().getESMStore()->get<ESM::Skill>().search(skillId);
            if (!skill) // Skip unknown skills
                continue;

            auto skillValue = mSkillValues.find(skill->mId);
            if (skillValue == mSkillValues.end())
            {
                Log(Debug::Error) << "Failed to update stats review window: can not find value for skill "
                                  << skill->mId;
                continue;
            }

            const MWMechanics::SkillValue& stat = skillValue->second;
            float base = stat.getBase();
            float modified = stat.getModified();

            std::string state = "normal";
            if (modified > base)
                state = "increased";
            else if (modified < base)
                state = "decreased";
            MyGUI::TextBox* widget = addValueItem(
                skill->mName, MyGUI::utility::toString(static_cast<int>(modified)), state, coord1, coord2);

            for (int i = 0; i < 2; ++i)
            {
                ToolTips::createSkillToolTip(mSkillWidgets[mSkillWidgets.size() - 1 - i], skill->mId);
            }

            mSkillWidgetMap[skill->mId] = widget;
        }
    }

    std::string ReviewDialog::dynamicStatValue(Widgets::MWDynamicStatPtr stat) const
    {
        return MyGUI::utility::toString(stat->getValue()) + " / " + MyGUI::utility::toString(stat->getMax());
    }

    std::vector<A11y::SubItem> ReviewDialog::attributeItems() const
    {
        std::vector<A11y::SubItem> items;
        // Iterate in the game's canonical attribute order. Each item reads the
        // live modified value; its tooltip is the attribute name + description.
        const auto& store = MWBase::Environment::get().getWorld()->getStore().get<ESM::Attribute>();
        for (const ESM::Attribute& attribute : store)
        {
            auto it = mAttributeWidgets.find(attribute.mId);
            if (it == mAttributeWidgets.end())
                continue;
            const std::string name = attribute.mName;
            const std::string description = attribute.mDescription;
            const int value = it->second->getAttributeValue().getModified();
            A11y::SubItem item;
            item.label = name + " " + MyGUI::utility::toString(value);
            item.tooltips = [name, description, value] {
                std::string line = name + " " + MyGUI::utility::toString(value);
                if (!description.empty())
                    line += ". " + description;
                return std::vector<std::string>{ line };
            };
            items.push_back(std::move(item));
        }
        return items;
    }

    void ReviewDialog::appendSkillItems(
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

            const std::string name = skill->mName;
            const std::string description = skill->mDescription;
            const ESM::RefId governingId = ESM::Attribute::indexToRefId(skill->mData.mAttribute);

            A11y::SubItem item;
            item.label = name + " " + MyGUI::utility::toString(modified);
            item.section = section;
            item.tooltips = [name, description, governingId, modified] {
                MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
                std::string line = name + " " + MyGUI::utility::toString(modified);
                const ESM::Attribute* governing
                    = MWBase::Environment::get().getESMStore()->get<ESM::Attribute>().search(governingId);
                if (governing)
                    line += ". " + std::string(winMgr->getGameSettingString("sGoverningAttribute", "Governing Attribute"))
                        + ": " + governing->mName;
                if (!description.empty())
                    line += ". " + description;
                return std::vector<std::string>{ line };
            };
            out.push_back(std::move(item));
        }
    }

    std::vector<ESM::RefId> ReviewDialog::computeStartingSpells() const
    {
        std::vector<ESM::RefId> spells;
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();

        const ESM::Race* race = nullptr;
        if (!mRaceId.empty())
            race = store.get<ESM::Race>().find(mRaceId);

        std::map<ESM::RefId, MWMechanics::AttributeValue> attributes;
        for (const auto& [key, value] : mAttributeWidgets)
            attributes[key] = value->getAttributeValue();

        // Level-1 auto-calculated spells from skills / attributes / race.
        for (ESM::RefId& spellId : MWMechanics::autoCalcPlayerSpells(mSkillValues, attributes, race))
        {
            if (std::find(spells.begin(), spells.end(), spellId) == spells.end())
                spells.push_back(spellId);
        }

        // Racial powers.
        if (race)
        {
            for (const ESM::RefId& spellId : race->mPowers.mList)
            {
                if (std::find(spells.begin(), spells.end(), spellId) == spells.end())
                    spells.push_back(spellId);
            }
        }

        // Birthsign powers.
        if (!mBirthSignId.empty())
        {
            const ESM::BirthSign* sign = store.get<ESM::BirthSign>().find(mBirthSignId);
            for (const ESM::RefId& spellId : sign->mPowers.mList)
            {
                if (std::find(spells.begin(), spells.end(), spellId) == spells.end())
                    spells.push_back(spellId);
            }
        }

        return spells;
    }

    void ReviewDialog::appendSpellItems(std::vector<A11y::SubItem>& out, const std::vector<ESM::RefId>& spells,
        int spellType, const std::string& section) const
    {
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        // Abilities are constant effects (no duration / range); powers and
        // spells are timed.
        const bool isConstant = (spellType == ESM::Spell::ST_Ability);
        for (const ESM::RefId& spellId : spells)
        {
            const ESM::Spell* spell = store.get<ESM::Spell>().search(spellId);
            if (!spell || spell->mData.mType != spellType)
                continue;
            const ESM::RefId id = spellId;
            const std::string name = spell->mName;
            A11y::SubItem item;
            item.label = name;
            item.section = section;
            item.tooltips = [id, name, isConstant] {
                const ESM::Spell* sp = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(id);
                std::string line = name;
                if (sp)
                {
                    for (const ESM::IndexedENAMstruct& effect : sp->mEffects.mList)
                    {
                        std::string effLine = A11y::formatSpellEffectLine(effect, isConstant);
                        if (!effLine.empty())
                            line += ". " + effLine;
                    }
                }
                return std::vector<std::string>{ line };
            };
            out.push_back(std::move(item));
        }
    }

    std::vector<A11y::SubItem> ReviewDialog::skillItems() const
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        std::vector<A11y::SubItem> items;
        appendSkillItems(
            items, mMajorSkills, std::string(winMgr->getGameSettingString("sSkillClassMajor", "Major Skills")));
        appendSkillItems(
            items, mMinorSkills, std::string(winMgr->getGameSettingString("sSkillClassMinor", "Minor Skills")));
        appendSkillItems(
            items, mMiscSkills, std::string(winMgr->getGameSettingString("sSkillClassMisc", "Misc Skills")));

        // Auto-calculated starting abilities, powers and spells, mirroring the
        // visual scroll area below the skills.
        const std::vector<ESM::RefId> spells = computeStartingSpells();
        appendSpellItems(items, spells, ESM::Spell::ST_Ability,
            std::string(winMgr->getGameSettingString("sTypeAbility", "Abilities")));
        appendSpellItems(
            items, spells, ESM::Spell::ST_Power, std::string(winMgr->getGameSettingString("sTypePower", "Powers")));
        appendSpellItems(
            items, spells, ESM::Spell::ST_Spell, std::string(winMgr->getGameSettingString("sTypeSpell", "Spells")));

        return items;
    }

    void ReviewDialog::updateSkillArea()
    {
        for (MyGUI::Widget* skillWidget : mSkillWidgets)
        {
            MyGUI::Gui::getInstance().destroyWidget(skillWidget);
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

        // starting spells
        const std::vector<ESM::RefId> spells = computeStartingSpells();

        if (!mSkillWidgets.empty())
            addSeparator(coord1, coord2);
        addGroup(MWBase::Environment::get().getWindowManager()->getGameSettingString("sTypeAbility", "Abilities"),
            coord1, coord2);
        for (auto& spellId : spells)
        {
            const ESM::Spell* spell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().find(spellId);
            if (spell->mData.mType == ESM::Spell::ST_Ability)
                addItem(spell, coord1, coord2);
        }

        addSeparator(coord1, coord2);
        addGroup(MWBase::Environment::get().getWindowManager()->getGameSettingString("sTypePower", "Powers"), coord1,
            coord2);
        for (auto& spellId : spells)
        {
            const ESM::Spell* spell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().find(spellId);
            if (spell->mData.mType == ESM::Spell::ST_Power)
                addItem(spell, coord1, coord2);
        }

        addSeparator(coord1, coord2);
        addGroup(MWBase::Environment::get().getWindowManager()->getGameSettingString("sTypeSpell", "Spells"), coord1,
            coord2);
        for (auto& spellId : spells)
        {
            const ESM::Spell* spell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().find(spellId);
            if (spell->mData.mType == ESM::Spell::ST_Spell)
                addItem(spell, coord1, coord2);
        }

        // Canvas size must be expressed with VScroll disabled, otherwise MyGUI would expand the scroll area when the
        // scrollbar is hidden
        mSkillView->setVisibleVScroll(false);
        mSkillView->setCanvasSize(mSkillView->getWidth(), std::max(mSkillView->getHeight(), coord1.top));
        mSkillView->setVisibleVScroll(true);
    }

    // widget controls

    void ReviewDialog::onOkClicked(MyGUI::Widget* /*sender*/)
    {
        eventDone(this);
    }

    void ReviewDialog::onBackClicked(MyGUI::Widget* /*sender*/)
    {
        eventBack();
    }

    void ReviewDialog::onNameClicked(MyGUI::Widget* /*sender*/)
    {
        eventActivateDialog(NAME_DIALOG);
    }

    void ReviewDialog::onRaceClicked(MyGUI::Widget* /*sender*/)
    {
        eventActivateDialog(RACE_DIALOG);
    }

    void ReviewDialog::onClassClicked(MyGUI::Widget* /*sender*/)
    {
        eventActivateDialog(CLASS_DIALOG);
    }

    void ReviewDialog::onBirthSignClicked(MyGUI::Widget* /*sender*/)
    {
        eventActivateDialog(BIRTHSIGN_DIALOG);
    }

    void ReviewDialog::onMouseWheel(MyGUI::Widget* /*sender*/, int rel)
    {
        if (mSkillView->getViewOffset().top + rel * 0.3 > 0)
            mSkillView->setViewOffset(MyGUI::IntPoint(0, 0));
        else
            mSkillView->setViewOffset(
                MyGUI::IntPoint(0, static_cast<int>(mSkillView->getViewOffset().top + rel * 0.3)));
    }

    bool ReviewDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            switch (mControllerFocus)
            {
                case 0:
                    onNameClicked(mButtons[0]);
                    break;
                case 1:
                    onRaceClicked(mButtons[1]);
                    break;
                case 2:
                    onClassClicked(mButtons[2]);
                    break;
                case 3:
                    onBirthSignClicked(mButtons[3]);
                    break;
                case 4:
                    onBackClicked(mButtons[4]);
                    break;
                case 5:
                    onOkClicked(mButtons[5]);
                    break;
            }
            return true;
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            onBackClicked(mButtons[4]);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_X)
        {
            onOkClicked(mButtons[5]);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP || arg.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT)
        {
            setControllerFocus(mButtons, mControllerFocus, false);
            mControllerFocus = wrap(mControllerFocus, mButtons.size(), -1);
            setControllerFocus(mButtons, mControllerFocus, true);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN || arg.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
        {
            setControllerFocus(mButtons, mControllerFocus, false);
            mControllerFocus = wrap(mControllerFocus, mButtons.size(), 1);
            setControllerFocus(mButtons, mControllerFocus, true);
        }

        return true;
    }
}
