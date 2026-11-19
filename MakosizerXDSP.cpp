#include "MakosizerXDSP.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace MakosizerX
{
namespace
{
constexpr double twoPi = 6.28318530717958647692;
constexpr float maximumFilterFrequency = 20000.0f;
constexpr std::array<std::uint32_t, 2> randomSeeds {{ 0x13579bdfu, 0x2468ace1u }};

float effectiveMacroValue (float control, float destroy) noexcept
{
    return std::clamp (control + destroy * 0.45f * (1.0f - control), 0.0f, 1.0f);
}

// Asymmetric triode tube saturation curve (generates warm 2nd and 3rd harmonics)
inline float tubeSaturate (float x, float amount) noexcept
{
    if (amount <= 0.0001f)
        return x;

    const float preGain = 1.0f + amount * 14.0f;
    const float bias = amount * 0.16f;
    const float driven = x * preGain + bias;

    float shaped = 0.0f;
    if (driven >= 0.0f)
    {
        shaped = std::tanh (driven * 1.15f);
    }
    else
    {
        // Softer negative half-cycle characteristic of triode grid conduction
        shaped = std::tanh (driven * 0.85f) / 0.92f;
    }

    const float biasOffset = std::tanh (bias * 1.15f);
    const float cleanNormalized = (shaped - biasOffset) / std::max (0.65f, std::tanh (preGain * 0.55f));
    return x + amount * (cleanNormalized - x);
}
}

MakosizerXDSP::MakosizerXDSP()
{
    prepareToPlay (currentSampleRate);
}

void MakosizerXDSP::prepareToPlay (double sampleRate)
{
    currentSampleRate = std::isfinite (sampleRate) ? std::max (1000.0, sampleRate) : 44100.0;
    latencySamples = std::max (1, static_cast<int> (std::round (currentSampleRate * 0.005)));

    const auto maximumModulationSamples = static_cast<std::size_t> (
        std::ceil (currentSampleRate * 0.0080));
    const auto delayLength = static_cast<std::size_t> (latencySamples)
                             + maximumModulationSamples + 8;

    for (auto& line : delayLines)
        line.assign (delayLength, 0.0f);

    reset();
}

void MakosizerXDSP::reset() noexcept
{
    for (auto& line : delayLines)
        std::fill (line.begin(), line.end(), 0.0f);

    writePosition = 0;
    limiterEnvelope = 1.0f;
    limiterGainReductionDb = 0.0f;
    limiterInputPeak = 0.0f;
    limiterOutputPeak = 0.0f;

    for (std::size_t channel = 0; channel < channelStates.size(); ++channel)
    {
        channelStates[channel] = {};
        channelStates[channel].randomState = randomSeeds[channel];
        channelStates[channel].wowPhase = channel == 0 ? 0.0 : 0.42;
        channelStates[channel].wowDriftPhase = channel == 0 ? 0.11 : 1.73;
        channelStates[channel].flutterPhase = channel == 0 ? 0.0 : 0.85;
        channelStates[channel].flutterSubPhase = channel == 0 ? 0.25 : 2.15;
        channelStates[channel].dropoutEnvelope = 1.0f;
    }
}

float MakosizerXDSP::clamp01 (float value) noexcept
{
    return std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f;
}

float MakosizerXDSP::readDelay (const std::vector<float>& line,
                                std::size_t position,
                                float delaySamples) noexcept
{
    const auto length = static_cast<float> (line.size());
    auto readPosition = static_cast<float> (position) - delaySamples;
    while (readPosition < 0.0f)
        readPosition += length;

    const auto firstIndex = static_cast<std::size_t> (readPosition);
    const auto secondIndex = (firstIndex + 1) % line.size();
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

float MakosizerXDSP::processNonlinearSubSample (ChannelState& state, float input,
                                                float drive, float destroy, float vinyl,
                                                float crush, float effectiveSampleRate) noexcept
{
    float wet = input;

    // 1. Input Drive (console/preamp saturation)
    if (drive > 0.0001f)
    {
        const float driveGain = 1.0f + drive * 18.0f;
        const float driveMakeup = 1.0f / std::pow (driveGain, 0.42f);
        const float sat = std::tanh (wet * driveGain) * driveMakeup;
        wet += drive * (sat - wet);
    }

    // 2. DESTROY: Warm Tube Saturation (asymmetric triode harmonics + DC blocker)
    if (destroy > 0.0001f)
    {
        const float tubeOut = tubeSaturate (wet, destroy);
        // DC block at ~10 Hz
        const float dcCoeff = 1.0f - static_cast<float> (twoPi * 10.0 / effectiveSampleRate);
        state.dcBlockOut = dcCoeff * (state.dcBlockOut + tubeOut - state.dcBlockIn);
        state.dcBlockIn = tubeOut;
        wet = state.dcBlockOut;
    }

    // 3. Vinyl Sim tonal softening & subtle magnetic/vinyl warmth
    if (vinyl > 0.0001f)
    {
        const float vinylCutoff = std::clamp (17500.0f - vinyl * 9500.0f, 3500.0f, effectiveSampleRate * 0.45f);
        const float vinylCoeff = 1.0f - std::exp (static_cast<float> (-twoPi) * vinylCutoff / effectiveSampleRate);
        state.vinylLowPass += vinylCoeff * (wet - state.vinylLowPass);

        // Gentle 180 Hz body warmth characteristic of SP-303 Vinyl Sim
        const float bumpCoeff = 1.0f - std::exp (static_cast<float> (-twoPi) * 180.0f / effectiveSampleRate);
        state.vinylMidBump += bumpCoeff * (state.vinylLowPass - state.vinylMidBump);

        const float warmed = state.vinylLowPass + state.vinylMidBump * (vinyl * 0.14f);
        const float vinylSat = std::tanh (warmed * (1.0f + vinyl * 1.1f));
        wet += vinyl * (vinylSat - wet);
    }

    // 4. Bitcrush & Sample Rate Reduction (lo-fi sampler DAC degradation)
    if (crush > 0.0001f)
    {
        // Sample rate reduction from 44.1 kHz down to ~3.2 kHz
        const float targetSr = std::max (2800.0f, 44100.0f * std::pow (0.075f, crush * crush));
        const float step = std::min (1.0f, targetSr / effectiveSampleRate);
        state.sampleHoldPhase += step;
        if (state.sampleHoldPhase >= 1.0f)
        {
            state.sampleHoldPhase -= 1.0f;
            state.heldSample = wet;
        }

        // Bit depth reduction from 16-bit down to 4.5-bit
        const float bits = 16.0f - crush * 11.5f;
        const float levels = std::exp2 (bits - 1.0f);
        const float bounded = std::clamp (state.heldSample, -1.2f, 1.2f);
        const float quantized = std::round (bounded * levels) / levels;
        wet += crush * (quantized - wet);
    }

    return wet;
}

void MakosizerXDSP::process (float* const* channels, int numChannels, int numSamples,
                             const DSPParameters& parameters) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0
        || delayLines[0].empty())
        return;

    const auto activeChannels = std::min (numChannels, 2);
    const auto destroy = clamp01 (parameters.destroy);
    const auto drive = clamp01 (parameters.drive);
    const auto vinyl = clamp01 (parameters.vinyl);
    const auto wow = clamp01 (parameters.wow);
    const auto flutter = clamp01 (parameters.flutter);
    const auto noise = clamp01 (parameters.noise);
    const auto dust = clamp01 (parameters.dust);
    const auto crush = clamp01 (parameters.crush);
    const auto comp = clamp01 (parameters.comp);
    const auto limiter = clamp01 (parameters.limiter);
    const auto width = std::clamp (std::isfinite (parameters.width) ? parameters.width : 1.0f,
                                   0.0f, 2.0f);
    const auto mix = clamp01 (parameters.mix);
    const auto osFactor = std::clamp (parameters.oversamplingFactor, 1, 8);
    const auto sampleRate = static_cast<float> (currentSampleRate);
    const auto osSampleRate = sampleRate * static_cast<float> (osFactor);

    // 2-pole Butterworth HPF and LPF coefficients
    const auto highPassFrequency = std::clamp (
        std::isfinite (parameters.hpf) ? parameters.hpf : 20.0f,
        20.0f, std::min (maximumFilterFrequency, sampleRate * 0.45f));
    const auto lowPassFrequency = std::clamp (
        std::isfinite (parameters.lpf) ? parameters.lpf : maximumFilterFrequency,
        20.0f, std::min (maximumFilterFrequency, sampleRate * 0.45f));

    const float hpfOmega = static_cast<float> (twoPi) * highPassFrequency / sampleRate;
    const float hpfAlpha = std::sin (hpfOmega) / (2.0f * 0.70710678f);
    const float hpfCos = std::cos (hpfOmega);
    const float hpfNorm = 1.0f / (1.0f + hpfAlpha);
    const float hpfB0 = ((1.0f + hpfCos) * 0.5f) * hpfNorm;
    const float hpfB1 = -(1.0f + hpfCos) * hpfNorm;
    const float hpfB2 = hpfB0;
    const float hpfA1 = (-2.0f * hpfCos) * hpfNorm;
    const float hpfA2 = (1.0f - hpfAlpha) * hpfNorm;

    const float lpfOmega = static_cast<float> (twoPi) * lowPassFrequency / sampleRate;
    const float lpfAlpha = std::sin (lpfOmega) / (2.0f * 0.70710678f);
    const float lpfCos = std::cos (lpfOmega);
    const float lpfNorm = 1.0f / (1.0f + lpfAlpha);
    const float lpfB0 = ((1.0f - lpfCos) * 0.5f) * lpfNorm;
    const float lpfB1 = (1.0f - lpfCos) * lpfNorm;
    const float lpfB2 = lpfB0;
    const float lpfA1 = (-2.0f * lpfCos) * lpfNorm;
    const float lpfA2 = (1.0f - lpfAlpha) * lpfNorm;

    // SP-303 Vinyl Sim Compressor constants:
    // Fast attack (~9 ms) to catch kick transients, musical pumping release (~115 ms),
    // low threshold with soft knee + upward makeup boost so quiet tails & vinyl noise pump between kicks.
    const float sp303Attack = std::exp (-1.0f / (0.009f * sampleRate));
    const float sp303Release = std::exp (-1.0f / (0.115f * sampleRate));
    const float sp303Threshold = 0.24f - comp * 0.16f; // lowers down to ~ -22 dB
    const float sp303Ratio = 1.0f + comp * 5.5f;
    const float sp303Makeup = 1.0f + comp * 1.45f; // lifts overall level + pumping floor

    // Wow & Flutter modulation depths & rates
    const float baseDelay = static_cast<float> (latencySamples);
    // Wow: warped vinyl slow pitch drift (0.45 Hz - 1.55 Hz + organic sub-drift)
    const float wowRate = 0.45f + wow * 1.10f;
    const float wowDepth = static_cast<float> (currentSampleRate * 0.0036) * wow;
    // Flutter: low setting = slow flutter (~3.5 Hz), full setting = fast flutter (~28 Hz)
    const float flutterRate = 3.5f + flutter * 24.5f;
    const float flutterDepth = static_cast<float> (currentSampleRate * 0.0011) * flutter;

    // Brickwall Limiter pushing toward -0.1 dBFS (0.9885 linear) without exceeding 0 dB
    const float ceilingLinear = 0.9885f;
    const float limiterDrive = 1.0f + limiter * 2.2f; // up to +10 dB push into ceiling
    const float limiterAttack = std::exp (-1.0f / (0.0004f * sampleRate));
    const float limiterRelease = std::exp (-1.0f / (0.065f * sampleRate));

    // Anti-aliasing 2-pole Butterworth filter for oversampling decimation
    const float osCutoffRatio = 0.42f / static_cast<float> (osFactor);
    const float osOmega = static_cast<float> (twoPi) * osCutoffRatio;
    const float osAlpha = std::sin (osOmega) / (2.0f * 0.70710678f);
    const float osCos = std::cos (osOmega);
    const float osNorm = 1.0f / (1.0f + osAlpha);
    const float osB0 = ((1.0f - osCos) * 0.5f) * osNorm;
    const float osB1 = (1.0f - osCos) * osNorm;
    const float osB2 = osB0;
    const float osA1 = (-2.0f * osCos) * osNorm;
    const float osA2 = (1.0f - osAlpha) * osNorm;

    float blockInputPeak = 0.0f;
    float blockOutputPeak = 0.0f;
    float minLimiterGain = 1.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        std::array<float, 2> dryDelayed {};
        std::array<float, 2> wetDelayed {};

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto channelIndex = static_cast<std::size_t> (channel);
            auto& state = channelStates[channelIndex];
            const auto dryInput = channels[channel][sample];
            float wet = dryInput;

            // 1. Pre-filtering: HPF & LPF
            if (highPassFrequency > 20.5f)
            {
                const float hpfOut = hpfB0 * wet + state.hpfZ1;
                state.hpfZ1 = hpfB1 * wet - hpfA1 * hpfOut + state.hpfZ2;
                state.hpfZ2 = hpfB2 * wet - hpfA2 * hpfOut;
                wet = hpfOut;
            }

            if (lowPassFrequency < 19950.0f)
            {
                const float lpfOut = lpfB0 * wet + state.lpfZ1;
                state.lpfZ1 = lpfB1 * wet - lpfA1 * lpfOut + state.lpfZ2;
                state.lpfZ2 = lpfB2 * wet - lpfA2 * lpfOut;
                wet = lpfOut;
            }

            // 2. Oversampled Nonlinear Stage (Drive, Tube Destroy, Vinyl Tone, Bitcrush)
            if (osFactor == 1)
            {
                wet = processNonlinearSubSample (state, wet, drive, destroy, vinyl, crush, sampleRate);
                state.osUpPrev = wet;
            }
            else
            {
                const float startSample = state.osUpPrev;
                const float endSample = wet;
                state.osUpPrev = endSample;
                float decimated = 0.0f;

                for (int step = 1; step <= osFactor; ++step)
                {
                    const float t = static_cast<float> (step) / static_cast<float> (osFactor);
                    const float interp = startSample + (endSample - startSample) * t;
                    const float processedSub = processNonlinearSubSample (
                        state, interp, drive, destroy, vinyl, crush, osSampleRate);

                    // Anti-aliasing low-pass before decimation
                    const float filteredSub = osB0 * processedSub + state.osDownZ1;
                    state.osDownZ1 = osB1 * processedSub - osA1 * filteredSub + state.osDownZ2;
                    state.osDownZ2 = osB2 * processedSub - osA2 * filteredSub;
                    decimated = filteredSub;
                }
                wet = decimated;
            }

            // 3. Noise Generator (warm analog/tape hiss)
            auto& randomState = state.randomState;
            if (noise > 0.0001f)
            {
                const float white = randomBipolar (randomState);
                // Band-limited warm hiss (~120 Hz to ~9.5 kHz)
                state.noiseLowPass += 0.42f * (white - state.noiseLowPass);
                state.noiseHighPass = 0.985f * (state.noiseHighPass + state.noiseLowPass - state.prevNoiseWhite);
                state.prevNoiseWhite = state.noiseLowPass;
                wet += state.noiseHighPass * (noise * noise) * 0.045f;
            }

            // 4. Vinyl Sim Surface Crackle & Dust (dirty vinyl + dropouts at max dust)
            const float combinedVinylDust = std::max (vinyl * 0.45f, dust);
            if (combinedVinylDust > 0.0001f)
            {
                // Fine continuous vinyl crackle (from Vinyl Sim + Dust)
                const float crackleProb = (vinyl * 0.0012f) + (dust * dust * 0.0035f);
                if (randomUnit (randomState) < crackleProb)
                {
                    const float impulse = randomBipolar (randomState) * (0.04f + randomUnit (randomState) * 0.24f);
                    state.crackleFilter = impulse;
                }
                state.crackleFilter *= 0.68f;
                wet += state.crackleFilter * (0.35f + combinedVinylDust * 0.85f);

                // Heavy dirty vinyl pops from Dust
                if (dust > 0.05f && randomUnit (randomState) < dust * dust * 0.00045f)
                {
                    const float pop = randomBipolar (randomState) * (0.18f + randomUnit (randomState) * 0.48f);
                    wet += pop * dust;
                }

                // At high/maximum Dust (> 0.72), dirty needle causes intermittent audio dropouts / stuttering
                if (dust > 0.72f)
                {
                    const float severity = (dust - 0.72f) / 0.28f; // 0..1 near max
                    if (state.dropoutSamplesRemaining <= 0)
                    {
                        // Up to ~18 dropouts per second at 100% Dust
                        const float dropTriggerProb = (severity * severity * 18.0f) / sampleRate;
                        if (randomUnit (randomState) < dropTriggerProb)
                        {
                            const float durationSec = 0.008f + randomUnit (randomState) * (0.028f + severity * 0.045f);
                            state.dropoutSamplesRemaining = static_cast<int> (durationSec * sampleRate);
                        }
                    }

                    if (state.dropoutSamplesRemaining > 0)
                    {
                        --state.dropoutSamplesRemaining;
                        const float targetGain = 1.0f - (0.75f + 0.25f * severity);
                        state.dropoutEnvelope += 0.15f * (targetGain - state.dropoutEnvelope);
                    }
                    else
                    {
                        state.dropoutEnvelope += 0.08f * (1.0f - state.dropoutEnvelope);
                    }

                    wet *= state.dropoutEnvelope;
                }
            }

            // 5. SP-303 Vinyl Sim Style Pumping Compressor (post-noise/vinyl so noise floor pumps with kick!)
            if (comp > 0.0001f)
            {
                // Low-end weighted sidechain so kick drums trigger classic SP-303 pumping
                state.compPreEmphasisState += 0.045f * (wet - state.compPreEmphasisState);
                const float sidechainSignal = std::abs (wet * 0.65f + state.compPreEmphasisState * 0.85f);

                const float envCoeff = sidechainSignal > state.compDetectorEnv ? sp303Attack : sp303Release;
                state.compDetectorEnv = envCoeff * state.compDetectorEnv + (1.0f - envCoeff) * sidechainSignal;

                float targetGain = 1.0f;
                if (state.compDetectorEnv > sp303Threshold)
                {
                    const float overRatio = state.compDetectorEnv / std::max (0.001f, sp303Threshold);
                    targetGain = std::pow (overRatio, -(1.0f - 1.0f / sp303Ratio));
                }

                // Soft knee & upward makeup lift
                state.compGainSmooth += 0.04f * (targetGain - state.compGainSmooth);
                const float compressed = wet * state.compGainSmooth * sp303Makeup;
                // Gentle SP-303 output stage soft-clip so pumped peaks stay glued
                const float sp303Glue = std::tanh (compressed * 1.15f) / 1.05f;
                wet += comp * (sp303Glue - wet);
            }

            // 6. Wow & Flutter Pitch/Time Modulation
            state.wowPhase += twoPi * wowRate / currentSampleRate;
            state.wowDriftPhase += twoPi * (wowRate * 0.37) / currentSampleRate;
            state.flutterPhase += twoPi * flutterRate / currentSampleRate;
            state.flutterSubPhase += twoPi * (flutterRate * 1.43) / currentSampleRate;

            if (state.wowPhase >= twoPi) state.wowPhase -= twoPi;
            if (state.wowDriftPhase >= twoPi) state.wowDriftPhase -= twoPi;
            if (state.flutterPhase >= twoPi) state.flutterPhase -= twoPi;
            if (state.flutterSubPhase >= twoPi) state.flutterSubPhase -= twoPi;

            const float wowMod = static_cast<float> (
                0.75 * std::sin (state.wowPhase) + 0.25 * std::sin (state.wowDriftPhase)) * wowDepth;
            const float flutterMod = static_cast<float> (
                0.80 * std::sin (state.flutterPhase) + 0.20 * std::sin (state.flutterSubPhase)) * flutterDepth;

            const float modulatedDelay = std::max (1.0f, baseDelay + wowMod + flutterMod);

            delayLines[channelIndex][writePosition] = dryInput;
            delayLines[channelIndex + 2][writePosition] = wet;
            dryDelayed[channelIndex] = readDelay (delayLines[channelIndex], writePosition, baseDelay);
            wetDelayed[channelIndex] = readDelay (delayLines[channelIndex + 2], writePosition, modulatedDelay);
        }

        // 7. Stereo Width (Mid/Side)
        if (activeChannels == 2)
        {
            const float mid = (wetDelayed[0] + wetDelayed[1]) * 0.5f;
            const float side = (wetDelayed[0] - wetDelayed[1]) * 0.5f * width;
            wetDelayed[0] = mid + side;
            wetDelayed[1] = mid - side;
        }

        // 8. Dry/Wet Mix & Brickwall Limiter (pushes toward 0 dB, never exceeds 0 dB)
        float stereoLinkPeak = 0.0f;
        std::array<float, 2> mixedChannels {};
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto idx = static_cast<std::size_t> (channel);
            float out = dryDelayed[idx] + mix * (wetDelayed[idx] - dryDelayed[idx]);
            if (limiter > 0.0001f)
                out *= (1.0f + limiter * (limiterDrive - 1.0f));
            mixedChannels[idx] = out;
            stereoLinkPeak = std::max (stereoLinkPeak, std::abs (out));
        }

        blockInputPeak = std::max (blockInputPeak, std::min (1.0f, stereoLinkPeak));

        float limGain = 1.0f;
        if (limiter > 0.0001f)
        {
            const float desiredGain = stereoLinkPeak > ceilingLinear
                                          ? (ceilingLinear / stereoLinkPeak)
                                          : 1.0f;
            const float coeff = desiredGain < limiterEnvelope ? limiterAttack : limiterRelease;
            limiterEnvelope = coeff * limiterEnvelope + (1.0f - coeff) * desiredGain;
            limGain = std::min (1.0f, limiterEnvelope);
            minLimiterGain = std::min (minLimiterGain, limGain);
        }
        else
        {
            limiterEnvelope = 1.0f;
        }

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            float finalSample = mixedChannels[static_cast<std::size_t> (channel)] * limGain;

            // Brickwall safety clamp when limiter is active so signal never exceeds 0.0 dBFS
            if (limiter > 0.0001f)
                finalSample = std::clamp (finalSample, -ceilingLinear, ceilingLinear);

            blockOutputPeak = std::max (blockOutputPeak, std::min (1.0f, std::abs (finalSample)));
            channels[channel][sample] = finalSample;
        }

        writePosition = (writePosition + 1) % delayLines[0].size();
    }

    limiterInputPeak = blockInputPeak;
    limiterOutputPeak = blockOutputPeak;
    limiterGainReductionDb = minLimiterGain < 0.9999f
                                 ? std::max (-24.0f, 20.0f * std::log10 (minLimiterGain))
                                 : 0.0f;
}
}
