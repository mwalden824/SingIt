#include "microphonerecorder.h"

#include <QDebug>


// ============================================================
// Constructor
// ============================================================

MicrophoneRecorder::MicrophoneRecorder(QObject* parent)
    : QObject(parent)
{
    audioFormat_.setSampleRate(16000);
    audioFormat_.setChannelCount(1);
    audioFormat_.setSampleFormat(QAudioFormat::Int16);
}


// ============================================================
// Destructor
// ============================================================

MicrophoneRecorder::~MicrophoneRecorder()
{
    if (recording_)
        stop();
}


// ============================================================
// Start recording
// ============================================================

bool MicrophoneRecorder::start()
{
    if (recording_)
        return false;

    audioSamples_.clear();

    audioSource_ =
        std::make_unique<QAudioSource>(
            audioFormat_);

    audioDevice_ =
        audioSource_->start();

    if (!audioDevice_)
    {
        emit recordingError(
            "Could not start microphone.");

        audioSource_.reset();

        return false;
    }

    connect(
        audioDevice_,
        &QIODevice::readyRead,
        this,
        [this]()
        {
            QByteArray data =
                audioDevice_->readAll();

            if (data.isEmpty())
                return;

            const qint16* samples =
                reinterpret_cast<const qint16*>(
                    data.constData());

            const int sampleCount =
                data.size() /
                static_cast<int>(sizeof(qint16));

            audioSamples_.reserve(
                audioSamples_.size() +
                sampleCount);

            for (int i = 0;
                 i < sampleCount;
                 ++i)
            {
                audioSamples_.push_back(
                    static_cast<float>(
                        samples[i]) /
                    32768.0f);
            }
        });

    recording_ = true;

    qDebug() << "Microphone recording started.";

    emit recordingStarted();

    return true;
}


// ============================================================
// Stop recording
// ============================================================

std::vector<float>
MicrophoneRecorder::stop()
{
    if (!recording_)
        return {};

    // Stop the audio source first so that no more
    // microphone data is generated.
    audioSource_->stop();

    // There may still be a small amount of data
    // waiting in the QIODevice. Read it before
    // destroying the audio source.
    if (audioDevice_)
    {
        QByteArray data =
            audioDevice_->readAll();

        if (!data.isEmpty())
        {
            const qint16* samples =
                reinterpret_cast<const qint16*>(
                    data.constData());

            const int sampleCount =
                data.size() /
                static_cast<int>(sizeof(qint16));

            audioSamples_.reserve(
                audioSamples_.size() +
                sampleCount);

            for (int i = 0;
                 i < sampleCount;
                 ++i)
            {
                audioSamples_.push_back(
                    static_cast<float>(
                        samples[i]) /
                    32768.0f);
            }
        }
    }

    recording_ = false;

    audioDevice_ = nullptr;
    audioSource_.reset();

    qDebug() << "Microphone recording stopped.";
    qDebug() << "Audio samples:"
             << audioSamples_.size();

    emit recordingStopped();

    // Move the samples out of the recorder.
    return std::move(audioSamples_);
}


// ============================================================
// Recording state
// ============================================================

bool MicrophoneRecorder::isRecording() const
{
    return recording_;
}