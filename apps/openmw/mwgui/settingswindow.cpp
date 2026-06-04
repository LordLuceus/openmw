#include "settingswindow.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

#include <unicode/locid.h>

#include <MyGUI_ComboBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_TabItem.h>
#include <MyGUI_UString.h>
#include <MyGUI_Window.h>

#include <SDL_keyboard.h>
#include <SDL_video.h>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_TextBox.h>

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

#include "accessibility/speech.hpp"
#include "confirmationdialog.hpp"
#include "weightedsearch.hpp"

namespace
{
    // Shorthand for the shared screen-reader speech entry point.
    void speakA11y(std::string_view text) { MWGui::A11y::say(text); }

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

        // Arm a deferred key-rebind, but only once the activation key (Enter /
        // Space) has been physically RELEASED. The user starts a rebind by
        // pressing Enter on the row; if we arm ICS detection while Enter is
        // still held, ICS captures that Enter (key-repeat) as "Return", the bind
        // completes, the row re-enables, and the still-held Enter re-triggers the
        // rebind -- an endless loop. Waiting for release breaks that cycle and
        // also stops the activating key from being captured as the new binding.
        if (mPendingRebindAction >= 0)
        {
            const Uint8* keyState = SDL_GetKeyboardState(nullptr);
            const bool activationHeld = keyState
                && (keyState[SDL_SCANCODE_RETURN] || keyState[SDL_SCANCODE_KP_ENTER]
                    || keyState[SDL_SCANCODE_SPACE]);
            if (!activationHeld)
            {
                const int action = mPendingRebindAction;
                mPendingRebindAction = -1;
                MWBase::Environment::get().getInputManager()->enableDetectingBindingMode(action, mKeyboardMode);
            }
        }

        // A bind (or a keyboard/controller-mode switch, or a reset) rebuilt the
        // controls list, destroying the widgets our A11y options referenced.
        // Rebuild the option list and restore the cursor to the rebound row.
        if (mRebuildControlsA11y)
        {
            mRebuildControlsA11y = false;
            buildAccessibilityElements(/*announceSelection=*/false);
            if (!mLastRebindLabel.empty() && mA11y.selectByLabel(mLastRebindLabel, /*announce=*/true))
            {
                // Selection (and its new binding value) announced.
            }
            else
            {
                mA11y.announceCurrent();
            }
            mLastRebindLabel.clear();
        }

        mA11y.onFrame(duration);
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

    std::string SettingsWindow::settingValueText(MyGUI::Widget* widget) const
    {
        if (!widget)
            return {};

        if (getSettingType(widget) == sliderType)
        {
            auto* scroll = widget->castType<MyGUI::ScrollBar>(false);
            if (!scroll)
                return {};
            // Sliders with a SettingLabelWidget bake the value into their
            // heading caption (e.g. "Difficulty: 0"); otherwise format the
            // bare number (master volume -> "0.75").
            if (MyGUI::TextBox* label = getSliderLabel(scroll))
            {
                std::string cap = label->getCaption();
                if (!cap.empty())
                    return cap;
            }
            return formatSliderValueForA11y(scroll);
        }

        if (getSettingType(widget) == checkButtonType)
        {
            if (auto* btn = widget->castType<MyGUI::Button>(false))
                return btn->getCaption();
        }

        if (auto* combo = widget->castType<MyGUI::ComboBox>(false))
        {
            const size_t pos = combo->getIndexSelected();
            if (pos != MyGUI::ITEM_NONE && pos < combo->getItemCount())
                return combo->getItemNameAt(pos);
            return {};
        }

        if (auto* list = widget->castType<MyGUI::ListBox>(false))
        {
            const size_t pos = list->getIndexSelected();
            if (pos != MyGUI::ITEM_NONE && pos < list->getItemCount())
                return list->getItemNameAt(pos);
            return {};
        }

        return {};
    }

    void SettingsWindow::changeSettingValue(MyGUI::Widget* widget, bool next)
    {
        if (!widget)
            return;

        // Slider: step by a fraction of the configured page, then reuse the
        // existing change handler to persist (speech is suppressed there; the
        // A11y framework speaks the new value once via the Element's value()).
        if (getSettingType(widget) == sliderType)
        {
            auto* scroll = widget->castType<MyGUI::ScrollBar>(false);
            if (!scroll)
                return;
            const size_t range = scroll->getScrollRange();
            if (range == 0)
                return;
            const size_t maxIndex = range - 1;
            const size_t page = std::max<size_t>(1, scroll->getScrollPage());
            const size_t fine = std::max<size_t>(1, page / 10);
            const size_t pos = scroll->getScrollPosition();
            const size_t newPos = next ? std::min(maxIndex, pos + fine) : (pos > fine ? pos - fine : 0);
            if (newPos == pos)
                return;
            scroll->setScrollPosition(newPos);
            mSuppressSettingSpeech = true;
            onSliderChangePosition(scroll, newPos); // setScrollPosition doesn't fire the event
            mSuppressSettingSpeech = false;
            return;
        }

        // Checkbox: toggle via the existing click handler (speech suppressed).
        if (getSettingType(widget) == checkButtonType)
        {
            mSuppressSettingSpeech = true;
            widget->eventMouseButtonClick(widget);
            mSuppressSettingSpeech = false;
            return;
        }

        // Combo box: move the selection and fire the change event so the
        // existing per-combo logic (apply settings etc.) runs.
        if (auto* combo = widget->castType<MyGUI::ComboBox>(false))
        {
            const size_t count = combo->getItemCount();
            if (count == 0)
                return;
            size_t cur = combo->getIndexSelected();
            if (cur == MyGUI::ITEM_NONE)
                cur = 0;
            const size_t pos = next ? (cur + 1) % count : (cur + count - 1) % count;
            if (pos == cur)
                return;
            combo->setIndexSelected(pos);
            mSuppressSettingSpeech = true;
            combo->eventComboChangePosition(combo, pos);
            mSuppressSettingSpeech = false;
            return;
        }

        // List box (resolution): move the highlight and speak, but DON'T fire
        // the change event -- that pops a confirmation dialog. Enter applies.
        if (auto* list = widget->castType<MyGUI::ListBox>(false))
        {
            const size_t count = list->getItemCount();
            if (count == 0)
                return;
            size_t cur = list->getIndexSelected();
            if (cur == MyGUI::ITEM_NONE)
                cur = 0;
            const size_t pos = next ? (cur + 1) % count : (cur + count - 1) % count;
            if (pos == cur)
                return;
            list->setIndexSelected(pos);
            list->beginToItemAt(pos);
            return;
        }
    }

    void SettingsWindow::registerSettingWidget(MyGUI::Widget* widget)
    {
        if (!widget)
            return;

        const std::string_view type = getSettingType(widget);
        const bool isSlider = type == sliderType;
        const bool isCheck = type == checkButtonType;
        auto* combo = widget->castType<MyGUI::ComboBox>(false);
        auto* list = widget->castType<MyGUI::ListBox>(false);
        auto* button = widget->castType<MyGUI::Button>(false);

        A11y::Element element;
        element.widget = widget;
        element.label = resolveAccessibilityLabel(widget);

        // A few video widgets live inside HBoxes, so their heading TextBox is
        // not a direct sibling and the generic resolver can't find it (or finds
        // the wrong one). Give them explicit, correct labels.
        if (widget == mResolutionList)
            element.label = "#{OMWEngine:Resolution}";
        else if (widget == mWindowModeList)
            element.label = "#{OMWEngine:WindowMode}";
        else if (widget == mVSyncModeList)
            element.label = "#{OMWEngine:VSync}";

        // Baked-label sliders (Difficulty, Actors Processing Range, FOV,
        // Gamma...) put the *name and value together* in their label caption
        // (e.g. "Difficulty: 0"), which is exactly what settingValueText()
        // returns. Don't also speak it as the label, or it reads twice; the
        // value() call alone announces "Difficulty: 0".
        if (isSlider)
        {
            auto* scroll = widget->castType<MyGUI::ScrollBar>(false);
            if (scroll && getSliderLabel(scroll) != nullptr)
                element.label.clear();
        }

        if (isSlider || combo || list)
        {
            element.value = [this, widget] { return settingValueText(widget); };
            element.change = [this, widget](bool next) { changeSettingValue(widget, next); };
        }

        if (isCheck)
        {
            // Toggle on Left/Right and on Enter; value() reports the new state.
            element.value = [this, widget] { return settingValueText(widget); };
            element.change = [this, widget](bool /*next*/) { changeSettingValue(widget, true); };
            element.activate = [widget] { widget->eventMouseButtonClick(widget); };
        }
        else if (list)
        {
            // Enter on the resolution list applies the highlighted resolution.
            element.activate = [list] {
                const size_t pos = list->getIndexSelected();
                if (pos != MyGUI::ITEM_NONE)
                    list->eventListChangePosition(list, pos);
            };
        }
        else if (!isSlider && !combo && button)
        {
            // Plain action button (OK, Reset, Keyboard / Controller switch,
            // key-rebind rows, Lights reset, ...): activate fires its click.
            element.activate = [button] { button->eventMouseButtonClick(button); };
        }

        mA11y.add(std::move(element));
    }

    void SettingsWindow::registerControlsBox()
    {
        // The controls list is built dynamically (see updateControlsBox) as a
        // flat sequence of SharedStateButton pairs: [description, binding],
        // [description, binding], ... Present each pair as ONE option whose
        // label is the action description, whose value is the current binding,
        // and whose activation starts rebinding (the existing onRebindAction
        // path via the binding button's click event).
        if (!mControlsBox)
            return;
        const size_t count = mControlsBox->getChildCount();
        for (size_t i = 0; i + 1 < count; i += 2)
        {
            MyGUI::Widget* descWidget = mControlsBox->getChildAt(i);
            MyGUI::Widget* bindWidget = mControlsBox->getChildAt(i + 1);
            auto* descBtn = descWidget->castType<MyGUI::Button>(false);
            auto* bindBtn = bindWidget->castType<MyGUI::Button>(false);
            if (!descBtn || !bindBtn)
                continue;

            A11y::Element element;
            // Anchor the option on the binding button (the actionable one).
            element.widget = bindWidget;
            element.label = descBtn->getCaption();
            element.value = [bindBtn] { return std::string(bindBtn->getCaption()); };
            // Enter starts the key/button detection; the existing handler shows
            // the "press a key" message box and updates the caption when done.
            element.activate = [bindBtn] { bindBtn->eventMouseButtonClick(bindBtn); };
            mA11y.add(std::move(element));
        }
    }

    void SettingsWindow::collectSettingWidgets(MyGUI::Widget* root)
    {
        if (!root)
            return;
        MyGUI::EnumeratorWidgetPtr it = root->getEnumerator();
        while (it.next())
        {
            MyGUI::Widget* current = it.current();
            if (!current->getVisible())
                continue; // skip hidden sub-tabs / unused sliders

            // The dynamically-built controls list gets bespoke pairing.
            if (current == mControlsBox)
            {
                registerControlsBox();
                continue; // don't descend; its children are handled above
            }

            const std::string_view type = getSettingType(current);
            const bool isSetting = (type == sliderType || type == checkButtonType);
            auto* combo = current->castType<MyGUI::ComboBox>(false);
            auto* list = current->castType<MyGUI::ListBox>(false);
            auto* button = current->castType<MyGUI::Button>(false);
            // Register controls and actionable buttons. ComboBox derives from
            // a Button-like widget, so check the setting/combo/list cases
            // first and only treat a *focusable* leftover button as an action.
            if (isSetting || combo || list)
                registerSettingWidget(current);
            else if (button && button->getNeedKeyFocus() && !button->getCaption().empty())
                registerSettingWidget(current);

            collectSettingWidgets(current);
        }
    }

    void SettingsWindow::buildAccessibilityElements(bool announceSelection)
    {
        mA11y.clear();

        // Find the content root of the deepest selected tab: the selected
        // outer TabItem, descending into an inner TabControl if present.
        MyGUI::Widget* contentRoot = nullptr;
        if (mSettingsTab)
        {
            if (MyGUI::TabItem* outer = mSettingsTab->getItemSelected())
            {
                contentRoot = outer;
                // Look for a nested TabControl (the Video tab) and use its
                // selected item instead.
                MyGUI::EnumeratorWidgetPtr it = outer->getEnumerator();
                while (it.next())
                {
                    if (auto* inner = it.current()->castType<MyGUI::TabControl>(false))
                    {
                        if (MyGUI::TabItem* innerItem = inner->getItemSelected())
                            contentRoot = innerItem;
                        break;
                    }
                }
            }
        }

        if (contentRoot)
            collectSettingWidgets(contentRoot);

        // The OK button lives outside the tab control; always offer it last.
        registerSettingWidget(mOkButton);

        mA11y.focusFirst(announceSelection);
    }

    void SettingsWindow::cycleTab(int delta)
    {
        if (!mSettingsTab)
            return;
        const size_t count = mSettingsTab->getItemCount();
        if (count == 0)
            return;
        const size_t cur = mSettingsTab->getIndexSelected();
        const size_t next = (cur + count + delta) % count;
        if (next != cur)
        {
            mSettingsTab->setIndexSelected(next);
            onTabChanged(mSettingsTab, next); // setIndexSelected doesn't fire the event
        }
        // Announce the tab name, then build its options without re-announcing
        // the first one (the tab name is the headline).
        speakA11y(mSettingsTab->getItemNameAt(mSettingsTab->getIndexSelected()));
        buildAccessibilityElements(/*announceSelection=*/false);
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

        // Screen-reader setup. The window is navigated through the shared
        // A11y::Screen in virtual-focus mode: a single invisible anchor widget
        // holds MyGUI key focus while the controller tracks the current option
        // internally, so native ListBox / ComboBox / ScrollBar widgets never
        // receive focus or consume our arrow keys. Tab / Shift+Tab cycle the
        // settings tabs; Up/Down move between options on the current tab.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);
        mA11y.setExtraKeyHandler([this](MyGUI::KeyCode key) -> bool {
            if (key == MyGUI::KeyCode::Tab)
            {
                cycleTab(MyGUI::InputManager::getInstance().isShiftPressed() ? -1 : 1);
                return true;
            }
            return false;
        });
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
            // Announce the new toggle state (\"On\" / \"Off\") so the
            // screen-reader user knows what they just selected. Use
            // the displayed caption since it's already localized. Skip when
            // the A11y framework is driving the change (it speaks the value).
            if (mMainWidget->getVisible() && !mSuppressSettingSpeech)
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
            if (mMainWidget->getVisible() && !mSuppressSettingSpeech)
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

        // We just destroyed and recreated every controls-list widget, so any
        // A11y options anchored on the old widgets now dangle. If the screen
        // reader is active, ask onFrame to rebuild the option list (and restore
        // the selection). Guarded on isActive() so the initial onOpen build --
        // which calls updateControlsBox() before the A11y list exists -- is
        // unaffected.
        if (mA11y.isActive())
            mRebuildControlsA11y = true;
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

        // Ignore re-entry while a rebind is already pending or in progress.
        // Holding Enter on a row produces MyGUI key-repeat activations; without
        // this guard each repeat re-announced "press a key" and re-started the
        // detection dance.
        if (mPendingRebindAction >= 0
            || MWBase::Environment::get().getInputManager()->isDetectingBindingState())
            return;

        sender->castType<MyGUI::Button>()->setCaptionWithReplacing("#{Interface:None}");

        MWBase::Environment::get().getWindowManager()->staticMessageBox("#{OMWEngine:RebindAction}");
        MWBase::Environment::get().getWindowManager()->disallowMouse();

        // Remember which row this is (by its action description) so that, once
        // the bind completes and updateControlsBox() rebuilds the widgets, we
        // can rebuild our A11y options and land the cursor back on this row.
        mLastRebindLabel = mA11y.currentLabel();

        // Do NOT arm ICS detection now: when this was triggered by pressing
        // Enter on the row (the screen-reader activation path), the activating
        // Enter key is still held. onFrame arms detection only once that key is
        // released, so ICS doesn't immediately capture it as "Return".
        mPendingRebindAction = actionId;
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

        // Hand input to the shared A11y controller: it pins key focus to the
        // anchor, disables engine spatial navigation, and registers + announces
        // the options on the current tab.
        mA11y.activate();
        buildAccessibilityElements(/*announceSelection=*/true);
    }

    void SettingsWindow::onClose()
    {
        mA11y.deactivate();

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
