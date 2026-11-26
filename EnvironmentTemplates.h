// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SpatialModel.h"
#include "StudioActivities.h"
#include <string_view>

namespace studio::environments
{
using spatial::Section;
using spatial::Seating;
enum class TemplateId { empty, symphony, film, jazz, electronic, chamber };
struct TrackPlan { const char* name; Section section; const char* icon; bool stage; };
inline constexpr TrackPlan emptyTracks[] {{ "Instrument 1", Section::other, "Other", false }};
inline constexpr TrackPlan symphonyTracks[] {
    {"Flutes",Section::woodwinds,"Woodwind",true},{"Oboes",Section::woodwinds,"Woodwind",true},
    {"Clarinets",Section::woodwinds,"Woodwind",true},{"Bassoons",Section::woodwinds,"Woodwind",true},
    {"Horns",Section::brass,"Brass",true},{"Trumpets",Section::brass,"Brass",true},
    {"Trombones",Section::brass,"Brass",true},{"Tuba",Section::brass,"Brass",true},
    {"Timpani",Section::percussion,"Percussion",true},{"Percussion",Section::percussion,"Percussion",true},
    {"Harp",Section::keys,"Other",true},{"First Violins",Section::strings,"Strings",true},
    {"Second Violins",Section::strings,"Strings",true},{"Violas",Section::strings,"Strings",true},
    {"Cellos",Section::strings,"Strings",true},{"Double Basses",Section::strings,"Strings",true}
};
inline constexpr TrackPlan filmTracks[] {
    {"High Woodwinds",Section::woodwinds,"Woodwind",true},{"Low Woodwinds",Section::woodwinds,"Woodwind",true},
    {"Horns",Section::brass,"Brass",true},{"Trumpets",Section::brass,"Brass",true},
    {"Low Brass",Section::brass,"Brass",true},{"Orchestral Percussion",Section::percussion,"Percussion",true},
    {"Piano",Section::keys,"Keyboard",true},{"First Violins",Section::strings,"Strings",true},
    {"Second Violins",Section::strings,"Strings",true},{"Violas",Section::strings,"Strings",true},
    {"Cellos",Section::strings,"Strings",true},{"Double Basses",Section::strings,"Strings",true}
};
inline constexpr TrackPlan jazzTracks[] {
    {"Piano",Section::keys,"Keyboard",true},{"Upright Bass",Section::other,"Bass",true},
    {"Drums",Section::percussion,"Percussion",true},{"Lead",Section::woodwinds,"Woodwind",true}
};
inline constexpr TrackPlan electronicTracks[] {
    {"Lead Synth",Section::keys,"Synth",false},{"Pad",Section::keys,"Synth",false},
    {"Bass Synth",Section::other,"Synth",false},{"Drums",Section::percussion,"Percussion",false}
};
inline constexpr TrackPlan chamberTracks[] {
    {"First Violin",Section::strings,"Strings",true},{"Second Violin",Section::strings,"Strings",true},
    {"Viola",Section::strings,"Strings",true},{"Cello",Section::strings,"Strings",true}
};
struct TemplatePlan
{
    TemplateId id; const char* key; const char* name; const TrackPlan* tracks; int count;
    Activity activity; Seating seating; int room; int bpm;
};
inline constexpr std::array templates {
    TemplatePlan{TemplateId::empty,"empty","Empty Studio",emptyTracks,1,Activity::compose,Seating::concert,-1,120},
    TemplatePlan{TemplateId::symphony,"symphony","Symphony Orchestra",symphonyTracks,16,Activity::compose,Seating::concert,0,100},
    TemplatePlan{TemplateId::film,"film","Film Scoring",filmTracks,12,Activity::score,Seating::scoring,3,100},
    TemplatePlan{TemplateId::jazz,"jazz","Jazz Studio",jazzTracks,4,Activity::record,Seating::chamber,6,120},
    TemplatePlan{TemplateId::electronic,"electronic","Electronic Studio",electronicTracks,4,Activity::design,Seating::chamber,4,120},
    TemplatePlan{TemplateId::chamber,"chamber","Chamber Music",chamberTracks,4,Activity::compose,Seating::chamber,1,100}
};
inline constexpr const TemplatePlan& plan (TemplateId id)
{
    for (const auto& p : templates) if (p.id == id) return p;
    return templates.front();
}
}
