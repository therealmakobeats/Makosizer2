// Standalone tests for the JUCE-free DSP core. No framework needed:
//   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
#include "../DSP/MakosizerXDSP.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
using MakosizerX::DSPParameters;
using MakosizerX::MakosizerXDSP;

int failures = 0;

void check (bool condition, const char* description)
{
    std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
    if (! condition)
        ++failures;
}

void run (MakosizerXDSP& dsp, std::vector<float>& left, std::vector<float>& right,
          const DSPParameters& parameters, int block)
{
    for (std::size_t start = 0; start < left.size(); start += static_cast<std::size_t> (block))
    {
        const auto count = static_cast<int> (std::min (static_cast<std::size_t> (block),
                                                       left.size() - start));
        float* channels[2] = { left.data() + start, right.data() + start };
        dsp.process (channels, 2, count, parameters);
    }
}

double goertzelPower (const std::vector<float>& signal, std::size_t begin, std::size_t length,
                      std::size_t bin)
{
    const auto omega = 6.283185307179586 * static_cast<double> (bin) / static_cast<double> (length);
    const auto coefficient = 2.0 * std::cos (omega);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const auto hann = 0.5 - 0.5 * std::cos (6.283185307179586 * static_cast<double> (i)
                                                / static_cast<double> (length));
        const auto s = static_cast<double> (signal[begin + i]) * hann + coefficient * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - coefficient * s1 * s2;
}

void testLatency()
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const auto factor : { 1, 2, 4, 8 })
        {
            MakosizerXDSP dsp;
            dsp.prepareToPlay (sampleRate);
            dsp.setOversamplingFactor (factor);

            std::vector<float> left (8192, 0.0f), right (8192, 0.0f);
            left[100] = 1.0f;
            DSPParameters parameters;
            parameters.mix = 0.0f;
            run (dsp, left, right, parameters, 300);

            const auto peak = static_cast<int> (std::distance (
                left.begin(), std::max_element (left.begin(), left.end(),
                                                [] (float a, float b) { return std::abs (a) < std::abs (b); })));
            char text[96];
            std::snprintf (text, sizeof text, "reported latency matches impulse delay (%.0f Hz, x%d)",
                           sampleRate, factor);
            check (peak - 100 == dsp.getLatencySamples(), text);
        }
    }
}

void testAliasing()
{
    constexpr std::size_t length = 16384;
    constexpr std::size_t fundamentalBin = 3103;  // 9.09 kHz at 48 kHz
    constexpr std::size_t aliasBin = length - 3 * fundamentalBin;
    const auto sampleRate = 48000.0;
    double aliasLevel[4] {};
    int index = 0;

    for (const auto factor : { 1, 2, 4, 8 })
    {
        MakosizerXDSP dsp;
        dsp.prepareToPlay (sampleRate);
        dsp.setOversamplingFactor (factor);

        std::vector<float> left (length * 2), right;
        for (std::size_t i = 0; i < left.size(); ++i)
            left[i] = 0.8f * static_cast<float> (std::sin (6.283185307179586 * static_cast<double> (fundamentalBin)
                                                          * static_cast<double> (i) / static_cast<double> (length)));
        right = left;

        DSPParameters parameters;
        parameters.drive = 1.0f;
        run (dsp, left, right, parameters, 512);

        const auto fundamental = goertzelPower (left, length, length, fundamentalBin);
        aliasLevel[index++] = 10.0 * std::log10 (goertzelPower (left, length, length, aliasBin) / fundamental);
    }

    std::printf ("      3rd-harmonic alias vs fundamental: x1 %.1f dB, x2 %.1f dB, x4 %.1f dB, x8 %.1f dB\n",
                 aliasLevel[0], aliasLevel[1], aliasLevel[2], aliasLevel[3]);
    // Above x1 the alias drops to the measurement floor (Hann window leakage),
    // so only the step from x1 to any oversampled mode is checked here.
    check (aliasLevel[1] < aliasLevel[0] - 20.0 && aliasLevel[2] < aliasLevel[0] - 20.0
               && aliasLevel[3] < aliasLevel[0] - 20.0,
           "oversampling lowers the 3rd-harmonic alias by at least 20 dB");
}

void testNoiseLevel()
{
    double rms[4] {};
    int index = 0;
    for (const auto factor : { 1, 2, 4, 8 })
    {
        MakosizerXDSP dsp;
        dsp.prepareToPlay (48000.0);
        dsp.setOversamplingFactor (factor);
        std::vector<float> left (192000, 0.0f), right (192000, 0.0f);
        DSPParameters parameters;
        parameters.noise = 0.5f;
        run (dsp, left, right, parameters, 512);

        double sum = 0.0;
        for (std::size_t i = 48000; i < left.size(); ++i)
            sum += static_cast<double> (left[i]) * static_cast<double> (left[i]);
        rms[index++] = std::sqrt (sum / static_cast<double> (left.size() - 48000));
    }

    bool consistent = true;
    for (int i = 1; i < 4; ++i)
        consistent = consistent && std::abs (rms[i] / rms[0] - 1.0) < 0.1;
    check (consistent, "noise level is the same at every oversampling factor (+-10 %)");
}

void testRobustness()
{
    std::mt19937 random (7);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    std::uniform_real_distribution<float> bipolar (-1.5f, 1.5f);
    MakosizerXDSP dsp;
    dsp.prepareToPlay (48000.0);
    bool finite = true;

    for (int iteration = 0; iteration < 300; ++iteration)
    {
        const auto count = 1 + static_cast<int> (random() % 1500);
        std::vector<float> left (static_cast<std::size_t> (count)), right (static_cast<std::size_t> (count));
        for (auto& sample : left) sample = bipolar (random);
        for (auto& sample : right) sample = bipolar (random);

        DSPParameters p;
        p.drive = unit (random); p.vinyl = unit (random); p.destroy = unit (random);
        p.wow = unit (random); p.flutter = unit (random); p.noise = unit (random);
        p.dust = unit (random); p.crush = unit (random); p.comp = unit (random);
        p.hpf = 20.0f + unit (random) * 19980.0f; p.lpf = 20.0f + unit (random) * 19980.0f;
        p.limiter = unit (random); p.width = unit (random) * 2.0f; p.mix = unit (random);
        if (iteration % 7 == 0) p.drive = std::nanf ("");
        if (iteration % 11 == 0) p.lpf = INFINITY;
        if (iteration % 3 == 0) dsp.setOversamplingFactor (1 << (random() % 4));

        run (dsp, left, right, p, 1 + static_cast<int> (random() % 700));
        for (std::size_t i = 0; i < left.size(); ++i)
            finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
    }

    check (finite, "output stays finite with random/NaN parameters and factor switching");
}
}

int main()
{
    testLatency();
    testAliasing();
    testNoiseLevel();
    testRobustness();
    std::printf (failures == 0 ? "\nAll tests passed.\n" : "\n%d test(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
