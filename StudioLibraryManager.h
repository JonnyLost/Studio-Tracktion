// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioLibrary.h"
#include "StudioUI.h"
#include <thread>

namespace studio::library
{
class Manager final : public Component, private ListBoxModel, private Timer
{
public:
    using Load=std::function<void(sampler::Instrument)>;
    explicit Manager(Load callback):onLoad(std::move(callback))
    {
        setLookAndFeel(&look);
        for(Component* c:std::initializer_list<Component*>{&roots,&addRoot,&removeRoot,&rescan,&search,&format,&rows,&details,&art,&assignArt,&repairSelected,&convertSelected,&loadSelected,&cancel,&status})
            addAndMakeVisible(*c);
        rows.setModel(this);rows.setMultipleSelectionEnabled(true);rows.setRowHeight(28);
        search.setTextToShowWhenEmpty("Search name, format or library path",colours::secondary);
        search.onTextChange=[this]{filter();};
        format.onChange=[this]{filter();};
        details.setMultiLine(true);details.setReadOnly(true);details.setScrollbarsShown(true);
        art.setImagePlacement(RectanglePlacement::centred|RectanglePlacement::onlyReduceInSize);
        addRoot.onClick=[this]{chooseRoot();};
        removeRoot.onClick=[this]
        {
            const int i=roots.getSelectedItemIndex();
            if(i<0||i>=catalog.roots.size())return;
            catalog.roots.remove(i);
            for(auto& e:catalog.entries)
            {
                bool within=false;for(const auto& path:catalog.roots)if(e.source.isAChildOf(File(path))){within=true;break;}
                if(!within)e.available=false;
            }
            persist();refreshRoots();filter();
            status.setText("Folder removed from scans; existing catalog entries and converted media retained.",dontSendNotification);
        };
        rescan.onClick=[this]{startScan();};
        convertSelected.onClick=[this]{startBatch();};
        loadSelected.onClick=[this]{startLoad();};
        cancel.onClick=[this]{if(task)task->cancel.store(true);};
        assignArt.onClick=[this]{chooseArt();};
        repairSelected.onClick=[this]{chooseRepairRoot();};
        if(auto r=read(index(),catalog);r.failed())
        {
            validCatalog=false;status.setText(r.getErrorMessage()+" Move the damaged catalog aside before reopening.",dontSendNotification);
        }
        refreshRoots();rebuildFormats();filter();
        if(validCatalog)status.setText("Choose library folders and scan. Convert selected instruments before loading.",dontSendNotification);
        setSize(920,600);startTimerHz(8);
    }
    ~Manager() override
    {
        stopTimer();task.reset();rows.setModel(nullptr);setLookAndFeel(nullptr);
    }
    void resized() override
    {
        auto a=getLocalBounds().reduced(12);
        auto top=a.removeFromTop(34);
        addRoot.setBounds(top.removeFromRight(100).reduced(2));removeRoot.setBounds(top.removeFromRight(100).reduced(2));rescan.setBounds(top.removeFromRight(90).reduced(2));roots.setBounds(top.reduced(2));
        top=a.removeFromTop(34);format.setBounds(top.removeFromRight(145).reduced(2));search.setBounds(top.reduced(2));
        auto bottom=a.removeFromBottom(64);
        auto buttons=bottom.removeFromTop(34);
        cancel.setBounds(buttons.removeFromRight(95).reduced(2));loadSelected.setBounds(buttons.removeFromRight(160).reduced(2));convertSelected.setBounds(buttons.removeFromRight(160).reduced(2));
        assignArt.setBounds(buttons.removeFromLeft(140).reduced(2));repairSelected.setBounds(buttons.removeFromLeft(155).reduced(2));status.setBounds(bottom.reduced(2));
        auto right=a.removeFromRight(jlimit(220,330,a.getWidth()/3));
        art.setBounds(right.removeFromTop(jmin(160,right.getHeight()/3)).reduced(4));details.setBounds(right.reduced(4));rows.setBounds(a.reduced(2));
    }
private:
    struct Task
    {
        enum Kind{scanTask,batchTask,loadTask} kind=scanTask;
        Catalog proposal;sampler::Instrument instrument;
        std::atomic<bool> cancel{false},done{false};std::atomic<int> progress{0};
        String message,error;std::thread worker;
        ~Task(){cancel.store(true);if(worker.joinable())worker.join();}
    };
    StudioLookAndFeel look;Load onLoad;Catalog catalog;bool validCatalog=true;
    std::vector<std::size_t> filtered;
    ComboBox roots,format;
    TextEditor search,details;
    ImageComponent art;
    ListBox rows;
    TextButton addRoot{"Add folder"},removeRoot{"Remove folder"},rescan{"Rescan"},convertSelected{"Convert selected"},loadSelected{"Load as new track"},cancel{"Cancel"},assignArt{"Assign artwork"},repairSelected{"Repair sample paths"};
    Label status;
    std::unique_ptr<FileChooser> chooser;
    std::unique_ptr<Task> task;
    static File index(){return directory().getChildFile("Catalog.xml");}
    void persist()
    {
        if(auto r=save(catalog,index());r.failed())status.setText(r.getErrorMessage(),dontSendNotification);
    }
    void refreshRoots()
    {
        roots.clear(dontSendNotification);
        for(int i=0;i<catalog.roots.size();++i)roots.addItem(catalog.roots[i],i+1);
        roots.setSelectedItemIndex(0,dontSendNotification);
    }
    void rebuildFormats()
    {
        auto previous=format.getText();StringArray values;
        for(const auto& e:catalog.entries)values.addIfNotAlreadyThere(e.format);
        values.sort(true);format.clear(dontSendNotification);format.addItem("All formats",1);
        for(int i=0;i<values.size();++i)format.addItem(values[i],i+2);
        const int i=values.indexOf(previous);format.setSelectedId(i<0?1:i+2,dontSendNotification);
    }
    void filter()
    {
        const auto q=search.getText().trim();const auto f=format.getText();
        filtered.clear();
        for(std::size_t i=0;i<catalog.entries.size();++i)
        {
            const auto& e=catalog.entries[i];
            if(format.getSelectedId()>1&&e.format!=f)continue;
            if(q.isNotEmpty()&&!(e.name+" "+e.format+" "+e.source.getFullPathName()).containsIgnoreCase(q))continue;
            filtered.push_back(i);
        }
        rows.deselectAllRows();rows.updateContent();rows.repaint();refreshSelection();
    }
    int getNumRows() override{return static_cast<int>(filtered.size());}
    void paintListBoxItem(int row,Graphics& g,int width,int height,bool selected) override
    {
        if(!isPositiveAndBelow(row,getNumRows()))return;
        const auto& e=catalog.entries[filtered[static_cast<std::size_t>(row)]];
        if(selected)g.fillAll(colours::control);
        g.setColour(colours::text);g.setFont(uiFont(13));
        const auto state=e.converted!=File()?"Native":e.available?"Source":"Offline";
        g.drawFittedText(e.name,8,0,jmax(0,width-150),height,Justification::centredLeft,1);
        g.setColour(colours::secondary);g.setFont(uiFont(11));
        g.drawFittedText(e.format.toUpperCase()+"  |  "+state,jmax(8,width-140),0,132,height,Justification::centredRight,1);
    }
    void selectedRowsChanged(int) override{refreshSelection();}
    std::vector<std::size_t> selectedIndices() const
    {
        std::vector<std::size_t> result;
        const auto selected=rows.getSelectedRows();
        for(int i=0;i<selected.size();++i)
            if(isPositiveAndBelow(selected[i],getNumRowsConst()))result.push_back(filtered[static_cast<std::size_t>(selected[i])]);
        return result;
    }
    int getNumRowsConst() const{return static_cast<int>(filtered.size());}
    void refreshSelection()
    {
        auto selected=selectedIndices();
        addRoot.setEnabled(validCatalog&&!task);removeRoot.setEnabled(validCatalog&&!task&&!catalog.roots.isEmpty());
        rescan.setEnabled(validCatalog&&!task&&!catalog.roots.isEmpty());cancel.setEnabled(task!=nullptr);
        convertSelected.setEnabled(validCatalog&&!task&&!selected.empty()&&selected.size()<=128);
        assignArt.setEnabled(validCatalog&&!task&&selected.size()==1);
        repairSelected.setEnabled(validCatalog&&!task&&selected.size()==1&&catalog.entries[selected.front()].converted!=File());
        loadSelected.setEnabled(!task&&selected.size()==1&&onLoad&&catalog.entries[selected.front()].converted!=File());
        art.setImage(Image());
        if(selected.size()!=1)
        {details.setText(String(static_cast<int>(selected.size()))+" selected / "+String(getNumRowsConst())+" shown. Select up to 128 for batch conversion.",false);return;}
        const auto& e=catalog.entries[selected.front()];
        details.setText(e.name+"\n"+e.source.getFullPathName()+"\n"+(e.converted!=File()?"Native: "+e.converted.getFullPathName()+"\n":String())+"\n"+e.report,false);
        if(e.artwork.existsAsFile()&&e.artwork.getSize()<=8*1024*1024)art.setImage(ImageFileFormat::loadFrom(e.artwork));
    }
    void chooseRoot()
    {
        chooser=std::make_unique<FileChooser>("Choose an instrument library folder",File::getSpecialLocation(File::userMusicDirectory),"*");
        const auto safe=Component::SafePointer<Manager>(this);
        chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectDirectories,[safe](const FileChooser& c)
        {
            const auto root=c.getResult();
            MessageManager::callAsync([safe,root]
            {
                if(!safe)return;safe->chooser.reset();
                if(!root.isDirectory()||safe->task)return;
                if(safe->catalog.roots.size()>=64){safe->status.setText("64 folder limit reached.",dontSendNotification);return;}
                if(root==File::getSpecialLocation(File::userHomeDirectory)||root.getParentDirectory()==root)
                {safe->status.setText("Choose a specific library folder.",dontSendNotification);return;}
                safe->catalog.roots.addIfNotAlreadyThere(root.getFullPathName());safe->persist();safe->refreshRoots();safe->startScan();
            });
        });
    }
    void chooseArt()
    {
        const auto selected=selectedIndices();if(selected.size()!=1)return;
        const auto key=catalog.entries[selected.front()].key();
        chooser=std::make_unique<FileChooser>("Choose instrument artwork",File(),"*.png;*.jpg;*.jpeg");
        const auto safe=Component::SafePointer<Manager>(this);
        chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles,[safe,key](const FileChooser& c)
        {
            const auto image=c.getResult();
            MessageManager::callAsync([safe,key,image]
            {
                if(!safe)return;safe->chooser.reset();
                if(safe->task||!image.existsAsFile()||image.getSize()>8*1024*1024)return;
                for(auto& e:safe->catalog.entries)if(e.key()==key){e.artwork=image;break;}
                safe->persist();safe->refreshSelection();
            });
        });
    }
    void chooseRepairRoot()
    {
        auto chosen=selectedIndices();if(task||chosen.size()!=1)return;
        const auto key=catalog.entries[chosen.front()].key();
        chooser=std::make_unique<FileChooser>("Choose relocated recordings folder (creates a new preset copy)",File::getSpecialLocation(File::userMusicDirectory),"*");
        const auto safe=Component::SafePointer<Manager>(this);
        chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectDirectories,[safe,key](const FileChooser& c)
        {
            const auto folder=c.getResult();
            MessageManager::callAsync([safe,key,folder]
            {
                if(!safe)return;safe->chooser.reset();
                if(safe->task||!folder.isDirectory())return;
                auto it=std::find_if(safe->catalog.entries.begin(),safe->catalog.entries.end(),[&](const auto& e){return e.key()==key;});
                if(it==safe->catalog.entries.end())return;
                const auto index=static_cast<std::size_t>(it-safe->catalog.entries.begin());
                safe->task=std::make_unique<Task>();auto* t=safe->task.get();t->kind=Task::batchTask;t->proposal=safe->catalog;
                t->worker=std::thread([t,index,folder]
                {
                    try
                    {
                        auto r=repair(t->proposal.entries[index],directory(),folder,t->cancel);
                        t->error=r.getErrorMessage();t->message="Repaired preset copy created. Review the new paths before loading.";
                    }
                    catch(const std::exception& e){t->error=e.what();}
                    t->done.store(true,std::memory_order_release);
                });
                safe->refreshSelection();
            });
        });
    }
    void startScan()
    {
        if(task||!validCatalog)return;
        task=std::make_unique<Task>();auto* t=task.get();t->proposal=catalog;
        t->worker=std::thread([t]
        {
            try{auto r=scan(t->proposal,t->cancel,t->progress,t->message);t->error=r.getErrorMessage();}
            catch(const std::exception& e){t->error=e.what();}
            t->done.store(true,std::memory_order_release);
        });
        refreshSelection();
    }
    void startBatch()
    {
        auto chosen=selectedIndices();if(task||chosen.empty()||chosen.size()>128)return;
        task=std::make_unique<Task>();auto* t=task.get();t->kind=Task::batchTask;t->proposal=catalog;
        t->worker=std::thread([t,chosen=std::move(chosen)]
        {
            int ok=0,failed=0;
            for(auto i:chosen)
            {
                if(t->cancel.load())break;
                auto& e=t->proposal.entries[i];
                try
                {
                    auto r=convert(e,directory(),t->cancel);
                    if(r.wasOk())++ok;else{++failed;e.report=r.getErrorMessage();}
                }
                catch(const std::exception& ex){++failed;e.report=ex.what();}
                ++t->progress;
            }
            t->message=String(ok)+" converted or already native; "+String(failed)+" failed. "+(t->cancel.load()?"Cancelled; completed presets retained.":"Review each conversion report.");
            t->done.store(true,std::memory_order_release);
        });
        refreshSelection();
    }
    void startLoad()
    {
        auto chosen=selectedIndices();if(task||chosen.size()!=1||!onLoad)return;
        const auto file=catalog.entries[chosen.front()].converted;
        if(!file.existsAsFile()){status.setText("Native preset missing; convert again.",dontSendNotification);return;}
        task=std::make_unique<Task>();auto* t=task.get();t->kind=Task::loadTask;
        t->worker=std::thread([t,file]
        {
            try
            {
                auto r=sampler::Plugin::preparePreset(file,{},t->instrument,&t->cancel);
                if(r.failed())t->error=r.getErrorMessage();
                else for(const auto& z:t->instrument.zones)
                    if(!z.mapping.sample||z.problem.isNotEmpty()){t->error="Native preset has missing recordings. Repair it in Studio Sampler.";break;}
                if(t->instrument.zones.empty()&&t->error.isEmpty())t->error="Native preset has no playable regions.";
            }
            catch(const std::exception& e){t->error=e.what();}
            t->done.store(true,std::memory_order_release);
        });
        refreshSelection();
    }
    void timerCallback() override
    {
        if(!task)return;
        if(!task->done.load(std::memory_order_acquire))
        {
            status.setText((task->kind==Task::scanTask?"Scanning files: ":task->kind==Task::batchTask?"Converting selected: ":"Preparing native instrument: ")+String(task->progress.load()),dontSendNotification);return;
        }
        auto t=std::move(task);
        if(t->error.isNotEmpty())status.setText(t->error,dontSendNotification);
        else if(t->kind==Task::loadTask)
        {
            if(!t->cancel.load()&&onLoad)
            {
                auto callback=onLoad;auto instrument=std::move(t->instrument);
                if(auto* dialog=findParentComponentOfClass<DialogWindow>())dialog->exitModalState(0);
                callback(std::move(instrument));return;
            }
        }
        else
        {
            catalog=std::move(t->proposal);rebuildFormats();filter();status.setText(t->message,dontSendNotification);persist();
        }
        refreshSelection();
    }
};
}
