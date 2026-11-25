// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstrumentImportWindow.h"
#include "StudioUI.h"

namespace studio::instrumentimport
{
using namespace juce;

ImportWindow::Task::~Task()
{
    progress.cancelled.store (true);
    if (worker.joinable()) worker.join();
}

ImportWindow::ImportWindow (File catalogDirectory)
    : DocumentWindow ("Import Instruments", colours::canvas, closeButton), catalog (std::move (catalogDirectory))
{
    setUsingNativeTitleBar (true);
    for (Component* component : std::initializer_list<Component*> { &status, &results, &instruments,
         &choose, &chooseInstrument, &builder, &resume, &locate, &relink, &convertOne,
         &batchConvert, &cancel, &reveal, &partial })
        content.addAndMakeVisible (*component);
    chooseInstrument.setBounds (14, 14, 155, 30);
    choose.setBounds (177, 14, 173, 30);
    builder.setBounds (358, 14, 155, 30);
    resume.setBounds (521, 14, 155, 30);
    locate.setBounds (14, 50, 179, 30);
    relink.setBounds (201, 50, 149, 30);
    reveal.setBounds (358, 50, 190, 30);
    cancel.setBounds (558, 50, 118, 30);
    status.setBounds (14, 86, 662, 27);
    instruments.setBounds (14, 119, 662, 30);
    results.setBounds (14, 157, 662, 292);
    partial.setBounds (14, 459, 290, 30);
    convertOne.setBounds (358, 459, 151, 30);
    batchConvert.setBounds (517, 459, 159, 30);
    content.setSize (690, 503);

    results.setMultiLine (true);
    results.setReadOnly (true);
    results.setScrollbarsShown (true);
    results.setText ("Choose an instrument, library folder, or a folder of recordings.");
    instruments.setTextWhenNothingSelected ("All discovered instruments");
    instruments.onChange = [this] { showSelection(); refreshButtons(); };
    choose.onClick = [this]
    { choosePath ("Import Library Folder", true, [this] (File file) { startScan (file); }); };
    chooseInstrument.onClick = [this]
    { choosePath ("Import Instrument", false, [this] (File file) { startScan (file); }); };
    builder.onClick = [this]
    { choosePath ("Choose Sample Folder", true, [this] (File file) { startScan (file, true); }); };
    resume.onClick = [this] { startScan (lastSource, lastWasBuilder, lastSamples); };
    locate.onClick = [this]
    {
        const auto source = selected() != nullptr ? selected()->source : lastSource;
        if (source.existsAsFile())
            choosePath ("Locate Missing Samples", true, [this, source] (File folder) { startScan (source, false, folder); });
    };
    relink.onClick = [this]
    {
        choosePath ("Choose Studio Sampler Preset", false, [this] (File preset)
        {
            choosePath ("Choose Relocated Sample Folder", true, [this, preset] (File root) { startRelink (preset, root); });
        });
    };
    reveal.onClick = [this] { report.revealToUser(); };
    cancel.onClick = [this] { if (task) task->progress.cancelled.store (true); };
    convertOne.onClick = [this] { startConversion (false); };
    batchConvert.onClick = [this] { startConversion (true); };
    partial.onClick = [this] { refreshButtons(); };
    setContentNonOwned (&content, true);
    centreWithSize (690, 503);
    refreshButtons();
}

ImportWindow::~ImportWindow()
{
    stopTimer();
    task.reset();
    clearContentComponent();
}

void ImportWindow::closeButtonPressed()
{
    if (task) task->progress.cancelled.store (true);
    setVisible (false);
}

void ImportWindow::choosePath (const String& title, bool folder, std::function<void (File)> onChosen)
{
    if (task || chooser) return;
    chooser = std::make_unique<FileChooser> (title, inventory.root);
    chooser->launchAsync (FileBrowserComponent::openMode
        | (folder ? FileBrowserComponent::canSelectDirectories : FileBrowserComponent::canSelectFiles),
        [safe = Component::SafePointer<ImportWindow> (this), onChosen] (const FileChooser& dialog)
        {
            if (safe == nullptr) return;
            const auto file = dialog.getResult();
            MessageManager::callAsync ([safe, file, onChosen]
            {
                if (safe == nullptr) return;
                safe->chooser.reset();
                if (file != File() && safe->isVisible()) onChosen (file);
            });
        });
}

void ImportWindow::startScan (const File& source, bool build, const File& sampleRoot)
{
    if (task || source == File()) return;
    lastSource = source;
    lastSamples = sampleRoot;
    lastWasBuilder = build;
    inventory = {};
    report = File();
    instruments.clear (dontSendNotification);
    results.clear();
    partial.setToggleState (false, dontSendNotification);
    task = std::make_unique<Task>();
    auto* job = task.get();
    const auto reports = catalog.getSiblingFile ("Instrument Inventories");
    const auto cache = catalog.getSiblingFile ("Instrument Index");
    job->worker = std::thread ([job, source, sampleRoot, build, reports, cache]
    {
        try
        {
            job->inventory = build ? proposeSampleFolder (source, job->progress)
                                   : scan (source, job->progress, cache, sampleRoot);
            if (! build) writeIndex (job->inventory, cache);
            if (job->inventory.root != File())
                job->error = saveInventory (job->inventory, reports, job->report).getErrorMessage();
        }
        catch (const std::exception& e) { job->error = String::fromUTF8 (e.what()); }
        job->done.store (true, std::memory_order_release);
    });
    status.setText (build ? "Analyzing sample folder..." : "Scanning library...", dontSendNotification);
    refreshButtons();
    startTimer (100);
}

const Definition* ImportWindow::selected() const
{
    const auto index = instruments.getSelectedId() - 1;
    return isPositiveAndBelow (index, static_cast<int> (inventory.definitions.size()))
        ? &inventory.definitions[static_cast<std::size_t> (index)] : nullptr;
}

void ImportWindow::startConversion (bool batch)
{
    if (task || ! inventory.complete || (! batch && selected() == nullptr)) return;
    const auto selectedRow = instruments.getSelectedId() - 1;
    task = std::make_unique<Task>();
    auto* job = task.get();
    job->inventory = std::move (inventory);
    job->kind = Task::Kind::convert;
    const auto allowPartial = partial.getToggleState();
    const auto eligible = [allowPartial] (const Definition& item) { return canConvert (item.compatibility, allowPartial); };
    job->total = batch ? static_cast<int> (std::count_if (job->inventory.definitions.begin(), job->inventory.definitions.end(), eligible)) : 1;
    job->worker = std::thread ([job, destination = catalog, allowPartial, eligible, batch, selectedRow]
    {
        try
        {
            for (int i = 0; i < static_cast<int> (job->inventory.definitions.size()); ++i)
            {
                if (job->progress.cancelled.load()) break;
                const auto& item = job->inventory.definitions[static_cast<std::size_t> (i)];
                if ((! batch && selectedRow != i) || ! eligible (item)) continue;
                File output;
                auto result = convert (job->inventory, item, destination, allowPartial, job->progress, output);
                if (result.wasOk()) ++job->imported;
                else if (! job->progress.cancelled.load())
                    job->failures.add (item.instrument.name + ": " + result.getErrorMessage());
                ++job->progress.completed;
            }
        }
        catch (const std::exception& e) { job->error = String::fromUTF8 (e.what()); }
        job->done.store (true, std::memory_order_release);
    });
    refreshButtons();
    startTimer (100);
}

void ImportWindow::startRelink (const File& preset, const File& newRoot)
{
    if (task) return;
    task = std::make_unique<Task>();
    auto* job = task.get();
    job->kind = Task::Kind::relink;
    job->total = 1;
    job->worker = std::thread ([job, preset, newRoot, destination = catalog]
    {
        try
        {
            File output;
            const auto result = relinkPreset (preset, {}, newRoot, destination, job->progress, output);
            if (result.wasOk()) job->imported = 1;
            else job->error = result.getErrorMessage();
        }
        catch (const std::exception& e) { job->error = String::fromUTF8 (e.what()); }
        job->done.store (true, std::memory_order_release);
    });
    refreshButtons();
    startTimer (100);
}

void ImportWindow::showInventory()
{
    const auto previous = instruments.getSelectedId();
    instruments.clear (dontSendNotification);
    int id = 1;
    for (const auto& item : inventory.definitions)
        instruments.addItem (item.instrument.name + " | " + statusName (item.compatibility), id++);
    instruments.setSelectedId (previous == 0 && id > 1 ? 1 : previous, dontSendNotification);
    showSelection();
}

void ImportWindow::showSelection()
{
    String text = inventory.issues.joinIntoString ("\n");
    if (const auto* item = selected())
    {
        text << "\n" << item->instrument.name << " | " << statusName (item->compatibility) << "\n"
             << item->source.getRelativePathFrom (inventory.root) << "\n";
        for (const auto& issue : item->issues) text << "\n" << issue;
        text << "\n";
        for (const auto& entry : item->instrument.zones)
        {
            const auto& z = entry.mapping;
            text << "\n" << entry.file.getFileName() << " | root " << z.root << ", keys " << z.low << "-" << z.high
                 << ", velocity " << z.soft << "-" << z.hard;
        }
    }
    if (text.trim().isEmpty()) text = "No recognized instrument definitions found.";
    results.setText (text.trimStart(), false);
    results.setCaretPosition (0);
}

void ImportWindow::timerCallback()
{
    if (! task) return;
    if (! task->done.load (std::memory_order_acquire))
    {
        const auto completed = String (task->progress.completed.load());
        status.setText (task->progress.cancelled.load() ? "Cancelling..."
            : task->kind == Task::Kind::scan ? "Scanned " + completed + " entries"
            : task->kind == Task::Kind::relink ? "Checking relocated samples..."
            : "Processed " + completed + " of " + String (task->total) + " instruments", dontSendNotification);
        return;
    }
    stopTimer();
    auto finished = std::move (task);
    const auto cancelled = finished->progress.cancelled.load();
    if (finished->kind != Task::Kind::relink) inventory = std::move (finished->inventory);
    if (finished->kind == Task::Kind::scan)
    {
        report = finished->report;
        showInventory();
        status.setText (cancelled ? "Scan cancelled. Rescan / Resume to continue."
            : String (inventory.definitions.size()) + " instruments, " + String (inventory.samples.size())
                + " sample files, " + String (inventory.reused) + " reused from index", dontSendNotification);
    }
    else
    {
        showInventory();
        const auto summary = String (finished->imported) + " ready in Imported Libraries, " + String (finished->failures.size()) + " failed.";
        status.setText (cancelled ? "Cancelled. " + summary : summary, dontSendNotification);
        if (! finished->failures.isEmpty())
            results.setText (finished->failures.joinIntoString ("\n") + "\n\n" + results.getText(), false);
        if (finished->imported && onCatalogChanged) onCatalogChanged();
    }
    if (finished->error.isNotEmpty()) results.setText (finished->error + "\n\n" + results.getText(), false);
    refreshButtons();
}

void ImportWindow::refreshButtons()
{
    for (auto* button : { &choose, &chooseInstrument, &builder, &relink }) button->setEnabled (! task);
    resume.setEnabled (! task && lastSource != File());
    instruments.setEnabled (! task);
    const auto* item = selected();
    locate.setEnabled (! task && item != nullptr && item->source.existsAsFile());
    cancel.setEnabled (task != nullptr);
    reveal.setEnabled (! task && report != File() && report.existsAsFile());
    partial.setEnabled (! task && inventory.complete && std::any_of (inventory.definitions.begin(), inventory.definitions.end(),
        [] (const auto& definition) { return definition.compatibility == Compatibility::partial; }));
    const auto allowPartial = partial.getToggleState();
    convertOne.setEnabled (! task && inventory.complete && item != nullptr && canConvert (item->compatibility, allowPartial));
    batchConvert.setEnabled (! task && inventory.complete && std::any_of (inventory.definitions.begin(), inventory.definitions.end(),
        [allowPartial] (const auto& definition) { return canConvert (definition.compatibility, allowPartial); }));
}
}
