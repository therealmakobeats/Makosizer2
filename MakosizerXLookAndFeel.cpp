#include "MakosizerXLookAndFeel.h"
#include <cmath>

namespace
{
const juce::Colour pointerRed (0xffff2e2e);

void drawRedGlow (juce::Graphics& graphics, juce::Point<float> centre,
                  float radius, float intensity)
{
    for (int layer = 3; layer > 0; --layer)
    {
        const auto glowRadius = radius + 2.0f + static_cast<float> (layer) * 2.0f;
        const auto alpha = 0.03f * intensity * static_cast<float> (4 - layer);
        graphics.setColour (pointerRed.withAlpha (alpha));
        graphics.drawEllipse (centre.x - glowRadius, centre.y - glowRadius,
                              glowRadius * 2.0f, glowRadius * 2.0f,
                              static_cast<float> (layer + 1));
    }
}
}

void MakosizerXLookAndFeel::drawRotarySlider (juce::Graphics& graphics,
                                               int x, int y, int width, int height,
                                               float sliderPosition,
                                               float rotaryStartAngle,
                                               float rotaryEndAngle,
                                               juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                 static_cast<float> (y),
                                                 static_cast<float> (width),
                                                 static_cast<float> (height));
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    if (diameter <= 0.0f)
        return;

    const auto centre = bounds.getCentre();
    const auto isDestroy = slider.getName().equalsIgnoreCase ("Destroy");
    const auto knobRadius = diameter * (isDestroy ? 0.41f : 0.40f);
    const auto intensity = juce::jlimit (0.0f, 1.0f, sliderPosition);
    const auto outerRadius = knobRadius + (isDestroy ? 7.0f : 4.0f);

    if (isDestroy)
        drawRedGlow (graphics, centre, outerRadius, 0.70f + intensity * 0.30f);
    else if (intensity > 0.015f)
        drawRedGlow (graphics, centre, outerRadius, intensity * 0.6f);

    auto outerBounds = juce::Rectangle<float> (centre.x - outerRadius, centre.y - outerRadius,
                                                outerRadius * 2.0f, outerRadius * 2.0f);
    juce::ColourGradient steelRim (juce::Colour (0xffbdbbb0), centre.x, outerBounds.getY(),
                                   juce::Colour (0xff1d201e), centre.x, outerBounds.getBottom(),
                                   false);
    graphics.setGradientFill (steelRim);
    graphics.fillEllipse (outerBounds);
    graphics.setColour (juce::Colour (0xff0e100e));
    graphics.drawEllipse (outerBounds, 1.5f);

    if (isDestroy)
    {
        graphics.setColour (pointerRed.withAlpha (0.75f + intensity * 0.25f));
        graphics.drawEllipse (outerBounds, 3.0f);
    }

    auto knobBounds = juce::Rectangle<float> (centre.x - knobRadius, centre.y - knobRadius,
                                               knobRadius * 2.0f, knobRadius * 2.0f);
    juce::ColourGradient darkMetal (juce::Colour (0xff484b46), centre.x, knobBounds.getY(),
                                      juce::Colour (0xff121412), centre.x, knobBounds.getBottom(), false);
    darkMetal.addColour (0.45, juce::Colour (0xff2b2e2a));
    graphics.setGradientFill (darkMetal);
    graphics.fillEllipse (knobBounds);

    graphics.setColour (juce::Colour (0x44ffffff));
    graphics.drawEllipse (knobBounds.reduced (1.0f), 1.0f);

    const auto capRadius = knobRadius * 0.68f;
    auto capBounds = juce::Rectangle<float> (centre.x - capRadius, centre.y - capRadius,
                                              capRadius * 2.0f, capRadius * 2.0f);
    juce::ColourGradient innerCap (juce::Colour (0xff353834), centre.x, capBounds.getY(),
                                   juce::Colour (0xff0d0f0d), centre.x, capBounds.getBottom(), false);
    graphics.setGradientFill (innerCap);
    graphics.fillEllipse (capBounds);

    const auto angle = rotaryStartAngle + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto innerStart = knobRadius * 0.16f;
    const auto pointerLength = knobRadius * 0.88f;
    const auto startX = centre.x + std::sin (angle) * innerStart;
    const auto startY = centre.y - std::cos (angle) * innerStart;
    const auto endX = centre.x + std::sin (angle) * pointerLength;
    const auto endY = centre.y - std::cos (angle) * pointerLength;

    graphics.setColour (pointerRed.withAlpha (0.35f));
    graphics.drawLine (startX, startY, endX, endY, isDestroy ? 8.0f : 5.5f);
    graphics.setColour (pointerRed);
    graphics.drawLine (startX, startY, endX, endY, isDestroy ? 3.8f : 2.6f);
}
