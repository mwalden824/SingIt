#include "optionsdialog.h"
#include "ui_optionsdialog.h"
#include "musicimporter.h"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QFileInfo>

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>

OptionsDialog::OptionsDialog(
    MusicDatabase& musicDatabase,
    SimSearchModel& model,
    QWidget* parent)
    : QDialog(parent),
    ui(new Ui::OptionsDialog),
    musicDatabase(musicDatabase),
    model(model)
{
    ui->setupUi(this);

    loadSettings();
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::loadSettings()
{
    QSettings settings("Michael Walden", "SingIt");

    ui->musicFolderLineEdit->setText(
        settings.value("musicLibraryPath").toString());

    ui->lrclibDatabaseLineEdit->setText(
        settings.value("lrclibDatabasePath").toString());
}

void OptionsDialog::saveSettings()
{
    QSettings settings("Michael Walden", "SingIt");

    settings.setValue(
        "musicLibraryPath",
        ui->musicFolderLineEdit->text());

    settings.setValue(
        "lrclibDatabasePath",
        ui->lrclibDatabaseLineEdit->text());
}

void OptionsDialog::on_chooseMusicFolderButton_clicked()
{
    QString currentPath = ui->musicFolderLineEdit->text();

    if (currentPath.isEmpty() || !QDir(currentPath).exists())
        currentPath = QDir::homePath();

    QString directory = QFileDialog::getExistingDirectory(
        this,
        tr("Select Music Library"),
        currentPath);

    if (!directory.isEmpty())
        ui->musicFolderLineEdit->setText(directory);
}

void OptionsDialog::on_chooseLrclibDatabaseFileButton_clicked()
{
    QString currentPath = ui->lrclibDatabaseLineEdit->text();

    QString initialDirectory = QDir::homePath();

    if (!currentPath.isEmpty())
    {
        QFileInfo fileInfo(currentPath);

        if (fileInfo.exists())
            initialDirectory = fileInfo.absolutePath();
        else if (QDir(fileInfo.absolutePath()).exists())
            initialDirectory = fileInfo.absolutePath();
    }

    QString filename = QFileDialog::getOpenFileName(
        this,
        tr("Select LRCLIB Database"),
        initialDirectory,
        tr("SQLite Database (*.sqlite3 *.sqlite *.db);;All Files (*)"));

    if (!filename.isEmpty())
        ui->lrclibDatabaseLineEdit->setText(filename);
}

void OptionsDialog::on_importButton_clicked()
{
    QString musicDirectory =
        ui->musicFolderLineEdit->text().trimmed();

    QString lrclibDatabase =
        ui->lrclibDatabaseLineEdit->text().trimmed();

    if (musicDirectory.isEmpty())
    {
        QMessageBox::warning(
            this,
            tr("Missing Music Folder"),
            tr("Please select your music folder before importing."));

        return;
    }

    if (!QDir(musicDirectory).exists())
    {
        QMessageBox::warning(
            this,
            tr("Invalid Music Folder"),
            tr("The selected music folder does not exist."));

        return;
    }

    if (lrclibDatabase.isEmpty())
    {
        QMessageBox::warning(
            this,
            tr("Missing LRCLIB Database"),
            tr("Please select your LRCLIB database before importing."));

        return;
    }

    if (!QFileInfo::exists(lrclibDatabase))
    {
        QMessageBox::warning(
            this,
            tr("Invalid LRCLIB Database"),
            tr("The selected LRCLIB database does not exist."));

        return;
    }

    // Save the paths immediately so the selected locations persist
    // even if the user later closes the dialog with Cancel.
    saveSettings();

    ui->importButton->setEnabled(false);

    QApplication::setOverrideCursor(Qt::WaitCursor);

    try
    {
        MusicImporter importer(
            musicDatabase,
            lrclibDatabase.toStdString(),
            musicDirectory.toStdString(),
            model);

        importer.importAll("import.log");

        QMessageBox::information(
            this,
            tr("Import Complete"),
            tr("The music import has completed."));
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(
            this,
            tr("Import Error"),
            QString("The import failed:\n\n%1")
                .arg(QString::fromStdString(e.what())));
    }
    catch (...)
    {
        QMessageBox::critical(
            this,
            tr("Import Error"),
            tr("The import failed due to an unknown error."));
    }

    QApplication::restoreOverrideCursor();

    ui->importButton->setEnabled(true);
}

void OptionsDialog::on_buttonBox_accepted()
{
    saveSettings();
    accept();
}

void OptionsDialog::on_buttonBox_rejected()
{
    reject();
}