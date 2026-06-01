#include "uimanager.hpp"

namespace MWGui::A11y
{
    UiManager& UiManager::instance()
    {
        static UiManager sInstance;
        return sInstance;
    }
}
