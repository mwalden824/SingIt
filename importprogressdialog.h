#ifndef IMPORTPROGRESSDIALOG_H
#define IMPORTPROGRESSDIALOG_H

#include <QDialog>

class QThread;
class ImportWorker;
class MusicDatabase;
class SimSearchModel;

namespace Ui
{
class ImportProgressDialog;
}

class ImportProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImportProgressDialog(
        MusicDatabase& musicDatabase,
        SimSearchModel& model,
        const QString& lrclibDatabasePath,
        const QString& musicDirectory,
        QWidget* parent = nullptr);

    ~ImportProgressDialog();

private slots:
    void onProgress(
        int current,
        int total,
        const QString& filename);

    void onImportFinished(
        int imported,
        int skipped);

    void onImportFailed(
        const QString& error);

private:
    void startImport();

    Ui::ImportProgressDialog* ui;

    MusicDatabase& musicDatabase;
    SimSearchModel& model;

    QString lrclibDatabasePath;
    QString musicDirectory;

    QThread* workerThread = nullptr;
    ImportWorker* worker = nullptr;
    void onImportCancelled();
};

#endif // IMPORTPROGRESSDIALOG_H