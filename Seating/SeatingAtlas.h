// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <vector>
#include <optional>

namespace studio::seating::atlas
{
// Source coordinates are schematic chart coordinates, not audio pan, room
// metres or the legacy Swift seating projection. Keep them unchanged.
struct Marker
{
    juce::String id,instrumentId,label,kind,zone,evidence;
    double x=0,y=0;
};
struct Instrument
{
    juce::String id,name,family;
};
struct Layout
{
    juce::String id,plate,title,subtitle,confidence,status,mode;
    int number=0;
    std::optional<int> chairCount;
    std::vector<Marker> markers;
    bool singleStage() const {return status=="editable"&&mode=="stage";}
};
struct Role
{
    juce::String instrumentId,zone,kind;
    std::vector<juce::String> markerIds;
    double chartX=0,chartY=0; // suggested centroid for review only
    bool performerGroup() const {return kind=="chair"||kind=="singer";}
};
class Catalog final
{
public:
    static constexpr size_t maxBytes=4*1024*1024;
    // UI/worker thread only. Failed reads retain the previous catalog;
    // loading this data never changes an Edit, inserts, or DSP.
    juce::Result load(const juce::String& json)
    {
        using namespace juce;
        const auto fail=[](const char* why){return Result::fail(why);};
        if(json.getNumBytesAsUTF8()>maxBytes)return fail("Atlas exceeds 4 MiB.");
        Catalog next;
        if(auto r=JSON::parse(json,next.source);r.failed())return r;
        if(!next.source.isObject()||next.source["schema"].toString()!="studio.orchestral-atlas.master.v1"||
           !integer(next.source["revision"])||juce::int64(next.source["revision"])!=1)
            return fail("Unsupported seating atlas schema.");
        auto coord=next.source["coordinateSystem"];
        if(!coord.isObject()||coord["units"].toString()!="schematic"||
           coord["x"].toString()!="image left to image right from conductor viewpoint"||
           coord["y"].toString()!="upstage to downstage")
            return fail("Unknown coordinate convention.");
        const auto domain=coord["domain"];
        auto* bounds=domain.getArray();
        if(!bounds||bounds->size()!=2||!numeric((*bounds)[0])||!numeric((*bounds)[1])||
           double((*bounds)[0])!=0.0||double((*bounds)[1])!=1.0)
            return fail("Unsupported coordinate domain.");
        const auto roles=next.source["instruments"];
        auto* object=roles.getDynamicObject();
        if(!object||object->getProperties().size()<1||object->getProperties().size()>1024)
            return fail("Invalid instrument index.");
        for(const auto& property:object->getProperties())
        {
            const auto& item=property.value;
            Instrument i{item["id"].toString(),item["name"].toString(),item["family"].toString()};
            if(!item.isObject()||!field(item["id"],128)||!field(item["name"],256)||!field(item["family"],128)||
               i.id!=property.name.toString()||!next.instrumentIndex.emplace(i.id,i).second)
                return fail("Invalid instrument identity.");
        }
        const auto rows=next.source["layouts"];
        auto* array=rows.getArray();
        if(!array||array->isEmpty()||array->size()>256)return fail("Invalid layout collection.");
        std::set<juce::String> ids,plates;
        std::set<int> numbers;
        size_t total=0;
        for(const auto& row:*array)
        {
            if(!row.isObject()||!field(row["id"],128)||!field(row["plate"],32)||!field(row["title"],256)||
               !field(row["subtitle"],1024,true)||!field(row["confidence"],512)||
               !integer(row["number"]))return fail("Invalid layout metadata.");
            Layout l;
            l.id=row["id"].toString();l.plate=row["plate"].toString();l.title=row["title"].toString();
            l.subtitle=row["subtitle"].toString();l.confidence=row["confidence"].toString();
            l.status=row["status"].toString();l.mode=row["mode"].toString();if(juce::int64(row["number"])<1||juce::int64(row["number"])>256)return fail("Invalid plate number.");
            l.number=int(row["number"]);
            if(l.number<1||l.number>256||!numbers.insert(l.number).second||
               !ids.insert(l.id).second||!plates.insert(l.plate).second)
                return fail("Duplicate or invalid plate identity.");
            if((l.status!="editable"&&l.status!="artwork-only")||
               (l.mode!="stage"&&l.mode!="rooms"&&l.mode!="composite"))
                return fail("Unknown layout status or mode.");
            const auto count=row["chairCount"];
            if(!count.isVoid())
            {
                if(!integer(count)||juce::int64(count)<0||juce::int64(count)>512)return fail("Invalid reported chair count.");
                l.chairCount=int(count);
            }
            const auto placements=row["placements"];
            auto* points=placements.getArray();
            if(l.status=="artwork-only")
            {
                if((!placements.isVoid()&&(!points||!points->isEmpty()))||l.chairCount)
                    return fail("Artwork-only plate cannot supply verified coordinates/count.");
            }
            else
            {
                if(!points||points->isEmpty()||points->size()>512)return fail("Invalid placement collection.");
                total+=size_t(points->size());
                if(total>20000)return fail("Too many atlas markers.");
                std::set<juce::String> markers;
                for(const auto& point:*points)
                {
                    if(!point.isObject()||!field(point["id"],128)||!field(point["instrumentId"],128)||
                       !field(point["label"],256)||!field(point["kind"],32)||!field(point["evidence"],512)||
                       (!point["zone"].isVoid()&&!field(point["zone"],128,true))||
                       !numeric(point["x"])||!numeric(point["y"]))
                        return fail("Invalid marker metadata.");
                    Marker m{point["id"].toString(),point["instrumentId"].toString(),point["label"].toString(),
                             point["kind"].toString(),point["zone"].toString(),point["evidence"].toString(),
                             double(point["x"]),double(point["y"])};
                    if(!markers.insert(m.id).second||!next.instrumentIndex.count(m.instrumentId)||
                       !std::isfinite(m.x)||!std::isfinite(m.y)||m.x<0||m.x>1||m.y<0||m.y>1||
                       (m.kind!="chair"&&m.kind!="singer"&&m.kind!="section"&&m.kind!="station"))
                        return fail("Unknown, duplicate or out-of-bounds marker.");
                    l.markers.push_back(std::move(m));
                }
            }
            next.layoutList.push_back(std::move(l));
        }
        *this=std::move(next);
        return Result::ok();
    }
    const std::vector<Layout>& layouts()const noexcept{return layoutList;}
    const std::map<juce::String,Instrument>& instruments()const noexcept{return instrumentIndex;}
    const Layout* find(const juce::String& id)const noexcept
    {
        for(const auto& layout:layoutList)if(layout.id==id)return &layout;
        return nullptr;
    }
    // Preserve original source annotations, equipment, doubling and venue
    // zones verbatim in the source asset. The clone cannot change the index.
    juce::var sourceDocument()const{return source.clone();}
    static std::vector<Role> reviewRoles(const Layout& layout)
    {
        std::vector<Role> out;
        std::map<std::tuple<juce::String,juce::String,juce::String>,size_t> lookup;
        for(const auto& m:layout.markers)
        {
            auto key=std::tuple(m.instrumentId,m.zone,m.kind);
            const auto [it,inserted]=lookup.emplace(key,out.size());
            if(inserted)out.push_back({m.instrumentId,m.zone,m.kind,{},0,0});
            auto& r=out[it->second];r.markerIds.push_back(m.id);
            r.chartX+=m.x;r.chartY+=m.y;
        }
        for(auto& r:out){const double n=double(r.markerIds.size());r.chartX/=n;r.chartY/=n;}
        return out;
    }
private:
    juce::var source;
    std::vector<Layout> layoutList;
    std::map<juce::String,Instrument> instrumentIndex;
    static bool integer(const juce::var& v){return v.isInt()||v.isInt64();}
    static bool numeric(const juce::var& v){return integer(v)||v.isDouble();}
    static bool field(const juce::var& v,size_t limit,bool empty=false)
    {
        if(!v.isString())return false;
        const auto s=v.toString();
        if((!empty&&s.isEmpty())||s.getNumBytesAsUTF8()>limit)return false;
        auto chars=s.getCharPointer();
        while(!chars.isEmpty()){const auto c=chars.getAndAdvance();if(c<32||c==127)return false;}
        return true;
    }
};
}
