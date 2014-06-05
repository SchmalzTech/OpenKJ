/*
 * Copyright (c) 2013-2014 Thomas Isaac Lightburn
 *
 *
 * This file is part of OpenKJ.
 *
 * OpenKJ is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dlgregularsingers.h"
#include "ui_dlgregularsingers.h"
#include <QDebug>
#include <QMessageBox>
#include <QSqlQuery>

DlgRegularSingers::DlgRegularSingers(KhRegularSingers *regSingers, RotationTableModel *rotationModel, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DlgRegularSingers)
{
    ui->setupUi(this);
    regularSingerModel = new RegularSingerModel(regSingers, this);
    ui->treeViewRegulars->setModel(regularSingerModel);
    ui->treeViewRegulars->hideColumn(0);
    ui->treeViewRegulars->setColumnWidth(3,20);
    ui->treeViewRegulars->setColumnWidth(4,20);
    ui->treeViewRegulars->setColumnWidth(5,20);
    ui->treeViewRegulars->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    ui->treeViewRegulars->header()->setSectionResizeMode(1,QHeaderView::Stretch);
    ui->treeViewRegulars->header()->setSectionResizeMode(3,QHeaderView::Fixed);
    ui->treeViewRegulars->header()->setSectionResizeMode(4,QHeaderView::Fixed);
    ui->treeViewRegulars->header()->setSectionResizeMode(5,QHeaderView::Fixed);
    ui->comboBoxAddPos->addItem("Fair");
    ui->comboBoxAddPos->addItem("Bottom");
    ui->comboBoxAddPos->addItem("Next");
    m_rotationModel = rotationModel;
    m_regSingers = regSingers;
    connect(regularSingerModel, SIGNAL(editSingerDuplicateError()), this, SLOT(editSingerDuplicateError()));
}

DlgRegularSingers::~DlgRegularSingers()
{
    delete ui;
}

void DlgRegularSingers::on_btnClose_clicked()
{
    close();
}

void DlgRegularSingers::on_treeViewRegulars_clicked(const QModelIndex &index)
{
    if (index.column() == 3)
    {
        // Add to rotation
        qDebug() << "Add to rotation clicked on row " << index.row();
        addRegularToRotation(index.row());

    }
    else if (index.column() == 4)
    {
        // Rename regular
        qDebug() << "Rename singer clicked on row " << index.row();
        QModelIndex child = regularSingerModel->index(index.row(), 1, index.parent());
        ui->treeViewRegulars->selectionModel()->setCurrentIndex(child,QItemSelectionModel::SelectCurrent);
        ui->treeViewRegulars->edit(child);
    }
    else if (index.column() == 5)
    {
        // Delete regular

        QMessageBox msgBox(this);
        msgBox.setText("Are you sure you want to delete this regular singer?");
        msgBox.setInformativeText("This will completely remove the regular singer from the database and can not be undone.  Note that if the singer is already loaded they won't be deleted from the rotation but regular tracking will be disabled.");
        QPushButton *yesButton = msgBox.addButton(QMessageBox::Yes);
        QPushButton *cancelButton = msgBox.addButton(QMessageBox::Cancel);
        msgBox.exec();
        if (msgBox.clickedButton() == yesButton)
        {
            qDebug() << "Delete singer clicked on row " << index.row();
            emit regularSingerDeleted(regularSingerModel->getRegularSingerByListIndex(index.row())->getIndex());
            regularSingerModel->removeByListIndex(index.row());
            return;
        }
        else if (msgBox.clickedButton() == cancelButton)
            return;
    }
}

void DlgRegularSingers::editSingerDuplicateError()
{
    QMessageBox::warning(this, tr("Duplicate Name"), tr("A regular singer by that name already exists, edit cancelled."),QMessageBox::Close);
}

void DlgRegularSingers::addRegularToRotation(int ListIndex)
{
    if (m_rotationModel->exists(m_regSingers->at(ListIndex)->getName()))
    {
        //Singer with name already exists in rotation
        QMessageBox::warning(this, tr("Naming conflict"), tr("A rotation singer already exists with the same name as the regular you're attempting to add. Action aborted."), QMessageBox::Close);
    }
    else
    {
        QMessageBox *msgBox = new QMessageBox(this);
        msgBox->setStandardButtons(0);
        msgBox->setText("Loading regular singer, please wait...");
        msgBox->show();
        KhRegularSinger *regSinger = m_regSingers->at(ListIndex);
        KhSinger *rotSinger;
        qDebug() << "RegularSingersDialog::addRegularToRotation() DB transaction start";
        QSqlQuery query("BEGIN TRANSACTION");
        qDebug() << "Adding regular " << m_regSingers->at(ListIndex)->getName();
        m_rotationModel->add(m_regSingers->at(ListIndex)->getName());
        rotSinger = m_rotationModel->at(m_rotationModel->size() -1);
        qDebug() << "Sorting songs by position";
        regSinger->getRegSongs()->sort();
        qDebug() << "Sort complete, adding songs to singer's queue";
        for (int i=0; i < regSinger->getRegSongs()->getRegSongs()->size(); i++)
        {
            KhRegularSong *regSong = regSinger->getRegSongs()->getRegSongs()->at(i);
            rotSinger->addSongAtEnd(regSong->getSongIndex());
            QApplication::processEvents();
            rotSinger->queueSongs()->at(i)->setKeyChange(regSong->getKeyChange());
            QApplication::processEvents();
            rotSinger->queueSongs()->at(i)->setRegSingerIndex(regSinger->getIndex());
            QApplication::processEvents();
            rotSinger->queueSongs()->at(i)->setRegSong(true);
            QApplication::processEvents();
            rotSinger->queueSongs()->at(i)->setRegSongIndex(regSong->getRegSongIndex());
            QApplication::processEvents();

        }
        qDebug() << "Done adding songs, setting as regular";
        rotSinger->setRegular(true);
        rotSinger->setRegularIndex(regSinger->getIndex());
        qDebug() << "Singer add complete";
        query.exec("COMMIT TRANSACTION");
        qDebug() << "RegularSingersDialog::addRegularToRotation() DB transaction end";
        if ((ui->comboBoxAddPos->currentText() == "Next") && (m_rotationModel->getCurrent() != NULL))
        {
            if (m_rotationModel->getCurrent()->position() != m_rotationModel->size())
                m_rotationModel->moveSinger(rotSinger->position(),m_rotationModel->getCurrent()->position() + 1);
        }
        else if ((ui->comboBoxAddPos->currentText() == "Fair") && (m_rotationModel->getCurrent() != NULL))
        {
            if (m_rotationModel->getCurrent()->position() != 1)
                m_rotationModel->moveSinger(rotSinger->position(), m_rotationModel->getCurrent()->position());
        }
        msgBox->close();
        delete msgBox;
    }
}