#ifndef MWGUI_LEVELUPDIALOG_H
#define MWGUI_LEVELUPDIALOG_H

#include <components/esm/attr.hpp>

#include "windowbase.hpp"

#include "accessibility/screen.hpp"

namespace MWGui
{

    class LevelupDialog : public WindowBase
    {
    public:
        LevelupDialog();

        void onOpen() override;
        void onClose() override;
        void onFrame(float dt) override;

        std::string_view getWindowIdForLua() const override { return "LevelUpDialog"; }

    private:
        struct Widgets
        {
            MyGUI::Button* mButton;
            MyGUI::TextBox* mValue;
            MyGUI::TextBox* mMultiplier;
        };
        MyGUI::Button* mOkButton;
        MyGUI::ImageBox* mClassImage;
        MyGUI::TextBox* mLevelText;
        MyGUI::EditBox* mLevelDescription;

        MyGUI::Widget* mCoinBox;
        MyGUI::ScrollView* mAssignWidget;

        std::map<ESM::Attribute::AttributeID, Widgets> mAttributeWidgets;
        std::vector<MyGUI::ImageBox*> mCoins;

        std::vector<ESM::Attribute::AttributeID> mSpentAttributes;

        size_t mPerCol;
        unsigned int mCoinCount;

        void onOkButtonClicked(MyGUI::Widget* sender);
        void onAttributeClicked(MyGUI::Widget* sender);
        // Screen-reader wrapper around onAttributeClicked: toggles the pick and
        // announces the outcome, including when a pick at the coin limit
        // displaces a previously-selected attribute.
        void onAttributeToggled(MyGUI::Widget* button, ESM::Attribute::AttributeID id);

        void assignCoins();
        void resetCoins();

        void setAttributeValues();

        std::string_view getLevelupClassImage(
            const int combatIncreases, const int magicIncreases, const int stealthIncreases);

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        std::vector<MyGUI::Button*> mAttributeButtons;
        size_t mControllerFocus = 0;

        // Screen-reader controller. Virtual focus via an invisible anchor: the
        // attribute buttons are rebuilt each onOpen() and navigated as
        // widget-backed options so their native attribute tooltips work (T key).
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        void buildAccessibility();
        // Spoken summary of an attribute option, e.g.
        // "Strength: 40, +5 if chosen" or "Luck: 100, maxed".
        std::string attributeOptionText(ESM::Attribute::AttributeID id) const;
    };

}

#endif
