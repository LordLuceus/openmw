#ifndef MWGUI_TRAININGWINDOW_H
#define MWGUI_TRAININGWINDOW_H

#include "referenceinterface.hpp"
#include "timeadvancer.hpp"
#include "waitdialog.hpp"
#include "windowbase.hpp"

#include "accessibility/screen.hpp"

namespace MWMechanics
{
    class NpcStats;
}

namespace MWGui
{

    class TrainingWindow : public WindowBase, public ReferenceInterface
    {
    public:
        TrainingWindow();

        void onOpen() override;
        void onClose() override;

        bool exit() override;

        void setPtr(const MWWorld::Ptr& actor) override;

        void onFrame(float dt) override;

        WindowBase* getProgressBar() { return &mProgressBar; }

        void clear() override { resetReference(); }

        std::string_view getWindowIdForLua() const override { return "Training"; }

    protected:
        void onReferenceUnavailable() override;

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onTrainingSelected(MyGUI::Widget* sender);

        void onTrainingProgressChanged(int cur, int total);
        void onTrainingFinished();

        // Retrieve the base skill value if the setting 'training skills based on base skill' is set;
        // otherwise returns the modified skill
        float getSkillForTraining(const MWMechanics::NpcStats& stats, ESM::RefId id) const;

        MyGUI::Widget* mTrainingOptions;
        MyGUI::Button* mCancelButton;
        MyGUI::TextBox* mPlayerGold;
        std::vector<MyGUI::Button*> mTrainingButtons;

        WaitDialogProgressBar mProgressBar;
        TimeAdvancer mTimeAdvancer;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        size_t mControllerFocus = 0;

        // Screen-reader controller. Virtual focus via an invisible anchor: the
        // skill buttons are rebuilt every setPtr() and we navigate them as
        // widget-backed options so their native skill tooltips work (T key).
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;
        void buildAccessibility();
    };

}

#endif
