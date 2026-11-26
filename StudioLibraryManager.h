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
    using Load=std::function<void(sampler::Instrument,Presentation)>;
    explicit Manager(Load callback):onLoad(std::move(callback))
    {
        setLookAndFeel(&look);
        for(Component* c:std::initializer_list<Component*>{&roots,&addRoot,&removeRoot,&rescan,&search,&format,&family,&favorites,&manage,&familyEdit,&favoriteEntry,&rows,&details,&art,&assignArt,&repairSelected,&convertSelected,&loadSelected,&cancel,&status})
            addAndMakeVisible(*c);
        rows.setModel(this);rows.setMultipleSelectionEnabled(true);rows.setRowHeight(40);
        search.setTextToShowWhenEmpty("Search name, format or library path",colours::secondary);
        search.onTextChange=[this]{filter();};
        format.onChange=[this]{filter();};
        family.addItem("All instruments",1);
        int id=2;for(const auto& name:families()){family.addItem(name,id++);familyEdit.addItem(name,id-2);}
        family.setSelectedId(1,dontSendNotification);
        family.onChange=[this]{filter();};
        favorites.onClick=[this]{filter();};
        manage.onClick=[this]{updateManagement();refreshSelection();rows.repaint();resized();};
        familyEdit.onChange=[this]
        {
            if(updating||task)return;
            const auto indices=selectedIndices();
            if(indices.empty())return;
            for(auto i:indices)catalog.entries[i].family=normalizedFamily(familyEdit.getText());
            persist();filter();
        };
        favoriteEntry.onClick=[this]
        {
            if(task)return;
            const auto indices=selectedIndices();if(indices.empty())return;
            const bool value=!std::all_of(indices.begin(),indices.end(),[this](auto i){return catalog.entries[i].favorite;});
            for(auto i:indices)catalog.entries[i].favorite=value;
            persist();filter();
        };
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
        if(validCatalog)status.setText("Browse instruments, or open Manage library to add folders and convert.",dontSendNotification);
        manage.setToggleState(catalog.roots.isEmpty(),dontSendNotification);
        updateManagement();setSize(840,560);startTimerHz(8);
    }
    ~Manager() override
    {
        stopTimer();
        if(task)
        {
            task->cancel.store(true);
            if(task->worker.joinable())task->worker.join();
            // Closing a dialog must not lose completed conversions/repairs.
            // The worker has stopped; persist its successful partial batch.
            if(task->kind==Task::batchTask&&task->error.isEmpty())
                if(auto r=save(task->proposal,index());r.failed())
                    Logger::writeToLog("Studio Library: "+r.getErrorMessage());
            task.reset();
        }
        rows.setModel(nullptr);setLookAndFeel(nullptr);
    }
    void resized() override
    {
        const auto& m=appearance;
        auto a=getLocalBounds().reduced(m.px(10));
        const int h=m.control(30,12), gap=m.px(4);
        auto top=a.removeFromTop(h);
        manage.setBounds(top.removeFromRight(jmin(m.span(140),top.getWidth()/3)).reduced(2));
        favorites.setBounds(top.removeFromRight(jmin(m.span(100),top.getWidth()/3)).reduced(2));
        family.setBounds(top.reduced(2));
        top=a.removeFromTop(h);
        if(manage.getToggleState())format.setBounds(top.removeFromRight(jmin(m.span(145),top.getWidth()/3)).reduced(2));
        search.setBounds(top.reduced(2));
        if(manage.getToggleState())
        {
            top=a.removeFromTop(h);
            for(auto* button:{&rescan,&removeRoot,&addRoot})
                button->setBounds(top.removeFromRight(jmin(m.span(100),top.getWidth()/3)).reduced(2));
            roots.setBounds(top.reduced(2));
        }
        a.removeFromTop(gap);
        status.setBounds(a.removeFromBottom(h).reduced(2));
        auto buttons=a.removeFromBottom(h);
        cancel.setBounds(buttons.removeFromRight(jmin(m.span(88),buttons.getWidth()/4)).reduced(2));
        loadSelected.setBounds(buttons.removeFromRight(jmin(m.span(180),buttons.getWidth()/2)).reduced(2));
        if(manage.getToggleState())convertSelected.setBounds(buttons.reduced(2));
        a.removeFromBottom(gap);
        auto right=a.removeFromRight(jmin(m.span(290),a.getWidth()*2/5));
        art.setBounds(right.removeFromTop(jmin(m.px(170),right.getHeight()/3)).reduced(4));
        auto edit=right.removeFromTop(h);
        favoriteEntry.setBounds(edit.removeFromRight(jmin(m.span(130),edit.getWidth()/2)).reduced(2));
        familyEdit.setBounds(edit.reduced(2));
        if(manage.getToggleState())
        {
            auto tools=right.removeFromTop(h);
            assignArt.setBounds(tools.removeFromLeft(tools.getWidth()/2).reduced(2));
            repairSelected.setBounds(tools.reduced(2));
        }
        details.setBounds(right.reduced(4));rows.setBounds(a.reduced(2));
    }
private:
    struct Task
    {
        enum Kind{scanTask,batchTask,loadTask} kind=scanTask;
        Catalog proposal;sampler::Instrument instrument;Presentation presentation;
        std::atomic<bool> cancel{false},done{false};std::atomic<int> progress{0};
        String message,error,entryKey;std::thread worker;
        ~Task(){cancel.store(true);if(worker.joinable())worker.join();}
    };
    StudioLookAndFeel look;Load onLoad;Catalog catalog;bool validCatalog=true;
    std::vector<std::size_t> filtered;
    ComboBox roots,format,family,familyEdit;
    TextEditor search,details;
    ImageComponent art;
    ListBox rows;
    TextButton addRoot{"Add folder"},removeRoot{"Remove folder"},rescan{"Rescan"},convertSelected{"Convert selected"},loadSelected{"Load as new track"},cancel{"Cancel"},assignArt{"Assign artwork"},repairSelected{"Repair sample paths"};
    ToggleButton favorites{"Favorites"},manage{"Manage library"};
    TextButton favoriteEntry{"Favorite"};
    Label status;
    bool updating=false;
    void updateManagement()
    {
        const bool visible=manage.getToggleState();
        for(Component* c:std::initializer_list<Component*>{&roots,&addRoot,&removeRoot,&rescan,&format,&assignArt,&repairSelected,&convertSelected})c->setVisible(visible);
        if(!visible&&format.getSelectedId()>1){format.setSelectedId(1,dontSendNotification);filter();}
    }
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
    void filter(const StringArray& preserve={})
    {
        const auto q=search.getText().trim();const auto f=format.getSelectedId()>1?format.getText():String();
        const auto group=family.getSelectedId()>1?family.getText():String();
        filtered.clear();
        for(std::size_t i=0;i<catalog.entries.size();++i)
        {
            const auto& e=catalog.entries[i];
            if(!matches(e,q,group,favorites.getToggleState(),f))continue;
            filtered.push_back(i);
        }
        rows.deselectAllRows();rows.updateContent();
        for(int i=0;i<getNumRowsConst();++i)if(preserve.contains(catalog.entries[filtered[static_cast<std::size_t>(i)]].key()))rows.selectRow(i,false,false);
        rows.repaint();refreshSelection();
    }
    int getNumRows() override{return static_cast<int>(filtered.size());}
    void paintListBoxItem(int row,Graphics& g,int width,int height,bool selected) override
    {
        if(!isPositiveAndBelow(row,getNumRows()))return;
        const auto& e=catalog.entries[filtered[static_cast<std::size_t>(row)]];
        if(selected)g.fillAll(colours::control);
        g.setColour(colours::text);g.setFont(uiFont(13));
        const auto state=e.converted!=File()?"Native":e.available?"Source":"Offline";
        g.drawFittedText((e.favorite?String("★ "):String())+e.name,8,1,jmax(0,width-104),height/2,Justification::centredLeft,1);
        g.setColour(colours::secondary);g.setFont(uiFont(11));
        g.drawFittedText(e.family+(manage.getToggleState()?" | "+e.format.toUpperCase():String()),8,height/2,jmax(0,width-104),height/2,Justification::centredLeft,1);
        g.drawFittedText(state,jmax(8,width-96),0,88,height,Justification::centredRight,1);
    }
    void selectedRowsChanged(int) override{refreshSelection();}
    void listBoxItemDoubleClicked(int,const MouseEvent&) override{if(loadSelected.isEnabled())startLoad();}
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
        const Entry* entry=selected.size()==1?&catalog.entries[selected.front()]:nullptr;
        const bool native=entry&&(entry->converted!=File()||entry->source.hasFileExtension("studiosampler"));
        const bool convertible=entry&&entry->available&&(!bank(entry->source)||entry->program>=0);
        loadSelected.setButtonText(native?"Load as new track":"Prepare instrument");
        loadSelected.setEnabled(validCatalog&&!task&&onLoad&&(native||convertible));
        familyEdit.setEnabled(validCatalog&&!task&&!selected.empty());favoriteEntry.setEnabled(validCatalog&&!task&&!selected.empty());
        const ScopedValueSetter<bool> hold(updating,true);
        familyEdit.setSelectedId(0,dontSendNotification);
        favoriteEntry.setButtonText(!selected.empty()&&std::all_of(selected.begin(),selected.end(),[this](auto i){return catalog.entries[i].favorite;})?"Unfavorite":"Favorite");
        art.setImage(Image());
        if(selected.size()!=1)
        {details.setText(String(static_cast<int>(selected.size()))+" selected / "+String(getNumRowsConst())+" shown. Select up to 128 for batch conversion.",false);return;}
        const auto& e=catalog.entries[selected.front()];
        familyEdit.setSelectedId(families().indexOf(e.family)+1,dontSendNotification);
        details.setText(e.name+"\n"+e.family+"\n"+(e.converted!=File()||e.source.hasFileExtension("studiosampler")?"Native instrument":"Source instrument — Prepare instrument, review notes, then load")+
            "\n"+(manage.getToggleState()?e.source.getFullPathName()+"\n"+(e.converted!=File()?"Native: "+e.converted.getFullPathName()+"\n":String())+"\n"+e.report:e.report.substring(0,1200)),false);
        art.setImage(previewImage(e.portrait.isNotEmpty()?portraitFile(e.portrait):e.artwork));
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
                String managed;
                if(auto r=retainPortrait(image,managed);r.failed()){safe->status.setText(r.getErrorMessage(),dontSendNotification);return;}
                for(auto& e:safe->catalog.entries)if(e.key()==key){e.artwork=image;e.portrait=managed;break;}
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
        const auto entry=catalog.entries[chosen.front()];
        if(entry.converted==File()&&!entry.source.hasFileExtension("studiosampler")){startBatch();return;}
        const auto file=entry.converted==File()&&entry.source.hasFileExtension("studiosampler")?entry.source:entry.converted;
        if(!file.existsAsFile()){status.setText("Native preset missing; convert again.",dontSendNotification);return;}
        task=std::make_unique<Task>();auto* t=task.get();t->kind=Task::loadTask;
        t->presentation={entry.name,entry.family,entry.portrait};t->entryKey=entry.key();
        t->worker=std::thread([t,file,entry]
        {
            try
            {
                auto r=sampler::Plugin::preparePreset(file,{},t->instrument,&t->cancel);
                if(r.failed())t->error=r.getErrorMessage();
                else for(const auto& z:t->instrument.zones)
                    if(!z.mapping.sample||z.problem.isNotEmpty()){t->error="Native preset has missing recordings. Repair it in Studio Sampler.";break;}
                if(t->instrument.zones.empty()&&t->error.isEmpty())t->error="Native preset has no playable regions.";
                if(t->error.isEmpty()&&!t->cancel.load()&&t->presentation.portrait.isEmpty()&&entry.artwork.existsAsFile())
                {String retained;if(retainPortrait(entry.artwork,retained).wasOk())t->presentation.portrait=retained;}
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
                for(auto& e:catalog.entries)if(e.key()==t->entryKey&&e.portrait.isEmpty()&&t->presentation.portrait.isNotEmpty())
                {e.portrait=t->presentation.portrait;persist();break;}
                auto callback=onLoad;auto instrument=std::move(t->instrument);auto presentation=std::move(t->presentation);
                if(auto* dialog=findParentComponentOfClass<DialogWindow>())dialog->exitModalState(0);
                MessageManager::callAsync([callback=std::move(callback),instrument=std::move(instrument),presentation=std::move(presentation)]() mutable
                {callback(std::move(instrument),std::move(presentation));});return;
            }
        }
        else
        {
            StringArray keep;for(auto i:selectedIndices())keep.add(catalog.entries[i].key());
            catalog=std::move(t->proposal);rebuildFormats();filter(keep);status.setText(t->message,dontSendNotification);persist();
        }
        refreshSelection();
    }
};
}
