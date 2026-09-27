#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QThread>

#include "musicdatabase.h"
#include <onnxruntime_cxx_api.h>
#include <tokenizers_cpp.h>

#include <memory>
#include <string>
#include <vector>


QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE


// ============================================================
// Model Context
// ============================================================

struct ModelContext
{
    Ort::Env env;
    Ort::Session session;

    std::unique_ptr<tokenizers::Tokenizer> tokenizer;

    ModelContext(
        Ort::Env&& env_,
        Ort::Session&& session_,
        std::unique_ptr<tokenizers::Tokenizer>&& tokenizer_
        )
        : env(std::move(env_)),
        session(std::move(session_)),
        tokenizer(std::move(tokenizer_))
    {
    }
};


// ============================================================
// MainWindow
// ============================================================

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:

    explicit MainWindow(
        QWidget *parent = nullptr
        );

    ModelContext initONNXRuntimeAndTokenizer();
    std::vector<int32_t> tokenizeString(
        tokenizers::Tokenizer& tokenizer,
        const std::string& sentence
        );
    static std::vector<float> calculateEmbeddingVector(
        Ort::Session& session,
        const std::vector<int32_t>& tokenIds
        );
    float cosineSimilarity(
        const std::vector<float>& a,
        const std::vector<float>& b
        );

    ~MainWindow() override;

private slots:
    void on_searchButton_clicked();

private:

    Ui::MainWindow *ui;

    std::unique_ptr<ModelContext> model;
    MusicDatabase musicDatabase;

    QMediaPlayer *mediaPlayer;
    QAudioOutput *audioOutput;

    qint64 playbackStopTime;

    void playMp3Section(
        const QString& filename,
        int startTimeMs,
        int stopTimeMs);
};


#endif // MAINWINDOW_H
