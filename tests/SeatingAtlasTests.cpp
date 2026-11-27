// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Seating/SeatingAtlas.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace studio::seating::atlas;
static void require(bool condition,const char* message)
{
    if(!condition){std::cerr<<message<<'\n';std::exit(1);}
}
int main(int argc,char** argv)
{
    require(argc==2,"expected atlas source file");
    const auto file=juce::File(juce::String::fromUTF8(argv[1]));
    require(file.existsAsFile()&&file.getSize()==970002,"original atlas file size");
    const auto json=file.loadFileAsString();
    Catalog catalog;
    auto result=catalog.load(json);
    if(result.failed())std::cerr<<result.getErrorMessage()<<'\n';
    require(result.wasOk(),"load original atlas");
    require(catalog.layouts().size()==40&&catalog.instruments().size()==118,"original corpus counts");
    int editable=0,singleStage=0,artwork=0,composite=0,rooms=0;
    size_t total=0,chairs=0,singers=0,sections=0,stations=0;
    for(const auto& l:catalog.layouts())
    {
        editable+=l.status=="editable";artwork+=l.status=="artwork-only";
        singleStage+=l.singleStage();composite+=l.mode=="composite";rooms+=l.mode=="rooms";
        total+=l.markers.size();
        for(const auto& m:l.markers)
        {
            chairs+=m.kind=="chair";singers+=m.kind=="singer";sections+=m.kind=="section";stations+=m.kind=="station";
        }
        size_t members=0;
        for(const auto& group:Catalog::reviewRoles(l))
        {
            members+=group.markerIds.size();
            require(group.chartX>=0&&group.chartX<=1&&group.chartY>=0&&group.chartY<=1,"valid review centroid");
            for(const auto& id:group.markerIds)
            {
                const auto it=std::find_if(l.markers.begin(),l.markers.end(),[&](const auto& m){return m.id==id;});
                require(it!=l.markers.end()&&it->instrumentId==group.instrumentId&&it->zone==group.zone&&it->kind==group.kind,
                    "role preserves zone and marker kind");
            }
        }
        require(members==l.markers.size(),"each marker retained");
    }
    require(editable==30&&singleStage==27&&artwork==10&&composite==1&&rooms==2,"layout classification");
    require(total==1817&&chairs==1699&&singers==98&&sections==8&&stations==12,"original marker counts");
    const auto* modern=catalog.find("plate-04");
    require(modern&&modern->markers.size()==89&&modern->singleStage(),"modern symphony");
    require(Catalog::reviewRoles(*modern).size()==20,"modern symphony role grouping");
    const auto* interstellar=catalog.find("plate-35");
    require(interstellar&&interstellar->markers.size()==134&&!interstellar->singleStage()&&!interstellar->chairCount,"multi-venue map is not one stage");
    const auto* psycho=catalog.find("plate-28");
    require(psycho&&psycho->markers.size()==34,"original Psycho layout");
    require(catalog.find("plate-09")&&catalog.find("plate-09")->markers.empty(),"artwork-only plate remains coordinate-free");
    require(catalog.find("not-a-layout")==nullptr,"unknown layout");
    require(catalog.load("{\"schema\":\"wrong\"}").failed()&&catalog.layouts().size()==40,"failed schema load is transactional");
    require(catalog.load(juce::String::repeatedString("x",int(Catalog::maxBytes+1))).failed(),"oversize rejected");
    auto doc=catalog.sourceDocument();
    auto* layouts=doc["layouts"].getArray();
    require(layouts!=nullptr,"source clone contains original layouts");
    auto* points=(*layouts)[0]["placements"].getArray();
    require(points!=nullptr&&!points->isEmpty(),"original coordinates preserved");
    (*points)[0].getDynamicObject()->setProperty("x",2.0);
    require(catalog.load(juce::JSON::toString(doc)).failed()&&catalog.layouts().size()==40,"out-of-domain marker rejected");
    doc=catalog.sourceDocument();
    layouts=doc["layouts"].getArray();
    auto duplicate=(*layouts)[0]["placements"];
    auto* dupPoints=duplicate.getArray();
    dupPoints->add((*dupPoints)[0].clone());
    require(catalog.load(juce::JSON::toString(doc)).failed(),"duplicate marker rejected");
    doc=catalog.sourceDocument();
    layouts=doc["layouts"].getArray();
    (*layouts)[0]["placements"].getArray()->getReference(0).getDynamicObject()->setProperty("instrumentId","unknown");
    require(catalog.load(juce::JSON::toString(doc)).failed(),"unknown role rejected");
    std::cout<<"Seating atlas: 40 plates, 30 editable, 27 single stage, 1817 original markers verified\n";
}
