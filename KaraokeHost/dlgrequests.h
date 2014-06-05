#ifndef KHREQUESTSDIALOG_H
#define KHREQUESTSDIALOG_H

#include <QDialog>
#include "requeststablemodel.h"
#include "songdbtablemodel.h"
#include "rotationtablemodel.h"
#include "khsinger.h"
#include "dlgcdgpreview.h"

namespace Ui {
class DlgRequests;
}

class DlgRequests : public QDialog
{
    Q_OBJECT

public:
    explicit DlgRequests(KhSongs *fullData,RotationTableModel *rotationModel,QWidget *parent = 0);
    ~DlgRequests();

private slots:
    void on_pushButtonClose_clicked();
    void requestsModified();

    void on_pushButtonSearch_clicked();

    void on_lineEditSearch_returnPressed();
    void requestSelectionChanged(const QModelIndex & current, const QModelIndex & previous);
    void songSelectionChanged(const QModelIndex & current, const QModelIndex & previous);

    void on_radioButtonExistingSinger_toggled(bool checked);

    void on_pushButtonClearReqs_clicked();

    void on_treeViewRequests_clicked(const QModelIndex &index);

    void on_pushButtonAddSong_clicked();

    void on_treeViewSearch_customContextMenuRequested(const QPoint &pos);

private:
    Ui::DlgRequests *ui;
    RequestsTableModel *requestsModel;
    SongDBTableModel *songDbModel;
    RotationTableModel *m_rotationModel;
    DlgCdgPreview *cdgPreviewDialog;
};

#endif // KHREQUESTSDIALOG_H