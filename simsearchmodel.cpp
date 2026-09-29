#include "simsearchmodel.h"

#include <QDebug>

#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>


// ============================================================
// SimSearchModel
// ============================================================

SimSearchModel::SimSearchModel()
{
    model =
        std::make_unique<ModelContext>(
            initONNXRuntimeAndTokenizer()
            );
}


// ============================================================
// Initialization
// ============================================================

SimSearchModel::ModelContext
SimSearchModel::initONNXRuntimeAndTokenizer()
{

    // --------------------------------------------------------
    // Read tokenizer.json into memory
    // --------------------------------------------------------

    std::ifstream file(
        "models/tokenizer.json"
        );

    if (!file)
    {
        qDebug() << "Could not open tokenizer.json";

        throw std::runtime_error(
            "Could not open tokenizer.json."
            );
    }

    std::stringstream buffer;

    buffer << file.rdbuf();

    std::string tokenizerJson =
        buffer.str();


    // --------------------------------------------------------
    // Create tokenizer
    // --------------------------------------------------------

    auto tokenizer =
        tokenizers::Tokenizer::FromBlobJSON(
            tokenizerJson
            );

    qDebug() << "Tokenizer loaded successfully!";


    // --------------------------------------------------------
    // Initialize ONNX Runtime
    // --------------------------------------------------------

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


    qDebug() << "ONNX model loaded.";


    // --------------------------------------------------------
    // Return model context
    // --------------------------------------------------------

    return ModelContext(
        std::move(env),
        std::move(session),
        std::move(tokenizer)
        );
}


// ============================================================
// Tokenization
// ============================================================

std::vector<int32_t>
SimSearchModel::tokenizeString(
    const std::string& sentence
    )
{
    return model->tokenizer->Encode(
        sentence
        );
}


// ============================================================
// Calculate Embedding
// ============================================================

std::vector<float>
SimSearchModel::calculateEmbeddingVector(
    const std::vector<int32_t>& tokenIds
    )
{
    // --------------------------------------------------------
    // Construct BERT input
    // --------------------------------------------------------

    std::vector<int64_t> inputIds;

    inputIds.reserve(
        tokenIds.size() + 2
        );


    // [CLS]
    inputIds.push_back(101);


    // Token IDs
    for (int32_t id : tokenIds)
    {
        inputIds.push_back(
            static_cast<int64_t>(id)
            );
    }


    // [SEP]
    inputIds.push_back(102);


    const int64_t sequenceLength =
        static_cast<int64_t>(
            inputIds.size()
            );


    // --------------------------------------------------------
    // Attention mask
    // --------------------------------------------------------

    std::vector<int64_t> attentionMask(
        sequenceLength,
        1
        );


    // --------------------------------------------------------
    // Token type IDs
    // --------------------------------------------------------

    std::vector<int64_t> tokenTypeIds(
        sequenceLength,
        0
        );


    // --------------------------------------------------------
    // Create ONNX tensors
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Run ONNX model
    // --------------------------------------------------------

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
        model->session.Run(
            Ort::RunOptions{nullptr},
            inputNames,
            inputTensors.data(),
            inputTensors.size(),
            outputNames,
            1
            );


    // --------------------------------------------------------
    // Get output information
    // --------------------------------------------------------

    auto outputInfo =
        outputTensors[0]
            .GetTensorTypeAndShapeInfo();


    auto outputShape =
        outputInfo.GetShape();


    const float* outputData =
        outputTensors[0]
            .GetTensorData<float>();


    // --------------------------------------------------------
    // Mean pooling
    // --------------------------------------------------------

    constexpr int embeddingDimension = 384;


    std::vector<float> embedding(
        embeddingDimension,
        0.0f
        );


    for (int64_t token = 0;
         token < sequenceLength;
         ++token)
    {
        if (attentionMask[token] == 0)
            continue;


        for (int dimension = 0;
             dimension < embeddingDimension;
             ++dimension)
        {
            const size_t index =
                static_cast<size_t>(token)
                    * embeddingDimension
                + dimension;


            embedding[dimension] +=
                outputData[index];
        }
    }


    // --------------------------------------------------------
    // Divide by number of valid tokens
    // --------------------------------------------------------

    float maskSum = 0.0f;


    for (int64_t value : attentionMask)
    {
        maskSum +=
            static_cast<float>(value);
    }


    if (maskSum == 0.0f)
    {
        throw std::runtime_error(
            "Attention mask contains no valid tokens."
            );
    }


    for (float& value : embedding)
    {
        value /= maskSum;
    }


    // --------------------------------------------------------
    // L2 normalization
    // --------------------------------------------------------

    double sumSquares = 0.0;


    for (float value : embedding)
    {
        sumSquares +=
            static_cast<double>(value)
            * static_cast<double>(value);
    }


    double l2Norm =
        std::sqrt(sumSquares);


    if (l2Norm == 0.0)
    {
        throw std::runtime_error(
            "Cannot normalize a zero embedding."
            );
    }


    for (float& value : embedding)
    {
        value =
            static_cast<float>(
                value / l2Norm
                );
    }


    return embedding;
}


// ============================================================
// Cosine Similarity
// ============================================================

float SimSearchModel::cosineSimilarity(
    const std::vector<float>& a,
    const std::vector<float>& b
    )
{
    if (a.size() != b.size())
    {
        throw std::runtime_error(
            "Vectors must have the same dimension."
            );
    }


    double dotProduct = 0.0;

    double magnitudeA = 0.0;

    double magnitudeB = 0.0;


    for (size_t i = 0;
         i < a.size();
         ++i)
    {
        dotProduct +=
            static_cast<double>(a[i])
            * static_cast<double>(b[i]);


        magnitudeA +=
            static_cast<double>(a[i])
            * static_cast<double>(a[i]);


        magnitudeB +=
            static_cast<double>(b[i])
            * static_cast<double>(b[i]);
    }


    if (magnitudeA == 0.0 ||
        magnitudeB == 0.0)
    {
        throw std::runtime_error(
            "Cannot calculate cosine similarity "
            "with a zero vector."
            );
    }


    return static_cast<float>(
        dotProduct /
        (
            std::sqrt(magnitudeA)
            * std::sqrt(magnitudeB)
            )
        );
}