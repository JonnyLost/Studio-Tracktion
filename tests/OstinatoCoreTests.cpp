// SPDX-License-Identifier: GPL-3.0-or-later
#include "../OstinatoCore.h"
#include "../OstinatoAudition.h"
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
    require(!decode("STUDIO_OSTINATO 5 \"x\" 16 4 1",restored)&&restored==p,"reject version");
    require(!decode("STUDIO_OSTINATO 1 \"x\" 16 4 99999",restored),"reject excessive rows");
    require(!decode("STUDIO_OSTINATO 1 \"x\" 1 4 1\n1 \"r\" \"t\" 1 60 0 0\n2 0 90 750\n",restored),"reject nonboolean flags");
    require(!p.setGrid(8,4)&&p.stepCount==16,"do not erase active steps during resize");
    auto extended=p;require(extended.setGrid(32,4)&&extended.rows[0].steps[15].enabled&&!extended.rows[0].steps[31].enabled,"extend grid without mutating old steps");
    require(!extended.setGrid(32,7),"legacy master grid rejects fractional ticks; use row timing");
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
    // O1 payload migration: retain the original grid and exact event stream.
    const std::string legacy="STUDIO_OSTINATO 1 \"Legacy\" 2 4 1\n1 \"Old row\" \"t\" 1 60 0 0\n1 1 80 500\n1 0 95 1000\n";
    Pattern migrated;require(decode(legacy,migrated)&&migrated.rows[0].timing==TimingMode::followGrid,"O1 migration");
    require(compile(migrated,{"t"},1,second,error)&&second.notes==std::vector<Note>{{0,1200,0,1,60,100},{2400,4800,0,1,60,95}},"legacy exact timing");
    require(decode(encode(migrated),restored)&&restored==migrated,"migrated v2 round trip");

    Pattern poly;poly.rows[0].trackId="taiko";poly.rows[0].pitch=36;
    require(poly.addRow()&&poly.addRow(),"three rows");
    poly.rows[1].trackId="claves";poly.rows[2].trackId="strings";
    require(poly.setRowTiming(1,TimingMode::sharedSpan,4)&&poly.setRowTiming(2,TimingMode::sharedSpan,5)
        &&poly.setRowTiming(3,TimingMode::sharedSpan,7),"4:5:7 shared span");
    for(auto& r:poly.rows)for(auto& step:r.steps){step.enabled=true;step.gate=1000;}
    require(compile(poly,{"taiko","claves","strings"},3,second,error)&&second.notes.size()==48,"shared span repeat counts");
    for(int route=0;route<3;++route)
    {
        const int count=route==0?4:route==1?5:7;
        int index=0;
        for(const auto& n:second.notes)if(n.route==route)
        {
            const Tick expectedStart=(index/count)*poly.duration()+(index%count)*poly.duration()/count;
            const Tick expectedEnd=(index/count)*poly.duration()+((index%count)+1)*poly.duration()/count;
            require(n.start==expectedStart&&n.end==expectedEnd,"absolute rational boundaries including cycle end");
            ++index;
        }
        require(index==3*count,"all row steps generated");
    }
    require(decode(encode(poly),restored)&&restored==poly,"O2 row timing round trip");
    auto changed=poly;
    require(!changed.setRowTiming(2,TimingMode::sharedSpan,4)&&changed==poly,"refuse truncating active row");
    require(!changed.setRowTiming(2,TimingMode::independentCycle,5,5,3)&&changed==poly,"reject invalid meter denominator");
    require(!changed.setRowTiming(2,static_cast<TimingMode>(99),5)&&changed==poly,"reject invalid timing mode");
    require(!changed.setRowTiming(2,TimingMode::followGrid,5),"follow-grid size must match");
    require(changed.setMasterMeter(6,8,1,4)&&changed.duration()==3*ticksPerQuarter
        &&changed.rows[1].steps.size()==5&&changed.rows[2].steps.size()==7,"master meter preserves independent row cells");
    require(compile(changed,{"taiko","claves","strings"},1,second,error)&&second.notes.back().end==changed.duration(),"shared span follows master meter");

    Pattern meters;meters.rows[0].trackId="a";require(meters.addRow(),"polymeter row");
    meters.rows[1].trackId="b";
    require(meters.setRowTiming(1,TimingMode::independentCycle,5,5,8)&&meters.setRowTiming(2,TimingMode::independentCycle,7,7,8),"5/8 and 7/8 cycles");
    meters.rows[0].steps[0].enabled=true;meters.rows[1].steps[0].enabled=true;
    require(meters.rowCycle(meters.rows[0])==24000&&meters.rowCycle(meters.rows[1])==33600,"meter duration");
    require(compile(meters,{"a","b"},5,second,error)&&second.duration==192000&&second.notes.size()==14,"independent cycles continue across master spans");
    int aCount=0,bCount=0,meeting=0;
    for(const auto& n:second.notes)
    {
        if(n.route==0)require(n.start==aCount++*24000,"5/8 phase");
        else require(n.start==bCount++*33600,"7/8 phase");
        if(n.start==168000)++meeting;
    }
    require(aCount==8&&bCount==6&&meeting==2,"5/8 and 7/8 realign at 35 eighth notes");
    auto independent=meters;
    require(independent.setMasterMeter(3,4,1,4)&&independent.rowCycle(independent.rows[0])==24000,"master change preserves independent cycle");
    require(decode(encode(meters),restored)&&restored==meters,"independent meter serialization");
    require(!decode("STUDIO_OSTINATO 2 \"x\" 1 4 1\n1 \"r\" \"t\" 1 60 0 0 2 4 0 1 1\n1 0 90 750\n",restored)&&restored==meters,"invalid meter fails transactionally");
    require(!decode("STUDIO_OSTINATO 2 \"x\" 1 4 1\n1 \"r\" \"t\" 1 60 0 0 2 4 4 1 257\n",restored),"row step limit");

    // Mixed subdivisions on one MIDI pitch must not leave overlapping notes.
    for(auto& r:poly.rows){r.trackId="t";r.pitch=60;}
    require(compile(poly,{"t"},4,second,error),"same destination poly rhythms");
    for(size_t i=1;i<second.notes.size();++i)require(second.notes[i-1].end<=second.notes[i].start,"overlap normalization across distinct subdivisions");
    require(second.notes.back().end==second.duration,"exact terminal boundary");
    emitted.clear();Cursor mixed(second);
    for(Tick t=0;t<=second.duration;t+=353)
        require(mixed.process(t,std::min(t+353,second.duration+1),sink),"mixed subdivision block");
    require(emitted==second.events&&mixed.held()==0,"mixed subdivision event lifecycle partition invariant");

    Pattern bounded;bounded.rows[0].trackId="t";
    require(bounded.setRowTiming(1,TimingMode::independentCycle,256,1,32),"short dense independent cycle");
    for(auto& step:bounded.rows[0].steps)step.enabled=true;
    require(!compile(bounded,{"t"},256,second,error),"independent dense cycle respects output bound");
    // Long cycle clipped to a finite print/audition range.
    Pattern clipped;clipped.rows[0].trackId="t";
    require(clipped.setRowTiming(1,TimingMode::independentCycle,1,7,4),"long cycle");
    clipped.rows[0].steps[0]={true,false,90,1000};
    require(compile(clipped,{"t"},1,second,error)&&second.notes.size()==1&&second.notes[0].end==clipped.duration(),"clip partial cycle at finite render end");
    // Exhaust all subdivision counts and check the matrix geometry agrees
    // with the generated note boundaries over two cycles.
    Pattern sweep;sweep.rows[0].trackId="t";
    require(sweep.setMasterMeter(7,8,1,4),"7/8 master span");
    for(int cells=1;cells<=maxSteps;++cells)
    {
        for(auto& step:sweep.rows[0].steps)step.enabled=false;
        require(sweep.setRowTiming(1,TimingMode::sharedSpan,cells),"all supported divisions");
        auto& row=sweep.rows[0];
        for(auto& step:row.steps){step.enabled=true;step.gate=1000;}
        require(compile(sweep,{"t"},2,second,error)&&second.notes.size()==size_t(2*cells),"division sweep count");
        for(int i=0;i<cells*2;++i)
        {
            const auto origin=(i/cells)*sweep.duration();
            require(second.notes[size_t(i)].start==origin+sweep.rowBoundary(row,size_t(i%cells))
                &&second.notes[size_t(i)].end==origin+sweep.rowBoundary(row,size_t(i%cells+1)),"matrix geometry matches engine");
        }
    }
    Pattern melody;melody.rows[0].trackId="t";melody.rows[0].pitchMode=PitchMode::intervals;
    melody.rows[0].steps[0]={true,false,90,500,0};
    melody.rows[0].steps[1]={true,false,90,500,7};
    melody.rows[0].steps[2]={true,false,90,500,-5};
    Render melodic;
    require(compile(melody,{"t"},1,melodic,error)&&melodic.notes.size()==3,"interval melody render");
    require(melodic.notes[0].pitch==60&&melodic.notes[1].pitch==67&&melodic.notes[2].pitch==55,"ascending and descending intervals");
    require(decode(encode(melody),restored)&&restored==melody,"v3 melody round trip");
    const std::string v2="STUDIO_OSTINATO 2 \"Old\" 1 4 1\n1 \"r\" \"t\" 1 60 0 0 1 4 4 1 1\n1 0 90 750\n";
    require(decode(v2,restored)&&restored.rows[0].pitchMode==PitchMode::fixed&&restored.rows[0].steps[0].interval==0,"v2 migration");
    auto invalid=melody;invalid.rows[0].pitch=127;
    require(!invalid.valid()&&!compile(invalid,{"t"},1,melodic,error),"reject out-of-range melody transactionally");
    TimedRender timed;
    auto clock=[](Tick t){return 10.0+double(t)/ticksPerQuarter*0.5;};
    require(prepareAudition(melodic,clock,timed)&&timed.duration==2.0&&timed.deadlines[0]==0,"tempo-mapped deadlines with origin");
    std::vector<Event> sent;
    auto output=[&](const Event& e){sent.push_back(e);};
    AuditionCursor audition(timed);
    for(int ms=0;ms<=2000;++ms)audition.advance(ms/1000.0,output);
    require(audition.completed()&&sent==melodic.events,"audition normal dispatch");
    sent.clear();AuditionCursor late(timed);
    require(late.advance(0,output)&&!late.advance(.5,output)&&sent.size()==2&&sent[0].on&&!sent[1].on,"late callback stops rather than bursting");
    sent.clear();AuditionCursor stopped(timed);
    require(stopped.advance(0,output),"start finite audition");stopped.stop(output);stopped.stop(output);
    require(sent.size()==2&&!sent.back().on,"stop releases once");
    require(!prepareAudition(melodic,[](Tick){return 0.0;},timed),"reject invalid tempo map");
    // O5: older patterns have no optional steps, so full intensity/density
    // reproduces the exact previously compiled MIDI.
    const std::string v3="STUDIO_OSTINATO 3 \"Old\" 1 4 1\n1 \"r\" \"t\" 1 60 0 0 1 4 4 1 1 0\n1 0 90 750 0\n";
    require(decode(v3,restored)&&restored.rows[0].steps[0].densityThreshold==0,"v3 density migration");
    Pattern conducted;conducted.rows[0].trackId="t";
    conducted.rows[0].steps[0]={true,false,100,1000,0,0};
    conducted.rows[0].steps[1]={true,false,100,1000,0,64};
    conducted.rows[0].steps[2]={true,true,100,1000,0,127};
    Render planned;
    require(compile(conducted,{"t"},1,planned,error)&&planned.notes.size()==3,"compile full density superset");
    require(decode(encode(conducted),restored)&&restored==conducted,"density tier persistence");
    require(conductedVelocity(100,0)==25&&conductedVelocity(100,127)==100&&conductedVelocity(1,0)==1,"velocity range and floor");
    ConductingMailbox mailbox;
    mailbox.receive(1,10);mailbox.receive(1,20);mailbox.receive(11,0);mailbox.receive(7,127);mailbox.receive(11,128);
    require(mailbox.consume({127,127})==ConductingValues{20,0},"latest CC mailbox and zero value");
    require(mailbox.consume({30,40})==ConductingValues{30,40},"consume only new values");
    mailbox.receive(1,99);mailbox.reset();require(mailbox.consume({7,8})==ConductingValues{7,8},"detach clears stale values");
    TimedRender conductedTimed;
    require(prepareAudition(planned,[](Tick t){return double(t)/ticksPerQuarter;},conductedTimed),"prepare conducted render");
    sent.clear();AuditionCursor live(conductedTimed);
    for(int ms=0;ms<=4000;++ms)
    {
        // Skip tier 64 at its onset. Raise density later; the skipped
        // note-off must not leak. The essential note remains until its end.
        const int den=ms<400?0:127;
        const int intensity=ms<500?0:127;
        live.advance(ms/1000.0,ConductingValues{intensity,den},output);
    }
    require(live.completed()&&sent.size()==4&&sent[0].on&&sent[0].velocity==25
        &&!sent[1].on&&sent[2].on&&sent[2].velocity==120&&!sent[3].on,"live thresholds and note-off ownership");
    sent.clear();AuditionCursor quiet(conductedTimed);
    for(int ms=0;ms<=4000;++ms)quiet.advance(ms/1000.0,ConductingValues{127,0},output);
    require(quiet.completed()&&sent.size()==2&&sent.front().on&&!sent.back().on,"zero density preserves essential beat");
    auto conflicting=conducted;require(conflicting.addRow(),"add conflicting row");
    conflicting.rows[1].trackId="t";conflicting.rows[1].steps[0]={true,false,90,1000,0,64};
    require(!compile(conflicting,{"t"},1,planned,error),"reject cross-tier overlap");
    conflicting.rows[1].pitch=61;
    require(compile(conflicting,{"t"},1,planned,error),"different pitch optional row valid");
    std::cout<<"O1–O5 ostinato core: "<<checks<<" checks passed\n";
}
