// Substituted for the pinned example's TrackComponent::mouseDown.
// Child clips keep their existing move and trim gestures.
void TrackComponent::mouseDown (const MouseEvent& event)
{
    lasso.reset();
    if (event.mods.isLeftButtonDown() && ! editViewState.edit.getTransport().isRecording())
        return;
    editViewState.selectionManager.selectOnly (track.get());
}

void TrackComponent::mouseDrag (const MouseEvent& event)
{
    auto* editor = getParentComponent();
    if (editor == nullptr || ! event.mods.isLeftButtonDown()
        || editViewState.edit.getTransport().isRecording() || ! event.mouseWasDraggedSinceMouseDown())
        return;

    if (lasso == nullptr)
    {
        lasso = std::make_unique<StudioRegionDragPreview> (studio::colours::focus);
        lasso->setInterceptsMouseClicks (false, false);
        lasso->setAlwaysOnTop (true);
        editor->addAndMakeVisible (*lasso);
    }
    const auto first = editor->getLocalPoint (this, event.getMouseDownPosition());
    const auto current = editor->getLocalPoint (this, event.getPosition());
    const Rectangle<int> lane (getX(), 0, getWidth(), editor->getHeight());
    lasso->setBounds (Rectangle<int> (first, current).expanded (1).getIntersection (lane));
}

void TrackComponent::mouseUp (const MouseEvent& event)
{
    auto& selection = editViewState.selectionManager;
    if (lasso == nullptr)
    {
        if (event.mods.isLeftButtonDown())
            selection.selectOnly (track.get());
        return;
    }

    const auto bounds = lasso->getBounds();
    lasso.reset();
    auto* editor = getParentComponent();
    if (editor == nullptr || editViewState.edit.getTransport().isRecording())
        return;
    if (! event.mods.isShiftDown())
        selection.deselectAll();
    for (auto* child : editor->getChildren())
        if (auto* row = dynamic_cast<TrackComponent*> (child); row != nullptr && bounds.intersects (row->getBounds()))
            for (auto* clip : row->clips)
                if (bounds.intersects (clip->getBounds() + row->getPosition()))
                    selection.addToSelection (&clip->getClip());
}
