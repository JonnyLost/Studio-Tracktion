// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioUI.h"
#include "StudioInstrumentIcons.h"
#include "UniversalInstrumentState.h"
#include "UniversalInstrumentCatalog.h"
#include "StudioInstrumentCatalogData.h"
#include <functional>

namespace studio::designer
{
class UniversalSurface final : public juce::Component
{
public:
    explicit UniversalSurface(bool compactView):compact(compactView)
    {
        using namespace juce;
        addAndMakeVisible(title);addAndMakeVisible(viewport);viewport.setViewedComponent(&body,false);
        for(auto* c:std::initializer_list<Component*>{&full,&design,&mix,&rooms,&original,&info,&volume,&pan,&room,&volumeLabel,&panLabel,&roomLabel})
            addChildComponent(*c);
        full.onClick=[this]{if(openFull)openFull();};
        design.onClick=[this]{if(openDesigner)openDesigner();};
        mix.onClick=[this]{if(openMixer)openMixer();};
        rooms.onClick=[this]{if(openRooms)openRooms();};
        original.onClick=[this]{if(openOriginal)openOriginal();};
        info.onClick=[this]{infoVisible=!infoVisible;resized();repaint();};
        title.setJustificationType(Justification::centredLeft);
        volumeLabel.setText("Volume",dontSendNotification);panLabel.setText("Pan",dontSendNotification);roomLabel.setText("Room",dontSendNotification);roomLabel.setJustificationType(Justification::centred);
        volumeLabel.setJustificationType(Justification::centred);panLabel.setJustificationType(Justification::centred);
        volume.setSliderStyle(Slider::LinearVertical);volume.setRange(-60,6,0.1);volume.setTextValueSuffix(" dB");
        pan.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);pan.setRange(-1,1,0.01);
        room.setSliderStyle(Slider::LinearVertical);room.setRange(-60,0,0.1);room.setTextValueSuffix(" dB");
        for(auto* s:{&volume,&pan,&room})s->setTextBoxStyle(Slider::TextBoxBelow,false,66,20);
        volume.onValueChange=[this]{if(mixerChange)mixerChange(true,volume.getValue());};
        pan.onValueChange=[this]{if(mixerChange)mixerChange(false,pan.getValue());};
        room.onValueChange=[this]{if(roomChange)roomChange(room.getValue());};
        if(!compact)
        {
            int bytes=0;auto* data=StudioInstrumentCatalogData::getNamedResource(StudioInstrumentCatalogData::namedResourceList[0],bytes);
            if(data)catalog.load(String::fromUTF8(data,bytes));
        }
    }
    ~UniversalSurface() override {viewport.setViewedComponent(nullptr,false);}
    std::function<void(const String&,int)> perform;
    std::function<void()> openFull,openDesigner,openMixer,openRooms,openOriginal;
    std::function<void(bool,double)> mixerChange;
    std::function<void(double)> roomChange;
    void setProfile(const String& key,const std::optional<Profile>& next,bool articulationsOnly=false)
    {
        if(key==contextKey&&onlyArts==articulationsOnly&&profile==next)return;
        contextKey=key;profile=next;onlyArts=articulationsOnly;infoVisible=profile&&profile->channelInfo;
        controls.clear();
        if(profile)
            for(const auto& e:profile->elements)
            {
                if(!(compact?e.transport:e.full)||(onlyArts&&e.kind!="articulation"))continue;
                auto c=std::make_unique<Control>();c->element=e;
                const bool continuous=e.kind=="control"&&e.presentation!="button"
                    &&(e.binding.type=="cc"||e.binding.type=="pressure"||e.binding.type=="bend");
                c->continuous=continuous;
                if(continuous)
                {
                    c->label.setText(e.label,juce::dontSendNotification);c->label.setJustificationType(juce::Justification::centred);
                    c->slider.setSliderStyle(compact?juce::Slider::LinearHorizontal:e.presentation=="fader"?juce::Slider::LinearVertical:juce::Slider::RotaryHorizontalVerticalDrag);
                    c->slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
                    c->slider.setRange(0,e.binding.type=="bend"?16383:127,1);c->slider.setValue(e.binding.value,juce::dontSendNotification);
                    c->slider.setTooltip(e.label+" — MIDI "+e.binding.type+"; shows last value sent here, not plug-in feedback");
                    c->slider.onValueChange=[this,p=c.get()]{if(perform)perform(p->element.id,(int)p->slider.getValue());};
                    body.addAndMakeVisible(c->label);body.addAndMakeVisible(c->slider);
                }
                else
                {
                    c->button.setButtonText(e.label);
                    c->button.setTooltip(e.binding.type=="unassigned"?"Unassigned — edit in Instrument Designer":e.label+" — MIDI "+e.binding.type);
                    c->button.onClick=[this,p=c.get()]{if(perform)perform(p->element.id,p->element.binding.value);};
                    body.addAndMakeVisible(c->button);
                }
                controls.push_back(std::move(c));
            }
        title.setText(profile?profile->name:"Select a track with an assigned instrument profile",juce::dontSendNotification);
        viewport.setViewPosition(0,0);resized();repaint();
    }
    void refresh(const LiveValues& values,bool enabled,bool mixerEnabled,double db,double p,bool roomEnabled=false,double roomDb=-60)
    {
        using namespace juce;
        for(auto& c:controls)
        {
            const auto& e=c->element;
            const bool valid=enabled&&e.binding.type!="unassigned";
            if(c->continuous)
            {
                c->slider.setEnabled(valid);
                if(!c->slider.isMouseButtonDown())
                    if(auto v=values.value(e.id))c->slider.setValue(*v,dontSendNotification);
            }
            else
            {
                c->button.setEnabled(valid);
                c->button.setToggleState(profile&&values.selected(e,profile->channel),dontSendNotification);
            }
        }
        volume.setEnabled(mixerEnabled);pan.setEnabled(mixerEnabled);room.setEnabled(roomEnabled);
        if(!room.isMouseButtonDown())room.setValue(roomDb,dontSendNotification);
        if(!volume.isMouseButtonDown())volume.setValue(db,dontSendNotification);
        if(!pan.isMouseButtonDown())pan.setValue(p,dontSendNotification);
    }
   #if STUDIO_SPATIAL_TESTS
    int visibleElementCount() const {return (int)controls.size();}
    bool exercise(const String& id,int value)
    {
        for(auto& c:controls)if(c->element.id==id)
        {
            if(c->continuous){if(!c->slider.isEnabled())return false;c->slider.setValue(value,juce::sendNotificationSync);}
            else {if(!c->button.isEnabled()||!c->button.onClick)return false;c->button.onClick();}
            return true;
        }
        return false;
    }
    int displayedValue(const String& id) const
    {
        for(const auto& c:controls)if(c->element.id==id&&c->continuous)return (int)c->slider.getValue();
        return -1;
    }
   #endif
    void resized() override
    {
        using namespace juce;
        auto a=getLocalBounds().reduced(appearance.px(5));
        const auto line=appearance.control(26);
        if(compact)
        {
            auto top=a.removeFromLeft(jmin(a.getWidth()/3,appearance.span(155)));
            full.setVisible(true);full.setBounds(top.removeFromBottom(jmin(line,top.getHeight()/2)));
            title.setBounds(top);
            design.setVisible(false);mix.setVisible(false);rooms.setVisible(false);original.setVisible(false);info.setVisible(false);
            volume.setVisible(false);pan.setVisible(false);volumeLabel.setVisible(false);panLabel.setVisible(false);room.setVisible(false);roomLabel.setVisible(false);
            viewport.setBounds(a);
            const int w=appearance.span(104),h=jmax(1,a.getHeight()-viewport.getScrollBarThickness());
            body.setSize(jmax(a.getWidth(),w*(int)controls.size()),h);
            int x=0;for(auto& c:controls)
            {
                auto cell=Rectangle<int>(x,0,w,h).reduced(2);x+=w;
                if(c->continuous){c->label.setBounds(cell.removeFromTop(jmin(line,cell.getHeight()/2)));c->slider.setBounds(cell);}
                else c->button.setBounds(cell);
            }
            return;
        }
        full.setVisible(false);
        auto head=a.removeFromTop(line);title.setBounds(head);
        const int side=appearance.span(112);
        artworkBounds=a.removeFromLeft(jmin(side,a.getWidth()/5));
        auto utility=a.removeFromRight(jmin(appearance.span(110),a.getWidth()/3));
        for(auto* b:{&design,&mix,&rooms,&original,&info})
        {b->setVisible(true);b->setBounds(utility.removeFromTop(line+4).reduced(2));}
        infoBounds=infoVisible?utility:Rectangle<int>{};
        auto audio=a.removeFromRight(jmin(appearance.span(186),a.getWidth()/3));
        auto left=audio.removeFromLeft(audio.getWidth()/3);auto middle=audio.removeFromLeft(audio.getWidth()/2);
        volumeLabel.setVisible(true);panLabel.setVisible(true);volume.setVisible(true);pan.setVisible(true);room.setVisible(true);roomLabel.setVisible(true);
        volumeLabel.setBounds(left.removeFromTop(line));panLabel.setBounds(middle.removeFromTop(line));roomLabel.setBounds(audio.removeFromTop(line));
        volume.setBounds(left.reduced(2));pan.setBounds(middle.reduced(2));room.setBounds(audio.reduced(2));
        viewport.setBounds(a);
        int arts=0,other=0;
        for(const auto& c:controls)if(c->element.kind=="articulation")++arts;else ++other;
        const int tile=appearance.span(90),ah=appearance.control(34),ch=appearance.px(104);
        const int h=jmax(appearance.px(150),a.getHeight()-viewport.getScrollBarThickness());
        const int artRows=jmax(1,(h-line)/ah),controlRows=jmax(1,(h-line)/ch);
        const int aw=jmax(tile,(arts+artRows-1)/artRows*tile),cw=jmax(tile,(other+controlRows-1)/controlRows*tile);
        body.setSize(jmax(a.getWidth(),aw+cw+8),h);
        int ai=0,ci=0;for(auto& c:controls)
        {
            const bool art=c->element.kind=="articulation";const int i=art?ai++:ci++,rows=art?artRows:controlRows,height=art?ah:ch;
            auto cell=Rectangle<int>((art?0:aw+8)+(i/rows)*tile,line+(i%rows)*height,tile,height).reduced(2);
            if(c->continuous){c->label.setBounds(cell.removeFromTop(line));c->slider.setBounds(cell);}
            else c->button.setBounds(cell);
        }
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(colours::panel);
        if(compact||!profile)return;
        auto id=profile->artwork.isEmpty()?profile->instrument:profile->artwork;
        auto name=catalog.instrument(id)["name"].toString();
        auto b=artworkBounds.reduced(5);
        g.setColour(colours::text);g.setFont(uiFont(12));g.drawFittedText(name,b.removeFromBottom(appearance.control(34)),juce::Justification::centred,2);
        drawInstrumentIcon(g,b.toFloat(),instrumentIcon(name),colours::action);
        if(!infoBounds.isEmpty())
        {
            g.setColour(colours::secondary);g.setFont(uiFont(11));
            g.drawFittedText(profile->library+"\n"+profile->patch+"\nMIDI "+String(profile->channel)+" / slot "+String(profile->slot),infoBounds.reduced(4),juce::Justification::centredTop,5);
        }
    }
private:
    struct Control
    {
        Element element;bool continuous=false;
        juce::TextButton button;juce::Label label;juce::Slider slider;
    };
    bool compact,onlyArts=false,infoVisible=true;
    String contextKey;std::optional<Profile> profile;
    Catalog catalog;
    juce::Component body;
    juce::Viewport viewport;
    juce::Label title,volumeLabel,panLabel,roomLabel;
    juce::TextButton full{"Full Instrument"},design{"Designer"},mix{"Mixer"},rooms{"Rooms"},original{"Original AU"},info{"Channel info"};
    juce::Slider volume,pan,room;
    juce::Rectangle<int> artworkBounds,infoBounds;
    std::vector<std::unique_ptr<Control>> controls;
};

class UniversalWindow final : public juce::DocumentWindow
{
public:
    UniversalWindow():DocumentWindow("Studio — Universal Instrument View",colours::canvas,closeButton)
    {
        setUsingNativeTitleBar(true);setResizable(true,false);setResizeLimits(600,260,1800,900);
        surface=new UniversalSurface(false);setContentOwned(surface,true);
        auto* display=juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        centreWithSize(display?juce::jmin(1000,display->userBounds.toNearestInt().getWidth()-40):1000,340);
    }
    void closeButtonPressed() override {setVisible(false);}
    UniversalSurface& panel(){return *surface;}
private:UniversalSurface* surface=nullptr;
};
}
