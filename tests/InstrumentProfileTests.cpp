// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstrumentProfile.h"
#include "InstrumentMidiBinding.h"
#include "UniversalInstrumentState.h"
#include <iostream>
int main()
{
    using namespace studio::designer;
    auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("StudioProfiles","",false);
    struct Cleanup { juce::File f; ~Cleanup(){f.deleteRecursively();} } cleanup{folder};
    ProfileStore store(folder);
    Profile p; p.name="Concert flute";p.instrument="flutes.concert-flute";p.artwork=p.instrument;
    p.library="GPO 5";p.patch="Flute Solo KS";p.pluginId="fixture";p.pluginVersion="1";
    p.channel=3;p.slot=3;p.variant="Standard";p.channelInfo=false;
    p.elements.push_back({juce::Uuid().toString(),"phrasing.vibrato","Vib","articulation","button",true,true});
    p.elements.push_back({juce::Uuid().toString(),"dynamics","Dynamics","control","fader",false,true});
    p.elements[0].binding={"note",0,36,100};
    p.elements[1].binding={"cc",3,1,90};
    if(auto r=store.save(p);r.failed()){std::cerr<<r.getErrorMessage()<<"\n";return 1;}
    auto items=store.list();
    if(items.size()!=1||!(items.front()==p))return 2;
    auto copy=p.duplicate();if(copy.id==p.id||copy.elements!=p.elements)return 3;
    if(store.save(copy).failed()||store.list().size()!=2)return 4;
    auto invalid=p;invalid.elements[1].id=invalid.elements[0].id;
    if(store.save(invalid).wasOk())return 5;
    invalid=p;invalid.elements[0].transport=invalid.elements[0].full=false;
    if(store.save(invalid).wasOk())return 6;
    invalid=p;invalid.id="../unsafe";if(store.save(invalid).wasOk())return 7;
    invalid=p;invalid.channel=17;if(store.save(invalid).wasOk())return 8;
    auto x=p.xml();x->setAttribute("schemaVersion",99);Profile unchanged=p;
    if(Profile::read(*x,unchanged).wasOk()||!(unchanged==p))return 9;
    x=p.xml();x->getFirstChildElement()->setAttribute("binding","keyswitch");
    if(Profile::read(*x,unchanged).wasOk()||!(unchanged==p))return 10;
    auto legacy=p;for(auto& e:legacy.elements)e.binding={};
    auto lx=legacy.xml();lx->setAttribute("schemaVersion",1);
    if(Profile::read(*lx,unchanged).failed()||!(unchanged==legacy))return 16;
    auto malformed=p.xml();malformed->getFirstChildElement()->setAttribute("bindingNumber","xyz");
    if(Profile::read(*malformed,unchanged).wasOk())return 17;
    LearnMailbox mailbox;mailbox.arm();
    mailbox.capture(juce::MidiMessage::noteOff(3,36));
    if(mailbox.consume())return 18;
    mailbox.capture(juce::MidiMessage::noteOn(3,36,(juce::uint8)92));
    mailbox.capture(juce::MidiMessage::controllerEvent(4,1,50));
    auto learned=mailbox.consume();
    if(!learned||learned->type!="note"||learned->channel!=3||learned->number!=36||learned->value!=92||mailbox.consume())return 19;
    mailbox.arm();mailbox.capture(juce::MidiMessage::controllerEvent(4,123,0));
    if(mailbox.consume())return 20;
    mailbox.capture(juce::MidiMessage::controllerEvent(4,74,65));
    learned=mailbox.consume();if(!learned||learned->type!="cc"||learned->channel!=4||learned->number!=74||learned->value!=65)return 21;
    mailbox.arm();mailbox.capture(juce::MidiMessage::pitchWheel(2,8192));
    learned=mailbox.consume();if(!learned||learned->type!="bend"||learned->value!=8192)return 22;
    mailbox.arm();mailbox.cancel();mailbox.capture(juce::MidiMessage::noteOn(1,60,(juce::uint8)100));
    if(mailbox.consume())return 23;
    auto on=auditionMessage(p.elements[0].binding,3),off=releaseMessage(p.elements[0].binding,3);
    if(!on||!off||!on->isNoteOn()||!off->isNoteOff()||on->getChannel()!=3||off->getNoteNumber()!=36)return 24;
    auto cc=auditionMessage(p.elements[1].binding,1);
    if(!cc||!cc->isController()||cc->getChannel()!=3||cc->getControllerNumber()!=1||releaseMessage(p.elements[1].binding,1))return 25;
    if(auditionMessage(Binding{"cc",1,123,0},1)||auditionMessage(Binding{},1))return 26;
    auto bend=releaseMessage(Binding{"bend",2,0,12345},1);
    if(!bend||bend->getPitchWheelValue()!=8192||bend->getChannel()!=2)return 27;
    Attachment attachment{p,"original-instance"};
    auto encoded=attachment.serialise();auto restored=Attachment::parse(encoded);
    if(!restored||!(restored->profile==p)||!restored->matches("original-instance","fixture","1"))return 28;
    if(restored->matches("replacement","fixture","1")||restored->matches("original-instance","different","1")
       ||restored->matches("original-instance","fixture","2")||Attachment::parse("<bad>"))return 29;
    LiveValues first,second;
    first.applied(p.elements[0],p.channel,100);
    if(!first.selected(p.elements[0],p.channel)||second.selected(p.elements[0],p.channel)||second.value(p.elements[0].id))return 30;
    first.applied(p.elements[1],p.channel,47);
    if(first.value(p.elements[1].id)!=47||!first.selected(p.elements[0],p.channel))return 31;
    auto other=p.elements[0];other.id=juce::Uuid().toString();other.binding.channel=4;
    first.applied(other,p.channel,100);
    if(!first.selected(other,p.channel)||!first.selected(p.elements[0],p.channel))return 32;
    auto same=other;same.id=juce::Uuid().toString();first.applied(same,p.channel,80);
    if(first.selected(other,p.channel)||!first.selected(same,p.channel))return 33;
    std::swap(p.elements[0],p.elements[1]);p.name="Updated flute";
    if(store.save(p).failed())return 11;
    auto dest=folder.getChildFile(p.id+".studioinstrument");
    if(ProfileStore::load(dest,unchanged).failed()||!(unchanged==p))return 12;
    if(!dest.replaceWithText("<bad>"))return 13;
    if(ProfileStore::load(dest,unchanged).wasOk()||!(unchanged==p))return 14;
    if(store.list().size()!=1)return 15;
    return 0;
}
