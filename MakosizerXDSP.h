#pragma once

#include <array>
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
    int oversamplingFactor = 1;
};

class MakosizerXDSP final
{
public:
    MakosizerXDSP();

    void prepareToPlay (double sampleRate);
    void reset() noexcept;
    void process (float* const* channels, int numChannels, int numSamples,
                  const DSPParameters& parameters) noexcept;

    int getLatencySamples() const noexcept { return latencySamples; }
    float getLimiterGainReductionDb() const noexcept { return limiterGainReductionDb; }
    float getLimiterInputPeak() const noexcept { return limiterInputPeak; }
    float getLimiterOutputPeak() const noexcept { return limiterOutputPeak; }

private:
    struct ChannelState
    {
        // 2-pole Butterworth HPF & LPF state
        float hpfZ1 = 0.0f;
        float hpfZ2 = 0.0f;
        float lpfZ1 = 0.0f;
        float lpfZ2 = 0.0f;

        // Vinyl warmth & resonance filters
        float vinylLowPass = 0.0f;
        float vinylMidBump = 0.0f;
        float vinylSubFilter = 0.0f;
        float crackleFilter = 0.0f;

        // Noise coloring filter
        float noiseLowPass = 0.0f;
        float noiseHighPass = 0.0f;
        float prevNoiseWhite = 0.0f;

        // Dust dropout & crackle state
        float dropoutEnvelope = 1.0f;
        int dropoutSamplesRemaining = 0;

        // Sample-and-hold crush state
        float heldSample = 0.0f;
        float sampleHoldPhase = 0.0f;

        // SP-303 Vinyl Sim style compressor state (RMS + Peak + HF pre-emphasis)
        float compDetectorEnv = 0.0f;
        float compGainSmooth = 1.0f;
        float compPreEmphasisState = 0.0f;

        // Wow & Flutter modulation
        double wowPhase = 0.0;
        double wowDriftPhase = 0.0;
        double flutterPhase = 0.0;
        double flutterSubPhase = 0.0;

        // Tube saturation DC blocker
        float dcBlockIn = 0.0f;
        float dcBlockOut = 0.0f;

        // Oversampling anti-aliasing filter state
        float osUpPrev = 0.0f;
        float osDownZ1 = 0.0f;
        float osDownZ2 = 0.0f;

        std::uint32_t randomState = 1;
    };

    static float clamp01 (float value) noexcept;
    static float readDelay (const std::vector<float>& line, std::size_t writePosition,
                            float delaySamples) noexcept;
    static std::uint32_t nextRandom (std::uint32_t& state) noexcept;
    static float randomUnit (std::uint32_t& state) noexcept;
    static float randomBipolar (std::uint32_t& state) noexcept;

    float processNonlinearSubSample (ChannelState& state, float input, float drive,
                                     float destroy, float vinyl, float crush,
                                     float effectiveSampleRate) noexcept;

    std::array<ChannelState, 2> channelStates;
    std::array<std::vector<float>, 4> delayLines;
    double currentSampleRate = 44100.0;
    std::size_t writePosition = 0;
    int latencySamples = 220;
    float limiterEnvelope = 1.0f;
    float limiterGainReductionDb = 0.0f;
    float limiterInputPeak = 0.0f;
    float limiterOutputPeak = 0.0f;
};
}
