#include "whisperspeechrecognizer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace
{
constexpr float Pi = 3.14159265358979323846f;
constexpr float TwoPi = 2.0f * Pi;

#pragma pack(push, 1)

struct WavRiffHeader
{
    char riff[4];
    uint32_t fileSize;
    char wave[4];
};

struct WavChunkHeader
{
    char id[4];
    uint32_t size;
};

struct WavFmt
{
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};

#pragma pack(pop)

bool fourCCEquals(const char* actual, const char* expected)
{
    return std::memcmp(actual, expected, 4) == 0;
}
}


// ============================================================
// Constructor
// ============================================================

WhisperSpeechRecognizer::WhisperSpeechRecognizer()
    : env_(ORT_LOGGING_LEVEL_WARNING, "SingIt-Whisper")
{
    sessionOptions_.SetIntraOpNumThreads(4);
    sessionOptions_.SetInterOpNumThreads(1);
}


// ============================================================
// Initialization
// ============================================================

void WhisperSpeechRecognizer::initialize()
{
    if (initialized_)
        return;

    QString applicationDir =
        QCoreApplication::applicationDirPath();

    QString encoderPath =
        QDir(applicationDir).filePath(
            "models/whisper/onnx/encoder_model.onnx");

    QString decoderPath =
        QDir(applicationDir).filePath(
            "models/whisper/onnx/decoder_model.onnx");

    QString tokenizerPath =
        QDir(applicationDir).filePath(
            "models/whisper/tokenizer.json");

    qDebug() << "Whisper encoder:"
             << encoderPath;

    qDebug() << "Whisper decoder:"
             << decoderPath;

    qDebug() << "Whisper tokenizer:"
             << tokenizerPath;

    std::ifstream tokenizerFile(
        tokenizerPath.toStdString(),
        std::ios::binary);

    if (!tokenizerFile)
    {
        throw std::runtime_error(
            "Could not open Whisper tokenizer.json");
    }

    std::string tokenizerBlob(
        (std::istreambuf_iterator<char>(tokenizerFile)),
        std::istreambuf_iterator<char>());

    tokenizer_ =
        tokenizers::Tokenizer::FromBlobJSON(tokenizerBlob);

    if (!tokenizer_)
    {
        throw std::runtime_error(
            "Failed to create Whisper tokenizer");
    }

    std::wstring encoderPathWide =
        encoderPath.toStdWString();

    std::wstring decoderPathWide =
        decoderPath.toStdWString();

    encoderSession_ =
        std::make_unique<Ort::Session>(
            env_,
            encoderPathWide.c_str(),
            sessionOptions_);

    decoderSession_ =
        std::make_unique<Ort::Session>(
            env_,
            decoderPathWide.c_str(),
            sessionOptions_);

    initialized_ = true;

    qDebug() << "Whisper initialized successfully.";
}


// ============================================================
// WAV loading
// ============================================================

std::vector<float>
WhisperSpeechRecognizer::loadWavFile(
    const std::string& filename)
{
    std::ifstream file(
        filename,
        std::ios::binary);

    if (!file)
    {
        throw std::runtime_error(
            "Could not open WAV file: " + filename);
    }

    WavRiffHeader riff{};

    file.read(
        reinterpret_cast<char*>(&riff),
        sizeof(riff));

    if (!file)
        throw std::runtime_error("Invalid WAV header");

    if (!fourCCEquals(riff.riff, "RIFF") ||
        !fourCCEquals(riff.wave, "WAVE"))
    {
        throw std::runtime_error(
            "File is not a RIFF/WAVE file");
    }

    WavFmt fmt{};

    std::vector<char> audioData;

    bool foundFmt = false;
    bool foundData = false;

    while (file)
    {
        WavChunkHeader chunk{};

        file.read(
            reinterpret_cast<char*>(&chunk),
            sizeof(chunk));

        if (!file)
            break;

        if (fourCCEquals(chunk.id, "fmt "))
        {
            if (chunk.size < sizeof(WavFmt))
            {
                throw std::runtime_error(
                    "Invalid WAV fmt chunk");
            }

            file.read(
                reinterpret_cast<char*>(&fmt),
                sizeof(WavFmt));

            if (chunk.size > sizeof(WavFmt))
            {
                file.seekg(
                    chunk.size - sizeof(WavFmt),
                    std::ios::cur);
            }

            foundFmt = true;
        }
        else if (fourCCEquals(chunk.id, "data"))
        {
            audioData.resize(chunk.size);

            file.read(
                audioData.data(),
                chunk.size);

            foundData = true;
        }
        else
        {
            file.seekg(
                chunk.size,
                std::ios::cur);
        }

        // WAV chunks are padded to even byte boundaries.
        if (chunk.size & 1)
            file.seekg(1, std::ios::cur);

        if (foundFmt && foundData)
            break;
    }

    if (!foundFmt)
        throw std::runtime_error(
            "WAV fmt chunk not found");

    if (!foundData)
        throw std::runtime_error(
            "WAV data chunk not found");

    if (fmt.audioFormat != 1)
    {
        throw std::runtime_error(
            "Only uncompressed PCM WAV files are supported");
    }

    if (fmt.bitsPerSample != 16)
    {
        throw std::runtime_error(
            "Only 16-bit WAV files are supported");
    }

    if (fmt.sampleRate != SampleRate)
    {
        throw std::runtime_error(
            "WAV must be sampled at 16000 Hz");
    }

    if (fmt.numChannels != 1 &&
        fmt.numChannels != 2)
    {
        throw std::runtime_error(
            "Only mono and stereo WAV files are supported");
    }

    const int bytesPerSample = 2;

    const size_t frameCount =
        audioData.size() /
        (bytesPerSample * fmt.numChannels);

    std::vector<float> audio;
    audio.resize(frameCount);

    const int16_t* samples =
        reinterpret_cast<const int16_t*>(
            audioData.data());

    for (size_t i = 0; i < frameCount; ++i)
    {
        if (fmt.numChannels == 1)
        {
            audio[i] =
                static_cast<float>(
                    samples[i]) / 32768.0f;
        }
        else
        {
            float left =
                static_cast<float>(
                    samples[i * 2]) / 32768.0f;

            float right =
                static_cast<float>(
                    samples[i * 2 + 1]) / 32768.0f;

            audio[i] =
                0.5f * (left + right);
        }
    }

    return audio;
}


// ============================================================
// Mel frequency conversion
// ============================================================

float WhisperSpeechRecognizer::hzToMel(float hz)
{
    // Slaney mel scale.
    if (hz < 1000.0f)
        return 3.0f * hz / 200.0f;

    return 15.0f +
           27.0f *
               std::log(hz / 1000.0f) /
               std::log(6.4f);
}


float WhisperSpeechRecognizer::melToHz(float mel)
{
    if (mel < 15.0f)
        return 200.0f * mel / 3.0f;

    return 1000.0f *
           std::exp(
               (mel - 15.0f) *
               std::log(6.4f) /
               27.0f);
}


// ============================================================
// Mel filter bank
// ============================================================

std::vector<float>
WhisperSpeechRecognizer::createMelFilterBank()
{
    constexpr int NumFrequencies =
        FFTSize / 2 + 1;

    constexpr float MinFrequency = 0.0f;
    constexpr float MaxFrequency = 8000.0f;

    constexpr int NumFilters = NumMelBins;

    const float minMel =
        hzToMel(MinFrequency);

    const float maxMel =
        hzToMel(MaxFrequency);

    std::vector<float> melPoints(
        NumFilters + 2);

    for (int i = 0;
         i < NumFilters + 2;
         ++i)
    {
        float mel =
            minMel +
            (maxMel - minMel) *
                static_cast<float>(i) /
                static_cast<float>(NumFilters + 1);

        melPoints[i] =
            melToHz(mel);
    }

    std::vector<float> filterBank(
        NumFilters * NumFrequencies,
        0.0f);

    for (int m = 0;
         m < NumFilters;
         ++m)
    {
        const float left =
            melPoints[m];

        const float center =
            melPoints[m + 1];

        const float right =
            melPoints[m + 2];

        for (int k = 0;
             k < NumFrequencies;
             ++k)
        {
            float frequency =
                static_cast<float>(k) *
                SampleRate /
                static_cast<float>(FFTSize);

            float value = 0.0f;

            if (frequency >= left &&
                frequency <= center &&
                center > left)
            {
                value =
                    (frequency - left) /
                    (center - left);
            }
            else if (frequency > center &&
                     frequency <= right &&
                     right > center)
            {
                value =
                    (right - frequency) /
                    (right - center);
            }

            // Slaney normalization.
            if (right > left)
            {
                value *=
                    2.0f /
                    (right - left);
            }

            filterBank[
                m * NumFrequencies + k] =
                value;
        }
    }

    return filterBank;
}


// ============================================================
// Mixed-radix FFT
// ============================================================

void WhisperSpeechRecognizer::fftRecursive(
    const std::vector<std::complex<float>>& input,
    std::vector<std::complex<float>>& output)
{
    const size_t n = input.size();

    output.resize(n);

    if (n == 1)
    {
        output[0] = input[0];
        return;
    }

    size_t factor = 0;

    if (n % 2 == 0)
        factor = 2;
    else if (n % 5 == 0)
        factor = 5;
    else
        throw std::runtime_error(
            "Unsupported FFT factor");

    const size_t m = n / factor;

    std::vector<std::vector<std::complex<float>>> subFFT(
        factor);

    for (size_t r = 0; r < factor; ++r)
    {
        std::vector<std::complex<float>> subInput(m);

        for (size_t k = 0; k < m; ++k)
        {
            subInput[k] =
                input[r + factor * k];
        }

        fftRecursive(
            subInput,
            subFFT[r]);
    }

    for (size_t k1 = 0; k1 < m; ++k1)
    {
        for (size_t k2 = 0;
             k2 < factor;
             ++k2)
        {
            std::complex<float> sum(0.0f, 0.0f);

            for (size_t r = 0;
                 r < factor;
                 ++r)
            {
                float angle =
                    -TwoPi *
                    (
                        static_cast<float>(r * k1) /
                            static_cast<float>(n)
                        +
                        static_cast<float>(r * k2) /
                            static_cast<float>(factor)
                        );

                std::complex<float> twiddle(
                    std::cos(angle),
                    std::sin(angle));

                sum +=
                    subFFT[r][k1] *
                    twiddle;
            }

            output[
                k1 + m * k2] =
                sum;
        }
    }
}


void WhisperSpeechRecognizer::fft400(
    const std::vector<float>& input,
    std::vector<std::complex<float>>& output)
{
    if (input.size() != FFTSize)
    {
        throw std::runtime_error(
            "fft400 requires exactly 400 samples");
    }

    std::vector<std::complex<float>> complexInput(
        FFTSize);

    for (int i = 0; i < FFTSize; ++i)
    {
        complexInput[i] =
            std::complex<float>(
                input[i],
                0.0f);
    }

    fftRecursive(
        complexInput,
        output);
}


// ============================================================
// Whisper log-mel spectrogram
// ============================================================

std::vector<float>
WhisperSpeechRecognizer::createMelSpectrogram(
    const std::vector<float>& inputAudio)
{
    // Whisper expects exactly 30 seconds.
    std::vector<float> audio(
        NumSamples,
        0.0f);

    const size_t copyCount =
        std::min(
            inputAudio.size(),
            static_cast<size_t>(NumSamples));

    std::copy_n(
        inputAudio.begin(),
        copyCount,
        audio.begin());

    const auto filterBank =
        createMelFilterBank();

    constexpr int NumFrequencies =
        FFTSize / 2 + 1;

    // Output is [80, 3000].
    std::vector<float> mel(
        NumMelBins * NumFrames,
        0.0f);

    // torch.stft(center=True) pads FFTSize/2 on both
    // sides. Whisper then removes the final frame.
    std::vector<float> padded(
        NumSamples + FFTSize,
        0.0f);

    std::copy(
        audio.begin(),
        audio.end(),
        padded.begin() + FFTSize / 2);

    std::vector<float> window(FFTSize);

    for (int n = 0; n < FFTSize; ++n)
    {
        window[n] =
            0.5f *
            (
                1.0f -
                std::cos(
                    TwoPi *
                    static_cast<float>(n) /
                    static_cast<float>(FFTSize))
                );
    }

    std::vector<float> frame(FFTSize);

    std::vector<std::complex<float>> spectrum;

    for (int frameIndex = 0;
         frameIndex < NumFrames;
         ++frameIndex)
    {
        const int start =
            frameIndex * HopLength;

        for (int n = 0;
             n < FFTSize;
             ++n)
        {
            frame[n] =
                padded[start + n] *
                window[n];
        }

        fft400(
            frame,
            spectrum);

        // Power spectrum.
        float power[
            NumFrequencies];

        for (int k = 0;
             k < NumFrequencies;
             ++k)
        {
            power[k] =
                std::norm(spectrum[k]);
        }

        // Apply 80 mel filters.
        for (int m = 0;
             m < NumMelBins;
             ++m)
        {
            float energy = 0.0f;

            for (int k = 0;
                 k < NumFrequencies;
                 ++k)
            {
                energy +=
                    filterBank[
                        m * NumFrequencies + k] *
                    power[k];
            }

            // Whisper uses log10(max(mel, 1e-10)).
            float logEnergy =
                std::log10(
                    std::max(
                        energy,
                        1.0e-10f));

            mel[
                m * NumFrames +
                frameIndex] =
                logEnergy;
        }
    }

    // Whisper's dynamic range normalization.
    float maximum =
        -std::numeric_limits<float>::infinity();

    for (float value : mel)
        maximum = std::max(maximum, value);

    const float floor =
        maximum - 8.0f;

    for (float& value : mel)
    {
        value =
            std::max(value, floor);

        value =
            (value + 4.0f) / 4.0f;
    }

    return mel;
}


// ============================================================
// Encoder
// ============================================================

std::vector<float>
WhisperSpeechRecognizer::runEncoder(
    const std::vector<float>& melSpectrogram)
{
    if (melSpectrogram.size() !=
        NumMelBins * NumFrames)
    {
        throw std::runtime_error(
            "Invalid Whisper mel spectrogram size");
    }

    Ort::MemoryInfo memoryInfo =
        Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator,
            OrtMemTypeDefault);

    std::array<int64_t, 3> inputShape{
        1,
        NumMelBins,
        NumFrames
    };

    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(
            memoryInfo,
            const_cast<float*>(
                melSpectrogram.data()),
            melSpectrogram.size(),
            inputShape.data(),
            inputShape.size());

    const char* inputName =
        "input_features";

    const char* outputName =
        "last_hidden_state";

    auto outputs =
        encoderSession_->Run(
            Ort::RunOptions{nullptr},
            &inputName,
            &inputTensor,
            1,
            &outputName,
            1);

    float* outputData =
        outputs[0].GetTensorMutableData<float>();

    const auto outputShape =
        outputs[0].GetTensorTypeAndShapeInfo()
            .GetShape();

    if (outputShape.size() != 3 ||
        outputShape[0] != 1 ||
        outputShape[1] != 1500 ||
        outputShape[2] != 512)
    {
        throw std::runtime_error(
            "Unexpected Whisper encoder output shape");
    }

    constexpr size_t outputSize =
        1500 * 512;

    return std::vector<float>(
        outputData,
        outputData + outputSize);
}


// ============================================================
// Decoder
// ============================================================

int64_t WhisperSpeechRecognizer::generateNextToken(
    const std::vector<int64_t>& tokenIds,
    const std::vector<float>& encoderOutput)
{
    Ort::MemoryInfo memoryInfo =
        Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator,
            OrtMemTypeDefault);

    std::array<int64_t, 2> inputIdsShape{
        1,
        static_cast<int64_t>(tokenIds.size())
    };

    std::array<int64_t, 3> encoderShape{
        1,
        1500,
        512
    };

    Ort::Value inputIdsTensor =
        Ort::Value::CreateTensor<int64_t>(
            memoryInfo,
            const_cast<int64_t*>(
                tokenIds.data()),
            tokenIds.size(),
            inputIdsShape.data(),
            inputIdsShape.size());

    Ort::Value encoderTensor =
        Ort::Value::CreateTensor<float>(
            memoryInfo,
            const_cast<float*>(
                encoderOutput.data()),
            encoderOutput.size(),
            encoderShape.data(),
            encoderShape.size());

    const char* inputNames[] = {
        "input_ids",
        "encoder_hidden_states"
    };

    const char* outputName =
        "logits";

    Ort::Value inputs[] = {
        std::move(inputIdsTensor),
        std::move(encoderTensor)
    };

    auto outputs =
        decoderSession_->Run(
            Ort::RunOptions{nullptr},
            inputNames,
            inputs,
            2,
            &outputName,
            1);

    auto shape =
        outputs[0]
            .GetTensorTypeAndShapeInfo()
            .GetShape();

    if (shape.size() != 3)
    {
        throw std::runtime_error(
            "Unexpected Whisper decoder output");
    }

    const int sequenceLength =
        static_cast<int>(shape[1]);

    const int vocabularySize =
        static_cast<int>(shape[2]);

    if (vocabularySize != VocabSize)
    {
        throw std::runtime_error(
            "Unexpected Whisper vocabulary size");
    }

    const float* logits =
        outputs[0].GetTensorData<float>();

    // We want logits from the final decoder position.
    const float* lastLogits =
        logits +
        (sequenceLength - 1) *
            vocabularySize;

    // The generation configuration forces
    // <|notimestamps|> immediately after
    // <|startoftranscript|>.
    if (tokenIds.size() == 1)
        return NoTimestampsToken;

    int bestToken = EndOfTextToken;
    float bestScore =
        -std::numeric_limits<float>::infinity();

    for (int token = 0;
         token < vocabularySize;
         ++token)
    {
        // Never generate EOS before we have produced
        // some actual text.
        if (token == EndOfTextToken &&
            tokenIds.size() <= 2)
        {
            continue;
        }

        // Do not generate the special control tokens.
        if (token >= 50256 &&
            token != EndOfTextToken)
        {
            continue;
        }

        float score =
            lastLogits[token];

        if (score > bestScore)
        {
            bestScore = score;
            bestToken = token;
        }
    }

    return bestToken;
}


// ============================================================
// Token decoding
// ============================================================

std::string
WhisperSpeechRecognizer::decodeTokens(
    const std::vector<int32_t>& tokenIds)
{
    if (!tokenizer_)
        throw std::runtime_error(
            "Whisper tokenizer is not initialized");

    return tokenizer_->Decode(tokenIds);
}


// ============================================================
// Public transcription function
// ============================================================

std::string
WhisperSpeechRecognizer::transcribeWav(
    const std::string& filename)
{
    if (!initialized_)
        initialize();

    qDebug() << "Loading WAV:"
             << QString::fromStdString(filename);

    std::vector<float> audio =
        loadWavFile(filename);

    qDebug() << "Audio samples:"
             << audio.size();

    std::vector<float> mel =
        createMelSpectrogram(audio);

    qDebug() << "Created mel spectrogram:"
             << mel.size();

    std::vector<float> encoderOutput =
        runEncoder(mel);

    qDebug() << "Encoder complete.";

    // Initial Whisper decoder sequence.
    std::vector<int64_t> tokens;

    tokens.push_back(
        StartOfTranscriptToken);

    tokens.push_back(
        NoTimestampsToken);

    constexpr int MaxTokens = 448;

    while (tokens.size() < MaxTokens)
    {
        int64_t nextToken =
            generateNextToken(
                tokens,
                encoderOutput);

        if (nextToken == EndOfTextToken)
            break;

        tokens.push_back(nextToken);

        qDebug() << "Whisper token:"
                 << nextToken;
    }

    // Convert ONNX int64 token IDs back to int32 for
    // tokenizers-cpp decoding.
    std::vector<int32_t> decodeTokenIds;

    decodeTokenIds.reserve(tokens.size());

    for (int64_t token : tokens)
    {
        decodeTokenIds.push_back(
            static_cast<int32_t>(token));
    }

    std::string result =
        decodeTokens(decodeTokenIds);

    return result;
}


std::string WhisperSpeechRecognizer::transcribeAudio(
    const std::vector<float>& audio)
{
    if (!initialized_)
        initialize();

    qDebug() << "Audio samples:"
             << audio.size();

    std::vector<float> mel =
        createMelSpectrogram(audio);

    qDebug() << "Created mel spectrogram:"
             << mel.size();

    std::vector<float> encoderOutput =
        runEncoder(mel);

    qDebug() << "Encoder complete.";

    std::vector<int64_t> tokens;

    tokens.push_back(
        StartOfTranscriptToken);

    tokens.push_back(
        NoTimestampsToken);

    constexpr int MaxTokens = 448;

    while (tokens.size() < MaxTokens)
    {
        int64_t nextToken =
            generateNextToken(
                tokens,
                encoderOutput);

        if (nextToken == EndOfTextToken)
            break;

        tokens.push_back(nextToken);

        qDebug() << "Whisper token:"
                 << nextToken;
    }

    std::vector<int32_t> decodeTokenIds;

    decodeTokenIds.reserve(tokens.size());

    for (int64_t token : tokens)
    {
        decodeTokenIds.push_back(
            static_cast<int32_t>(token));
    }

    return decodeTokens(decodeTokenIds);
}