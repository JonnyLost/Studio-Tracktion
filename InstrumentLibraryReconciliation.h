// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AriaCatalog.h"
#include "InstrumentProfile.h"
#include "UniversalInstrumentCatalog.h"
#include <algorithm>
#include <optional>

namespace studio::designer
{
struct PatchSuggestion
{
    String label, semantic, evidence, printedNote;
    int note=-1; // never an enabled binding
};
inline String patchStamp(const aria::Patch& p)
{
    return String(p.modified)+":"+String(p.size);
}
inline String rawSemantic(const String& label)
{
    return "aria.label."+String::toHexString(label.trim().toLowerCase().hashCode64());
}
inline std::vector<PatchSuggestion> reconcilePatch(const aria::Patch& p,const Catalog& vocabulary)
{
    std::vector<PatchSuggestion> result;
    if(!p.present||p.kind!="sfz")return result;
    const bool fresh=p.sourceCurrent();
    // Keep original labels; only a unique exact term/alias may supply a canonical ID.
    for(const auto& raw:p.articulations)
    {
        const auto label=raw.trim();
        if(label.isEmpty()||label.length()>80||result.size()>=128)continue;
        if(std::any_of(result.begin(),result.end(),[&](const auto& h){return h.label.equalsIgnoreCase(label);}))continue;
        auto semantic=vocabulary.exactTechnique(label);
        PatchSuggestion suggestion{label,semantic.isEmpty()?rawSemantic(label):semantic,fresh?"Label only":"Stale source — rescan",{},-1};
        std::set<int> notes;
        bool allCurrent=fresh, collision=false;
        for(const auto& hint:p.switchHints)
            if(hint.label.equalsIgnoreCase(label))
            {
                notes.insert(hint.note);allCurrent&=hint.current();
            }
        if(notes.size()==1)
            for(const auto& hint:p.switchHints)
                if(hint.note==*notes.begin()&&!hint.label.equalsIgnoreCase(label))collision=true;
        if(notes.size()>1||collision)suggestion.evidence="Conflicting literal switches — manual assignment";
        else if(!notes.empty()&&!allCurrent)suggestion.evidence="Changed definition — rescan";
        else if(notes.size()==1){suggestion.note=*notes.begin();suggestion.evidence="Literal SFZ hint — review and audition";}
        result.push_back(std::move(suggestion));
    }
    // Manufacturer-printed octave names are never translated implicitly.
    const auto docs=vocabulary.documentedHints();
    if(p.library=="Personal Orchestra 5")
        if(auto* rows=docs.getArray())for(const auto& row:*rows)
        {
            if(row["libraryId"].toString()!="garritan.gpo5"||!row["patchName"].toString().equalsIgnoreCase(p.name))continue;
            const auto bindings=row["bindings"];
            if(auto* values=bindings.getArray())for(const auto& b:*values)
            {
                if(result.size()>=128)break;
                const auto id=b["techniqueId"].toString();auto name=vocabulary.technique(id)["name"].toString();
                const auto qualifier=b["qualifier"].toString();
                if(qualifier.isNotEmpty())name+=" ("+qualifier+")";
                if(name.isEmpty()||name.length()>80)continue;
                const auto printed=b["printedNote"].toString();
                if(printed.isEmpty())continue;
                // If a source label already has that canonical meaning, preserve it,
                // but only attach the printed hint when its label is exact.
                auto found=std::find_if(result.begin(),result.end(),[&](const auto& h){return h.label.equalsIgnoreCase(name);});
                if(found!=result.end())
                {
                    if(found->printedNote.isEmpty())found->printedNote=printed;
                    else if(found->printedNote!=printed)found->printedNote="ambiguous";
                }
                else result.push_back({name,id,"GPO 5 manual — patch variant and octave unverified",printed,-1});
            }
        }
    return result;
}
inline juce::Result linkPatch(Profile& p,const aria::Patch& patch)
{
    if(!patch.sourceCurrent()||patch.kind!="sfz")return juce::Result::fail("Select a current readable SFZ patch; rescan if necessary.");
    if(!p.catalogPatchId.isEmpty()&&p.catalogPatchId!=patch.id)
        return juce::Result::fail("Different patch identity. Create a new profile instead of replacing your mappings.");
    if((p.library.isNotEmpty()&&!p.library.equalsIgnoreCase(patch.library))
       ||(p.patch.isNotEmpty()&&!p.patch.equalsIgnoreCase(patch.name)))
        return juce::Result::fail("Profile already names another library or patch. Create a new profile.");
    p.library=patch.library;p.patch=patch.name;p.catalogPatchId=patch.id;
    p.catalogLibraryVersion=patch.libraryVersion;p.catalogStamp=patchStamp(patch);
    return juce::Result::ok();
}
inline int findSuggestionElement(const Profile& p,const PatchSuggestion& suggestion)
{
    // Do not merge distinct source labels on a shared semantic ID.
    for(int i=0;i<(int)p.elements.size();++i)
        if(p.elements[(size_t)i].kind=="articulation"&&p.elements[(size_t)i].label.equalsIgnoreCase(suggestion.label))return i;
    return -1;
}
inline int addSuggestion(Profile& p,const PatchSuggestion& s)
{
    if(int i=findSuggestionElement(p,s);i>=0)return i;
    if(p.elements.size()>=128||s.label.isEmpty()||s.label.length()>80||s.semantic.isEmpty())return -1;
    p.elements.push_back({juce::Uuid().toString(),s.semantic,s.label,"articulation","button",true,true,Binding{}});
    return (int)p.elements.size()-1;
}
}
