#include "mainwindow.h"
#include "whisperspeechrecognizer.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("3Point");
    QCoreApplication::setApplicationName("SingIt");
    // MainWindow w;
    // w.show();

    try
    {
        WhisperSpeechRecognizer recognizer;

        recognizer.initialize();

        std::string text =
            recognizer.transcribeWav(
                "test.wav");

        qDebug() << "TRANSCRIPTION:"
                 << QString::fromStdString(text);
    }
    catch (const std::exception& e)
    {
        qDebug() << "WHISPER ERROR:"
                 << e.what();
    }

    return QApplication::exec();
}

