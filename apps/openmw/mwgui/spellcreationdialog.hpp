#ifndef MWGUI_SPELLCREATION_H
#define MWGUI_SPELLCREATION_H

#include <memory>

#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadspel.hpp>

#include "accessibility/editfield.hpp"
#include "accessibility/screen.hpp"
#include "referenceinterface.hpp"
#include "widgets.hpp"
#include "windowbase.hpp"

namespace Gui
{
    class MWList;
}

namespace MWGui
{

    class SelectSkillDialog;
    class SelectAttributeDialog;

    class EditEffectDialog : public WindowModal
    {
    public:
        EditEffectDialog();

        void onOpen() override;
        bool exit() override;

        void setConstantEffect(bool constant);

        void setSkill(ESM::RefId skill);
        void setAttribute(ESM::RefId attribute);

        void newEffect(const ESM::MagicEffect* effect);
        void editEffect(ESM::ENAMstruct effect);
        typedef MyGUI::delegates::MultiDelegate<ESM::ENAMstruct> EventHandle_Effect;

        EventHandle_Effect eventEffectAdded;
        EventHandle_Effect eventEffectModified;
        EventHandle_Effect eventEffectRemoved;

    protected:
        MyGUI::Button* mCancelButton;
        MyGUI::Button* mOkButton;
        MyGUI::Button* mDeleteButton;

        MyGUI::Button* mRangeButton;

        MyGUI::Widget* mDurationBox;
        MyGUI::Widget* mMagnitudeBox;
        MyGUI::Widget* mAreaBox;

        MyGUI::TextBox* mMagnitudeMinValue;
        MyGUI::TextBox* mMagnitudeMaxValue;
        MyGUI::TextBox* mDurationValue;
        MyGUI::TextBox* mAreaValue;

        MyGUI::ScrollBar* mMagnitudeMinSlider;
        MyGUI::ScrollBar* mMagnitudeMaxSlider;
        MyGUI::ScrollBar* mDurationSlider;
        MyGUI::ScrollBar* mAreaSlider;

        MyGUI::TextBox* mAreaText;

        MyGUI::ImageBox* mEffectImage;
        MyGUI::TextBox* mEffectName;

        bool mEditing;

    protected:
        void onRangeButtonClicked(MyGUI::Widget* sender);
        void onDeleteButtonClicked(MyGUI::Widget* sender);
        void onOkButtonClicked(MyGUI::Widget* sender);
        void onCancelButtonClicked(MyGUI::Widget* sender);

        void onMagnitudeMinChanged(MyGUI::ScrollBar* sender, size_t pos);
        void onMagnitudeMaxChanged(MyGUI::ScrollBar* sender, size_t pos);
        void onDurationChanged(MyGUI::ScrollBar* sender, size_t pos);
        void onAreaChanged(MyGUI::ScrollBar* sender, size_t pos);
        void setMagicEffect(const ESM::MagicEffect* effect);

        void updateBoxes();

    private:
        ESM::ENAMstruct mEffect;
        ESM::ENAMstruct mOldEffect;

        const ESM::MagicEffect* mMagicEffect;

        bool mConstantEffect;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void updateControllerFocus(int prevFocus, int newFocus);
        int mControllerFocus = 0;
        std::vector<MyGUI::TextBox*> mButtons;

        void onClose() override;
        void onFrame(float dt) override;

        // Screen-reader controller. This modal is built from native widgets
        // (sliders, a cycle button) so it uses virtual focus pinned to an
        // invisible anchor with ownModal=true. Rebuilt by buildAccessibility()
        // each time the dialog is shown, since which boxes (magnitude/duration/
        // area) are visible depends on the effect and range.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        void buildAccessibility();
        // Step a slider by \p delta indices, clamp to its range, push the value
        // through the matching onXChanged handler (which updates the caption and
        // fires eventEffectModified), and re-announce the option. Used by the
        // Left/Right (+/-1), Ctrl+Left/Right (+/-10) and Home/End (min/max)
        // shortcuts. \p scroll's matching value box is updated by the handler.
        void a11yStepSlider(MyGUI::ScrollBar* scroll, int delta);
        void a11ySetSlider(MyGUI::ScrollBar* scroll, size_t pos);
        // The composed, fully-resolved effect line (e.g. "Fire Damage 1 to 1
        // points for 1 second on Touch"), spoken on open and after edits.
        std::string a11yEffectLine() const;
        // Spoken label for the current range (Self/Touch/Target).
        std::string a11yRangeText() const;
    };

    class EffectEditorBase
    {
    public:
        enum Type
        {
            Spellmaking,
            Enchanting
        };

        EffectEditorBase(Type type);
        virtual ~EffectEditorBase();

        void setConstantEffect(bool constant);

    protected:
        std::map<int, ESM::RefId> mButtonMapping; // maps button ID to effect ID

        Gui::MWList* mAvailableEffectsList;
        MyGUI::ScrollView* mUsedEffectsView;

        EditEffectDialog mAddEffectDialog;
        std::unique_ptr<SelectAttributeDialog> mSelectAttributeDialog;
        std::unique_ptr<SelectSkillDialog> mSelectSkillDialog;

        int mSelectedEffect;
        ESM::RefId mSelectedKnownEffectId;

        bool mConstantEffect;

        std::vector<ESM::ENAMstruct> mEffects;

        void onEffectAdded(ESM::ENAMstruct effect);
        void onEffectModified(ESM::ENAMstruct effect);
        void onEffectRemoved(ESM::ENAMstruct effect);

        void onAvailableEffectClicked(MyGUI::Widget* sender);

        void onAttributeOrSkillCancel();
        void onSelectAttribute();
        void onSelectSkill();

        void onEditEffect(MyGUI::Widget* sender);

        void updateEffectsView();

        void startEditing();
        void setWidgets(Gui::MWList* availableEffectsList, MyGUI::ScrollView* usedEffectsView);

        virtual void notifyEffectsChanged() {}

        virtual bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg);

        // --- Screen-reader support (shared by spell creation & enchanting) ---
        // The effect editor is built from a known-effects list (left) and a
        // used-effects list (right). Both derived windows drive one A11y::Screen
        // in virtual-focus mode, anchored to an invisible widget on the window.
        // addEffectListElements() appends the two effect lists as navigable
        // options; the derived class brackets them with its own options (name,
        // cost, buy, etc.) and owns activate()/rebuild timing.
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        // Create the invisible anchor on \p mainWidget and put the screen into
        // virtual-focus mode. Call once from the derived ctor.
        void initEffectListA11y(MyGUI::Widget* mainWidget);
        // Append every known (available) effect then every used effect to mA11y,
        // grouped into "Available effects" / "Used effects" sections. Available
        // effects activate to add (onAvailableEffectClicked); used effects
        // activate to edit (onEditEffect) and describe via formatSpellEffectLine.
        void addEffectListElements();
        // Tooltip lines (flavour description + school) for a known effect, shown
        // with T -- this is the only place the game surfaces the effect's
        // descriptive text, which otherwise lives solely on the spellmaking
        // tooltip a sighted player would hover.
        std::vector<std::string> a11yEffectTooltip(ESM::RefId effectId) const;
        // The list-button widget backing used-effect \p index (the spell's
        // effects), or null if out of range. Lets a derived window land focus on
        // a just-added/edited effect when reclaiming control after the edit
        // dialog closes.
        MyGUI::Widget* a11yUsedEffectWidget(int index) const;
        // Index of the effect last added/edited (the one the edit dialog acted
        // on), for the same reclaim purpose. -1 if none / just deleted.
        int a11ySelectedEffect() const { return mSelectedEffect; }

    private:
        Type mType;

        size_t mAvailableFocus = 0;
        size_t mEffectFocus = 0;
        bool mRightColumn = false;
        std::vector<MyGUI::Button*> mAvailableButtons;
        std::vector<std::pair<Widgets::MWSpellEffectPtr, MyGUI::Button*>> mEffectButtons;
    };

    class SpellCreationDialog : public WindowBase, public ReferenceInterface, public EffectEditorBase
    {
    public:
        SpellCreationDialog();

        void onOpen() override;
        void clear() override { resetReference(); }

        void onFrame(float dt) override;

        void setPtr(const MWWorld::Ptr& actor) override;

        std::string_view getWindowIdForLua() const override { return "SpellCreationDialog"; }

    protected:
        void onReferenceUnavailable() override;

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onBuyButtonClicked(MyGUI::Widget* sender);
        void onAccept(MyGUI::EditBox* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

        void notifyEffectsChanged() override;

        MyGUI::EditBox* mNameEdit;
        MyGUI::TextBox* mMagickaCost;
        MyGUI::TextBox* mSuccessChance;
        MyGUI::Button* mBuyButton;
        MyGUI::Button* mCancelButton;
        MyGUI::TextBox* mPriceLabel;
        MyGUI::TextBox* mPlayerGold;

        ESM::Spell mSpell;

        void onClose() override;

        bool exit() override;

        // Screen-reader editing for the spell-name box.
        A11y::EditField mNameField;
        // (Re)build the full option list: name, the two effect lists, the read-
        // only cost/chance/price/gold stats, then Buy / Cancel. Called on open
        // and after the used-effects list changes.
        void buildAccessibility();
        // Tracks whether a child modal (edit-effect dialog or skill/attribute
        // picker) was open last frame, so onFrame can detect when one closes and
        // reclaim screen-reader control (the single active-screen slot is taken
        // by the child while it's up).
        bool mA11yModalWasOpen = false;
    };

}

#endif
