#include "LCDDisplay.h"

#include <cmath>
#include <utility>

LCDDisplay::LCDDisplay()
{
    setOpaque (true);
}

float LCDDisplay::clampParameter (float value) noexcept
{
    return std::isfinite (value) ? juce::jlimit (0.0f, 1.0f, value) : 0.0f;
}

void LCDDisplay::setState (State newState)
{
    newState.drive = clampParameter (newState.drive);
    newState.vinyl = clampParameter (newState.vinyl);
    newState.destroy = clampParameter (newState.destroy);
    newState.wow = clampParameter (newState.wow);
    newState.flutter = clampParameter (newState.flutter);
    newState.noise = clampParameter (newState.noise);
    newState.dust = clampParameter (newState.dust);
    newState.crush = clampParameter (newState.crush);
    newState.comp = clampParameter (newState.comp);
    newState.limiter = clampParameter (newState.limiter);
    newState.mix = clampParameter (newState.mix);
    newState.outputLevel = clampParameter (newState.outputLevel);

    state = std::move (newState);
    repaint();
}

void LCDDisplay::paint (juce::Graphics& g)
{
    const juce::Colour lcdBg (0xff49c964);
    const juce::Colour lcdBgDark (0xff35a84e);
    const juce::Colour lcdInk (0xff0b1f0f);
    const juce::Colour lcdInkSoft (0xff15361c);

    juce::ColourGradient bgGrad (lcdBg, 0.0f, 0.0f, lcdBgDark, 0.0f,
                                 static_cast<float> (getHeight()), false);
    g.setGradientFill (bgGrad);
    g.fillAll();

    g.setColour (lcdInk.withAlpha (0.07f));
    for (int y = 0; y < getHeight(); y += 3)
        g.fillRect (0, y, getWidth(), 1);
    for (int x = 0; x < getWidth(); x += 3)
        g.fillRect (x, 0, 1, getHeight());

    g.setColour (lcdInk.withAlpha (0.45f));
    g.drawRect (getLocalBounds(), 2);

    auto area = getLocalBounds().reduced (10, 7);

    auto rightPane = area.removeFromRight (juce::roundToInt (area.getWidth() * 0.34f));
    g.setColour (lcdInk.withAlpha (0.35f));
    g.drawLine (static_cast<float> (rightPane.getX() - 6), static_cast<float> (area.getY()),
                static_cast<float> (rightPane.getX() - 6), static_cast<float> (area.getBottom()), 1.5f);

    const float effectiveSrKhz = (44.1f * std::pow (0.075f, state.crush * state.crush));
    const int effectiveBits = juce::roundToInt (16.0f - state.crush * 11.5f);

    struct Row { juce::String label; juce::String val; };
    const Row rows[] = {
        { "PRESET:", state.presetName },
        { "SR:",     juce::String (effectiveSrKhz, 1) + " kHz" },
        { "BITS:",   juce::String (effectiveBits) + " BIT" },
        { "WOW:",    juce::String (juce::roundToInt (state.wow * 100.0f)) + "%" },
        { "FLUTTER:",juce::String (juce::roundToInt (state.flutter * 100.0f)) + "%" },
        { "COMP:",   juce::String (juce::roundToInt (state.comp * 100.0f)) + "%" },
        { "OS:",     juce::String (state.oversampling) + "x" }
    };

    const int rowH = rightPane.getHeight() / 7;
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::bold));
    for (int i = 0; i < 7; ++i)
    {
        auto r = rightPane.removeFromTop (rowH);
        g.setColour (lcdInkSoft);
        g.drawText (rows[i].label, r.removeFromLeft (68), juce::Justification::centredLeft, false);
        g.setColour (lcdInk);
        g.drawText (rows[i].val, r, juce::Justification::centredLeft, true);
    }

    area.removeFromRight (10);
    auto titleArea = area.removeFromTop (juce::roundToInt (area.getHeight() * 0.44f));
    g.setColour (lcdInk);
    g.setFont (juce::Font (34.0f, juce::Font::bold));
    g.drawText ("Makosizer", titleArea, juce::Justification::centred, false);

    auto subArea = area.removeFromTop (20);
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 13.5f, juce::Font::bold));
    g.drawText ("LO-FI IS A FEATURE", subArea, juce::Justification::centred, false);

    area.removeFromTop (6);
    auto statusBar = area.removeFromBottom (22);
    g.setColour (lcdInk.withAlpha (0.22f));
    g.fillRect (statusBar);
    g.setColour (lcdInk);
    g.drawRect (statusBar, 1);

    juce::String statusText = "READY // ";
    if (state.destroy > 0.05f)      statusText = "TUBE DESTROY " + juce::String (juce::roundToInt (state.destroy * 100.0f)) + "%";
    else if (state.vinyl > 0.05f)   statusText = "VINYL SIM ACTIVE....";
    else if (state.comp > 0.05f)    statusText = "SP303 COMP ACTIVE...";
    else if (state.crush > 0.05f)   statusText = "BITCRUSH ACTIVE.....";

    auto textPart = statusBar.reduced (6, 2).removeFromLeft (185);
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold));
    g.drawText (statusText, textPart, juce::Justification::centredLeft, true);

    auto meterPart = statusBar.reduced (6, 4);
    meterPart.removeFromLeft (188);
    const int segments = 24;
    const float activity = juce::jmax (state.outputLevel, state.destroy * 0.85f);
    const int lit = juce::roundToInt (activity * segments);
    const int segW = juce::jmax (2, (meterPart.getWidth() - segments) / segments);
    for (int s = 0; s < segments; ++s)
    {
        g.setColour (s < lit ? lcdInk : lcdInk.withAlpha (0.22f));
        g.fillRect (meterPart.getX() + s * (segW + 1), meterPart.getY(), segW, meterPart.getHeight());
    }
}
