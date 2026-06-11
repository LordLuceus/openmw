#ifndef MWGUI_SETTINGS_H
#define MWGUI_SETTINGS_H

#include <deque>
#include <map>

#include <MyGUI_KeyCode.h>
#include <MyGUI_Types.h>

#include <components/files/configurationmanager.hpp>
#include <components/lua_ui/adapter.hpp>

#include "accessibility/editfield.hpp"
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

        // True when the currently selected outer tab is the Scripts (Lua-mod
        // settings) tab. The Scripts tab is built from a mod-defined LuaUi
        // widget tree the generic settings-widget walker can't read, so it gets
        // its own a11y enrolment path (mod switcher + page walker).
        bool isScriptsTabSelected() const;
        // Register the Scripts tab's mod switcher: the search filter (editable)
        // and the mod list (Left/Right picks which mod's page is shown).
        void buildScriptSwitcherA11y();
        // Walk the mod-defined LuaUi page attached to mScriptAdapter. The page
        // is built by the engine's builtin settings renderer (menu.lua), so it
        // has a known scaffold: a "groups" flex -> group flexes (named
        // "global_<key>" / "player_<key>") -> a "settings" flex -> one row flex
        // per setting (named with the setting key). We anchor on those fixed
        // names and emit ONE a11y option per setting row, using the group title
        // as the option's section. \p section is the current group title.
        void collectLuaPageWidgets(MyGUI::Widget* root, const std::string& groupName, const std::string& section);
        // Emit a single a11y option for one setting row: title -> label,
        // control state -> value, description -> tooltip, group -> section.
        // \p groupName is the row's group widget name (e.g. "global_Settings_x")
        // and \p row->getName() is the setting key; both are stable across the
        // synchronous widget destroy/rebuild a value change triggers, so the
        // option's callbacks re-resolve the live widget by name (never caching a
        // pointer that a toggle would dangle).
        void emitLuaSettingRow(MyGUI::Widget* row, const std::string& groupName, const std::string& section);
        // Re-find the live setting-row widget for (groupName, settingKey) under
        // the current Lua page, or nullptr if it is gone. Cheap; called only on
        // focus / interaction, never per frame.
        MyGUI::Widget* findScriptRow(const std::string& groupName, const std::string& settingKey) const;
        // After the user switches mods (Left/Right on the "Mod" option), the
        // displayed page changes, so the Scripts-tab a11y options below the
        // switcher must be rebuilt to reflect the new page. Deferred to onFrame
        // (like mRebuildControlsA11y) so we don't rebuild the option list from
        // inside the key/change callback that's still walking it.
        bool mRebuildScriptsA11y = false;

        // Pending "announce the value once it settles" watch for a Scripts-tab
        // option whose change is applied ASYNCHRONOUSLY. A global Lua setting is
        // written via core.sendGlobalEvent, which round-trips through the global
        // script context and only re-renders the row some frames later -- so the
        // value read immediately after the toggle is the STALE pre-change value
        // (this caused checkboxes to announce one state behind). Instead of
        // reading at a fixed time, we poll the option's live value each frame
        // until it differs from the pre-change value (or a timeout elapses) and
        // then speak it once. Player settings re-render synchronously, so for
        // them the value differs on the very next frame -- the same path works
        // for both. The (group, key) re-resolve the row by name; the index
        // guards against the user navigating away mid-wait.
        // Tracks whether the a11y screen was in text-edit mode last frame, so
        // onFrame can detect the Escape that ENDS editing a Scripts-tab number
        // field. The commit (Lua focusLoss -> set -> re-render) destroys the box
        // the EditField was bound to, so on that transition we rebuild the page
        // a11y (re-attaching to the fresh box) and re-announce the settled value.
        bool mScriptEditModePrev = false;
        // The (group,key,oldValue) of the number field being edited, captured on
        // entering edit mode so the post-commit rebuild can announce the new
        // value once it settles (same async path as a checkbox toggle).
        std::string mScriptEditGroup;
        std::string mScriptEditKey;
        std::string mScriptEditOldValue;
        // Maps each editable Scripts-tab box (an EditField's widget) to its
        // setting (group, key), populated as the page is walked. Lets onFrame
        // identify which setting just entered edit mode from the current edit
        // box. Rebuilt with the page; pointers are only compared, never
        // dereferenced (the box may be destroyed by a commit before lookup).
        std::map<MyGUI::Widget*, std::pair<std::string, std::string>> mScriptEditFieldKeys;

        bool mScriptValueWatchActive = false;
        std::string mScriptValueWatchGroup;
        std::string mScriptValueWatchKey;
        std::string mScriptValueWatchOldValue;
        std::size_t mScriptValueWatchIndex = static_cast<std::size_t>(-1);
        float mScriptValueWatchTimer = 0.f;
        // When true, the settle announcement re-speaks the WHOLE option (label +
        // value), queued -- used after editing a number field, where exitEditMode
        // deliberately stays silent so this isn't clobbered, and the user expects
        // to hear the option label again. When false (a checkbox/select change),
        // only the terse new value is spoken.
        bool mScriptValueWatchAnnounceOption = false;
        // Begin watching the given setting row's value for an async-applied
        // change; speaks the new value once it settles. \\p oldValue is the row's
        // value text captured immediately BEFORE the change was triggered. When
        // \\p announceOption is true, the whole option (label + value) is spoken
        // (queued) on settle instead of just the value.
        void watchScriptValue(const std::string& groupName, const std::string& settingKey,
            const std::string& oldValue, bool announceOption = false);
        // Spoken feedback for the script search filter while in edit mode.
        A11y::EditField mScriptFilterEdit;
        // Spoken feedback for each editable text field on the current Lua page
        // (textLine / number / color renderers). Recreated on every Scripts-tab
        // rebuild because the mod-defined widget tree is destroyed and rebuilt
        // when the page or filter changes; a deque keeps stable addresses for
        // the A11y::Element pointers that reference these for the build's life.
        std::deque<A11y::EditField> mScriptPageEdits;

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

        // Key-rebinding via the keyboard needs care. The user starts a rebind by
        // pressing Enter on a binding row; if we armed ICS detection right then,
        // ICS would capture that still-held Enter (key-repeat) as "Return", the
        // bind would complete, the row would re-enable, and the held Enter would
        // re-trigger the rebind -- an endless loop. We instead remember the
        // requested action here and let onFrame arm detection only once the
        // activation key (Enter / Space) is physically released. -1 means
        // "nothing pending".
        int mPendingRebindAction = -1;
        // After a successful rebind, updateControlsBox() destroys and recreates
        // every controls-list widget, which dangles the widget pointers captured
        // by our A11y options (so the row's binding value stops being spoken).
        // This asks onFrame to rebuild the A11y option list and restore the
        // selection to the row we just rebound (matched by its description).
        bool mRebuildControlsA11y = false;
        std::string mLastRebindLabel;

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
