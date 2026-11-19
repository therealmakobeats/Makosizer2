#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace
{
struct ControlPlacement
{
    const char* parameterId;
    const char* label;
    int x;
    int y;
    int width;
    int height;
    juce::Rectangle<int> readoutBox;
};

constexpr std::array<ControlPlacement, 14> controlPlacements {{
    { "drive",   "Drive",   298, 367, 115, 110, { 306, 483, 98, 28 } },
    { "vinyl",   "Vinyl",   480, 367, 115, 110, { 488, 483, 98, 28 } },
    { "destroy", "Destroy", 650, 364, 240, 229, { 720, 606, 100, 34 } },
    { "wow",     "Wow",     946, 367, 115, 110, { 954, 483, 98, 28 } },
    { "flutter", "Flutter", 1116, 367, 115, 110, { 1124, 483, 98, 28 } },
    { "noise",   "Noise",   296, 575, 115, 110, { 304, 691, 98, 28 } },
    { "dust",    "Dust",    470, 575, 115, 110, { 478, 691, 98, 28 } },
    { "crush",   "Crush",   943, 575, 115, 110, { 951, 691, 98, 28 } },
    { "comp",    "Comp",    1116, 575, 115, 110, { 1124, 691, 98, 28 } },
    { "hpf",     "HPF",     310, 795, 96, 91,   { 312, 892, 92, 26 } },
    { "lpf",     "LPF",     462, 795, 96, 91,   { 464, 892, 92, 26 } },
    { "limiter", "Limiter", 628, 795, 96, 91,   { 630, 892, 92, 26 } },
    { "width",   "Width",   967, 795, 96, 91,   { 969, 892, 92, 26 } },
    { "mix",     "Mix",     1127, 795, 96, 91,  { 1129, 892, 92, 26 } }
}};

struct OverlayPlacement
{
    int x;
    int y;
    int width;
    int height;
};

constexpr std::array<OverlayPlacement, 9> presetPlacements {{
    { 56, 163, 180, 34 },
    { 56, 201, 180, 34 },
    { 56, 239, 180, 34 },
    { 56, 277, 180, 34 },
    { 56, 315, 180, 34 },
    { 56, 353, 180, 34 },
    { 56, 391, 180, 34 },
    { 56, 429, 180, 34 },
    { 56, 467, 180, 34 }
}};

constexpr std::array<OverlayPlacement, 2> presetFileButtonPlacements {{
    { 56, 525, 84, 42 },
    { 151, 525, 85, 42 }
}};

constexpr std::array<OverlayPlacement, 4> oversamplingPlacements {{
    { 1295, 694, 35, 51 },
    { 1337, 694, 35, 51 },
    { 1379, 694, 35, 51 },
    { 1422, 694, 35, 51 }
}};

constexpr std::array<const char*, 4> oversamplingNames {{
    "1x", "2x", "4x", "8x"
}};

class ArtworkButton final : public juce::Button
{
public:
    explicit ArtworkButton (const juce::String& name) : juce::Button (name) {}

    void paintButton (juce::Graphics& graphics, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override
    {
        if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
        {
            graphics.setColour (juce::Colours::white.withAlpha (
                shouldDrawButtonAsDown ? 0.14f : 0.07f));
            graphics.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 3.0f);
        }
    }
};

enum ControlIndex : std::size_t
{
    driveControl, vinylControl, destroyControl, wowControl, flutterControl,
    noiseControl, dustControl, crushControl, compControl,
    hpfControl, lpfControl, limiterControl, widthControl, mixControl
};
}

MakosizerXAudioProcessorEditor::MakosizerXAudioProcessorEditor (
    MakosizerXAudioProcessor& processor)
    : juce::AudioProcessorEditor (processor),
      audioProcessor (processor)
{
    backgroundImage = juce::ImageCache::getFromMemory (
        BinaryData::makosizerx_background_png,
        BinaryData::makosizerx_background_pngSize);

    for (std::size_t i = 0; i < controlPlacements.size(); ++i)
    {
        const auto& placement = controlPlacements[i];
        auto& slider = sliders[i];

        slider.setName (placement.label);
        slider.setTooltip (placement.label);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setLookAndFeel (&rotaryLookAndFeel);
        slider.setOpaque (false);
        slider.setBounds (placement.x, placement.y, placement.width, placement.height);
        addAndMakeVisible (slider);
        slider.addListener (this);

        attachments[i] = std::make_unique<SliderAttachment> (
            processor.getParameterState(), placement.parameterId, slider);
    }

    const auto presets = processor.getPresetManager().getFactoryPresets();
    for (std::size_t i = 0; i < presetButtons.size() && i < presets.size(); ++i)
    {
        auto button = std::make_unique<ArtworkButton> (presets[i].name);
        button->setTitle (presets[i].name);
        button->setBounds (presetPlacements[i].x, presetPlacements[i].y,
                           presetPlacements[i].width, presetPlacements[i].height);
        button->setClickingTogglesState (true);
        button->onClick = [this, name = presets[i].name] { loadPreset (name); };
        addAndMakeVisible (*button);
        presetButtons[i] = std::move (button);
    }

    constexpr std::array<const char*, 2> presetFileButtonNames {{ "LOAD", "SAVE" }};
    for (std::size_t i = 0; i < presetFileButtons.size(); ++i)
    {
        auto button = std::make_unique<ArtworkButton> (presetFileButtonNames[i]);
        button->setTitle (presetFileButtonNames[i]);
        button->setBounds (presetFileButtonPlacements[i].x, presetFileButtonPlacements[i].y,
                           presetFileButtonPlacements[i].width, presetFileButtonPlacements[i].height);
        button->onClick = [this, i]
        {
            if (i == 0) openPresetLoadDialog();
            else        openPresetSaveDialog();
        };
        addAndMakeVisible (*button);
        presetFileButtons[i] = std::move (button);
    }

    for (std::size_t i = 0; i < oversamplingButtons.size(); ++i)
    {
        auto button = std::make_unique<ArtworkButton> (oversamplingNames[i]);
        button->setTitle (oversamplingNames[i]);
        button->setBounds (oversamplingPlacements[i].x, oversamplingPlacements[i].y,
                           oversamplingPlacements[i].width, oversamplingPlacements[i].height);
        button->setClickingTogglesState (true);
        button->onClick = [this, i] { setOversamplingMode (static_cast<int> (i)); };
        addAndMakeVisible (*button);
        oversamplingButtons[i] = std::move (button);
    }

    lcdDisplay.setBounds (564, 128, 678, 166);
    addAndMakeVisible (lcdDisplay);

    ledMeter.setBounds (1287, 117, 178, 510);
    addAndMakeVisible (ledMeter);

    setSize (1536, 1024);
    updateLCDDisplay();
    updateOversamplingSelection();
    startTimerHz (30);
}

MakosizerXAudioProcessorEditor::~MakosizerXAudioProcessorEditor()
{
    stopTimer();
    for (auto& slider : sliders)
    {
        slider.removeListener (this);
        slider.setLookAndFeel (nullptr);
    }
}

void MakosizerXAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    if (backgroundImage.isValid())
        graphics.drawImage (backgroundImage, getLocalBounds().toFloat());
    else
        graphics.fillAll (juce::Colour (0xff202020));

    const auto accent = juce::Colour (0xffff3838);

    for (std::size_t i = 0; i < presetButtons.size(); ++i)
    {
        const auto& button = presetButtons[i];
        if (button == nullptr) continue;

        const auto bounds = button->getBounds().toFloat().reduced (1.0f);
        const bool isSelected = button->getToggleState();

        if (i == 0 && ! isSelected)
        {
            graphics.setColour (juce::Colour (0xd81b1d1c));
            graphics.fillRoundedRectangle (bounds, 3.0f);
            graphics.setColour (juce::Colour (0xffcfd2cc));
            graphics.setFont (juce::Font (15.0f, juce::Font::bold));
            graphics.drawText ("INIT", bounds.reduced (34.0f, 0.0f), juce::Justification::centredLeft, false);
        }

        auto ledBox = juce::Rectangle<float> (bounds.getX() + 7.0f, bounds.getCentreY() - 8.0f, 16.0f, 16.0f);
        if (isSelected)
        {
            graphics.setColour (accent.withAlpha (0.18f));
            graphics.fillRoundedRectangle (bounds, 3.0f);
            graphics.setColour (accent);
            graphics.drawRoundedRectangle (bounds, 3.0f, 1.5f);
            graphics.fillRoundedRectangle (ledBox, 2.0f);
            graphics.setColour (juce::Colours::white);
            graphics.fillEllipse (ledBox.reduced (5.5f));
        }
        else
        {
            graphics.setColour (juce::Colour (0xff2c2f2d));
            graphics.fillRoundedRectangle (ledBox, 2.0f);
            graphics.setColour (juce::Colour (0xff4d524e));
            graphics.drawRoundedRectangle (ledBox, 2.0f, 1.0f);
        }
    }

    for (std::size_t i = 0; i < oversamplingButtons.size(); ++i)
    {
        const auto& button = oversamplingButtons[i];
        if (button == nullptr) continue;

        const auto bounds = button->getBounds().toFloat().reduced (1.0f);
        const bool isSelected = button->getToggleState();

        graphics.setColour (isSelected ? juce::Colour (0xff2b1414) : juce::Colour (0xff1c1e1d));
        graphics.fillRoundedRectangle (bounds, 3.0f);
        graphics.setColour (isSelected ? accent : juce::Colour (0xff494d4a));
        graphics.drawRoundedRectangle (bounds, 3.0f, isSelected ? 1.8f : 1.0f);

        graphics.setColour (isSelected ? juce::Colours::white : juce::Colour (0xffb7bbb4));
        graphics.setFont (juce::Font (15.0f, juce::Font::bold));
        graphics.drawText (oversamplingNames[i], bounds, juce::Justification::centred, false);
    }

    for (std::size_t i = 0; i < controlPlacements.size(); ++i)
    {
        const auto& p = controlPlacements[i];
        const auto box = p.readoutBox.toFloat();
        graphics.setColour (juce::Colour (0xff121413));
        graphics.fillRoundedRectangle (box, 3.0f);
        graphics.setColour (juce::Colour (0xff404441));
        graphics.drawRoundedRectangle (box, 3.0f, 1.0f);

        const double minVal = sliders[i].getMinimum();
        const double maxVal = sliders[i].getMaximum();
        const double norm = (maxVal > minVal)
                                ? ((sliders[i].getValue() - minVal) / (maxVal - minVal))
                                : 0.0;
        const int pct = juce::jlimit (0, 100, juce::roundToInt (norm * 100.0));

        graphics.setColour (juce::Colour (0xffe5e8e1));
        graphics.setFont (juce::Font (i == destroyControl ? 19.0f : 15.0f, juce::Font::bold));
        graphics.drawText (juce::String (pct) + " %", box, juce::Justification::centred, false);
    }

    {
        juce::Graphics::ScopedSaveState saved (graphics);
        graphics.addTransform (juce::AffineTransform::rotation (-0.19f, 125.0f, 770.0f));

        graphics.setColour (juce::Colour (0xff171918));
        graphics.fillRoundedRectangle (34.0f, 608.0f, 186.0f, 26.0f, 2.0f);
        graphics.setColour (juce::Colour (0xffd5d8d0));
        graphics.setFont (juce::Font (15.0f, juce::Font::bold));
        graphics.drawText ("MKBTS", 38.0f, 609.0f, 76.0f, 24.0f, juce::Justification::centredLeft, false);
        graphics.setColour (juce::Colour (0xffff4b3e));
        graphics.drawText ("SX-666", 122.0f, 609.0f, 92.0f, 24.0f, juce::Justification::centredRight, false);

        juce::Rectangle<float> miniLcd (96.0f, 672.0f, 58.0f, 44.0f);
        graphics.setColour (juce::Colour (0xff120c0c));
        graphics.fillRoundedRectangle (miniLcd, 4.0f);
        graphics.setColour (juce::Colour (0xff493232));
        graphics.drawRoundedRectangle (miniLcd, 4.0f, 1.2f);
        graphics.setColour (outputLevel > 0.005f ? juce::Colour (0xffff4838) : juce::Colour (0xff8a2b24));
        graphics.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 28.0f, juce::Font::bold));
        graphics.drawText (juce::String (sxStepCounter), miniLcd, juce::Justification::centred, false);

        const float padStartX = 36.0f;
        const float padStartY = 812.0f;
        const float padW = 36.0f;
        const float padH = 26.0f;
        const float gapX = 6.0f;
        const float gapY = 6.0f;

        for (int pad = 0; pad < 12; ++pad)
        {
            const int col = pad % 4;
            const int row = pad / 4;
            juce::Rectangle<float> padRect (padStartX + col * (padW + gapX),
                                            padStartY + row * (padH + gapY),
                                            padW, padH);
            const bool isLit = (pad == activePadIndex);
            graphics.setColour (isLit ? juce::Colour (0xffff9b3d) : juce::Colour (0xff4e4b45));
            graphics.fillRoundedRectangle (padRect, 3.0f);
            graphics.setColour (isLit ? juce::Colour (0xffffe4a8) : juce::Colour (0xff232220));
            graphics.drawRoundedRectangle (padRect, 3.0f, 1.2f);

            graphics.setColour (isLit ? juce::Colour (0xff1c1208) : juce::Colour (0xffdedbd2));
            graphics.setFont (juce::Font (13.0f, juce::Font::bold));
            graphics.drawText (juce::String (pad + 1), padRect, juce::Justification::centred, false);
        }
    }
}

void MakosizerXAudioProcessorEditor::sliderValueChanged (juce::Slider* slider)
{
    juce::ignoreUnused (slider);
    updateLCDDisplay();
    repaint();
}

void MakosizerXAudioProcessorEditor::sliderDragStarted (juce::Slider* slider)
{
    juce::ignoreUnused (slider);
    audioProcessor.getPresetManager().markCurrentPresetCustom();
    updateLCDDisplay();
}

void MakosizerXAudioProcessorEditor::timerCallback()
{
    const auto levels = audioProcessor.getAndResetOutputLevels();
    ledMeter.setLevels (levels.left, levels.right);
    outputLevel = juce::jmax (levels.left, levels.right);

    if (outputLevel > 0.008f)
    {
        ++sxTickCounter;
        if (sxTickCounter >= 4)
        {
            sxTickCounter = 0;
            sxStepCounter = (sxStepCounter % 4) + 1;
            activePadIndex = randomGen.nextInt (12);
        }
    }
    else
    {
        sxStepCounter = 1;
        activePadIndex = 0;
    }

    updateLCDDisplay();
    updateOversamplingSelection();
    repaint();
}

void MakosizerXAudioProcessorEditor::updateLCDDisplay()
{
    LCDDisplay::State state;
    state.presetName = audioProcessor.getPresetManager().getCurrentPresetName();
    if (state.presetName.isEmpty())
        state.presetName = "INIT";

    state.drive = static_cast<float> (sliders[driveControl].getValue());
    state.vinyl = static_cast<float> (sliders[vinylControl].getValue());
    state.destroy = static_cast<float> (sliders[destroyControl].getValue());
    state.wow = static_cast<float> (sliders[wowControl].getValue());
    state.flutter = static_cast<float> (sliders[flutterControl].getValue());
    state.noise = static_cast<float> (sliders[noiseControl].getValue());
    state.dust = static_cast<float> (sliders[dustControl].getValue());
    state.crush = static_cast<float> (sliders[crushControl].getValue());
    state.comp = static_cast<float> (sliders[compControl].getValue());
    state.hpf = static_cast<float> (sliders[hpfControl].getValue());
    state.lpf = static_cast<float> (sliders[lpfControl].getValue());
    state.limiter = static_cast<float> (sliders[limiterControl].getValue());
    state.width = static_cast<float> (sliders[widthControl].getValue());
    state.mix = static_cast<float> (sliders[mixControl].getValue());
    state.oversampling = audioProcessor.getOversamplingFactor();
    state.outputLevel = outputLevel;
    lcdDisplay.setState (state);
    updatePresetSelection();
}

void MakosizerXAudioProcessorEditor::updatePresetSelection()
{
    auto currentPreset = audioProcessor.getPresetManager().getCurrentPresetName();
    if (currentPreset.isEmpty())
        currentPreset = "INIT";

    auto selectedIndex = presetButtons.size() - 1;
    for (std::size_t i = 0; i < presetButtons.size(); ++i)
    {
        if (presetButtons[i] != nullptr
            && presetButtons[i]->getName().equalsIgnoreCase (currentPreset))
        {
            selectedIndex = i;
            break;
        }
    }

    for (std::size_t i = 0; i < presetButtons.size(); ++i)
        if (presetButtons[i] != nullptr)
            presetButtons[i]->setToggleState (i == selectedIndex,
                                              juce::dontSendNotification);
}

void MakosizerXAudioProcessorEditor::updateOversamplingSelection()
{
    const auto factor = audioProcessor.getOversamplingFactor();
    for (std::size_t i = 0; i < oversamplingButtons.size(); ++i)
    {
        const auto choiceFactor = 1 << static_cast<int> (i);
        oversamplingButtons[i]->setToggleState (choiceFactor == factor,
                                                juce::dontSendNotification);
    }
}

void MakosizerXAudioProcessorEditor::loadPreset (const juce::String& presetName)
{
    const auto result = audioProcessor.getPresetManager().loadFactoryPreset (presetName);
    if (result.failed())
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "Preset", result.getErrorMessage());

    updateLCDDisplay();
    repaint();
}

void MakosizerXAudioProcessorEditor::openPresetSaveDialog()
{
    if (presetFileChooser != nullptr)
        return;

    const auto initialFile = juce::File::getSpecialLocation (
        juce::File::userDocumentsDirectory).getChildFile (
            "CUSTOM" + juce::String (PresetManager::presetFileExtension));
    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Save MakosizerX Preset", initialFile, "*.makopreset");

    const juce::Component::SafePointer<MakosizerXAudioProcessorEditor> safeThis (this);
    presetFileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr) return;
            auto selectedFile = chooser.getResult();
            juce::MessageManager::callAsync ([safeThis] {
                if (safeThis != nullptr) safeThis->presetFileChooser.reset();
            });
            if (selectedFile.getFullPathName().isEmpty()) return;

            if (! selectedFile.getFileName().endsWithIgnoreCase (PresetManager::presetFileExtension))
                selectedFile = selectedFile.withFileExtension (PresetManager::presetFileExtension);

            safeThis->savePresetToFile (selectedFile);
        });
}

void MakosizerXAudioProcessorEditor::openPresetLoadDialog()
{
    if (presetFileChooser != nullptr)
        return;

    const auto initialDirectory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Load MakosizerX Preset", initialDirectory, "*.makopreset");

    const juce::Component::SafePointer<MakosizerXAudioProcessorEditor> safeThis (this);
    presetFileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr) return;
            const auto selectedFile = chooser.getResult();
            juce::MessageManager::callAsync ([safeThis] {
                if (safeThis != nullptr) safeThis->presetFileChooser.reset();
            });
            if (selectedFile.getFullPathName().isEmpty()) return;
            safeThis->loadPresetFromFile (selectedFile);
        });
}

void MakosizerXAudioProcessorEditor::savePresetToFile (const juce::File& file)
{
    const auto result = audioProcessor.getPresetManager().savePreset (file, "CUSTOM");
    if (result.failed())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "Save Preset", result.getErrorMessage());
        return;
    }
    updateLCDDisplay();
    repaint();
}

void MakosizerXAudioProcessorEditor::loadPresetFromFile (const juce::File& file)
{
    const auto result = audioProcessor.getPresetManager().loadPreset (file);
    if (result.failed())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "Load Preset", result.getErrorMessage());
        return;
    }
    updateLCDDisplay();
    updateOversamplingSelection();
    repaint();
}

void MakosizerXAudioProcessorEditor::setOversamplingMode (int choiceIndex)
{
    auto* parameter = audioProcessor.getParameterState().getParameter ("oversampling");
    if (parameter == nullptr || choiceIndex < 0 || choiceIndex >= 4)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (choiceIndex)));
    parameter->endChangeGesture();
    updateOversamplingSelection();
    repaint();
}
