// SPDX-License-Identifier: GPL-3.0-or-later
#include "../OstinatoCore.h"
#include <iostream>
#include <cstdlib>

using namespace studio::ostinato;
static int checks=0;
static void require(bool ok,const char* what)
{
    ++checks;
    if(!ok){std::cerr<<"FAIL: "<<what<<"\n";std::exit(1);}
}

int main()
{
    Pattern p;
    require(p.valid()&&p.rows.size()==1&&p.stepCount==16&&p.duration()==4*ticksPerQuarter,"blank one-row default");
    require(!p.removeRow(1),"retain minimum row");
    require(p.addRow()&&p.rows[1].id!=p.rows[0].id,"add independent row");
    p.rows[0].trackId="taiko";p.rows[0].pitch=36;p.rows[0].channel=10;
    p.rows[0].steps[0]={true,true,115,1000};p.rows[0].steps[1]={true,false,65,1000};
    p.rows[0].steps[15]={true,false,90,1000};
    p.rows[1].trackId="strings";p.rows[1].pitch=60;
    p.rows[1].steps[0]={true,false,72,500};
    p.rows[1].steps[4]={true,false,77,750};
    p.name="Taiko / \"Strings\"";
    const auto encoded=encode(p);Pattern restored;
    require(!encoded.empty()&&decode(encoded,restored)&&restored==p,"versioned round trip");
    require(!decode(encoded+"garbage",restored)&&restored==p,"reject trailing payload transactionally");
    require(!decode("STUDIO_OSTINATO 2 \"x\" 16 4 1",restored)&&restored==p,"reject version");
    require(!decode("STUDIO_OSTINATO 1 \"x\" 16 4 99999",restored),"reject excessive rows");
    require(!decode("STUDIO_OSTINATO 1 \"x\" 1 4 1\n1 \"r\" \"t\" 1 60 0 0\n2 0 90 750\n",restored),"reject nonboolean flags");
    require(!p.setGrid(8,4)&&p.stepCount==16,"do not erase active steps during resize");
    auto extended=p;require(extended.setGrid(32,4)&&extended.rows[0].steps[15].enabled&&!extended.rows[0].steps[31].enabled,"extend grid without mutating old steps");
    require(!extended.setGrid(32,7),"O1 rejects fractional ticks rather than drifting");
    auto malformed=p;malformed.rows[1].id=malformed.rows[0].id;
    require(!malformed.valid()&&encode(malformed).empty(),"reject duplicate row IDs");
    malformed=p;malformed.rows[0].steps[0].gate=0;require(!malformed.valid(),"reject zero gate");
    malformed=p;malformed.rows[0].pitch=128;require(!malformed.valid(),"reject invalid pitch");

    Render render;std::string error;
    require(compile(p,{"taiko","strings"},2,render,error)&&error.empty(),"compile two tracks");
    require(render.routes.size()==2&&render.notes.size()==10&&render.events.size()==20,"routes and exact event counts");
    require(render.duration==8*ticksPerQuarter&&render.notes.front().velocity==127,"accent saturation and duration");
    require(render.notes.front().start==0&&render.notes.front().end==2400,"full gate beat timing");
    require(render.notes[1].end==1200,"half gate");
    require(render.notes.back().end<=render.duration,"end bound");
    for(size_t i=1;i<render.events.size();++i)
    {
        require(render.events[i-1].tick<=render.events[i].tick,"ordered events");
        if(render.events[i-1].tick==render.events[i].tick)
            require(!render.events[i-1].on||render.events[i].on,"off before on at same tick");
    }
    Render second;
    require(compile(p,{"taiko","strings"},2,second,error)&&second.events==render.events&&encode(p)==encoded,"deterministic and immutable render");
    require(!compile(p,{"taiko"},2,second,error)&&second.events==render.events,"missing destination fails transactionally");
    require(!compile(p,{"taiko","strings"},0,second,error),"invalid repeat count");

    auto unassigned=p;unassigned.rows[1].trackId.clear();
    require(compile(unassigned,{"taiko"},1,second,error)&&second.unassignedRows==1&&second.notes.size()==3,"no fallback for unassigned");
    auto solo=p;solo.rows[1].solo=true;
    require(compile(solo,{"strings"},1,second,error)&&second.notes.size()==2&&second.routes[0]=="strings","solo routing");
    solo.rows[1].mute=true;
    require(compile(solo,{},1,second,error)&&second.notes.empty(),"mute overrides solo");
    auto same=p;same.rows[1].trackId="taiko";same.rows[1].channel=10;same.rows[1].pitch=36;
    require(compile(same,{"taiko"},1,second,error)&&second.routes.size()==1&&second.notes.size()==4,"merge simultaneous same-pitch hits");
    require(second.notes[0].velocity==127&&second.notes[0].end==2400,"merge maximum velocity and gate");
    same.rows[1].channel=11;
    require(compile(same,{"taiko"},1,second,error)&&second.notes.size()==5,"separate MIDI channels");
    same.rows[1].trackId="strings";same.rows[1].channel=10;
    require(compile(same,{"taiko","strings"},1,second,error)&&second.notes.size()==5,"separate track destinations");

    std::vector<Event> emitted;emitted.reserve(render.events.size()+64);
    const auto sink=[&](const Event& e){emitted.push_back(e);};
    Cursor cursor(render);
    for(Tick t=0;t<render.duration+1;t+=137)
        require(cursor.process(t,std::min(t+137,render.duration+1),sink),"contiguous arbitrary block partition");
    require(emitted==render.events&&cursor.held()==0,"block partition equals complete render including final note off");
    emitted.clear();
    require(cursor.process(0,1,sink)&&cursor.held()==2,"seek begins with exact boundary events");
    cursor.stop(1,sink);
    require(cursor.held()==0&&emitted.size()==4&&!emitted[2].on&&!emitted[3].on,"stop releases owned notes only");
    emitted.clear();
    require(cursor.process(0,1,sink)&&cursor.process(1300,1301,sink)&&cursor.held()==0,"discontinuity releases without chasing old notes");
    require(emitted.size()==4&&!emitted[2].on&&!emitted[3].on&&emitted[2].tick==1300,"seek releases at new position");
    require(cursor.process(2400,2401,sink)&&cursor.held()==1&&emitted.back().on,"ignore orphan note off after seek");
    require(!cursor.process(2401,2400,sink)&&cursor.held()==0&&!emitted.back().on,"invalid block releases held notes");

    Pattern stress;require(stress.setGrid(256,4),"max grid");
    stress.rows[0].trackId="t";
    for(auto& s:stress.rows[0].steps)s.enabled=true;
    for(int i=1;i<maxRows;++i){require(stress.addRow(),"bounded row growth");stress.rows.back().trackId="t";for(auto& s:stress.rows.back().steps)s.enabled=true;}
    require(!stress.addRow(),"row cap");
    require(!compile(stress,{"t"},maxRepeats,second,error),"reject oversized finite render");
    require(compile(stress,{"t"},1,second,error)&&second.notes.size()==256,"dense duplicate hits normalized");
    require(stress.removeRow(stress.rows.back().id)&&stress.rows.size()==63,"delete selected row");
    std::cout<<"O1 ostinato core: "<<checks<<" checks passed\n";
}
