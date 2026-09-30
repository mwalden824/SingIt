#ifndef MICROPHONERECORDER_H
#define MICROPHONERECORDER_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>

#include <memory>
#include <vector>

class MicrophoneRecorder : public QObject
{
    Q_OBJECT

public:
    explicit MicrophoneRecorder(QObject* parent = nullptr);
    ~MicrophoneRecorder();

    bool start();

    std::vector<float> stop();

    bool isRecording() const;

signals:
    void recordingStarted();
    void recordingStopped();
    void recordingError(const QString& error);

private:
    QAudioFormat audioFormat_;

    std::unique_ptr<QAudioSource> audioSource_;

    QIODevice* audioDevice_ = nullptr;

    std::vector<float> audioSamples_;

    bool recording_ = false;
};

#endif // MICROPHONERECORDER_H