// Studio's arrangement gestures, substituted for the pinned example's
// ClipComponent::mouseDown in a generated build-directory copy.
// The Tracktion checkout and the Swift Garritan Studio project are untouched.
class StudioRegionDragPreview final : public Component
{
public:
    explicit StudioRegionDragPreview (Colour c) : colour (c) {}

    void paint (Graphics& g) override
    {
        g.setColour ((allowed ? colour : Colours::tomato).withAlpha (0.54f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 3.0f);
        g.setColour (allowed ? Colours::white : Colours::tomato);
        g.drawRect (getLocalBounds().reduced (1), 2);
    }

    bool hitTest (int, int) override { return false; }
    bool allowed = true;
    Colour colour;
};

static TrackComponent* studioRegionDropRow (ClipComponent& component,
                                            const MouseEvent& event)
{
    auto* origin = dynamic_cast<TrackComponent*> (component.getParentComponent());
    auto* editor = origin != nullptr ? origin->getParentComponent() : nullptr;
    if (editor == nullptr)
        return nullptr;

    const auto point = editor->getLocalPoint (&component, event.getPosition());
    for (auto* child : editor->getChildren())
        if (auto* row = dynamic_cast<TrackComponent*> (child))
            if (row->getBounds().contains (point))
                return row;

    return nullptr;
}

static double studioRegionSnapSeconds (te::Edit& edit, double seconds, bool snap)
{
    seconds = jmax (0.0, seconds);
    if (! snap)
        return seconds;

    auto& tempo = edit.tempoSequence;
    const auto beats = tempo.toBeats (te::TimePosition::fromSeconds (seconds)).inBeats();
    // Default: quarter-note arrangement snap. Hold Option for freehand edits.
    return tempo.toTime (te::BeatPosition::fromBeats (std::round (beats))).inSeconds();
}

void ClipComponent::mouseDown (const MouseEvent& event)
{
    regionGesture = RegionGesture::none;
    regionDragStarted = false;
    regionDragClips.clear();
    regionDragPreview.reset();

    auto& selection = editViewState.selectionManager;
    if (event.mods.isShiftDown())
    {
        if (selection.isSelected (clip.get()))
            selection.deselect (clip.get());
        else
            selection.addToSelection (clip.get());
        return;
    }

    // Clicking a selected clip retains a multi-selection for group dragging.
    if (! selection.isSelected (clip.get()))
        selection.selectOnly (clip.get());

    if (! event.mods.isLeftButtonDown()
        || editViewState.edit.getTransport().isRecording()
        || clip->getClipTrack() == nullptr
        || clip->getClipTrack()->isFrozen (te::Track::anyFreeze))
        return;

    constexpr int edgePixels = 8;
    if (getWidth() > edgePixels * 3 && event.x <= edgePixels)
        regionGesture = RegionGesture::trimLeft;
    else if (getWidth() > edgePixels * 3 && event.x >= getWidth() - edgePixels)
        regionGesture = RegionGesture::trimRight;
    else
        regionGesture = RegionGesture::move;

    if (regionGesture == RegionGesture::move)
    {
        for (auto* selected : selection.getItemsOfType<te::Clip>())
            if (selected->getClipTrack() != nullptr)
                regionDragClips.emplace_back (te::Clip::Ptr (selected), selected->getPosition());
    }
    else
        regionDragClips.emplace_back (clip, clip->getPosition());
}

void ClipComponent::mouseMove (const MouseEvent& event)
{
    constexpr int edgePixels = 8;
    const bool resizeEdge = getWidth() > edgePixels * 3
                             && (event.x <= edgePixels || event.x >= getWidth() - edgePixels);
    setMouseCursor (resizeEdge ? MouseCursor::LeftRightResizeCursor
                               : MouseCursor::DraggingHandCursor);
}

void ClipComponent::mouseExit (const MouseEvent&)
{
    if (! regionDragStarted)
        setMouseCursor (MouseCursor::NormalCursor);
}

void ClipComponent::mouseDrag (const MouseEvent& event)
{
    if (regionGesture == RegionGesture::none || regionDragClips.empty()
        || editViewState.edit.getTransport().isRecording())
        return;

    if (! regionDragStarted && std::abs (event.getDistanceFromDragStartX()) < 3
                           && std::abs (event.getDistanceFromDragStartY()) < 3)
        return;

    auto* row = dynamic_cast<TrackComponent*> (getParentComponent());
    auto* editor = row != nullptr ? row->getParentComponent() : nullptr;
    if (editor == nullptr || row->getWidth() <= 0)
        return;

    regionDragStarted = true;
    if (regionDragPreview == nullptr)
    {
        regionDragPreview = std::make_unique<StudioRegionDragPreview> (clip->getColour());
        editor->addAndMakeVisible (regionDragPreview.get());
        regionDragPreview->setAlwaysOnTop (true);
        regionDragPreview->setInterceptsMouseClicks (false, false);
    }

    const auto original = clip->getPosition();
    const auto secondsPerPixel = (editViewState.viewX2.get()
                                  - editViewState.viewX1.get()).inSeconds() / row->getWidth();
    const auto dx = event.getDistanceFromDragStartX();
    const auto secondsDelta = dx * secondsPerPixel;
    const bool snap = ! event.mods.isAltDown();
    const auto start = original.getStart().inSeconds();
    const auto end = original.getEnd().inSeconds();
    const double minSeconds = jmin (0.025, jmax (0.0001, end - start) * 0.5);
    double proposedStart = start, proposedEnd = end;
    if (regionGesture == RegionGesture::trimLeft)
        proposedStart = jlimit (0.0, end - minSeconds,
                                studioRegionSnapSeconds (editViewState.edit,
                                                         start + secondsDelta, snap));
    else if (regionGesture == RegionGesture::trimRight)
        proposedEnd = jmax (start + minSeconds,
                            studioRegionSnapSeconds (editViewState.edit,
                                                     end + secondsDelta, snap));
    else
    {
        // A purely vertical cross-track move must not quantize the clip's
        // existing start time (recorded takes may be intentionally off-grid).
        proposedStart = dx == 0 ? start
                      : studioRegionSnapSeconds (editViewState.edit,
                                                 start + secondsDelta, snap);
        proposedEnd = proposedStart + (end - start);
    }

    auto* dropRow = regionGesture == RegionGesture::move
                      ? studioRegionDropRow (*this, event) : row;
    auto* target = dropRow != nullptr
                     ? dynamic_cast<te::ClipTrack*> (dropRow->getTrack()) : nullptr;
    bool allowed = target != nullptr && ! target->isFrozen (te::Track::anyFreeze);
    if (regionGesture == RegionGesture::move && allowed)
    {
        const auto* source = clip->getClipTrack();
        for (const auto& item : regionDragClips)
        {
            if (item.first->getClipTrack() == nullptr
                || item.first->getClipTrack()->isFrozen (te::Track::anyFreeze)
                || (target != source && (item.first->getClipTrack() != source
                                          || ! item.first->canBeAddedTo (*target))))
            {
                allowed = false;
                break;
            }
        }
    }

    const auto* previewRow = dropRow != nullptr ? dropRow : row;
    const int x1 = editViewState.timeToX (
        te::TimePosition::fromSeconds (proposedStart), row->getWidth());
    const int x2 = editViewState.timeToX (
        te::TimePosition::fromSeconds (proposedEnd), row->getWidth());
    regionDragPreview->setBounds (row->getX() + x1, previewRow->getY(),
                                  jmax (2, x2 - x1), row->getHeight());
    static_cast<StudioRegionDragPreview*> (regionDragPreview.get())->allowed = allowed;
    regionDragPreview->repaint();
}

void ClipComponent::mouseUp (const MouseEvent& event)
{
    // The local pointer remains valid even if the drop changes its parent
    // track; that track's view will rebuild on a later message-loop turn.
    const te::Clip::Ptr heldClip = clip;
    const auto gesture = regionGesture;
    regionGesture = RegionGesture::none;
    regionDragPreview.reset();
    if (! regionDragStarted || gesture == RegionGesture::none
        || regionDragClips.empty() || editViewState.edit.getTransport().isRecording())
    {
        regionDragClips.clear();
        regionDragStarted = false;
        return;
    }

    auto* row = dynamic_cast<TrackComponent*> (getParentComponent());
    if (row == nullptr || row->getWidth() <= 0)
    {
        regionDragClips.clear();
        return;
    }

    auto& edit = editViewState.edit;
    const auto original = heldClip->getPosition();
    const auto secondsDelta = event.getDistanceFromDragStartX()
        * (editViewState.viewX2.get() - editViewState.viewX1.get()).inSeconds()
        / row->getWidth();
    const bool snap = ! event.mods.isAltDown();
    const auto start = original.getStart().inSeconds();
    const auto end = original.getEnd().inSeconds();
    const double minSeconds = jmin (0.025, jmax (0.0001, end - start) * 0.5);

    if (gesture == RegionGesture::trimLeft)
    {
        const auto next = jlimit (0.0, end - minSeconds,
                                  studioRegionSnapSeconds (edit, start + secondsDelta, snap));
        if (std::abs (next - start) > 0.00001)
        {
            edit.getUndoManager().beginNewTransaction ("Trim region start");
            heldClip->setStart (te::TimePosition::fromSeconds (next), true, false);
        }
    }
    else if (gesture == RegionGesture::trimRight)
    {
        const auto next = jmax (start + minSeconds,
                                studioRegionSnapSeconds (edit, end + secondsDelta, snap));
        if (std::abs (next - end) > 0.00001)
        {
            edit.getUndoManager().beginNewTransaction ("Trim region end");
            heldClip->setEnd (te::TimePosition::fromSeconds (next), true);
        }
    }
    else
    {
        auto* dropRow = studioRegionDropRow (*this, event);
        auto* target = dropRow != nullptr
                         ? dynamic_cast<te::ClipTrack*> (dropRow->getTrack()) : nullptr;
        auto* source = heldClip->getClipTrack();
        bool allowed = target != nullptr && source != nullptr
                        && ! target->isFrozen (te::Track::anyFreeze);
        for (const auto& item : regionDragClips)
        {
            if (! allowed || item.first->getClipTrack() == nullptr
                || item.first->getClipTrack()->isFrozen (te::Track::anyFreeze)
                || (target != source && (item.first->getClipTrack() != source
                                          || ! item.first->canBeAddedTo (*target))))
            {
                allowed = false;
                break;
            }
        }

        if (allowed)
        {
            const auto next = event.getDistanceFromDragStartX() == 0 ? start
                : studioRegionSnapSeconds (edit, start + secondsDelta, snap);
            const auto delta = next - start;
            if (std::abs (delta) > 0.00001 || target != source)
            {
                edit.getUndoManager().beginNewTransaction ("Move regions");
                for (const auto& item : regionDragClips)
                {
                    // Native moveTo preserves MIDI/audio state, sample sources,
                    // channel data and plugin parameters.
                    if (target != source)
                        item.first->moveTo (*target);
                    item.first->setStart (te::TimePosition::fromSeconds (
                        jmax (0.0, item.second.getStart().inSeconds() + delta)), false, true);
                }
            }
        }
    }

    regionDragClips.clear();
    regionDragStarted = false;
}
