#include "MakosizerXDSP.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace MakosizerX
{
namespace
{
constexpr double twoPi = 6.28318530717958647692;
constexpr double pi = 3.14159265358979323846;
constexpr float maximumFilterFrequency = 20000.0f;
constexpr float parameterSmoothingSeconds = 0.020f;
constexpr std::array<std::uint32_t, 2> randomSeeds {{ 0x13579bdfu, 0x2468ace1u }};

float effectiveMacroValue (float control, float destroy) noexcept
{
    return control + destroy * (1.0f - control);
}

float effectiveDestructiveMacroValue (float control, float destroy) noexcept
{
    return effectiveMacroValue (control, std::sqrt (destroy));
}

double besselI0 (double x) noexcept
{
    double sum = 1.0;
    double term = 1.0;
    const auto halfX = x * 0.5;

    for (int k = 1; k < 60; ++k)
    {
        term *= (halfX / k) * (halfX / k);
        sum += term;
    }

    return sum;
}

// Half-band low-pass, 65 taps, Kaiser window (beta 8). Only the 32 odd-offset
// taps are non-zero (besides the centre tap, which is 0.5 and handled
// separately), so they are all that is stored.
struct HalfbandCoefficients
{
    std::array<float, detail::halfbandOddTaps> down {};
    std::array<float, detail::halfbandOddTaps> up {};

    HalfbandCoefficients()
    {
        constexpr double beta = 8.0;
        constexpr int halfLength = 32;
        std::array<double, detail::halfbandOddTaps> taps {};
        double sum = 0.0;

        for (int i = 0; i < detail::halfbandOddTaps; ++i)
        {
            const auto offset = static_cast<double> (2 * i - 31);
            const auto ideal = std::sin (pi * offset * 0.5) / (pi * offset);
            const auto ratio = offset / static_cast<double> (halfLength);
            const auto window = besselI0 (beta * std::sqrt (1.0 - ratio * ratio))
                                / besselI0 (beta);
            taps[static_cast<std::size_t> (i)] = ideal * window;
            sum += taps[static_cast<std::size_t> (i)];
        }

        // Normalise so the filter has exactly unity gain at DC.
        const auto scale = 0.5 / sum;
        for (std::size_t i = 0; i < taps.size(); ++i)
        {
            down[i] = static_cast<float> (taps[i] * scale);
            up[i] = static_cast<float> (taps[i] * scale * 2.0);
        }
    }
};

const HalfbandCoefficients& halfbandCoefficients()
{
    static const HalfbandCoefficients coefficients;
    return coefficients;
}
}

namespace detail
{
HalfbandUpsampler::HalfbandUpsampler() noexcept
{
    halfbandCoefficients();
}

void HalfbandUpsampler::reset() noexcept
{
    history.fill (0.0f);
    position = 0;
}

void HalfbandUpsampler::process (const float* input, float* output, int numInput) noexcept
{
    const auto& coefficients = halfbandCoefficients().up;

    for (int sample = 0; sample < numInput; ++sample)
    {
        position = position == 0 ? halfbandOddTaps - 1 : position - 1;
        const auto value = input[sample];
        history[static_cast<std::size_t> (position)] = value;
        history[static_cast<std::size_t> (position + halfbandOddTaps)] = value;

        const float* newestFirst = history.data() + position;
        float accumulator = 0.0f;
        for (int tap = 0; tap < halfbandOddTaps; ++tap)
            accumulator += coefficients[static_cast<std::size_t> (tap)] * newestFirst[tap];

        output[2 * sample] = newestFirst[halfbandCentreDelay];
        output[2 * sample + 1] = accumulator;
    }
}

HalfbandDownsampler::HalfbandDownsampler() noexcept
{
    halfbandCoefficients();
}

void HalfbandDownsampler::reset() noexcept
{
    evenHistory.fill (0.0f);
    oddHistory.fill (0.0f);
    evenPosition = 0;
    oddPosition = 0;
}

void HalfbandDownsampler::process (const float* input, float* output, int numOutput) noexcept
{
    const auto& coefficients = halfbandCoefficients().down;

    for (int sample = 0; sample < numOutput; ++sample)
    {
        const auto even = input[2 * sample];
        const auto odd = input[2 * sample + 1];

        evenPosition = evenPosition == 0 ? halfbandOddTaps - 1 : evenPosition - 1;
        evenHistory[static_cast<std::size_t> (evenPosition)] = even;
        evenHistory[static_cast<std::size_t> (evenPosition + halfbandOddTaps)] = even;

        const float* oddNewestFirst = oddHistory.data() + oddPosition;
        auto accumulator = 0.5f * evenHistory[static_cast<std::size_t> (
                                      evenPosition + halfbandCentreDelay)];
        for (int tap = 0; tap < halfbandOddTaps; ++tap)
            accumulator += coefficients[static_cast<std::size_t> (tap)] * oddNewestFirst[tap];

        output[sample] = accumulator;

        oddPosition = oddPosition == 0 ? halfbandOddTaps - 1 : oddPosition - 1;
        oddHistory[static_cast<std::size_t> (oddPosition)] = odd;
        oddHistory[static_cast<std::size_t> (oddPosition + halfbandOddTaps)] = odd;
    }
}
}

struct MakosizerXDSP::Derived
{
    float drive = 0.0f;
    float vinyl = 0.0f;
    float comp = 0.0f;
    float crush = 0.0f;
    float limiter = 0.0f;
    float width = 1.0f;
    float mix = 1.0f;
    float wow = 0.0f;
    float flutter = 0.0f;
    float noiseGain = 0.0f;
    float dust = 0.0f;
    float dustProbability = 0.0f;
    float dustAmplitude = 0.0f;
    float highPassCoefficient = 0.0f;
    float lowPassCoefficient = 0.0f;
    float vinylCoefficient = 0.0f;
    float compressorAttack = 0.0f;
    float compressorRelease = 0.0f;
    float compressorRatio = 1.0f;
    float crushLevels = 1.0f;
    float driveGain = 1.0f;
    float driveMakeup = 1.0f;
    float baseDelay = 0.0f;
    float wowDepth = 0.0f;
    float flutterDepth = 0.0f;
    float limiterThreshold = 1.0f;
    double wowPhaseStep = 0.0;
    double flutterPhaseStep = 0.0;
};

MakosizerXDSP::MakosizerXDSP()
{
    prepareToPlay (hostSampleRate);
}

void MakosizerXDSP::prepareToPlay (double sampleRate)
{
    hostSampleRate = std::isfinite (sampleRate) ? std::max (1000.0, sampleRate) : 44100.0;

    // Allocate for the largest oversampling factor so that the factor can be
    // changed later on the audio thread without allocating.
    const auto maximumRate = hostSampleRate * maximumOversamplingFactor;
    const auto delayCapacity = static_cast<std::size_t> (std::ceil (maximumRate * 0.005))
                               + static_cast<std::size_t> (std::ceil (maximumRate * 0.0040))
                               + 8;
    for (auto& line : delayLines)
        line.assign (delayCapacity, 0.0f);

    for (auto& channelScratch : scratch)
        for (auto& buffer : channelScratch)
            buffer.assign (static_cast<std::size_t> (chunkSize * maximumOversamplingFactor), 0.0f);

    applyOversamplingFactor (oversamplingFactor);
}

void MakosizerXDSP::setOversamplingFactor (int factor) noexcept
{
    const auto sanitised = (factor == 2 || factor == 4 || factor == 8) ? factor : 1;
    if (sanitised != oversamplingFactor)
        applyOversamplingFactor (sanitised);
}

void MakosizerXDSP::applyOversamplingFactor (int factor) noexcept
{
    oversamplingFactor = factor;
    oversamplingStages = factor == 8 ? 3 : (factor == 4 ? 2 : (factor == 2 ? 1 : 0));
    workingSampleRate = hostSampleRate * factor;
    // 5 ms, rounded to a whole number of host-rate samples so that the reported
    // latency is exact for every oversampling factor.
    coreLatencySamples = std::max (1, static_cast<int> (std::round (hostSampleRate * 0.005)))
                         * factor;

    const auto maximumModulationSamples = static_cast<std::size_t> (
        std::ceil (workingSampleRate * 0.0040));
    activeDelayLength = static_cast<std::size_t> (coreLatencySamples)
                        + maximumModulationSamples + 4;
    if (! delayLines[0].empty())
        activeDelayLength = std::min (activeDelayLength, delayLines[0].size());

    // Each stage adds halfbandLatencyHighRate samples at its own (doubled) rate.
    double filterLatency = 0.0;
    for (int stage = 0; stage < oversamplingStages; ++stage)
        filterLatency += detail::halfbandLatencyHighRate / static_cast<double> (2 << stage);

    reportedLatency.store (
        static_cast<int> (std::lround (static_cast<double> (coreLatencySamples) / factor
                                       + filterLatency)),
        std::memory_order_relaxed);

    reset();
}

void MakosizerXDSP::reset() noexcept
{
    for (auto& line : delayLines)
        std::fill (line.begin(), line.end(), 0.0f);

    for (auto& stages : upsamplers)
        for (auto& stage : stages)
            stage.reset();
    for (auto& stages : downsamplers)
        for (auto& stage : stages)
            stage.reset();

    writePosition = 0;
    snapSmoothing = true;
    for (std::size_t channel = 0; channel < channelStates.size(); ++channel)
    {
        channelStates[channel] = {};
        channelStates[channel].randomState = randomSeeds[channel];
        channelStates[channel].wowPhase = channel == 0 ? 0.0 : 0.17;
        channelStates[channel].flutterPhase = channel == 0 ? 0.0 : 0.31;
    }
}

float MakosizerXDSP::clamp01 (float value) noexcept
{
    return std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f;
}

float MakosizerXDSP::readDelay (const std::vector<float>& line, std::size_t length,
                                std::size_t position, float delaySamples) noexcept
{
    const auto lengthFloat = static_cast<float> (length);
    auto readPosition = static_cast<float> (position) - delaySamples;
    while (readPosition < 0.0f)
        readPosition += lengthFloat;

    auto firstIndex = static_cast<std::size_t> (readPosition);
    if (firstIndex >= length)
        firstIndex -= length;
    const auto secondIndex = (firstIndex + 1) % length;
    const auto fraction = readPosition - static_cast<float> (firstIndex);
    return line[firstIndex] + (line[secondIndex] - line[firstIndex]) * fraction;
}

std::uint32_t MakosizerXDSP::nextRandom (std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float MakosizerXDSP::randomUnit (std::uint32_t& state) noexcept
{
    return static_cast<float> (nextRandom (state) >> 8) * (1.0f / 16777216.0f);
}

float MakosizerXDSP::randomBipolar (std::uint32_t& state) noexcept
{
    return randomUnit (state) * 2.0f - 1.0f;
}

DSPParameters MakosizerXDSP::sanitise (const DSPParameters& parameters) const noexcept
{
    const auto filterLimit = std::min (maximumFilterFrequency,
                                       static_cast<float> (hostSampleRate) * 0.45f);

    DSPParameters result;
    result.drive = clamp01 (parameters.drive);
    result.vinyl = clamp01 (parameters.vinyl);
    result.destroy = clamp01 (parameters.destroy);
    result.wow = clamp01 (parameters.wow);
    result.flutter = clamp01 (parameters.flutter);
    result.noise = clamp01 (parameters.noise);
    result.dust = clamp01 (parameters.dust);
    result.crush = clamp01 (parameters.crush);
    result.comp = clamp01 (parameters.comp);
    result.hpf = std::clamp (std::isfinite (parameters.hpf) ? parameters.hpf : 20.0f,
                             20.0f, filterLimit);
    result.lpf = std::clamp (std::isfinite (parameters.lpf) ? parameters.lpf
                                                             : maximumFilterFrequency,
                             20.0f, filterLimit);
    result.limiter = clamp01 (parameters.limiter);
    result.width = std::clamp (std::isfinite (parameters.width) ? parameters.width : 1.0f,
                               0.0f, 2.0f);
    result.mix = clamp01 (parameters.mix);
    return result;
}

void MakosizerXDSP::updateSmoothing (const DSPParameters& target, int numSamples) noexcept
{
    if (snapSmoothing)
    {
        smoothed = target;
        snapSmoothing = false;
        return;
    }

    const auto alpha = 1.0f - std::exp (-static_cast<float> (numSamples)
                                        / (parameterSmoothingSeconds
                                           * static_cast<float> (hostSampleRate)));

    const auto smoothLinear = [alpha] (float& current, float goal)
    {
        if (current == goal)
            return;
        current += (goal - current) * alpha;
        if (std::abs (goal - current) < 1.0e-6f)
            current = goal;
    };
    const auto smoothFrequency = [alpha] (float& current, float goal)
    {
        if (current == goal)
            return;
        current = std::exp (std::log (current) + (std::log (goal) - std::log (current)) * alpha);
        if (std::abs (goal - current) < 1.0e-5f * goal)
            current = goal;
    };

    smoothLinear (smoothed.drive, target.drive);
    smoothLinear (smoothed.vinyl, target.vinyl);
    smoothLinear (smoothed.destroy, target.destroy);
    smoothLinear (smoothed.wow, target.wow);
    smoothLinear (smoothed.flutter, target.flutter);
    smoothLinear (smoothed.noise, target.noise);
    smoothLinear (smoothed.dust, target.dust);
    smoothLinear (smoothed.crush, target.crush);
    smoothLinear (smoothed.comp, target.comp);
    smoothFrequency (smoothed.hpf, target.hpf);
    smoothFrequency (smoothed.lpf, target.lpf);
    smoothLinear (smoothed.limiter, target.limiter);
    smoothLinear (smoothed.width, target.width);
    smoothLinear (smoothed.mix, target.mix);
}

MakosizerXDSP::Derived MakosizerXDSP::computeDerived (const DSPParameters& values) const noexcept
{
    Derived d;
    const auto destroy = values.destroy;
    const auto factor = static_cast<float> (oversamplingFactor);
    const auto hostRate = static_cast<float> (hostSampleRate);
    const auto workingRate = static_cast<float> (workingSampleRate);

    const auto drive = effectiveDestructiveMacroValue (values.drive, destroy);
    const auto vinyl = effectiveMacroValue (values.vinyl, destroy);
    const auto wow = effectiveDestructiveMacroValue (values.wow, destroy);
    const auto flutter = effectiveDestructiveMacroValue (values.flutter, destroy);
    const auto noise = effectiveMacroValue (values.noise, destroy);
    const auto dust = effectiveMacroValue (values.dust, destroy);
    const auto crush = effectiveMacroValue (values.crush, destroy);

    d.drive = drive;
    d.vinyl = vinyl;
    d.comp = values.comp;
    d.crush = crush;
    d.limiter = values.limiter;
    d.width = values.width;
    d.mix = values.mix;
    d.wow = wow;
    d.flutter = flutter;
    d.dust = dust;

    // White noise and single-sample clicks keep the same level in the host-rate
    // band whatever the oversampling factor: noise power is spread over a
    // wider band (so amplitude scales with sqrt(factor)), and a click is
    // spread by the decimation filter (so its height scales with factor while
    // it occurs factor times less often per working-rate sample).
    d.noiseGain = noise * 0.012f * std::sqrt (factor);
    d.dustProbability = dust * 0.00002f / factor;
    d.dustAmplitude = dust * factor;

    d.highPassCoefficient = std::exp (static_cast<float> (-twoPi) * values.hpf / workingRate);
    d.lowPassCoefficient = 1.0f - std::exp (static_cast<float> (-twoPi) * values.lpf
                                            / workingRate);
    const auto vinylCutoff = std::min (maximumFilterFrequency - vinyl * 9000.0f,
                                       hostRate * 0.45f);
    d.vinylCoefficient = 1.0f - std::exp (static_cast<float> (-twoPi) * vinylCutoff
                                          / workingRate);
    d.compressorAttack = std::exp (-1.0f / (0.012f * workingRate));
    d.compressorRelease = std::exp (-1.0f / (0.120f * workingRate));
    d.compressorRatio = 1.0f + values.comp * 7.0f;
    d.crushLevels = std::exp2 ((16.0f - crush * 12.0f) - 1.0f);
    d.driveGain = 1.0f + drive * 23.0f;
    d.driveMakeup = 1.0f / std::sqrt (d.driveGain);
    d.baseDelay = static_cast<float> (coreLatencySamples);
    d.wowDepth = static_cast<float> (workingSampleRate * 0.0028);
    d.flutterDepth = static_cast<float> (workingSampleRate * 0.0009);
    d.limiterThreshold = 1.0f - values.limiter * 0.25f;
    d.wowPhaseStep = twoPi * (0.5 + wow * 1.35) / workingSampleRate;
    d.flutterPhaseStep = twoPi * (7.0 + flutter * 18.0) / workingSampleRate;
    return d;
}

void MakosizerXDSP::process (float* const* channels, int numChannels, int numSamples,
                             const DSPParameters& parameters) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0
        || delayLines[0].empty())
        return;

    const auto activeChannels = std::min (numChannels, 2);
    const auto target = sanitise (parameters);

    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const auto chunk = std::min (chunkSize, numSamples - offset);
        std::array<float*, 2> chunkChannels {};
        for (int channel = 0; channel < activeChannels; ++channel)
            chunkChannels[static_cast<std::size_t> (channel)] = channels[channel] + offset;

        processChunk (chunkChannels, activeChannels, chunk, target);
    }
}

void MakosizerXDSP::processChunk (std::array<float*, 2>& chunkChannels, int activeChannels,
                                  int numSamples, const DSPParameters& target) noexcept
{
    updateSmoothing (target, numSamples);
    const auto derived = computeDerived (smoothed);

    auto working = chunkChannels;
    auto workingSamples = numSamples;

    if (oversamplingStages > 0)
    {
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto channelIndex = static_cast<std::size_t> (channel);
            const float* source = chunkChannels[channelIndex];
            float* destination = nullptr;
            auto count = numSamples;

            for (int stage = 0; stage < oversamplingStages; ++stage)
            {
                destination = scratch[channelIndex][static_cast<std::size_t> (stage & 1)].data();
                upsamplers[static_cast<std::size_t> (stage)][channelIndex]
                    .process (source, destination, count);
                source = destination;
                count *= 2;
            }

            working[channelIndex] = destination;
        }

        workingSamples = numSamples << oversamplingStages;
    }

    runCore (working.data(), activeChannels, workingSamples, derived);

    if (oversamplingStages > 0)
    {
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto channelIndex = static_cast<std::size_t> (channel);
            float* data = working[channelIndex];
            auto count = workingSamples;

            for (int stage = oversamplingStages - 1; stage > 0; --stage)
            {
                downsamplers[static_cast<std::size_t> (stage)][channelIndex]
                    .process (data, data, count / 2);
                count /= 2;
            }

            downsamplers[0][channelIndex].process (data, chunkChannels[channelIndex], count / 2);
        }
    }
}

void MakosizerXDSP::runCore (float* const* channels, int activeChannels, int numSamples,
                             const Derived& d) noexcept
{
    const auto length = activeDelayLength;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        std::array<float, 2> dryDelayed {};
        std::array<float, 2> wetDelayed {};

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto channelIndex = static_cast<std::size_t> (channel);
            auto& state = channelStates[channelIndex];
            const auto dryInput = channels[channel][sample];
            auto wet = dryInput;

            state.highPassOutput = d.highPassCoefficient
                                   * (state.highPassOutput + wet
                                      - state.previousHighPassInput);
            state.previousHighPassInput = wet;
            wet = state.highPassOutput;

            state.lowPassOutput += d.lowPassCoefficient * (wet - state.lowPassOutput);
            wet = state.lowPassOutput;

            const auto saturated = std::tanh (wet * d.driveGain) * d.driveMakeup;
            wet += d.drive * (saturated - wet);

            state.vinylLowPassOutput += d.vinylCoefficient
                                        * (wet - state.vinylLowPassOutput);
            wet += d.vinyl * (state.vinylLowPassOutput - wet);
            const auto vinylSaturation = std::tanh (wet * (1.0f + d.vinyl * 1.5f));
            wet += d.vinyl * 0.25f * (vinylSaturation - wet);

            auto& randomState = state.randomState;
            wet += randomBipolar (randomState) * d.noiseGain;
            if (d.dust > 0.0f && randomUnit (randomState) < d.dustProbability)
            {
                const auto click = 0.04f + randomUnit (randomState) * 0.32f;
                wet += randomBipolar (randomState) * click * d.dustAmplitude;
            }

            if (d.crush > 0.0f)
            {
                const auto bounded = std::clamp (wet, -1.0f, 1.0f);
                const auto quantized = std::round (bounded * d.crushLevels) / d.crushLevels;
                wet += d.crush * (quantized - wet);
            }

            const auto envelopeCoefficient = std::abs (wet) > state.compressorEnvelope
                                                 ? d.compressorAttack
                                                 : d.compressorRelease;
            state.compressorEnvelope = envelopeCoefficient * state.compressorEnvelope
                                       + (1.0f - envelopeCoefficient) * std::abs (wet);
            if (d.comp > 0.0f && state.compressorEnvelope > 0.5f)
            {
                const auto gain = std::pow (
                    0.5f / state.compressorEnvelope, 1.0f - 1.0f / d.compressorRatio);
                wet *= gain;
            }

            state.wowPhase += d.wowPhaseStep;
            state.flutterPhase += d.flutterPhaseStep;
            if (state.wowPhase >= twoPi)
                state.wowPhase -= twoPi;
            if (state.flutterPhase >= twoPi)
                state.flutterPhase -= twoPi;

            const auto modulation = static_cast<float> (
                std::sin (state.wowPhase) * d.wow * d.wowDepth
                + std::sin (state.flutterPhase) * d.flutter * d.flutterDepth);
            const auto modulatedDelay = std::max (1.0f, d.baseDelay + modulation);

            delayLines[channelIndex][writePosition] = dryInput;
            delayLines[channelIndex + 2][writePosition] = wet;
            dryDelayed[channelIndex] = readDelay (
                delayLines[channelIndex], length, writePosition, d.baseDelay);
            wetDelayed[channelIndex] = readDelay (
                delayLines[channelIndex + 2], length, writePosition, modulatedDelay);
        }

        if (activeChannels == 2)
        {
            const auto mid = (wetDelayed[0] + wetDelayed[1]) * 0.5f;
            const auto side = (wetDelayed[0] - wetDelayed[1]) * 0.5f * d.width;
            wetDelayed[0] = mid + side;
            wetDelayed[1] = mid - side;
        }

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto channelIndex = static_cast<std::size_t> (channel);
            auto output = dryDelayed[channelIndex]
                          + d.mix * (wetDelayed[channelIndex] - dryDelayed[channelIndex]);

            if (d.limiter > 0.0f)
            {
                const auto magnitude = std::abs (output);
                if (magnitude > d.limiterThreshold)
                {
                    const auto range = std::max (0.0001f, 1.0f - d.limiterThreshold);
                    const auto limitedMagnitude = d.limiterThreshold
                                                  + range * std::tanh (
                                                      (magnitude - d.limiterThreshold) / range);
                    const auto limited = std::copysign (limitedMagnitude, output);
                    output += d.limiter * (limited - output);
                }
            }

            channels[channel][sample] = output;
        }

        writePosition = (writePosition + 1) % length;
    }
}
}
