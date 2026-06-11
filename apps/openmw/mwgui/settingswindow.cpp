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
#include <components/lua_ui/widget.hpp>
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
    namespace
    {
        // Defined further down (with the rest of the Lua-page walker helpers);
        // forward-declared here because SettingsWindow::onFrame -- which appears
        // before that definition -- polls a setting row's value via this.
        std::string rowValueText(MyGUI::Widget* row);
    }

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

        // Detect the end of editing a Scripts-tab number field FIRST, before
        // touching mScriptPageEdits below. Leaving edit mode (Escape) drops focus
        // off the box, which fires the Lua focusLoss commit -> set() -> the row
        // re-renders and DESTROYS the box this EditField is bound to. So on the
        // edit-mode true->false transition we must rebuild the page a11y (which
        // clears + re-attaches mScriptPageEdits to the fresh widgets) before any
        // field.onFrame() dereferences the now-dangling box, and then announce
        // the committed value once it settles (async for global settings).
        const bool editingNow = mA11y.editing();
        if (!mScriptEditModePrev && editingNow)
        {
            // Just entered edit mode: if it's one of our number fields, record
            // its setting + pre-edit value so the post-commit settle can be
            // announced. (Not ours, e.g. the search filter -> key stays empty.)
            mScriptEditGroup.clear();
            mScriptEditKey.clear();
            mScriptEditOldValue.clear();
            if (MyGUI::Widget* box = mA11y.currentEditWidget())
            {
                if (auto it = mScriptEditFieldKeys.find(box); it != mScriptEditFieldKeys.end())
                {
                    mScriptEditGroup = it->second.first;
                    mScriptEditKey = it->second.second;
                    if (MyGUI::Widget* row = findScriptRow(mScriptEditGroup, mScriptEditKey))
                    {
                        std::string v = rowValueText(row);
                        mScriptEditOldValue = v.empty() ? std::string("blank") : v;
                    }
                }
            }
        }
        if (mScriptEditModePrev && !editingNow && !mScriptEditKey.empty())
        {
            const size_t cursor = mA11y.currentIndex();
            buildAccessibilityElements(/*announceSelection=*/false);
            if (cursor != A11y::Screen::npos && cursor < mA11y.size())
                mA11y.selectIndex(cursor, /*announce=*/false);
            // Announce the committed option (label + value) once it settles.
            // exitEditMode stayed silent for this async field, so the watch is
            // responsible for the feedback -- and it announces even on a no-op
            // (invalid / unchanged text) so escaping is never met with silence.
            watchScriptValue(
                mScriptEditGroup, mScriptEditKey, mScriptEditOldValue, /*announceOption=*/true);
            mScriptEditGroup.clear();
            mScriptEditKey.clear();
            mScriptEditOldValue.clear();
        }
        mScriptEditModePrev = editingNow;

        // Drive spoken editing feedback for the script search box and any
        // editable fields on the current Lua settings page.
        mScriptFilterEdit.onFrame();
        for (A11y::EditField& field : mScriptPageEdits)
            field.onFrame();

        // The selected mod changed (Left/Right on the "Mod" option): the page
        // below changed, so rebuild the Scripts-tab options to match. Deferred
        // to onFrame so we don't rebuild the option list from inside the change
        // callback that's still walking it. Restore the cursor to the same
        // option WITHOUT re-announcing: changeValue() already spoke the new mod
        // name, and re-announcing here would double-speak label + value.
        if (mRebuildScriptsA11y)
        {
            mRebuildScriptsA11y = false;
            const size_t cursor = mA11y.currentIndex();
            buildAccessibilityElements(/*announceSelection=*/false);
            if (cursor != A11y::Screen::npos && cursor < mA11y.size())
                mA11y.selectIndex(cursor, /*announce=*/false);
        }

        // Poll a pending async value change (e.g. a global checkbox toggled via
        // a global-event round-trip) and announce the new value once it has
        // actually settled, so we never speak the stale pre-change value.
        if (mScriptValueWatchActive)
        {
            mScriptValueWatchTimer += duration;
            const bool stillOnOption = (mA11y.currentIndex() == mScriptValueWatchIndex);
            std::string live;
            if (MyGUI::Widget* row = findScriptRow(mScriptValueWatchGroup, mScriptValueWatchKey))
            {
                std::string v = rowValueText(row);
                live = v.empty() ? std::string("blank") : v;
            }
            // Settled: the value changed from what it was before the toggle.
            if (!live.empty() && live != mScriptValueWatchOldValue)
            {
                mScriptValueWatchActive = false;
                if (stillOnOption)
                {
                    if (mScriptValueWatchAnnounceOption)
                        // After editing a number field: re-speak the whole option
                        // (label + new value), QUEUED so it doesn't clobber any
                        // in-progress speech (exitEditMode stayed silent for us).
                        mA11y.announceCurrent();
                    else
                        // A checkbox/select change: terse value only, interrupting
                        // so a rapid re-toggle replaces a stale announcement.
                        A11y::say(live, /*interrupt=*/true);
                }
            }
            // Give up after a short window (or if the user navigated away): the
            // change may have been a no-op (e.g. already at min/max, or invalid
            // text reverted) or the row is gone.
            else if (mScriptValueWatchTimer > 1.0f || !stillOnOption)
            {
                mScriptValueWatchActive = false;
                // For a number-field edit, the user just left edit mode and
                // exitEditMode stayed silent expecting us to speak -- so even on
                // a no-op (value unchanged / reverted) announce the option once
                // (queued) so escaping always gives feedback, never silence.
                if (mScriptValueWatchAnnounceOption && stillOnOption)
                    mA11y.announceCurrent();
            }
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

    namespace
    {
        // First non-empty text caption anywhere in a subtree. Used to read a
        // control's state text (e.g. a checkbox's Yes/No lives in a child) and
        // a row's title (the first LuaText under the row's title layout).
        std::string firstCaptionInSubtree(MyGUI::Widget* root)
        {
            if (!root)
                return {};
            if (auto* tb = root->castType<MyGUI::TextBox>(false))
            {
                std::string cap = tb->getCaption();
                if (!cap.empty())
                    return cap;
            }
            MyGUI::EnumeratorWidgetPtr it = root->getEnumerator();
            while (it.next())
            {
                std::string cap = firstCaptionInSubtree(it.current());
                if (!cap.empty())
                    return cap;
            }
            return {};
        }

        // A Lua widget is interactive if it registered a mouseClick callback
        // (checkbox boxes, select arrows, the per-group Reset box).
        bool isLuaClickable(MyGUI::Widget* widget)
        {
            auto* ext = dynamic_cast<LuaUi::WidgetExtension*>(widget);
            return ext && ext->hasEventCallback("mouseClick");
        }

        // Find the first clickable control and/or editable text box anywhere in
        // a setting row's subtree (skipping the title/description text), so a
        // row can be classified as checkbox / select / text-field regardless of
        // the exact renderer nesting.
        void findRowControl(MyGUI::Widget* root, MyGUI::Widget*& clickable, MyGUI::EditBox*& editable,
            std::vector<MyGUI::Widget*>& clickables)
        {
            MyGUI::EnumeratorWidgetPtr it = root->getEnumerator();
            while (it.next())
            {
                MyGUI::Widget* w = it.current();
                if (isLuaClickable(w))
                {
                    clickables.push_back(w);
                    if (!clickable)
                        clickable = w;
                }
                if (auto* eb = w->castType<MyGUI::EditBox>(false); eb && !eb->getEditStatic() && !editable)
                    editable = eb;
                findRowControl(w, clickable, editable, clickables);
            }
        }

        // Depth-first search for a descendant with the given widget name.
        MyGUI::Widget* findByName(MyGUI::Widget* root, std::string_view name)
        {
            if (!root)
                return nullptr;
            MyGUI::EnumeratorWidgetPtr it = root->getEnumerator();
            while (it.next())
            {
                MyGUI::Widget* w = it.current();
                if (w->getName() == name)
                    return w;
                if (MyGUI::Widget* hit = findByName(w, name))
                    return hit;
            }
            return nullptr;
        }

        // Find a group's Reset button: the clickable widget in the group header
        // (a sibling of the "settings" flex, NOT inside it). Returns nullptr if
        // absent. Used to re-resolve it live, since it carries no stable name.
        MyGUI::Widget* findGroupReset(MyGUI::Widget* group)
        {
            MyGUI::EnumeratorWidgetPtr it = group->getEnumerator();
            while (it.next())
            {
                MyGUI::Widget* w = it.current();
                if (w->getName() == "settings")
                    continue; // the settings rows themselves, not the reset box
                if (isLuaClickable(w))
                    return w;
                if (MyGUI::Widget* hit = findGroupReset(w))
                    return hit;
            }
            return nullptr;
        }

        // First non-empty caption (TextBox or EditBox) in a subtree, in
        // document order. Like firstCaptionInSubtree but also reads EditBoxes
        // (used for editable field contents and select value labels).
        std::string firstAnyCaption(MyGUI::Widget* root)
        {
            MyGUI::EnumeratorWidgetPtr it = root->getEnumerator();
            while (it.next())
            {
                MyGUI::Widget* c = it.current();
                std::string cap;
                if (auto* tb = c->castType<MyGUI::TextBox>(false))
                    cap = tb->getCaption();
                else if (auto* eb = c->castType<MyGUI::EditBox>(false))
                    cap = eb->getCaption();
                if (!cap.empty())
                    return cap;
                if (std::string deep = firstAnyCaption(c); !deep.empty())
                    return deep;
            }
            return {};
        }

        // The current value (state) text of a setting row. A row is laid out as
        // [titleLayout, interval, control] (see menu.lua renderSetting): the
        // title and description live in the FIRST child (titleLayout), while the
        // control's state -- a checkbox's Yes/No, a select's value label, or an
        // editable field's contents -- is in a LATER child. So skip the first
        // child and return the first caption found among the rest.
        std::string rowValueText(MyGUI::Widget* row)
        {
            MyGUI::EnumeratorWidgetPtr it = row->getEnumerator();
            bool first = true;
            while (it.next())
            {
                MyGUI::Widget* c = it.current();
                if (first)
                {
                    first = false; // titleLayout: holds title + description, not the value
                    continue;
                }
                if (std::string cap = firstAnyCaption(c); !cap.empty())
                    return cap;
            }
            return {};
        }
    }

    MyGUI::Widget* SettingsWindow::findScriptRow(const std::string& groupName, const std::string& settingKey) const
    {
        MyGUI::Widget* group = findByName(mScriptAdapter, groupName);
        if (!group)
            return nullptr;
        return findByName(group, settingKey);
    }

    void SettingsWindow::watchScriptValue(const std::string& groupName, const std::string& settingKey,
        const std::string& oldValue, bool announceOption)
    {
        mScriptValueWatchActive = true;
        mScriptValueWatchGroup = groupName;
        mScriptValueWatchKey = settingKey;
        mScriptValueWatchOldValue = oldValue;
        mScriptValueWatchAnnounceOption = announceOption;
        mScriptValueWatchIndex = mA11y.currentIndex();
        mScriptValueWatchTimer = 0.f;
    }

    void SettingsWindow::emitLuaSettingRow(
        MyGUI::Widget* row, const std::string& groupName, const std::string& section)
    {
        // A setting row is: a title layout (title LuaText [+ description EditBox])
        // then a control. The control is a clickable container (checkbox =>
        // toggle; select => two clickable arrows around a value label) or an
        // editable EditBox (textLine / number / color renderer).
        //
        // CRITICAL: changing a value makes the Lua layer synchronously DESTROY
        // and recreate this row's widgets (storage subscribe -> renderSetting).
        // So we must NOT capture any widget pointer in the option's callbacks --
        // they would dangle the instant the user toggles a checkbox. Instead we
        // capture the stable widget NAMES (group name + setting key, which Lua
        // preserves across the rebuild) and re-resolve the live row on demand.
        const std::string settingKey(row->getName());

        // The row is laid out as [titleLayout, interval, control] (menu.lua
        // renderSetting). The title and description live ENTIRELY in the first
        // child (titleLayout): title = its first text, description = any further
        // text. We must NOT scan the whole row here, or the control's own state
        // text (a checkbox's Yes/No, a select's value label) would be swept into
        // the description -- which is exactly what made the tooltip read the
        // state and the state get double-spoken.
        std::vector<std::string> texts;
        std::function<void(MyGUI::Widget*)> gatherText = [&](MyGUI::Widget* w) {
            MyGUI::EnumeratorWidgetPtr it = w->getEnumerator();
            while (it.next())
            {
                MyGUI::Widget* c = it.current();
                std::string cap;
                if (auto* tb = c->castType<MyGUI::TextBox>(false))
                    cap = tb->getCaption();
                else if (auto* eb = c->castType<MyGUI::EditBox>(false); eb && eb->getEditStatic())
                    cap = eb->getCaption();
                if (!cap.empty())
                    texts.push_back(cap);
                gatherText(c);
            }
        };
        if (MyGUI::EnumeratorWidgetPtr rowIt = row->getEnumerator(); rowIt.next())
            gatherText(rowIt.current()); // first child only = the title layout

        std::string title = texts.empty() ? std::string() : texts.front();
        std::string description;
        for (size_t i = 1; i < texts.size(); ++i)
        {
            if (!description.empty())
                description += " ";
            description += texts[i];
        }
        if (title.empty())
            title = settingKey.empty() ? std::string("Setting") : settingKey;

        // Classify the control ONCE (the renderer type doesn't change across
        // value edits): count clickables and detect an editable box.
        MyGUI::Widget* clickable = nullptr;
        MyGUI::EditBox* editable = nullptr;
        std::vector<MyGUI::Widget*> clickables;
        findRowControl(row, clickable, editable, clickables);
        const size_t clickableCount = clickables.size();
        const bool hasEditable = editable != nullptr;

        A11y::Element element;
        element.widget = nullptr;
        element.label = title;
        element.section = section;
        if (!description.empty())
            element.tooltips = [description] { return std::vector<std::string>{ description }; };

        // Live value: re-resolve the row by name, then read its control state
        // (the text outside the title layout).
        element.value = [this, groupName, settingKey] {
            MyGUI::Widget* live = findScriptRow(groupName, settingKey);
            if (!live)
                return std::string("unavailable");
            std::string val = rowValueText(live);
            return val.empty() ? std::string("blank") : val;
        };

        // The control's value is applied asynchronously for GLOBAL settings
        // (core.sendGlobalEvent round-trip), so the value read right after a
        // toggle is stale. We don't cache widget pointers (value/change always
        // re-resolve the row by name), so we never need to rebuild the option
        // list on a value change; instead we suppress changeValue's immediate
        // (stale) speak via asyncValue and announce the settled value via the
        // onFrame watch. Player settings settle on the next frame, so the same
        // path serves both.
        element.asyncValue = true;

        if (clickableCount >= 2)
        {
            // select renderer: value label flanked by left/right clickable
            // arrows. Left/Right click the matching arrow (re-resolved live).
            element.change = [this, groupName, settingKey](bool next) {
                MyGUI::Widget* live = findScriptRow(groupName, settingKey);
                if (!live)
                    return;
                MyGUI::Widget* clk = nullptr;
                MyGUI::EditBox* edt = nullptr;
                std::vector<MyGUI::Widget*> clks;
                findRowControl(live, clk, edt, clks);
                if (clks.size() < 2)
                    return;
                const std::string oldValue = rowValueText(live);
                MyGUI::Widget* arrow = next ? clks.back() : clks.front();
                arrow->eventMouseButtonClick(arrow);
                watchScriptValue(groupName, settingKey, oldValue.empty() ? std::string("blank") : oldValue);
            };
        }
        else if (clickableCount == 1 && !hasEditable)
        {
            // checkbox: one clickable container; Left/Right and Enter toggle it.
            auto toggle = [this, groupName, settingKey] {
                MyGUI::Widget* live = findScriptRow(groupName, settingKey);
                if (!live)
                    return;
                MyGUI::Widget* clk = nullptr;
                MyGUI::EditBox* edt = nullptr;
                std::vector<MyGUI::Widget*> clks;
                findRowControl(live, clk, edt, clks);
                if (!clk)
                    return;
                const std::string oldValue = rowValueText(live);
                clk->eventMouseButtonClick(clk);
                watchScriptValue(groupName, settingKey, oldValue.empty() ? std::string("blank") : oldValue);
            };
            element.change = [toggle](bool /*next*/) { toggle(); };
            element.activate = toggle;
        }
        else if (hasEditable)
        {
            // number / textLine renderer: a free-form editable box that commits
            // on focus-loss (Lua validates the text, then set()s it). Enter ->
            // edit mode: keystrokes go to the box with spoken feedback; Escape
            // exits, which drops focus and fires the Lua commit + a re-render.
            //
            // We bind an EditField to the LIVE box for this build. The address
            // is stable (deque), but the box itself is destroyed when the commit
            // re-renders the row, so we must rebuild the page a11y afterwards to
            // re-attach to the fresh box -- see the focusLoss commit watch below.
            A11y::EditField& field = mScriptPageEdits.emplace_back();
            field.attach(editable);
            field.setActive(false);
            element.edit = &field;
            // Map this box to its setting so onFrame can tell which number field
            // entered edit mode (and capture its pre-edit value for the settle
            // announcement). The commit-driven rebuild is handled centrally when
            // edit mode ends (see onFrame's edit-mode tracking).
            mScriptEditFieldKeys[editable] = { groupName, settingKey };
        }
        // else: an info-only row (no control) -- just the title + value.

        mA11y.add(std::move(element));
    }

    void SettingsWindow::collectLuaPageWidgets(
        MyGUI::Widget* root, const std::string& groupName, const std::string& section)
    {
        if (!root)
            return;

        MyGUI::EnumeratorWidgetPtr it = root->getEnumerator();
        while (it.next())
        {
            MyGUI::Widget* current = it.current();
            if (!current->getVisible())
                continue;

            const std::string name(current->getName());

            // A settings container: each direct child is one setting row. The
            // row's own name is the setting key; combined with the enclosing
            // group name it uniquely (and stably) identifies the row.
            if (name == "settings")
            {
                MyGUI::EnumeratorWidgetPtr rows = current->getEnumerator();
                while (rows.next())
                {
                    MyGUI::Widget* row = rows.current();
                    if (!row->getVisible())
                        continue;
                    // Named rows are real settings; the unnamed flexes between
                    // them are separator lines (skip those).
                    if (!row->getName().empty())
                        emitLuaSettingRow(row, groupName, section);
                }
                continue; // handled the whole settings block
            }

            // A group flex: its title becomes the section for the rows within,
            // and its name is the stable handle used to re-resolve those rows.
            if (name.rfind("global_", 0) == 0 || name.rfind("player_", 0) == 0)
            {
                std::string groupTitle = firstCaptionInSubtree(current);
                collectLuaPageWidgets(current, name, groupTitle);
                // Offer the group's Reset (restore-defaults) box last, so it
                // reads e.g. "Movement: Reset". Re-resolved live by group name
                // because clicking it re-renders the whole group.
                if (MyGUI::Widget* reset = findGroupReset(current))
                {
                    std::string resetLabel = firstCaptionInSubtree(reset);
                    if (resetLabel.empty())
                        resetLabel = "Reset";
                    const std::string grp = name;
                    mA11y.add({ .widget = nullptr,
                        .label = resetLabel,
                        .section = groupTitle,
                        .activate =
                            [this, grp] {
                                MyGUI::Widget* group = findByName(mScriptAdapter, grp);
                                MyGUI::Widget* live = group ? findGroupReset(group) : nullptr;
                                if (!live)
                                    return;
                                live->eventMouseButtonClick(live);
                                // Resetting re-renders every row in the group;
                                // rebuild so no captured pointer dangles.
                                mRebuildScriptsA11y = true;
                            } });
                }
                continue;
            }

            // Otherwise keep descending (page root, "groups" flex, title layouts).
            collectLuaPageWidgets(current, groupName, section);
        }
    }

    void SettingsWindow::buildAccessibilityElements(bool announceSelection)
    {
        mA11y.clear();
        // Drop EditFields bound to the previous build's (now-cleared) Lua-page
        // widgets; they're recreated by collectLuaPageWidgets if needed. Safe
        // because mA11y.clear() above just dropped every Element referencing
        // them, and the screen is not in edit mode during a rebuild.
        mScriptPageEdits.clear();
        mScriptEditFieldKeys.clear();

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

        // The Scripts tab is a mod-defined LuaUi page the generic engine-widget
        // walker can't read; it gets a bespoke path (mod switcher + Lua walker).
        if (isScriptsTabSelected())
        {
            buildScriptSwitcherA11y();
            collectLuaPageWidgets(mScriptAdapter, /*groupName=*/std::string(), /*section=*/std::string());
        }
        else if (contentRoot)
        {
            collectSettingWidgets(contentRoot);
        }

        // The OK button lives outside the tab control; always offer it last.
        registerSettingWidget(mOkButton);

        mA11y.focusFirst(announceSelection);
    }

    bool SettingsWindow::isScriptsTabSelected() const
    {
        if (!mSettingsTab || !mScriptBox)
            return false;
        MyGUI::TabItem* outer = mSettingsTab->getItemSelected();
        if (!outer)
            return false;
        // The Scripts TabItem is the one hosting the script widgets. Detect it
        // structurally (mScriptBox is a descendant) rather than by tab index or
        // localized caption, so it stays correct if tabs are reordered.
        for (MyGUI::Widget* w = mScriptBox; w != nullptr; w = w->getParent())
        {
            if (w == outer)
                return true;
        }
        return false;
    }

    void SettingsWindow::buildScriptSwitcherA11y()
    {
        // When no Lua settings pages exist (e.g. no game loaded), the engine
        // shows only the "scripts disabled" notice. Surface that as a read-only
        // line so a blind user isn't met with silence.
        if (mScriptDisabled && mScriptDisabled->getVisible())
        {
            auto* tb = mScriptDisabled->castType<MyGUI::TextBox>(false);
            std::string note = tb ? std::string(tb->getCaption()) : std::string("#{OMWEngine:ScriptsDisabled}");
            mA11y.add({ .widget = nullptr, .label = note });
            return;
        }

        // Search filter: an editable field that re-filters the mod list as you
        // type (handled in onFrame via mRebuildScriptsA11y).
        // NB: "Search" / "Mod" are hardcoded English (the settings l10n context
        // has no key for them yet); part of the post-beta localization epic.
        mA11y.add({ .widget = nullptr,
            .label = "Search",
            .value =
                [this] {
                    const std::string text = mScriptFilter->getOnlyText().asUTF8();
                    return text.empty() ? std::string("blank") : text;
                },
            .edit = &mScriptFilterEdit });

        // Mod list: Left/Right step through the matching pages, switching which
        // mod's settings page is shown below (fires onScriptListSelection so the
        // adapter re-attaches the chosen page). The value reports the selection.
        if (mScriptList && mScriptList->getItemCount() > 0)
        {
            // Nothing is shown until a page is attached; the mouse UI relies on
            // the user clicking a list row. For keyboard/a11y, auto-select the
            // first mod so the page below is populated and walkable on arrival.
            if (mScriptList->getIndexSelected() == MyGUI::ITEM_NONE)
            {
                mScriptList->setIndexSelected(0);
                onScriptListSelection(mScriptList, 0);
            }
            mA11y.add({ .widget = nullptr,
                .label = "Mod",
                .value =
                    [this] {
                        const size_t pos = mScriptList->getIndexSelected();
                        if (pos != MyGUI::ITEM_NONE && pos < mScriptList->getItemCount())
                            return std::string(mScriptList->getItemNameAt(pos));
                        return std::string("none");
                    },
                .change = [this](bool next) {
                    const size_t count = mScriptList->getItemCount();
                    if (count == 0)
                        return;
                    size_t cur = mScriptList->getIndexSelected();
                    if (cur == MyGUI::ITEM_NONE)
                        cur = 0;
                    const size_t pos = next ? (cur + 1) % count : (cur + count - 1) % count;
                    if (pos == cur)
                        return;
                    mScriptList->setIndexSelected(pos);
                    mScriptList->beginToItemAt(pos);
                    // Switch the displayed page, then rebuild so the page's own
                    // controls (below the switcher) reflect the new mod.
                    onScriptListSelection(mScriptList, pos);
                    mRebuildScriptsA11y = true;
                } });
        }
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

        // Spoken editing feedback for the script search box. Inactive until the
        // user enters edit mode on that option (mirrors the inventory filter).
        mScriptFilterEdit.attach(mScriptFilter);
        mScriptFilterEdit.setActive(false);

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
