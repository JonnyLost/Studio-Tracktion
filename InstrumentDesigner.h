// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioUI.h"
#include "StudioInstrumentIcons.h"
#include "UniversalInstrumentCatalog.h"
#include "StudioInstrumentCatalogData.h"
#include "InstrumentProfile.h"
#include "InstrumentMidiBinding.h"
#include <functional>
#include <optional>
#include <algorithm>

namespace studio::designer
{
struct SourceContext { String track, pluginId, pluginVersion; };
class DesignerPanel final : public juce::Component, private juce::Timer
{
public:
    // Callbacks run on the message thread. Studio owns routing and device listeners.
    std::function<bool()> beginLearn;
    std::function<std::optional<Binding>()> pollLearn;
    std::function<void()> endLearn;
    std::function<juce::Result(const Profile&,const Binding&,std::function<void()>&)> sendAudition;
    void cancelLive()
    {
        ++liveRevision;
        stopTimer();
        if(learning && endLearn)endLearn();
        learning=false;learnId.clear();
        if(release){auto action=std::move(release);release={};action();}
    }
    explicit DesignerPanel(SourceContext source, juce::File folder=ProfileStore::localDirectory())
        : context(std::move(source)), store(std::move(folder))
    {
        using namespace juce;
        addAndMakeVisible(viewport); viewport.setViewedComponent(&body,false);
        int bytes=0;
        const auto* data=StudioInstrumentCatalogData::getNamedResource(StudioInstrumentCatalogData::namedResourceList[0],bytes);
        auto result=data!=nullptr ? catalog.load(String::fromUTF8(data,bytes)) : Result::fail("Missing embedded catalog");
        ready=result.wasOk();
        for (auto* c : std::initializer_list<Component*>{&heading,&contextLabel,&collection,&load,&newProfile,&duplicate,&save,&discard,
             &name,&library,&patch,&variant,&channel,&slot,&instrumentSearch,&instrument,&artworkSearch,&artwork,
             &kind,&candidateSearch,&candidates,&allTechniques,&add,&elements,&label,&style,&transport,&full,
             &apply,&up,&down,&remove,&channelInfo,&status,&preview,&bindingKind,&bindingChannel,&bindingNumber,&bindingValue,&assign,&learn,&audition,&bindingStatus})
            body.addAndMakeVisible(*c);
        heading.setText("Instrument Designer",dontSendNotification);
        heading.setFont(uiFont(20,Font::bold));
        contextLabel.setFont(uiFont(12)); status.setFont(uiFont(12));
        status.setJustificationType(Justification::centredLeft);
        for (auto* e : {&name,&library,&patch,&variant,&channel,&slot})
            e->onTextChange=[this]{ if(!refreshing) {dirty=true;preview.repaint();} };
        setupField(name,"Profile name"); setupField(library,"Library"); setupField(patch,"Patch name or stable ID");
        setupField(variant,"Patch variant"); setupField(channel,"MIDI channel 1–16"); setupField(slot,"ARIA slot 1–16");
        setupField(instrumentSearch,"Search instruments"); setupField(artworkSearch,"Search vector artwork");
        setupField(candidateSearch,"Search available techniques or controls"); setupField(label,"Custom button or control label");
        kind.addItem("Articulations",1); kind.addItem("Performance controls",2); kind.setSelectedId(1,dontSendNotification);
        bindingKind.addItem("Unassigned",1);bindingKind.addItem("Keyswitch",2);bindingKind.addItem("MIDI CC",3);
        bindingKind.addItem("Channel pressure",4);bindingKind.addItem("Pitch bend",5);
        setupField(bindingChannel,"Channel (0 = profile)");setupField(bindingNumber,"Note / CC (0–127; C4 = 60)");
        setupField(bindingValue,"Value / velocity (bend: 0–16383)");
        assign.onClick=[this]{applyBinding();};learn.onClick=[this]{startLearn();};audition.onClick=[this]{auditionSelected();};
        bindingKind.onChange=[this]{updateBindingFields();};
        style.addItem("Button",1); style.addItem("Knob",2); style.addItem("Fader",3);
        instrumentSearch.onTextChange=[this]{ refillInstruments(false); };
        artworkSearch.onTextChange=[this]{ refillInstruments(true); };
        instrument.onChange=[this]{
            if(refreshing) return;
            if(auto id=choice(instrument,instrumentIds);id.isNotEmpty())
            { profile.instrument=id; if(profile.artwork.isEmpty()) {profile.artwork=id;refillInstruments(true);} dirty=true; refillCandidates(); preview.repaint(); }
        };
        artwork.onChange=[this]{ if(!refreshing) { profile.artwork=choice(artwork,artworkIds); dirty=true; preview.repaint(); }};
        kind.onChange=[this]{refillCandidates();}; candidateSearch.onTextChange=[this]{refillCandidates();};
        allTechniques.onClick=[this]{refillCandidates();}; elements.onChange=[this]{
            int next=elements.getSelectedItemIndex();
            if(!commitElementDraft()){elements.setSelectedId(editingIndex+1,dontSendNotification);return;}
            refreshElements(next);
        };
        channelInfo.onClick=[this]{profile.channelInfo=channelInfo.getToggleState();dirty=true;preview.repaint();};
        add.onClick=[this]{addElement();}; apply.onClick=[this]{applyElement();};
        up.onClick=[this]{move(-1);}; down.onClick=[this]{move(1);};
        remove.onClick=[this]{int i=elements.getSelectedItemIndex();if(validIndex(i)){profile.elements.erase(profile.elements.begin()+i);dirty=true;refreshElements(jmin(i,(int)profile.elements.size()-1));}};
        save.onClick=[this]{saveCurrent();};
        newProfile.onClick=[this]{if(canSwitch()) startNew();};
        load.onClick=[this]{if(canSwitch()) loadSelected();};
        duplicate.onClick=[this]{if(readFields()){profile=profile.duplicate();saved.reset();dirty=true;refresh();say("Copy created. Save to retain it.");}};
        discard.onClick=[this]{
            // Explicit discard restores the last saved profile; for a new draft resets it.
            if(saved){profile=*saved;dirty=false;refresh();say("Restored saved profile.");}
            else startNew();
        };
        refreshCollection(); startNew();
        if(!ready){ say(result.getErrorMessage()); body.setEnabled(false); }
        setSize(920,620);
    }
    void setContext(SourceContext source)
    {
        cancelLive();
        context=std::move(source);
        contextLabel.setText(context.track.isEmpty() ? "Design a standalone profile" : "Selected track: "+context.track+" (reference only)",juce::dontSendNotification);
    }
    bool hasUnsavedChanges() const { return dirty; }
   #if STUDIO_SPATIAL_TESTS
    bool verifyDesigner()
    {
        using namespace juce;
        if(!ready)return false;
        instrumentSearch.setText("Concert flute",false);refillInstruments(false);
        const int index=instrumentIds.indexOf("flutes.concert-flute");
        if(index<0)return false;
        instrument.setSelectedId(index+1,dontSendNotification);instrument.onChange();
        if(profile.instrument!="flutes.concert-flute")return false;
        candidateSearch.setText("flutter",false);refillCandidates();
        if(candidateIds.indexOf("wind.flutter-tongue")<0)return false;
        candidates.setSelectedId(candidateIds.indexOf("wind.flutter-tongue")+1,dontSendNotification);
        addElement();
        kind.setSelectedId(2,dontSendNotification);candidateSearch.setText("dynamics",false);refillCandidates();
        const int ci=candidateIds.indexOf("dynamics");
        if(ci<0)return false;
        candidates.setSelectedId(ci+1,dontSendNotification);addElement();
        label.setText("Dynamics fader",false);style.setSelectedId(3,dontSendNotification);
        transport.setToggleState(false,dontSendNotification);full.setToggleState(true,dontSendNotification);
        move(-1);
        if(profile.elements.size()!=2||profile.elements[0].presentation!="fader"||profile.elements[0].transport)return false;
        name.setText("Flute fixture",false);saveCurrent();
        if(dirty||store.list().size()!=1)return false;
        const auto original=profile;
        duplicate.onClick();saveCurrent();
        if(dirty||profile.id==original.id||store.list().size()!=2)return false;
        const auto found=std::find_if(profiles.begin(),profiles.end(),[&](const auto& p){return p.id==original.id;});
        if(found==profiles.end())return false;
        collection.setSelectedId(int(found-profiles.begin())+1,dontSendNotification);loadSelected();
        setSize(660,420);resized();
        return !dirty&&profile==original&&viewport.getViewedComponent()==&body&&preview.getHeight()>0;
    }
   #endif
    void appearanceChanged()
    {
        heading.setFont(uiFont(20,juce::Font::bold));
        contextLabel.setFont(uiFont(12)); status.setFont(uiFont(12));
        resized(); repaint();
    }
    ~DesignerPanel() override { cancelLive();viewport.setViewedComponent(nullptr,false); }
    void resized() override
    {
        viewport.setBounds(getLocalBounds());
        const int w=juce::jmax(640,getWidth()-22), line=juce::jmax(28,appearance.control(30));
        const int h=juce::jmax(getHeight(), 18*line+205);
        body.setSize(w,h);
        auto r=body.getLocalBounds().reduced(12);
        heading.setBounds(r.removeFromTop(line)); contextLabel.setBounds(r.removeFromTop(line));
        auto row=r.removeFromTop(line);
        collection.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));
        load.setBounds(row.removeFromLeft(row.getWidth()/4).reduced(2));
        newProfile.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));
        duplicate.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2)); discard.setBounds(row.reduced(2));
        auto columns=r.removeFromTop(11*line);
        auto left=columns.removeFromLeft(columns.getWidth()/2).reduced(4);
        auto right=columns.reduced(4);
        for(auto* c : std::initializer_list<Component*>{&name,&library,&patch,&variant,&channel,&slot,&instrumentSearch,&instrument,&artworkSearch,&artwork,&channelInfo})
            c->setBounds(left.removeFromTop(line).reduced(2));
        for(auto* c : std::initializer_list<Component*>{&kind,&candidateSearch,&allTechniques,&candidates,&add,&elements,&label,&style})
            c->setBounds(right.removeFromTop(line).reduced(2));
        auto visibility=right.removeFromTop(line);
        transport.setBounds(visibility.removeFromLeft(visibility.getWidth()/2)); full.setBounds(visibility);
        apply.setBounds(right.removeFromTop(line).reduced(2));
        auto reorder=right.removeFromTop(line);
        up.setBounds(reorder.removeFromLeft(reorder.getWidth()/3).reduced(2));
        down.setBounds(reorder.removeFromLeft(reorder.getWidth()/2).reduced(2)); remove.setBounds(reorder.reduced(2));
        auto bind=r.removeFromTop(line);
        bindingKind.setBounds(bind.removeFromLeft(bind.getWidth()/3).reduced(2));
        bindingChannel.setBounds(bind.removeFromLeft(bind.getWidth()/3).reduced(2));
        bindingNumber.setBounds(bind.removeFromLeft(bind.getWidth()/2).reduced(2));bindingValue.setBounds(bind.reduced(2));
        auto actions=r.removeFromTop(line);
        assign.setBounds(actions.removeFromLeft(actions.getWidth()/3).reduced(2));
        learn.setBounds(actions.removeFromLeft(actions.getWidth()/2).reduced(2));audition.setBounds(actions.reduced(2));
        bindingStatus.setBounds(r.removeFromTop(line).reduced(2));
        auto footer=r.removeFromBottom(line);
        save.setBounds(footer.removeFromRight(juce::jmin(180,footer.getWidth()/3)).reduced(2)); status.setBounds(footer.reduced(2));
        preview.setBounds(r.reduced(4));
    }
private:
    using Component=juce::Component;
    using StringArray=juce::StringArray;
    using ComboBox=juce::ComboBox;
    using TextEditor=juce::TextEditor;
    using TextButton=juce::TextButton;
    using ToggleButton=juce::ToggleButton;
    using Label=juce::Label;
    using Graphics=juce::Graphics;
    SourceContext context;
    Catalog catalog; ProfileStore store; Profile profile;
    std::optional<Profile> saved;
    std::vector<Profile> profiles;
    bool dirty=false, refreshing=false, ready=false;
    int editingIndex=-1;
    unsigned liveRevision=0;
    bool learning=false;
    String learnId;
    double learnDeadline=0;
    std::function<void()> release;
    ComboBox bindingKind;
    TextEditor bindingChannel,bindingNumber,bindingValue;
    TextButton assign{"Save assignment"},learn{"MIDI Learn"},audition{"Audition (confirm patch)"};
    Label bindingStatus;
    juce::Viewport viewport;
    Component body;
    Label heading,contextLabel,status;
    ComboBox collection,instrument,artwork,kind,candidates,elements,style;
    TextEditor name,library,patch,variant,channel,slot,instrumentSearch,artworkSearch,candidateSearch,label;
    TextButton load{"Open"},newProfile{"New"},duplicate{"Duplicate"},save{"Save profile"},discard{"Discard edits"},
               add{"Add to layout"},apply{"Apply element"},up{"Move up"},down{"Move down"},remove{"Remove"};
    ToggleButton allTechniques{"Show all techniques"},transport{"Transport"},full{"Full view"},channelInfo{"Show channel information"};
    StringArray instrumentIds,artworkIds,candidateIds;
    struct Preview final : Component
    {
        explicit Preview(DesignerPanel& p):owner(p){}
        DesignerPanel& owner;
        std::vector<std::pair<juce::Rectangle<int>,String>> hits;
        void mouseDown(const juce::MouseEvent& event) override
        {
            if(!event.mods.isPopupMenu())return;
            for(const auto& hit:hits)if(hit.first.contains(event.getPosition())){owner.assignmentMenu(hit.second);return;}
        }
        void paint(Graphics& g) override
        {
            hits.clear();
            using namespace juce;
            g.fillAll(colours::panel);
            auto area=getLocalBounds().reduced(8);
            g.setColour(colours::text);g.setFont(uiFont(13,Font::bold));
            g.drawText("Layout preview — right-click a control to assign",area.removeFromTop(24),Justification::centredLeft);
            auto strip=area.removeFromTop(38);
            g.setColour(colours::well);g.fillRoundedRectangle(strip.toFloat(),4);
            g.setColour(colours::secondary);g.setFont(uiFont(10));
            g.drawText("Transport",strip.removeFromLeft(78),Justification::centredLeft);
            drawElements(g,strip,true);
            area.removeFromTop(6);
            auto art=area.removeFromLeft(jmin(105,area.getWidth()/5));
            auto item=owner.catalog.instrument(owner.profile.artwork.isEmpty()?owner.profile.instrument:owner.profile.artwork);
            auto title=item["name"].toString();
            drawInstrumentIcon(g,art.removeFromTop(jmax(0,art.getHeight()-24)).toFloat().reduced(5),instrumentIcon(title),colours::action);
            g.setColour(colours::text);g.drawFittedText(title,art,Justification::centred,1);
            if(owner.profile.channelInfo)
            {
                auto info=area.removeFromRight(jmin(112,area.getWidth()/4));
                g.setColour(colours::secondary);
                g.drawFittedText("Channel info\n"+owner.channel.getText()+" / Slot "+owner.slot.getText(),info,Justification::centred,3);
            }
            auto utility=area.removeFromRight(jmin(46,area.getWidth()/6));
            g.setColour(colours::secondary);g.drawFittedText("Mix\nRooms\nOstinato\nInstrument",utility,Justification::centred,4);
            auto arts=area.removeFromLeft(area.getWidth()/2);
            drawMatrix(g,arts,true);
            drawMatrix(g,area,false);
        }
        void drawMatrix(Graphics& g,juce::Rectangle<int> a,bool articulations)
        {
            using namespace juce;
            g.setColour(colours::secondary);g.setFont(uiFont(10));
            g.drawFittedText(articulations?"Articulations":"Controls",a.removeFromTop(16),Justification::centred,1);
            const int cols=articulations?3:2, cellWidth=a.getWidth()/cols;
            if(cellWidth<20||a.getHeight()<18)return;
            int count=0;for(const auto& e:owner.profile.elements)if(e.full&&(e.kind=="articulation")==articulations)++count;
            const int rowHeight=articulations?27:58, rows=jmax(1,a.getHeight()/rowHeight), capacity=cols*rows;
            int index=0;
            for(const auto& e:owner.profile.elements)
            {
                if(!e.full||(e.kind=="articulation")!=articulations)continue;
                if(index>=capacity)break;
                auto cell=Rectangle<int>(a.getX()+(index%cols)*cellWidth,a.getY()+(index/cols)*rowHeight,cellWidth,rowHeight).reduced(2);
                hits.emplace_back(cell,e.id);
                g.setColour(colours::control);g.fillRoundedRectangle(cell.toFloat(),3);
                g.setColour(colours::secondary);
                if(!articulations&&e.presentation!="button")
                {
                    auto shape=cell.removeFromTop(jmax(0,cell.getHeight()-18)).toFloat().reduced(5);
                    if(e.presentation=="knob"){auto d=jmin(shape.getWidth(),shape.getHeight());g.drawEllipse(shape.withSizeKeepingCentre(d,d),1.4f);}
                    else g.drawRoundedRectangle(shape.withSizeKeepingCentre(7,shape.getHeight()),2,1.4f);
                }
                g.drawFittedText(index==capacity-1&&count>capacity?"+"+String(count-capacity+1):e.label,cell,Justification::centred,2);
                ++index;
            }
        }
        void drawElements(Graphics& g,juce::Rectangle<int> a,bool compact)
        {
            using namespace juce;
            int shown=0,hidden=0;
            for(const auto& e:owner.profile.elements)
            {
                if(!(compact?e.transport:e.full))continue;
                const int w=compact?75:92;
                if(a.getWidth()<w || a.getHeight()<12){++hidden;continue;}
                auto cell=a.removeFromLeft(w).reduced(2);
                hits.emplace_back(cell,e.id);
                g.setColour(colours::control);g.fillRoundedRectangle(cell.toFloat(),3);
                g.setColour(colours::secondary);g.setFont(uiFont(10));
                if(!compact && e.presentation!="button")
                {
                    auto shape=cell.removeFromTop(jmax(0,cell.getHeight()-22)).toFloat().reduced(8);
                    if(e.presentation=="knob"){auto d=jmin(shape.getWidth(),shape.getHeight());g.drawEllipse(shape.withSizeKeepingCentre(d,d),1.5f);}
                    else g.drawRoundedRectangle(shape.withSizeKeepingCentre(8,shape.getHeight()),2,1.5f);
                }
                g.drawFittedText(e.label,cell,Justification::centred,2);++shown;
            }
            if(hidden>0 && a.getWidth()>10){g.setColour(colours::secondary);g.drawFittedText("+"+String(hidden),a,Justification::centred,1);}
            if(shown==0 && hidden==0){g.setColour(colours::secondary);g.drawFittedText("No controls selected",a,Justification::centred,1);}
        }
    } preview{*this};
    static void setupField(TextEditor& e,const String& hint){e.setTextToShowWhenEmpty(hint,colours::secondary);e.setTitle(hint);e.setTooltip(hint);}
    void say(const String& s){status.setText(s,juce::dontSendNotification);}
    static String choice(const ComboBox& c,const StringArray& ids){int i=c.getSelectedItemIndex();return juce::isPositiveAndBelow(i,ids.size())?ids[i]:String();}
    bool validIndex(int i)const{return juce::isPositiveAndBelow(i,(int)profile.elements.size());}
    bool canSwitch(){cancelLive();if(!commitElementDraft())return false;if(!dirty)return true;say("Save or discard changes before opening another profile.");return false;}
    void refillInstruments(bool art)
    {
        using namespace juce;
        auto& box=art?artwork:instrument;auto& ids=art?artworkIds:instrumentIds;
        auto current=art?profile.artwork:profile.instrument;
        ids=catalog.searchInstruments(art?artworkSearch.getText():instrumentSearch.getText());
        box.clear(dontSendNotification);
        for(int i=0;i<ids.size();++i)box.addItem(catalog.instrument(ids[i])["name"].toString(),i+1);
        box.setSelectedId(ids.indexOf(current)+1,dontSendNotification);
        box.setTextWhenNothingSelected(art?"Choose artwork silhouette":"Choose physical instrument");
    }
    void refillCandidates()
    {
        using namespace juce;
        const bool art=kind.getSelectedId()==1;
        auto ids=art?(allTechniques.getToggleState()?catalog.allTechniques():catalog.candidateTechniques(profile.instrument)):catalog.allControls();
        candidates.clear(dontSendNotification);candidateIds.clear();
        for(const auto& id:ids)
        {
            auto item=art?catalog.technique(id):catalog.control(id);
            auto title=item["name"].toString();
            if(candidateSearch.getText().isNotEmpty() && !title.containsIgnoreCase(candidateSearch.getText()) && !id.containsIgnoreCase(candidateSearch.getText()))continue;
            candidateIds.add(id);candidates.addItem(title+" ["+id+"]",candidateIds.size());
        }
        if(!candidateIds.isEmpty())candidates.setSelectedId(1,dontSendNotification);
    }
    void refreshElements(int selected)
    {
        using namespace juce;
        elements.clear(dontSendNotification);
        for(int i=0;i<(int)profile.elements.size();++i)
        {
            const auto& e=profile.elements[(size_t)i];
            elements.addItem(String(i+1)+". "+e.label+" ["+(e.transport?"T":"")+(e.full?"F":"")+"]"+(e.binding.type=="unassigned"?" · unassigned":" · "+e.binding.type),i+1);
        }
        elements.setSelectedId(validIndex(selected)?selected+1:0,dontSendNotification);
        refreshElement();preview.repaint();
    }
    void refreshElement()
    {
        using namespace juce;
        const int i=elements.getSelectedItemIndex();editingIndex=i;const bool valid=validIndex(i);
        cancelLive();
        for(auto* c:std::initializer_list<Component*>{&label,&style,&transport,&full,&apply,&up,&down,&remove})c->setEnabled(valid);
        for(auto* c:std::initializer_list<Component*>{&bindingKind,&bindingChannel,&bindingNumber,&bindingValue,&assign,&learn,&audition})c->setEnabled(valid);
        if(!valid){label.clear();bindingStatus.setText("Select a control to assign",dontSendNotification);return;}
        const auto& e=profile.elements[(size_t)i];
        label.setText(e.label,false);style.setSelectedId(e.presentation=="button"?1:e.presentation=="knob"?2:3,dontSendNotification);
        style.setEnabled(e.kind=="control");transport.setToggleState(e.transport,dontSendNotification);full.setToggleState(e.full,dontSendNotification);
        up.setEnabled(i>0);down.setEnabled(i+1<(int)profile.elements.size());
        showBinding(e.binding);
    }
    void showBinding(const Binding& b)
    {
        using namespace juce;
        bindingKind.setSelectedId(b.type=="note"?2:b.type=="cc"?3:b.type=="pressure"?4:b.type=="bend"?5:1,dontSendNotification);
        bindingChannel.setText(String(b.channel),false);bindingNumber.setText(String(b.number),false);bindingValue.setText(String(b.value),false);
        updateBindingFields();
        bindingStatus.setText(b.type=="unassigned"?"Unassigned — no MIDI":
            "Assigned: "+b.type+(b.type=="note"?" "+MidiMessage::getMidiNoteName(b.number,true,true,4)+" ("+String(b.number)+")":
            b.type=="cc"?" "+String(b.number):String())+" / channel "+(b.channel==0?"profile":String(b.channel)),dontSendNotification);
    }
    void updateBindingFields()
    {
        const auto k=bindingKind.getSelectedId();
        bindingChannel.setEnabled(k>1);bindingNumber.setEnabled(k==2||k==3);bindingValue.setEnabled(k>1);
    }
    std::optional<Binding> bindingDraft()
    {
        Binding b;const auto k=bindingKind.getSelectedId();
        if(k<=1)return b;
        auto parse=[](const TextEditor& e,int& out){auto t=e.getText().trim();if(t.isEmpty()||t.length()>5||!t.containsOnly("0123456789"))return false;out=t.getIntValue();return true;};
        b.type=k==2?"note":k==3?"cc":k==4?"pressure":"bend";
        if(!parse(bindingChannel,b.channel)||!parse(bindingValue,b.value)||((k==2||k==3)&&!parse(bindingNumber,b.number))
           ||!b.valid()||(b.type=="cc"&&b.number>=120)){say("Invalid MIDI assignment; CC 120–127 are reserved.");return {};}
        return b;
    }
    void applyBinding()
    {
        cancelLive();
        if(!validIndex(editingIndex))return;
        if(auto b=bindingDraft()){profile.elements[(size_t)editingIndex].binding=*b;dirty=true;refreshElements(editingIndex);say("Assignment saved in draft. Save profile to keep it.");}
    }
    void startLearn()
    {
        cancelLive();
        if(!validIndex(editingIndex)||!beginLearn||!beginLearn()){say("Select a stopped instrument track with an assigned enabled MIDI input.");return;}
        learnId=profile.elements[(size_t)editingIndex].id;learning=true;learnDeadline=juce::Time::getMillisecondCounterHiRes()+15000;
        bindingStatus.setText("Play one key or move a controller (15 seconds).",juce::dontSendNotification);startTimer(30);
    }
    void timerCallback() override
    {
        if(release){cancelLive();return;}
        if(!learning){stopTimer();return;}
        if(!validIndex(editingIndex)||profile.elements[(size_t)editingIndex].id!=learnId){cancelLive();return;}
        if(pollLearn)
            if(auto b=pollLearn())
            {
                cancelLive();showBinding(*b);say("MIDI captured. Review channel and value, then Save assignment.");return;
            }
        if(juce::Time::getMillisecondCounterHiRes()>=learnDeadline){cancelLive();say("MIDI Learn timed out.");}
    }
    void auditionSelected()
    {
        cancelLive();
        if(!validIndex(editingIndex)||!readFields()||!validIndex(editingIndex))return;
        const auto b=profile.elements[(size_t)editingIndex].binding;
        if(b.type=="unassigned"||!sendAudition){say("Save an assignment and select the matching loaded instrument.");return;}
        auto safe=juce::Component::SafePointer<DesignerPanel>(this);
        const auto profileId=profile.id, elementId=profile.elements[(size_t)editingIndex].id;
        const auto revision=liveRevision;
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,"Audition MIDI assignment",
            "Confirm the selected instrument contains this exact patch and slot. This sends MIDI to the existing instrument, not a new instance.",
            "Audition","Cancel",this,juce::ModalCallbackFunction::create([safe,profileId,elementId,b,revision](int ok){
                if(!ok||safe==nullptr||!safe->isShowing()||safe->liveRevision!=revision||safe->profile.id!=profileId||!safe->validIndex(safe->editingIndex)
                    ||safe->profile.elements[(size_t)safe->editingIndex].id!=elementId
                    ||!(safe->profile.elements[(size_t)safe->editingIndex].binding==b))return;
                auto r=safe->sendAudition(safe->profile,b,safe->release);
                safe->say(r.wasOk()?"MIDI sent — verify the sound in the instrument.":r.getErrorMessage());
                if(safe->release)safe->startTimer(120);
            }));
    }
    void assignmentMenu(const String& id)
    {
        if(!commitElementDraft())return;
        const auto found=std::find_if(profile.elements.begin(),profile.elements.end(),[&](const auto& e){return e.id==id;});
        if(found==profile.elements.end())return;
        refreshElements(int(found-profile.elements.begin()));
        juce::PopupMenu menu;menu.addItem(1,"Edit assignment");menu.addItem(2,"MIDI Learn");menu.addItem(3,"Audition saved assignment");
        menu.addItem(4,"Clear assignment");
        const auto safe=juce::Component::SafePointer<DesignerPanel>(this);const auto pid=profile.id;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&preview),[safe,pid,id](int result){
            if(safe==nullptr||safe->profile.id!=pid||!safe->validIndex(safe->editingIndex)
               ||safe->profile.elements[(size_t)safe->editingIndex].id!=id)return;
            if(result==1){safe->viewport.setViewPosition(0,safe->bindingKind.getY());safe->bindingKind.grabKeyboardFocus();}
            if(result==2)safe->startLearn();
            if(result==3)safe->auditionSelected();
            if(result==4){safe->cancelLive();safe->profile.elements[(size_t)safe->editingIndex].binding={};safe->dirty=true;safe->refreshElements(safe->editingIndex);}
        });
    }
    void addElement()
    {
        if(!commitElementDraft())return;
        if(profile.instrument.isEmpty()){say("Choose an instrument first.");return;}
        auto id=choice(candidates,candidateIds);if(id.isEmpty()||profile.elements.size()>=128)return;
        bool art=kind.getSelectedId()==1;
        auto item=art?catalog.technique(id):catalog.control(id);
        profile.elements.push_back({juce::Uuid().toString(),id,item["name"].toString(),art?"articulation":"control",art?"button":"knob",true,true});
        dirty=true;refreshElements((int)profile.elements.size()-1);say("Added unassigned control.");
    }
    bool commitElementDraft()
    {
        const int i=editingIndex;if(!validIndex(i))return true;
        auto e=profile.elements[(size_t)i];
        e.label=label.getText().trim();
        e.presentation=style.getSelectedId()==1?"button":style.getSelectedId()==2?"knob":"fader";
        e.transport=transport.getToggleState();e.full=full.getToggleState();
        if(e.label.isEmpty()||e.label.length()>80||(!e.transport&&!e.full)){say("Enter a label and choose at least one view.");return false;}
        if(!(e==profile.elements[(size_t)i])){profile.elements[(size_t)i]=e;dirty=true;}
        return true;
    }
    bool applyElement(bool report=true)
    {
        if(!commitElementDraft())return false;
        refreshElements(editingIndex);if(report)say("Layout updated.");return true;
    }
    void move(int offset)
    {
        int i=elements.getSelectedItemIndex();if(!validIndex(i)||!validIndex(i+offset)||!applyElement(false))return;
        std::swap(profile.elements[(size_t)i],profile.elements[(size_t)(i+offset)]);dirty=true;refreshElements(i+offset);
    }
    bool readFields()
    {
        if(!applyElement(false))return false;
        auto p=profile;p.name=name.getText().trim();p.library=library.getText().trim();p.patch=patch.getText().trim();p.variant=variant.getText().trim();
        if(!channel.getText().containsOnly("0123456789")||!slot.getText().containsOnly("0123456789")){say("Channel and slot must be integers 1–16.");return false;}
        p.channel=channel.getText().getIntValue();p.slot=slot.getText().getIntValue();
        if(auto r=p.validate();r.failed()){say(r.getErrorMessage());return false;}
        profile=std::move(p);return true;
    }
    void refresh()
    {
        using namespace juce;
        const ScopedValueSetter<bool> guard(refreshing,true);
        editingIndex=-1;
        name.setText(profile.name,false);library.setText(profile.library,false);patch.setText(profile.patch,false);variant.setText(profile.variant,false);
        channel.setText(String(profile.channel),false);slot.setText(String(profile.slot),false);
        instrumentSearch.setText({},false);artworkSearch.setText({},false);
        channelInfo.setToggleState(profile.channelInfo,dontSendNotification);
        refillInstruments(false);refillInstruments(true);refillCandidates();refreshElements(-1);setContext(context);
    }
    void startNew()
    {
        profile=Profile{};saved.reset();editingIndex=-1;
        if(context.track.isNotEmpty()){profile.name=context.track;profile.pluginId=context.pluginId;profile.pluginVersion=context.pluginVersion;}
        dirty=false;refresh();say("Choose an instrument, then add articulations and controls.");
    }
    void refreshCollection()
    {
        using namespace juce;
        profiles=store.list();collection.clear(dontSendNotification);
        for(int i=0;i<(int)profiles.size();++i)
        {
            const auto& p=profiles[(size_t)i];collection.addItem(p.name+" ("+p.id.substring(0,8)+")",i+1);
            if(p.id==profile.id)collection.setSelectedId(i+1,dontSendNotification);
        }
        collection.setTextWhenNothingSelected("Saved instrument collection");
    }
    void loadSelected()
    {
        int i=collection.getSelectedItemIndex();if(!juce::isPositiveAndBelow(i,(int)profiles.size()))return;
        profile=profiles[(size_t)i];saved=profile;dirty=false;refresh();say("Profile opened — no live instrument changes.");
    }
    void saveCurrent()
    {
        if(!readFields())return;
        if(auto r=store.save(profile);r.failed()){say(r.getErrorMessage());return;}
        saved=profile;dirty=false;refreshCollection();say("Saved to your instrument collection.");
    }
};
class DesignerWindow final : public juce::DocumentWindow
{
public:
    explicit DesignerWindow(SourceContext context)
        : DocumentWindow("Studio — Instrument Designer",colours::canvas,closeButton)
    {
        setUsingNativeTitleBar(true);setResizable(true,false);setResizeLimits(660,420,1800,1200);
        panel=new DesignerPanel(std::move(context));setContentOwned(panel,true);
        auto bounds=juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const int w=bounds?juce::jmin(920,bounds->userBounds.toNearestInt().getWidth()-32):920;
        const int h=bounds?juce::jmin(620,bounds->userBounds.toNearestInt().getHeight()-60):620;
        centreWithSize(w,h);
    }
    void closeButtonPressed() override {panel->cancelLive();setVisible(false);} // preserve the unsaved draft while Studio is running
    void open(SourceContext context){panel->setContext(std::move(context));setVisible(true);toFront(true);}
    DesignerPanel& designer(){return *panel;}
    void appearanceChanged(){setBackgroundColour(colours::canvas);panel->appearanceChanged();}
private:
    DesignerPanel* panel=nullptr; // owned by DocumentWindow
};
}
