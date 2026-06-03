#ifndef GAME_MWACCESSIBILITY_CATEGORY_H
#define GAME_MWACCESSIBILITY_CATEGORY_H

namespace MWAccessibility
{
    /// Categories the screen-reader scanner can cycle through. Shared by the
    /// Scanner and the ProximityCue so neither has to include the other.
    enum class Category
    {
        Npcs = 0,
        Doors,
        Containers,
        Items,
        Activators,

        Count
    };
}

#endif
