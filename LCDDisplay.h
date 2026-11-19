#pragma once

#include <JuceHeader.h>

class LCDDisplay final : public juce::Component
{
public:
    struct State
    {
        juce::String presetName { "INIT" };
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
        int oversampling = 1;
        float outputLevel = 0.0f;
    };

    LCDDisplay();

    void setState (State newState);
    const State& getState() const noexcept { return state; }

    void paint (juce::Graphics& graphics) override;

private:
    static float clampParameter (float value) noexcept;
    State state;
};
