#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QDialog>

#include "musicdatabase.h"
#include "simsearchmodel.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class OptionsDialog;
}
QT_END_NAMESPACE

class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(
        MusicDatabase& musicDatabase,
        SimSearchModel& model,
        QWidget* parent = nullptr);

    ~OptionsDialog();

private slots:
    void on_chooseMusicFolderButton_clicked();
    void on_chooseLrclibDatabaseFileButton_clicked();
    void on_importButton_clicked();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

private:
    Ui::OptionsDialog *ui;

    MusicDatabase& musicDatabase;
    SimSearchModel& model;

    void loadSettings();
    void saveSettings();
};

#endif // OPTIONSDIALOG_H