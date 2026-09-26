#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

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

    ~MainWindow() override;


private:

    Ui::MainWindow *ui;

    std::unique_ptr<ModelContext> model;
};


#endif // MAINWINDOW_H
