#include <components/contentselector/model/contentmodel.hpp>
#include <components/contentselector/model/esmfile.hpp>

#include <QStringList>

#include <gtest/gtest.h>

namespace
{
    using namespace ContentSelectorModel;

    // The reorder logic added for keyboard accessibility: content files could
    // previously only be reordered by dragging. These tests pin the boundary
    // arithmetic, which is easy to get subtly wrong and produces a silently
    // wrong load order rather than an obvious failure.

    struct ContentModelTest : ::testing::Test
    {
        QIcon mWarningIcon;
        QIcon mErrorIcon;
        ContentModel mModel{ nullptr, mWarningIcon, mErrorIcon, /*showOMWScripts=*/true };

        QStringList fileNames() const
        {
            QStringList names;
            for (int row = 0; row < mModel.rowCount(); ++row)
                names.append(mModel.item(row)->fileName());
            return names;
        }
    };

    TEST_F(ContentModelTest, MoveWithNoSelectionDoesNothing)
    {
        EXPECT_FALSE(mModel.canMoveRowsBy({}, -1));
        EXPECT_FALSE(mModel.canMoveRowsBy({}, 1));
    }

    TEST_F(ContentModelTest, ZeroStepIsRejected)
    {
        EXPECT_FALSE(mModel.canMoveRowsBy({ 0 }, 0));
    }

    TEST_F(ContentModelTest, MoveAboveTheTopIsRejected)
    {
        // Row 0 cannot go up; asking for it must fail rather than clamp,
        // because a silent no-op reads identically to a broken feature.
        EXPECT_FALSE(mModel.canMoveRowsBy({ 0 }, -1));
    }

    TEST_F(ContentModelTest, MoveBelowTheBottomIsRejected)
    {
        EXPECT_FALSE(mModel.canMoveRowsBy({ 0 }, 1));
    }

    TEST_F(ContentModelTest, EmptyModelHasNoModifiableRows)
    {
        EXPECT_EQ(mModel.firstModifiableRow(), 0);
        EXPECT_EQ(mModel.rowCount(), 0);
    }
}
