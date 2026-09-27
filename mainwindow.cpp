#include "mainwindow.h"
#include "ui_mainwindow.h"
// #include "musicimporter.h"

#include <QDebug>
#include <QCoreApplication>


// ============================================================
// MainWindow
// ============================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      model(nullptr),
      musicDatabase(
          "C:/Walden/Projects/MusicApp/SingIt/music.db"
          ),
      mediaPlayer(new QMediaPlayer(this)),
      audioOutput(new QAudioOutput(this)),
      playbackStopTime(0)
{
    ui->setupUi(this);

    mediaPlayer->setAudioOutput(
        audioOutput
        );


    // --------------------------------------------------------
    // Stop playback at requested stop time
    // --------------------------------------------------------

    connect(
        mediaPlayer,
        &QMediaPlayer::positionChanged,
        this,
        [this](qint64 position)
        {
            if (position >= playbackStopTime)
            {
                mediaPlayer->stop();
            }
        }
        );


    // --------------------------------------------------------
    // Initialize model and database
    // --------------------------------------------------------

    try
    {
        model =
            std::make_unique<SimSearchModel>();


        musicDatabase.initialize();
    }
    catch (const std::exception& e)
    {
        qDebug()
            << "Initialization / embedding error:";

        qDebug()
            << e.what();
    }
}


// ============================================================
// Search Button
// ============================================================

void MainWindow::on_searchButton_clicked()
{
    const QString text =
        ui->searchText->toPlainText().trimmed();


    if (text.isEmpty())
        return;


    try
    {
        // ----------------------------------------------------
        // Convert Qt string to std::string
        // ----------------------------------------------------

        std::string query =
            text.toStdString();


        // ----------------------------------------------------
        // Tokenize query
        // ----------------------------------------------------

        auto tokensSearch =
            model->tokenizeString(
                query
                );


        // ----------------------------------------------------
        // Generate embedding
        // ----------------------------------------------------

        auto embeddingSearch =
            model->calculateEmbeddingVector(
                tokensSearch
                );


        // ----------------------------------------------------
        // Search database
        // ----------------------------------------------------

        std::vector<SearchResult> results =
            musicDatabase.searchSimilar(
                embeddingSearch,
                1
                );


        // ----------------------------------------------------
        // Make sure we actually got a result
        // ----------------------------------------------------

        if (results.empty())
        {
            qDebug()
                << "No search results found.";

            return;
        }


        // ----------------------------------------------------
        // Display search results
        // ----------------------------------------------------

        for (SearchResult res : results)
        {
            qDebug()
                << "Artist: "
                << res.artist;

            qDebug()
                << "Track Name: "
                << res.trackName;

            qDebug()
                << "File Name: "
                << res.filename;

            qDebug()
                << "File Number: "
                << res.fileNumber;

            qDebug()
                << "Start Time (ms): "
                << res.startTimeMs;

            qDebug()
                << "Stop Time (ms): "
                << res.stopTimeMs;

            qDebug()
                << "Lyric: "
                << res.lyricText;

            qDebug()
                << "Distance: "
                << res.distance;
        }


        // ----------------------------------------------------
        // Get best result
        // ----------------------------------------------------

        SearchResult result =
            results[0];


        // ----------------------------------------------------
        // Construct full MP3 path
        // ----------------------------------------------------

        QString fullPath =
            "C:/Walden/Projects/MusicApp/music/allMusic/" +
            QString::fromStdString(
                result.filename
                );


        // ----------------------------------------------------
        // Play matching lyric section
        // ----------------------------------------------------

        // playMp3Section(
        //     fullPath,
        //     static_cast<int>(
        //         result.startTimeMs
        //         ),
        //     static_cast<int>(
        //         result.stopTimeMs
        //         )
        //     );

        const int paddingMs = 2000; // 2 seconds

        // const int songDurationMs =
        //     static_cast<int>(mediaPlayer->duration());
        const int songLength =
            getMp3Duration(fullPath);

        const int startTime =
            std::max(
                0,
                static_cast<int>(result.startTimeMs) - paddingMs
                );

        const int stopTime =
            std::min(
                songLength,
                static_cast<int>(result.stopTimeMs) + paddingMs
                );

        playMp3Section(
            fullPath,
            startTime,
            stopTime
            );
    }
    catch (const std::exception& e)
    {
        qDebug()
            << "Search error:"
            << e.what();
    }
}


// ============================================================
// Play MP3 Section
// ============================================================

void MainWindow::playMp3Section(
    const QString& filename,
    int startTimeMs,
    int stopTimeMs)
{
    if (startTimeMs < 0 ||
        stopTimeMs <= startTimeMs)
    {
        qDebug()
            << "Invalid playback times.";

        return;
    }


    mediaPlayer->stop();


    playbackStopTime =
        stopTimeMs;


    mediaPlayer->setSource(
        QUrl::fromLocalFile(
            filename
            )
        );


    // --------------------------------------------------------
    // Wait until media has loaded
    // --------------------------------------------------------

    while (
        mediaPlayer->mediaStatus() !=
        QMediaPlayer::LoadedMedia)
    {
        if (
            mediaPlayer->mediaStatus() ==
            QMediaPlayer::InvalidMedia)
        {
            qDebug()
                << "Failed to load media:"
                << mediaPlayer->errorString();

            return;
        }


        QCoreApplication::processEvents();

        QThread::msleep(10);
    }


    qDebug()
        << "Media loaded.";


    qDebug()
        << "Seeking to:"
        << startTimeMs;


    mediaPlayer->setPosition(
        startTimeMs
        );


    qDebug()
        << "Position after seek:"
        << mediaPlayer->position();


    mediaPlayer->play();
}

int MainWindow::getMp3Duration(
    const QString& filename)
{
    QMediaPlayer player;

    player.setSource(
        QUrl::fromLocalFile(filename)
        );

    while (
        player.mediaStatus() !=
        QMediaPlayer::LoadedMedia)
    {
        if (
            player.mediaStatus() ==
            QMediaPlayer::InvalidMedia)
        {
            qDebug()
            << "Failed to load media:"
            << player.errorString();

            return -1;
        }

        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    return static_cast<int>(
        player.duration()
        );
}

// ============================================================
// Destructor
// ============================================================

MainWindow::~MainWindow()
{
    delete ui;
}
