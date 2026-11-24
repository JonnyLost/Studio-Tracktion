// SPDX-License-Identifier: GPL-3.0-or-later
#include "AtelierInstrumentEditor.h"
#include "SpatialState.h"
#include "SpatialRouting.h"
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

// Portraits are the user's independent artwork collection. Audio Unit patch
// names cannot be reliably inferred from an ARIA Player instance, so the
// portrait is selected explicitly and stored with this instrument view.
File portraitDirectory()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile ("Studio").getChildFile ("AtelierArtwork");
}

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

void drawContained (Graphics& g, const Image& image, Rectangle<int> destination)
{
    // The entire approved instrument portrait must remain visible: never
    // crop scrolls, scroll heads, bows, bells, instrument feet or borders.
    if (! image.isValid() || destination.isEmpty()) return;
    const float scale = jmin ((float) destination.getWidth() / image.getWidth(),
                              (float) destination.getHeight() / image.getHeight());
    const int width = jmax (1, roundToInt (image.getWidth() * scale));
    const int height = jmax (1, roundToInt (image.getHeight() * scale));
    auto fitted = Rectangle<int> (width, height).withCentre (destination.getCentre());
    g.drawImage (image, fitted.getX(), fitted.getY(), fitted.getWidth(), fitted.getHeight(),
                 0, 0, image.getWidth(), image.getHeight());
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

// Atelier-only visual vocabulary. It is owned by this editor so the rest of
// Studio (and the original Audio Unit) retains its existing LookAndFeel.
class AtelierLookAndFeel final : public LookAndFeel_V4
{
public:
    Font getTextButtonFont (TextButton&, int height) override
    {
        return FontOptions (Font::getDefaultSerifFontName(), jmin (16.0f, (float) height * .48f), Font::plain);
    }

    void drawButtonBackground (Graphics& g, Button& button, const Colour&, bool hovered, bool down) override
    {
        const auto r = button.getLocalBounds().toFloat().reduced (1.0f);
        if (r.isEmpty()) return;
        const bool active = button.getToggleState();
        const auto top = active ? Colour (0xff75451d) : down ? Colour (0xff51321f)
                     : hovered ? Colour (0xff483023) : Colour (0xff261b17);
        const auto bottom = active ? Colour (0xff372117) : Colour (0xff171315);
        g.setGradientFill (ColourGradient (top, r.getTopLeft(), bottom, r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (active ? amber : amber.withAlpha (hovered ? .76f : .32f));
        g.drawRoundedRectangle (r.reduced (.5f), 7.0f, active ? 1.6f : .9f);
        if (active)
        {
            g.setColour (amber.withAlpha (.28f));
            g.drawRoundedRectangle (r.reduced (2.5f), 5.0f, .8f);
        }
        if (button.hasKeyboardFocus (false))
        {
            g.setColour (Colour (0xffffdeaa));
            g.drawRoundedRectangle (r.reduced (3.0f), 5.0f, 1.0f);
        }
    }

    void drawRotarySlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, Slider& slider) override
    {
        auto circle = Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const float diameter = jmin (circle.getWidth(), circle.getHeight()) * .75f;
        circle = circle.withSizeKeepingCentre (diameter, diameter);
        const auto centre = circle.getCentre();
        const float radius = circle.getWidth() * .5f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, endAngle, true);
        g.setColour (Colour (0xff6c5240));
        g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, angle, true);
        g.setColour (slider.isEnabled() ? amber : muted.withAlpha (.5f));
        g.strokePath (value, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        circle = circle.reduced (radius * .22f);
        g.setGradientFill (ColourGradient (Colour (0xffbc884a), circle.getTopLeft(),
                                           Colour (0xff50301b), circle.getBottomRight(), false));
        g.fillEllipse (circle);
        g.setColour (Colour (0xffffd08a).withAlpha (.65f));
        g.drawEllipse (circle.reduced (.5f), 1.3f);
        const auto hand = Point<float> (centre.x + std::sin (angle) * radius * .42f,
                                        centre.y - std::cos (angle) * radius * .42f);
        g.setColour (Colour (0xfffce2ad));
        g.drawLine (centre.x, centre.y, hand.x, hand.y, 2.3f);
        g.fillEllipse (centre.x - 2.0f, centre.y - 2.0f, 4.0f, 4.0f);
    }

    void drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox&) override
    {
        auto r = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (.75f);
        g.setGradientFill (ColourGradient (Colour (0xff39271b), r.getTopLeft(),
                                           Colour (0xff1e1716), r.getBottomRight(), false));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (amber.withAlpha (.48f));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        g.setColour (amber);
        Path arrow;
        const float cx = r.getRight() - 17.0f, cy = r.getCentreY();
        arrow.startNewSubPath (cx - 5, cy - 2);
        arrow.lineTo (cx, cy + 3);
        arrow.lineTo (cx + 5, cy - 2);
        g.strokePath (arrow, PathStrokeType (1.6f));
    }
};

}

AtelierInstrumentEditor::AtelierInstrumentEditor (te::Plugin& source, te::AudioTrack& audioTrack)
    : instrument (&source), trackId (audioTrack.itemID), blueHour (loadStudy())
{
    setName ("Cinematic Atelier");
    setTitle ("Studio Instrument - " + audioTrack.getName());
    atelierStyle = std::make_unique<AtelierLookAndFeel>();
    setLookAndFeel (atelierStyle.get());
    limits.setSizeLimits (440, 340, 1600, 1000);
    setWantsKeyboardFocus (true);

    for (auto* label : { &title, &subtitle, &mixHeading, &volumeLabel, &panLabel,
                         &expressionHeading, &hint, &sceneCaption, &stageHeading,
                         &stageSummary, &routingSummary, &mappingSummary, &keyboardSummary })
        addAndMakeVisible (*label);

    initLabel (title, audioTrack.getName(), 23.0f, ivory, true);
    initLabel (subtitle, source.getName() + "  |  CINEMATIC ATELIER", 11.5f, muted);
    initLabel (mixHeading, "CHANNEL", 12.0f, amber, true);
    initLabel (volumeLabel, "VOLUME", 12.0f, ivory, true);
    initLabel (panLabel, "PAN", 12.0f, ivory, true);
    volumeLabel.setJustificationType (Justification::centred);
    panLabel.setJustificationType (Justification::centred);
    initLabel (expressionHeading, "EXPRESSION", 12.0f, ivory, true);
    expressionHeading.setJustificationType (Justification::centred);
    initLabel (hint, "Only supported controls are shown. Detailed instrument editing remains available.",
               10.5f, muted);
    initLabel (sceneCaption, "BLUE-HOUR STUDY", 11.0f, ivory, true);
    sceneCaption.setJustificationType (Justification::centred);
    initLabel (stageHeading, "ORCHESTRAL STAGE", 12.0f, amber, true);
    initLabel (stageSummary, "Stage placement is not set", 12.0f, ivory);
    initLabel (routingSummary, "Audio routing follows the Studio mixer", 11.0f, muted);
    initLabel (mappingSummary, "No verified mappings for this instrument", 12.0f, ivory);
    initLabel (keyboardSummary, "KEYBOARD REFERENCE  •  Use MIDI input to play", 10.5f, amber);

    portraitChoice.setTitle ("Instrument artwork");
    portraitChoice.setTooltip ("Choose the artwork for this instrument. The image does not change its patch.");
    portraitChoice.addItem ("Composer Study", 1);
    int portraitItem = 2;
    for (const auto* name : { "Violin", "Cello", "Flute", "Clarinet", "Horn", "Oboe" })
        portraitChoice.addItem (name, portraitItem++);
    portraitChoice.setSelectedId (1, dontSendNotification);
    portraitChoice.onChange = [this] { changePortrait(); };
    addAndMakeVisible (portraitChoice);

    for (auto* button : { &original, &articulation, &stage })
    {
        addAndMakeVisible (*button);
        initButton (*button);
    }
    original.setTooltip ("Open the original Audio Unit editor for this same instrument instance");
    articulation.setTooltip ("Open Studio's existing articulation mappings for this instrument");
    stage.setTooltip ("Open the existing Stage placement and reverb-send workspace");

    for (auto* tab : { &mainTab, &articulationTab, &mixerTab, &advancedTab })
    {
        addAndMakeVisible (*tab);
        initButton (*tab);
        tab->setClickingTogglesState (false);
    }
    mainTab.onClick = [this] { setPage (0); };
    articulationTab.onClick = [this] { setPage (1); };
    mixerTab.onClick = [this] { setPage (2); };
    advancedTab.onClick = [this] { setPage (3); };
    addAndMakeVisible (fullView);
    initButton (fullView);
    fullView.setTooltip ("Enlarge this editor to show the full Cinematic Atelier layout");
    fullView.onClick = [this] { if (onFullView) onFullView(); };

    for (size_t i = 0; i < mappingPads.size(); ++i)
    {
        auto& pad = mappingPads[i];
        addAndMakeVisible (pad);
        initButton (pad);
        pad.setClickingTogglesState (false);
        pad.onClick = [this, i] { selectArticulation ((int) i); };
        pad.setVisible (false);
    }

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
    refreshArticulations();
    setPage (0);
    startTimerHz (6);  // GUI-only polling; never touches the audio thread.
}

AtelierInstrumentEditor::~AtelierInstrumentEditor()
{
    stopTimer();
    // JUCE's children outlive the function body but not the editor. Return
    // them to the inherited LookAndFeel before the owned style is destroyed.
    setLookAndFeel (nullptr);
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
    refreshArticulations();
}

int AtelierInstrumentEditor::minimumDockHeight() const
{
    // The dock can contract on smaller MacBooks without forcing a vertical
    // scrollbar. The Main page adapts its dials and portrait to the height;
    // Studio still prefers a taller dock whenever the display has room.
    return appearance.px (315);
}

void AtelierInstrumentEditor::loadPortrait()
{
    portraitImage = {};
    if (portraitName.isNotEmpty())
    {
        const auto artwork = portraitDirectory().getChildFile (portraitName + ".jpg");
        if (artwork.existsAsFile())
        {
            portraitImage = ImageFileFormat::loadFrom (artwork);
            if (portraitImage.isValid())
            {
                // Cap cached decoded art to a generous instrument-card size,
                // never resample on the audio callback or every paint.
                const float scale = jmin (1.0f, jmin (900.0f / portraitImage.getWidth(),
                                                       1100.0f / portraitImage.getHeight()));
                if (scale < 1.0f)
                    portraitImage = portraitImage.rescaled (
                        jmax (1, roundToInt (portraitImage.getWidth() * scale)),
                        jmax (1, roundToInt (portraitImage.getHeight() * scale)),
                        Graphics::ResamplingQuality::mediumResamplingQuality);
            }
        }
    }

    sceneCaption.setText (portraitName.isEmpty() ? "BLUE-HOUR STUDY"
                         : portraitImage.isValid() ? portraitName.toUpperCase()
                                                   : "PORTRAIT NOT INSTALLED",
                         dontSendNotification);
    hint.setText (portraitName.isNotEmpty() && ! portraitImage.isValid()
            ? "This portrait is not installed. The instrument and its audio are unaffected."
            : "Only supported controls are shown. Detailed instrument editing remains available.",
            dontSendNotification);
    repaint (portraitBounds);
}

void AtelierInstrumentEditor::changePortrait()
{
    const int id = portraitChoice.getSelectedId();
    portraitName = id <= 1 ? String() : portraitChoice.getText();
    loadPortrait();
    if (onSizePreferenceChanged)
        onSizePreferenceChanged(); // Persist per-instance visual state via InstrumentWorkspace.
}

String AtelierInstrumentEditor::viewState() const
{
    return "atelier-v2|" + portraitName;
}

void AtelierInstrumentEditor::restoreView (const String& saved)
{
    portraitName = saved.startsWith ("atelier-v2|")
        ? saved.fromFirstOccurrenceOf ("|", false, false) : String();
    int chosen = 1;
    for (int i = 1; i < portraitChoice.getNumItems(); ++i)
        if (portraitChoice.getItemText (i) == portraitName)
            chosen = portraitChoice.getItemId (i);
    portraitChoice.setSelectedId (chosen, dontSendNotification);
    loadPortrait();
    refreshControls();
}

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
        const bool artworkInstalled = portraitImage.isValid();
        g.setColour (wood.darker (0.15f));
        g.fillRect (portraitBounds);
        // Reserve a caption footer outside the art; even the bottom of a
        // violin/cello image stays visible in the reduced 425-point dock.
        auto imageArea = portraitBounds.reduced (appearance.px (6));
        imageArea.removeFromBottom (appearance.control (24, 10));
        drawContained (g, artworkInstalled ? portraitImage : blueHour, imageArea);
        if (! artworkInstalled)
        {
            g.setColour (walnut.withAlpha (0.20f));
            g.fillRect (imageArea);
        }
        auto rect = portraitBounds.toFloat().reduced (1);
        g.setColour (amber.withAlpha (0.54f));
        g.drawRoundedRectangle (rect, 8.0f, 0.85f);
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
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (13));

    // One consistent hierarchy in both docking modes: instrument name first,
    // actual AU name beneath it. Do not scatter the two labels horizontally.
    auto header = area.removeFromTop (m.control (70, 17));
    auto titleRow = header.removeFromTop (m.control (37, 10));
    const int portraitMenuW = jmin (m.span (175), jmax (m.span (105), titleRow.getWidth() / 3));
    portraitChoice.setBounds (titleRow.removeFromRight (portraitMenuW).reduced (m.px (2), m.px (1)));
    title.setBounds (titleRow);
    subtitle.setBounds (header.removeFromTop (m.control (23, 9)));
    area.removeFromTop (m.px (6));

    const int gap = m.px (12);
    const bool sideBySide = area.getWidth() >= m.span (680);
    if (sideBySide)
    {
        // A narrow portrait column matches the original paintings' vertical
        // aspect better than a 30%-wide landscape crop. More room for controls.
        portraitBounds = area.removeFromLeft (jlimit (m.span (170), m.span (245),
                                                     area.getWidth() * 23 / 100));
        area.removeFromLeft (gap);
    }
    else
    {
        portraitBounds = area.removeFromTop (jmin (m.control (150), area.getHeight() / 3));
        area.removeFromTop (gap);
    }
    controlBounds = area;

    sceneCaption.setBounds (portraitBounds.reduced (m.px (6))
                               .removeFromBottom (m.control (24, 10)));

    auto controls = controlBounds.reduced (m.px (17));
    const int commandHeight = m.control (32, 11);
    auto commands = controls.removeFromBottom (commandHeight);
    const int commandGap = m.px (7);
    if (commands.getWidth() >= m.span (550))
    {
        original.setBounds (commands.removeFromRight (jmin (m.span (205), commands.getWidth() / 3)));
        commands.removeFromRight (commandGap);
        stage.setBounds (commands.removeFromRight (jmin (m.span (170), commands.getWidth() / 2)));
        commands.removeFromRight (commandGap);
        articulation.setBounds (commands);
    }
    else
    {
        // At narrow widths the dock keeps the original editor accessible.
        // Stage and articulation remain reachable through Studio's sidebar.
        original.setBounds (commands);
        articulation.setBounds ({});
        stage.setBounds ({});
    }

    controls.removeFromBottom (m.px (9));
    hint.setBounds (controls.removeFromBottom (m.control (35, 12)));
    auto mix = controls;
    mixHeading.setBounds (mix.removeFromTop (m.control (28, 10)));
    mix.removeFromTop (m.px (5));

    const bool hasExpression = expressionParameter != nullptr;
    const int count = hasExpression ? 3 : 2;
    const int cellWidth = jmax (1, mix.getWidth() / count);

    // Keep the controls *physically sized*, rather than stretching JUCE
    // rotary sliders to fill an 800px-wide floating editor. Textbox positions
    // and the underlying Tracktion parameter bindings are unchanged.
    const auto placeDial = [&] (Label& heading, Slider& slider, Rectangle<int> cell)
    {
        const int labelH = jmin (m.control (24, 9), cell.getHeight());
        const int sliderH = jmax (0, jmin (m.control (145, 14), cell.getHeight() - labelH));
        const int sliderW = jmax (0, jmin (m.span (125), cell.getWidth() - m.px (10)));
        const int totalH = labelH + sliderH;
        const int top = cell.getY() + jmax (0, (cell.getHeight() - totalH) / 2);
        const int left = cell.getCentreX() - sliderW / 2;
        heading.setBounds (left, top, sliderW, labelH);
        slider.setBounds (left, top + labelH, sliderW, sliderH);
    };

    placeDial (volumeLabel, volume, mix.removeFromLeft (cellWidth));
    if (hasExpression)
    {
        placeDial (panLabel, pan, mix.removeFromLeft (cellWidth));
        placeDial (expressionHeading, expression, mix);
    }
    else
    {
        placeDial (panLabel, pan, mix);
        expressionHeading.setBounds ({});
        expression.setBounds ({});
    }
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
        && bounds.contains (title.getBounds()) && bounds.contains (portraitChoice.getBounds())
        && bounds.contains (original.getBounds())
        && bounds.contains (volumeLabel.getBounds()) && bounds.contains (panLabel.getBounds())
        && bounds.contains (volume.getBounds()) && bounds.contains (pan.getBounds())
        && volume.getWidth() <= appearance.span (125)
        && pan.getWidth() <= appearance.span (125);
}
#endif
}
