// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioEnvironments.h"
#include "StudioUI.h"
#include <StudioEnvironmentData.h>

#include <algorithm>
#include <array>

namespace studio
{
using namespace juce;

namespace
{
struct SceneInfo { const char* title; const char* subtitle; const char* resource; };
constexpr std::array<SceneInfo, environments::sceneCount> scenes {{
    { "Blue-Hour Study", "A quiet evening with the score.", "BlueHourStudy_jpg" },
    { "Midnight Film Scoring", "Where stories find their music.", "MidnightScoring_jpg" },
    { "Orchestral Stage", "A room waiting for its first downbeat.", "OrchestralStage_jpg" },
    { "The Composer's Desk", "A place for the next idea.", "ComposerDesk_jpg" },
    { "Modern Scoring Studio", "Your orchestra, at your fingertips.", "ModernStudio_jpg" }
}};

constexpr auto showKey = "studio.environments.showAtStartup";
constexpr auto lastKey = "studio.environments.lastScene";
constexpr auto rotationKey = "studio.environments.rotation";
constexpr auto cycleKey = "studio.environments.cycle";
constexpr auto favouritesKey = "studio.environments.favourites";

Rectangle<float> cropToFill (const Image& image, Rectangle<float> destination)
{
    auto source = image.getBounds().toFloat();
    if (source.isEmpty() || destination.isEmpty()) return source;
    const auto ratio = destination.getWidth() / destination.getHeight();
    const auto sourceRatio = source.getWidth() / source.getHeight();
    if (sourceRatio > ratio)
        source = source.withSizeKeepingCentre (source.getHeight() * ratio, source.getHeight());
    else
        source = source.withSizeKeepingCentre (source.getWidth(), source.getWidth() / ratio);
    return source;
}

void drawCover (Graphics& graphics, const Image& image, Rectangle<float> destination)
{
    if (! image.isValid() || destination.isEmpty()) return;
    const auto target = destination.getSmallestIntegerContainer();
    const auto source = cropToFill (image, destination).getSmallestIntegerContainer();
    graphics.drawImage (image, target.getX(), target.getY(), target.getWidth(), target.getHeight(),
                        source.getX(), source.getY(), source.getWidth(), source.getHeight());
}

environments::Rotation currentRotation (const ComboBox& box)
{
    switch (box.getSelectedId())
    {
        case 2: return environments::Rotation::sequential;
        case 3: return environments::Rotation::fixed;
        default: return environments::Rotation::random;
    }
}
}

StudioEnvironmentWelcome::SceneTile::SceneTile() : Button ("Studio environment")
{
    setWantsKeyboardFocus (true);
    setClickingTogglesState (false);
}

void StudioEnvironmentWelcome::SceneTile::setPreview (Image image, String title)
{
    preview = std::move (image);
    caption = std::move (title);
    setTitle (caption);
    setDescription ("Show the " + caption + " illustration");
    repaint();
}

void StudioEnvironmentWelcome::SceneTile::paintButton (Graphics& g, bool hovered, bool pressed)
{
    auto bounds = getLocalBounds().reduced (1).toFloat();
    if (bounds.isEmpty()) return;
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 5.0f);
    if (preview.isValid())
    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (bounds.getSmallestIntegerContainer());
        drawCover (g, preview, bounds);
    }
    auto title = bounds.removeFromBottom ((float) appearance.control (25, 11));
    g.setColour (Colour (0xe810141c));
    g.fillRect (title);
    g.setColour (colours::text);
    g.setFont (uiFont (11.0f, Font::bold));
    g.drawFittedText (caption, title.toNearestInt().reduced (appearance.px (6), 0),
                      Justification::centredLeft, 1);
    g.setColour (getToggleState() ? colours::focus : hovered || pressed
                     ? colours::action : colours::divider.withAlpha (0.65f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 5.0f,
                            getToggleState() ? 2.5f : 1.0f);
}

StudioEnvironmentWelcome::StudioEnvironmentWelcome (PropertiesFile& prefs) : preferences (prefs)
{
    setWantsKeyboardFocus (true);
    setTitle ("Studio Environments welcome screen");
    setDescription ("Choose a project, browse Studio artwork, or continue to the arrangement.");
    setOpaque (true);
    favourites = environments::eligibleMask (preferences.getIntValue (favouritesKey,
                                                        environments::allScenesMask));

    for (int i = 0; i < environments::sceneCount; ++i)
    {
        auto tile = std::make_unique<SceneTile>();
        const auto decoded = loadArtwork (i);
        Image preview;
        if (decoded.isValid())
        {
            preview = Image (Image::RGB, 300, 175, true);
            Graphics graphics (preview);
            drawCover (graphics, decoded, preview.getBounds().toFloat());
        }
        tile->setPreview (std::move (preview), scenes[(std::size_t) i].title);
        tile->onClick = [this, i] { selectScene (i, true); };
        addAndMakeVisible (*tile);
        tiles[(std::size_t) i] = std::move (tile);
    }

    for (auto* button : { &newProject, &openProject, &continueButton, &previousButton, &nextButton })
        addAndMakeVisible (*button);
    for (auto* control : { &launchToggle, &cycleToggle, &favouriteToggle })
    {
        addAndMakeVisible (*control);
        control->setColour (ToggleButton::textColourId, colours::text);
    }

    newProject.setColour (TextButton::buttonColourId, colours::action);
    newProject.setColour (TextButton::textColourOffId, colours::canvas);
    newProject.setTooltip ("Create a new Studio project");
    openProject.setTooltip ("Open an existing Studio project");
    continueButton.setTooltip ("Close the welcome screen and show the arrangement");
    previousButton.setTooltip ("Previous Studio environment");
    nextButton.setTooltip ("Next Studio environment");
    newProject.onClick = [this] { dismiss(); if (onNewProject) onNewProject(); };
    openProject.onClick = [this] { dismiss(); if (onOpenProject) onOpenProject(); };
    continueButton.onClick = [this] { dismiss(); if (onContinue) onContinue(); };
    previousButton.onClick = [this]
    {
        selectScene ((scene + environments::sceneCount - 1) % environments::sceneCount, true);
    };
    nextButton.onClick = [this]
    {
        selectScene ((scene + 1) % environments::sceneCount, true);
    };

    addAndMakeVisible (rotationMode);
    addAndMakeVisible (rotationLabel);
    rotationMode.addItem ("Shuffle", 1);
    rotationMode.addItem ("In Order", 2);
    rotationMode.addItem ("Keep Selected", 3);
    rotationMode.setTitle ("Environment rotation mode");
    rotationMode.setSelectedId (jlimit (1, 3, preferences.getIntValue (rotationKey, 1)), dontSendNotification);
    rotationLabel.setText ("Rotation", dontSendNotification);
    rotationLabel.setColour (Label::textColourId, colours::text);
    rotationLabel.setFont (uiFont (12.0f));

    launchToggle.setToggleState (preferences.getBoolValue (showKey, true), dontSendNotification);
    cycleToggle.setToggleState (preferences.getBoolValue (cycleKey, true), dontSendNotification);
    launchToggle.onClick = [this] { persistPreferences(); };
    cycleToggle.onClick = [this] { updateRotationTimer(); persistPreferences(); };
    rotationMode.onChange = [this] { updateRotationTimer(); persistPreferences(); };
    favouriteToggle.onClick = [this]
    {
        const auto bit = 1 << scene;
        const auto candidate = favouriteToggle.getToggleState() ? favourites | bit : favourites & ~bit;
        // Keep at least one illustration in the rotation.
        favourites = candidate != 0 ? candidate : bit;
        updateFavouriteToggle();
        persistPreferences();
    };
    selectScene (jlimit (0, environments::sceneCount - 1,
                         preferences.getIntValue (lastKey, 0)), false);
    setVisible (false);
}

StudioEnvironmentWelcome::~StudioEnvironmentWelcome()
{
    stopTimer();
}

bool StudioEnvironmentWelcome::showAtLaunch() const
{
    return preferences.getBoolValue (showKey, true);
}

void StudioEnvironmentWelcome::open (bool chooseNextArtwork)
{
    if (chooseNextArtwork)
        selectScene (environments::nextScene (preferences.getIntValue (lastKey, -1), favourites,
                      currentRotation (rotationMode), (std::uint32_t) Random::getSystemRandom().nextInt()), false);
    setVisible (true);
    toFront (false);
    grabKeyboardFocus();
    updateRotationTimer();
    repaint();
}

void StudioEnvironmentWelcome::dismiss()
{
    stopTimer();
    setVisible (false);
    persistPreferences();
}

void StudioEnvironmentWelcome::selectForReview (int index)
{
    selectScene (index, false);
}

bool StudioEnvironmentWelcome::essentialControlsFit() const
{
    const auto inside = [this] (const Component& component)
    {
        return ! component.getBounds().isEmpty()
            && getLocalBounds().contains (component.getBounds());
    };
    for (const Component* control : std::array<const Component*, 9> {
             &newProject, &openProject, &continueButton, &launchToggle, &cycleToggle,
             &favouriteToggle, &rotationMode, &previousButton, &nextButton })
        if (! inside (*control)) return false;
    for (const auto& tile : tiles)
        if (! inside (*tile)) return false;
    return true;
}

void StudioEnvironmentWelcome::selectScene (int next, bool persist)
{
    if (! isPositiveAndBelow (next, environments::sceneCount)) return;
    scene = next;
    artwork = loadArtwork (scene); // One full-size decoded image at a time.
    for (int i = 0; i < environments::sceneCount; ++i)
        tiles[(std::size_t) i]->setToggleState (scene == i, dontSendNotification);
    updateFavouriteToggle();
    repaint();
    if (persist)
    {
        updateRotationTimer();
        persistPreferences();
    }
}

void StudioEnvironmentWelcome::updateFavouriteToggle()
{
    favouriteToggle.setToggleState ((favourites & (1 << scene)) != 0, dontSendNotification);
}

void StudioEnvironmentWelcome::persistPreferences()
{
    preferences.setValue (showKey, launchToggle.getToggleState());
    preferences.setValue (cycleKey, cycleToggle.getToggleState());
    preferences.setValue (rotationKey, rotationMode.getSelectedId());
    preferences.setValue (lastKey, scene);
    preferences.setValue (favouritesKey, favourites);
    preferences.saveIfNeeded();
}

void StudioEnvironmentWelcome::updateRotationTimer()
{
    stopTimer();
    if (isVisible() && cycleToggle.getToggleState()
        && currentRotation (rotationMode) != environments::Rotation::fixed)
        startTimer (12000);
}

void StudioEnvironmentWelcome::advanceScene()
{
    selectScene (environments::nextScene (scene, favourites, currentRotation (rotationMode),
                               (std::uint32_t) Random::getSystemRandom().nextInt()), false);
}

void StudioEnvironmentWelcome::timerCallback()
{
    if (isVisible()) advanceScene();
}

Image StudioEnvironmentWelcome::loadArtwork (int index) const
{
    if (! isPositiveAndBelow (index, environments::sceneCount)) return {};
    int length = 0;
    const char* bytes = StudioEnvironmentData::getNamedResource (scenes[(std::size_t) index].resource, length);
    if (bytes == nullptr || length <= 0) return {};
    return ImageFileFormat::loadFrom (bytes, (std::size_t) length);
}

bool StudioEnvironmentWelcome::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::escapeKey || key == KeyPress::returnKey)
    {
        continueButton.triggerClick();
        return true;
    }
    if (key == KeyPress::leftKey)
    {
        previousButton.triggerClick();
        return true;
    }
    if (key == KeyPress::rightKey)
    {
        nextButton.triggerClick();
        return true;
    }
    return false;
}

void StudioEnvironmentWelcome::paint (Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll (colours::canvas);
    if (artwork.isValid())
        drawCover (g, artwork, bounds.toFloat());

    // The artwork stays unobstructed toward the right; the left gradient keeps
    // project actions legible regardless of the selected photograph.
    g.setGradientFill (ColourGradient (Colour (0xed0b1019), 0.0f, (float) getHeight() / 2,
                                       Colour (0x000b1019), (float) getWidth() * 0.75f,
                                       (float) getHeight() / 2, false));
    g.fillAll();
    g.setGradientFill (ColourGradient (Colour (0x0010151d), 0.0f, (float) getHeight() * 0.63f,
                                       Colour (0xf40b1019), 0.0f, (float) getHeight(), false));
    g.fillAll();

    const auto& m = appearance;
    const int left = m.px (26);
    const int top = m.px (27);
    g.setColour (colours::action);
    g.setFont (uiFont (11.0f, Font::bold));
    g.drawText ("WELCOME TO", left, top, m.span (260), m.line (13, 8), Justification::centredLeft);
    g.setColour (colours::text);
    g.setFont (uiFont (38.0f, Font::bold));
    g.drawFittedText ("STUDIO", left, top + m.line (15, 10), jmin (m.span (360), getWidth() * 3 / 5), m.line (43, 8),
                      Justification::centredLeft, 1);
    g.setColour (colours::secondary);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (String (scenes[(std::size_t) scene].title) + "  |  " + scenes[(std::size_t) scene].subtitle,
                      left, top + m.line (70, 8), jmin (getWidth() * 3 / 5, m.span (520)),
                      m.line (14, 8), Justification::centredLeft, 1);

    g.setColour (colours::secondary.withAlpha (0.65f));
    g.setFont (uiFont (11.0f));
    g.drawText ("YOUR ENVIRONMENTS", m.px (26), tiles[0]->getY() - m.line (13, 8),
                m.span (230), m.line (13, 8), Justification::centredLeft);
}

void StudioEnvironmentWelcome::resized()
{
    const auto& m = appearance;
    const int margin = m.px (26);
    const int gap = m.px (8);
    const int buttonWidth = jmin (m.span (220), jmax (100, getWidth() / 3));
    const int buttonHeight = jmin (m.control (39), jmax (28, getHeight() / 13));
    const int tileHeight = jmin (m.px (114), jmax (72, getHeight() / 5));
    const int tileTop = jmax (0, getHeight() - margin - tileHeight);
    const int preferredButtonTop = jmax (m.px (158), tileTop - m.px (23) - 3 * buttonHeight - 2 * gap);
    const int buttonTop = jmin (preferredButtonTop, tileTop - 3 * buttonHeight - 3 * gap);
    newProject.setBounds (margin, buttonTop, buttonWidth, buttonHeight);
    openProject.setBounds (margin, buttonTop + buttonHeight + gap, buttonWidth, buttonHeight);
    continueButton.setBounds (margin, buttonTop + 2 * (buttonHeight + gap), buttonWidth, buttonHeight);

    const int tileWidth = jmax (1, (getWidth() - margin * 2 - 4 * gap) / environments::sceneCount);
    for (int i = 0; i < environments::sceneCount; ++i)
        tiles[(std::size_t) i]->setBounds (margin + i * (tileWidth + gap), tileTop, tileWidth, tileHeight);

    const int optionWidth = jmin (m.span (200), jmax (115, getWidth() / 3));
    const int optionX = jmax (margin, getWidth() - margin - optionWidth);
    const int optionHeight = m.control (24, 12);
    launchToggle.setBounds (optionX, margin, optionWidth, optionHeight);
    cycleToggle.setBounds (optionX, margin + optionHeight, optionWidth, optionHeight);
    favouriteToggle.setBounds (optionX, margin + 2 * optionHeight, optionWidth, optionHeight);
    rotationLabel.setBounds (optionX, margin + 3 * optionHeight, optionWidth, optionHeight);
    rotationMode.setBounds (optionX, margin + 4 * optionHeight, optionWidth, m.control (28));
    const int arrowWidth = jmin (m.span (85), optionWidth / 2);
    previousButton.setBounds (optionX, margin + 4 * optionHeight + m.control (28) + gap,
                              arrowWidth - gap / 2, m.control (27));
    nextButton.setBounds (optionX + arrowWidth, margin + 4 * optionHeight + m.control (28) + gap,
                          arrowWidth - gap / 2, m.control (27));
}
}
