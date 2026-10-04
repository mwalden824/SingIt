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
#include <QDir>
#include <QEventLoop>
#include <QMediaMetaData>
#include <QPixmap>
#include <QUrl>
#include <QRandomGenerator>

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

    connect(ui->prevButton, &QPushButton::clicked,
                this, &MainWindow::onPrevButtonClicked);

    connect(ui->playPauseButton, &QPushButton::clicked,
            this, &MainWindow::onPlayPauseButtonClicked);

    connect(ui->skipButton, &QPushButton::clicked,
            this, &MainWindow::onSkipButtonClicked);

    connect(ui->shuffleButton, &QPushButton::clicked,
            this, &MainWindow::onShuffleButtonClicked);


    connect(ui->volumeButton, &QPushButton::clicked,
            this, &MainWindow::onVolumeButtonClicked);

    connect(ui->volumeSlider, &QSlider::valueChanged,
            this, &MainWindow::onVolumeSliderValueChanged);


    connect(ui->trackSlider, &QSlider::sliderPressed,
            this, &MainWindow::onTrackSliderPressed);

    connect(ui->trackSlider, &QSlider::sliderReleased,
            this, &MainWindow::onTrackSliderReleased);

    connect(ui->trackSlider, &QSlider::sliderMoved,
            this, &MainWindow::onTrackSliderMoved);

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
        &MainWindow::onPositionChanged
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

    mediaPlayer->audioOutput()->setVolume(1.0);
}

void MainWindow::onPositionChanged(qint64 position)
{
    // Update slider to current position
    if (!isSeeking)
    {
        ui->trackSlider->setValue(static_cast<int>(position));
    }
    int totalSeconds = position / 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;

    QString formattedTime = QString("%1:%2")
                                .arg(minutes)
                                .arg(seconds, 2, 10, QChar('0'));

    ui->currentTrackTimeLabel->setText(formattedTime);

    if (position >= playbackStopTime)
    {
        if (isPlayingFullSong)
        {
            onSkipButtonClicked();
        }
        else
        {
            mediaPlayer->stop();
        }
    }
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

        songLength =
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

        isPlayingFullSong = false;
        displayTrack(result);
        ui->playPauseButton->setIcon(QIcon(":resources/icons/player-play.svg"));
        isPlay = true;

        // Enable buttons
        ui->prevButton->setEnabled(false);
        ui->skipButton->setEnabled(false);
        ui->playPauseButton->setEnabled(true);
        ui->shuffleButton->setEnabled(true);
        ui->trackSlider->setEnabled(true);

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

void MainWindow::playMp3(const QString& filename)
{
    QString fullPath = getMusicLibraryPath() + "/" + filename;

    qDebug() << "Loading MP3:" << fullPath;

    mediaPlayer->setSource(QUrl::fromLocalFile(fullPath));
    playbackStopTime =
        getMp3Duration(fullPath);

    // Wait until the media has loaded
    while (mediaPlayer->mediaStatus() == QMediaPlayer::LoadingMedia)
    {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    if (mediaPlayer->mediaStatus() == QMediaPlayer::LoadedMedia)
    {
        mediaPlayer->play();
    }
    else
    {
        qDebug() << "Failed to load MP3:"
                 << mediaPlayer->errorString();
    }
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

void MainWindow::onPrevButtonClicked()
{
    QDir musicDir(getMusicLibraryPath());

    QStringList files = musicDir.entryList(
        QStringList() << "*.mp3",
        QDir::Files,
        QDir::NoSort
        );

    if (files.isEmpty())
        return;

    QString currentFilename =
        QString::fromStdString(currentTrack.filename);

    int currentIndex = files.indexOf(currentFilename);

    if (currentIndex == -1)
    {
        qDebug() << "Current track not found:" << currentFilename;
        return;
    }

    // int previousIndex = currentIndex - 1;
    int previousIndex;

    if (isShuffling)
    {
        previousIndex = QRandomGenerator::global()->bounded(files.size());
    }
    else
    {
        previousIndex = currentIndex - 1;

        if (previousIndex < 0)
            previousIndex = files.size() - 1;
    }

    // First track -> last track
    if (previousIndex < 0)
        previousIndex = files.size() - 1;

    QString previousFilename = files[previousIndex];

    qDebug() << "Going back to:" << previousFilename;

    playMp3(previousFilename);

    // Get metadata from the existing media player
    QMediaMetaData metadata = mediaPlayer->metaData();

    SearchResult newTrack{};

    newTrack.filename = previousFilename.toStdString();

    QVariant title = metadata.value(QMediaMetaData::Title);
    if (title.isValid())
        newTrack.trackName = title.toString().toStdString();

    QVariant artist = metadata.value(QMediaMetaData::ContributingArtist);
    if (artist.isValid())
    {
        QStringList artists = artist.toStringList();

        if (!artists.isEmpty())
            newTrack.artist = artists.first().toStdString();
    }

    currentTrack = newTrack;
    QString fullPath = QDir(getMusicLibraryPath()).filePath(QString::fromStdString(currentTrack.filename));
    songLength = getMp3Duration(fullPath);
    isPlayingFullSong = true;
    ui->syncedLyricLabel->setText("");

    displayTrack(currentTrack);
}

void MainWindow::onPlayPauseButtonClicked()
{
    ui->syncedLyricLabel->setText("");
    if (isPlay)
    {
        if (isPlayingFullSong)
        {
            mediaPlayer->play();
        }
        else
        {
            playMp3(QString::fromStdString(currentTrack.filename));
            isPlayingFullSong = true;
            ui->prevButton->setEnabled(true);
            ui->skipButton->setEnabled(true);
        }

        ui->playPauseButton->setIcon(QIcon(":resources/icons/player-pause.svg"));
        isPlay = false;
    }
    else
    {
        mediaPlayer->pause();
        ui->playPauseButton->setIcon(QIcon(":resources/icons/player-play.svg"));
        isPlay = true;
    }
}

void MainWindow::onSkipButtonClicked()
{
    QDir musicDir(getMusicLibraryPath());

    QStringList files = musicDir.entryList(
        QStringList() << "*.mp3",
        QDir::Files,
        QDir::NoSort
        );

    if (files.isEmpty())
        return;

    QString currentFilename =
        QString::fromStdString(currentTrack.filename);

    int currentIndex = files.indexOf(currentFilename);

    if (currentIndex == -1)
    {
        qDebug() << "Current track not found:" << currentFilename;
        return;
    }

    // int nextIndex = currentIndex + 1;
    int nextIndex;

    if (isShuffling)
    {
        nextIndex = QRandomGenerator::global()->bounded(files.size());
    }
    else
    {
        nextIndex = currentIndex + 1;

        if (nextIndex >= files.size())
            nextIndex = 0;
    }

    // Last track -> first track
    if (nextIndex >= files.size())
        nextIndex = 0;

    QString nextFilename = files[nextIndex];

    qDebug() << "Skipping to:" << nextFilename;

    playMp3(nextFilename);

    // Get metadata from the existing media player
    QMediaMetaData metadata = mediaPlayer->metaData();

    SearchResult newTrack{};

    newTrack.filename = nextFilename.toStdString();

    QVariant title = metadata.value(QMediaMetaData::Title);
    if (title.isValid())
        newTrack.trackName = title.toString().toStdString();

    QVariant artist = metadata.value(QMediaMetaData::ContributingArtist);
    if (artist.isValid())
    {
        QStringList artists = artist.toStringList();

        if (!artists.isEmpty())
            newTrack.artist = artists.first().toStdString();
    }

    currentTrack = newTrack;
    QString fullPath = QDir(getMusicLibraryPath()).filePath(QString::fromStdString(currentTrack.filename));
    songLength = getMp3Duration(fullPath);
    isPlayingFullSong = true;
    ui->syncedLyricLabel->setText("");

    displayTrack(currentTrack);
}

void MainWindow::onShuffleButtonClicked()
{
    if (isShuffling)
    {
        // Shuffle OFF
        ui->shuffleButton->setIcon(
            QIcon(":resources/icons/arrows-shuffle.svg")
            );
        isShuffling = false;
    }
    else
    {
        // Shuffle ON
        ui->shuffleButton->setIcon(
            QIcon(":resources/icons/arrows-shuffle-green.svg")
            );
        isShuffling = true;
    }
}

void MainWindow::onVolumeButtonClicked()
{
    if (isMuted)
    {
        // Unmute
        isMuted = false;

        mediaPlayer->audioOutput()->setVolume(previousVolume);

        ui->volumeSlider->setValue(static_cast<int>(previousVolume * 100.0));

        updateVolumeIcon(previousVolume);
    }
    else
    {
        // Mute
        previousVolume = static_cast<float>(ui->volumeSlider->value()) / 100.0;

        isMuted = true;

        mediaPlayer->audioOutput()->setVolume(0.0);

        ui->volumeSlider->setValue(0);

        updateVolumeIcon(0.0);
    }
}

void MainWindow::updateVolumeIcon(float volume)
{
    if (volume <= 0.0) // MUTE
    {
        ui->volumeButton->setIcon(
            QIcon(":resources/icons/volume-3.svg")
            );
    }
    else if (volume < 0.1) // low volume icon
    {
        ui->volumeButton->setIcon(
            QIcon(":resources/icons/volume-4.svg")
            );
    }
    else if (volume <= 0.7) // Medium volume icon
    {
        ui->volumeButton->setIcon(
            QIcon(":resources/icons/volume-2.svg")
            );
    }
    else // Max volume icon
    {
        ui->volumeButton->setIcon(
            QIcon(":resources/icons/volume.svg")
            );
    }
}

void MainWindow::onVolumeSliderValueChanged(int value)
{
    float newValue = static_cast<float>(value) / 100.0;
    mediaPlayer->audioOutput()->setVolume(newValue);

    if (value == 0)
    {
        isMuted = true;
    }
    else
    {
        isMuted = false;
        previousVolume = value;
    }
    updateVolumeIcon(newValue);
}

void MainWindow::onTrackSliderPressed()
{
    isSeeking = true;
}

void MainWindow::onTrackSliderReleased()
{
    isSeeking = false;
    int position = ui->trackSlider->value();

    mediaPlayer->setPosition(position);
}

void MainWindow::onTrackSliderMoved(int position)
{
    // Update the current time label while dragging
    int totalSeconds = position / 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;

    QString formattedTime = QString("%1:%2")
                                .arg(minutes)
                                .arg(seconds, 2, 10, QChar('0'));

    ui->currentTrackTimeLabel->setText(formattedTime);
}

void MainWindow::displayTrack(const SearchResult& result)
{
    currentTrack = result;

    // Artist
    ui->artistNameLabel->setText(
        QString::fromStdString(result.artist)
        );

    // Track name
    ui->trackNameLabel->setText(
        QString::fromStdString(result.trackName)
        );

    // Album artwork
    updateAlbumArtLabel();

    // Display Synced Lyric + Match Percentage
    float similarity =
        1.0f - (currentTrack.distance * currentTrack.distance) / 2.0f;

    float matchPercent = similarity * 100.0f;

    QString lyricText = QString::fromStdString(currentTrack.lyricText);

    lyricText += QString(" (%1%)").arg(matchPercent, 0, 'f', 1);


    if (!isPlayingFullSong)
    {
        ui->syncedLyricLabel->setText(lyricText);
    }

    // Display song length
    int totalSeconds = songLength / 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;

    QString formattedTime = QString("%1:%2")
                                .arg(minutes)
                                .arg(seconds, 2, 10, QChar('0'));

    ui->totalTrackTimeLabel->setText(formattedTime);

    ui->trackSlider->setRange(0, songLength);
}

void MainWindow::updateAlbumArtLabel()
{
    QString filename = QString::fromStdString(currentTrack.filename);

    QString fullPath =
        QDir(getMusicLibraryPath()).filePath(filename);

    QMediaPlayer player;

    QEventLoop loop;

    connect(&player, &QMediaPlayer::mediaStatusChanged,
            &loop, [&](QMediaPlayer::MediaStatus status)
            {
                if (status == QMediaPlayer::LoadedMedia ||
                    status == QMediaPlayer::InvalidMedia)
                {
                    loop.quit();
                }
            });

    player.setSource(QUrl::fromLocalFile(fullPath));

    loop.exec();

    QMediaMetaData metadata = player.metaData();

    QVariant coverArt =
        metadata.value(QMediaMetaData::ThumbnailImage);

    if (coverArt.isValid())
    {
        currentAlbumArt =
            coverArt.value<QImage>();

        ui->albumArtLabel->setPixmap(
            QPixmap::fromImage(currentAlbumArt).scaled(
                ui->albumArtLabel->size(),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
                )
            );
    }
    else
    {
        currentAlbumArt = QImage();
        ui->albumArtLabel->clear();
    }
}

// ============================================================
// Destructor
// ============================================================

MainWindow::~MainWindow()
{
    delete ui;
}
