#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace MakosizerX
{
struct DSPParameters
{
    float drive = 0.0f;
    float vinyl = 0.0f;
    float destroy = 0.0f;
    float wow = 0.0f;
    float flutter = 0.0f;
    float noise = 0.0f;
    float dust = 0.0f;
    float crush = 0.0f;
    float comp = 0.0f;
    float hpf = 20.0f;
    float lpf = 20000.0f;
    float limiter = 0.0f;
    float width = 1.0f;
    float mix = 1.0f;
};

namespace detail
{
// Linear-phase half-band FIR stages (65 taps, Kaiser window) used by the
// oversampler. Both stages are allocation-free once constructed.
constexpr int halfbandOddTaps = 32;
constexpr int halfbandCentreDelay = 16;      // in input samples of the stage
constexpr int halfbandLatencyHighRate = 64;  // up + down, in samples at the stage's high rate

class HalfbandUpsampler final
{
public:
    HalfbandUpsampler() noexcept;
    void reset() noexcept;
    // Writes 2 * numInput samples to output.
    void process (const float* input, float* output, int numInput) noexcept;

private:
    std::array<float, 2 * halfbandOddTaps> history {};
    int position = 0;
};

class HalfbandDownsampler final
{
public:
    HalfbandDownsampler() noexcept;
    void reset() noexcept;
    // Reads 2 * numOutput samples from input and writes numOutput samples to
    // output. Input and output may point at the same buffer.
    void process (const float* input, float* output, int numOutput) noexcept;

private:
    std::array<float, 2 * halfbandOddTaps> evenHistory {};
    std::array<float, 2 * halfbandOddTaps> oddHistory {};
    int evenPosition = 0;
    int oddPosition = 0;
};
}

class MakosizerXDSP final
{
public:
    static constexpr int maximumOversamplingFactor = 8;

    MakosizerXDSP();

    void prepareToPlay (double sampleRate);
    void reset() noexcept;

    // Accepts 1, 2, 4 or 8 (anything else selects 1). Does not allocate, so it
    // can be called from the audio thread. Clears the internal state.
    void setOversamplingFactor (int factor) noexcept;
    int getOversamplingFactor() const noexcept { return oversamplingFactor; }

    void process (float* const* channels, int numChannels, int numSamples,
                  const DSPParameters& parameters) noexcept;

    // Total latency in host-rate samples (delay line + oversampling filters).
    int getLatencySamples() const noexcept { return reportedLatency.load (std::memory_order_relaxed); }

private:
    struct ChannelState
    {
        float previousHighPassInput = 0.0f;
        float highPassOutput = 0.0f;
        float lowPassOutput = 0.0f;
        float vinylLowPassOutput = 0.0f;
        float compressorEnvelope = 0.0f;
        double wowPhase = 0.0;
        double flutterPhase = 0.0;
        std::uint32_t randomState = 1;
    };

    struct Derived;

    static constexpr int chunkSize = 64;

    static float clamp01 (float value) noexcept;
    static float readDelay (const std::vector<float>& line, std::size_t length,
                            std::size_t writePosition, float delaySamples) noexcept;
    static std::uint32_t nextRandom (std::uint32_t& state) noexcept;
    static float randomUnit (std::uint32_t& state) noexcept;
    static float randomBipolar (std::uint32_t& state) noexcept;

    DSPParameters sanitise (const DSPParameters& parameters) const noexcept;
    void updateSmoothing (const DSPParameters& target, int numSamples) noexcept;
    Derived computeDerived (const DSPParameters& values) const noexcept;
    void applyOversamplingFactor (int factor) noexcept;
    void processChunk (std::array<float*, 2>& chunkChannels, int activeChannels,
                       int numSamples, const DSPParameters& target) noexcept;
    void runCore (float* const* channels, int activeChannels, int numSamples,
                  const Derived& derived) noexcept;

    std::array<ChannelState, 2> channelStates;
    std::array<std::vector<float>, 4> delayLines;
    std::array<std::array<detail::HalfbandUpsampler, 2>, 3> upsamplers;
    std::array<std::array<detail::HalfbandDownsampler, 2>, 3> downsamplers;
    std::array<std::array<std::vector<float>, 2>, 2> scratch;

    DSPParameters smoothed;
    bool snapSmoothing = true;

    double hostSampleRate = 44100.0;
    double workingSampleRate = 44100.0;
    int oversamplingFactor = 1;
    int oversamplingStages = 0;
    int coreLatencySamples = 220;  // at the working (oversampled) rate
    std::size_t activeDelayLength = 0;
    std::size_t writePosition = 0;
    std::atomic<int> reportedLatency { 220 };
};
}
