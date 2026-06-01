#ifndef MWGUI_SETTINGS_H
#define MWGUI_SETTINGS_H

#include <MyGUI_KeyCode.h>
#include <MyGUI_Types.h>

#include <components/files/configurationmanager.hpp>
#include <components/lua_ui/adapter.hpp>

#include "accessibility/screen.hpp"
#include "windowbase.hpp"

namespace MWGui
{
    class SettingsWindow : public WindowBase
    {
    public:
        SettingsWindow(Files::ConfigurationManager& cfgMgr);

        void onOpen() override;

        void onClose() override;

        void onFrame(float duration) override;

        void updateControlsBox();

        void updateLightSettings();

        void updateVSyncModeSettings();

        void updateWindowModeSettings();

        void onResChange(int, int) override;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    protected:
        MyGUI::TabControl* mSettingsTab;
        MyGUI::Button* mOkButton;

        // graphics
        MyGUI::ListBox* mResolutionList;
        MyGUI::ComboBox* mWindowModeList;
        MyGUI::ComboBox* mVSyncModeList;
        MyGUI::Button* mWindowBorderButton;
        MyGUI::ComboBox* mTextureFilteringButton;

        MyGUI::Button* mWaterRefractionButton;
        MyGUI::Button* mSunlightScatteringButton;
        MyGUI::Button* mWobblyShoresButton;
        MyGUI::ComboBox* mWaterTextureSize;
        MyGUI::ComboBox* mWaterReflectionDetail;
        MyGUI::ComboBox* mWaterRainRippleDetail;

        MyGUI::ComboBox* mMaxLights;
        MyGUI::ComboBox* mLightingMethodButton;
        MyGUI::Button* mLightsResetButton;

        MyGUI::ComboBox* mPrimaryLanguage;
        MyGUI::ComboBox* mSecondaryLanguage;
        MyGUI::Button* mGmstOverridesL10n;

        MyGUI::Widget* mWindowModeHint;

        // controls
        MyGUI::ScrollView* mControlsBox;
        MyGUI::Button* mResetControlsButton;
        MyGUI::Button* mKeyboardSwitch;
        MyGUI::Button* mControllerSwitch;
        bool mKeyboardMode; // if true, setting up the keyboard. Otherwise, it's controller

        MyGUI::EditBox* mScriptFilter;
        MyGUI::ListBox* mScriptList;
        MyGUI::Widget* mScriptBox;
        MyGUI::Widget* mScriptDisabled;
        MyGUI::ScrollView* mScriptView;
        LuaUi::LuaAdapter* mScriptAdapter;
        size_t mCurrentPage;

        void onTabChanged(MyGUI::TabControl* sender, size_t index);
        void onOkButtonClicked(MyGUI::Widget* sender);
        void onTextureFilteringChanged(MyGUI::ComboBox* sender, size_t pos);
        void onSliderChangePosition(MyGUI::ScrollBar* scroller, size_t pos);
        void onButtonToggled(MyGUI::Widget* sender);
        void onResolutionSelected(MyGUI::ListBox* sender, size_t index);
        void onResolutionAccept();
        void onResolutionCancel();
        void highlightCurrentResolution();

        void onRefractionButtonClicked(MyGUI::Widget* sender);
        void onWaterTextureSizeChanged(MyGUI::ComboBox* sender, size_t pos);
        void onWaterReflectionDetailChanged(MyGUI::ComboBox* sender, size_t pos);
        void onWaterRainRippleDetailChanged(MyGUI::ComboBox* sender, size_t pos);

        void onLightingMethodButtonChanged(MyGUI::ComboBox* sender, size_t pos);
        void onLightsResetButtonClicked(MyGUI::Widget* sender);
        void onMaxLightsChanged(MyGUI::ComboBox* sender, size_t pos);

        void onPrimaryLanguageChanged(MyGUI::ComboBox* sender, size_t pos) { onLanguageChanged(0, sender, pos); }
        void onSecondaryLanguageChanged(MyGUI::ComboBox* sender, size_t pos) { onLanguageChanged(1, sender, pos); }
        void onLanguageChanged(size_t langPriority, MyGUI::ComboBox* sender, size_t pos);
        void onGmstOverridesL10nChanged(MyGUI::Widget* sender);

        void onWindowModeChanged(MyGUI::ComboBox* sender, size_t pos);
        void onVSyncModeChanged(MyGUI::ComboBox* sender, size_t pos);

        void onRebindAction(MyGUI::Widget* sender);
        void onInputTabMouseWheel(MyGUI::Widget* sender, int rel);
        void onResetDefaultBindings(MyGUI::Widget* sender);
        void onResetDefaultBindingsAccept();
        void onKeyboardSwitchClicked(MyGUI::Widget* sender);
        void onControllerSwitchClicked(MyGUI::Widget* sender);

        void onWindowResize(MyGUI::Window* sender);

        void onScriptFilterChange(MyGUI::EditBox*);
        void onScriptListSelection(MyGUI::ListBox*, size_t index);

        // Screen-reader framework. The window is navigated entirely through
        // the shared A11y::Screen in virtual-focus mode: a single hidden anchor
        // widget keeps MyGUI key focus, while the controller tracks the current
        // option internally so native ListBox / ComboBox / ScrollBar widgets
        // never receive focus or eat our arrow keys.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        // When true, the existing combo/slider/toggle change handlers skip
        // their own speech: the A11y framework speaks the new value once
        // instead, avoiding double-announcements during keyboard navigation.
        bool mSuppressSettingSpeech = false;

        // (Re)build the option list for the currently selected tab and focus
        // its first option. \p announceSelection controls whether that first
        // option is spoken (false right after the tab name was just announced).
        void buildAccessibilityElements(bool announceSelection);
        // Register one settings widget (slider / combo / list / checkbox /
        // button) as an A11y option, deriving its label, value and change
        // behaviour from its type and UserStrings.
        void registerSettingWidget(MyGUI::Widget* widget);
        // Recursively walk \p root registering every settings widget found.
        void collectSettingWidgets(MyGUI::Widget* root);
        // Register the dynamic key-binding list as description/binding pairs.
        void registerControlsBox();
        // Switch the visible settings tab by \p delta (Tab / Shift+Tab) and
        // rebuild the option list.
        void cycleTab(int delta);
        std::string resolveAccessibilityLabel(MyGUI::Widget* widget) const;
        // Speak / compute the current value text for a slider / combo / list.
        std::string settingValueText(MyGUI::Widget* widget) const;
        // Adjust a slider / combo / list value by one step in \p next direction,
        // reusing the existing change handlers.
        void changeSettingValue(MyGUI::Widget* widget, bool next);

        void apply();

        void configureWidgets(MyGUI::Widget* widget, bool init);
        MyGUI::TextBox* getSliderLabel(MyGUI::ScrollBar* scroller) const;

        void layoutControlsBox();
        void renderScriptSettings();

        void computeMinimumWindowSize();

    private:
        void resetScrollbars();
        Files::ConfigurationManager& mCfgMgr;
    };
}

#endif
