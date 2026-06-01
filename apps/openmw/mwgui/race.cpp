#include "race.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <MyGUI_Button.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_StringUtility.h>
#include <MyGUI_UString.h>

#include <osg/Texture2D>

#include <components/debug/debuglog.hpp>
#include <components/esm/defs.hpp>
#include <components/esm3/effectlist.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/myguiplatform/myguitexture.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwmechanics/magiceffects.hpp"
#include "../mwrender/characterpreview.hpp"
#include "../mwworld/esmstore.hpp"

#include "accessibility/speech.hpp"
#include "tooltips.hpp"

namespace
{
    bool sortRaces(const std::pair<ESM::RefId, std::string>& left, const std::pair<ESM::RefId, std::string>& right)
    {
        return left.second.compare(right.second) < 0;
    }

    // Build a human-readable description of a single spell effect,
    // including magnitude, duration, area and range -- mirroring the
    // text shown in the on-screen spell tooltip (see
    // MWSpellEffect::updateWidgets). Returned with #{...} L10n tags
    // intact; speakA11y resolves them.
    std::string formatSpellEffectLine(const ESM::IndexedENAMstruct& effect)
    {
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::MagicEffect* magicEffect = store.get<ESM::MagicEffect>().search(effect.mData.mEffectID);
        if (!magicEffect)
            return {};
        const ESM::Attribute* attribute = store.get<ESM::Attribute>().search(effect.mData.mAttribute);
        const ESM::Skill* skill = store.get<ESM::Skill>().search(effect.mData.mSkill);

        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();

        const std::string_view pt = wm->getGameSettingString("spoint", {});
        const std::string_view pts = wm->getGameSettingString("spoints", {});
        const std::string_view pct = wm->getGameSettingString("spercent", {});
        const std::string_view ft = wm->getGameSettingString("sfeet", {});
        const std::string_view lvl = wm->getGameSettingString("sLevel", {});
        const std::string_view lvls = wm->getGameSettingString("sLevels", {});
        const std::string to = " " + std::string{ wm->getGameSettingString("sTo", {}) } + " ";
        const std::string sec = " " + std::string{ wm->getGameSettingString("ssecond", {}) };
        const std::string secs = " " + std::string{ wm->getGameSettingString("sseconds", {}) };

        std::string line = MWMechanics::getMagicEffectString(*magicEffect, attribute, skill);

        const int magnMin = effect.mData.mMagnMin;
        const int magnMax = effect.mData.mMagnMax;
        if (magnMin || magnMax)
        {
            ESM::MagicEffect::MagnitudeDisplayType displayType = magicEffect->getMagnitudeDisplayType();
            if (displayType == ESM::MagicEffect::MDT_TimesInt)
            {
                std::string_view timesInt = wm->getGameSettingString("sXTimesINT", {});
                std::stringstream formatter;
                formatter << std::fixed << std::setprecision(1) << " " << (magnMin / 10.0f);
                if (magnMin != magnMax)
                    formatter << to << (magnMax / 10.0f);
                formatter << timesInt;
                line += formatter.str();
            }
            else if (displayType != ESM::MagicEffect::MDT_None)
            {
                line += " " + MyGUI::utility::toString(magnMin);
                if (magnMin != magnMax)
                    line += to + MyGUI::utility::toString(magnMax);

                if (displayType == ESM::MagicEffect::MDT_Percentage)
                    line += pct;
                else if (displayType == ESM::MagicEffect::MDT_Feet)
                    line += std::string(" ") + std::string(ft);
                else if (displayType == ESM::MagicEffect::MDT_Level)
                    line += std::string(" ") + std::string((magnMin == magnMax && std::abs(magnMin) == 1) ? lvl : lvls);
                else // MDT_Points
                    line += std::string(" ") + std::string((magnMin == magnMax && std::abs(magnMin) == 1) ? pt : pts);
            }
        }

        // Racial powers are abilities (constant) or powers (timed). Show
        // duration / area / range for non-constant effects.
        const bool isConstant = false;
        if (!isConstant)
        {
            int duration = effect.mData.mDuration;
            if (!(magicEffect->mData.mFlags & ESM::MagicEffect::AppliedOnce))
                duration = std::max(1, duration);
            if (duration > 0 && !(magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration))
            {
                line += ' ';
                line += wm->getGameSettingString("sfor", {});
                line += ' ' + MyGUI::utility::toString(duration) + ((duration == 1) ? sec : secs);
            }
            if (effect.mData.mArea > 0)
                line += " #{sin} " + MyGUI::utility::toString(effect.mData.mArea) + " #{sfootarea}";

            line += ' ';
            line += wm->getGameSettingString("sonword", {});
            line += ' ';
            if (effect.mData.mRange == ESM::RT_Self)
                line += wm->getGameSettingString("sRangeSelf", {});
            else if (effect.mData.mRange == ESM::RT_Touch)
                line += wm->getGameSettingString("sRangeTouch", {});
            else if (effect.mData.mRange == ESM::RT_Target)
                line += wm->getGameSettingString("sRangeTarget", {});
        }

        return line;
    }
}

namespace MWGui
{

    RaceDialog::RaceDialog(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : WindowModal("openmw_chargen_race.layout")
        , mParent(parent)
        , mResourceSystem(resourceSystem)
        , mGenderIndex(0)
        , mFaceIndex(0)
        , mHairIndex(0)
        , mCurrentAngle(0)
        , mPreviewDirty(true)
    {
        // Centre dialog
        center();

        setText("AppearanceT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu1", "Appearance"));
        getWidget(mPreviewImage, "PreviewImage");

        mPreviewImage->eventMouseWheel += MyGUI::newDelegate(this, &RaceDialog::onPreviewScroll);

        getWidget(mHeadRotate, "HeadRotate");

        mHeadRotate->setScrollRange(1000);
        mHeadRotate->setScrollPosition(500);
        mHeadRotate->setScrollViewPage(50);
        mHeadRotate->setScrollPage(50);
        mHeadRotate->setScrollWheelPage(50);
        mHeadRotate->eventScrollChangePosition += MyGUI::newDelegate(this, &RaceDialog::onHeadRotate);

        // Set up next/previous buttons
        MyGUI::Button *prevButton, *nextButton;

        setText("GenderChoiceT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu2", "Change Sex"));
        getWidget(prevButton, "PrevGenderButton");
        getWidget(nextButton, "NextGenderButton");
        prevButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousGender);
        nextButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextGender);

        setText("FaceChoiceT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu3", "Change Face"));
        getWidget(prevButton, "PrevFaceButton");
        getWidget(nextButton, "NextFaceButton");
        prevButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousFace);
        nextButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextFace);

        setText("HairChoiceT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu4", "Change Hair"));
        getWidget(prevButton, "PrevHairButton");
        getWidget(nextButton, "NextHairButton");
        prevButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousHair);
        nextButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextHair);

        setText("RaceT", MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu5", "Race"));
        getWidget(mRaceFocusProxy, "RaceT");
        getWidget(mRaceList, "RaceList");
        mRaceList->setScrollVisible(true);
        mRaceList->eventListSelectAccept += MyGUI::newDelegate(this, &RaceDialog::onAccept);
        mRaceList->eventListChangePosition += MyGUI::newDelegate(this, &RaceDialog::onSelectRace);

        setText("SkillsT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sBonusSkillTitle", "Skill Bonus"));
        getWidget(mSkillList, "SkillList");
        setText("SpellPowerT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu7", "Specials"));
        getWidget(mSpellPowerList, "SpellPowerList");

        getWidget(mBackButton, "BackButton");
        mBackButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onBackClicked);

        getWidget(mOkButton, "OKButton");
        mOkButton->setCaption(
            MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sOK", {})));
        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onOkClicked);

        if (Settings::gui().mControllerMenus)
        {
            mControllerButtons.mLStick = "#{Interface:Mouse}";
            mControllerButtons.mA = "#{Interface:Select}";
            mControllerButtons.mB = "#{Interface:Back}";
            mControllerButtons.mY = "#{Interface:Sex}";
            mControllerButtons.mL1 = "#{Interface:Hair}";
            mControllerButtons.mR1 = "#{Interface:Face}";
        }

        updateRaces();
        updateSkills();
        updateSpellPowers();

        setupAccessibility();
    }

    void RaceDialog::setupAccessibility()
    {
        // Register every navigable option with the shared A11y framework.
        // The race ListBox and the +/- selector rows are represented by
        // their header TextBox (a focus proxy) so the list / buttons don't
        // eat the arrow keys we use for navigation and value changes.
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        getWidget(mGenderChoice, "GenderChoiceT");
        getWidget(mFaceChoice, "FaceChoiceT");
        getWidget(mHairChoice, "HairChoiceT");

        mA11y.add({ .widget = mRaceFocusProxy,
            .label = std::string(winMgr->getGameSettingString("sRaceMenu5", "Race")),
            .value = [this] { return raceValue(); },
            .change = [this](bool next) { changeRace(next); },
            .tooltips = [this] { return raceTooltips(); } });
        mA11y.add({ .widget = mGenderChoice,
            .label = std::string(winMgr->getGameSettingString("sRaceMenu2", "Change Sex")),
            .value = [this] { return genderLabel(); },
            .change = [this](bool next) { if (next) onSelectNextGender(mGenderChoice); else onSelectPreviousGender(mGenderChoice); } });
        mA11y.add({ .widget = mFaceChoice,
            .label = std::string(winMgr->getGameSettingString("sRaceMenu3", "Change Face")),
            .value = [this] { return faceLabel(); },
            .change = [this](bool next) { if (next) onSelectNextFace(mFaceChoice); else onSelectPreviousFace(mFaceChoice); } });
        mA11y.add({ .widget = mHairChoice,
            .label = std::string(winMgr->getGameSettingString("sRaceMenu4", "Change Hair")),
            .value = [this] { return hairLabel(); },
            .change = [this](bool next) { if (next) onSelectNextHair(mHairChoice); else onSelectPreviousHair(mHairChoice); } });
        mA11y.add({ .widget = mHeadRotate,
            .label = "Head rotation",
            .value = [this] { return headRotateValue(); },
            .change = [this](bool next) { changeHeadRotate(next); } });
        mA11y.add({ .widget = mBackButton,
            .label = std::string(winMgr->getGameSettingString("sBack", "Back")),
            .tooltips = [] { return std::vector<std::string>{ "Return to the previous screen." }; },
            .activate = [this] { onBackClicked(mBackButton); } });
        mA11y.add({ .widget = mOkButton,
            .label = std::string(winMgr->getGameSettingString("sOK", "OK")),
            .tooltips = [] { return std::vector<std::string>{ "Confirm your selection and continue." }; },
            .activate = [this] { onOkClicked(mOkButton); } });
    }

    void RaceDialog::onFrame(float duration)
    {
        mA11y.onFrame(duration);
    }

    std::string RaceDialog::raceValue() const
    {
        const size_t idx = mRaceList->getIndexSelected();
        if (idx == MyGUI::ITEM_NONE)
            return {};
        std::string out = mRaceList->getItemNameAt(idx).asUTF8();

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::Race* race = store.get<ESM::Race>().search(mCurrentRaceId);
        if (!race)
            return out;

        // Skill bonuses (concise summary; full descriptions are in tooltips).
        std::string skills;
        for (const auto& bonus : race->mData.mBonus)
        {
            ESM::RefId skillId = ESM::Skill::indexToRefId(bonus.mSkill);
            if (skillId.empty())
                continue;
            const ESM::Skill* skill = store.get<ESM::Skill>().search(skillId);
            if (!skill)
                continue;
            if (!skills.empty())
                skills += ", ";
            skills += skill->mName + " " + std::to_string(bonus.mBonus);
        }
        if (!skills.empty())
            out += ". Skill bonuses: " + skills;

        // Specials (racial powers) summary.
        std::string powers;
        for (const ESM::RefId& spellId : race->mPowers.mList)
        {
            const ESM::Spell* spell = store.get<ESM::Spell>().search(spellId);
            if (!spell)
                continue;
            if (!powers.empty())
                powers += ", ";
            powers += spell->mName;
        }
        if (!powers.empty())
            out += ". Specials: " + powers;

        return out;
    }

    std::vector<std::string> RaceDialog::raceTooltips() const
    {
        std::vector<std::string> lines;
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::Race* race = store.get<ESM::Race>().search(mCurrentRaceId);
        if (!race)
            return lines;

        // Race name + flavour description.
        std::string intro = race->mName;
        if (!race->mDescription.empty())
            intro += ". " + race->mDescription;
        lines.push_back(intro);

        // One entry per skill bonus: name, bonus amount, and the skill's
        // own description.
        for (const auto& bonus : race->mData.mBonus)
        {
            ESM::RefId skillId = ESM::Skill::indexToRefId(bonus.mSkill);
            if (skillId.empty())
                continue;
            const ESM::Skill* skill = store.get<ESM::Skill>().search(skillId);
            if (!skill)
                continue;
            std::string line = skill->mName + " plus " + std::to_string(bonus.mBonus);
            if (!skill->mDescription.empty())
                line += ". " + skill->mDescription;
            lines.push_back(line);
        }

        // One entry per special (racial power) with its full effect
        // breakdown, matching the on-screen tooltip.
        for (const ESM::RefId& spellId : race->mPowers.mList)
        {
            const ESM::Spell* spell = store.get<ESM::Spell>().search(spellId);
            if (!spell)
                continue;
            std::string line = spell->mName;
            for (const ESM::IndexedENAMstruct& effect : spell->mEffects.mList)
            {
                std::string effLine = formatSpellEffectLine(effect);
                if (!effLine.empty())
                    line += ". " + effLine;
            }
            lines.push_back(line);
        }
        return lines;
    }

    void RaceDialog::changeRace(bool next)
    {
        const size_t count = mRaceList->getItemCount();
        if (count == 0)
            return;
        size_t cur = mRaceList->getIndexSelected();
        if (cur == MyGUI::ITEM_NONE)
            cur = 0;
        size_t nextIdx = next ? (cur + 1) % count : (cur + count - 1) % count;
        mRaceList->setIndexSelected(nextIdx);
        mRaceList->beginToItemAt(nextIdx); // keep selection visible
        // setIndexSelected doesn't fire eventListChangePosition.
        onSelectRace(mRaceList, nextIdx);
    }

    std::string RaceDialog::headRotateValue() const
    {
        // Report the angle in degrees, with 0 facing forward.
        const size_t range = mHeadRotate->getScrollRange();
        if (range < 2)
            return {};
        const float t = mHeadRotate->getScrollPosition() / float(range - 1);
        const int degrees = static_cast<int>(std::round((t - 0.5f) * 360.f));
        return std::to_string(degrees) + " degrees";
    }

    void RaceDialog::changeHeadRotate(bool next)
    {
        const size_t range = mHeadRotate->getScrollRange();
        if (range < 2)
            return;
        const size_t step = std::max<size_t>(1, range / 20);
        size_t pos = mHeadRotate->getScrollPosition();
        size_t newPos = next ? std::min<size_t>(range - 1, pos + step) : (pos > step ? pos - step : 0);
        if (newPos == pos)
            return;
        mHeadRotate->setScrollPosition(newPos);
        onHeadRotate(mHeadRotate, newPos);
    }

    std::string RaceDialog::genderLabel() const
    {
        return mGenderIndex == 0 ? "Male" : "Female";
    }

    std::string RaceDialog::faceLabel() const
    {
        if (mAvailableHeads.empty())
            return "None";
        return "Face " + std::to_string(mFaceIndex + 1) + " of "
            + std::to_string(mAvailableHeads.size());
    }

    std::string RaceDialog::hairLabel() const
    {
        if (mAvailableHairs.empty())
            return "None";
        return "Hair " + std::to_string(mHairIndex + 1) + " of "
            + std::to_string(mAvailableHairs.size());
    }

    void RaceDialog::setNextButtonShow(bool shown)
    {
        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");

        if (shown)
        {
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sNext", {})));
            mControllerButtons.mX = "#{Interface:Next}";
        }
        else if (Settings::gui().mControllerMenus)
        {
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sDone", {})));
            mControllerButtons.mX = "#{Interface:Done}";
        }
        else
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sOK", {})));
    }

    void RaceDialog::onOpen()
    {
        WindowModal::onOpen();

        updateRaces();
        updateSkills();
        updateSpellPowers();

        mPreviewImage->setRenderItemTexture(nullptr);

        mPreview.reset(nullptr);
        mPreviewTexture.reset(nullptr);

        mPreview = std::make_unique<MWRender::RaceSelectionPreview>(mParent, mResourceSystem);
        mPreview->rebuild();
        mPreview->setAngle(mCurrentAngle);

        mPreviewTexture
            = std::make_unique<MyGUIPlatform::OSGTexture>(mPreview->getTexture(), mPreview->getTextureStateSet());
        mPreviewImage->setRenderItemTexture(mPreviewTexture.get());
        // The widget is Y-down, the RTT image is Y-up, so this UV is inverted
        mPreviewImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 1.f, 1.f, 0.f));

        const ESM::NPC& proto = mPreview->getPrototype();
        setRaceId(proto.mRace);
        setGender(proto.isMale() ? GM_Male : GM_Female);
        recountParts();

        for (size_t i = 0; i < mAvailableHeads.size(); ++i)
        {
            if (mAvailableHeads[i] == proto.mHead)
                mFaceIndex = i;
        }

        for (size_t i = 0; i < mAvailableHairs.size(); ++i)
        {
            if (mAvailableHairs[i] == proto.mHair)
                mHairIndex = i;
        }

        mPreviewDirty = true;

        size_t initialPos = mHeadRotate->getScrollRange() / 2 + mHeadRotate->getScrollRange() / 10;
        mHeadRotate->setScrollPosition(initialPos);
        onHeadRotate(mHeadRotate, initialPos);

        // Hand input to the shared A11y controller: it disables engine
        // spatial navigation (which collides with our arrow-key scheme and
        // the race list's built-in arrow handling), focuses the first
        // option and announces it.
        mA11y.activate(mRaceFocusProxy);
    }

    void RaceDialog::setRaceId(const ESM::RefId& raceId)
    {
        mCurrentRaceId = raceId;
        mRaceList->setIndexSelected(MyGUI::ITEM_NONE);
        size_t count = mRaceList->getItemCount();
        for (size_t i = 0; i < count; ++i)
        {
            if (*mRaceList->getItemDataAt<ESM::RefId>(i) == raceId)
            {
                mRaceList->setIndexSelected(i);
                break;
            }
        }

        updateSkills();
        updateSpellPowers();
    }

    void RaceDialog::onClose()
    {
        WindowModal::onClose();

        // Relinquish input and restore default keyboard navigation.
        mA11y.deactivate();

        mPreviewImage->setRenderItemTexture(nullptr);

        mPreviewTexture.reset(nullptr);
        mPreview.reset(nullptr);
    }

    // widget controls

    void RaceDialog::onOkClicked(MyGUI::Widget* /*sender*/)
    {
        if (mRaceList->getIndexSelected() == MyGUI::ITEM_NONE)
            return;
        eventDone(this);
    }

    void RaceDialog::onBackClicked(MyGUI::Widget* /*sender*/)
    {
        eventBack();
    }

    void RaceDialog::onPreviewScroll(MyGUI::Widget*, int delta)
    {
        size_t oldPos = mHeadRotate->getScrollPosition();
        size_t maxPos = mHeadRotate->getScrollRange() - 1;
        size_t scrollPage = mHeadRotate->getScrollWheelPage();
        if (delta < 0)
            mHeadRotate->setScrollPosition(oldPos + std::min(maxPos - oldPos, scrollPage));
        else
            mHeadRotate->setScrollPosition(oldPos - std::min(oldPos, scrollPage));

        onHeadRotate(mHeadRotate, mHeadRotate->getScrollPosition());
    }

    void RaceDialog::onHeadRotate(MyGUI::ScrollBar* scroll, size_t position)
    {
        float angle = (float(position) / (scroll->getScrollRange() - 1) - 0.5f) * osg::PIf * 2;
        mPreview->setAngle(angle);

        mCurrentAngle = angle;
    }

    void RaceDialog::onSelectPreviousGender(MyGUI::Widget*)
    {
        mGenderIndex = wrap(mGenderIndex, 2, -1);

        recountParts();
        updatePreview();
    }

    void RaceDialog::onSelectNextGender(MyGUI::Widget*)
    {
        mGenderIndex = wrap(mGenderIndex, 2, 1);

        recountParts();
        updatePreview();
    }

    void RaceDialog::onSelectPreviousFace(MyGUI::Widget*)
    {
        mFaceIndex = wrap(mFaceIndex, mAvailableHeads.size(), -1);
        updatePreview();
    }

    void RaceDialog::onSelectNextFace(MyGUI::Widget*)
    {
        mFaceIndex = wrap(mFaceIndex, mAvailableHeads.size(), 1);
        updatePreview();
    }

    void RaceDialog::onSelectPreviousHair(MyGUI::Widget*)
    {
        mHairIndex = wrap(mHairIndex, mAvailableHairs.size(), -1);
        updatePreview();
    }

    void RaceDialog::onSelectNextHair(MyGUI::Widget*)
    {
        mHairIndex = wrap(mHairIndex, mAvailableHairs.size(), 1);
        updatePreview();
    }

    void RaceDialog::onSelectRace(MyGUI::ListBox* sender, size_t index)
    {
        if (index == MyGUI::ITEM_NONE)
            return;

        ESM::RefId& raceId = *mRaceList->getItemDataAt<ESM::RefId>(index);
        if (mCurrentRaceId == raceId)
            return;

        mCurrentRaceId = raceId;

        recountParts();

        updatePreview();
        updateSkills();
        updateSpellPowers();
        // Note: the spoken announcement of the new race is driven by the
        // A11y framework (Screen::changeValue speaks raceValue() after the
        // change), so we deliberately don't announce here to avoid speaking
        // the race twice on keyboard navigation.
    }

    void RaceDialog::onAccept(MyGUI::ListBox* sender, size_t index)
    {
        onSelectRace(sender, index);
        if (mRaceList->getIndexSelected() == MyGUI::ITEM_NONE)
            return;
        eventDone(this);
    }

    void RaceDialog::getBodyParts(int part, std::vector<ESM::RefId>& out)
    {
        out.clear();
        const MWWorld::Store<ESM::BodyPart>& store = MWBase::Environment::get().getESMStore()->get<ESM::BodyPart>();

        for (const ESM::BodyPart& bodypart : store)
        {
            if (bodypart.mData.mFlags & ESM::BodyPart::BPF_NotPlayable)
                continue;
            if (bodypart.mData.mType != ESM::BodyPart::MT_Skin)
                continue;
            if (bodypart.mData.mPart != static_cast<ESM::BodyPart::MeshPart>(part))
                continue;
            if (mGenderIndex != (bodypart.mData.mFlags & ESM::BodyPart::BPF_Female))
                continue;
            if (ESM::isFirstPersonBodyPart(bodypart))
                continue;
            if (bodypart.mRace == mCurrentRaceId)
                out.push_back(bodypart.mId);
        }
    }

    void RaceDialog::recountParts()
    {
        getBodyParts(ESM::BodyPart::MP_Hair, mAvailableHairs);
        getBodyParts(ESM::BodyPart::MP_Head, mAvailableHeads);

        mFaceIndex = 0;
        mHairIndex = 0;
    }

    // update widget content

    void RaceDialog::updatePreview()
    {
        ESM::NPC record = mPreview->getPrototype();
        record.mRace = mCurrentRaceId;
        record.setIsMale(mGenderIndex == 0);

        if (mFaceIndex < mAvailableHeads.size())
            record.mHead = mAvailableHeads[mFaceIndex];

        if (mHairIndex < mAvailableHairs.size())
            record.mHair = mAvailableHairs[mHairIndex];

        try
        {
            mPreview->setPrototype(record);
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Error creating preview: " << e.what();
        }
    }

    void RaceDialog::updateRaces()
    {
        mRaceList->removeAllItems();

        const MWWorld::Store<ESM::Race>& races = MWBase::Environment::get().getESMStore()->get<ESM::Race>();

        std::vector<std::pair<ESM::RefId, std::string>> items; // ID, name
        for (const ESM::Race& race : races)
        {
            bool playable = race.mData.mFlags & ESM::Race::Playable;
            if (!playable) // Only display playable races
                continue;

            items.emplace_back(race.mId, race.mName);
        }
        std::sort(items.begin(), items.end(), sortRaces);

        int index = 0;
        for (auto& item : items)
        {
            mRaceList->addItem(item.second, item.first);
            if (item.first == mCurrentRaceId)
                mRaceList->setIndexSelected(index);
            ++index;
        }
    }

    void RaceDialog::updateSkills()
    {
        for (MyGUI::Widget* widget : mSkillItems)
        {
            MyGUI::Gui::getInstance().destroyWidget(widget);
        }
        mSkillItems.clear();

        if (mCurrentRaceId.empty())
            return;

        Widgets::MWSkillPtr skillWidget;
        const int lineHeight = Settings::gui().mFontSize + 2;
        MyGUI::IntCoord coord1(0, 0, mSkillList->getWidth(), 18);

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::Race* race = store.get<ESM::Race>().find(mCurrentRaceId);
        for (const auto& bonus : race->mData.mBonus)
        {
            ESM::RefId skill = ESM::Skill::indexToRefId(bonus.mSkill);
            if (skill.empty()) // Skip unknown skill indexes
                continue;

            skillWidget = mSkillList->createWidget<Widgets::MWSkill>("MW_StatNameValue", coord1, MyGUI::Align::Default);
            skillWidget->setSkillId(skill);
            skillWidget->setSkillValue(Widgets::MWSkill::SkillValue(static_cast<float>(bonus.mBonus), 0.f));
            ToolTips::createSkillToolTip(skillWidget, skill);

            mSkillItems.push_back(skillWidget);

            coord1.top += lineHeight;
        }
    }

    void RaceDialog::updateSpellPowers()
    {
        for (MyGUI::Widget* widget : mSpellPowerItems)
        {
            MyGUI::Gui::getInstance().destroyWidget(widget);
        }
        mSpellPowerItems.clear();

        if (mCurrentRaceId.empty())
            return;

        const int lineHeight = Settings::gui().mFontSize + 2;
        MyGUI::IntCoord coord(0, 0, mSpellPowerList->getWidth(), lineHeight);

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::Race* race = store.get<ESM::Race>().find(mCurrentRaceId);

        int i = 0;
        for (const ESM::RefId& spellpower : race->mPowers.mList)
        {
            Widgets::MWSpellPtr spellPowerWidget = mSpellPowerList->createWidget<Widgets::MWSpell>(
                "MW_StatName", coord, MyGUI::Align::Default, std::string("SpellPower") + MyGUI::utility::toString(i));
            spellPowerWidget->setSpellId(spellpower);
            spellPowerWidget->setUserString("ToolTipType", "Spell");
            spellPowerWidget->setUserString("Spell", spellpower.serialize());

            mSpellPowerItems.push_back(spellPowerWidget);

            coord.top += lineHeight;
            ++i;
        }
    }

    bool RaceDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            onBackClicked(mBackButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_X)
        {
            onOkClicked(mOkButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_Y)
        {
            onSelectNextGender(nullptr);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        {
            onSelectNextHair(nullptr);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        {
            onSelectNextFace(nullptr);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
        {
            MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
            winMgr->setKeyFocusWidget(mRaceList);
            winMgr->injectKeyPress(MyGUI::KeyCode::ArrowUp, 0, false);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
        {
            MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
            winMgr->setKeyFocusWidget(mRaceList);
            winMgr->injectKeyPress(MyGUI::KeyCode::ArrowDown, 0, false);
        }

        return true;
    }

    bool RaceDialog::onControllerThumbstickEvent(const SDL_ControllerAxisEvent& arg)
    {
        if (arg.axis == SDL_CONTROLLER_AXIS_RIGHTX)
        {
            onPreviewScroll(nullptr, arg.value < 0 ? 1 : -1);
            return true;
        }

        return false;
    }

    const ESM::NPC& RaceDialog::getResult() const
    {
        return mPreview->getPrototype();
    }
}
