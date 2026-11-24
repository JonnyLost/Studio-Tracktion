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
                         &stageSummary, &routingSummary, &mappingSummary, &keyboardSummary,
                         &stagePanLabel, &stageDepthLabel, &sendTrimLabel })
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
    initLabel (keyboardSummary, "LIVE KEYBOARD  •  click to audition", 10.5f, amber);
    initLabel (stagePanLabel, "PAN", 10.5f, muted);
    initLabel (stageDepthLabel, "DEPTH", 10.5f, muted);
    initLabel (sendTrimLabel, "ROOM SEND", 10.5f, muted);

    portraitChoice.setTitle ("Instrument artwork");
    portraitChoice.setTooltip ("Choose the artwork for this instrument. The image does not change its patch.");
    portraitChoice.addItem ("Composer Study", 1);
    int portraitItem = 2;
    StringArray availablePortraits { "Violin", "Cello", "Flute", "Clarinet", "Horn", "Oboe" };
    // A user can add independently created instrument art without recompiling
    // Studio. No bundle downloads, network calls or dependence on paid packs.
    Array<File> artFiles;
    for (const auto* pattern : { "*.jpg", "*.jpeg", "*.png" })
        portraitDirectory().findChildFiles (artFiles, File::findFiles, false, pattern);
    for (const auto& image : artFiles)
    {
        const auto name = image.getFileNameWithoutExtension();
        if (name.isNotEmpty() && ! name.containsChar ('|') && name.length() <= 64)
            availablePortraits.addIfNotAlreadyThere (name);
    }
    for (const auto& name : availablePortraits)
        portraitChoice.addItem (name, portraitItem++);
    portraitChoice.setSelectedId (1, dontSendNotification);
    portraitChoice.onChange = [this] { changePortrait(); };
    addAndMakeVisible (portraitChoice);

    previewChannel.setTitle ("MIDI preview channel");
    previewChannel.setTooltip ("Audition channel only. Does not change the track's MIDI channel, clips or ARIA patch.");
    for (int channel = 1; channel <= 16; ++channel)
        previewChannel.addItem ("MIDI Ch " + String (channel), channel);
    previewChannel.setSelectedId (1, dontSendNotification);
    previewChannel.onChange = [this]
    {
        stopKeyboardNote();
        if (onSizePreferenceChanged) onSizePreferenceChanged();
    };
    addAndMakeVisible (previewChannel);

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

    for (auto* slider : { &stagePan, &stageDepth, &sendTrim })
    {
        addAndMakeVisible (*slider);
        slider->setSliderStyle (Slider::LinearHorizontal);
        slider->setTextBoxStyle (Slider::TextBoxRight, false, 74, 20);
        slider->setColour (Slider::trackColourId, amber);
        slider->setColour (Slider::thumbColourId, Colour (0xfff3ce8a));
        slider->setColour (Slider::backgroundColourId, Colour (0xff3f2b22));
        slider->setColour (Slider::textBoxTextColourId, ivory);
        slider->setColour (Slider::textBoxBackgroundColourId, walnut);
        slider->setColour (Slider::textBoxOutlineColourId, amber.withAlpha (.24f));
    }
    stagePan.setRange (-1.0, 1.0, 0.01);
    stagePan.setDoubleClickReturnValue (true, 0);
    stageDepth.setRange (0.0, 1.0, 0.01);
    sendTrim.setRange (-60.0, 0.0, 0.1);
    sendTrim.setTextValueSuffix (" dB");
    stagePan.setTooltip ("Stage seat left/right. Edits the same saved seat as Orchestral Stage.");
    stageDepth.setTooltip ("Stage seat front/back. Zero is front, one is back.");
    sendTrim.setTooltip ("Shared room send trim. Changes live audio only when Stage audio is enabled.");
    stagePan.onValueChange = [this] { if (! restoring) editStage (0); };
    stageDepth.onValueChange = [this] { if (! restoring) editStage (1); };
    sendTrim.onValueChange = [this] { if (! restoring) editStage (2); };
    for (auto* slider : { &stagePan, &stageDepth, &sendTrim })
        slider->onDragStart = [this]
        {
            if (auto* source = track())
                source->edit.getUndoManager().beginNewTransaction ("Atelier stage position");
        };

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
    stopKeyboardNote();
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
    if (auto* source = track())
    {
        const auto seat = source->state.getChildWithName (spatial::seatType);
        const auto placed = seat.isValid();
        const bool canChange = placed && ! source->isFrozen (te::Track::anyFreeze)
            && ! source->edit.getTransport().isRecording();
        for (auto* slider : { &stagePan, &stageDepth, &sendTrim })
            slider->setEnabled (canChange);
        if (placed)
        {
            const auto value = spatial::readSeat (seat);
            if (! stagePan.isMouseButtonDown())
                stagePan.setValue (value.position.x, dontSendNotification);
            if (! stageDepth.isMouseButtonDown())
                stageDepth.setValue (value.position.depth, dontSendNotification);
            if (! sendTrim.isMouseButtonDown())
                sendTrim.setValue (value.sendTrim, dontSendNotification);
        }
        String stageText = placed ? "Placed  •  " : "Not yet placed on the Stage";
        if (placed)
        {
            const auto position = spatial::readSeat (seat).position;
            stageText << "Pan " << String (roundToInt (position.x * 100)) << "  •  Depth "
                      << String (roundToInt (position.depth * 100)) << "%";
        }
        const auto room = spatial::activeRoomSendDb (*source);
        const String routeText = room.has_value()
            ? "Shared room send  " + String (*room, 1) + " dB"
            : "Shared room send off  •  Studio track output";
        if (stageSummary.getText() != stageText) stageSummary.setText (stageText, dontSendNotification);
        if (routingSummary.getText() != routeText) routingSummary.setText (routeText, dontSendNotification);
    }
    restoring = false;
}

void AtelierInstrumentEditor::timerCallback()
{
    if (heldPreviewNote >= 0 && ! canPreviewKeyboard())
        stopKeyboardNote();
    // A keyswitch needs a short real note duration to work reliably in
    // hosted AUs; release after one GUI tick, independently of the held key.
    if (heldKeyswitchNote >= 0)
    {
        if (auto* source = track())
            source->injectLiveMidiMessage (MidiMessage::noteOff (
                heldPreviewChannel, heldKeyswitchNote), {});
        heldKeyswitchNote = -1;
    }
    refreshControls();
    refreshArticulations();
    const String help = canPreviewKeyboard() ? "LIVE KEYBOARD  •  click keys to audition"
                                            : "KEYBOARD  •  load an active Audio Unit to audition";
    if (keyboardSummary.getText() != help)
        keyboardSummary.setText (help, dontSendNotification);
}


void AtelierInstrumentEditor::editStage (int field)
{
    auto* source = track();
    if (source == nullptr || source->isFrozen (te::Track::anyFreeze)
        || source->edit.getTransport().isRecording()) return;
    auto savedSeat = source->state.getChildWithName (spatial::seatType);
    if (! savedSeat.isValid()) return; // Placement must be explicit in Stage.

    const auto key = field == 0 ? Identifier ("x")
                   : field == 1 ? Identifier ("depth") : Identifier ("trim");
    const float value = field == 0 ? (float) stagePan.getValue()
                     : field == 1 ? (float) stageDepth.getValue()
                                  : (float) sendTrim.getValue();
    const auto old = (float) savedSeat.getProperty (key);
    if (std::abs (value - old) < 0.0001f) return;

    auto& history = source->edit.getUndoManager();
    if (! stagePan.isMouseButtonDown() && ! stageDepth.isMouseButtonDown()
        && ! sendTrim.isMouseButtonDown())
        history.beginNewTransaction ("Atelier stage seat");
    savedSeat.setProperty (key, value, &history);
    const auto updated = spatial::readSeat (savedSeat);
    spatial::updateRoomSend (*source, updated.position, updated.sendTrim);
    refreshControls();
}

void AtelierInstrumentEditor::setPage (int next)
{
    stopKeyboardNote();
    next = jlimit (0, 3, next);
    if (page == next && ! restoring)
    {
        updatePageVisibility();
        return;
    }
    page = next;
    updatePageVisibility();
    resized();
    repaint();
    if (! restoring && onSizePreferenceChanged)
        onSizePreferenceChanged(); // Studio keeps the page per instrument.
}

void AtelierInstrumentEditor::updatePageVisibility()
{
    for (int i = 0; i < 4; ++i)
    {
        auto* tab = std::array<TextButton*, 4> { &mainTab, &articulationTab, &mixerTab, &advancedTab }[(size_t) i];
        tab->setToggleState (page == i, dontSendNotification);
    }
    const bool mixing = page == 0 || page == 2;
    const bool showPads = page == 0 || page == 1;
    volume.setVisible (mixing);
    pan.setVisible (mixing);
    expression.setVisible (mixing && expressionParameter != nullptr);
    volumeLabel.setVisible (mixing);
    panLabel.setVisible (mixing);
    expressionHeading.setVisible (mixing && expressionParameter != nullptr);
    mixHeading.setVisible (mixing);
    stageHeading.setVisible ((page == 0 && fullPresentation) || (page == 2 && fullPresentation) || page == 3);
    const bool editableStage = fullPresentation && page == 0;
    for (auto* item : std::initializer_list<Component*> { &stagePan, &stageDepth, &sendTrim,
                                                            &stagePanLabel, &stageDepthLabel, &sendTrimLabel })
        item->setVisible (editableStage);
    stageSummary.setVisible (page == 0 || page == 2 || page == 3);
    routingSummary.setVisible ((page == 0 && fullPresentation) || (page == 2 && fullPresentation) || page == 3);
    mappingSummary.setVisible (showPads);
    hint.setVisible (page == 3);
    original.setVisible ((page == 0 && fullPresentation) || page == 3);
    articulation.setVisible (page == 1 || page == 3);
    stage.setVisible ((page == 0 && fullPresentation) || (page == 2 && fullPresentation) || page == 3);
    for (size_t i = 0; i < mappingPads.size(); ++i)
        mappingPads[i].setVisible (showPads && i < currentMappings.size()
                                      && (page == 1 || fullPresentation || i < 4));
}

void AtelierInstrumentEditor::refreshArticulations()
{
    auto* source = track();
    if (source == nullptr || instrument == nullptr) return;
    const auto serialized = source->state.getProperty ("studioArticulationMappings").toString();
    if (mappingStamp == serialized) return;
    mappingStamp = serialized;
    currentMappings.clear();
    activeMappingId.clear();
    if (auto root = parseXML (serialized); root != nullptr && root->hasTagName ("StudioArticulations"))
        for (auto* profile : root->getChildWithTagNameIterator ("Instrument"))
            if (profile->getStringAttribute ("instance") == instrument->itemID.toString())
            {
                currentMappings = readArticulations (*profile);
                activeMappingId = profile->getStringAttribute ("active");
                break;
            }
    mappingSummary.setText (currentMappings.empty()
        ? "No verified articulations yet  •  Use Edit Mappings to assign them."
        : String (currentMappings.size()) + " verified articulations"
          + (currentMappings.size() > mappingPads.size() ? "  •  More in Mappings" : String()),
        dontSendNotification);
    for (size_t i = 0; i < mappingPads.size(); ++i)
    {
        auto& button = mappingPads[i];
        if (i >= currentMappings.size()) continue;
        const auto& mapping = currentMappings[i];
        button.setButtonText (mapping.name);
        button.setTooltip ("Select " + mapping.name + "  •  " + (mapping.message == ArticulationMessage::keyswitch
            ? "note " : "CC ") + String (mapping.number) + "  •  channel " + String (mapping.channel)
            + ". Selection does not send a MIDI audition.");
        button.setToggleState (mapping.id == activeMappingId, dontSendNotification);
    }
    updatePageVisibility();
    resized();
}

void AtelierInstrumentEditor::selectArticulation (int index)
{
    auto* source = track();
    if (instrument == nullptr || source == nullptr
        || ! isPositiveAndBelow (index, (int) currentMappings.size())
        || source->edit.getTransport().isRecording()
        || source->isFrozen (te::Track::anyFreeze))
        return;
    // Use the existing Studio Articulation Palette's canonical track property,
    // instrument-instance ID and undo semantics; never synthesize mapping data.
    const auto serialized = source->state.getProperty ("studioArticulationMappings").toString();
    auto root = parseXML (serialized);
    if (root == nullptr || ! root->hasTagName ("StudioArticulations")) return;
    for (auto* profile : root->getChildWithTagNameIterator ("Instrument"))
        if (profile->getStringAttribute ("instance") == instrument->itemID.toString())
        {
            const auto& selected = currentMappings[(size_t) index];
            if (profile->getStringAttribute ("active") == selected.id)
                profile->removeAttribute ("active");
            else
                profile->setAttribute ("active", selected.id);
            auto& history = source->edit.getUndoManager();
            history.beginNewTransaction ("Select Atelier articulation");
            source->state.setProperty ("studioArticulationMappings",
                root->toString (XmlElement::TextFormat().singleLine()), &history);
            history.beginNewTransaction();
            refreshArticulations();
            return;
        }
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
        File artwork;
        for (const auto* suffix : { ".jpg", ".jpeg", ".png" })
        {
            auto candidate = portraitDirectory().getChildFile (portraitName + suffix);
            if (candidate.existsAsFile()) { artwork = candidate; break; }
        }
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
    return "atelier-v4|" + portraitName + "|" + String (page)
        + "|" + String (jlimit (1, 16, previewChannel.getSelectedId()));
}

void AtelierInstrumentEditor::restoreView (const String& saved)
{
    const ScopedValueSetter<bool> restoringState (restoring, true);
    if (saved.startsWith ("atelier-v4|"))
    {
        const auto rest = saved.fromFirstOccurrenceOf ("|", false, false);
        const auto preceding = rest.upToLastOccurrenceOf ("|", false, false);
        portraitName = preceding.upToLastOccurrenceOf ("|", false, false);
        page = jlimit (0, 3, preceding.fromLastOccurrenceOf ("|", false, false).getIntValue());
        previewChannel.setSelectedId (jlimit (1, 16,
            rest.fromLastOccurrenceOf ("|", false, false).getIntValue()), dontSendNotification);
    }
    else if (saved.startsWith ("atelier-v3|"))
    {
        const auto rest = saved.fromFirstOccurrenceOf ("|", false, false);
        portraitName = rest.upToLastOccurrenceOf ("|", false, false);
        page = jlimit (0, 3, rest.fromLastOccurrenceOf ("|", false, false).getIntValue());
    }
    else
    {
        portraitName = saved.startsWith ("atelier-v2|")
            ? saved.fromFirstOccurrenceOf ("|", false, false) : String();
        page = 0;
    }
    int chosen = 1;
    for (int i = 1; i < portraitChoice.getNumItems(); ++i)
        if (portraitChoice.getItemText (i) == portraitName)
            chosen = portraitChoice.getItemId (i);
    portraitChoice.setSelectedId (chosen, dontSendNotification);
    loadPortrait();
    refreshControls();
    refreshArticulations();
    updatePageVisibility();
    resized();
}

void AtelierInstrumentEditor::appearanceChanged()
{
    resized();
    repaint();
}

void AtelierInstrumentEditor::drawAtelierSurface (Graphics& g, Rectangle<int> bounds, const String& caption) const
{
    if (bounds.isEmpty()) return;
    const auto card = bounds.toFloat().reduced (1.0f);
    g.setGradientFill (ColourGradient (Colour (0xff32231b), card.getTopLeft(),
                                      Colour (0xff1a1515), card.getBottomRight(), false));
    g.fillRoundedRectangle (card, 8.0f);
    g.setColour (amber.withAlpha (.47f));
    g.drawRoundedRectangle (card.reduced (.5f), 8.0f, .8f);
    g.setColour (amber.withAlpha (.17f));
    g.drawLine (card.getX() + 13, card.getY() + 28,
                card.getRight() - 13, card.getY() + 28, .8f);
    if (caption.isNotEmpty())
    {
        g.setColour (amber);
        g.setFont (FontOptions (Font::getDefaultSerifFontName(), appearance.fontSize (13.0f), Font::plain));
        g.drawFittedText (caption.toUpperCase(), bounds.reduced (appearance.px (14))
              .removeFromTop (appearance.control (23, 10)), Justification::centredLeft, 1, .85f);
    }
}

void AtelierInstrumentEditor::paintKeyboardReference (Graphics& g) const
{
    if (keyboardBounds.isEmpty()) return;
    auto background = keyboardBounds.toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff432918), background.getTopLeft(),
                                       Colour (0xff130f12), background.getBottomLeft(), false));
    g.fillRoundedRectangle (background.reduced (1), 8.0f);
    g.setColour (amber.withAlpha (.65f));
    g.drawRoundedRectangle (background.reduced (1.5f), 8.0f, 1.1f);
    auto keys = keyboardBounds.reduced (appearance.px (12), appearance.px (7));
    keys.removeFromTop (appearance.control (18, 9));
    constexpr int whiteCount = 42;
    if (keys.getWidth() < whiteCount) return;
    const float keyW = (float) keys.getWidth() / whiteCount;
    for (int index = 0; index < whiteCount; ++index)
    {
        auto key = Rectangle<float> (keys.getX() + keyW * index, (float) keys.getY(),
                                     keyW - .6f, (float) keys.getHeight());
        g.setGradientFill (ColourGradient (Colour (0xfffff5db), key.getTopLeft(),
                                           Colour (0xffba916a), key.getBottomLeft(), false));
        g.fillRoundedRectangle (key, 2.5f);
        g.setColour (Colour (0xff5a3825));
        g.drawRoundedRectangle (key, 2.5f, .65f);
    }
    constexpr std::array<bool, 7> blackAfter { true, true, false, true, true, true, false };
    for (int index = 0; index < whiteCount - 1; ++index)
        if (blackAfter[(size_t) (index % 7)])
        {
            auto key = Rectangle<float> (keys.getX() + keyW * (index + 1) - keyW * .31f,
                (float) keys.getY(), keyW * .62f, (float) keys.getHeight() * .62f);
            g.setGradientFill (ColourGradient (Colour (0xff45332b), key.getTopLeft(),
                                               Colour (0xff0c0b0f), key.getBottomLeft(), false));
            g.fillRoundedRectangle (key, 2.0f);
            g.setColour (amber.withAlpha (.52f));
            g.drawRoundedRectangle (key, 2.0f, .6f);
        }
}

bool AtelierInstrumentEditor::canPreviewKeyboard() const
{
    auto* source = track();
    auto* hosted = dynamic_cast<te::ExternalPlugin*> (instrument.get());
    return source != nullptr && hosted != nullptr && hosted->getAudioPluginInstance() != nullptr
        && instrument->isEnabled() && ! source->isFrozen (te::Track::anyFreeze)
        && ! source->edit.getTransport().isRecording()
        && source->edit.getCurrentPlaybackContext() != nullptr;
}

int AtelierInstrumentEditor::keyboardNoteAt (Point<int> position) const
{
    if (! fullPresentation || keyboardBounds.isEmpty()) return -1;
    auto keys = keyboardBounds.reduced (appearance.px (12), appearance.px (7));
    keys.removeFromTop (appearance.control (18, 9));
    if (! keys.contains (position) || keys.getWidth() < 42) return -1;
    constexpr std::array<int, 7> step { 0, 2, 4, 5, 7, 9, 11 };
    constexpr std::array<bool, 7> blackAfter { true, true, false, true, true, true, false };
    const float keyWidth = (float) keys.getWidth() / 42.0f;
    // Black keys are painted above white keys, so hit-test them first.
    if (position.y < keys.getY() + keys.getHeight() * .62f)
        for (int i = 0; i < 41; ++i)
            if (blackAfter[(size_t) (i % 7)])
            {
                const float left = keys.getX() + keyWidth * (i + 1) - keyWidth * .31f;
                if (position.x >= left && position.x < left + keyWidth * .62f)
                    return 36 + (i / 7) * 12 + step[(size_t) (i % 7)] + 1;
            }
    const int white = jlimit (0, 41, (int) ((position.x - keys.getX()) / keyWidth));
    return 36 + (white / 7) * 12 + step[(size_t) (white % 7)];
}

void AtelierInstrumentEditor::stopKeyboardNote()
{
    if (heldPreviewNote < 0 && heldKeyswitchNote < 0) return;
    if (auto* source = track())
    {
        if (heldPreviewNote >= 0)
            source->injectLiveMidiMessage (MidiMessage::noteOff (heldPreviewChannel, heldPreviewNote), {});
        if (heldKeyswitchNote >= 0)
            source->injectLiveMidiMessage (MidiMessage::noteOff (heldPreviewChannel, heldKeyswitchNote), {});
    }
    heldPreviewNote = heldKeyswitchNote = -1;
}

void AtelierInstrumentEditor::previewKeyboardNote (int note)
{
    if (note == heldPreviewNote) return;
    stopKeyboardNote();
    if (note < 0 || note > 127 || ! canPreviewKeyboard()) return;
    auto* source = track();
    if (source == nullptr) return;
    const int channel = jlimit (1, 16, previewChannel.getSelectedId());
    heldPreviewChannel = channel;

    // The same verified, per-instrument active articulation is sent before a
    // preview note, using the same track injection route as Studio's palette.
    // No guessed CC1/CC11 or instrument-specific keyswitch is synthesized.
    for (const auto& mapping : currentMappings)
        if (mapping.id == activeMappingId && mapping.channel == channel)
        {
            if (mapping.message == ArticulationMessage::controller)
                source->injectLiveMidiMessage (MidiMessage::controllerEvent (
                    channel, mapping.number, mapping.value), {});
            else if (mapping.number != note)
            {
                source->injectLiveMidiMessage (MidiMessage::noteOn (
                    channel, mapping.number, (uint8) mapping.value), {});
                heldKeyswitchNote = mapping.number;
            }
            break;
        }
    source->injectLiveMidiMessage (MidiMessage::noteOn (channel, note, (uint8) 100), {});
    heldPreviewNote = note;
    heldPreviewChannel = channel;
}

void AtelierInstrumentEditor::mouseDown (const MouseEvent& event)
{
    previewKeyboardNote (keyboardNoteAt (event.getPosition()));
}

void AtelierInstrumentEditor::mouseDrag (const MouseEvent& event)
{
    if (event.mods.isAnyMouseButtonDown())
        previewKeyboardNote (keyboardNoteAt (event.getPosition()));
}

void AtelierInstrumentEditor::mouseUp (const MouseEvent&)
{
    stopKeyboardNote();
}

void AtelierInstrumentEditor::mouseExit (const MouseEvent&)
{
    stopKeyboardNote();
}

void AtelierInstrumentEditor::focusLost (FocusChangeType)
{
    stopKeyboardNote();
}

void AtelierInstrumentEditor::paint (Graphics& g)
{
    auto canvas = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff171317), canvas.getTopLeft(),
                                        Colour (0xff312019), canvas.getBottomRight(), false));
    g.fillRect (canvas);

    // Fine wood-grain lines are deterministic vector paint, not bitmap assets
    // or any additional DSP/audio work.
    g.setColour (amber.withAlpha (.018f));
    for (int y = 0; y < getHeight(); y += 11)
        g.drawHorizontalLine (y, 0.0f, (float) getWidth());

    if (! instrumentHeaderBounds.isEmpty())
    {
        const auto bar = instrumentHeaderBounds.toFloat();
        g.setGradientFill (ColourGradient (Colour (0xff3f291b), bar.getTopLeft(),
                                            Colour (0xff1b1516), bar.getBottomRight(), false));
        g.fillRect (bar);
        g.setColour (amber.withAlpha (.46f));
        g.drawHorizontalLine (instrumentHeaderBounds.getBottom() - 1, bar.getX(), bar.getRight());
    }

    if (! portraitBounds.isEmpty())
    {
        auto outer = portraitBounds.toFloat().reduced (.5f);
        g.setColour (Colour (0xff120f12));
        g.fillRoundedRectangle (outer, 11.0f);
        g.setColour (amber.withAlpha (.87f));
        g.drawRoundedRectangle (outer.reduced (1.0f), 10.0f, 1.4f);
        g.setColour (amber.withAlpha (.27f));
        g.drawRoundedRectangle (outer.reduced (3.4f), 8.0f, .8f);
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (portraitBounds.reduced (appearance.px (5)));
        auto imageArea = portraitBounds.reduced (appearance.px (6));
        imageArea.removeFromBottom (appearance.control (24, 10));
        g.setColour (Colour (0xff332219));
        g.fillRect (imageArea);
        drawContained (g, portraitImage.isValid() ? portraitImage : blueHour, imageArea);
    }

    drawAtelierSurface (g, mappingBounds, page == 1 ? "Articulation Palette" : "Articulations");
    drawAtelierSurface (g, mixerBounds, page == 2 ? "Channel Mixer" : "Volume & Expression");
    drawAtelierSurface (g, stageBounds, "Stage & Room");
    drawAtelierSurface (g, routingBounds, page == 3 ? "Instrument & Routing" : "Routing");
    paintKeyboardReference (g);

    // Read-only placement overview is derived from the actual saved seat,
    // not an imaginary stage control or a fake active reverb.
    if (fullPresentation && ! stageBounds.isEmpty())
        if (auto* source = track())
        {
            auto seat = source->state.getChildWithName (spatial::seatType);
            auto field = stageBounds.reduced (appearance.px (24));
            field.removeFromTop (appearance.px (55));
            field.removeFromBottom (appearance.px (26));
            if (seat.isValid() && field.getWidth() > 75 && field.getHeight() > 45)
            {
                const auto position = spatial::readSeat (seat).position;
                auto plot = field.toFloat().withSizeKeepingCentre (
                    jmin ((float) field.getWidth(), 260.0f),
                    jmin ((float) field.getHeight(), 90.0f));
                g.setColour (amber.withAlpha (.32f));
                Path arc;
                arc.addCentredArc (plot.getCentreX(), plot.getBottom(),
                                  plot.getWidth() * .5f, plot.getHeight() * .9f,
                                  0, -MathConstants<float>::pi * .99f, 0, true);
                g.strokePath (arc, PathStrokeType (1.0f));
                const float x = plot.getCentreX() + position.x * plot.getWidth() * .43f;
                const float y = plot.getBottom() - (1.0f - position.depth) * plot.getHeight() * .83f;
                g.setColour (amber);
                g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            }
        }

    auto bottomLine = getLocalBounds().toFloat().reduced (appearance.px (8));
    g.setColour (amber.withAlpha (.38f));
    g.drawHorizontalLine ((int) bottomLine.getBottom() - 1,
                          bottomLine.getX(), bottomLine.getRight());
}

void AtelierInstrumentEditor::layoutSliders (Rectangle<int> area, bool horizontal)
{
    const auto& m = appearance;
    const bool hasExpression = expressionParameter != nullptr;
    const int count = hasExpression ? 3 : 2;
    const auto layoutOne = [&] (Label& label, Slider& slider, Rectangle<int> cell)
    {
        if (horizontal)
        {
            if (slider.getSliderStyle() != Slider::LinearHorizontal)
                slider.setSliderStyle (Slider::LinearHorizontal);
            slider.setTextBoxStyle (Slider::TextBoxRight, false, m.span (82), m.control (20, 11));
            label.setJustificationType (Justification::centredLeft);
            label.setBounds (cell.removeFromLeft (jmin (m.span (103), cell.getWidth() / 3))
                                  .reduced (m.px (2), 0));
            slider.setBounds (cell.reduced (m.px (6), m.px (2)));
        }
        else
        {
            if (slider.getSliderStyle() != Slider::RotaryHorizontalVerticalDrag)
                slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (Slider::TextBoxBelow, false, m.span (78), m.control (20, 11));
            label.setJustificationType (Justification::centred);
            const int labelH = jmin (m.control (22, 9), cell.getHeight());
            const int sliderH = jmax (0, jmin (m.px (160), cell.getHeight() - labelH));
            const int sliderW = jmax (0, jmin (m.span (142), cell.getWidth() - m.px (8)));
            const auto centreX = cell.getCentreX();
            const int top = cell.getY() + jmax (0, (cell.getHeight() - labelH - sliderH) / 2);
            label.setBounds (centreX - sliderW / 2, top, sliderW, labelH);
            slider.setBounds (centreX - sliderW / 2, top + labelH, sliderW, sliderH);
        }
    };
    if (horizontal)
    {
        const int rowHeight = jmax (1, area.getHeight() / count);
        layoutOne (volumeLabel, volume, area.removeFromTop (rowHeight));
        layoutOne (panLabel, pan, area.removeFromTop (rowHeight));
        if (hasExpression) layoutOne (expressionHeading, expression, area);
    }
    else
    {
        const int cellWidth = jmax (1, area.getWidth() / count);
        layoutOne (volumeLabel, volume, area.removeFromLeft (cellWidth));
        layoutOne (panLabel, pan, area.removeFromLeft (hasExpression ? cellWidth : area.getWidth()));
        if (hasExpression) layoutOne (expressionHeading, expression, area);
    }
}

void AtelierInstrumentEditor::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (11));
    const bool nextFull = area.getWidth() >= m.span (875)
        && area.getHeight() >= m.px (625);
    if (fullPresentation && ! nextFull) stopKeyboardNote();
    fullPresentation = nextFull;
    previewChannel.setVisible (fullPresentation);

    portraitBounds = controlBounds = stageBounds = mappingBounds = keyboardBounds
        = instrumentHeaderBounds = mixerBounds = routingBounds = {};

    const auto headerHeight = m.control (fullPresentation ? 84 : 69, 14);
    instrumentHeaderBounds = area.removeFromTop (headerHeight).expanded (m.px (3));
    auto row = instrumentHeaderBounds.reduced (m.px (11), m.px (5));
    auto titleRow = row.removeFromTop (m.control (42, 12));
    const int tabWidth = jmin (m.span (99), jmax (m.span (52), titleRow.getWidth() / 9));
    auto tabArea = titleRow.removeFromRight (tabWidth * 4);
    for (auto* tab : { &mainTab, &articulationTab, &mixerTab, &advancedTab })
        tab->setBounds (tabArea.removeFromLeft (tabWidth).reduced (m.px (2), m.px (1)));
    const bool offerFullView = ! fullPresentation && isFloatingWindow && isFloatingWindow();
    fullView.setBounds (offerFullView
        ? titleRow.removeFromRight (jmin (m.span (90), titleRow.getWidth() / 4)).reduced (m.px (3), m.px (2))
        : Rectangle<int>());
    portraitChoice.setBounds (titleRow.removeFromRight (jmin (m.span (175), titleRow.getWidth() / 2))
                                     .reduced (m.px (4), m.px (2)));
    title.setBounds (titleRow.reduced (m.px (3), 0));
    subtitle.setBounds (row.removeFromTop (m.control (23, 9)).reduced (m.px (3), 0));
    fullView.setVisible (offerFullView);

    area.removeFromTop (m.px (7));
    if (fullPresentation)
    {
        keyboardBounds = area.removeFromBottom (m.px (105));
        area.removeFromBottom (m.px (9));
    }
    const int artWidth = fullPresentation ? jmin (m.span (360), area.getWidth() * 31 / 100)
                                           : jlimit (m.span (165), m.span (255), area.getWidth() * 23 / 100);
    portraitBounds = area.removeFromLeft (artWidth);
    area.removeFromLeft (m.px (13));
    controlBounds = area;
    sceneCaption.setBounds (portraitBounds.reduced (m.px (7))
                               .removeFromBottom (m.control (24, 10)));
    if (keyboardBounds.isEmpty())
    {
        keyboardSummary.setBounds ({});
        previewChannel.setBounds ({});
    }
    else
    {
        auto captionRow = keyboardBounds.reduced (m.px (14), m.px (4))
                                        .removeFromTop (m.control (23, 10));
        previewChannel.setBounds (captionRow.removeFromRight (m.span (142)));
        keyboardSummary.setBounds (captionRow);
    }

    auto body = controlBounds;
    if (fullPresentation)
    {
        if (page == 0)
        {
            auto top = body.removeFromTop (body.getHeight() * 55 / 100);
            mappingBounds = top.removeFromLeft (top.getWidth() * 52 / 100);
            top.removeFromLeft (m.px (8));
            mixerBounds = top;
            body.removeFromTop (m.px (9));
            stageBounds = body.removeFromLeft (body.getWidth() * 55 / 100);
            body.removeFromLeft (m.px (8));
            routingBounds = body;
        }
        else if (page == 1) mappingBounds = body;
        else if (page == 2)
        {
            mixerBounds = body.removeFromTop (body.getHeight() * 67 / 100);
            body.removeFromTop (m.px (8));
            stageBounds = body;
        }
        else routingBounds = body;
    }
    else
    {
        if (page == 0)
        {
            mappingBounds = body.removeFromTop (jmin (m.px (98), body.getHeight() / 3));
            body.removeFromTop (m.px (7));
            mixerBounds = body;
        }
        else if (page == 1) mappingBounds = body;
        else if (page == 2) mixerBounds = body;
        else routingBounds = body;
    }

    // Every page has a real destination; never display a control surface
    // without corresponding real backing state or an explicit status label.
    mappingSummary.setBounds ({});
    for (auto& pad : mappingPads) pad.setBounds ({});
    if (! mappingBounds.isEmpty())
    {
        auto content = mappingBounds.reduced (m.px (15));
        content.removeFromTop (m.control (28, 10));
        mappingSummary.setBounds (content.removeFromTop (m.control (26, 10)));
        const bool editorPage = page == 1;
        const int footer = editorPage ? m.control (34, 11) : 0;
        if (footer > 0)
        {
            content.removeFromBottom (m.px (4));
            articulation.setBounds (content.removeFromBottom (footer));
        }
        const int maxPads = page == 0 && ! fullPresentation ? 4 : 8;
        const int columns = 4;
        const int rows = page == 0 && ! fullPresentation ? 1 : 2;
        const int gap = m.px (5);
        auto padsArea = content.reduced (0, m.px (3));
        if (! currentMappings.empty())
        {
            const int cellW = jmax (1, (padsArea.getWidth() - (columns - 1) * gap) / columns);
            const int cellH = jmax (1, (padsArea.getHeight() - (rows - 1) * gap) / rows);
            for (int i = 0; i < jmin (maxPads, (int) currentMappings.size()); ++i)
                mappingPads[(size_t) i].setBounds (padsArea.getX() + (i % columns) * (cellW + gap),
                    padsArea.getY() + (i / columns) * (cellH + gap), cellW, cellH);
        }
    }
    else articulation.setBounds ({});

    mixHeading.setBounds ({});
    if (! mixerBounds.isEmpty())
    {
        auto content = mixerBounds.reduced (m.px (15));
        mixHeading.setBounds (content.removeFromTop (m.control (28, 10)));
        if (page == 0 && ! fullPresentation)
        {
            // Compact Main gets a small, genuine Stage status line below
            // the volume/pan/Expression controls rather than extra buttons.
            stageSummary.setBounds (content.removeFromBottom (m.control (24, 9)));
        }
        content.removeFromTop (m.px (4));
        layoutSliders (content, fullPresentation && page == 0);
    }
    else
        for (auto* component : std::initializer_list<Component*> { &volume, &pan, &expression,
                                 &volumeLabel, &panLabel, &expressionHeading })
            component->setBounds ({});

    stageHeading.setBounds ({});
    for (auto* item : std::initializer_list<Component*> { &stagePan, &stageDepth, &sendTrim,
                                                            &stagePanLabel, &stageDepthLabel, &sendTrimLabel })
        item->setBounds ({});
    if (! stageBounds.isEmpty())
    {
        auto content = stageBounds.reduced (m.px (14));
        content.removeFromTop (m.control (29, 10));
        stageSummary.setBounds (content.removeFromTop (m.control (26, 10)));
        if (page == 0 && fullPresentation)
        {
            stage.setBounds (content.removeFromBottom (m.control (32, 11)));
            content.removeFromBottom (m.px (4));
            const int rows = 3;
            const int rowHeight = jmax (1, content.getHeight() / rows);
            for (auto [label, slider] : std::initializer_list<std::pair<Label*, Slider*>> {
                     { &stagePanLabel, &stagePan }, { &stageDepthLabel, &stageDepth },
                     { &sendTrimLabel, &sendTrim } })
            {
                auto line = content.removeFromTop (rowHeight);
                label->setBounds (line.removeFromLeft (jmin (m.span (92), line.getWidth() / 3)));
                slider->setBounds (line.reduced (m.px (3), m.px (2)));
            }
        }
        else if (page == 2)
            stage.setBounds (content.removeFromBottom (m.control (34, 11)));
    }
    if (! routingBounds.isEmpty())
    {
        auto content = routingBounds.reduced (m.px (15));
        content.removeFromTop (m.control (30, 10));
        if (page == 3)
        {
            stageHeading.setBounds (content.removeFromTop (m.control (25, 10)));
            stageSummary.setBounds (content.removeFromTop (m.control (28, 10)));
        }
        routingSummary.setBounds (content.removeFromTop (m.control (38, 10)));
        content.removeFromTop (m.px (6));
        if (page == 3)
        {
            hint.setBounds (content.removeFromTop (m.control (38, 10)));
            content.removeFromTop (m.px (7));
        }
        const int buttonH = m.control (34, 11);
        const int gap = m.px (7);
        if (page == 3)
        {
            auto actions = content.removeFromBottom (buttonH);
            const int width = jmax (1, (actions.getWidth() - 2 * gap) / 3);
            articulation.setBounds (actions.removeFromLeft (width));
            actions.removeFromLeft (gap);
            stage.setBounds (actions.removeFromLeft (width));
            actions.removeFromLeft (gap);
            original.setBounds (actions);
        }
        else
        {
            original.setBounds (content.removeFromBottom (buttonH));
            if (page == 0)
                stage.setBounds (content.removeFromBottom (buttonH + gap).withTrimmedBottom (gap));
        }
    }
    else if (page != 2)
    {
        original.setBounds ({});
        stage.setBounds ({});
    }

    if (page != 3) hint.setBounds ({});
    if (page == 2 && stageBounds.isEmpty()) // compact Mixer
    {
        auto line = mixerBounds.reduced (m.px (15)).removeFromBottom (m.control (24, 9));
        stageSummary.setBounds (line);
    }

    updatePageVisibility();
    repaint();
}

#if STUDIO_SPATIAL_TESTS
bool AtelierInstrumentEditor::reviewPage (int requested, bool shouldBeFull)
{
    setPage (requested);
    if (page != requested || fullPresentation != shouldBeFull) return false;
    const auto frame = getLocalBounds();
    if (! frame.contains (portraitBounds) || portraitBounds.isEmpty()
        || ! frame.contains (title.getBounds()) || ! frame.contains (mainTab.getBounds())
        || ! frame.contains (advancedTab.getBounds()))
        return false;
    if (shouldBeFull != ! keyboardBounds.isEmpty()) return false;
    if (requested == 0 || requested == 2)
        if (mixerBounds.isEmpty() || ! volume.isVisible() || ! frame.contains (volume.getBounds())
            || ! frame.contains (pan.getBounds()))
            return false;
    if (requested == 1)
        if (mappingBounds.isEmpty() || ! articulation.isVisible() || ! frame.contains (articulation.getBounds()))
            return false;
    if (requested == 3)
        if (routingBounds.isEmpty() || ! original.isVisible() || ! frame.contains (original.getBounds()))
            return false;
    return true;
}

bool AtelierInstrumentEditor::reviewSelectMappedArticulation()
{
    refreshArticulations();
    if (currentMappings.empty() || instrument == nullptr || track() == nullptr)
        return false;
    const auto id = currentMappings.front().id;
    selectArticulation (0);
    return activeMappingId == id && mappingPads.front().getToggleState();
}

bool AtelierInstrumentEditor::reviewStageEditing()
{
    auto* source = track();
    if (source == nullptr) return false;
    auto seat = source->state.getChildWithName (spatial::seatType);
    if (! seat.isValid()) return false;
    const float original = spatial::readSeat (seat).position.x;
    const double next = original < .6f ? original + .13f : original - .13f;
    stagePan.setValue (next, sendNotificationSync);
    const auto after = spatial::readSeat (seat).position.x;
    const bool worked = std::abs (after - next) < .011f;
    source->edit.undo();
    refreshControls();
    return worked && std::abs (spatial::readSeat (seat).position.x - original) < .011f;
}

bool AtelierInstrumentEditor::reviewKeyboardHitMap() const
{
    if (! fullPresentation || keyboardBounds.isEmpty()) return false;
    auto keys = keyboardBounds.reduced (appearance.px (12), appearance.px (7));
    keys.removeFromTop (appearance.control (18, 9));
    if (keys.getWidth() < 42) return false;
    const float keyWidth = (float) keys.getWidth() / 42.0f;
    const int left = keys.getX() + roundToInt (keyWidth * .4f);
    const int y = keys.getBottom() - 4;
    const int right = keys.getX() + roundToInt (keyWidth * 41.4f);
    // Bottom halves are white keys: 36 is C2 and the far right is B7.
    return keyboardNoteAt ({ left, y }) == 36
        && keyboardNoteAt ({ right, y }) == 107
        && keyboardNoteAt ({ keys.getX(), keys.getY() - 1 }) == -1;
}

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
    if (portraitBounds.isEmpty() || controlBounds.isEmpty()
        || ! bounds.contains (title.getBounds()) || ! bounds.contains (portraitChoice.getBounds())
        || ! bounds.contains (portraitBounds)) return false;
    if (page == 0 || page == 2)
        return ! mixerBounds.isEmpty() && bounds.contains (volume.getBounds())
            && bounds.contains (pan.getBounds()) && volume.getWidth() <= appearance.span (142)
            && pan.getWidth() <= appearance.span (142);
    if (page == 1)
        return ! mappingBounds.isEmpty() && bounds.contains (articulation.getBounds());
    return ! routingBounds.isEmpty() && bounds.contains (original.getBounds())
        && bounds.contains (stage.getBounds());
}
#endif
}
