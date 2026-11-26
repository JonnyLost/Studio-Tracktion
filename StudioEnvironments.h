// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>

#include "EnvironmentSelection.h"
#include "EnvironmentTemplates.h"

namespace studio
{
// Artwork rotation is independent of an explicit project-template choice.
// The welcome screen requests creation; only the project owner changes the Edit.
class StudioEnvironmentWelcome final : public juce::Component, private juce::Timer
{
public:
    explicit StudioEnvironmentWelcome (juce::PropertiesFile&);
    ~StudioEnvironmentWelcome() override;

    std::function<void(environments::TemplateId)> onNewProject;
    std::function<void()> onOpenProject, onContinue;

    bool showAtLaunch() const;
    void open (bool chooseNextArtwork);
    void dismiss();
    void selectForReview (int index);
    int selectedScene() const { return scene; }
    bool essentialControlsFit() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class SceneTile final : public juce::Button
    {
    public:
        SceneTile();
        void setPreview (juce::Image, juce::String);
        void paintButton (juce::Graphics&, bool hovered, bool pressed) override;

    private:
        juce::Image preview;
        juce::String caption;
    };

    void timerCallback() override;
    void selectScene (int, bool persist);
    void advanceScene();
    void updateFavouriteToggle();
    void updateRotationTimer();
    void persistPreferences();
    juce::Image loadArtwork (int) const;

    juce::PropertiesFile& preferences;
    std::array<std::unique_ptr<SceneTile>, (std::size_t) environments::sceneCount> tiles;
    juce::Image artwork;
    int scene = 0;
    int favourites = environments::allScenesMask;

    juce::TextButton newProject { "New Project..." }, openProject { "Open Project" },
                     continueButton { "Continue to Studio" }, previousButton { "Previous" },
                     nextButton { "Next" };
    juce::ToggleButton launchToggle { "Show at startup" },
                       cycleToggle { "Cycle while open" },
                       favouriteToggle { "Favorite" };
    juce::ComboBox rotationMode;
    juce::Label rotationLabel;
};
}
