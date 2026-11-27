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
                &cell,&velocity,&gate,&accent,&enabled,&applyCell,&preview,&mixer,&stage,&effects,&print,&power,&pitchMode,&interval,&repeatCount,&densityThreshold,&intensity,&density,&hardware})
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
        pitchMode.addItem("Fixed pitch",1);pitchMode.addItem("Root + intervals",2);
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
        gate.setTooltip("Gate in thousandths (750 = 75%, 4000 = four cells). Right-click a cell for advanced properties.");
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
        applyCell.onClick=[this]{applyCellEdit();};
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

    void auditionState(bool active,const juce::String& statusText){auditioning=active;power.setButtonText(active?"Stop":"Audition");updateEnabled();say(statusText);}

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
        status.setBounds(line.reduced(gap));
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
                        bar.setWidth(int(bar.getWidth()*std::min(1000,step.gate)/1000.0));g.fillRect(bar);
                    }
                    if(step.enabled&&(step.repeats>1||step.offset!=0||step.probability!=100||!step.articulation.empty()||step.gate>1000))
                    {g.setColour(colours::text.withAlpha(.8f));g.fillRect(rect.getRight()-5,rect.getY()+2,3,5);}
                    if(step.enabled&&step.densityThreshold>0){g.setColour(colours::text.withAlpha(.8f));g.fillEllipse(float(rect.getX()+2),float(rect.getY()+2),4.f,4.f);}
                    if(row.id==owner.selected&&j==owner.selectedCell){g.setColour(colours::focus);g.drawRect(rect,2);}
                    if(rect.getWidth()>=appearance.span(19))
                    {g.setColour(colours::text);auto label=row.pitchMode==PitchMode::intervals?String(step.interval):String(j+1);g.drawFittedText(label,rect,Justification::centred,1);}
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
            if(hit>=0){if(e.mods.isPopupMenu())owner.advancedStep();else owner.toggle(id,size_t(hit));}
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
            set(pitch,r->pitch);set(channel,r->channel);set(division,(int)r->steps.size());
            mode.setSelectedId(int(r->timing)+1,dontSendNotification);set(numerator,r->numerator);
            denominator.setSelectedId(r->denominator,dontSendNotification);set(bars,r->bars);
            mute.setToggleState(r->mute,dontSendNotification);solo.setToggleState(r->solo,dontSendNotification);
            auto& s=r->steps[selectedCell];set(cell,int(selectedCell+1));set(velocity,s.velocity);set(gate,s.gate);set(interval,s.interval);set(densityThreshold,s.densityThreshold);
            accent.setToggleState(s.accent,dontSendNotification);enabled.setToggleState(s.enabled,dontSendNotification);
        }
        updateTimingFields();
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
    void advancedStep()
    {
        using namespace juce;
        if(!writable||auditioning||!row())return;
        const auto id=selected;const auto index=selectedCell;const auto before=pattern;
        const auto& step=row()->steps[index];
        auto choices=articulationChoices?articulationChoices(row()->trackId,row()->channel):std::vector<std::pair<std::string,std::string>>{};
        StringArray labels;labels.add("No articulation change");int selectedChoice=0;
        for(const auto& choice:choices){labels.add(String::fromUTF8(choice.second.c_str()));if(choice.first==step.articulation)selectedChoice=labels.size()-1;}
        if(!step.articulation.empty()&&selectedChoice==0){choices.push_back({step.articulation,"Missing mapping (retained)"});labels.add("Missing mapping (retained)");selectedChoice=labels.size()-1;}
        auto* dialog=new AlertWindow("Advanced step "+String((int)index+1),"Changes apply to this cell only. Extended gate holds a note across cells; repeated strikes require gate ≤ 1000.",MessageBoxIconType::NoIcon);
        dialog->addTextEditor("strikes",String(step.repeats),"Strikes (1–8)");
        dialog->addTextEditor("offset",String(step.offset),"Timing offset (−250…250 / 1000)");
        dialog->addTextEditor("probability",String(step.probability),"Probability (0–100%)");
        dialog->addComboBox("articulation",labels,"Articulation / mapped expression");
        dialog->getComboBoxComponent("articulation")->setSelectedItemIndex(selectedChoice,dontSendNotification);
        dialog->addButton("Apply",1,KeyPress(KeyPress::returnKey));
        dialog->addButton("Cancel",0,KeyPress(KeyPress::escapeKey));
        Component::SafePointer<MatrixEditor> safe(this);
        dialog->enterModalState(true,ModalCallbackFunction::create([safe,dialog,before,id,index,choices](int result){
            if(result!=1||!safe||safe->pattern!=before||!safe->writable||safe->auditioning)return;
            auto p=before;
            for(auto& r:p.rows)if(r.id==id&&index<r.steps.size())
            {
                auto& step=r.steps[index];int strikes=0,prob=0;
                const auto* a=dialog->getTextEditor("strikes");const auto* b=dialog->getTextEditor("probability");
                auto t=dialog->getTextEditorContents("offset");auto digits=t.startsWithChar('-')?t.substring(1):t;
                if(!a||!b||a->getText().length()>1||b->getText().length()>3||!read(*a,1,8,strikes)||!read(*b,0,100,prob)||digits.isEmpty()||digits.length()>3||!digits.containsOnly("0123456789")||t.getIntValue()<-250||t.getIntValue()>250)
                {safe->say("Invalid advanced step settings.");return;}
                const int choice=dialog->getComboBoxComponent("articulation")->getSelectedItemIndex();
                if(choice<0||choice>(int)choices.size())return;
                step.repeats=strikes;step.offset=t.getIntValue();step.probability=prob;step.articulation=choice==0?std::string{}:choices[size_t(choice-1)].first;
                safe->commit(std::move(p));return;
            }
        }),true);
    }
    void applyCellEdit()
    {
        auto p=pattern;int c=0,v=0,g=0;
        for(auto& r:p.rows)if(r.id==selected)
        {
            if(!read(cell,1,(int)r.steps.size(),c)||!read(velocity,1,127,v)||!read(gate,1,4000,g)){say("Invalid cell, velocity or gate.");return;}
            auto text=interval.getText();auto digits=text.startsWithChar('-')?text.substring(1):text;
            if(digits.isEmpty()||!digits.containsOnly("0123456789")||text.getIntValue()<-127||text.getIntValue()>127){say("Invalid semitone interval.");return;}
            int tier=0;if(!read(densityThreshold,0,127,tier)){say("Density tier must be 0–127.");return;}
            auto& step=r.steps[size_t(c-1)];step.enabled=enabled.getToggleState();step.accent=accent.getToggleState();step.velocity=v;step.gate=g;step.interval=text.getIntValue();step.densityThreshold=tier;
            if(commit(std::move(p)))select(selected,size_t(c-1));return;
        }
    }
    void previewRender()
    {
        std::vector<std::string> ids;for(const auto& t:tracks)ids.push_back(t.id);
        Render render;std::string error;
        if(!compile(pattern,ids,1,render,error)){say(juce::String::fromUTF8(error.c_str()));return;}
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
