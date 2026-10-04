#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QThread>
#include <QTimer>

#include "musicdatabase.h"
#include "simsearchmodel.h"
#include "microphonerecorder.h"
#include "whisperspeechrecognizer.h"

#include <memory>


QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:

    explicit MainWindow(
        QWidget *parent = nullptr
        );

    ~MainWindow() override;


private slots:
    void on_actionOptions_triggered();
    void on_actionExit_triggered();
    void on_searchButton_clicked();
    void on_speakButton_clicked();
    void stopVoiceRecording();
    void onPrevButtonClicked();
    void onPlayPauseButtonClicked();
    void onSkipButtonClicked();
    void onShuffleButtonClicked();

    void onVolumeButtonClicked();
    void onVolumeSliderValueChanged(int value);

    void onTrackSliderPressed();
    void onTrackSliderReleased();
    void onTrackSliderMoved(int position);
    void onPositionChanged(qint64 position);

private:

    Ui::MainWindow *ui;

    std::unique_ptr<SimSearchModel> model;

    MusicDatabase musicDatabase;

    QMediaPlayer *mediaPlayer;

    QAudioOutput *audioOutput;

    qint64 playbackStopTime;
    SearchResult currentTrack;
    QImage currentAlbumArt;

    int getMp3Duration(
        const QString& filename);

    void playMp3Section(
        const QString& filename,
        int startTimeMs,
        int stopTimeMs
        );
    void playMp3(const QString& filename);

    int songLength = 0;
    bool isPlay = true;
    bool isPlayingFullSong = false;
    bool isSeeking = false;
    bool isShuffling = false;
    bool isMuted = false;
    float previousVolume = 100;
    void updateVolumeIcon(float volume);

    QString getMusicLibraryPath() const;
    void inspectWhisperModels();
    void queryDatabaseAndPlayClip(QString text);
    std::unique_ptr<MicrophoneRecorder> microphoneRecorder_;
    std::unique_ptr<WhisperSpeechRecognizer> whisperRecognizer_;

    QTimer voiceRecordingTimer_;

    void updateAlbumArtLabel();

    void displayTrack(const SearchResult& result);
};

#endif // MAINWINDOW_H