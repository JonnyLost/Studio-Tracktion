// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioSpatialEffect.h"
#include "StudioUI.h"
#include <iomanip>
#include <locale>
#include <sstream>
#include <limits>

namespace studio::spatial
{
using namespace juce;
namespace te=tracktion;
namespace
{
const Identifier settingsKey{"studioSpatialSourceSettingsV1"};
}
ParameterValues valuesOf(SourceParameters raw) noexcept
{
    const auto p=source::validate(raw);
    return {p.distance,p.width,p.tiltDb,p.outputDb,p.amount,p.air,p.distanceWidth,p.mono,
            p.direction.angle,p.direction.timing,p.direction.level,p.direction.shadow,p.direction.focus,
            p.reflections.amount,p.reflections.room};
}
SourceParameters parametersOf(ParameterValues v) noexcept
{
    SourceParameters p{v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]};
    p.direction={v[8],v[9],v[10],v[11],v[12]};
    p.reflections={v[13],v[14]};
    return source::validate(p);
}
String encodeSource(SourceParameters p)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<std::setprecision(std::numeric_limits<float>::max_digits10)<<"2";
    for(auto value:valuesOf(p))out<<' '<<value;
    return String(out.str());
}
SourceParameters decodeSource(const String& text)
{
    if(text.isEmpty()||text.length()>512)return {};
    std::istringstream in(text.toStdString());in.imbue(std::locale::classic());
    int version=0;
    if(!(in>>version)||(version!=1&&version!=2))return {};
    auto v=valuesOf(SourceParameters{});
    const size_t fields=version==1?13:v.size();
    for(size_t i=0;i<fields;++i)if(!(in>>v[i])||!std::isfinite(v[i]))return {};
    in>>std::ws;
    if(!in.eof())return {};
    return parametersOf(v);
}
const std::array<SourcePreset,8>& sourcePresets()
{
    static const auto result=[]
    {
        std::array<SourcePreset,8> p{{
            {"Neutral",{}},{"Near solo",{}},{"Left ensemble",{}},{"Right ensemble",{}},
            {"Middle distance",{}},{"Distant ensemble",{}},{"Wide stereo",{}},{"Speaker-friendly position",{}}
        }};
        p[1].parameters.distance=-.35f;p[1].parameters.width=.65f;
        p[2].parameters.distance=.2f;p[2].parameters.direction.angle=-35;p[2].parameters.direction.focus=.3f;
        p[3].parameters=p[2].parameters;p[3].parameters.direction.angle=35;
        p[4].parameters.distance=.35f;p[4].parameters.distanceWidth=.35f;
        p[5].parameters.distance=.65f;p[5].parameters.distanceWidth=.65f;
        p[6].parameters.width=1.35f;
        p[7].parameters.direction={25,.15f,1,.35f,.1f};
        return p;
    }();
    return result;
}

SourcePlugin::SourcePlugin(te::PluginCreationInfo info):Plugin(info){publish();}
SourcePlugin::~SourcePlugin(){notifyListenersOfDeletion();}
SourceParameters SourcePlugin::parameters() const{return decodeSource(state.getProperty(settingsKey).toString());}
void SourcePlugin::setParameters(SourceParameters p)
{
    const auto text=encodeSource(p);
    if(text!=state.getProperty(settingsKey).toString())
        state.setProperty(settingsKey,text,getUndoManager());
}
void SourcePlugin::beginGesture(const String& name){if(auto* undo=getUndoManager())undo->beginNewTransaction(name);}
void SourcePlugin::endGesture(){if(auto* undo=getUndoManager())undo->beginNewTransaction();}
void SourcePlugin::valueTreePropertyChanged(ValueTree& tree,const Identifier& property)
{
    if(tree==state&&property==settingsKey)publish();
    Plugin::valueTreePropertyChanged(tree,property);
}
void SourcePlugin::publish()
{
    // One message-thread writer, whole validated snapshot, no audio access
    // to ValueTree or string parsing. Odd sequence means write in progress.
    const auto v=valuesOf(parameters());
    ++revision;
    for(size_t i=0;i<v.size();++i)published[i].store(v[i]);
    ++revision;
}
bool SourcePlugin::read(ParameterValues& v,unsigned& seq) const noexcept
{
    seq=revision.load();
    if(seq&1u)return false;
    for(size_t i=0;i<v.size();++i)v[i]=published[i].load();
    return seq==revision.load();
}
void SourcePlugin::initialise(const te::PluginInitialisationInfo& info)
{
    rate=info.sampleRate;accepted=1;
    ParameterValues v{};unsigned seq=0;
    const bool coherent=read(v,seq);
    const auto p=coherent?parametersOf(v):SourceParameters{};
    active=dsp.prepare(rate,p);
    earlyReady.store(active&&reflections.prepare(rate,early::makePlan(rate,p.direction.angle,p.distance,p.reflections)));
    if(coherent&&active)accepted=seq;
    peakL.store(0);peakR.store(0);
}
void SourcePlugin::deinitialise()
{
    active=false;earlyReady.store(false);peakL.store(0);peakR.store(0);
}
source::Peaks SourcePlugin::peaks() const noexcept
{
    return {peakL.load(std::memory_order_relaxed),peakR.load(std::memory_order_relaxed)};
}
void SourcePlugin::applyToBuffer(const te::PluginRenderContext& c)
{
    auto* b=c.destBuffer;
    if(!active||b==nullptr||b->getNumChannels()<2||c.bufferStartSample<0||
       c.bufferNumSamples<=0||c.bufferStartSample>b->getNumSamples()||
       c.bufferNumSamples>b->getNumSamples()-c.bufferStartSample)return;
    if(revision.load()!=accepted)
    {
        ParameterValues v{};unsigned seq=0;
        // Rate-specific coefficient math only once per changed, coherent
        // block snapshot, not per sample. Bounded, no allocations or locks.
        if(read(v,seq)&&seq!=accepted)
        {
            const auto p=parametersOf(v);
            if(dsp.setPlan(source::makePlan(rate,p)))
            {
                if(earlyReady.load(std::memory_order_relaxed))
                    reflections.setPlan(early::makePlan(rate,p.direction.angle,p.distance,p.reflections));
                accepted=seq;
            }
        }
    }
    const auto result=dsp.process(b->getWritePointer(0,c.bufferStartSample),
                                  b->getWritePointer(1,c.bufferStartSample),c.bufferNumSamples,
                                  earlyReady.load(std::memory_order_relaxed)?&reflections:nullptr);
    const float fall=1.f/(1.f+float(c.bufferNumSamples)/float(.3*rate));
    peakL.store(std::max(result.left,fall*peakL.load(std::memory_order_relaxed)),std::memory_order_relaxed);
    peakR.store(std::max(result.right,fall*peakR.load(std::memory_order_relaxed)),std::memory_order_relaxed);
    // Extra channels, if supplied unexpectedly, are untouched. This is
    // a stereo insert, not a surround renderer.
}

namespace
{
struct Control {size_t index;const char* name;float low,high,step,scale,initial;const char* suffix;};
constexpr std::array<Control,12> controls{{
    {8,"Direction",-90,90,1,1,0," deg"},
    {0,"Distance",-100,100,1,100,0," %"},
    {1,"Stereo width",0,200,1,100,100," %"},
    {2,"Tonal tilt",-6,6,.1f,1,0," dB"},
    {5,"Air damping",0,100,1,100,100," %"},
    {6,"Distance width",0,100,1,100,0," %"},
    {9,"Ear timing",0,200,1,100,100," %"},
    {10,"Ear level",0,200,1,100,100," %"},
    {11,"Head shadow",0,200,1,100,100," %"},
    {12,"Stereo focus",0,100,1,100,0," %"},
    {4,"Amount",0,100,1,100,100," %"},
    {3,"Output gain",-24,12,.1f,1,0," dB"}
}};
class PositionPad final : public Component,public SettableTooltipClient
{
public:
    std::function<void()> begin,end;
    std::function<void(float,float)> change;
    float angle=0,distance=0;
    PositionPad()
    {
        setName("Spatial direction and perceptual distance");
        setTooltip("Drag left/right for direction, up/down for far/close. Distance is perceptual, not metres.");
    }
    Rectangle<float> plot() const{return getLocalBounds().toFloat().reduced(34.f,19.f);}
    void paint(Graphics& g) override
    {
        auto p=plot();
        if(p.isEmpty())return;
        g.setColour(colours::well);g.fillRoundedRectangle(p,5);
        g.setColour(colours::divider);g.drawRoundedRectangle(p,5,1);
        for(int i=1;i<4;++i)
        {
            const float x=p.getX()+p.getWidth()*float(i)/4;
            const float y=p.getY()+p.getHeight()*float(i)/4;
            g.drawVerticalLine(roundToInt(x),p.getY(),p.getBottom());
            g.drawHorizontalLine(roundToInt(y),p.getX(),p.getRight());
        }
        g.setColour(colours::secondary);g.setFont(uiFont(11.f));
        g.drawText("FAR",0,0,getWidth(),19,Justification::centred);
        g.drawText("CLOSE",0,getHeight()-19,getWidth(),19,Justification::centred);
        g.drawText("L",0,roundToInt(p.getCentreY())-10,30,20,Justification::centred);
        g.drawText("R",getWidth()-30,roundToInt(p.getCentreY())-10,30,20,Justification::centred);
        const auto point=Point<float>{p.getX()+p.getWidth()*(angle+90)/180,
                                      p.getY()+p.getHeight()*(1-distance)*.5f};
        g.setColour(colours::action);
        g.drawLine(p.getCentreX(),p.getCentreY(),point.x,point.y,1.5f);
        g.fillEllipse(Rectangle<float>(13,13).withCentre(point));
        g.setColour(colours::text);g.drawEllipse(Rectangle<float>(13,13).withCentre(point),1);
    }
    void mouseDown(const MouseEvent& e) override{if(begin)begin();mouseDrag(e);}
    void mouseDrag(const MouseEvent& e) override
    {
        const auto p=plot();if(p.isEmpty()||!change)return;
        change(jlimit(-90.f,90.f,180*(e.position.x-p.getX())/p.getWidth()-90),
               jlimit(-1.f,1.f,1-2*(e.position.y-p.getY())/p.getHeight()));
    }
    void mouseUp(const MouseEvent&) override{if(end)end();}
};

class SourceEditor final : public Component,private Timer
{
public:
    explicit SourceEditor(SourceWindow::Resolve r):resolve(std::move(r))
    {
        addAndMakeVisible(preset);preset.setName("Spatial preset");preset.setTextWhenNothingSelected("Custom settings");
        int id=1;for(const auto& p:sourcePresets())preset.addItem(p.name,id++);
        preset.onChange=[this]
        {
            const int selected=preset.getSelectedId();
            if(selected>0&&selected<=int(sourcePresets().size()))
                edit([&](auto& p){p=sourcePresets()[size_t(selected-1)].parameters;},"Spatial preset");
        };
        addAndMakeVisible(reset);reset.onClick=[this]{edit([](auto& p){p={};},"Reset Studio Spatial");};
        addAndMakeVisible(bypass);bypass.onClick=[this]
        {
            if(auto* p=resolve())
            {
                p->beginGesture("Bypass Studio Spatial");
                p->setEnabled(!bypass.getToggleState());
                p->endGesture();
            }
            refresh();
        };
        addAndMakeVisible(mono);mono.onClick=[this]
        {
            edit([this](auto& p){p.mono=mono.getToggleState()?1.f:0.f;},"Spatial mono check");
        };
        addAndMakeVisible(room);room.setName("Reflection room");
        int roomId=1;for(const auto& r:early::rooms)room.addItem(r.name,roomId++);
        room.setTooltip("Local early reflection geometry, not the shared Stage Room Return");
        room.onChange=[this]{if(room.getSelectedId()>0)edit([this](auto& p){p.reflections.room=float(room.getSelectedId()-1);},"Reflection room");};
        addAndMakeVisible(earlyLabel);earlyLabel.setText("Early reflections",dontSendNotification);
        addAndMakeVisible(earlyAmount);earlyAmount.setName("Early reflections");
        earlyAmount.setSliderStyle(Slider::LinearHorizontal);earlyAmount.setRange(0,100,1);
        earlyAmount.setTextBoxStyle(Slider::TextBoxRight,false,78,23);earlyAmount.setTextValueSuffix(" %");
        earlyAmount.setDoubleClickReturnValue(true,0);
        earlyAmount.onDragStart=[this]{begin("Adjust early reflections");};
        earlyAmount.onDragEnd=[this]{finish();};
        earlyAmount.onValueChange=[this]{edit([this](auto& p){p.reflections.amount=float(earlyAmount.getValue())/100.f;},"Adjust early reflections");};
        addAndMakeVisible(pad);
        pad.begin=[this]{begin("Position source");};
        pad.end=[this]{finish();};
        pad.change=[this](float angle,float distance)
        {
            edit([&](auto& p){p.direction.angle=angle;p.distance=distance;},"Position source");
        };
        addAndMakeVisible(status);status.setJustificationType(Justification::centredLeft);
        for(size_t i=0;i<controls.size();++i)
        {
            const auto& c=controls[i];auto& slider=sliders[i];auto& label=labels[i];
            addAndMakeVisible(label);label.setText(c.name,dontSendNotification);
            addAndMakeVisible(slider);slider.setName(c.name);
            slider.setSliderStyle(Slider::LinearHorizontal);
            slider.setRange(c.low,c.high,c.step);
            slider.setTextBoxStyle(Slider::TextBoxRight,false,78,23);
            slider.setTextValueSuffix(c.suffix);
            slider.setDoubleClickReturnValue(true,c.initial);
            slider.onDragStart=[this,i]{begin("Adjust "+String(controls[i].name));};
            slider.onDragEnd=[this]{finish();};
            slider.onValueChange=[this,i]
            {
                edit([&](auto& p)
                {
                    auto v=valuesOf(p);v[controls[i].index]=float(sliders[i].getValue())/controls[i].scale;
                    p=parametersOf(v);
                },"Adjust "+String(controls[i].name));
            };
        }
        setSize(720,565);appearanceChanged();refresh();startTimerHz(20);
    }
    void appearanceChanged()
    {
        for(auto& label:labels)label.setFont(uiFont(12.f));
        status.setFont(uiFont(11.f));earlyLabel.setFont(uiFont(12.f));
        resized();repaint();
    }
    void paint(Graphics& g) override
    {
        g.fillAll(colours::canvas);
        auto r=getLocalBounds().reduced(14);
        g.setColour(colours::text);g.setFont(uiFont(17.f,Font::bold));
        g.drawText("STUDIO SPATIAL",r.removeFromTop(26),Justification::centredLeft);
    }
    void resized() override
    {
        auto a=getLocalBounds().reduced(14);a.removeFromTop(30);
        auto bar=a.removeFromTop(30);
        reset.setBounds(bar.removeFromRight(92).reduced(2));
        mono.setBounds(bar.removeFromRight(132).reduced(2));
        bypass.setBounds(bar.removeFromRight(92).reduced(2));
        preset.setBounds(bar.reduced(2));
        status.setBounds(a.removeFromBottom(30));
        auto reflectionsRow=a.removeFromBottom(32).reduced(3,2);
        room.setBounds(reflectionsRow.removeFromRight(reflectionsRow.getWidth()/2).reduced(3,0));
        earlyLabel.setBounds(reflectionsRow.removeFromLeft(jmin(appearance.span(126),reflectionsRow.getWidth()/2)));
        earlyAmount.setBounds(reflectionsRow);
        pad.setBounds(a.removeFromTop(jlimit(105,185,a.getHeight()/2)));
        a.removeFromTop(5);
        const int height=jmax(26,a.getHeight()/6);
        for(size_t row=0;row<6;++row)
        {
            auto line=a.removeFromTop(height);
            auto left=line.removeFromLeft(line.getWidth()/2);
            for(int col=0;col<2;++col)
            {
                const auto i=row*2+size_t(col);
                auto cell=(col==0?left:line).reduced(4,2);
                labels[i].setBounds(cell.removeFromLeft(jmin(appearance.span(116),cell.getWidth()/2)));
                sliders[i].setBounds(cell);
            }
        }
    }
private:
    SourceWindow::Resolve resolve;
    ComboBox preset;TextButton reset{"Reset"};ToggleButton mono{"Mono check"},bypass{"Bypass"};
    PositionPad pad;Label status,earlyLabel;ComboBox room;Slider earlyAmount;
    std::array<Slider,12> sliders;
    std::array<Label,12> labels;
    bool gesture=false;
    void begin(const String& name)
    {
        if(auto* p=resolve()){p->beginGesture(name);gesture=true;}
    }
    void finish()
    {
        if(gesture)if(auto* p=resolve())p->endGesture();
        gesture=false;refresh();
    }
    template<class Fn> void edit(Fn fn,const String& name)
    {
        if(auto* p=resolve())
        {
            if(!gesture)p->beginGesture(name);
            auto v=p->parameters();fn(v);p->setParameters(v);
            if(!gesture)p->endGesture();
        }
        refresh();
    }
    void timerCallback() override{if(isShowing())refresh();}
    void refresh()
    {
        auto* p=resolve();
        const bool available=p!=nullptr;
        preset.setEnabled(available);reset.setEnabled(available);mono.setEnabled(available);bypass.setEnabled(available);
        pad.setEnabled(available);room.setEnabled(available);earlyAmount.setEnabled(available);
        for(auto& slider:sliders)slider.setEnabled(available);
        if(!p)
        {
            status.setText("Source unavailable or project busy. Stop recording or reopen the insert.",dontSendNotification);
            return;
        }
        const auto raw=p->parameters();const auto v=valuesOf(raw);
        pad.angle=raw.direction.angle;pad.distance=raw.distance;pad.repaint();
        if(!gesture)
        {
            for(size_t i=0;i<controls.size();++i)
            {
                const auto& c=controls[i];
                if(!sliders[i].hasKeyboardFocus(true))
                    sliders[i].setValue(v[c.index]*c.scale,dontSendNotification);
            }
            int matched=0;
            for(size_t i=0;i<sourcePresets().size();++i)
                if(v==valuesOf(sourcePresets()[i].parameters))matched=int(i+1);
            preset.setSelectedId(matched,dontSendNotification);
            room.setSelectedId(int(raw.reflections.room)+1,dontSendNotification);
            if(!earlyAmount.hasKeyboardFocus(true))earlyAmount.setValue(raw.reflections.amount*100,dontSendNotification);
        }
        mono.setToggleState(raw.mono>=.5f,dontSendNotification);
        bypass.setToggleState(!p->isEnabled(),dontSendNotification);
        const auto peaks=p->peaks();
        const auto db=[](float gain){return String(Decibels::gainToDecibels(gain,-90.f),1);};
        status.setText(p->isEnabled() ? "Output  L "+db(peaks.left)+" dB   R "+db(peaks.right)
                       +" dB  |  "+(raw.reflections.amount==0?String("Early reflections off"):
                           p->reflectionsAvailable()?String("Local early reflections"):String("Reflections inactive / not prepared"))
                       : String("Bypassed | Settings retained"),dontSendNotification);
    }
};
}
SourceWindow::SourceWindow(Resolve resolve)
    :DocumentWindow("Studio Spatial",colours::canvas,DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new SourceEditor(std::move(resolve)),true);
    setResizable(true,false);setResizeLimits(610,505,1400,900);
    centreWithSize(720,565);setVisible(true);
}
void SourceWindow::appearanceChanged()
{
    setBackgroundColour(colours::canvas);
    if(auto* editor=dynamic_cast<SourceEditor*>(getContentComponent()))editor->appearanceChanged();
    repaint();
}
}
