#ifndef MWGUI_MAPWINDOW_H
#define MWGUI_MAPWINDOW_H

#include <cstdint>
#include <memory>

#include <osg/Vec2f>

#include <MyGUI_ITexture.h>

#include "windowpinnablebase.hpp"

#include "accessibility/screen.hpp"

#include <components/esm3/custommarkerstate.hpp>
#include <components/misc/constants.hpp>

namespace MWRender
{
    class GlobalMap;
    class LocalMap;
}

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace MWWorld
{
    class Cell;
    class CellStore;
}

namespace Loading
{
    class Listener;
}

namespace SceneUtil
{
    class WorkQueue;
}

namespace MWGui
{
    class MarkerWidget;

    class CustomMarkerCollection
    {
    public:
        void addMarker(const ESM::CustomMarker& marker, bool triggerEvent = true);
        void deleteMarker(const ESM::CustomMarker& marker);
        void updateMarker(const ESM::CustomMarker& marker, const std::string& newNote);

        void clear();

        size_t size() const;

        typedef std::multimap<ESM::RefId, ESM::CustomMarker> ContainerType;

        typedef std::pair<ContainerType::const_iterator, ContainerType::const_iterator> RangeType;

        ContainerType::const_iterator begin() const;
        ContainerType::const_iterator end() const;

        RangeType getMarkers(const ESM::RefId& cellId) const;

        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;
        EventHandle_Void eventMarkersChanged;

    private:
        ContainerType mMarkers;
    };

    class LocalMapBase
    {
    public:
        LocalMapBase(CustomMarkerCollection& markers, MWRender::LocalMap* localMapRender, bool fogOfWarEnabled);
        virtual ~LocalMapBase();
        void init(MyGUI::ScrollView* widget, MyGUI::ImageBox* compass, int cellDistance = Constants::CellGridRadius);

        void setActiveCell(const MWWorld::Cell& cell);
        void requestMapRender(const MWWorld::CellStore* cell);
        void setPlayerDir(const float x, const float y);
        void setPlayerPos(int cellX, int cellY, const float nx, const float ny);

        void onFrame(float dt);

        bool toggleFogOfWar();

        struct MarkerUserData
        {
            MarkerUserData(MWRender::LocalMap* map)
                : mLocalMapRender(map)
                , cellX(0)
                , cellY(0)
                , nX(0.f)
                , nY(0.f)
            {
            }

            bool isPositionExplored() const;

            MWRender::LocalMap* mLocalMapRender;
            int cellX;
            int cellY;
            float nX;
            float nY;
            std::vector<std::string> notes;
            std::string caption;
        };

    protected:
        void updateLocalMap();

        MWRender::LocalMap* mLocalMapRender;
        const MWWorld::Cell* mActiveCell = nullptr;
        osg::Vec2f mCurPos; // the position of the player in the world (in cell coords)

        MyGUI::ScrollView* mLocalMap = nullptr;
        MyGUI::ImageBox* mCompass = nullptr;
        float mLocalMapZoom = 1.f;
        bool mFogOfWarToggled = true;
        bool mFogOfWarEnabled;
        bool mNeedDoorMarkersUpdate = false;

        // Stores markers that were placed by a player. May be shared between multiple map views.
        CustomMarkerCollection& mCustomMarkers;

        struct MapEntry
        {
            MapEntry(MyGUI::ImageBox* mapWidget, MyGUI::ImageBox* fogWidget)
                : mMapWidget(mapWidget)
                , mFogWidget(fogWidget)
                , mCellX(0)
                , mCellY(0)
            {
            }

            MyGUI::ImageBox* mMapWidget;
            MyGUI::ImageBox* mFogWidget;
            std::unique_ptr<MyGUI::ITexture> mMapTexture;
            std::unique_ptr<MyGUI::ITexture> mFogTexture;
            int mCellX;
            int mCellY;
        };
        std::vector<MapEntry> mMaps;

        // Keep track of created marker widgets, just to easily remove them later.
        std::vector<MarkerWidget*> mExteriorDoorMarkerWidgets;
        std::map<std::pair<int, int>, std::vector<MarkerWidget*>> mExteriorDoorsByCell;
        std::vector<MarkerWidget*> mInteriorDoorMarkerWidgets;
        std::vector<MyGUI::Widget*> mMagicMarkerWidgets;
        std::vector<MyGUI::Widget*> mCustomMarkerWidgets;
        std::vector<MarkerWidget*> mDoorMarkersToRecycle;

        std::vector<MarkerWidget*>& currentDoorMarkersWidgets();

        virtual void updateCustomMarkers();

        void applyFogOfWar();

        MyGUI::IntPoint getPosition(int cellX, int cellY, float nx, float ny) const;
        MyGUI::IntPoint getMarkerPosition(float worldX, float worldY, MarkerUserData& markerPos) const;
        MyGUI::IntCoord getMarkerCoordinates(
            float worldX, float worldY, MarkerUserData& markerPos, unsigned short markerSize) const;
        MarkerWidget* createDoorMarker(const std::string& name, float x, float y) const;
        void updateMarkerCoordinates(MyGUI::Widget* widget, unsigned short markerSize) const;

        virtual void notifyPlayerUpdate() {}
        virtual void centerView();

        virtual void customMarkerCreated(MyGUI::Widget* marker) {}
        virtual void doorMarkerCreated(MyGUI::Widget* marker) {}

        void updateRequiredMaps();

        void updateMagicMarkers();
        void addDetectionMarkers(int type);

        void redraw();
        float getWidgetSize() const;

        MWGui::LocalMapBase::MapEntry& addMapEntry();

        MyGUI::IntRect mGrid{ -1, -1, 1, 1 };
        float mMarkerUpdateTimer = 0.f;

        float mLastDirectionX = 0.f;
        float mLastDirectionY = 0.f;

    private:
        void updateDoorMarkers();
    };

    class EditNoteDialog : public MWGui::WindowModal
    {
    public:
        EditNoteDialog();

        void onOpen() override;

        void showDeleteButton(bool show);
        bool getDeleteButtonShown();
        void setText(const std::string& text);
        std::string getText();

        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;

        EventHandle_Void eventDeleteClicked;
        EventHandle_Void eventOkClicked;

        ControllerButtons* getControllerButtons() override;

    private:
        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onOkButtonClicked(MyGUI::Widget* sender);
        void onDeleteButtonClicked(MyGUI::Widget* sender);

        MyGUI::TextBox* mTextEdit;
        MyGUI::Button* mOkButton;
        MyGUI::Button* mCancelButton;
        MyGUI::Button* mDeleteButton;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        size_t mControllerFocus = 0;
    };

    class MapWindow : public MWGui::WindowPinnableBase, public LocalMapBase, public NoDrop
    {
    public:
        MapWindow(CustomMarkerCollection& customMarkers, DragAndDrop* drag, MWRender::LocalMap* localMapRender,
            SceneUtil::WorkQueue* workQueue);
        virtual ~MapWindow();

        void setCellName(const std::string& cellName);

        /// Accessibility: place a map note (custom marker) with the given text
        /// at the player's current world position and cell. This is the
        /// keyboard-driven equivalent of double-clicking a spot on the map to
        /// drop a note, used by the scanner's "drop note" (N) feature so a blind
        /// player can mark waypoints without aiming the mouse. Returns false (and
        /// places nothing) if there is no player/cell or the text is empty.
        bool addNoteAtPlayerPosition(const std::string& text);

        void setAlpha(float alpha) override;
        void setVisible(bool visible) override;

        void renderGlobalMap();

        /// adds the marker to the global map
        /// @param name The ESM::Cell::mName
        void addVisitedLocation(const std::string& name, int x, int y);

        // reveals this cell's map on the global map
        void cellExplored(int x, int y);

        void setGlobalMapPlayerPosition(float worldX, float worldY);
        void setGlobalMapPlayerDir(const float x, const float y);

        void ensureGlobalMapLoaded();

        void onOpen() override;
        void onClose() override;

        void onFrame(float dt) override;

        void updateCustomMarkers() override;

        /// Clear all savegame-specific data
        void clear() override;

        void write(ESM::ESMWriter& writer, Loading::Listener& progress);
        void readRecord(ESM::ESMReader& reader, uint32_t type);

        void asyncPrepareSaveMap();

        std::string_view getWindowIdForLua() const override { return "Map"; }

    protected:
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

    private:
        void onDragStart(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onMouseDrag(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onWorldButtonClicked(MyGUI::Widget* sender);
        void onMapDoubleClicked(MyGUI::Widget* sender);
        void onMapZoomed(MyGUI::Widget* sender, int rel);
        void zoomOnCursor(float speedDiff);
        void updateGlobalMap();
        void onCustomMarkerDoubleClicked(MyGUI::Widget* sender);
        void onNoteEditOk();
        void onNoteEditDelete();
        void onNoteEditDeleteConfirm();
        void onNoteDoubleClicked(MyGUI::Widget* sender);
        void onChangeScrollWindowCoord(MyGUI::Widget* sender);
        void globalMapUpdatePlayer();
        void setGlobalMapMarkerTooltip(MyGUI::Widget* widget, int x, int y);
        float getMarkerSize(size_t agregatedWeight) const;
        void resizeGlobalMap();
        void shiftMap(int dx, int dy);
        void worldPosToGlobalMapImageSpace(float x, float z, float& imageX, float& imageY) const;
        MyGUI::IntCoord createMarkerCoords(float x, float y, float agregatedWeight) const;
        MyGUI::Widget* createMarker(const std::string& name, float x, float y, float agregatedWeight);

        MyGUI::ScrollView* mGlobalMap;
        std::unique_ptr<MyGUI::ITexture> mGlobalMapTexture;
        std::unique_ptr<MyGUI::ITexture> mGlobalMapOverlayTexture;
        MyGUI::ImageBox* mGlobalMapImage;
        MyGUI::ImageBox* mGlobalMapOverlay;
        MyGUI::ImageBox* mPlayerArrowLocal;
        MyGUI::ImageBox* mPlayerArrowGlobal;
        MyGUI::Button* mButton;
        MyGUI::IntPoint mLastDragPos;

        MyGUI::IntCoord mLastScrollWindowCoordinates;

        // Markers on global map
        typedef std::pair<int, int> CellId;
        std::set<CellId> mMarkers;

        MyGUI::Button* mEventBoxGlobal;
        MyGUI::Button* mEventBoxLocal;

        float mGlobalMapZoom = 1.0f;
        std::unique_ptr<MWRender::GlobalMap> mGlobalMapRender;

        struct MapMarkerType
        {
            osg::Vec2f position;
            MyGUI::Widget* widget = nullptr;

            bool operator<(const MapMarkerType& right) const { return widget < right.widget; }
        };

        std::map<std::string, MapMarkerType> mGlobalMapMarkersByName;
        std::map<MapMarkerType, std::vector<MapMarkerType>> mGlobalMapMarkers;

        EditNoteDialog mEditNoteDialog;
        ESM::CustomMarker mEditingMarker;

        void onPinToggled() override;
        void onTitleDoubleClicked() override;

        void doorMarkerCreated(MyGUI::Widget* marker) override;
        void customMarkerCreated(MyGUI::Widget* marker) override;

        void notifyPlayerUpdate() override;

        void centerView() override;

        // --- Screen-reader accessibility ---------------------------------
        // The map is a spatial image with no navigable widget list, so this is
        // a virtual-focus screen (an invisible anchor holds key focus). It
        // exposes the map's native textual data rather than the picture:
        //  - the current location (cell/region) name, announced first;
        //  - player-placed note markers, as an expandable submenu.
        // Each note reads its text plus a compass direction and distance in
        // metres from the player, since the player can't see where it sits.
        // (Live Detect-spell radar lives in the scanner's "Detected" category,
        // not here.) Shown alongside Stats/Inventory/Magic; switched to via Tab
        // through the A11y::PaneGroup (Map = pane 3).
        A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;

        // Build the spoken option list (Location, Notes, Detected).
        void buildAccessibility();
        // The current location name (window title text), or a fallback.
        std::string a11yLocationName() const;
        // Submenu items for player note markers in the current cell(s).
        std::vector<A11y::SubItem> a11yNoteItems() const;
        // Format "<name>, <direction>, <distance> metres" for a world position
        // relative to the player.
        std::string a11yBearingLabel(const std::string& name, float worldX, float worldY) const;
    };
}
#endif
