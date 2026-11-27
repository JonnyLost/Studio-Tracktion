// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioUI.h"
#include "OstinatoCore.h"
#include "OstinatoAudition.h"
#include <functional>
#include <optional>

namespace studio::ostinato
{
struct TrackChoice
{
    std::string id,name;
    bool operator==(const TrackChoice&) const=default;
};

class MatrixEditor final : public juce::Component
{
public:
    MatrixEditor():matrix(*this)
    {
        using namespace juce;
        for(auto* c:std::initializer_list<Component*>{&name,&masterSteps,&masterDivision,&applyMaster,&add,&remove,&status,&viewport,
                &track,&pitch,&channel,&mode,&division,&numerator,&denominator,&bars,&applyRow,&mute,&solo,
                &cell,&velocity,&gate,&accent,&enabled,&applyCell,&preview,&mixer,&stage,&effects,&print,&power,&pitchMode,&interval,&repeatCount,&densityThreshold,&intensity,&density,&hardware,&advanced,&harmonyButton,&variationButton})
            addAndMakeVisible(*c);
        viewport.setViewedComponent(&matrix,false);viewport.setScrollBarsShown(true,true);
        effects.setTooltip("Open the arrangement track inspector for effects; no extra processing is created.");
        for(auto* l:std::initializer_list<Label*>{&masterCountLabel,&pitchLabel,&channelLabel,&divisionLabel,&numeratorLabel,&barsLabel,&cellLabel,&velocityLabel,&gateLabel,&intervalLabel,&repeatLabel,&thresholdLabel,&intensityLabel,&densityLabel})
        {addAndMakeVisible(*l);l->setFont(uiFont(10));l->setJustificationType(Justification::centredLeft);}
        name.setTextToShowWhenEmpty("Pattern name",colours::secondary);
        name.setInputRestrictions(160);name.setReturnKeyStartsNewLine(false);
        for(auto* e:{&masterSteps,&pitch,&channel,&division,&numerator,&bars,&cell,&velocity,&gate})
            e->setInputRestrictions(5,"0123456789");
        interval.setInputRestrictions(4,"-0123456789");repeatCount.setInputRestrictions(3,"0123456789");repeatCount.setText("4",false);
        pitchMode.addItem("Fixed pitch",1);pitchMode.addItem("Root + intervals",2);pitchMode.addItem("Follow played root",3);pitchMode.addItem("Scale degrees",4);pitchMode.addItem("Chord tones",5);pitchMode.addItem("Arpeggio up",6);pitchMode.addItem("Arpeggio down",7);pitchMode.addItem("Arpeggio bounce",8);
        harmonyButton.onClick=[this]{editHarmony();};
        variationButton.onClick=[this]{editVariations();};
        variationButton.setTooltip("Set A–D cycle sequence, fills, humanization and density evolution.");
        harmonyButton.setTooltip("Set the starting root, scale and chord. Dedicated MIDI input changes the live harmony during audition.");
        interval.setTooltip("Signed semitones relative to row root");repeatCount.setTooltip("Finite audition master repeats (1–256)");
        densityThreshold.setInputRestrictions(3,"0123456789");densityThreshold.setTooltip("0 = essential; 1–127 = optional at CC11 density threshold");
        for(auto* slider:{&intensity,&density}){slider->setSliderStyle(Slider::LinearHorizontal);slider->setTextBoxStyle(Slider::TextBoxRight,false,44,20);slider->setRange(0,127,1);slider->setValue(127,dontSendNotification);}
        intensity.setTooltip("CC1: scales newly generated note velocity from 25% to 100%");density.setTooltip("CC11: admits optional steps up to the selected threshold");
        hardware.onClick=[this]{if(chooseHardware)chooseHardware(&hardware);};
        masterSteps.setTooltip("Master step count 1–256");
        pitch.setTooltip("MIDI note 0–127 (C4 = 60)");channel.setTooltip("MIDI channel 1–16");
        division.setTooltip("Number of cells in the selected row");
        numerator.setTooltip("Independent meter numerator");bars.setTooltip("Independent cycle bars");
        cell.setTooltip("Selected cell number (1-based)");velocity.setTooltip("MIDI velocity 1–127");
        gate.setTooltip("Gate in thousandths (750 = 75%)");
        mode.addItem("Follow grid",1);mode.addItem("Shared span",2);mode.addItem("Independent cycle",3);
        for(int d=1;d<=32;++d)if(ticksPerQuarter%d==0)masterDivision.addItem(String(d)+" / beat",d);
        for(int d:{1,2,4,8,16,32})denominator.addItem("/ "+String(d),d);
        status.setJustificationType(Justification::centredLeft);
        name.onReturnKey=[this]{applyMasterEdit();};
        applyMaster.onClick=[this]{applyMasterEdit();};
        add.onClick=[this]{auto p=pattern;if(p.addRow()){auto id=p.rows.back().id;if(commit(std::move(p)))select(id,0);}else say("Maximum 64 rows.");};
        remove.onClick=[this]{auto p=pattern;if(!p.removeRow(selected)){say("Keep at least one row.");return;}if(commit(std::move(p)))select(pattern.rows.front().id,0);};
        remove.setTooltip("Remove the selected pattern row (Studio Undo can restore it). Does not delete an arrangement track.");
        applyRow.onClick=[this]{applyRowEdit();};
        mode.onChange=[this]{updateTimingFields();};
        pitchMode.onChange=[this]{updatePitchCaption();};
        applyCell.onClick=[this]{applyCellEdit();};
        advanced.onClick=[this]{editAdvanced();};
        cell.onReturnKey=[this]{
            if(auto* r=row()){int value=cell.getText().getIntValue();if(value>=1&&value<=(int)r->steps.size())select(selected,(size_t)value-1);else say("Cell number out of range.");}
        };
        preview.onClick=[this]{previewRender();};
        mixer.onClick=[this]{if(openMixer)openMixer();};stage.onClick=[this]{if(openStage)openStage();};
        effects.onClick=[this]{if(openEffects)openEffects();};
        power.onClick=[this]{if(auditioning){if(stopAudition)stopAudition();}else{int repeats=0;if(!read(repeatCount,1,maxRepeats,repeats)){say("Enter 1–256 repeats.");return;}if(startAudition)startAudition(pattern,repeats);}};
        power.setTooltip("Audition with main transport stopped. Do not play other MIDI notes on destination instruments during audition.");
        print.setEnabled(false);print.setTooltip("Print to individual MIDI regions arrives in O10.");
        appearanceChanged();
        refreshFields();
    }
    ~MatrixEditor() override {viewport.setViewedComponent(nullptr,false);}
    std::function<bool(const Pattern&,const Pattern&)> write;
    std::function<void()> openMixer,openStage,openEffects,stopAudition;
    std::function<void(const Pattern&,int)> startAudition;
    std::function<void(juce::Component*)> chooseHardware;
    std::function<std::vector<std::pair<std::string,std::string>>(const std::string&,int)> articulationChoices;
    ConductingValues conducting()const{return {int(intensity.getValue()),int(density.getValue())};}
    void setConducting(ConductingValues v){if(int(intensity.getValue())!=v.intensity)intensity.setValue(v.intensity,juce::dontSendNotification);if(int(density.getValue())!=v.density)density.setValue(v.density,juce::dontSendNotification);}
    void setConductingMapping(ConductingMapping m)
    {
        intensityLabel.setText("CC"+juce::String(m.intensityCC)+" · Intensity",juce::dontSendNotification);
        densityLabel.setText("CC"+juce::String(m.densityCC)+" · Density",juce::dontSendNotification);
        intensity.setTooltip("Scales newly generated note velocity from 25% to 100%");
        density.setTooltip("Admits optional steps up to the selected threshold");
    }
    void hardwareLabel(const juce::String& text){hardware.setButtonText(text);hardware.setTooltip(text);}

    void reflectHarmony(const Harmony& h)
    {
        static const char* names[]={"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
        if(!h.valid())return;
        juce::String label=names[h.root];label+=" · ";
        for(int i=0;i<12;++i)if(h.chord&(1u<<i)){label+=names[(h.root+i)%12];label+=" ";}
        if(harmonyButton.getButtonText()!=label){harmonyButton.setButtonText(label);harmonyButton.setTooltip(label+" — click while stopped to edit starting harmony");}
    }
    void auditionState(bool active,const juce::String& statusText){auditioning=active;power.setButtonText(active?"Stop":"Audition");if(!active)reflectHarmony(pattern.harmony);updateEnabled();say(statusText);}

    void setContext(const std::string& serialized,std::vector<TrackChoice> choices,bool editable)
    {
        const bool tracksChanged=tracks!=choices;tracks=std::move(choices);
        bool changed=serialized!=payload||!hasContext;
        hasContext=true;
        if(changed)
        {
            payload=serialized;Pattern p;
            healthy=serialized.empty()||decode(serialized,p);
            pattern=healthy?std::move(p):Pattern{};
            if(!row())selected=pattern.rows.front().id;
            selectedCell=std::min(selectedCell,row()->steps.size()-1);
            refreshFields();
            if(!healthy)say("Invalid saved pattern — editing disabled to preserve the original data.");
        }
        else if(tracksChanged)refillTracks();
        writable=editable&&healthy;
        updateEnabled();
        if(changed||tracksChanged){resized();matrix.repaint();}
    }
    void appearanceChanged()
    {
        viewport.setScrollBarThickness(appearance.scrollbar());
        status.setFont(uiFont(12));
        for(auto* l:{&masterCountLabel,&pitchLabel,&channelLabel,&divisionLabel,&numeratorLabel,&barsLabel,&cellLabel,&velocityLabel,&gateLabel,&intervalLabel,&repeatLabel,&thresholdLabel,&intensityLabel,&densityLabel})l->setFont(uiFont(10));
        resized();repaint();matrix.repaint();
    }
    void paint(juce::Graphics& g) override {g.fillAll(colours::canvas);}
    void resized() override
    {
        auto a=getLocalBounds().reduced(appearance.px(8));
        const int h=appearance.control(30),gap=appearance.px(3);
        const int caption=appearance.control(15);
        auto field=[&](juce::Component& c,juce::Label& l,juce::Rectangle<int> r){r=r.reduced(gap,0);l.setBounds(r.removeFromTop(caption));c.setBounds(r.reduced(0,gap));};
        auto line=a.removeFromTop(h+caption);
        name.setBounds(line.removeFromLeft(line.getWidth()*2/5).reduced(gap));
        field(masterSteps,masterCountLabel,line.removeFromLeft(line.getWidth()/5));
        masterDivision.setBounds(line.removeFromLeft(line.getWidth()/3).reduced(gap));
        applyMaster.setBounds(line.removeFromLeft(line.getWidth()/2).reduced(gap));preview.setBounds(line.reduced(gap));
        line=a.removeFromTop(h);
        add.setBounds(line.removeFromLeft(line.getWidth()/6).reduced(gap));remove.setBounds(line.removeFromLeft(line.getWidth()/5).reduced(gap));
        advanced.setBounds(line.removeFromLeft(line.getWidth()/4).reduced(gap));harmonyButton.setBounds(line.removeFromLeft(line.getWidth()/3).reduced(gap));variationButton.setBounds(line.removeFromLeft(line.getWidth()/2).reduced(gap));status.setBounds(line.reduced(gap));
        auto controls=a.removeFromBottom(h+caption);
        hardware.setBounds(controls.removeFromRight(controls.getWidth()/3).reduced(gap));
        field(intensity,intensityLabel,controls.removeFromLeft(controls.getWidth()/2));field(density,densityLabel,controls);
        auto bottom=a.removeFromBottom((h+caption)*4);
        line=bottom.removeFromTop(h+caption);
        track.setBounds(line.removeFromLeft(line.getWidth()*2/7).reduced(gap));
        field(pitch,pitchLabel,line.removeFromLeft(line.getWidth()/7));field(channel,channelLabel,line.removeFromLeft(line.getWidth()/6));
        mute.setBounds(line.removeFromLeft(line.getWidth()/5).reduced(gap));solo.setBounds(line.removeFromLeft(line.getWidth()/4).reduced(gap));
        mode.setBounds(line.reduced(gap));
        line=bottom.removeFromTop(h+caption);
        field(division,divisionLabel,line.removeFromLeft(line.getWidth()/6));field(numerator,numeratorLabel,line.removeFromLeft(line.getWidth()/5));
        denominator.setBounds(line.removeFromLeft(line.getWidth()/4).reduced(gap));field(bars,barsLabel,line.removeFromLeft(line.getWidth()/3));
        applyRow.setBounds(line.reduced(gap));
        line=bottom.removeFromTop(h+caption);
        pitchMode.setBounds(line.removeFromLeft(line.getWidth()/2).reduced(gap));
        field(interval,intervalLabel,line.removeFromLeft(line.getWidth()/3));field(densityThreshold,thresholdLabel,line.removeFromLeft(line.getWidth()/2));field(repeatCount,repeatLabel,line);
        line=bottom.removeFromTop(h+caption);
        field(cell,cellLabel,line.removeFromLeft(line.getWidth()/6));field(velocity,velocityLabel,line.removeFromLeft(line.getWidth()/5));
        field(gate,gateLabel,line.removeFromLeft(line.getWidth()/4));accent.setBounds(line.removeFromLeft(line.getWidth()/3).reduced(gap));
        enabled.setBounds(line.removeFromLeft(line.getWidth()/2).reduced(gap));applyCell.setBounds(line.reduced(gap));
        auto side=a.removeFromRight(juce::jmin(a.getWidth()/4,appearance.span(116)));
        const int utilityHeight=juce::jmin(h+gap,side.getHeight()/5);
        for(auto* b:{&power,&mixer,&effects,&stage,&print})b->setBounds(side.removeFromTop(utilityHeight).reduced(gap));
        viewport.setBounds(a.reduced(gap));
        matrix.setSize(matrix.preferredWidth(viewport.getWidth()),matrix.preferredHeight());
    }

   #if STUDIO_SPATIAL_TESTS
    bool reviewToggleFirstStep(){auto* r=row();if(!r)return false;const auto id=r->id;const bool before=r->steps[0].enabled;toggle(id,0);return row()->steps[0].enabled!=before;}
    bool verifyEditor()
    {
        setContext({},{{"taiko","Taiko"},{"claves","Claves"}},true);
        Pattern stored;
        write=[&](const Pattern& old,const Pattern& next){if(old!=stored)return false;stored=next;return true;};
        add.onClick();
        if(stored.rows.size()!=2||selected!=stored.rows.back().id)return false;
        track.setSelectedId(3,juce::dontSendNotification);pitch.setText("36",false);
        mode.setSelectedId(2,juce::dontSendNotification);division.setText("5",false);applyRow.onClick();
        if(stored.rows[1].trackId!="claves"||stored.rows[1].steps.size()!=5||stored.rows[1].pitch!=36)return false;
        select(selected,4);velocity.setText("111",false);gate.setText("500",false);accent.setToggleState(true,juce::dontSendNotification);
        enabled.setToggleState(true,juce::dontSendNotification);applyCell.onClick();
        if(!stored.rows[1].steps[4].enabled||!stored.rows[1].steps[4].accent||stored.rows[1].steps[4].velocity!=111)return false;
        pitchMode.setSelectedId(2,juce::dontSendNotification);applyRow.onClick();
        interval.setText("-5",false);densityThreshold.setText("80",false);applyCell.onClick();
        if(stored.rows[1].pitchMode!=PitchMode::intervals||stored.rows[1].steps[4].interval!=-5||stored.rows[1].steps[4].densityThreshold!=80)return false;
        setConducting({20,80});if(conducting()!=ConductingValues{20,80})return false;
        const auto before=stored;division.setText("4",false);applyRow.onClick();if(stored!=before)return false;
        setContext(encode(stored),tracks,true);
        if(matrix.cellAt(matrix.xFor(stored.rowBoundary(stored.rows[1],4))+1,1)!=4)return false;
        setContext(encode(stored),tracks,false);toggle(selected,4);if(stored!=before)return false;
        setContext(encode(stored),tracks,true);remove.onClick();if(stored.rows.size()!=1)return false;
        write={};return true;
    }
   #endif
private:
    Pattern pattern;
    std::vector<TrackChoice> tracks;
    std::string payload;
    bool healthy=true,writable=false,hasContext=false,auditioning=false;
    std::uint32_t selected=1;
    size_t selectedCell=0;
    juce::TextEditor name,masterSteps,pitch,channel,division,numerator,bars,cell,velocity,gate;
    juce::ComboBox masterDivision,track,mode,denominator,pitchMode;
    juce::TextEditor interval,repeatCount,densityThreshold;
    juce::TextButton advanced{"Step details…"},harmonyButton{"Harmony…"},variationButton{"Variations…"};
    juce::Slider intensity,density;
    juce::TextButton hardware{"MIDI input: Off"};
    juce::Label thresholdLabel{"","Density tier"},intensityLabel{"","CC1 · Intensity"},densityLabel{"","CC11 · Density"};
    juce::Label intervalLabel{"","Semitones"},repeatLabel{"","Repeats"};
    juce::TextButton applyMaster{"Set grid"},add{"+ Row"},remove{"− Row"},applyRow{"Apply row"},applyCell{"Apply step"},
        preview{"Check pattern"},mixer{"Mixer"},stage{"Stage"},effects{"Effects"},print{"Print MIDI"},power{"Audition"};
    juce::ToggleButton mute{"Mute"},solo{"Solo"},accent{"Accent"},enabled{"Active"};
    juce::Label status,masterCountLabel{"","Grid steps"},pitchLabel{"","MIDI note"},channelLabel{"","Channel"},divisionLabel{"","Divisions"},numeratorLabel{"","Meter"},barsLabel{"","Bars"},cellLabel{"","Step"},velocityLabel{"","Velocity"},gateLabel{"","Gate / 1000"};
    juce::Viewport viewport;

    class Matrix final : public juce::Component
    {
    public:
        explicit Matrix(MatrixEditor& o):owner(o){setWantsKeyboardFocus(true);}
        int rowHeight()const{return appearance.control(34);}
        int headerHeight()const{return appearance.control(28);}
        int left()const{return appearance.span(180);}
        int right()const{return appearance.span(90);}
        Tick span()const
        {
            Tick span=owner.pattern.duration();
            for(const auto& r:owner.pattern.rows)span=std::max(span,owner.pattern.rowCycle(r));
            return std::max<Tick>(1,span);
        }
        int preferredWidth(int available)const
        {
            double width=std::max(360,available-left()-right());
            for(const auto& r:owner.pattern.rows)
                width=std::max(width,double(appearance.span(22))*r.steps.size()*double(span())/double(owner.pattern.rowCycle(r)));
            return left()+right()+int(std::min(16384.0,width));
        }
        int preferredHeight()const{return headerHeight()+rowHeight()*(int)owner.pattern.rows.size();}
        int xFor(Tick tick)const{return left()+int(tick*double(std::max(1,getWidth()-left()-right()))/double(span()));}
        int cellAt(int x,size_t index)const
        {
            if(index>=owner.pattern.rows.size())return -1;
            const auto& r=owner.pattern.rows[index];
            for(size_t i=0;i<r.steps.size();++i)if(x>=xFor(owner.pattern.rowBoundary(r,i))&&x<xFor(owner.pattern.rowBoundary(r,i+1)))return (int)i;
            return -1;
        }
        void paint(juce::Graphics& g) override
        {
            using namespace juce;
            g.fillAll(colours::panel);g.setFont(uiFont(12));g.setColour(colours::text);
            const int h=headerHeight(),rh=rowHeight(),l=left(),r=right();
            const int noteWidth=appearance.span(52);
            g.drawFittedText("Track",Rectangle<int>(4,0,l-noteWidth-8,h),Justification::centredLeft,1);
            g.drawFittedText("Note",Rectangle<int>(l-noteWidth,0,noteWidth,h),Justification::centred,1);
            g.drawFittedText("Pattern matrix — first cycle",Rectangle<int>(l+4,0,getWidth()-l-r-8,h),Justification::centredLeft,1);
            g.drawFittedText("Division",Rectangle<int>(getWidth()-r,0,r,h),Justification::centred,1);
            for(size_t i=0;i<owner.pattern.rows.size();++i)
            {
                const auto& row=owner.pattern.rows[i];const int y=h+int(i)*rh;
                g.setColour(row.id==owner.selected?colours::selected:colours::control);
                g.fillRect(0,y,l-2,rh-1);
                g.setColour(colours::text);
                auto text=String((int)i+1)+"  "+owner.trackName(row.trackId);
                if(row.mute)text+=" M";
                if(row.solo)text+=" S";
                g.drawFittedText(text,Rectangle<int>(4,y,l-noteWidth-8,rh),Justification::centredLeft,1);
                g.drawFittedText(MidiMessage::getMidiNoteName(row.pitch,true,true,4),Rectangle<int>(l-noteWidth,y,noteWidth-2,rh),Justification::centred,1);
                for(size_t j=0;j<row.steps.size();++j)
                {
                    const auto& step=row.steps[j];const int x=xFor(owner.pattern.rowBoundary(row,j)),end=xFor(owner.pattern.rowBoundary(row,j+1));
                    auto rect=Rectangle<int>(x,y,end-x,rh).reduced(1);
                    if(rect.isEmpty())continue;
                    g.setColour(step.enabled?(step.accent?colours::record:colours::action):colours::well);
                    g.fillRoundedRectangle(rect.toFloat(),2);
                    if(step.enabled)
                    {
                        g.setColour(colours::text.withAlpha(.65f));auto bar=rect.reduced(2).removeFromBottom(3);
                        bar.setWidth(int(bar.getWidth()*step.gate/1000.0));g.fillRect(bar);
                    }
                    if(step.enabled&&step.densityThreshold>0){g.setColour(colours::text.withAlpha(.8f));g.fillEllipse(float(rect.getX()+2),float(rect.getY()+2),4.f,4.f);}
                    if(step.enabled&&(step.sustainCells>1||step.strikes>1||step.offset||step.chance<100||!step.articulation.empty()))
                    {g.setColour(colours::text.withAlpha(.85f));g.fillRect(rect.getRight()-5,rect.getY()+2,3,3);}

                    if(row.id==owner.selected&&j==owner.selectedCell){g.setColour(colours::focus);g.drawRect(rect,2);}
                    if(rect.getWidth()>=appearance.span(19))
                    {g.setColour(colours::text);auto label=row.pitchMode==PitchMode::fixed?String(j+1):int(row.pitchMode)>=5?String(int(j)+step.interval):String(step.interval);if(step.enabled&&step.variationMask!=15){label+="\n";for(int k=0;k<5;++k)if(step.variationMask&(1u<<k))label+=String::charToString("ABCDF"[k]);}g.drawFittedText(label,rect,Justification::centred,2);}
                }
                g.setColour(colours::text);
                String detail=String(row.steps.size());
                if(row.timing==TimingMode::independentCycle)detail+=" · "+String(row.numerator)+"/"+String(row.denominator);
                else if(row.timing==TimingMode::followGrid)detail+=" · grid";
                g.drawFittedText(detail,Rectangle<int>(getWidth()-r,y,r,rh),Justification::centred,2);
                g.setColour(colours::divider);g.drawHorizontalLine(y+rh-1,0,float(getWidth()));
            }
            g.setColour(colours::focus.withAlpha(.7f));g.drawVerticalLine(xFor(owner.pattern.duration()),float(h),float(getHeight()));
        }
        void mouseDown(const juce::MouseEvent& e)override
        {
            const int y=e.y-headerHeight();if(y<0)return;
            const auto index=size_t(y/rowHeight());if(index>=owner.pattern.rows.size())return;
            const auto id=owner.pattern.rows[index].id;const int hit=cellAt(e.x,index);
            grabKeyboardFocus();owner.select(id,hit>=0?size_t(hit):0);
            if(hit>=0&&e.mods.isPopupMenu())owner.editAdvanced();else if(hit>=0)owner.toggle(id,size_t(hit));
        }
        bool keyPressed(const juce::KeyPress& k)override
        {
            if(k==juce::KeyPress::spaceKey){owner.toggle(owner.selected,owner.selectedCell);return true;}
            if(k==juce::KeyPress::leftKey||k==juce::KeyPress::rightKey)
            {
                if(auto* r=owner.row())owner.select(owner.selected,size_t(juce::jlimit(0,(int)r->steps.size()-1,(int)owner.selectedCell+(k==juce::KeyPress::leftKey?-1:1))));
                return true;
            }
            return false;
        }
    private:MatrixEditor& owner;
    } matrix;

    Row* row(){for(auto& r:pattern.rows)if(r.id==selected)return &r;return nullptr;}
    juce::String trackName(const std::string& id)const
    {
        if(id.empty())return "Unassigned";
        for(const auto& t:tracks)if(t.id==id)return juce::String::fromUTF8(t.name.c_str());
        return "Missing track";
    }
    void say(const juce::String& s){status.setText(s,juce::dontSendNotification);status.setTooltip(s);}
    static void set(juce::TextEditor& e,int n){e.setText(juce::String(n),false);}
    static bool read(const juce::TextEditor& e,int min,int max,int& result)
    {
        auto t=e.getText();if(t.isEmpty()||!t.containsOnly("0123456789"))return false;
        result=t.getIntValue();return result>=min&&result<=max;
    }
    void refillTracks()
    {
        using namespace juce;
        track.clear(dontSendNotification);track.addItem("Unassigned",1);
        int choice=1;
        if(auto* r=row())
        {
            for(size_t i=0;i<tracks.size();++i){track.addItem(String((int)i+1)+". "+String::fromUTF8(tracks[i].name.c_str()),int(i)+2);if(tracks[i].id==r->trackId)choice=int(i)+2;}
            if(!r->trackId.empty()&&choice==1){choice=int(tracks.size())+2;track.addItem("Missing track (retained)",choice);}
        }
        track.setSelectedId(choice,dontSendNotification);
    }
    void refreshFields()
    {
        using namespace juce;
        name.setText(String::fromUTF8(pattern.name.c_str()),false);set(masterSteps,pattern.stepCount);masterDivision.setSelectedId(pattern.stepsPerQuarter,dontSendNotification);
        refillTracks();
        if(auto* r=row())
        {
            pitchMode.setSelectedId(int(r->pitchMode)+1,dontSendNotification);
            updatePitchCaption();
            set(pitch,r->pitch);set(channel,r->channel);set(division,(int)r->steps.size());
            mode.setSelectedId(int(r->timing)+1,dontSendNotification);set(numerator,r->numerator);
            denominator.setSelectedId(r->denominator,dontSendNotification);set(bars,r->bars);
            mute.setToggleState(r->mute,dontSendNotification);solo.setToggleState(r->solo,dontSendNotification);
            auto& s=r->steps[selectedCell];set(cell,int(selectedCell+1));set(velocity,s.velocity);set(gate,s.gate);set(interval,s.interval);set(densityThreshold,s.densityThreshold);
            accent.setToggleState(s.accent,dontSendNotification);enabled.setToggleState(s.enabled,dontSendNotification);
        }
        variationButton.setTooltip("Sequence: "+String::fromUTF8(pattern.variations.sequence.c_str())+" — click to edit variations, fills and evolution.");
        if(!auditioning)reflectHarmony(pattern.harmony);
        updateTimingFields();
    }
    void updatePitchCaption()
    {
        const int m=pitchMode.getSelectedId();
        intervalLabel.setText(m>=6?"Arp phase":m==4||m==5?"Degree":"Semitones",juce::dontSendNotification);
        interval.setTooltip(m>=6?"Signed offset to the cell-index arpeggio phase":m==4||m==5?"Signed zero-based scale or chord degree (0 = root)":"Signed semitones relative to the row or played root");
    }
    void updateTimingFields()
    {
        const bool independent=writable&&mode.getSelectedId()==3;
        numerator.setEnabled(independent);denominator.setEnabled(independent);bars.setEnabled(independent);
        division.setEnabled(writable&&mode.getSelectedId()!=1);
    }
    void updateEnabled()
    {
        for(auto* c:std::initializer_list<juce::Component*>{&name,&masterSteps,&masterDivision,&applyMaster,&add,&remove,&track,&pitch,&channel,&mode,&applyRow,&mute,&solo,&cell,&velocity,&gate,&accent,&enabled,&applyCell})c->setEnabled(writable);
        remove.setEnabled(writable&&pattern.rows.size()>1);add.setEnabled(writable&&pattern.rows.size()<maxRows);
        advanced.setEnabled(writable&&!auditioning);harmonyButton.setEnabled(writable&&!auditioning);variationButton.setEnabled(writable&&!auditioning);
        hardware.setEnabled(writable&&!auditioning&&bool(chooseHardware));densityThreshold.setEnabled(writable&&!auditioning);
        pitchMode.setEnabled(writable&&!auditioning);interval.setEnabled(writable&&!auditioning);repeatCount.setEnabled(!auditioning);
        power.setEnabled(auditioning||(healthy&&writable&&bool(startAudition)));
        preview.setEnabled(healthy&&!auditioning);updateTimingFields();
    }
    void select(std::uint32_t id,size_t index)
    {
        selected=id;if(auto* r=row())selectedCell=std::min(index,r->steps.size()-1);
        refreshFields();matrix.repaint();
    }
    bool commit(Pattern p)
    {
        if(auditioning||!writable||!p.valid()||!write||!write(pattern,p)){say("Pattern edit refused — project changed, locked, or invalid.");return false;}
        pattern=std::move(p);payload=encode(pattern);
        if(!row())selected=pattern.rows.front().id;
        selectedCell=std::min(selectedCell,row()->steps.size()-1);
        refreshFields();updateEnabled();resized();matrix.repaint();say("Pattern updated — save project to keep it.");return true;
    }
    void toggle(std::uint32_t id,size_t index)
    {
        if(!writable)return;
        auto p=pattern;for(auto& r:p.rows)if(r.id==id&&index<r.steps.size()){r.steps[index].enabled=!r.steps[index].enabled;commit(std::move(p));return;}
    }
    void applyMasterEdit()
    {
        int count=0;auto p=pattern;
        p.name=name.getText().trim().toStdString();
        if(!read(masterSteps,1,maxSteps,count)||!p.setGrid(count,masterDivision.getSelectedId())){say("Invalid master grid, name, or active cells would be lost.");return;}
        commit(std::move(p));
    }
    void applyRowEdit()
    {
        auto p=pattern;int n=4,d=4,b=1,count=p.stepCount,pitchValue=0,ch=0;
        const int choice=mode.getSelectedId();
        if(choice<1||choice>3||!read(pitch,0,127,pitchValue)||!read(channel,1,16,ch)
            ||(choice!=1&&!read(division,1,maxSteps,count))
            ||(choice==3&&(!read(numerator,1,32,n)||!read(bars,1,16,b))))
        {say("Invalid row fields.");return;}
        if(choice==3)d=denominator.getSelectedId();
        if(!p.setRowTiming(selected,static_cast<TimingMode>(choice-1),count,n,d,b)){say("Invalid timing or active cells would be lost.");return;}
        for(auto& r:p.rows)if(r.id==selected)
        {
            const int id=track.getSelectedId();
            if(id==1)r.trackId.clear();else if(id>=2&&id-2<(int)tracks.size())r.trackId=tracks[size_t(id-2)].id;
            r.pitch=pitchValue;r.channel=ch;r.pitchMode=static_cast<PitchMode>(pitchMode.getSelectedId()-1);r.mute=mute.getToggleState();r.solo=solo.getToggleState();
        }
        commit(std::move(p));
    }
    void applyCellEdit()
    {
        auto p=pattern;int c=0,v=0,g=0;
        for(auto& r:p.rows)if(r.id==selected)
        {
            if(!read(cell,1,(int)r.steps.size(),c)||!read(velocity,1,127,v)||!read(gate,1,1000,g)){say("Invalid cell, velocity or gate.");return;}
            auto text=interval.getText();auto digits=text.startsWithChar('-')?text.substring(1):text;
            if(digits.isEmpty()||!digits.containsOnly("0123456789")||text.getIntValue()<-127||text.getIntValue()>127){say("Invalid semitone interval.");return;}
            int tier=0;if(!read(densityThreshold,0,127,tier)){say("Density tier must be 0–127.");return;}
            auto& s=r.steps[size_t(c-1)];s.enabled=enabled.getToggleState();s.accent=accent.getToggleState();s.velocity=v;s.gate=g;s.interval=text.getIntValue();s.densityThreshold=tier;
            if(commit(std::move(p)))select(selected,size_t(c-1));return;
        }
    }
    void editHarmony()
    {
        using namespace juce;
        if(!writable||auditioning)return;
        const auto snapshot=pattern;
        auto* w=new AlertWindow("Starting harmony","Choose the starting harmony for all following rows. The row MIDI note sets the register. Scale and chord degrees are zero-based; arpeggios use cell index plus the step interval.",MessageBoxIconType::NoIcon,this);
        StringArray roots{"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
        StringArray scales{"Major","Natural minor","Harmonic minor","Dorian","Mixolydian","Major pentatonic","Chromatic"};
        StringArray chords{"Major","Minor","Diminished","Augmented","Sus2","Sus4","Dominant 7","Major 7","Minor 7","Root only","Keep current chord"};
        const std::array<unsigned,10> masks{0x091,0x089,0x049,0x111,0x085,0x0a1,0x491,0x891,0x489,1};
        w->addComboBox("root",roots,"Root");w->addComboBox("scale",scales,"Scale");w->addComboBox("chord",chords,"Chord");
        w->addComboBox("rootRule",StringArray{"Lowest played note","Recognize chord root"},"Keyboard root rule");
        w->getComboBoxComponent("rootRule")->setSelectedItemIndex(pattern.harmony.recognizeInversions?1:0,dontSendNotification);
        w->getComboBoxComponent("root")->setSelectedItemIndex(pattern.harmony.root,dontSendNotification);
        w->getComboBoxComponent("scale")->setSelectedItemIndex(int(pattern.harmony.scale),dontSendNotification);
        const auto found=std::find(masks.begin(),masks.end(),pattern.harmony.chord);
        w->getComboBoxComponent("chord")->setSelectedItemIndex(int(found-masks.begin()),dontSendNotification);
        w->addButton("Apply",1,KeyPress(KeyPress::returnKey));w->addButton("Cancel",0,KeyPress(KeyPress::escapeKey));
        const Component::SafePointer<MatrixEditor> safe(this);
        w->enterModalState(true,ModalCallbackFunction::create([safe,w,snapshot,masks](int result){
            if(result!=1||!safe||!safe->writable||safe->auditioning||safe->pattern!=snapshot)return;
            auto p=snapshot;
            p.harmony.root=w->getComboBoxComponent("root")->getSelectedItemIndex();
            p.harmony.recognizeInversions=w->getComboBoxComponent("rootRule")->getSelectedItemIndex()==1;
            p.harmony.scale=static_cast<Scale>(w->getComboBoxComponent("scale")->getSelectedItemIndex());
            const int chord=w->getComboBoxComponent("chord")->getSelectedItemIndex();
            if(chord>=0&&chord<int(masks.size()))p.harmony.chord=masks[size_t(chord)];
            safe->commit(std::move(p));
        }),true);
    }
    void editVariations()
    {
        using namespace juce;
        if(!writable||auditioning)return;
        const auto snapshot=pattern;
        const auto& v=pattern.variations;
        auto* w=new AlertWindow("Variations and evolution","One letter per master cycle. Set step membership in Step details (A–D or F for additive fills). Humanization is repeatable with the same seed.",MessageBoxIconType::NoIcon,this);
        w->addTextEditor("sequence",String::fromUTF8(v.sequence.c_str()),"Sequence (A–D, max 64)");
        w->addTextEditor("fill",String(v.fillEvery),"Fill every N cycles (0 = off)");
        w->addTextEditor("timing",String(v.timing),"Timing variation (0–200 / 1000 cell)");
        w->addTextEditor("velocity",String(v.velocity),"Velocity variation (0–30)");
        w->addTextEditor("seed",String(v.seed),"Random seed (0–999999)");
        w->addTextEditor("first",String(v.densityStart),"Starting density ceiling (0–127)");
        w->addTextEditor("last",String(v.densityEnd),"Ending density ceiling (0–127)");
        w->addButton("Apply",1,KeyPress(KeyPress::returnKey));w->addButton("Cancel",0,KeyPress(KeyPress::escapeKey));
        const Component::SafePointer<MatrixEditor> safe(this);
        w->enterModalState(true,ModalCallbackFunction::create([safe,w,snapshot](int result){
            if(result!=1||!safe||!safe->writable||safe->auditioning||safe->pattern!=snapshot)return;
            auto p=snapshot;auto& v=p.variations;
            v.sequence=w->getTextEditorContents("sequence").trim().toUpperCase().toStdString();
            auto read=[&](const char* key,int& value){
                auto t=w->getTextEditorContents(key).trim();
                if(t.isEmpty()||t.length()>6||!t.containsOnly("0123456789"))return false;
                value=t.getIntValue();return true;
            };
            if(!read("fill",v.fillEvery)||!read("timing",v.timing)||!read("velocity",v.velocity)||!read("seed",v.seed)
                ||!read("first",v.densityStart)||!read("last",v.densityEnd)||!v.valid())
            {safe->say("Invalid variation settings.");return;}
            safe->commit(std::move(p));
        }),true);
    }
    void editAdvanced()
    {
        using namespace juce;
        if(!writable||auditioning||!row())return;
        const auto snapshot=pattern;const auto id=selected;const auto index=selectedCell;
        const auto& step=row()->steps[index];
        auto choices=articulationChoices?articulationChoices(row()->trackId,row()->channel):std::vector<std::pair<std::string,std::string>>{};
        StringArray names;names.add("Leave articulation unchanged");
        int current=0;
        for(size_t i=0;i<choices.size();++i){names.add(String::fromUTF8(choices[i].second.c_str()));if(choices[i].first==step.articulation)current=int(i)+1;}
        if(!step.articulation.empty()&&current==0){choices.push_back({step.articulation,"Missing mapping (retained)"});names.add("Missing mapping (retained)");current=names.size()-1;}
        auto* w=new AlertWindow("Step details","Sustain spans inactive following cells; repeated strikes require a one-cell span. Articulations use saved track mappings.",MessageBoxIconType::NoIcon,this);
        w->addTextEditor("span",String(step.sustainCells),"Sustain cells (1–16)");
        w->addTextEditor("strikes",String(step.strikes),"Strikes (1–8)");
        w->addTextEditor("offset",String(step.offset),"Timing offset (−450 to +450 / 1000)");
        w->addTextEditor("chance",String(step.chance),"Probability (0–100%)");
        String membership;
        for(int i=0;i<5;++i)if(step.variationMask&(1u<<i))membership+=String::charToString("ABCDF"[i]);
        w->addTextEditor("variation",membership,"Variations (A B C D F, no spaces)");
        w->addComboBox("art",names,"Articulation");
        w->getComboBoxComponent("art")->setSelectedItemIndex(current,dontSendNotification);
        w->addButton("Apply",1,KeyPress(KeyPress::returnKey));w->addButton("Cancel",0,KeyPress(KeyPress::escapeKey));
        const Component::SafePointer<MatrixEditor> safe(this);
        w->enterModalState(true,ModalCallbackFunction::create([safe,w,snapshot,id,index,choices](int result){
            if(result!=1||!safe||!safe->writable||safe->auditioning||safe->pattern!=snapshot)return;
            auto p=snapshot;
            auto readSigned=[&](const char* key,int min,int max,int& value){
                auto t=w->getTextEditorContents(key).trim();auto digits=t.startsWithChar('-')?t.substring(1):t;
                if(digits.isEmpty()||digits.length()>4||!digits.containsOnly("0123456789"))return false;
                value=t.getIntValue();return value>=min&&value<=max;
            };
            for(auto& r:p.rows)if(r.id==id)
            {
                auto& s=r.steps[index];
                if(!readSigned("span",1,16,s.sustainCells)||!readSigned("strikes",1,8,s.strikes)||!readSigned("offset",-450,450,s.offset)||!readSigned("chance",0,100,s.chance))
                {safe->say("Invalid step details.");return;}
                const auto letters=w->getTextEditorContents("variation").trim().toUpperCase().toStdString();
                if(letters.empty()||letters.size()>5||letters.find_first_not_of("ABCDF")!=std::string::npos)
                {safe->say("Use A–D and/or F for step membership.");return;}
                s.variationMask=0;
                for(char letter:letters)s.variationMask|=1u<<(letter=='F'?4:letter-'A');
                const int choice=w->getComboBoxComponent("art")->getSelectedItemIndex();
                if(choice==0)s.articulation.clear();else if(choice>0&&choice<=(int)choices.size())s.articulation=choices[size_t(choice-1)].first;
                if(safe->commit(std::move(p)))safe->select(id,index);
                return;
            }
        }),true);
    }
    void previewRender()
    {
        std::vector<std::string> ids;for(const auto& t:tracks)ids.push_back(t.id);
        Render render;std::string error;
        int repeats=0;if(!read(repeatCount,1,maxRepeats,repeats)){say("Enter 1–256 repeats.");return;}
        if(!compile(pattern,ids,repeats,render,error)){say(juce::String::fromUTF8(error.c_str()));return;}
        say(juce::String((int)render.notes.size())+" notes · "+juce::String(render.duration/double(ticksPerQuarter),2)+" beats · "+juce::String(render.unassignedRows)+" unassigned rows. No MIDI sent.");
    }
};

class MatrixWindow final : public juce::DocumentWindow
{
public:
    MatrixWindow():DocumentWindow("Studio — Ostinato Engine",colours::canvas,closeButton)
    {
        setUsingNativeTitleBar(true);setResizable(true,false);setResizeLimits(640,520,1800,1100);
        panel=new MatrixEditor;setContentOwned(panel,true);
        if(auto* display=juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto bounds=display->userBounds.toNearestInt();
            centreWithSize(juce::jmin(980,bounds.getWidth()-30),juce::jmin(620,bounds.getHeight()-30));
        }
        else centreWithSize(980,560);
    }
    std::function<void()> onClose;
    void closeButtonPressed()override{if(onClose)onClose();setVisible(false);}
    MatrixEditor& editor(){return *panel;}
private:MatrixEditor* panel=nullptr;
};
}
