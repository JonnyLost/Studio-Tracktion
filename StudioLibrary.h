// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioSampler.h"
#include <map>
#include <set>
#include <algorithm>

namespace studio::library
{
using namespace juce;
struct Entry
{
    File source, converted, artwork;
    String name, format, report;
    int program=-1;
    int64 size=0,time=0;
    bool available=false;
    String key() const { return source.getFullPathName()+"|"+String(program); }
};
struct Catalog
{
    StringArray roots;
    std::vector<Entry> entries;
};
inline File directory()
{
    return File::getSpecialLocation(File::userMusicDirectory).getChildFile("Studio").getChildFile("Instrument Library");
}
inline bool bank(const File& f){return f.hasFileExtension("sf2;sf3;dls;gig;p1;p3;s1;s3");}
inline bool instrument(const File& f){return bank(f)||f.hasFileExtension("studiosampler;sfz;exs;dspreset;dslibrary;nki");}
inline Result save(const Catalog& catalog,const File& target)
{
    if(auto r=target.getParentDirectory().createDirectory();r.failed())return r;
    XmlElement xml("STUDIO_LIBRARY");xml.setAttribute("version",1);
    for(const auto& root:catalog.roots)xml.createNewChildElement("ROOT")->setAttribute("path",root);
    for(const auto& e:catalog.entries)
    {
        auto* x=xml.createNewChildElement("ENTRY");
        x->setAttribute("source",e.source.getFullPathName());x->setAttribute("name",e.name);
        x->setAttribute("format",e.format);x->setAttribute("program",e.program);
        x->setAttribute("size",e.size);x->setAttribute("time",e.time);x->setAttribute("available",e.available);
        x->setAttribute("converted",e.converted==File()?String():e.converted.getFullPathName());
        x->setAttribute("artwork",e.artwork==File()?String():e.artwork.getFullPathName());
        x->setAttribute("report",e.report.substring(0,4000));
    }
    TemporaryFile temp(target);
    if(!xml.writeTo(temp.getFile())||!temp.overwriteTargetFileWithTemporary())return Result::fail("Could not save library catalog.");
    return Result::ok();
}
inline Result read(const File& file,Catalog& result)
{
    if(!file.existsAsFile())return Result::ok();
    if(file.getSize()>64ll*1024*1024)return Result::fail("Library catalog is oversized.");
    auto xml=XmlDocument::parse(file);
    if(!xml||!xml->hasTagName("STUDIO_LIBRARY")||xml->getIntAttribute("version")!=1)return Result::fail("Invalid library catalog.");
    Catalog next;std::set<String> keys;
    for(auto* x=xml->getFirstChildElement();x;x=x->getNextElement())
    {
        if(x->hasTagName("ROOT"))
        {
            auto path=x->getStringAttribute("path");
            if(File::isAbsolutePath(path)&&next.roots.size()<64)next.roots.addIfNotAlreadyThere(path);
        }
        else if(x->hasTagName("ENTRY"))
        {
            if(next.entries.size()>=50000)return Result::fail("Library catalog entry limit exceeded.");
            const auto path=x->getStringAttribute("source");if(!File::isAbsolutePath(path))continue;
            Entry e;e.source=File(path);e.name=x->getStringAttribute("name");e.format=x->getStringAttribute("format");
            e.program=x->getIntAttribute("program",-1);e.size=x->getStringAttribute("size").getLargeIntValue();
            e.time=x->getStringAttribute("time").getLargeIntValue();e.available=x->getBoolAttribute("available");
            for(auto [key,dest]:std::array<std::pair<const char*,File*>,2>{{{"converted",&e.converted},{"artwork",&e.artwork}}})
            {auto p=x->getStringAttribute(key);if(File::isAbsolutePath(p))*dest=File(p);}
            e.report=x->getStringAttribute("report");
            if(keys.insert(e.key()).second)next.entries.push_back(std::move(e));
        }
    }
    result=std::move(next);return Result::ok();
}
inline Result scan(Catalog& catalog,std::atomic<bool>& cancelled,std::atomic<int>& progress,String& message)
{
    std::map<String,Entry> found;
    // Preserve metadata and previously converted presets, including offline drives.
    for(auto e:catalog.entries){e.available=false;found.emplace(e.key(),std::move(e));}
    int visited=0,errors=0;std::set<String> files;
    for(const auto& path:catalog.roots)
    {
        File root(path);
        if(!root.isDirectory())continue;
        if(root==File::getSpecialLocation(File::userHomeDirectory)||root.getParentDirectory()==root)
            return Result::fail("Choose a specific library folder, not your home or disk root.");
        for(const auto& item:RangedDirectoryIterator(root,true,"*",File::findFiles,File::FollowSymlinks::no))
        {
            if(cancelled.load())return Result::fail("Scan cancelled; catalog unchanged.");
            if(++visited>100000)return Result::fail("Scan exceeds 100,000 file entries. Choose smaller library folders.");
            progress.store(visited);
            const auto f=item.getFile();if(!instrument(f)||!files.insert(f.getFullPathName()).second)continue;
            const auto size=f.getSize(),time=f.getLastModificationTime().toMilliseconds();
            auto add=[&](int program,String name,String report)
            {
                Entry e;e.source=f;e.name=name;e.format=f.getFileExtension().toLowerCase().substring(1);
                e.program=program;e.size=size;e.time=time;e.available=true;e.report=report;
                const auto key=e.key();
                if(auto old=found.find(key);old!=found.end())
                {
                    e.artwork=old->second.artwork;
                    if(old->second.size==size&&old->second.time==time)
                    {e.converted=old->second.converted;if(report.isEmpty())e.report=old->second.report;}
                    else e.report="Source changed; reconvert before loading. "+report;
                }
                found[key]=std::move(e);
            };
            if(bank(f))
            {
                std::vector<sampler::BankProgram> programs;
                if(auto r=sampler::Plugin::catalogSoundBank(f,programs,&cancelled);r.failed())
                {++errors;add(-1,f.getFileNameWithoutExtension(),r.getErrorMessage());}
                else for(std::size_t i=0;i<programs.size();++i)
                {
                    const auto& p=programs[i];
                    add(static_cast<int>(i),p.name+" [Bank "+String(p.bank)+" / "+String(p.number)+"]",{});
                }
            }
            else add(-1,f.getFileNameWithoutExtension(),{});
            if(found.size()>50000)return Result::fail("Catalog exceeds 50,000 entries; choose fewer libraries.");
        }
    }
    if(cancelled.load())return Result::fail("Scan cancelled; catalog unchanged.");
    catalog.entries.clear();for(auto& [key,e]:found)catalog.entries.push_back(std::move(e));
    std::sort(catalog.entries.begin(),catalog.entries.end(),[](const auto& a,const auto& b){return a.name.compareNatural(b.name)<0;});
    message=String(static_cast<int>(catalog.entries.size()))+" catalog entries; "+String(errors)+" bank catalog errors. Offline and removed sources retained.";
    return Result::ok();
}
inline Result prepare(const Entry& e,sampler::Instrument& out,sampler::BankImportMedia& media,String& report,std::atomic<bool>& cancel)
{
    using P=sampler::Plugin;
    if(cancel.load())return Result::fail("Cancelled.");
    if(!e.source.existsAsFile()||e.source.getSize()!=e.size||e.source.getLastModificationTime().toMilliseconds()!=e.time)
        return Result::fail("Source missing or changed; rescan first.");
    Result r=Result::fail("Unsupported format.");
    if(e.source.hasFileExtension("studiosampler"))r=P::preparePreset(e.source,{},out,&cancel);
    else if(e.source.hasFileExtension("sfz"))r=P::prepareSfz(e.source,{},out,report,&cancel);
    else if(e.source.hasFileExtension("exs"))r=P::prepareExs(e.source,{}, {},out,report,&cancel);
    else if(e.source.hasFileExtension("dspreset;dslibrary;nki"))r=P::prepareXmlInstrument(e.source,{},out,report,media,&cancel);
    else if(bank(e.source)&&e.program>=0)r=P::prepareSoundBank(e.source,e.program,out,report,media,&cancel);
    if(r.failed())return r;
    if(cancel.load())return Result::fail("Cancelled.");
    for(const auto& z:out.zones)
        if(!z.mapping.sample||z.problem.isNotEmpty()||!z.file.existsAsFile())
            return Result::fail("Missing/unreadable sample: "+z.file.getFileName()+". Repair in sampler or relocate the source library.");
    if(out.zones.empty())return Result::fail("No playable regions.");
    return Result::ok();
}
inline Result convert(Entry& e,const File& root,std::atomic<bool>& cancel)
{
    if(e.converted.existsAsFile())return Result::ok();
    if(e.source.hasFileExtension("studiosampler"))
    {
        sampler::Instrument out;sampler::BankImportMedia media;String report;
        if(auto r=prepare(e,out,media,report,cancel);r.failed())return r;
        e.converted=e.source;e.report="Native preset verified.";return Result::ok();
    }
    sampler::BankImportMedia media;media.parent=root.getChildFile("Media");
    sampler::Instrument out;String report;
    if(auto r=prepare(e,out,media,report,cancel);r.failed())return r;
    if(cancel.load())return Result::fail("Cancelled.");
    // Unique destination never overwrites a previous conversion or original.
    const auto folder=root.getChildFile("Presets").getChildFile(Uuid().toString());
    if(auto r=folder.createDirectory();r.failed())return r;
    const auto target=folder.getChildFile(File::createLegalFileName(e.name).substring(0,100)+".studiosampler");
    if(auto r=sampler::Plugin::writeConvertedPreset(out,target);r.failed()){folder.deleteRecursively();return r;}
    media.keep=true;e.converted=target;e.report=report.isEmpty()?"Converted to native preset.":report;
    return Result::ok();
}
inline Result repair(Entry& e,const File& root,const File& sampleRoot,std::atomic<bool>& cancel)
{
    if(!e.converted.existsAsFile()||!sampleRoot.isDirectory()||
       sampleRoot==File::getSpecialLocation(File::userHomeDirectory)||sampleRoot.getParentDirectory()==sampleRoot)
        return Result::fail("Choose an existing native preset and a specific recording folder.");
    sampler::Instrument model;
    if(auto r=sampler::Plugin::preparePreset(e.converted,{},model,&cancel);r.failed())return r;
    std::set<String> wanted;
    for(const auto& z:model.zones)if(!z.mapping.sample||z.problem.isNotEmpty())wanted.insert(z.file.getFileName().toLowerCase());
    if(wanted.empty())return Result::fail("No unavailable samples in this native preset.");
    std::map<String,std::vector<File>> matches;
    int visited=0;
    for(const auto& item:RangedDirectoryIterator(sampleRoot,true,"*",File::findFiles,File::FollowSymlinks::no))
    {
        if(cancel.load())return Result::fail("Repair cancelled.");
        if(++visited>20000)return Result::fail("Repair search exceeds 20,000 files. Choose a narrower folder.");
        const auto f=item.getFile();const auto name=f.getFileName().toLowerCase();
        if(wanted.count(name))matches[name].push_back(f);
    }
    String report="Relocated sample candidates, matched by unique filename and native boundary checks. Compare with original before using.\n";
    for(int i=0;i<static_cast<int>(model.zones.size());++i)
    {
        if(cancel.load())return Result::fail("Repair cancelled.");
        const auto& z=model.zones[static_cast<std::size_t>(i)];
        if(z.mapping.sample&&z.problem.isEmpty())continue;
        const auto old=z.file;const auto name=old.getFileName().toLowerCase();
        const auto it=matches.find(name);
        if(it==matches.end()||it->second.size()!=1)
            return Result::fail("Missing or ambiguous replacement: "+old.getFileName()+". Use Locate Sample manually.");
        sampler::Instrument proposal;
        if(auto r=sampler::Plugin::prepareSample(model,it->second.front(),i,proposal,&cancel);r.failed())return r;
        report+=old.getFullPathName()+" -> "+it->second.front().getFullPathName()+"\n";
        model=std::move(proposal);
    }
    if(cancel.load())return Result::fail("Repair cancelled.");
    const auto folder=root.getChildFile("Presets").getChildFile(Uuid().toString());
    if(auto r=folder.createDirectory();r.failed())return r;
    const auto target=folder.getChildFile(File::createLegalFileName(e.name).substring(0,100)+" - repaired.studiosampler");
    if(auto r=sampler::Plugin::writeConvertedPreset(model,target);r.failed()){folder.deleteRecursively();return r;}
    e.converted=target;e.report=report+"Original preset retained. Review the replacement paths before loading.";
    return Result::ok();
}

}
