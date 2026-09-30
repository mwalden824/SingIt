#ifndef WHISPERSPEECHRECOGNIZER_H
#define WHISPERSPEECHRECOGNIZER_H

#include <string>
#include <vector>
#include <complex>
#include <memory>

#include <onnxruntime_cxx_api.h>
#include <tokenizers_cpp.h>

class WhisperSpeechRecognizer
{
public:
    WhisperSpeechRecognizer();

    // Loads the Whisper ONNX models and tokenizer.
    void initialize();

    // Transcribes a 16-bit PCM WAV file.
    //
    // Initial version requirements:
    //   - PCM
    //   - 16-bit
    //   - 16,000 Hz
    //   - mono or stereo
    //
    // Audio is converted to mono, padded/truncated to 30 seconds,
    // and passed through Whisper.
    std::string transcribeWav(const std::string& filename);
    std::string transcribeAudio(const std::vector<float>& audio);
private:
    static constexpr int SampleRate = 16000;
    static constexpr int NumSamples = 480000;   // 30 seconds
    static constexpr int NumMelBins = 80;
    static constexpr int NumFrames = 3000;
    static constexpr int FFTSize = 400;
    static constexpr int HopLength = 160;

    static constexpr int VocabSize = 51864;

    static constexpr int StartOfTranscriptToken = 50257;
    static constexpr int EndOfTextToken = 50256;
    static constexpr int NoTimestampsToken = 50362;

    Ort::Env env_;
    Ort::SessionOptions sessionOptions_;

    std::unique_ptr<Ort::Session> encoderSession_;
    std::unique_ptr<Ort::Session> decoderSession_;

    std::unique_ptr<tokenizers::Tokenizer> tokenizer_;

    bool initialized_ = false;

    std::vector<float> loadWavFile(const std::string& filename);

    std::vector<float> createMelSpectrogram(
        const std::vector<float>& audio);

    void fft400(
        const std::vector<float>& input,
        std::vector<std::complex<float>>& output);

    void fftRecursive(
        const std::vector<std::complex<float>>& input,
        std::vector<std::complex<float>>& output);

    std::vector<float> runEncoder(
        const std::vector<float>& melSpectrogram);

    int64_t generateNextToken(
        const std::vector<int64_t>& tokenIds,
        const std::vector<float>& encoderOutput);

    std::string decodeTokens(
        const std::vector<int32_t>& tokenIds);

    std::vector<float> createMelFilterBank();

    static float hzToMel(float hz);
    static float melToHz(float mel);
};

#endif // WHISPERSPEECHRECOGNIZER_H