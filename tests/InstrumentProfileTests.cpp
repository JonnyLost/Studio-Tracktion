// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstrumentProfile.h"
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
    std::swap(p.elements[0],p.elements[1]);p.name="Updated flute";
    if(store.save(p).failed())return 11;
    auto dest=folder.getChildFile(p.id+".studioinstrument");
    if(ProfileStore::load(dest,unchanged).failed()||!(unchanged==p))return 12;
    if(!dest.replaceWithText("<bad>"))return 13;
    if(ProfileStore::load(dest,unchanged).wasOk()||!(unchanged==p))return 14;
    if(store.list().size()!=1)return 15;
    return 0;
}
