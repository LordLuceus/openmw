#ifndef CONTENTSELECTOR_HPP
#define CONTENTSELECTOR_HPP

#include <memory>

#include <QComboBox>
#include <QDialog>
#include <QMenu>
#include <QTableView>
#include <QToolButton>

#include <components/contentselector/model/contentmodel.hpp>

class QSortFilterProxyModel;

namespace Ui
{
    class ContentSelector;
}

namespace ContentSelectorView
{
    class ContentSelector : public QObject
    {
        Q_OBJECT

        QMenu* mContextMenu;

    protected:
        ContentSelectorModel::ContentModel* mContentModel;
        QSortFilterProxyModel* mAddonProxyModel;

    public:
        explicit ContentSelector(QWidget* parent = nullptr, bool showOMWScripts = false);

        ~ContentSelector() override;

        QString currentFile() const;

        void addFiles(const QString& path, bool newfiles = false);
        void sortFiles();
        bool containsDataFiles(const QString& path);
        void clearFiles();
        void setNonUserContent(const QStringList& fileList);
        void setProfileContent(const QStringList& fileList);

        void clearCheckStates();
        void setEncoding(const QString& encoding);
        void setContentList(const QStringList& list, bool orderOnly = false);

        ContentSelectorModel::ContentFileList selectedFiles() const;

        void setGameFile(const QString& filename = QString(""));

        bool isGamefileSelected() const;

        QWidget* uiWidget() const;

        QComboBox* languageBox() const;

        QToolButton* refreshButton() const;

        QLineEdit* searchFilter() const;

        /// The game file (master) selector. Exposed so the launcher can set an
        /// explicit tab order; see DataFilesPage's constructor.
        QComboBox* gameFileView() const;

        /// The content file list itself.
        QTableView* addonView() const;

        /// Move the selected content files one position up (step -1) or down
        /// (step +1). Returns true if anything moved. Exposed so the launcher
        /// can drive it from Move Up / Move Down buttons: the view supports
        /// drag-and-drop reordering, which is unusable without a mouse.
        bool moveSelection(int step);

        /// Whether moveSelection(step) would do anything. Used to enable or
        /// disable the Move Up / Move Down buttons, which is how a screen
        /// reader user perceives "this is the end of the list".
        bool moveSelectionPossible(int step) const;

    private:
        std::unique_ptr<Ui::ContentSelector> ui;

        /// Rows of the underlying content model that are currently selected,
        /// or an empty list if reordering is not currently meaningful (e.g. a
        /// search filter is hiding rows).
        QList<int> selectedSourceRows() const;

        void buildContentModel(bool showOMWScripts);
        void buildGameFileView();
        void buildAddonView();
        void buildContextMenu();
        void setGameFileSelected(int index, bool selected);
        void setCheckStateForMultiSelectedItems(Qt::CheckState checkState);

    signals:
        void signalCurrentGamefileIndexChanged(int);

        void signalAddonDataChanged(const QModelIndex& topleft, const QModelIndex& bottomright);
        void signalSelectedFilesChanged(QStringList selectedFiles);
        /// The set of highlighted rows changed (not the checked files).
        void signalSelectionChanged();

    private slots:

        void slotCurrentGameFileIndexChanged(int index);
        void slotAddonTableItemActivated(const QModelIndex& index);
        void slotShowContextMenu(const QPoint& pos);
        void slotCheckMultiSelectedItems();
        void slotUncheckMultiSelectedItems();
        void slotCopySelectedItemsPaths();
        void slotSearchFilterTextChanged(const QString& newText);
        void slotRowsMoved();
    };
}

#endif // CONTENTSELECTOR_HPP
