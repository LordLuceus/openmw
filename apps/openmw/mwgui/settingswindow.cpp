#include "settingswindow.hpp"

#include <array>
#include <cstdio>

#include <unicode/locid.h>

#include <MyGUI_ComboBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_UString.h>
#include <MyGUI_Window.h>

#include <SDL_video.h>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_TextBox.h>

#include <components/accessibility/accessibilitymanager.hpp>
#include <components/debug/debuglog.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/l10n/manager.hpp>
#include <components/lua_ui/scriptsettings.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/display.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>
#include <components/widgets/sharedstatebutton.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwlua/luamanagerimp.hpp"

#include "confirmationdialog.hpp"
#include "weightedsearch.hpp"

namespace
{
    std::string_view textureFilteringToStr(const std::string& mipFilter, const std::string& magFilter)
    {
        if (mipFilter == "none")
            return "#{OMWEngine:TextureFilteringDisabled}";

        if (magFilter == "linear")
        {
            if (mipFilter == "linear")
                return "#{OMWEngine:TextureFilteringTrilinear}";
            if (mipFilter == "nearest")
                return "#{OMWEngine:TextureFilteringBilinear}";
        }
        else if (magFilter == "nearest")
            return "#{OMWEngine:TextureFilteringNearest}";

        Log(Debug::Warning) << "Warning: Invalid texture filtering options: " << mipFilter << ", " << magFilter;
        return "#{OMWEngine:TextureFilteringOther}";
    }

    MyGUI::UString lightingMethodToStr(SceneUtil::LightingMethod method)
    {
        std::string_view result;
        switch (method)
        {
            case SceneUtil::LightingMethod::PerObjectUniform:
                result = "#{OMWEngine:LightingMethodShadersCompatibility}";
                break;
            case SceneUtil::LightingMethod::SingleUBO:
            default:
                result = "#{OMWEngine:LightingMethodShaders}";
                break;
        }

        return MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(result));
    }

    bool sortResolutions(std::pair<int, int> left, std::pair<int, int> right)
    {
        if (left.first == right.first)
            return left.second > right.second;
        return left.first > right.first;
    }

    const std::string_view checkButtonType = "CheckButton";
    const std::string_view sliderType = "Slider";

    std::string_view getSettingType(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingType");
    }

    std::string_view getSettingName(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingName");
    }

    std::string_view getSettingCategory(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingCategory");
    }

    std::string_view getSettingValueType(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingValueType");
    }

    void getSettingMinMax(MyGUI::Widget* widget, float& min, float& max)
    {
        const char* settingMin = "SettingMin";
        const char* settingMax = "SettingMax";
        min = 0.f;
        max = 1.f;
        if (!widget->getUserString(settingMin).empty())
            min = MyGUI::utility::parseFloat(widget->getUserString(settingMin));
        if (!widget->getUserString(settingMax).empty())
            max = MyGUI::utility::parseFloat(widget->getUserString(settingMax));
    }

    void updateMaxLightsComboBox(MyGUI::ComboBox* box)
    {
        constexpr int min = 8;
        constexpr int max = 64;
        constexpr int increment = 8;
        const int maxLights = Settings::shaders().mMaxLights;
        // show increments of 8 in dropdown
        if (maxLights >= min && maxLights <= max && !(maxLights % increment))
            box->setIndexSelected((maxLights / increment) - 1);
        else
            box->setIndexSelected(MyGUI::ITEM_NONE);
    }

    // Render a slider's current value as it would be displayed in its
    // formatted label, for screen-reader announcement. Mirrors the
    // value math in SettingsWindow::onSliderChangePosition.
    std::string formatSliderValueForA11y(MyGUI::ScrollBar* scroll)
    {
        if (!scroll)
            return {};
        std::string_view valueType = getSettingValueType(scroll);
        char buf[32]{};
        if (valueType == "Float" || valueType == "Cell")
        {
            float min, max;
            getSettingMinMax(scroll, min, max);
            const size_t range = scroll->getScrollRange();
            float value = range > 1 ? scroll->getScrollPosition() / float(range - 1) : 0.f;
            value = min + (max - min) * value;
            if (valueType == "Cell")
                std::snprintf(buf, sizeof(buf), "%.2f cells", value / Constants::CellSizeInUnits);
            else
                std::snprintf(buf, sizeof(buf), "%.2f", value);
            return buf;
        }
        if (valueType == "Integer")
        {
            float min, max;
            getSettingMinMax(scroll, min, max);
            const size_t range = scroll->getScrollRange();
            float value = range > 1 ? scroll->getScrollPosition() / float(range - 1) : 0.f;
            int intVal = static_cast<int>(min + (max - min) * value);
            std::snprintf(buf, sizeof(buf), "%d", intVal);
            return buf;
        }
        // Default: the scroll position itself is the integer value
        // (e.g. difficulty in [-100..100] mapped 0..200).
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(scroll->getScrollPosition()));
        return buf;
    }

    // Push a string to the screen reader, resolving any MyGUI #{...}
    // L10n tags first and queuing rather than interrupting so successive
    // focus / value events don't clobber each other.
    void speakA11y(const std::string& text)
    {
        if (text.empty())
            return;
        auto resolved
            = MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(text));
        std::string utf8 = resolved.asUTF8();
        if (utf8.empty())
            return;
        Accessibility::AccessibilityManager::instance().speak(utf8, /*interrupt=*/false);
    }

    void updateSliderLabel(MyGUI::ScrollBar* scroller, MyGUI::TextBox* textBox,
        const std::vector<icu::UnicodeString>& argNames, const std::vector<icu::Formattable>& args)
    {
        if (textBox != nullptr)
        {
            auto l10n = MWBase::Environment::get().getL10nManager()->getContext("OMWEngine");
            std::string labelCaption
                = l10n->formatMessage(scroller->getUserString("SettingLabelCaption"), argNames, args);
            textBox->setCaption(labelCaption);
        }
    }
}

namespace MWGui
{
    void SettingsWindow::configureWidgets(MyGUI::Widget* widget, bool init)
    {
        MyGUI::EnumeratorWidgetPtr widgets = widget->getEnumerator();
        while (widgets.next())
        {
            MyGUI::Widget* current = widgets.current();

            std::string_view type = getSettingType(current);
            if (type == checkButtonType)
            {
                std::string_view initialValue
                    = Settings::get<bool>(getSettingCategory(current), getSettingName(current)) ? "#{Interface:On}"
                                                                                                : "#{Interface:Off}";
                current->castType<MyGUI::Button>()->setCaptionWithReplacing(initialValue);
                if (init)
                    current->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onButtonToggled);
            }
            if (type == sliderType)
            {
                MyGUI::ScrollBar* scroll = current->castType<MyGUI::ScrollBar>();
                std::string_view valueType = getSettingValueType(current);
                std::vector<icu::UnicodeString> argNames;
                std::vector<icu::Formattable> args;
                if (valueType == "Float" || valueType == "Integer" || valueType == "Cell")
                {
                    // TODO: ScrollBar isn't meant for this. should probably use a dedicated FloatSlider widget
                    float min, max;
                    getSettingMinMax(scroll, min, max);
                    float value;

                    if (valueType == "Cell")
                    {
                        value = Settings::get<float>(getSettingCategory(current), getSettingName(current));
                        argNames.emplace_back("cells");
                        args.emplace_back(value / Constants::CellSizeInUnits);
                    }
                    else if (valueType == "Float")
                    {
                        value = Settings::get<float>(getSettingCategory(current), getSettingName(current));
                        argNames.emplace_back("value");
                        args.emplace_back(value);
                    }
                    else
                    {
                        const int intValue = Settings::get<int>(getSettingCategory(current), getSettingName(current));
                        argNames.emplace_back("value");
                        args.emplace_back(intValue);
                        value = static_cast<float>(intValue);
                    }

                    value = std::clamp(value, min, max);
                    value = (value - min) / (max - min);

                    scroll->setScrollPosition(static_cast<size_t>(value * (scroll->getScrollRange() - 1)));
                }
                else
                {
                    const int value = Settings::get<int>(getSettingCategory(current), getSettingName(current));
                    argNames.emplace_back("value");
                    args.emplace_back(value);
                    scroll->setScrollPosition(value);
                }
                if (init)
                    scroll->eventScrollChangePosition
                        += MyGUI::newDelegate(this, &SettingsWindow::onSliderChangePosition);
                if (scroll->getVisible())
                    updateSliderLabel(scroll, getSliderLabel(scroll), argNames, args);
            }

            configureWidgets(current, init);
        }
    }

    void SettingsWindow::onFrame(float duration)
    {
        if (mScriptView->getVisible())
        {
            const auto scriptsSize = mScriptAdapter->getSize();
            if (mScriptView->getCanvasSize() != scriptsSize)
                mScriptView->setCanvasSize(scriptsSize);
        }
    }

    MyGUI::TextBox* SettingsWindow::getSliderLabel(MyGUI::ScrollBar* scroller) const
    {
        auto labelWidgetName = scroller->getUserString("SettingLabelWidget");
        if (!labelWidgetName.empty())
        {
            MyGUI::TextBox* textBox;
            getWidget(textBox, labelWidgetName);
            return textBox;
        }
        return nullptr;
    }

    std::string SettingsWindow::resolveAccessibilityLabel(MyGUI::Widget* widget) const
    {
        if (!widget)
            return {};

        // Sliders: their captioned counterpart lives in the
        // SettingLabelWidget UserString. When set, prefer the
        // formatted label text (e.g. "Difficulty: 0").
        if (auto* scroll = widget->castType<MyGUI::ScrollBar>(false))
        {
            if (auto* lbl = getSliderLabel(scroll))
            {
                std::string cap = lbl->getCaption();
                if (!cap.empty())
                    return cap;
            }
        }

        // Checkbox toggles have their own caption set to the
        // On / Off state, so the descriptive label is a sibling
        // TextBox in the same parent layout. Sliders/combos usually
        // sit *after* their heading; checkboxes usually sit *before*
        // their text. Search both directions and return the first
        // non-empty, non-state sibling caption.
        const std::string onText = MWBase::Environment::get()
                                       .getL10nManager()
                                       ->getMessage("Interface", "On");
        const std::string offText = MWBase::Environment::get()
                                        .getL10nManager()
                                        ->getMessage("Interface", "Off");
        auto siblingCaption = [&](MyGUI::Widget* w) -> std::string {
            auto* tb = w->castType<MyGUI::TextBox>(false);
            if (!tb)
                return {};
            std::string cap = tb->getCaption();
            if (cap.empty() || cap == onText || cap == offText)
                return {};
            return cap;
        };

        if (auto* parent = widget->getParent())
        {
            size_t count = parent->getChildCount();
            size_t selfIndex = count;
            for (size_t i = 0; i < count; ++i)
            {
                if (parent->getChildAt(i) == widget)
                {
                    selfIndex = i;
                    break;
                }
            }
            // Walk backwards first (sliders, combos): the heading
            // TextBox is usually the closest preceding sibling.
            for (size_t i = selfIndex; i > 0; --i)
            {
                std::string cap = siblingCaption(parent->getChildAt(i - 1));
                if (!cap.empty())
                    return cap;
            }
            // Then forwards (checkbox toggles inside an HBox).
            for (size_t i = selfIndex + 1; i < count; ++i)
            {
                std::string cap = siblingCaption(parent->getChildAt(i));
                if (!cap.empty())
                    return cap;
            }
        }

        // Fallback: the widget's own caption (works for plain buttons
        // like OK / Reset / Keyboard / Controller, plus tab items).
        if (auto* tb = widget->castType<MyGUI::TextBox>(false))
            return tb->getCaption();
        return {};
    }

    void SettingsWindow::onWidgetKeyFocus(MyGUI::Widget* sender, MyGUI::Widget* /*oldFocus*/)
    {
        if (!sender || !mMainWidget->getVisible())
            return;

        std::string label = resolveAccessibilityLabel(sender);
        speakA11y(label);

        // For a CheckButton, also speak the current On/Off state so the
        // user knows whether toggling will turn it on or off.
        if (getSettingType(sender) == checkButtonType)
        {
            if (auto* btn = sender->castType<MyGUI::Button>(false))
                speakA11y(btn->getCaption());
        }

        // For a slider, announce the current value. Sliders with a
        // SettingLabelWidget already bake the value into their label
        // text (e.g. "Difficulty: 0") which was just spoken; for the
        // rest (master volume, tooltip delay, FOV...) speak it
        // explicitly so the user knows what they're starting from.
        if (getSettingType(sender) == sliderType)
        {
            auto* scroll = sender->castType<MyGUI::ScrollBar>(false);
            if (scroll && getSliderLabel(scroll) == nullptr)
                speakA11y(formatSliderValueForA11y(scroll));
        }
    }

    void SettingsWindow::onComboValueAnnounce(MyGUI::ComboBox* sender, size_t pos)
    {
        if (!sender || !mMainWidget->getVisible())
            return;
        if (pos == MyGUI::ITEM_NONE || pos >= sender->getItemCount())
            return;
        speakA11y(sender->getItemNameAt(pos));
    }

    void SettingsWindow::onListValueAnnounce(MyGUI::ListBox* sender, size_t pos)
    {
        if (!sender || !mMainWidget->getVisible())
            return;
        if (pos == MyGUI::ITEM_NONE || pos >= sender->getItemCount())
            return;
        speakA11y(sender->getItemNameAt(pos));
    }

    void SettingsWindow::onTabAnnounce(MyGUI::TabControl* sender, size_t index)
    {
        if (!sender || !mMainWidget->getVisible())
            return;
        if (index >= sender->getItemCount())
            return;
        speakA11y(sender->getItemNameAt(index));
    }

    void SettingsWindow::hookAccessibilityEvents(MyGUI::Widget* root)
    {
        if (!root)
            return;
        MyGUI::EnumeratorWidgetPtr it = root->getEnumerator();
        while (it.next())
        {
            MyGUI::Widget* current = it.current();

            // Sliders (ScrollBar with SettingType=Slider) default to
            // not-focusable in the layout, so they're skipped by Tab
            // navigation. Force them focusable here and intercept
            // arrow / Home / End / PgUp / PgDn to step the value.
            if (getSettingType(current) == sliderType)
            {
                current->setNeedKeyFocus(true);
                current->eventKeyButtonPressed
                    += MyGUI::newDelegate(this, &SettingsWindow::onSliderKeyPressed);
            }

            if (current->getNeedKeyFocus())
            {
                current->eventKeySetFocus
                    += MyGUI::newDelegate(this, &SettingsWindow::onWidgetKeyFocus);
                // Catch tab-navigation shortcuts (Ctrl+Tab,
                // Ctrl+PgUp/PgDn) regardless of which widget is
                // currently focused.
                current->eventKeyButtonPressed
                    += MyGUI::newDelegate(this, &SettingsWindow::onAccessibilityKeyPressed);
            }

            // Combo boxes: announce the newly-selected item.
            if (auto* combo = current->castType<MyGUI::ComboBox>(false))
                combo->eventComboChangePosition
                    += MyGUI::newDelegate(this, &SettingsWindow::onComboValueAnnounce);
            // List boxes: same.
            else if (auto* list = current->castType<MyGUI::ListBox>(false))
                list->eventListChangePosition
                    += MyGUI::newDelegate(this, &SettingsWindow::onListValueAnnounce);

            hookAccessibilityEvents(current);
        }
    }

    void SettingsWindow::onAccessibilityKeyPressed(
        MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char /*ch*/)
    {
        if (!mMainWidget->getVisible() || !mSettingsTab)
            return;

        // Skip if the user is typing in an edit box.
        if (sender && sender->castType<MyGUI::EditBox>(false) != nullptr)
            return;

        const bool ctrl = MyGUI::InputManager::getInstance().isControlPressed();

        // Ctrl+Left / Ctrl+Right cycles between settings tabs (Audio,
        // Video, Controls, Prefs, ...). Plain Left/Right is reserved
        // for adjusting slider values.
        if (ctrl
            && (key == MyGUI::KeyCode::ArrowLeft || key == MyGUI::KeyCode::ArrowRight))
        {
            const size_t count = mSettingsTab->getItemCount();
            if (count == 0)
                return;
            int delta = key == MyGUI::KeyCode::ArrowRight ? 1 : -1;
            size_t current = mSettingsTab->getIndexSelected();
            size_t next = (current + count + delta) % count;
            if (next == current)
                return;
            mSettingsTab->setIndexSelected(next);
            // setIndexSelected doesn't fire eventTabChangeSelect, so
            // run our hooks manually -- announcement + onTabChanged
            // plumbing.
            onTabChanged(mSettingsTab, next);
            onTabAnnounce(mSettingsTab, next);
            return;
        }

        // Up / Down moves focus between options on the current tab.
        // OpenMW's built-in KeyboardNavigation only does spatial
        // arrow-nav between buttons, so for sliders / combos we have
        // to translate to Tab / Shift+Tab which it handles for all
        // focusable widgets.
        if (key == MyGUI::KeyCode::ArrowDown)
        {
            MWBase::Environment::get().getWindowManager()->injectKeyPress(
                MyGUI::KeyCode::Tab, 0, false);
        }
        else if (key == MyGUI::KeyCode::ArrowUp)
        {
            MyGUI::InputManager::getInstance().injectKeyPress(
                MyGUI::KeyCode::LeftShift, 0);
            MWBase::Environment::get().getWindowManager()->injectKeyPress(
                MyGUI::KeyCode::Tab, 0, false);
            MyGUI::InputManager::getInstance().injectKeyRelease(
                MyGUI::KeyCode::LeftShift);
        }
    }

    void SettingsWindow::onSliderKeyPressed(
        MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char /*ch*/)
    {
        auto* scroll = sender->castType<MyGUI::ScrollBar>(false);
        if (!scroll)
            return;
        // ScrollRange is exclusive upper bound. Compute step sizes from
        // the bar's configured Page so coarse / fine controls roughly
        // mirror how a mouse drag feels.
        const size_t range = scroll->getScrollRange();
        if (range == 0)
            return;
        const size_t maxIndex = range - 1;
        const size_t page = std::max<size_t>(1, scroll->getScrollPage());
        const size_t fine = std::max<size_t>(1, page / 10);

        size_t pos = scroll->getScrollPosition();
        size_t newPos = pos;
        if (key == MyGUI::KeyCode::ArrowLeft)
            newPos = pos > fine ? pos - fine : 0;
        else if (key == MyGUI::KeyCode::ArrowRight)
            newPos = std::min(maxIndex, pos + fine);
        else if (key == MyGUI::KeyCode::PageDown)
            newPos = pos > page ? pos - page : 0;
        else if (key == MyGUI::KeyCode::PageUp)
            newPos = std::min(maxIndex, pos + page);
        else if (key == MyGUI::KeyCode::Home)
            newPos = 0;
        else if (key == MyGUI::KeyCode::End)
            newPos = maxIndex;
        else
            return;
        if (newPos == pos)
            return;
        scroll->setScrollPosition(newPos);
        // setScrollPosition does NOT fire eventScrollChangePosition, so
        // run the existing handler manually so settings are persisted
        // and the value is spoken.
        onSliderChangePosition(scroll, newPos);
    }

    SettingsWindow::SettingsWindow(Files::ConfigurationManager& cfgMgr)
        : WindowBase("openmw_settings_window.layout")
        , mKeyboardMode(true)
        , mCurrentPage(static_cast<size_t>(-1))
        , mCfgMgr(cfgMgr)
    {
        const bool terrain = Settings::terrain().mDistantTerrain;
        const std::string_view widgetName = terrain ? "RenderingDistanceSlider" : "LargeRenderingDistanceSlider";
        MyGUI::Widget* unusedSlider;
        getWidget(unusedSlider, widgetName);
        unusedSlider->setVisible(false);

        configureWidgets(mMainWidget, true);

        setTitle("#{OMWEngine:SettingsWindow}");

        getWidget(mSettingsTab, "SettingsTab");
        getWidget(mOkButton, "OkButton");
        getWidget(mResolutionList, "ResolutionList");
        getWidget(mWindowModeList, "WindowModeList");
        getWidget(mVSyncModeList, "VSyncModeList");
        getWidget(mWindowBorderButton, "WindowBorderButton");
        getWidget(mTextureFilteringButton, "TextureFilteringButton");
        getWidget(mControlsBox, "ControlsBox");
        getWidget(mResetControlsButton, "ResetControlsButton");
        getWidget(mKeyboardSwitch, "KeyboardButton");
        getWidget(mControllerSwitch, "ControllerButton");
        getWidget(mWaterRefractionButton, "WaterRefractionButton");
        getWidget(mSunlightScatteringButton, "SunlightScatteringButton");
        getWidget(mWobblyShoresButton, "WobblyShoresButton");
        getWidget(mWaterTextureSize, "WaterTextureSize");
        getWidget(mWaterReflectionDetail, "WaterReflectionDetail");
        getWidget(mWaterRainRippleDetail, "WaterRainRippleDetail");
        getWidget(mPrimaryLanguage, "PrimaryLanguage");
        getWidget(mSecondaryLanguage, "SecondaryLanguage");
        getWidget(mGmstOverridesL10n, "GmstOverridesL10nButton");
        getWidget(mWindowModeHint, "WindowModeHint");
        getWidget(mLightingMethodButton, "LightingMethodButton");
        getWidget(mLightsResetButton, "LightsResetButton");
        getWidget(mMaxLights, "MaxLights");
        getWidget(mScriptFilter, "ScriptFilter");
        getWidget(mScriptList, "ScriptList");
        getWidget(mScriptBox, "ScriptBox");
        getWidget(mScriptView, "ScriptView");
        getWidget(mScriptAdapter, "ScriptAdapter");
        getWidget(mScriptDisabled, "ScriptDisabled");

#ifndef WIN32
        // hide gamma controls since it currently does not work under Linux
        MyGUI::ScrollBar* gammaSlider;
        getWidget(gammaSlider, "GammaSlider");
        gammaSlider->setVisible(false);
        MyGUI::TextBox* textBox;
        getWidget(textBox, "GammaText");
        textBox->setVisible(false);
        getWidget(textBox, "GammaTextDark");
        textBox->setVisible(false);
        getWidget(textBox, "GammaTextLight");
        textBox->setVisible(false);
#endif

        mMainWidget->castType<MyGUI::Window>()->eventWindowChangeCoord
            += MyGUI::newDelegate(this, &SettingsWindow::onWindowResize);

        mSettingsTab->eventTabChangeSelect += MyGUI::newDelegate(this, &SettingsWindow::onTabChanged);
        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onOkButtonClicked);
        mTextureFilteringButton->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onTextureFilteringChanged);
        mResolutionList->eventListChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onResolutionSelected);

        mWaterRefractionButton->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onRefractionButtonClicked);
        mWaterTextureSize->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onWaterTextureSizeChanged);
        mWaterReflectionDetail->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onWaterReflectionDetailChanged);
        mWaterRainRippleDetail->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onWaterRainRippleDetailChanged);

        mLightingMethodButton->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onLightingMethodButtonChanged);
        mLightsResetButton->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onLightsResetButtonClicked);
        mMaxLights->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onMaxLightsChanged);

        mWindowModeList->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onWindowModeChanged);
        mVSyncModeList->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onVSyncModeChanged);

        mKeyboardSwitch->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onKeyboardSwitchClicked);
        mControllerSwitch->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onControllerSwitchClicked);

        mPrimaryLanguage->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onPrimaryLanguageChanged);
        mSecondaryLanguage->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onSecondaryLanguageChanged);
        mGmstOverridesL10n->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onGmstOverridesL10nChanged);

        computeMinimumWindowSize();

        center();

        mResetControlsButton->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onResetDefaultBindings);

        // fill resolution list
        const int screen = Settings::video().mScreen;
        int numDisplayModes = SDL_GetNumDisplayModes(screen);
        std::vector<std::pair<int, int>> resolutions;
        for (int i = 0; i < numDisplayModes; i++)
        {
            SDL_DisplayMode mode;
            SDL_GetDisplayMode(screen, i, &mode);
            resolutions.emplace_back(mode.w, mode.h);
        }
        std::sort(resolutions.begin(), resolutions.end(), sortResolutions);
        for (std::pair<int, int>& resolution : resolutions)
        {
            std::string str = Misc::getResolutionText(resolution.first, resolution.second);

            if (mResolutionList->findItemIndexWith(str) == MyGUI::ITEM_NONE)
                mResolutionList->addItem(str, resolution);
        }
        highlightCurrentResolution();

        mTextureFilteringButton->setCaptionWithReplacing(
            textureFilteringToStr(Settings::general().mTextureMipmap, Settings::general().mTextureMinFilter));

        int waterTextureSize = Settings::water().mRttSize;
        if (waterTextureSize >= 512)
            mWaterTextureSize->setIndexSelected(0);
        if (waterTextureSize >= 1024)
            mWaterTextureSize->setIndexSelected(1);
        if (waterTextureSize >= 2048)
            mWaterTextureSize->setIndexSelected(2);

        const int waterReflectionDetail = Settings::water().mReflectionDetail;
        mWaterReflectionDetail->setIndexSelected(waterReflectionDetail);

        const int waterRainRippleDetail = Settings::water().mRainRippleDetail;
        mWaterRainRippleDetail->setIndexSelected(waterRainRippleDetail);

        const bool waterRefraction = Settings::water().mRefraction;
        mSunlightScatteringButton->setEnabled(waterRefraction);
        mWobblyShoresButton->setEnabled(waterRefraction);

        updateMaxLightsComboBox(mMaxLights);

        const Settings::WindowMode windowMode = Settings::video().mWindowMode;
        mWindowBorderButton->setEnabled(
            windowMode != Settings::WindowMode::Fullscreen && windowMode != Settings::WindowMode::WindowedFullscreen);

        mWindowModeHint->setVisible(windowMode == Settings::WindowMode::WindowedFullscreen);

        mKeyboardSwitch->setStateSelected(true);
        mControllerSwitch->setStateSelected(false);

        mScriptFilter->eventEditTextChange += MyGUI::newDelegate(this, &SettingsWindow::onScriptFilterChange);
        mScriptList->eventListMouseItemActivate += MyGUI::newDelegate(this, &SettingsWindow::onScriptListSelection);

        std::vector<std::string> availableLanguages;
        const VFS::Manager* vfs = MWBase::Environment::get().getResourceSystem()->getVFS();
        constexpr VFS::Path::NormalizedView l10n("l10n/");
        for (const auto& path : vfs->getRecursiveDirectoryIterator(l10n))
        {
            if (path.extension() == "yaml")
            {
                std::string_view localeName(path.stem());
                if (localeName == "gmst")
                    continue; // fake locale to get gmst strings from content files
                if (std::find(availableLanguages.begin(), availableLanguages.end(), localeName)
                    == availableLanguages.end())
                    availableLanguages.emplace_back(localeName);
            }
        }

        std::sort(availableLanguages.begin(), availableLanguages.end());

        std::vector<std::string> currentLocales = Settings::general().mPreferredLocales;
        if (currentLocales.empty())
            currentLocales.push_back("en");

        icu::Locale primaryLocale(currentLocales[0].c_str());

        mPrimaryLanguage->removeAllItems();
        mPrimaryLanguage->setIndexSelected(MyGUI::ITEM_NONE);

        mSecondaryLanguage->removeAllItems();
        mSecondaryLanguage->addItem(
            MyGUI::LanguageManager::getInstance().replaceTags("#{Interface:None}"), std::string());
        mSecondaryLanguage->setIndexSelected(0);

        size_t i = 0;
        for (const auto& language : availableLanguages)
        {
            icu::Locale locale(language.c_str());

            icu::UnicodeString str(language.c_str());
            locale.getDisplayName(primaryLocale, str);
            std::string localeString;
            str.toUTF8String(localeString);

            mPrimaryLanguage->addItem(localeString, language);
            mSecondaryLanguage->addItem(localeString, language);

            if (language == currentLocales[0])
                mPrimaryLanguage->setIndexSelected(i);
            if (currentLocales.size() > 1 && language == currentLocales[1])
                mSecondaryLanguage->setIndexSelected(i + 1);

            i++;
        }

        mControllerButtons.mA = "#{Interface:Select}";
        mControllerButtons.mB = "#{Interface:OK}";
        mControllerButtons.mLStick = "#{Interface:Mouse}";

        // Wire up screen-reader focus announcements last so every widget
        // we created above is hooked in a single recursive pass.
        hookAccessibilityEvents(mMainWidget);
        mSettingsTab->eventTabChangeSelect += MyGUI::newDelegate(this, &SettingsWindow::onTabAnnounce);
    }

    void SettingsWindow::onTabChanged(MyGUI::TabControl* /*sender*/, size_t /*index*/)
    {
        resetScrollbars();
    }

    void SettingsWindow::onOkButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->toggleSettingsWindow();
    }

    void SettingsWindow::onResolutionSelected(MyGUI::ListBox* /*sender*/, size_t index)
    {
        if (index == MyGUI::ITEM_NONE)
            return;

        ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
        dialog->askForConfirmation("#{OMWEngine:ConfirmResolution}");
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &SettingsWindow::onResolutionAccept);
        dialog->eventCancelClicked.clear();
        dialog->eventCancelClicked += MyGUI::newDelegate(this, &SettingsWindow::onResolutionCancel);
    }

    void SettingsWindow::onResolutionAccept()
    {
        auto resolution = mResolutionList->getItemDataAt<std::pair<int, int>>(mResolutionList->getIndexSelected());
        if (resolution)
        {
            Settings::video().mResolutionX.set(resolution->first);
            Settings::video().mResolutionY.set(resolution->second);

            apply();
        }
    }

    void SettingsWindow::onResolutionCancel()
    {
        highlightCurrentResolution();
    }

    void SettingsWindow::highlightCurrentResolution()
    {
        mResolutionList->setIndexSelected(MyGUI::ITEM_NONE);

        const int currentX = Settings::video().mResolutionX;
        const int currentY = Settings::video().mResolutionY;

        for (size_t i = 0; i < mResolutionList->getItemCount(); ++i)
        {
            auto resolution = mResolutionList->getItemDataAt<std::pair<int, int>>(i);
            if (resolution && resolution->first == currentX && resolution->second == currentY)
            {
                mResolutionList->setIndexSelected(i);
                break;
            }
        }
    }

    void SettingsWindow::onRefractionButtonClicked(MyGUI::Widget* /*sender*/)
    {
        const bool refractionEnabled = Settings::water().mRefraction;

        mSunlightScatteringButton->setEnabled(refractionEnabled);
        mWobblyShoresButton->setEnabled(refractionEnabled);
    }

    void SettingsWindow::onWaterTextureSizeChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        int size = 0;
        if (pos == 0)
            size = 512;
        else if (pos == 1)
            size = 1024;
        else if (pos == 2)
            size = 2048;
        Settings::water().mRttSize.set(size);
        apply();
    }

    void SettingsWindow::onWaterReflectionDetailChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        Settings::water().mReflectionDetail.set(static_cast<int>(pos));
        apply();
    }

    void SettingsWindow::onWaterRainRippleDetailChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        Settings::water().mRainRippleDetail.set(static_cast<int>(pos));
        apply();
    }

    void SettingsWindow::onLightingMethodButtonChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        sender->setCaptionWithReplacing(sender->getItemNameAt(sender->getIndexSelected()));

        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(
            "#{OMWEngine:ChangeRequiresRestart}", { "#{Interface:OK}" }, true);

        Settings::shaders().mLightingMethod.set(
            Settings::parseLightingMethod(*sender->getItemDataAt<std::string>(pos)));
        apply();
    }

    void SettingsWindow::onLanguageChanged(size_t langPriority, MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        sender->setCaptionWithReplacing(sender->getItemNameAt(sender->getIndexSelected()));

        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(
            "#{OMWEngine:ChangeRequiresRestart}", { "#{Interface:OK}" }, true);

        std::vector<std::string> currentLocales = Settings::general().mPreferredLocales;
        if (currentLocales.size() <= langPriority)
            currentLocales.resize(langPriority + 1, "en");

        const auto& languageCode = *sender->getItemDataAt<std::string>(pos);
        if (!languageCode.empty())
            currentLocales[langPriority] = languageCode;
        else
            currentLocales.resize(1);

        Settings::general().mPreferredLocales.set(currentLocales);
    }

    void SettingsWindow::onGmstOverridesL10nChanged(MyGUI::Widget*)
    {
        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(
            "#{OMWEngine:ChangeRequiresRestart}", { "#{Interface:OK}" }, true);
    }

    void SettingsWindow::onVSyncModeChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        Settings::video().mVsyncMode.set(static_cast<SDLUtil::VSyncMode>(sender->getIndexSelected()));
        apply();
    }

    void SettingsWindow::onWindowModeChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        const Settings::WindowMode windowMode = static_cast<Settings::WindowMode>(sender->getIndexSelected());
        if (windowMode == Settings::WindowMode::WindowedFullscreen)
        {
            mResolutionList->setEnabled(false);
            mWindowModeHint->setVisible(true);
        }
        else
        {
            mResolutionList->setEnabled(true);
            mWindowModeHint->setVisible(false);
        }

        if (windowMode == Settings::WindowMode::Windowed)
            mWindowBorderButton->setEnabled(true);
        else
            mWindowBorderButton->setEnabled(false);

        Settings::video().mWindowMode.set(windowMode);
        apply();
    }

    void SettingsWindow::onMaxLightsChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        Settings::shaders().mMaxLights.set(8 * static_cast<int>(pos + 1));
        apply();
        configureWidgets(mMainWidget, false);
    }

    void SettingsWindow::onLightsResetButtonClicked(MyGUI::Widget* /*sender*/)
    {
        std::vector<std::string> buttons = { "#{Interface:Yes}", "#{Interface:No}" };
        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(
            "#{OMWEngine:LightingResetToDefaults}", buttons, true);
        int selectedButton = MWBase::Environment::get().getWindowManager()->readPressedButton();
        if (selectedButton == 1 || selectedButton == -1)
            return;

        Settings::shaders().mForcePerPixelLighting.reset();
        Settings::shaders().mClassicFalloff.reset();
        Settings::shaders().mClampLighting.reset();
        Settings::shaders().mMatchSunlightToSun.reset();
        Settings::shaders().mLightRadiusMultiplier.reset();
        Settings::shaders().mMaximumLightDistance.reset();
        Settings::shaders().mLightFadeStart.reset();
        Settings::shaders().mMinimumInteriorBrightness.reset();
        Settings::shaders().mMaxLights.reset();
        Settings::shaders().mLightingMethod.reset();

        const SceneUtil::LightingMethod lightingMethod = Settings::shaders().mLightingMethod;
        const std::size_t lightIndex = mLightingMethodButton->findItemIndexWith(lightingMethodToStr(lightingMethod));
        mLightingMethodButton->setIndexSelected(lightIndex);
        updateMaxLightsComboBox(mMaxLights);

        apply();
        configureWidgets(mMainWidget, false);
    }

    void SettingsWindow::onButtonToggled(MyGUI::Widget* sender)
    {
        const std::string on = MWBase::Environment::get().getL10nManager()->getMessage("Interface", "On");
        const std::string off = MWBase::Environment::get().getL10nManager()->getMessage("Interface", "Off");
        bool newState;
        if (sender->castType<MyGUI::Button>()->getCaption() == on)
        {
            sender->castType<MyGUI::Button>()->setCaption(MyGUI::UString(off));
            newState = false;
        }
        else
        {
            sender->castType<MyGUI::Button>()->setCaption(MyGUI::UString(on));
            newState = true;
        }

        if (getSettingType(sender) == checkButtonType)
        {
            Settings::get<bool>(getSettingCategory(sender), getSettingName(sender)).set(newState);
            // Announce the new toggle state ("On" / "Off") so the
            // screen-reader user knows what they just selected. Use
            // the displayed caption since it's already localized.
            if (mMainWidget->getVisible())
                speakA11y(sender->castType<MyGUI::Button>()->getCaption());
            apply();
            return;
        }
    }

    void SettingsWindow::onTextureFilteringChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        auto& generalSettings = Settings::general();
        switch (pos)
        {
            case 0: // Bilinear with mips
                generalSettings.mTextureMipmap.set("nearest");
                generalSettings.mTextureMagFilter.set("linear");
                generalSettings.mTextureMinFilter.set("linear");
                break;
            case 1: // Trilinear with mips
                generalSettings.mTextureMipmap.set("linear");
                generalSettings.mTextureMagFilter.set("linear");
                generalSettings.mTextureMinFilter.set("linear");
                break;
            default:
                Log(Debug::Warning) << "Unexpected texture filtering option pos " << pos;
                break;
        }

        apply();
    }

    void SettingsWindow::onResChange(int /*width*/, int /*height*/)
    {
        center();
        highlightCurrentResolution();
    }

    void SettingsWindow::onSliderChangePosition(MyGUI::ScrollBar* scroller, size_t pos)
    {
        if (getSettingType(scroller) == "Slider")
        {
            std::vector<icu::UnicodeString> argNames;
            std::vector<icu::Formattable> args;
            std::string_view valueType = getSettingValueType(scroller);
            if (valueType == "Float" || valueType == "Integer" || valueType == "Cell")
            {
                float value = pos / float(scroller->getScrollRange() - 1);

                float min, max;
                getSettingMinMax(scroller, min, max);
                value = min + (max - min) * value;

                if (valueType == "Cell")
                {
                    Settings::get<float>(getSettingCategory(scroller), getSettingName(scroller)).set(value);
                    argNames.emplace_back("cells");
                    args.emplace_back(value / Constants::CellSizeInUnits);
                }
                else if (valueType == "Float")
                {
                    Settings::get<float>(getSettingCategory(scroller), getSettingName(scroller)).set(value);
                    argNames.emplace_back("value");
                    args.emplace_back(value);
                }
                else
                {
                    int intValue = static_cast<int>(value);
                    Settings::get<int>(getSettingCategory(scroller), getSettingName(scroller)).set(intValue);
                    argNames.emplace_back("value");
                    args.emplace_back(intValue);
                }
            }
            else
            {
                int intValue = static_cast<int>(pos);
                Settings::get<int>(getSettingCategory(scroller), getSettingName(scroller)).set(intValue);
                argNames.emplace_back("value");
                args.emplace_back(intValue);
            }
            MyGUI::TextBox* sliderLabel = getSliderLabel(scroller);
            updateSliderLabel(scroller, sliderLabel, argNames, args);

            // Announce the new value. For sliders that bake the value
            // into a tagged label TextBox (e.g. "Difficulty: -25") we
            // speak the whole formatted caption; otherwise we just
            // speak the bare number so adjusting master volume etc.
            // reads as "0.75", "0.80", etc.
            if (mMainWidget->getVisible())
            {
                if (sliderLabel != nullptr)
                    speakA11y(sliderLabel->getCaption());
                else
                    speakA11y(formatSliderValueForA11y(scroller));
            }

            apply();
        }
    }

    void SettingsWindow::apply()
    {
        const Settings::CategorySettingVector changed = Settings::Manager::getPendingChanges();
        MWBase::Environment::get().getWorld()->processChangedSettings(changed);
        MWBase::Environment::get().getSoundManager()->processChangedSettings(changed);
        MWBase::Environment::get().getWindowManager()->processChangedSettings(changed);
        MWBase::Environment::get().getInputManager()->processChangedSettings(changed);
        MWBase::Environment::get().getMechanicsManager()->processChangedSettings(changed);
        Settings::Manager::resetPendingChanges();
    }

    void SettingsWindow::onKeyboardSwitchClicked(MyGUI::Widget* /*sender*/)
    {
        if (mKeyboardMode)
            return;
        mKeyboardMode = true;
        mKeyboardSwitch->setStateSelected(true);
        mControllerSwitch->setStateSelected(false);
        updateControlsBox();
        resetScrollbars();
    }

    void SettingsWindow::onControllerSwitchClicked(MyGUI::Widget* /*sender*/)
    {
        if (!mKeyboardMode)
            return;
        mKeyboardMode = false;
        mKeyboardSwitch->setStateSelected(false);
        mControllerSwitch->setStateSelected(true);
        updateControlsBox();
        resetScrollbars();
    }

    void SettingsWindow::updateControlsBox()
    {
        while (mControlsBox->getChildCount())
            MyGUI::Gui::getInstance().destroyWidget(mControlsBox->getChildAt(0));

        MWBase::Environment::get().getWindowManager()->removeStaticMessageBox();
        const auto inputManager = MWBase::Environment::get().getInputManager();
        const auto& actions
            = mKeyboardMode ? inputManager->getActionKeySorting() : inputManager->getActionControllerSorting();

        for (const int& action : actions)
        {
            std::string desc{ inputManager->getActionDescription(action) };
            if (desc.empty())
                continue;

            std::string binding;
            if (mKeyboardMode)
                binding = inputManager->getActionKeyBindingName(action);
            else
                binding = inputManager->getActionControllerBindingName(action);

            Gui::SharedStateButton* leftText = mControlsBox->createWidget<Gui::SharedStateButton>(
                "SandTextButton", MyGUI::IntCoord(), MyGUI::Align::Default);
            leftText->setCaptionWithReplacing(desc);

            Gui::SharedStateButton* rightText = mControlsBox->createWidget<Gui::SharedStateButton>(
                "SandTextButton", MyGUI::IntCoord(), MyGUI::Align::Default);
            rightText->setCaptionWithReplacing(binding);
            rightText->setTextAlign(MyGUI::Align::Right);
            rightText->setUserData(action); // save the action id for callbacks
            rightText->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onRebindAction);
            rightText->eventMouseWheel += MyGUI::newDelegate(this, &SettingsWindow::onInputTabMouseWheel);

            Gui::ButtonGroup group;
            group.push_back(leftText);
            group.push_back(rightText);
            Gui::SharedStateButton::createButtonGroup(group);
        }

        layoutControlsBox();
    }

    void SettingsWindow::updateLightSettings()
    {
        auto lightingMethod = MWBase::Environment::get().getResourceSystem()->getSceneManager()->getLightingMethod();
        MyGUI::UString lightingMethodStr = lightingMethodToStr(lightingMethod);

        mLightingMethodButton->removeAllItems();

        std::array<SceneUtil::LightingMethod, 2> methods = {
            SceneUtil::LightingMethod::PerObjectUniform,
            SceneUtil::LightingMethod::SingleUBO,
        };

        for (const auto& method : methods)
        {
            if (!MWBase::Environment::get().getResourceSystem()->getSceneManager()->isSupportedLightingMethod(method))
                continue;

            mLightingMethodButton->addItem(
                lightingMethodToStr(method), SceneUtil::LightManager::getLightingMethodString(method));
        }
        mLightingMethodButton->setIndexSelected(mLightingMethodButton->findItemIndexWith(lightingMethodStr));
    }

    void SettingsWindow::updateWindowModeSettings()
    {
        const Settings::WindowMode windowMode = Settings::video().mWindowMode;
        const std::size_t windowModeIndex = static_cast<std::size_t>(windowMode);

        mWindowModeList->setIndexSelected(windowModeIndex);

        if (windowMode != Settings::WindowMode::Windowed && windowModeIndex != MyGUI::ITEM_NONE)
        {
            // check if this resolution is supported in fullscreen
            if (mResolutionList->getIndexSelected() != MyGUI::ITEM_NONE)
            {
                auto resolution
                    = mResolutionList->getItemDataAt<std::pair<int, int>>(mResolutionList->getIndexSelected());
                if (resolution)
                {
                    Settings::video().mResolutionX.set(resolution->first);
                    Settings::video().mResolutionY.set(resolution->second);
                }
            }

            bool supported = false;
            int fallbackX = 0, fallbackY = 0;
            for (size_t i = 0; i < mResolutionList->getItemCount(); ++i)
            {
                auto resolution = mResolutionList->getItemDataAt<std::pair<int, int>>(i);
                if (!resolution)
                    continue;

                if (i == 0)
                {
                    fallbackX = resolution->first;
                    fallbackY = resolution->second;
                }

                if (resolution->first == Settings::video().mResolutionX
                    && resolution->second == Settings::video().mResolutionY)
                    supported = true;
            }

            if (!supported && mResolutionList->getItemCount())
            {
                if (fallbackX != 0 && fallbackY != 0)
                {
                    Settings::video().mResolutionX.set(fallbackX);
                    Settings::video().mResolutionY.set(fallbackY);
                }
            }

            mWindowBorderButton->setEnabled(false);
        }

        if (windowMode == Settings::WindowMode::WindowedFullscreen)
            mResolutionList->setEnabled(false);
    }

    void SettingsWindow::updateVSyncModeSettings()
    {
        mVSyncModeList->setIndexSelected(static_cast<size_t>(Settings::video().mVsyncMode));
    }

    void SettingsWindow::layoutControlsBox()
    {
        const int h = Settings::gui().mFontSize + 2;
        const int w = mControlsBox->getWidth() - 28;
        const int noWidgetsInRow = 2;
        const int totalH = static_cast<int>(mControlsBox->getChildCount() / noWidgetsInRow) * h;

        for (size_t i = 0; i < mControlsBox->getChildCount(); i++)
        {
            MyGUI::Widget* widget = mControlsBox->getChildAt(i);
            widget->setCoord(0, static_cast<int>(i / noWidgetsInRow * h), w, h);
        }

        // Canvas size must be expressed with VScroll disabled, otherwise MyGUI would expand the scroll area when the
        // scrollbar is hidden
        mControlsBox->setVisibleVScroll(false);
        mControlsBox->setCanvasSize(mControlsBox->getWidth(), std::max(totalH, mControlsBox->getHeight()));
        mControlsBox->setVisibleVScroll(true);
    }

    void SettingsWindow::renderScriptSettings()
    {
        mScriptAdapter->detach();

        mScriptList->removeAllItems();
        mScriptView->setCanvasSize({ 0, 0 });

        struct WeightedPage
        {
            size_t mIndex;
            std::string mName;
            size_t mNameWeight;
            size_t mHintWeight;

            constexpr bool operator<(const WeightedPage& rhs) const
            {
                if (mNameWeight != rhs.mNameWeight)
                    return mNameWeight > rhs.mNameWeight;
                if (mHintWeight != rhs.mHintWeight)
                    return mHintWeight > rhs.mHintWeight;
                return mName < rhs.mName;
            }
        };

        const std::vector<std::string> patternArray = generatePatternArray(mScriptFilter->getCaption());
        std::vector<WeightedPage> weightedPages;
        weightedPages.reserve(LuaUi::scriptSettingsPageCount());
        for (size_t i = 0; i < LuaUi::scriptSettingsPageCount(); ++i)
        {
            LuaUi::ScriptSettingsPage page = LuaUi::scriptSettingsPageAt(i);
            size_t nameWeight = weightedSearch(page.mName, patternArray);
            size_t hintWeight = weightedSearch(page.mSearchHints, patternArray);
            if ((nameWeight + hintWeight) > 0)
                weightedPages.push_back({ i, page.mName, nameWeight, hintWeight });
        }
        std::sort(weightedPages.begin(), weightedPages.end());
        for (const WeightedPage& weightedPage : weightedPages)
            mScriptList->addItem(weightedPage.mName, weightedPage.mIndex);

        // Hide script settings when the game world isn't loaded
        bool disabled = LuaUi::scriptSettingsPageCount() == 0;
        mScriptFilter->setVisible(!disabled);
        mScriptList->setVisible(!disabled);
        mScriptBox->setVisible(!disabled);
        mScriptDisabled->setVisible(disabled);

        LuaUi::attachPageAt(mCurrentPage, mScriptAdapter);
    }

    void SettingsWindow::onScriptFilterChange(MyGUI::EditBox*)
    {
        renderScriptSettings();
    }

    void SettingsWindow::onScriptListSelection(MyGUI::ListBox*, size_t index)
    {
        mScriptAdapter->detach();
        mCurrentPage = static_cast<size_t>(-1);
        if (index < mScriptList->getItemCount())
        {
            mCurrentPage = *mScriptList->getItemDataAt<size_t>(index);
            LuaUi::attachPageAt(mCurrentPage, mScriptAdapter);
        }
    }

    void SettingsWindow::onRebindAction(MyGUI::Widget* sender)
    {
        int actionId = *sender->getUserData<int>();

        sender->castType<MyGUI::Button>()->setCaptionWithReplacing("#{Interface:None}");

        MWBase::Environment::get().getWindowManager()->staticMessageBox("#{OMWEngine:RebindAction}");
        MWBase::Environment::get().getWindowManager()->disallowMouse();

        MWBase::Environment::get().getInputManager()->enableDetectingBindingMode(actionId, mKeyboardMode);
    }

    void SettingsWindow::onInputTabMouseWheel(MyGUI::Widget* /*sender*/, int rel)
    {
        if (mControlsBox->getViewOffset().top + rel * 0.3f > 0)
            mControlsBox->setViewOffset(MyGUI::IntPoint(0, 0));
        else
            mControlsBox->setViewOffset(
                MyGUI::IntPoint(0, static_cast<int>(mControlsBox->getViewOffset().top + rel * 0.3f)));
    }

    void SettingsWindow::onResetDefaultBindings(MyGUI::Widget* /*sender*/)
    {
        ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
        dialog->askForConfirmation("#{OMWEngine:ConfirmResetBindings}");
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &SettingsWindow::onResetDefaultBindingsAccept);
        dialog->eventCancelClicked.clear();
    }

    void SettingsWindow::onResetDefaultBindingsAccept()
    {
        if (mKeyboardMode)
            MWBase::Environment::get().getInputManager()->resetToDefaultKeyBindings();
        else
            MWBase::Environment::get().getInputManager()->resetToDefaultControllerBindings();
        updateControlsBox();
    }

    void SettingsWindow::onOpen()
    {
        highlightCurrentResolution();
        updateControlsBox();
        updateLightSettings();
        updateWindowModeSettings();
        updateVSyncModeSettings();
        resetScrollbars();
        renderScriptSettings();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mOkButton);
    }

    void SettingsWindow::onClose()
    {
        // Save user settings
        Settings::Manager::saveUser(mCfgMgr.getUserConfigPath() / "settings.cfg");
        MWBase::Environment::get().getLuaManager()->savePermanentStorage(mCfgMgr.getUserConfigPath());
        MWBase::Environment::get().getInputManager()->saveBindings();
    }

    void SettingsWindow::onWindowResize(MyGUI::Window* /*sender*/)
    {
        layoutControlsBox();
    }

    void SettingsWindow::computeMinimumWindowSize()
    {
        auto* window = mMainWidget->castType<MyGUI::Window>();
        auto minSize = window->getMinSize();

        // Window should be at minimum wide enough to show all tabs.
        int tabBarWidth = 0;
        for (uint32_t i = 0; i < mSettingsTab->getItemCount(); i++)
        {
            tabBarWidth += mSettingsTab->getButtonWidthAt(i);
        }

        // Need to include window margins
        int margins = mMainWidget->getWidth() - mSettingsTab->getWidth();
        int minimumWindowWidth = tabBarWidth + margins;

        if (minimumWindowWidth > minSize.width)
        {
            minSize.width = minimumWindowWidth;
            window->setMinSize(minSize);

            // Make a dummy call to setSize so MyGUI can apply any resize resulting from the change in MinSize
            mMainWidget->setSize(mMainWidget->getSize());
        }
    }

    void SettingsWindow::resetScrollbars()
    {
        mResolutionList->setScrollPosition(0);
        mControlsBox->setViewOffset(MyGUI::IntPoint(0, 0));
    }

    bool SettingsWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            onOkButtonClicked(mOkButton);
            return true;
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        {
            size_t index = mSettingsTab->getIndexSelected();
            index = wrap(index, mSettingsTab->getItemCount(), -1);
            mSettingsTab->setIndexSelected(index);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
            return true;
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        {
            size_t index = mSettingsTab->getIndexSelected();
            index = wrap(index, mSettingsTab->getItemCount(), 1);
            mSettingsTab->setIndexSelected(index);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
            return true;
        }

        return false;
    }

}
