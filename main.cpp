// #include "mainwindow.h"

// #include <QApplication>

// int main(int argc, char *argv[])
// {
//     QApplication a(argc, argv);
//     MainWindow w;
//     w.show();
//     return QApplication::exec();
// }


// ONNX RUNTIME INITIALIZATION TEST
// #include <QApplication>
// #include <QDebug>

// #include <onnxruntime_cxx_api.h>

// int main(int argc, char *argv[])
// {
//     QApplication app(argc, argv);

//     try
//     {
//         Ort::Env env(
//             ORT_LOGGING_LEVEL_WARNING,
//             "SingIt"
//             );

//         qDebug() << "ONNX Runtime initialized successfully!";
//     }
//     catch (const Ort::Exception& e)
//     {
//         qDebug() << "ONNX Runtime error:";
//         qDebug() << e.what();

//         return 1;
//     }

//     return 0;
// }



// // MODEL INITIALIZATION TEST USING ONNX RUNTIME AND MODEL INPUT/OUTPUT PARAMETER QUERY
// #include <QApplication>
// #include <QDebug>

// #include <onnxruntime_cxx_api.h>
// #include <tokenizers_cpp.h>

// #include <string>

// void printTensorInfo(
//     const char* label,
//     const Ort::TypeInfo& typeInfo)
// {
//     auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();

//     auto shape = tensorInfo.GetShape();

//     qDebug() << label;

//     qDebug() << "  Element type:"
//              << static_cast<int>(tensorInfo.GetElementType());

//     qDebug() << "  Shape:";

//     for (auto dimension : shape)
//     {
//         qDebug() << "    " << dimension;
//     }
// }

// int main(int argc, char *argv[])
// {
//     QApplication app(argc, argv);

//     try
//     {
//         Ort::Env env(
//             ORT_LOGGING_LEVEL_WARNING,
//             "SingIt"
//             );

//         Ort::SessionOptions sessionOptions;

//         std::wstring modelPath =
//             L"models/model.onnx";

//         Ort::Session session(
//             env,
//             modelPath.c_str(),
//             sessionOptions
//             );

//         qDebug() << "Model loaded successfully!";

//         // Inputs
//         auto inputNames = session.GetInputNames();

//         qDebug() << "INPUTS:" << inputNames.size();

//         for (size_t i = 0; i < inputNames.size(); ++i)
//         {
//             qDebug() << "Input"
//                      << i
//                      << ":"
//                      << QString::fromUtf8(inputNames[i].c_str());
//         }

//         qDebug() << "";
//         qDebug() << "INPUT TENSOR INFORMATION";

//         for (size_t i = 0; i < inputNames.size(); ++i)
//         {
//             auto typeInfo = session.GetInputTypeInfo(i);

//             printTensorInfo(
//                 inputNames[i].c_str(),
//                 typeInfo
//                 );
//         }

//         // Outputs
//         auto outputNames = session.GetOutputNames();

//         qDebug() << "OUTPUTS:" << outputNames.size();

//         for (size_t i = 0; i < outputNames.size(); ++i)
//         {
//             qDebug() << "Output"
//                      << i
//                      << ":"
//                      << QString::fromUtf8(outputNames[i].c_str());
//         }

//         qDebug() << "";
//         qDebug() << "OUTPUT TENSOR INFORMATION";

//         for (size_t i = 0; i < outputNames.size(); ++i)
//         {
//             auto typeInfo = session.GetOutputTypeInfo(i);

//             printTensorInfo(
//                 outputNames[i].c_str(),
//                 typeInfo
//                 );
//         }

//         qDebug() << "Model inspection complete.";
//     }
//     catch (const Ort::Exception& e)
//     {
//         qDebug() << "ONNX Runtime error:";
//         qDebug() << e.what();

//         return 1;
//     }

//     return 0;
// }




// Tokenizer Test
#include <QApplication>
#include <QDebug>

#include <onnxruntime_cxx_api.h>
#include <array>

#include <tokenizers_cpp.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    try
    {
        // Read tokenizer.json into memory
        std::ifstream file(
            "models/tokenizer.json"
            );

        if (!file)
        {
            qDebug() << "Could not open tokenizer.json";
            return 1;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        std::string tokenizerJson = buffer.str();

        // Create tokenizer
        auto tokenizer =
            tokenizers::Tokenizer::FromBlobJSON(tokenizerJson);

        qDebug() << "Tokenizer loaded successfully!";

        // Test sentence
        std::string sentence =
            "The quick brown fox jumps over the lazy dog.";

        std::vector<int32_t> tokenIds =
            tokenizer->Encode(sentence);

        qDebug() << "Sentence:"
                 << QString::fromStdString(sentence);

        qDebug() << "Number of tokens:"
                 << tokenIds.size();

        qDebug() << "First token ID:"
                 << tokenIds.front();

        qDebug() << "Last token ID:"
                 << tokenIds.back();

        qDebug() << "Token IDs:";

        for (int32_t id : tokenIds)
        {
            qDebug() << "  " << id;
        }

        qDebug() << "";
        qDebug() << "Special token IDs:";

        qDebug() << "[CLS] ID:"
                 << tokenizer->TokenToId("[CLS]");

        qDebug() << "[SEP] ID:"
                 << tokenizer->TokenToId("[SEP]");

        qDebug() << "[PAD] ID:"
                 << tokenizer->TokenToId("[PAD]");

        qDebug() << "[UNK] ID:"
                 << tokenizer->TokenToId("[UNK]");

        qDebug() << "Tokenizer test complete.";


        // ------------------------------------------------------------
        // Construct BERT input
        // ------------------------------------------------------------

        std::vector<int64_t> inputIds;
        inputIds.reserve(tokenIds.size() + 2);

        // [CLS]
        inputIds.push_back(101);

        // Token IDs
        for (int32_t id : tokenIds)
        {
            inputIds.push_back(static_cast<int64_t>(id));
        }

        // [SEP]
        inputIds.push_back(102);

        const int64_t sequenceLength =
            static_cast<int64_t>(inputIds.size());

        std::vector<int64_t> attentionMask(
            sequenceLength, 1);

        std::vector<int64_t> tokenTypeIds(
            sequenceLength, 0);

        qDebug() << "";
        qDebug() << "ONNX input sequence length:"
                 << sequenceLength;

        qDebug() << "input_ids:";
        for (int64_t id : inputIds)
        {
            qDebug() << "  " << id;
        }

        qDebug() << "attention_mask:";
        for (int64_t value : attentionMask)
        {
            qDebug() << "  " << value;
        }

        qDebug() << "token_type_ids:";
        for (int64_t value : tokenTypeIds)
        {
            qDebug() << "  " << value;
        }

        // ------------------------------------------------------------
        // Load ONNX model
        // ------------------------------------------------------------

        Ort::Env env(
            ORT_LOGGING_LEVEL_WARNING,
            "SingIt"
            );

        Ort::SessionOptions sessionOptions;

        std::wstring modelPath =
            L"models/model.onnx";

        Ort::Session session(
            env,
            modelPath.c_str(),
            sessionOptions
            );

        qDebug() << "";
        qDebug() << "ONNX model loaded.";

        // ------------------------------------------------------------
        // Create ONNX tensors
        // ------------------------------------------------------------

        std::array<int64_t, 2> inputShape = {
            1,
            sequenceLength
        };

        Ort::MemoryInfo memoryInfo =
            Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator,
                OrtMemTypeDefault
                );

        Ort::Value inputIdsTensor =
            Ort::Value::CreateTensor<int64_t>(
                memoryInfo,
                inputIds.data(),
                inputIds.size(),
                inputShape.data(),
                inputShape.size()
                );

        Ort::Value attentionMaskTensor =
            Ort::Value::CreateTensor<int64_t>(
                memoryInfo,
                attentionMask.data(),
                attentionMask.size(),
                inputShape.data(),
                inputShape.size()
                );

        Ort::Value tokenTypeIdsTensor =
            Ort::Value::CreateTensor<int64_t>(
                memoryInfo,
                tokenTypeIds.data(),
                tokenTypeIds.size(),
                inputShape.data(),
                inputShape.size()
                );

        qDebug() << "ONNX input tensors created.";

        // ------------------------------------------------------------
        // Run ONNX model
        // ------------------------------------------------------------

        const char* inputNames[] = {
            "input_ids",
            "attention_mask",
            "token_type_ids"
        };

        const char* outputNames[] = {
            "last_hidden_state"
        };

        std::array<Ort::Value, 3> inputTensors = {
            std::move(inputIdsTensor),
            std::move(attentionMaskTensor),
            std::move(tokenTypeIdsTensor)
        };

        auto outputTensors =
            session.Run(
                Ort::RunOptions{nullptr},
                inputNames,
                inputTensors.data(),
                inputTensors.size(),
                outputNames,
                1
                );

        qDebug() << "";
        qDebug() << "ONNX inference completed.";

        auto outputInfo =
            outputTensors[0].GetTensorTypeAndShapeInfo();

        auto outputShape =
            outputInfo.GetShape();

        qDebug() << "Output shape:";

        for (auto dimension : outputShape)
        {
            qDebug() << "  " << dimension;
        }

        const float* outputData =
            outputTensors[0].GetTensorData<float>();

        qDebug() << "";
        qDebug() << "First token vector (first 10 dimensions):";

        for (int i = 0; i < 10; ++i)
        {
            qDebug() << "  " << outputData[i];
        }

        // ------------------------------------------------------------
        // Mean pooling
        // ------------------------------------------------------------

        std::vector<float> embedding(384, 0.0f);

        for (int64_t token = 0; token < sequenceLength; ++token)
        {
            if (attentionMask[token] == 0)
                continue;

            for (int dimension = 0; dimension < 384; ++dimension)
            {
                const size_t index =
                    static_cast<size_t>(token) * 384 + dimension;

                embedding[dimension] += outputData[index];
            }
        }

        // Divide by the number of valid tokens
        float maskSum = 0.0f;

        for (int64_t value : attentionMask)
        {
            maskSum += static_cast<float>(value);
        }

        for (float& value : embedding)
        {
            value /= maskSum;
        }

        qDebug() << "";
        qDebug() << "Mean-pooled embedding size:"
                 << embedding.size();

        qDebug() << "First 10 embedding values:";

        for (int i = 0; i < 10; ++i)
        {
            qDebug() << "  " << embedding[i];
        }

        // ------------------------------------------------------------
        // L2 normalization
        // ------------------------------------------------------------

        double sumSquares = 0.0;

        for (float value : embedding)
        {
            sumSquares +=
                static_cast<double>(value) *
                static_cast<double>(value);
        }

        double l2Norm = std::sqrt(sumSquares);

        qDebug() << "";
        qDebug() << "Embedding size:"
                 << embedding.size();

        qDebug() << "L2 norm before normalization:"
                 << l2Norm;

        if (l2Norm > 0.0)
        {
            for (float& value : embedding)
            {
                value = static_cast<float>(
                    value / l2Norm
                    );
            }
        }

        // Verify normalization
        double normalizedSumSquares = 0.0;

        for (float value : embedding)
        {
            normalizedSumSquares +=
                static_cast<double>(value) *
                static_cast<double>(value);
        }

        double normalizedNorm =
            std::sqrt(normalizedSumSquares);

        qDebug() << "L2 norm after normalization:"
                 << normalizedNorm;

        qDebug() << "First 10 normalized embedding values:";

        for (int i = 0; i < 10; ++i)
        {
            qDebug() << "  " << embedding[i];
        }

    }
    catch (const std::exception& e)
    {
        qDebug() << "Tokenizer error:";
        qDebug() << e.what();

        return 1;
    }

    return 0;
}