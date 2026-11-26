// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioSampler.h"
#include "StudioUI.h"
#include "StudioInstrumentHelp.h"
#include "SamplerNoteText.h"
#include <thread>
#include <cstdio>

namespace studio::sampler
{
class GuidedBuilder final : public juce::Component, private juce::Timer
{
public:
    using Apply=std::function<juce::Result(Instrument)>;
    explicit GuidedBuilder(Apply callback):apply(std::move(callback))
    {
        using namespace juce;
        setLookAndFeel(&look);
        for(Component* c:std::initializer_list<Component*>{&title,&status,&prepare,&accept,&cancel,&helpButton,&scroll})addAndMakeVisible(*c);
        scroll.setViewedComponent(&form,false);scroll.setScrollBarsShown(true,false);
        scroll.setTitle("Instrument builder form");
        for(Component* c:std::initializer_list<Component*>{&intro,&nameLabel,&name,&layoutLabel,&layout,&rootLabel,&root,&playbackLabel,&playback,&spread,&alternates,&files,&folder,&selection,&report})form.addAndMakeVisible(*c);
        title.setText("Create an instrument",dontSendNotification);title.setFont(FontOptions(20));
        helpButton.setTooltip("Explain naming, sample mapping, velocity layers and playback choices.");
        helpButton.onClick=[this]{if(helpWindow)helpWindow->toFront(true);else helpWindow=studio::help::show(*this,studio::help::Topic::builder);};
        files.setTooltip("Select one or more audio recordings. Original files are never modified.");
        folder.setTooltip("Search this recordings folder and its subfolders.");
        prepare.setTooltip("Prepare and review the exact mapping without changing your instrument.");
        accept.setTooltip("Replace only this sampler instrument with the reviewed mapping.");
        name.setTitle("Instrument name");layout.setTitle("Instrument type");root.setTitle("Original pitch or first drum key");playback.setTitle("Playback behavior");report.setTitle("Mapping review");
        intro.setText("Choose recordings, preview their mapping, then create your instrument.",dontSendNotification);
        nameLabel.setText("Instrument name",dontSendNotification);name.setText("New instrument",false);name.setInputRestrictions(120);
        layoutLabel.setText("Instrument type",dontSendNotification);
        layout.addItem("Pitched instrument",1);layout.addItem("Drum / percussion kit",2);layout.setSelectedId(1,dontSendNotification);
        rootLabel.setText("Single sample root",dontSendNotification);
        root.setSliderStyle(Slider::LinearHorizontal);root.setTextBoxStyle(Slider::TextBoxRight,false,100,24);
        root.setRange(0,127,1);root.setValue(60,dontSendNotification);
        useMusicalNotes(root);
        root.setTooltip("Used when a single recording has no root metadata, or as the first key of a percussion kit. C4 = MIDI 60.");
        playbackLabel.setText("How notes play",dontSendNotification);
        playback.addItem("Held notes",1);playback.addItem("Play recording to end",2);playback.addItem("Use existing embedded loops",3);
        playback.setSelectedId(1,dontSendNotification);
        spread.setToggleState(true,dontSendNotification);
        spread.setTooltip("Extend each root to the midpoint between neighboring recordings. Off = original keys only.");
        alternates.setTooltip("Cycle recordings with identical root and velocity ranges instead of layering them.");
        selection.setText("No recordings selected",dontSendNotification);
        report.setMultiLine(true);report.setReadOnly(true);report.setScrollbarsShown(true);
        report.setText("1. Select audio files or a folder.\n2. Name the instrument and choose its playback behavior.\n3. Preview and check the proposed mapping.\n4. Create instrument; then play or save it in Studio Sampler.",false);
        status.setText("The existing instrument will not change until you choose Create.",dontSendNotification);
        files.onClick=[this]{choose(false);};folder.onClick=[this]{choose(true);};
        layout.onChange=[this]
        {
            const bool drums=layout.getSelectedId()==2;
            if(drums){root.setValue(36,dontSendNotification);playback.setSelectedId(2,dontSendNotification);alternates.setToggleState(false,dontSendNotification);}
            else{root.setValue(60,dontSendNotification);playback.setSelectedId(1,dontSendNotification);}
            invalidate();updateControls();
        };
        name.onTextChange=[this]
        {
            if (!ready || reviewedForm != formSignature()) invalidate();
        };
        root.onValueChange=[this]{invalidate();};playback.onChange=[this]{invalidate();};
        spread.onClick=[this]{invalidate();};alternates.onClick=[this]{invalidate();};
        prepare.onClick=[this]{start();};
        accept.onClick=[this]
        {
            if(!ready||task||!apply)return;
            // TextEditor change notifications can be queued. Validate the live
            // form synchronously before committing a reviewed mapping.
            if (reviewedForm != formSignature()) { invalidate(); return; }
            auto result=apply(proposal);
            if(result.failed()){status.setText(result.getErrorMessage(),dontSendNotification);return;}
            if(auto* d=findParentComponentOfClass<DialogWindow>())d->exitModalState(1);
        };
        cancel.onClick=[this]
        {
            if(task){task->cancel.store(true);return;}
            if(auto* d=findParentComponentOfClass<DialogWindow>())d->exitModalState(0);
        };
        updateControls();setSize(660,510);startTimerHz(10);
    }
#if STUDIO_SPATIAL_TESTS
    bool review(const juce::File& recording,const juce::File& output)
    {
        using namespace juce;
        const auto reviewFailure=[](const char* detail)
        {
            std::fprintf(stderr,"Guided builder review failed: %s\n",detail);
            return false;
        };
        const ScopedValueSetter<AppearanceMetrics> restore(appearance,appearance);
        for(int text:{100,130})
        {
            appearance.set(100,text);setSize(660,510);resized();
            if(scroll.getHeight()<=0||!getLocalBounds().contains(prepare.getBounds())||
               !getLocalBounds().contains(accept.getBounds())||!getLocalBounds().contains(cancel.getBounds())||
               prepare.getBounds().intersects(accept.getBounds())||accept.getBounds().intersects(cancel.getBounds())||
               !form.getLocalBounds().contains(report.getBounds())||report.getHeight()<=0)
                return reviewFailure("form or footer bounds");
            if(output!=File())
            {
                FileOutputStream stream(output.getChildFile("m32-builder-text"+String(text)+".png"));
                if(!stream.openedOk()||!stream.setPosition(0)||stream.truncate().failed()||
                   !PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),stream))return false;
            }
        }
        appearance.set(100,100);resized();
        paths={recording};name.setText("M32 guided review",false);invalidate();prepare.onClick();
        const auto deadline=Time::getMillisecondCounter()+10000;
        while(task&&Time::getMillisecondCounter()<deadline)
        {timerCallback();MessageManager::getInstance()->runDispatchLoopUntil(10);}
        if(task||!ready||!accept.isEnabled()||proposal.zones.size()!=1)
            return reviewFailure("initial mapping did not produce one zone");
        name.setText("M32 revised review",true);
        // Exercise the real Create callback before the queued edit notification.
        // It must reject the obsolete proposal even in that short window.
        accept.onClick();
        MessageManager::getInstance()->runDispatchLoopUntil(20);
        if(ready||accept.isEnabled())return reviewFailure("changed form did not invalidate proposal");
        prepare.onClick();
        const auto nextDeadline=Time::getMillisecondCounter()+10000;
        while(task&&Time::getMillisecondCounter()<nextDeadline)
        {timerCallback();MessageManager::getInstance()->runDispatchLoopUntil(10);}
        if(task||!ready||proposal.name!="M32 revised review")
            return reviewFailure("revised mapping did not become ready");
        accept.onClick();return true;
    }
#endif
    ~GuidedBuilder() override {stopTimer();if(helpWindow)helpWindow->exitModalState(0);task.reset();scroll.setViewedComponent(nullptr,false);setLookAndFeel(nullptr);}
    void resized() override
    {
        using namespace juce;
        const auto& m=appearance;auto area=getLocalBounds().reduced(m.px(12));
        const int h=m.control(30,12),gap=m.px(4);
        auto heading=area.removeFromTop(h);helpButton.setBounds(heading.removeFromRight(jmin(m.span(75),heading.getWidth()/4)));title.setBounds(heading);
        auto row=area.removeFromBottom(h);
        cancel.setBounds(row.removeFromRight(jmin(m.span(100),row.getWidth()/3)).reduced(2));
        accept.setBounds(row.removeFromRight(jmin(m.span(175),row.getWidth()/2)).reduced(2));
        prepare.setBounds(row.removeFromRight(jmin(m.span(175),row.getWidth())).reduced(2));
        status.setBounds(area.removeFromBottom(h*2));
        area.removeFromBottom(gap);scroll.setBounds(area);
        const int width=jmax(1,scroll.getMaximumVisibleWidth()-scroll.getScrollBarThickness());
        const bool narrow=width<m.span(520);
        const int formHeight=(narrow?14:9)*h+gap+m.px(120);
        form.setSize(width,jmax(formHeight,scroll.getHeight()));
        auto content=form.getLocalBounds();
        intro.setBounds(content.removeFromTop(h*2));
        auto line=[&](Label& label,Component& control)
        {
            auto line=content.removeFromTop(narrow?2*h:h);
            if(narrow)label.setBounds(line.removeFromTop(h));
            else label.setBounds(line.removeFromLeft(jmin(m.span(160),line.getWidth()/3)));
            control.setBounds(line.reduced(2));
        };
        line(nameLabel,name);line(layoutLabel,layout);line(rootLabel,root);line(playbackLabel,playback);
        row=content.removeFromTop(narrow?h*2:h);
        if(narrow){spread.setBounds(row.removeFromTop(h));alternates.setBounds(row);}
        else{spread.setBounds(row.removeFromLeft(row.getWidth()/2));alternates.setBounds(row);}
        row=content.removeFromTop(h);
        files.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));
        folder.setBounds(row.reduced(2));
        selection.setBounds(content.removeFromTop(h));
        content.removeFromTop(gap);report.setBounds(content.reduced(2));
    }
private:
    struct Task
    {
        Instrument proposal;juce::String report,error;
        std::atomic<bool> cancel{false},done{false};std::atomic<int> progress{0};
        std::thread worker;
        ~Task(){cancel.store(true);if(worker.joinable())worker.join();}
    };
    StudioLookAndFeel look;Apply apply;
    juce::Component form;
    juce::Viewport scroll;
    juce::Label title,intro,nameLabel,layoutLabel,rootLabel,playbackLabel,selection,status;
    juce::TextEditor name,report;
    juce::ComboBox layout,playback;
    juce::Slider root;
    juce::ToggleButton spread{"Fill keyboard ranges"},alternates{"Cycle alternate takes"};
    juce::TextButton files{"Choose files"},folder{"Choose folder"},prepare{"Preview mapping"},accept{"Create instrument"},cancel{"Cancel"},helpButton{"Help"};
    juce::Component::SafePointer<juce::DialogWindow> helpWindow;
    juce::Array<juce::File> paths;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Task> task;
    Instrument proposal;bool ready=false;
    juce::String reviewedForm;
    juce::String formSignature() const
    {
        // The fields are fixed while the worker runs. The signature also
        // covers queued/programmatic changes not yet delivered to listeners.
        juce::String signature;
        signature << name.getText().length() << ":" << name.getText()
                  << "|" << layout.getSelectedId() << "|" << juce::roundToInt(root.getValue())
                  << "|" << playback.getSelectedId() << "|" << int(spread.getToggleState())
                  << "|" << int(alternates.getToggleState());
        for (const auto& path : paths)
            signature << "|" << path.getFullPathName().length() << ":" << path.getFullPathName();
        return signature;
    }
    void invalidate()
    {
        if(task)return;
        ready=false;proposal={};reviewedForm.clear();report.setText("Settings changed. Choose Preview mapping to generate an updated proposal.",false);
        status.setText("Preview the mapping before creating the instrument.",juce::dontSendNotification);updateControls();
    }
    void updateControls()
    {
        using namespace juce;
        const bool drums=layout.getSelectedId()==2;
        const bool idle=!task;
        for(Component* c:std::initializer_list<Component*>{&name,&layout,&root,&playback,&files,&folder})c->setEnabled(idle);
        spread.setEnabled(idle&&!drums);alternates.setEnabled(idle&&!drums);
        rootLabel.setText(drums?"First drum key":"Single sample root",dontSendNotification);
        prepare.setEnabled(idle&&!paths.isEmpty()&&name.getText().trim().isNotEmpty());
        accept.setEnabled(idle&&ready);
        cancel.setButtonText(idle?"Cancel":"Cancel scan");
    }
    void choose(bool directory)
    {
        using namespace juce;
        chooser=std::make_unique<FileChooser>(directory?"Choose a recordings folder":"Choose recordings",File::getSpecialLocation(File::userMusicDirectory),
            directory?"*":"*.wav;*.wave;*.aif;*.aiff;*.aifc;*.caf;*.flac");
        const auto safe=Component::SafePointer<GuidedBuilder>(this);
        chooser->launchAsync(FileBrowserComponent::openMode|(directory?FileBrowserComponent::canSelectDirectories:FileBrowserComponent::canSelectFiles|FileBrowserComponent::canSelectMultipleItems),
            [safe](const FileChooser& dialog)
        {
            auto chosen=dialog.getResults();
            MessageManager::callAsync([safe,chosen]
            {
                if(!safe)return;safe->chooser.reset();
                if(chosen.isEmpty()||safe->task)return;
                safe->paths=chosen;
                safe->selection.setText(chosen.size()==1?chosen.getFirst().getFileName():String(chosen.size())+" files selected",dontSendNotification);
                safe->invalidate();
            });
        });
    }
    void start()
    {
        using namespace juce;
        if(task||paths.isEmpty())return;
        GuidedBuild options;options.name=name.getText().trim();
        options.layout=layout.getSelectedId()==2?GuidedBuild::Layout::percussion:GuidedBuild::Layout::pitched;
        options.playback=static_cast<GuidedBuild::Playback>(jlimit(0,2,playback.getSelectedId()-1));
        options.firstNote=roundToInt(root.getValue());options.spread=spread.getToggleState();
        options.roundRobin=options.layout==GuidedBuild::Layout::pitched&&alternates.getToggleState();
        ready=false;proposal={};reviewedForm=formSignature();task=std::make_unique<Task>();auto* t=task.get();
        t->worker=std::thread([t,options,chosen=paths]
        {
            try{auto r=Plugin::prepareGuided(options,chosen,t->proposal,t->report,&t->cancel,&t->progress);t->error=r.getErrorMessage();}
            catch(const std::exception& e){t->error=e.what();}
            t->done.store(true,std::memory_order_release);
        });
        updateControls();
    }
    void timerCallback() override
    {
        using namespace juce;
        if(!task)return;
        if(!task->done.load(std::memory_order_acquire))
        {status.setText("Preparing recordings: "+String(task->progress.load())+"%",dontSendNotification);return;}
        auto t=std::move(task);
        if(t->cancel.load()){status.setText("Cancelled. Existing instrument unchanged.",dontSendNotification);}
        else if(t->error.isNotEmpty()){report.setText(t->error,false);status.setText("Review the issue, change settings and preview again.",dontSendNotification);}
        else if(reviewedForm!=formSignature()){invalidate();return;}
        else{proposal=std::move(t->proposal);ready=true;report.setText(t->report,false);status.setText("Review the mapping. Create replaces only the current sampler instrument.",dontSendNotification);}
        updateControls();
    }
};
}
