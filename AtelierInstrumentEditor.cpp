// SPDX-License-Identifier: GPL-3.0-or-later
#include "AtelierInstrumentEditor.h"
#include <StudioEnvironmentData.h>

namespace studio
{
using namespace juce;
namespace te = tracktion;
namespace
{
const Colour walnut { 0xff231913 };
const Colour wood   { 0xff39271c };
const Colour amber  { 0xffdca95e };
const Colour ivory  { 0xfff1e2c7 };
const Colour muted  { 0xffc5b49d };
const Colour blue   { 0xff294660 };

// The study already belongs to Studio's bundled environment collection. Keep
// a small, predecoded copy; neither the audio callback nor the GUI render path
// performs I/O or image decoding.
Image loadStudy()
{
    int length = 0;
    if (const auto* data = StudioEnvironmentData::getNamedResource ("BlueHourStudy_jpg", length);
        data != nullptr && length > 0)
    {
        const auto image = ImageFileFormat::loadFrom (data, static_cast<size_t> (length));
        if (image.isValid())
            return image.rescaled (960, 540, Graphics::ResamplingQuality::mediumResamplingQuality);
    }
    return {};
}

void drawCover (Graphics& g, const Image& image, Rectangle<int> destination)
{
    if (! image.isValid() || destination.isEmpty()) return;
    const auto ratio = static_cast<float> (destination.getWidth()) / destination.getHeight();
    int sx = 0, sy = 0, sw = image.getWidth(), sh = image.getHeight();
    if (static_cast<float> (sw) / sh > ratio)
    {
        sw = jmax (1, roundToInt (sh * ratio));
        sx = (image.getWidth() - sw) / 2;
    }
    else
    {
        sh = jmax (1, roundToInt (sw / ratio));
        sy = (image.getHeight() - sh) / 2;
    }
    g.drawImage (image, destination.getX(), destination.getY(), destination.getWidth(), destination.getHeight(),
                 sx, sy, sw, sh);
}

void initLabel (Label& label, const String& text, float size, Colour colour, bool bold = false)
{
    label.setText (text, dontSendNotification);
    label.setColour (Label::textColourId, colour);
    label.setFont (FontOptions (size, bold ? Font::bold : Font::plain));
    label.setMinimumHorizontalScale (0.75f);
}

void initButton (TextButton& button)
{
    button.setColour (TextButton::buttonColourId, wood);
    button.setColour (TextButton::textColourOffId, ivory);
    button.setColour (TextButton::buttonOnColourId, amber.darker (0.45f));
    button.setColour (TextButton::textColourOnId, ivory);
}
}

AtelierInstrumentEditor::AtelierInstrumentEditor (te::Plugin& source, te::AudioTrack& audioTrack)
    : instrument (&source), trackId (audioTrack.itemID), blueHour (loadStudy())
{
    setName ("Cinematic Atelier");
    setTitle ("Studio Instrument - " + audioTrack.getName());
    limits.setSizeLimits (440, 340, 1600, 1000);
    setWantsKeyboardFocus (true);

    for (auto* label : { &title, &subtitle, &mixHeading, &expressionHeading, &hint, &sceneCaption })
        addAndMakeVisible (*label);

    initLabel (title, audioTrack.getName(), 23.0f, ivory, true);
    initLabel (subtitle, source.getName() + "  |  CINEMATIC ATELIER", 11.5f, muted);
    initLabel (mixHeading, "CHANNEL", 12.0f, amber, true);
    initLabel (expressionHeading, "EXPRESSION", 12.0f, amber, true);
    initLabel (hint, "Only supported controls are shown. Detailed instrument editing remains available.",
               10.5f, muted);
    initLabel (sceneCaption, "BLUE-HOUR STUDY", 11.0f, ivory, true);
    sceneCaption.setJustificationType (Justification::centred);

    for (auto* button : { &original, &articulation, &stage })
    {
        addAndMakeVisible (*button);
        initButton (*button);
    }
    original.setTooltip ("Open the original Audio Unit editor for this same instrument instance");
    articulation.setTooltip ("Open Studio's existing articulation mappings for this instrument");
    stage.setTooltip ("Open the existing Stage placement and reverb-send workspace");

    original.onClick = [this]
    {
        if (instrument != nullptr) instrument->showWindowExplicitly();
    };
    articulation.onClick = [this]
    {
        if (onArticulations) onArticulations (trackId);
    };
    stage.onClick = [this]
    {
        if (onStage) onStage (trackId);
    };

    const auto configureSlider = [this] (Slider& control, const String& name)
    {
        addAndMakeVisible (control);
        control.setName (name);
        control.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        control.setTextBoxStyle (Slider::TextBoxBelow, false, 78, 20);
        control.setColour (Slider::rotarySliderFillColourId, amber);
        control.setColour (Slider::rotarySliderOutlineColourId, wood.brighter (0.4f));
        control.setColour (Slider::thumbColourId, ivory);
        control.setColour (Slider::textBoxTextColourId, ivory);
        control.setColour (Slider::textBoxBackgroundColourId, walnut);
        control.setColour (Slider::textBoxOutlineColourId, amber.withAlpha (0.28f));
    };
    configureSlider (volume, "Volume (dB)");
    volume.setRange (-100.0, 6.0, 0.1);
    volume.setSkewFactorFromMidPoint (-24.0);
    volume.setTextValueSuffix (" dB");
    volume.setDoubleClickReturnValue (true, 0.0);
    configureSlider (pan, "Pan");
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    configureSlider (expression, "Expression");

    volume.setTooltip ("Track volume. This is the same fader as Studio's Mixer.");
    pan.setTooltip ("Track stereo balance. This is the same pan as Studio's Mixer.");
    expression.setTooltip ("This control appears only if the Audio Unit exposes an Expression parameter.");

    volume.onDragStart = [this]
    {
        if (auto* fader = volumePlugin()) fader->edit.getUndoManager().beginNewTransaction ("Atelier volume");
    };
    pan.onDragStart = [this]
    {
        if (auto* fader = volumePlugin()) fader->edit.getUndoManager().beginNewTransaction ("Atelier pan");
    };
    volume.onValueChange = [this]
    {
        if (! restoring)
            if (auto* fader = volumePlugin()) fader->setVolumeDb ((float) volume.getValue());
    };
    pan.onValueChange = [this]
    {
        if (! restoring)
            if (auto* fader = volumePlugin()) fader->setPan ((float) pan.getValue());
    };

    // Do not invent CC1/CC11 mappings. Expose an Expression control only when
    // the loaded Audio Unit publishes an actual automatable parameter.
    if (auto* hosted = dynamic_cast<te::ExternalPlugin*> (&source))
        if (hosted->getAudioPluginInstance() != nullptr)
            for (auto* parameter : hosted->getAutomatableParameters())
                if (parameter != hosted->dryGain.get() && parameter != hosted->wetGain.get()
                    && parameter->getParameterName().equalsIgnoreCase ("Expression")
                    && ! parameter->getValueRange().isEmpty())
                {
                    expressionParameter = parameter;
                    break;
                }

    if (expressionParameter != nullptr)
    {
        const auto range = expressionParameter->getValueRange();
        expression.setRange (range.getStart(), range.getEnd(),
                             jmax (0.0001f, range.getLength() * 0.001f));
        expression.onDragStart = [this]
        {
            if (expressionParameter != nullptr) expressionParameter->parameterChangeGestureBegin();
        };
        expression.onDragEnd = [this]
        {
            if (expressionParameter != nullptr) expressionParameter->parameterChangeGestureEnd();
        };
        expression.onValueChange = [this]
        {
            if (! restoring && expressionParameter != nullptr)
                expressionParameter->setParameter ((float) expression.getValue(), sendNotificationSync);
        };
    }
    const bool hasExpression = expressionParameter != nullptr;
    expression.setVisible (hasExpression);
    expressionHeading.setVisible (hasExpression);
    refreshControls();
    startTimerHz (6);  // GUI-only polling; never touches the audio thread.
}

AtelierInstrumentEditor::~AtelierInstrumentEditor()
{
    stopTimer();
}

te::AudioTrack* AtelierInstrumentEditor::track() const
{
    return instrument != nullptr ? te::findAudioTrackForID (instrument->edit, trackId) : nullptr;
}

te::VolumeAndPanPlugin* AtelierInstrumentEditor::volumePlugin() const
{
    if (auto* selected = track())
        for (int i = selected->pluginList.size(); --i >= 0;)
            if (auto* found = dynamic_cast<te::VolumeAndPanPlugin*> (selected->pluginList[i]))
                return found;
    return nullptr;
}

void AtelierInstrumentEditor::refreshControls()
{
    restoring = true;
    if (auto* channel = volumePlugin())
    {
        volume.setEnabled (true);
        pan.setEnabled (true);
        if (! volume.isMouseButtonDown() && ! volume.hasKeyboardFocus (true))
            volume.setValue (channel->getVolumeDb(), dontSendNotification);
        if (! pan.isMouseButtonDown() && ! pan.hasKeyboardFocus (true))
            pan.setValue (channel->getPan(), dontSendNotification);
    }
    else
    {
        volume.setEnabled (false);
        pan.setEnabled (false);
    }
    if (expressionParameter != nullptr && ! expression.isMouseButtonDown())
        expression.setValue (expressionParameter->getCurrentValue(), dontSendNotification);
    restoring = false;
}

void AtelierInstrumentEditor::timerCallback()
{
    refreshControls();
}

int AtelierInstrumentEditor::minimumDockHeight() const
{
    return appearance.control (30, 12) + appearance.px (420);
}

String AtelierInstrumentEditor::viewState() const { return "atelier-v1"; }
void AtelierInstrumentEditor::restoreView (const String&) { refreshControls(); }

void AtelierInstrumentEditor::appearanceChanged()
{
    resized();
    repaint();
}

void AtelierInstrumentEditor::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff171719), bounds.getTopLeft(), walnut,
                                        bounds.getBottomRight(), false));
    g.fillRect (bounds);

    // The calm photographic mood is confined to the portrait. Functional
    // controls live on opaque surfaces so text remains readable.
    if (! portraitBounds.isEmpty())
    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (portraitBounds);
        drawCover (g, blueHour, portraitBounds);
        g.setGradientFill (ColourGradient (Colour (0x50100808), portraitBounds.getTopLeft().toFloat(),
                                            Colour (0xdd160d0a), portraitBounds.getBottomRight().toFloat(),
                                            false));
        g.fillRect (portraitBounds);
        auto rect = portraitBounds.toFloat().reduced (1);
        g.setColour (amber.withAlpha (0.80f));
        g.drawRoundedRectangle (rect, 9.0f, 1.0f);
    }

    if (! controlBounds.isEmpty())
    {
        auto card = controlBounds.toFloat().reduced (1);
        g.setColour (walnut);
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (amber.withAlpha (0.45f));
        g.drawRoundedRectangle (card, 10.0f, 1.0f);

        auto base = controlBounds.toFloat().reduced (17.0f);
        const auto midY = (float) mixHeading.getY() - 8;
        g.setColour (amber.withAlpha (0.27f));
        g.drawHorizontalLine ((int) midY, base.getX(), base.getRight());
    }

    const auto line = getLocalBounds().toFloat().reduced (13);
    g.setColour (blue.withAlpha (0.7f));
    g.drawLine (line.getX(), line.getBottom(), line.getRight(), line.getBottom(), 2.0f);
}

void AtelierInstrumentEditor::resized()
{
    auto area = getLocalBounds().reduced (appearance.px (13));
    auto header = area.removeFromTop (appearance.control (68, 17));
    title.setBounds (header.removeFromLeft (jmin (header.getWidth() * 45 / 100, appearance.span (330)))
                      .removeFromTop (appearance.control (34, 9)));
    subtitle.setBounds (header.removeFromTop (appearance.control (24)));
    area.removeFromTop (appearance.px (6));

    const int gap = appearance.px (12);
    const bool wide = area.getWidth() >= appearance.span (720);
    if (wide)
    {
        portraitBounds = area.removeFromLeft (jlimit (appearance.span (180), appearance.span (290),
                                                     area.getWidth() * 32 / 100));
        area.removeFromLeft (gap);
    }
    else
    {
        portraitBounds = area.removeFromTop (jmin (appearance.control (160), area.getHeight() / 3));
        area.removeFromTop (gap);
    }
    controlBounds = area;

    auto portraitFooter = portraitBounds.reduced (appearance.px (8)).removeFromBottom (appearance.control (32));
    sceneCaption.setBounds (portraitFooter);

    auto controls = controlBounds.reduced (appearance.px (17));
    const int buttonHeight = appearance.control (32, 11);
    auto commands = controls.removeFromBottom (buttonHeight);
    const auto commandGap = appearance.px (7);
    if (commands.getWidth() > appearance.span (600))
    {
        original.setBounds (commands.removeFromRight (jmin (appearance.span (205), commands.getWidth() / 3)));
        commands.removeFromRight (commandGap);
        stage.setBounds (commands.removeFromRight (jmin (appearance.span (170), commands.getWidth() / 2)));
        commands.removeFromRight (commandGap);
        articulation.setBounds (commands);
    }
    else
    {
        // Compact dock: priority to opening the original instrument. The
        // remaining destinations are still available from the Studio dock.
        original.setBounds (commands);
        articulation.setBounds ({});
        stage.setBounds ({});
    }

    controls.removeFromBottom (appearance.px (9));
    hint.setBounds (controls.removeFromBottom (appearance.control (35, 12)));
    auto mix = controls;
    const int labelHeight = appearance.control (28, 10);
    mixHeading.setBounds (mix.removeFromTop (labelHeight));
    const bool hasExpression = expressionParameter != nullptr;
    const int count = hasExpression ? 3 : 2;
    const int cell = jmax (1, mix.getWidth() / count);
    auto first = mix.removeFromLeft (cell);
    auto second = hasExpression ? mix.removeFromLeft (cell) : mix;
    volume.setBounds (first.reduced (appearance.px (6)));
    pan.setBounds (second.reduced (appearance.px (6)));
    if (hasExpression)
    {
        expression.setBounds (mix.reduced (appearance.px (6)));
        expressionHeading.setBounds (expression.getBounds().removeFromTop (appearance.control (20)));
    }
    else expression.setBounds ({});
}

#if STUDIO_SPATIAL_TESTS
bool AtelierInstrumentEditor::verifyBindings()
{
    if (auto* fader = volumePlugin())
    {
        const auto initial = fader->getVolumeDb();
        volume.setValue (jlimit (-100.0, 6.0, (double) initial - 1.0), sendNotificationSync);
        const bool works = std::abs (fader->getVolumeDb() - volume.getValue()) < 0.15;
        volume.setValue (initial, sendNotificationSync);
        return works && std::abs (fader->getVolumeDb() - initial) < 0.15;
    }
    return ! volume.isEnabled();
}

bool AtelierInstrumentEditor::essentialControlsFit() const
{
    const auto bounds = getLocalBounds();
    return ! portraitBounds.isEmpty() && ! controlBounds.isEmpty()
        && bounds.contains (title.getBounds()) && bounds.contains (original.getBounds())
        && bounds.contains (volume.getBounds()) && bounds.contains (pan.getBounds());
}
#endif
}
