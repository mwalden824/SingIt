#ifndef SIMSEARCHMODEL_H
#define SIMSEARCHMODEL_H

#include <onnxruntime_cxx_api.h>
#include <tokenizers_cpp.h>

#include <memory>
#include <string>
#include <vector>


class SimSearchModel
{
public:

    SimSearchModel();

    std::vector<int32_t> tokenizeString(
        const std::string& sentence
        );

    std::vector<float> calculateEmbeddingVector(
        const std::vector<int32_t>& tokenIds
        );

    float cosineSimilarity(
        const std::vector<float>& a,
        const std::vector<float>& b
        );


private:

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


    ModelContext initONNXRuntimeAndTokenizer();

    std::unique_ptr<ModelContext> model;
};

#endif // SIMSEARCHMODEL_H