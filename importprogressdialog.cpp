#include "importprogressdialog.h"
#include "ui_importprogressdialog.h"

#include "musicimporter.h"
#include "musicdatabase.h"
#include "simsearchmodel.h"

#include <QDebug>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <QPushButton>
#include <QDialogButtonBox>

// ============================================================
// ImportWorker
// ============================================================

class ImportWorker : public QObject
{
    Q_OBJECT

public:
    ImportWorker(
        MusicDatabase& musicDatabase,
        SimSearchModel& model,
        const QString& lrclibDatabasePath,
        const QString& musicDirectory)
        :
        musicDatabase(musicDatabase),
        model(model),
        lrclibDatabasePath(lrclibDatabasePath),
        musicDirectory(musicDirectory)
    {
        importer = nullptr;
    }
public slots:

    void run()
    {
        try
        {
            importer = new MusicImporter(
                musicDatabase,
                lrclibDatabasePath.toStdString(),
                musicDirectory.toStdString(),
                model,
                [this](
                    int current,
                    int total,
                    const std::string& filename)
                {
                    emit progress(
                        current,
                        total,
                        QString::fromStdString(filename));
                });

            importer->importAll("import.log");

            if (importer->isCancelled())
            {
                delete importer;
                importer = nullptr;
                emit cancelled();
                return;
            }

            emit finished(
                static_cast<int>(importer->getImportedCount()),
                static_cast<int>(importer->getSkippedCount()));
            delete importer;
            importer = nullptr;
        }
        catch (const std::exception& e)
        {
            emit failed(
                QString::fromUtf8(e.what()));
            delete importer;
            importer = nullptr;
        }
        catch (...)
        {
            emit failed(
                "Unknown error occurred during import.");
            delete importer;
            importer = nullptr;
        }
    }
    void cancel()
    {
        if (importer != nullptr)
            importer->cancel();
    }
signals:

    void progress(
        int current,
        int total,
        const QString& filename);

    void finished(
        int imported,
        int skipped);

    void failed(
        const QString& error);

    void cancelled();

private:

    MusicDatabase& musicDatabase;
    SimSearchModel& model;

    QString lrclibDatabasePath;
    QString musicDirectory;
    MusicImporter* importer;
};


// ============================================================
// ImportProgressDialog
// ============================================================

ImportProgressDialog::ImportProgressDialog(
    MusicDatabase& musicDatabase,
    SimSearchModel& model,
    const QString& lrclibDatabasePath,
    const QString& musicDirectory,
    QWidget* parent)
    :
    QDialog(parent),
    ui(new Ui::ImportProgressDialog),
    musicDatabase(musicDatabase),
    model(model),
    lrclibDatabasePath(lrclibDatabasePath),
    musicDirectory(musicDirectory)
{
    ui->setupUi(this);

    // Don't start the worker until the dialog has had a chance
    // to display itself.
    QTimer::singleShot(
        0,
        this,
        &ImportProgressDialog::startImport);
}


ImportProgressDialog::~ImportProgressDialog()
{
    if (workerThread != nullptr)
    {
        workerThread->quit();
        workerThread->wait();
    }

    delete ui;
}


// ============================================================
// Start import
// ============================================================

void ImportProgressDialog::startImport()
{
    workerThread = new QThread(this);

    worker = new ImportWorker(
        musicDatabase,
        model,
        lrclibDatabasePath,
        musicDirectory);

    worker->moveToThread(workerThread);

    connect(
        ui->buttonBox->button(QDialogButtonBox::Cancel),
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (worker != nullptr)
                worker->cancel();
        }
    );

    connect(
        workerThread,
        &QThread::started,
        worker,
        &ImportWorker::run);

    connect(
        worker,
        &ImportWorker::progress,
        this,
        &ImportProgressDialog::onProgress);

    connect(
        worker,
        &ImportWorker::finished,
        this,
        &ImportProgressDialog::onImportFinished);

    connect(
        worker,
        &ImportWorker::failed,
        this,
        &ImportProgressDialog::onImportFailed);

    connect(
        worker,
        &ImportWorker::finished,
        workerThread,
        &QThread::quit);

    connect(
        worker,
        &ImportWorker::failed,
        workerThread,
        &QThread::quit);

    connect(
        workerThread,
        &QThread::finished,
        worker,
        &QObject::deleteLater);

    connect(
        worker,
        &ImportWorker::cancelled,
        this,
        &ImportProgressDialog::onImportCancelled
        );

    connect(
        worker,
        &ImportWorker::cancelled,
        workerThread,
        &QThread::quit);

    workerThread->start();
}

void ImportProgressDialog::onImportCancelled()
{
    accept();
}

// ============================================================
// Progress
// ============================================================

void ImportProgressDialog::onProgress(
    int current,
    int total,
    const QString& filename)
{
    if (total <= 0)
    {
        ui->progressBar->setRange(0, 0);
        return;
    }

    ui->progressBar->setRange(
        0,
        total);

    ui->progressBar->setValue(
        current);

    ui->countLabel->setText(
        QString("%1 of %2")
            .arg(current)
            .arg(total));

    ui->statusLabel->setText(
        QString("Processing: %1")
            .arg(filename));
}


// ============================================================
// Import finished
// ============================================================

void ImportProgressDialog::onImportFinished(
    int imported,
    int skipped)
{
    ui->progressBar->setValue(
        ui->progressBar->maximum());

    ui->statusLabel->setText(
        QString("Import complete. "
                "Imported: %1    Skipped: %2")
            .arg(imported)
            .arg(skipped));

    ui->countLabel->setText(
        QString("%1 of %1")
            .arg(ui->progressBar->maximum()));

    accept();
    worker = nullptr;
}


// ============================================================
// Import failed
// ============================================================

void ImportProgressDialog::onImportFailed(
    const QString& error)
{
    ui->statusLabel->setText(
        "Import failed.");

    ui->buttonBox->button(
                     QDialogButtonBox::Close
                     )->setEnabled(true);

    QMessageBox::critical(
        this,
        "Import Error",
        error);

    worker = nullptr;
}

#include "importprogressdialog.moc"