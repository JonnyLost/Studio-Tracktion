// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstrumentWorkspace.h"
#include "AtelierInstrumentEditor.h"
#include "StudioSampler.h"
#include "StudioSynth.h"

namespace studio
{
using namespace juce;
namespace te = tracktion;
namespace
{
Identifier viewKey (te::EditItemID id) { return Identifier ("view" + String (static_cast<int64> (id.getRawID()))); }
}

bool isNativeInstrument (te::Plugin& plugin)
{
    return dynamic_cast<sampler::Plugin*> (&plugin) != nullptr || dynamic_cast<synth::Plugin*> (&plugin) != nullptr;
}

class InstrumentWorkspace::Window final : public DocumentWindow
{
public:
    explicit Window (InstrumentWorkspace& owner)
        : DocumentWindow ("Studio Instrument", colours::panel, closeButton), host (owner)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        // A saved tiny floating window must not reopen with the bottom
        // controls clipped. Native Synth/Sampler windows retain their limits.
        const int minimumHeight = host.atelierMode
            ? jmax (340, host.editorHeight() + appearance.px (42)) : 340;
        setResizeLimits (440, minimumHeight, 1600, 1000);
        viewport.setScrollBarsShown (true, false);
        viewport.setViewedComponent (&host.content, false);
        setContentNonOwned (&viewport, false);
    }

    ~Window() override
    {
        viewport.setViewedComponent (nullptr, false);
        clearContentComponent();
    }

    void resized() override
    {
        DocumentWindow::resized();
        viewport.setScrollBarThickness (appearance.scrollbar());
        const auto minHeight = host.editorHeight();
        const auto width = viewport.getWidth() - (minHeight > viewport.getHeight() ? viewport.getScrollBarThickness() : 0);
        host.content.setSize (jmax (0, width), jmax (minHeight, viewport.getHeight()));
    }

    void paint (Graphics& g) override { g.fillAll (colours::panel); }

    void closeButtonPressed() override
    {
        MessageManager::callAsync ([safe = SafePointer<Window> (this)]
        {
            if (safe != nullptr) safe->host.setFloating (false);
        });
    }

private:
    InstrumentWorkspace& host;
    Viewport viewport;
};

InstrumentWorkspace::Content::Content (InstrumentWorkspace& workspace) : host (workspace)
{
    choice.setTitle ("Instrument instance");
    choice.setTextWhenNothingSelected ("No instrument selected");
    floating.setTooltip ("Move the instrument between Studio and a floating window");
    expand.setTooltip ("Give the instrument more room in Studio");
    addAndMakeVisible (choice);
    addAndMakeVisible (floating);
    addAndMakeVisible (expand);
    choice.onChange = [this]
    {
        const auto index = choice.getSelectedItemIndex();
        if (host.plugin == nullptr || ! isPositiveAndBelow (index, host.choices.size())) return;
        if (auto* track = te::findAudioTrackForID (host.plugin->edit, host.choiceTrack))
            for (auto* candidate : track->pluginList)
                if (candidate->itemID == host.choices[index])
                {
                    if (isNativeInstrument (*candidate))
                        host.open (*candidate, *track);
                    else
                        host.openAtelier (*candidate, *track);
                    break;
                }
    };
    floating.onClick = [this] { host.setFloating (! host.isFloating()); };
    expand.onClick = [this] { if (host.onExpand) host.onExpand(); };
}

void InstrumentWorkspace::Content::resized()
{
    auto area = getLocalBounds();
    if (! host.dockedHeader)
    {
        // Floating windows retain their compact selector/Dock toolbar. Studio
        // provides these same controls in its own header when docked.
        const auto rowHeight = appearance.control (30, 12);
        auto toolbar = area.removeFromTop (rowHeight + appearance.px (6)).reduced (appearance.px (12), appearance.px (3));
        const auto actionWidth = jmin (appearance.span (88), toolbar.getWidth() / 4);
        if (expand.isVisible())
            expand.setBounds (toolbar.removeFromRight (actionWidth).reduced (appearance.px (2), 0));
        floating.setBounds (toolbar.removeFromRight (actionWidth).reduced (appearance.px (2), 0));
        choice.setBounds (toolbar.withWidth (jmin (toolbar.getWidth(), appearance.span (390))));
    }
    if (host.editor) host.editor->setBounds (area);
}

InstrumentWorkspace::InstrumentWorkspace (PropertiesFile& settings) : preferences (settings)
{
    addAndMakeVisible (content);
    addChildComponent (empty);
    addChildComponent (bringBack);
    empty.setText ("Choose an instrument track to open its editor.", dontSendNotification);
    empty.setFont (FontOptions (14.0f));
    bringBack.onClick = [this] { setFloating (false); };
}

InstrumentWorkspace::~InstrumentWorkspace() { clear(); }

const char* InstrumentWorkspace::windowStateKey() const
{
    // Native Synth/Sampler geometry must not override the screenshot-approved
    // Cinematic Atelier opening size, or vice versa.
    return atelierMode ? "studio.atelierInstrumentWindow" : "studio.instrumentEditorWindow";
}

void InstrumentWorkspace::setProject (const File& file)
{
    clear();
    relocateProject (file);
    session = ValueTree ("InstrumentWorkspace");
    if (const auto xml = XmlDocument::parse (preferences.getValue (projectKey)); xml != nullptr && xml->hasTagName ("InstrumentWorkspace"))
        session = ValueTree::fromXml (*xml);
}

void InstrumentWorkspace::relocateProject (const File& file)
{
    projectKey = file == File() ? String() : "studio.instrumentWorkspace." + String::toHexString (file.getFullPathName().hashCode64());
}

void InstrumentWorkspace::rememberView()
{
    if (editor && plugin != nullptr) session.setProperty (viewKey (plugin->itemID), editor->viewState(), nullptr);
}

void InstrumentWorkspace::releaseEditor()
{
    rememberView();
    editor.reset();
    plugin = nullptr;
    atelierMode = false;
}

void InstrumentWorkspace::clear()
{
    layoutDockedHeader (nullptr, {});
    if (window)
    {
        preferences.setValue (windowStateKey(), window->getWindowStateAsString());
        window.reset();
    }
    addAndMakeVisible (content);
    releaseEditor();
    choiceTrack = {};
    choices.clear();
    content.choice.clear (dontSendNotification);
    resized();
}

void InstrumentWorkspace::populateChoices (te::AudioTrack* track)
{
    Array<te::EditItemID> ids;
    StringArray labels;
    if (track != nullptr)
        for (auto* item : track->pluginList)
            if (isNativeInstrument (*item) || (atelierMode && dynamic_cast<te::ExternalPlugin*> (item) != nullptr))
            {
                ids.add (item->itemID);
                labels.add (track->getName() + "  |  " + item->getName()
                            + (ids.size() > 1 ? " " + String (ids.size()) : String()));
            }
    choiceTrack = track != nullptr ? track->itemID : te::EditItemID();
    bool changed = choices != ids || content.choice.getNumItems() != labels.size();
    for (int i = 0; ! changed && i < labels.size(); ++i)
        changed = content.choice.getItemText (i) != labels[i];
    if (changed)
    {
        choices = std::move (ids);
        content.choice.clear (dontSendNotification);
        content.choice.addItemList (labels, 1);
    }
    content.choice.setSelectedItemIndex (plugin != nullptr ? choices.indexOf (plugin->itemID) : -1, dontSendNotification);
}

bool InstrumentWorkspace::open (te::Plugin& candidate, te::AudioTrack& track)
{
    if (! isNativeInstrument (candidate) || track.pluginList.indexOf (&candidate) < 0) return false;
    if (plugin.get() != &candidate || atelierMode)
    {
        auto component = candidate.createEditor();
        auto* native = dynamic_cast<NativeInstrumentEditor*> (component.get());
        if (native == nullptr) return false;
        releaseEditor();
        plugin = &candidate;
        atelierMode = false;
        component.release();
        editor.reset (native);
        editor->restoreView (session.getProperty (viewKey (plugin->itemID)).toString());
        editor->onSizePreferenceChanged = [this]
        {
            if (window) window->resized();
            if (onLayout) onLayout();
        };
        content.addAndMakeVisible (*editor);
    }
    populateChoices (&track);
    if (window) window->setName (track.getName() + " - " + plugin->getName());
    resized();
    content.resized();
    return true;
}

bool InstrumentWorkspace::openAtelier (te::Plugin& candidate, te::AudioTrack& track)
{
    // Opt-in visual wrapper for hosted instruments only. The original Audio
    // Unit remains untouched and is always available from the Atelier view.
    if (dynamic_cast<te::ExternalPlugin*> (&candidate) == nullptr
        || track.pluginList.indexOf (&candidate) < 0) return false;
    if (plugin.get() != &candidate || ! atelierMode)
    {
        auto component = std::make_unique<AtelierInstrumentEditor> (candidate, track);
        component->onArticulations = [this] (te::EditItemID id)
        {
            if (onArticulations) onArticulations (id);
        };
        component->onStage = [this] (te::EditItemID id)
        {
            if (onStage) onStage (id);
        };
        component->onFullView = [this]
        {
            if (window)
            {
                // A separate, explicit action enlarges legacy 960x510 floating
                // windows to the full Atelier canvas. The saved floating
                // geometry remains the user's choice until they change it.
                const auto* display = Desktop::getInstance().getDisplays().getDisplayForRect (window->getBounds());
                const auto available = display != nullptr ? display->userArea
                    : Rectangle<int> (0, 0, appearance.px (1300), appearance.px (850));
                const auto width = jmin (appearance.px (1240), jmax (440, available.getWidth() - appearance.px (40)));
                const auto height = jmin (appearance.px (830), jmax (340, available.getHeight() - appearance.px (40)));
                window->centreWithSize (width, height);
                window->resized();
                preferences.setValue (windowStateKey(), window->getWindowStateAsString());
            }
            else if (onExpand) onExpand();
        };
        releaseEditor();
        plugin = &candidate;
        atelierMode = true;
        editor = std::move (component);
        editor->restoreView (session.getProperty (viewKey (plugin->itemID)).toString());
        editor->onSizePreferenceChanged = [this]
        {
            // The selector changes presentation only; persist the chosen art
            // with this instrument, leaving Audio Unit/project audio state alone.
            rememberView();
            if (window) window->resized();
            if (onLayout) onLayout();
            if (onPreferenceChange) onPreferenceChange();
        };
        content.addAndMakeVisible (*editor);
    }
    populateChoices (&track);
    if (window) window->setName (track.getName() + " - Cinematic Atelier");
    resized();
    content.resized();
    return true;
}

void InstrumentWorkspace::updateSelection (te::AudioTrack* selected, const Array<te::AudioTrack*>& tracks, bool follow)
{
    bool changed = false;
    const auto foundTrack = std::find_if (tracks.begin(), tracks.end(), [this] (const auto* track) { return track->itemID == choiceTrack; });
    auto* currentTrack = foundTrack != tracks.end() ? *foundTrack : nullptr;
    if (plugin != nullptr && (currentTrack == nullptr || currentTrack->pluginList.indexOf (plugin.get()) < 0))
    {
        if (window)
        {
            preferences.setValue (windowStateKey(), window->getWindowStateAsString());
            window.reset();
            addAndMakeVisible (content);
        }
        releaseEditor();
        changed = true;
    }
    if (follow && selected != nullptr)
    {
        if (selected == currentTrack && plugin != nullptr) populateChoices (selected);
        else
        {
            const auto preferred = te::EditItemID::fromRawID (static_cast<uint64_t> (static_cast<int64> (session.getProperty ("instrument", var (0)))));
            te::Plugin* first = nullptr;
            const bool followingAtelier = atelierMode;
            for (auto* item : selected->pluginList)
                if ((followingAtelier ? dynamic_cast<te::ExternalPlugin*> (item) != nullptr
                                     : isNativeInstrument (*item))
                    && (first == nullptr || item->itemID == preferred))
                    first = item;
            if (first != nullptr)
                changed |= followingAtelier ? openAtelier (*first, *selected) : open (*first, *selected);
            else if (! window && plugin != nullptr) { releaseEditor(); changed = true; }
        }
    }
    if (changed)
    {
        if (plugin == nullptr) populateChoices (selected);
        resized();
        if (onLayout) onLayout();
    }
}

void InstrumentWorkspace::saveSession (bool open, DockMode mode)
{
    if (projectKey.isEmpty()) return;
    rememberView();
    session.setProperty ("instrument", open && plugin != nullptr ? static_cast<int64> (plugin->itemID.getRawID()) : 0, nullptr);
    session.setProperty ("floating", window != nullptr, nullptr);
    session.setProperty ("atelier", atelierMode, nullptr);
    session.setProperty ("layout", static_cast<int> (mode), nullptr);
    if (window) preferences.setValue (windowStateKey(), window->getWindowStateAsString());
    if (const auto xml = session.createXml())
        preferences.setValue (projectKey, xml->toString (XmlElement::TextFormat().singleLine()));
}

te::EditItemID InstrumentWorkspace::rememberedInstrument() const
{
    return te::EditItemID::fromRawID (static_cast<uint64_t> (static_cast<int64> (session.getProperty ("instrument", var (0)))));
}
DockMode InstrumentWorkspace::rememberedLayout() const
{
    return static_cast<DockMode> (jlimit (0, 2, static_cast<int> (session.getProperty ("layout", 1))));
}
bool InstrumentWorkspace::rememberedFloating() const { return static_cast<bool> (session.getProperty ("floating", false)); }
bool InstrumentWorkspace::rememberedAtelier() const { return static_cast<bool> (session.getProperty ("atelier", false)); }

int InstrumentWorkspace::minimumContentHeight() const
{
    if (! editor || window) return appearance.control (48, 13);
    // Docked selector and Float now live in the host header; do not reserve
    // a second invisible toolbar before checking for vertical overflow.
    return editor->minimumDockHeight();
}

int InstrumentWorkspace::editorHeight() const
{
    return appearance.control (30, 12) + appearance.px (6) + editor->minimumDockHeight();
}

void InstrumentWorkspace::appearanceChanged()
{
    if (editor) editor->appearanceChanged();
    resized();
    content.resized();
    if (window) window->resized();
}

void InstrumentWorkspace::setFloating (bool makeWindow)
{
    if (makeWindow == isFloating() || ! editor) return;
    if (makeWindow)
    {
        // Reparent selector and Float/Dock to the content before that content
        // moves into the top-level window's Viewport.
        layoutDockedHeader (nullptr, {});
        window = std::make_unique<Window> (*this);
        window->setName (plugin->getName());
        // The MacBook-approved Atelier composition uses a wide, low window
        // (about 960 x 510 logical points at 100%); a user's later resize is
        // restored independently of the native Synth/Sampler window size.
        window->centreWithSize (atelierMode ? appearance.px (960) : 820,
                                atelierMode ? appearance.px (510) : 620);
        window->restoreWindowStateFromString (preferences.getValue (windowStateKey()));
        if (atelierMode)
        {
            // The saved window geometry may predate the Atelier layout.
            const int minimumHeight = editorHeight() + appearance.px (42);
            if (window->getHeight() < minimumHeight)
                window->setSize (window->getWidth(), minimumHeight);
        }
        window->setVisible (true);
        window->toFront (true);
    }
    else
    {
        preferences.setValue (windowStateKey(), window->getWindowStateAsString());
        window.reset();
        addAndMakeVisible (content);
        if (onDock) onDock();
    }
    resized();
    content.resized();
    if (onLayout) onLayout();
    if (onPreferenceChange) onPreferenceChange();
}

void InstrumentWorkspace::layoutDockedHeader (Component* parent, Rectangle<int> bounds)
{
    const bool useDockHeader = parent != nullptr && window == nullptr && editor != nullptr;
    Component* owner = useDockHeader ? parent : static_cast<Component*> (&content);

    // The controls themselves move, not the instrument/plugin or any state.
    // Reparent only when necessary: resizing must not dismiss an open menu.
    if (content.choice.getParentComponent() != owner)
        owner->addAndMakeVisible (content.choice);
    if (content.floating.getParentComponent() != owner)
        owner->addAndMakeVisible (content.floating);
    dockedHeader = useDockHeader;
    content.expand.setVisible (! useDockHeader && window == nullptr);

    if (useDockHeader)
    {
        const auto gap = appearance.px (7);
        const auto buttonWidth = jmin (appearance.span (76), bounds.getWidth() / 3);
        auto floatArea = bounds.removeFromRight (buttonWidth);
        bounds.removeFromRight (jmin (gap, bounds.getWidth()));
        content.floating.setBounds (floatArea.reduced (appearance.px (2), 0));
        content.choice.setBounds (bounds.withWidth (jmin (bounds.getWidth(), appearance.span (350)))
                                      .reduced (appearance.px (2), 0));
    }
    content.resized();
}

void InstrumentWorkspace::resized()
{
    content.floating.setButtonText (window ? "Dock" : "Float");
    content.expand.setVisible (! window && ! dockedHeader);
    auto area = getLocalBounds();
    if (! window)
    {
        content.setVisible (editor != nullptr);
        content.setBounds (area);
    }
    else window->resized();
    bringBack.setVisible (window != nullptr);
    empty.setVisible (editor == nullptr);
    bringBack.setBounds (area.withSizeKeepingCentre (jmin (area.getWidth(), appearance.span (165)), appearance.control (30)));
    empty.setBounds (area.reduced (appearance.px (12), 0));
}
}
