// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioUI.h"

namespace studio
{
class NativeInstrumentEditor : public tracktion::Plugin::EditorComponent
{
public:
    virtual void appearanceChanged() = 0;
    virtual int minimumDockHeight() const = 0;
    virtual juce::String viewState() const = 0;
    virtual void restoreView (const juce::String&) = 0;
    bool isEditorResizable() override { return true; }
    std::function<void()> onSizePreferenceChanged;
};

bool isNativeInstrument (tracktion::Plugin&);

class InstrumentWorkspace final : public juce::Component
{
public:
    explicit InstrumentWorkspace (juce::PropertiesFile&);
    ~InstrumentWorkspace() override;
    void setProject (const juce::File&);
    void relocateProject (const juce::File&);
    void clear();
    bool open (tracktion::Plugin&, tracktion::AudioTrack&);
    bool openAtelier (tracktion::Plugin&, tracktion::AudioTrack&);
    void updateSelection (tracktion::AudioTrack*, const juce::Array<tracktion::AudioTrack*>&, bool follow);
    void saveSession (bool open, DockMode);
    tracktion::EditItemID rememberedInstrument() const;
    DockMode rememberedLayout() const;
    bool rememberedFloating() const;
    bool rememberedAtelier() const;
    int minimumContentHeight() const;
    void appearanceChanged();
    void setFloating (bool);
    bool isFloating() const { return window != nullptr; }
    NativeInstrumentEditor* getEditor() const { return editor.get(); }
    tracktion::Plugin* getPlugin() const { return plugin.get(); }
    void resized() override;

    std::function<void()> onLayout, onExpand, onDock, onPreferenceChange;
    std::function<void (tracktion::EditItemID)> onArticulations, onStage;

private:
    class Window;
    class Content final : public juce::Component
    {
    public:
        explicit Content (InstrumentWorkspace&);
        void resized() override;
        InstrumentWorkspace& host;
        juce::ComboBox choice;
        juce::TextButton floating { "Float" }, expand { "Expand" };
    } content { *this };
    juce::PropertiesFile& preferences;
    juce::String projectKey;
    juce::ValueTree session { "InstrumentWorkspace" };
    juce::Label empty;
    juce::TextButton bringBack { "Dock Instrument" };
    std::unique_ptr<Window> window;
    bool atelierMode = false;
    tracktion::Plugin::Ptr plugin;
    std::unique_ptr<NativeInstrumentEditor> editor;
    juce::Array<tracktion::EditItemID> choices;
    tracktion::EditItemID choiceTrack;
    void rememberView();
    void releaseEditor();
    void populateChoices (tracktion::AudioTrack*);
    const char* windowStateKey() const;
    int editorHeight() const;
};
}
