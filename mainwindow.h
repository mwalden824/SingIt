#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QThread>

#include "musicdatabase.h"
#include "simsearchmodel.h"

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

    void on_searchButton_clicked();


private:

    Ui::MainWindow *ui;

    std::unique_ptr<SimSearchModel> model;

    MusicDatabase musicDatabase;

    QMediaPlayer *mediaPlayer;

    QAudioOutput *audioOutput;

    qint64 playbackStopTime;


    void playMp3Section(
        const QString& filename,
        int startTimeMs,
        int stopTimeMs
        );
};

#endif // MAINWINDOW_H