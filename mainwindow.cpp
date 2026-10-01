#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "optionsdialog.h"

#include <QDebug>
#include <QCoreApplication>
#include <QFileDialog>
#include <QSettings>
#include <QDir>
#include <QApplication>

#include <string>
#include <vector>

// ============================================================
// MainWindow
// ============================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      model(nullptr),
      musicDatabase(
          "music.db"
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
        // inspectWhisperModels();
    }
    catch (const std::exception& e)
    {
        qDebug()
            << "Initialization / embedding error:";

        qDebug()
            << e.what();
    }

    microphoneRecorder_ =
        std::make_unique<MicrophoneRecorder>(this);

    whisperRecognizer_ =
        std::make_unique<WhisperSpeechRecognizer>();

    voiceRecordingTimer_.setSingleShot(true);

    connect(
        &voiceRecordingTimer_,
        &QTimer::timeout,
        this,
        &MainWindow::stopVoiceRecording);
}

// ============================================================
// Options Menu Item
// ============================================================
void MainWindow::on_actionOptions_triggered()
{
    OptionsDialog dialog(musicDatabase, *model, this);
    dialog.exec();
}

void MainWindow::on_actionExit_triggered()
{
    close();
}

// ============================================================
// Speak Button
// ============================================================
void MainWindow::on_speakButton_clicked()
{
    if (ui->speakButton->isChecked())
    {
        // Start recording
        if (!microphoneRecorder_->start())
        {
            ui->speakButton->setChecked(false);

            qWarning() << "Could not start microphone.";
            return;
        }

        qDebug() << "Voice recording started.";

        // Maximum recording time: 15 seconds
        voiceRecordingTimer_.start(15000);

        return;
    }

    // User pressed the button again.
    stopVoiceRecording();
}

void MainWindow::stopVoiceRecording()
{
    // Make sure the timer can't fire again.
    voiceRecordingTimer_.stop();

    if (!microphoneRecorder_->isRecording())
    {
        ui->speakButton->setChecked(false);
        return;
    }

    std::vector<float> audio =
        microphoneRecorder_->stop();

    ui->speakButton->setChecked(false);

    if (audio.empty())
    {
        qWarning() << "No audio was recorded.";
        return;
    }

    qDebug() << "Recorded"
             << audio.size()
             << "samples.";

    try
    {
        std::string transcription =
            whisperRecognizer_->transcribeAudio(audio);

        qDebug() << "Transcription:"
                 << QString::fromStdString(transcription);

        // Pass transcription to your next function here.
        //
        // For example:
        //
        // performTextSearch(transcription);
        ui->searchText->setPlainText(QString::fromStdString(transcription));
        queryDatabaseAndPlayClip(QString::fromStdString(transcription));
    }
    catch (const std::exception& e)
    {
        qCritical() << "Voice transcription failed:"
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

    queryDatabaseAndPlayClip(text);
}

void MainWindow::queryDatabaseAndPlayClip(QString text)
{
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

        for (const SearchResult& res : results)
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
            getMusicLibraryPath() + "/" +
            QString::fromStdString(
                result.filename
                );

        qDebug() << fullPath;
        // ----------------------------------------------------
        // Play matching lyric section
        // ----------------------------------------------------

        const int paddingMs = 2000; // 2 seconds

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

    mediaPlayer->setPosition(
        startTimeMs
        );

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

QString MainWindow::getMusicLibraryPath() const
{
    QSettings settings;

    return settings.value("musicLibraryPath").toString();
}

void MainWindow::inspectWhisperModels()
{
    const std::wstring encoderPath =
        L"models/whisper/onnx/encoder_model.onnx";

    const std::wstring decoderPath =
        L"models/whisper/onnx/decoder_model.onnx";

    Ort::Env env(
        ORT_LOGGING_LEVEL_WARNING,
        "WhisperInspection"
        );

    Ort::SessionOptions sessionOptions;

    Ort::Session encoderSession(
        env,
        encoderPath.c_str(),
        sessionOptions
        );

    Ort::Session decoderSession(
        env,
        decoderPath.c_str(),
        sessionOptions
        );

    auto inspectSession =
        [](const char* name, Ort::Session& session)
    {
        qDebug() << "\n====" << name << "====";

        Ort::AllocatorWithDefaultOptions allocator;

        size_t inputCount = session.GetInputCount();

        qDebug() << "Inputs:" << inputCount;

        for (size_t i = 0; i < inputCount; ++i)
        {
            auto inputName = session.GetInputNameAllocated(
                i,
                allocator
                );

            auto typeInfo = session.GetInputTypeInfo(i);

            auto tensorInfo =
                typeInfo.GetTensorTypeAndShapeInfo();

            auto shape = tensorInfo.GetShape();

            qDebug() << "Input" << i
                     << ":" << inputName.get();

            qDebug() << "  Type:"
                     << tensorInfo.GetElementType();

            qDebug() << "  Shape:";

            for (auto dimension : shape)
                qDebug() << "   " << dimension;
        }

        size_t outputCount = session.GetOutputCount();

        qDebug() << "Outputs:" << outputCount;

        for (size_t i = 0; i < outputCount; ++i)
        {
            auto outputName = session.GetOutputNameAllocated(
                i,
                allocator
                );

            auto typeInfo = session.GetOutputTypeInfo(i);

            auto tensorInfo =
                typeInfo.GetTensorTypeAndShapeInfo();

            auto shape = tensorInfo.GetShape();

            qDebug() << "Output" << i
                     << ":" << outputName.get();

            qDebug() << "  Type:"
                     << tensorInfo.GetElementType();

            qDebug() << "  Shape:";

            for (auto dimension : shape)
                qDebug() << "   " << dimension;
        }
    };

    inspectSession("WHISPER ENCODER", encoderSession);
    inspectSession("WHISPER DECODER", decoderSession);
}

// ============================================================
// Destructor
// ============================================================

MainWindow::~MainWindow()
{
    delete ui;
}
