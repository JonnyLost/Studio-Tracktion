// SPDX-License-Identifier: GPL-3.0-or-later
#include "../OstinatoCore.h"
#include "../OstinatoAudition.h"
#include "../OstinatoLibrary.h"
#include "../OstinatoPrint.h"
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
    require(!decode("STUDIO_OSTINATO 9 \"x\" 16 4 1",restored)&&restored==p,"reject version");
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
    ConductingMapping mapping{21,22};
    require(mapping.valid()&&mapping.role(21)==1&&mapping.role(22)==11&&mapping.role(1)==-1,"custom controller role mapping");
    require(!ConductingMapping{21,21}.valid()&&!ConductingMapping{64,11}.valid()&&!ConductingMapping{1,123}.valid(),"reject duplicate or reserved controllers");
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
    Pattern details;details.rows[0].trackId="t";
    auto& steps=details.rows[0].steps;
    steps[0].enabled=true;steps[0].sustainCells=3;steps[0].gate=500;steps[0].articulation="sustain";
    steps[4].enabled=true;steps[4].strikes=3;steps[4].gate=1000;steps[4].articulation="short";
    steps[5].enabled=true;steps[5].offset=-250;steps[5].gate=500;steps[5].articulation="short";
    steps[8].enabled=true;steps[8].chance=0;
    Render advanced;
    require(compile(details,{"t"},1,advanced,error)&&advanced.notes.size()==5,"sustain ratchet and zero chance");
    require(advanced.notes.front().start==0&&advanced.notes.front().end==6000,"tie through two full cells and half final cell");
    require(advanced.notes[1].start==9600&&advanced.notes[2].start==10400&&advanced.notes[3].start==11200,"three exact ratchet starts");
    require(advanced.notes[4].start==11400&&advanced.notes[3].end==11400,"early step safely retriggers");
    require(advanced.articulations==std::vector<std::string>{"sustain","short"}&&advanced.events.front().articulation==1,"stable articulation references");
    require(decode(encode(details),restored)&&restored==details,"schema 5 advanced details roundtrip");
    auto bad=details;bad.rows[0].steps[1].enabled=true;require(!bad.valid(),"tie continuation must be inactive");
    bad=details;bad.rows[0].steps[0].strikes=2;require(!bad.valid(),"tie cannot ratchet");
    bad=details;bad.rows[0].steps[15].enabled=true;bad.rows[0].steps[15].sustainCells=2;require(!bad.valid(),"tie cannot cross row cycle");
    const std::string v4="STUDIO_OSTINATO 4 \"Old\" 1 4 1\n1 \"r\" \"t\" 1 60 0 0 1 4 4 1 1 0\n1 0 90 750 0 0\n";
    require(decode(v4,restored)&&restored.rows[0].steps[0].chance==100&&restored.rows[0].steps[0].strikes==1,"schema 4 migration");
    auto random=details;random.rows[0].steps[8].chance=50;
    Render a6,b6;
    require(compile(random,{"t"},16,a6,error)&&compile(random,{"t"},16,b6,error)&&a6.events==b6.events,"repeatable probability");
    auto conflict=details;require(conflict.addRow(),"add conflict row");
    conflict.rows[1].trackId="t";conflict.rows[1].pitch=67;conflict.rows[1].steps[0].enabled=true;
    conflict.rows[1].steps[0].articulation="short";
    require(!compile(conflict,{"t"},1,b6,error),"reject simultaneous differing techniques on shared channel");
    conflict.rows[1].channel=2;
    require(compile(conflict,{"t"},1,b6,error),"independent articulation channels");
    Harmony cMajor;
    require(resolvePitch({PitchMode::scaleDegree,60,2},cMajor)==64&&resolvePitch({PitchMode::scaleDegree,60,-1},cMajor)==59,"signed scale degrees");
    Harmony dMinor{2,Scale::naturalMinor,0x089};
    require(resolvePitch({PitchMode::scaleDegree,60,2},dMinor)==65&&resolvePitch({PitchMode::chordDegree,60,2},dMinor)==69,"D minor scale and chord");
    require(resolvePitch({PitchMode::followRoot,60,7},dMinor)==69,"live root relative interval");
    for(int i=0;i<6;++i)
    {
        const int up[]={60,64,67,60,64,67},down[]={67,64,60,67,64,60},bounce[]={60,64,67,64,60,64};
        require(resolvePitch({PitchMode::arpUp,60,i},cMajor)==up[i],"ascending arp");
        require(resolvePitch({PitchMode::arpDown,60,i},cMajor)==down[i],"descending arp");
        require(resolvePitch({PitchMode::arpBounce,60,i},cMajor)==bounce[i],"bouncing arp");
    }
    Pattern harmonyPattern;harmonyPattern.rows[0].trackId="t";harmonyPattern.rows[0].pitchMode=PitchMode::chordDegree;
    harmonyPattern.rows[0].steps[0].enabled=true;harmonyPattern.rows[0].steps[0].gate=1000;
    harmonyPattern.rows[0].steps[1].enabled=true;harmonyPattern.rows[0].steps[1].interval=2;
    harmonyPattern.harmony=dMinor;
    Render harmonyRender;
    require(compile(harmonyPattern,{"t"},1,harmonyRender,error)&&harmonyRender.notes.size()==2&&harmonyRender.notes[0].pitch==62&&harmonyRender.notes[1].pitch==69,"authored chord rendering");
    require(decode(encode(harmonyPattern),restored)&&restored==harmonyPattern,"schema 6 round trip");
    const std::string v5="STUDIO_OSTINATO 5 \"Old\" 1 4 1\n1 \"r\" \"t\" 1 60 0 0 1 4 4 1 1 0\n1 0 90 750 0 0 1 1 0 100 \"\"\n";
    require(decode(v5,restored)&&restored.harmony==cMajor&&restored.rows[0].pitchMode==PitchMode::fixed,"schema 5 migration");
    HarmonyMailbox keys;HarmonyLatch latch;Harmony current;
    keys.note(62,true);keys.note(65,true);keys.note(69,true);
    current=latch.consume(keys.snapshot(),0,current);require(current==cMajor,"chord attack settling");
    current=latch.consume(keys.snapshot(),.04,current);require(current.root==2&&current.chord==0x089,"held chord bass and intervals");
    keys.releaseAll();current=latch.consume(keys.snapshot(),.1,current);require(current.root==2&&current.chord==0x089,"release latches chord");
    keys.note(64,true);keys.note(68,true);keys.note(71,true);
    current=latch.consume(keys.snapshot(),.2,current);current=latch.consume(keys.snapshot(),.24,current);
    require(current.root==4&&current.chord==0x091,"new chord");
    TimedRender harmonicTimed;require(prepareAudition(harmonyRender,[](Tick t){return double(t)/ticksPerQuarter;},harmonicTimed),"prepare harmonic audition");
    std::vector<Event> heard;AuditionCursor harmonicCursor(harmonicTimed);
    auto hear=[&](const Event& e){heard.push_back(e);};
    require(harmonicCursor.advance(0,ConductingValues{},cMajor,hear)&&heard.size()==1&&heard[0].pitch==60,"live first attack");
    require(harmonicCursor.advance(.25,ConductingValues{},dMinor,hear)&&heard.size()==3&&!heard[1].on&&heard[1].pitch==60&&heard[2].pitch==69,"old pitch releases across chord change");
    harmonicCursor.stop(hear);require(heard.size()==4&&!heard.back().on&&heard.back().pitch==69,"stop releases resolved pitch");
    auto collision=harmonyPattern;collision.harmony=cMajor;require(collision.addRow(),"add collision row");
    collision.rows[1].trackId="t";collision.rows[1].pitch=62;collision.rows[1].steps[0].enabled=true;
    require(compile(collision,{"t"},1,harmonyRender,error)&&prepareAudition(harmonyRender,[](Tick t){return double(t)/ticksPerQuarter;},harmonicTimed),"prepare possible live collision");
    AuditionCursor collided(harmonicTimed);heard.clear();
    require(collided.advance(0,ConductingValues{},dMinor,hear)&&collided.skippedNotes()==1&&heard.size()==1,"live pitch collision skipped");
    collided.stop(hear);require(heard.size()==2&&heard[0].pitch==heard[1].pitch,"collision retains ownership");
    Harmony recognize=cMajor;recognize.recognizeInversions=true;
    std::array<bool,128> inversion{};
    inversion[52]=inversion[55]=inversion[60]=true;
    require(harmonyFromKeys(inversion,cMajor).root==4,"legacy bass-relative interpretation");
    require(harmonyFromKeys(inversion,recognize)==recognize,"recognize C major first inversion");
    for(int root=0;root<12;++root)
    {
        for(unsigned quality:{0x091u,0x089u,0x491u,0x891u,0x489u})
        {
            std::array<bool,128> voiced{};
            // Put the third in the bass, retain all chord tones in the upper octave.
            const int third=(quality&8)?3:4;
            voiced[size_t(36+root+third)]=true;
            for(int i=0;i<12;++i)if(quality&(1u<<i))voiced[size_t(60+root+i)]=true;
            auto inferred=harmonyFromKeys(voiced,recognize);
            require(inferred.root==root&&inferred.chord==quality,"all roots and common inverted triads/sevenths");
        }
    }
    inversion={};inversion[52]=inversion[56]=inversion[60]=true;
    require(harmonyFromKeys(inversion,recognize).root==4,"ambiguous augmented chord retains bass");
    inversion={};
    require(harmonyFromKeys(inversion,recognize)==recognize,"empty chord retains last harmony");
    auto savedHarmony=harmonyPattern;savedHarmony.harmony.recognizeInversions=true;
    require(decode(encode(savedHarmony),restored)&&restored==savedHarmony,"schema 7 root rule round trip");
    const std::string v6="STUDIO_OSTINATO 6 \"Old\" 1 4 1 0 0 145\n1 \"r\" \"t\" 1 60 0 0 1 4 4 1 1 0\n1 0 90 750 0 0 1 1 0 100 \"\"\n";
    require(decode(v6,restored)&&restored.harmony==cMajor,"schema 6 keeps bass-relative rule");
    Pattern variation;variation.rows[0].trackId="t";
    auto& vs=variation.rows[0].steps;
    vs[0].enabled=true;vs[0].variationMask=1;
    vs[1].enabled=true;vs[1].variationMask=2;
    vs[2].enabled=true;vs[2].variationMask=4;
    vs[3].enabled=true;vs[3].variationMask=8;
    vs[4].enabled=true;vs[4].variationMask=16;
    vs[5].enabled=true;vs[5].densityThreshold=100;
    variation.variations.sequence="ABCD";variation.variations.fillEvery=2;
    variation.variations.densityStart=0;variation.variations.densityEnd=127;
    Render vr;
    require(compile(variation,{"t"},4,vr,error)&&vr.notes.size()==7,"A–D sequence, additive fills and density ramp");
    const auto span=variation.duration(),cellTicks=variation.stepTicks();
    const std::vector<Tick> starts{0,span+cellTicks,span+4*cellTicks,2*span+2*cellTicks,3*span+3*cellTicks,3*span+4*cellTicks,3*span+5*cellTicks};
    for(size_t i=0;i<starts.size();++i)require(vr.notes[i].start==starts[i],"exact master-cycle switch");
    require(decode(encode(variation),restored)&&restored==variation,"schema 8 roundtrip");
    const std::string v7="STUDIO_OSTINATO 7 \"Old\" 1 4 1 0 0 145 0\n1 \"r\" \"t\" 1 60 0 0 1 4 4 1 1 0\n1 0 90 750 0 0 1 1 0 100 \"\"\n";
    require(decode(v7,restored)&&restored.variations==VariationPlan{}&&restored.rows[0].steps[0].variationMask==15,"schema 7 preserves notes in all variations");
    auto invalidVariation=variation;invalidVariation.variations.sequence="AE";
    require(!invalidVariation.valid(),"reject unknown variation");
    invalidVariation=variation;invalidVariation.rows[0].steps[0].variationMask=32;
    require(!invalidVariation.valid(),"reject invalid membership");
    variation.variations.timing=200;variation.variations.velocity=30;variation.variations.seed=42;
    Render humanA,humanB;
    require(compile(variation,{"t"},4,humanA,error)&&compile(variation,{"t"},4,humanB,error)&&humanA.events==humanB.events,"seeded humanization reproducible");
    require(humanA.notes.size()==vr.notes.size(),"humanization preserves these attacks");
    for(size_t i=0;i<humanA.notes.size();++i)
    {
        const auto& n=humanA.notes[i];const auto& original=vr.notes[i];
        require(std::abs(n.start-original.start)<=cellTicks/5&&n.start/span==original.start/span,"timing bounded and master transitions intact");
        require(std::abs(n.velocity-original.velocity)<=30&&n.start<n.end&&n.end<=humanA.duration,"bounded velocity and duration");
    }
    variation.variations.seed=43;
    require(compile(variation,{"t"},4,humanB,error)&&humanA.events!=humanB.events,"different seeds vary playback");
    Pattern variationPoly;variationPoly.rows[0].trackId="t";variationPoly.variations.sequence="AB";
    require(variationPoly.setRowTiming(1,TimingMode::independentCycle,3,3,4,1),"independent 3/4 row");
    for(auto& step:variationPoly.rows[0].steps){step.enabled=true;step.variationMask=2;}
    require(compile(variationPoly,{"t"},2,vr,error)&&vr.notes.size()==4,"independent cycle retains phase at master switch");
    require(vr.notes.front().start==4*ticksPerQuarter&&vr.notes.back().start==7*ticksPerQuarter,"master switch does not restart polyrhythm");
    auto portableSource=variation;
    portableSource.rows[0].trackId="project-123";
    portableSource.rows[0].name="Cellos";
    portableSource.rows[0].steps[0].articulation="local-keyswitch-id";
    const auto originalSource=portableSource;
    Pattern loadedPreset;
    require(decodePreset(encodePreset(portableSource),loadedPreset),"portable preset roundtrip");
    require(loadedPreset==portablePattern(portableSource)&&portableSource==originalSource,"portability strips local IDs without modifying source");
    require(loadedPreset.rows[0].trackId.empty()&&loadedPreset.rows[0].steps[0].articulation.empty()&&loadedPreset.rows[0].name=="Cellos","preserve roles, never auto-route");
    require(decodePreset(encode(portableSource),loadedPreset)&&loadedPreset==portablePattern(portableSource),"raw project pattern import sanitized too");
    const auto priorPreset=loadedPreset;
    require(!decodePreset("broken",loadedPreset)&&loadedPreset==priorPreset,"bad import leaves output unchanged");
    require(!decodePreset(std::string(maxPresetBytes+1,'x'),loadedPreset)&&loadedPreset==priorPreset,"oversized import refused");
    require(!presetUtf8(std::string("\xc0\xaf"))&&!presetUtf8(std::string("\xed\xa0\x80"))&&!presetUtf8(std::string("\xf4\x90\x80\x80")),"reject malformed UTF-8");
    require(presetUtf8("Viola – pizzicato"),"allow valid international text");
    require(decodePreset(v7,loadedPreset)&&loadedPreset.variations==VariationPlan{}&&loadedPreset.rows[0].trackId.empty(),"old patterns migrate as portable presets");
    auto library=factoryPatterns();std::set<std::string> factoryIds,factoryNames;
    require(library.size()==16,"16 original factory patterns");
    for(const auto& entry:library)
    {
        require(factoryIds.insert(entry.id).second&&factoryNames.insert(entry.pattern.name).second,"unique factory identity and name");
        require(entry.pattern.valid()&&!entry.category.empty()&&!entry.description.empty(),"valid factory metadata and pattern");
        require(decodePreset(encodePreset(entry.pattern),loadedPreset)&&loadedPreset==entry.pattern,"factory portable roundtrip");
        Pattern routed=entry.pattern;std::vector<std::string> destinations;
        for(auto& r:routed.rows)
        {
            require(r.trackId.empty()&&!r.name.empty(),"factory roles unassigned");
            for(const auto& step:r.steps)require(step.articulation.empty(),"no invented keyswitch mapping");
            r.trackId="test-"+std::to_string(r.id);destinations.push_back(r.trackId);
        }
        Render factoryRender,repeatRender;
        require(compile(routed,destinations,8,factoryRender,error)&&!factoryRender.notes.empty(),"factory renders eight cycles when routed");
        require(compile(routed,destinations,8,repeatRender,error)&&repeatRender.events==factoryRender.events,"factory rendering deterministic");
        require(compile(entry.pattern,{},8,repeatRender,error)&&repeatRender.events.empty(),"unassigned factory never emits MIDI");
    }
    Pattern printSource;
    printSource.rows[0].trackId="strings";printSource.rows[0].steps[0].enabled=true;
    printSource.rows[0].steps[0].articulation="pizz";printSource.rows[0].steps[2].enabled=true;
    printSource.rows[0].steps[2].articulation="pizz";
    printSource.rows[0].steps[4].enabled=true;printSource.rows[0].steps[4].articulation="cc";
    printSource.rows[0].steps[6].enabled=true;printSource.rows[0].steps[6].densityThreshold=100;
    require(printSource.addRow(),"second print channel");
    printSource.rows[1].trackId="strings";printSource.rows[1].channel=2;printSource.rows[1].pitch=72;
    printSource.rows[1].steps[1].enabled=true;
    require(printSource.addRow(),"second print destination");
    printSource.rows[2].trackId="drums";printSource.rows[2].pitch=36;printSource.rows[2].steps[0].enabled=true;
    Render printRender;
    require(compile(printSource,{"strings","drums"},2,printRender,error),"compile multi-destination print");
    std::vector<PrintMapping> printMappings{{0,1,1,26,100,true},{0,2,1,20,70,false}};
    PrintPlan printed;
    require(planPrint(printRender,printMappings,{64,80},printed,error),"plan with keyswitch pre-roll and CC");
    require(printed.parts.size()==3&&printed.duration==2*printSource.duration()&&printed.lead==printLead,"split parts by destination and MIDI channel");
    require(printed.musicalNotes==10&&printed.switchNotes==4&&printed.controllerEvents==2,"filtered attacks and explicit articulation events");
    const auto& firstPart=printed.parts.front();
    require(firstPart.route==0&&firstPart.channel==1&&firstPart.notes.front()==PrintNote{-printLead,1800,26,100},"switch lead and hold exact");
    require(firstPart.notes[1]==PrintNote{0,1800,60,conductedVelocity(90,64)},"musical start unaffected by lead");
    require(firstPart.controllers.front()==PrintCC{9600-printLead,20,70},"controller lead exact");
    const auto priorCount=printed.musicalNotes;
    require(!planPrint(printRender,{},ConductingValues{},printed,error)&&printed.musicalNotes==priorCount,"missing mapping rejects transactionally");
    auto conflictingPrint=printMappings;conflictingPrint[0].number=60;
    require(!planPrint(printRender,conflictingPrint,ConductingValues{},printed,error),"keyswitch collision refused");
    auto noRoom=printSource;noRoom.rows[0].steps[0].gate=1000;
    noRoom.rows[0].steps[1].enabled=true;noRoom.rows[0].steps[1].articulation="cc";
    require(compile(noRoom,{"strings","drums"},1,printRender,error)&&!planPrint(printRender,printMappings,ConductingValues{},printed,error),"adjacent technique without lead refused");
    auto unboundPrint=printSource;unboundPrint.rows[2].trackId.clear();
    require(compile(unboundPrint,{"strings"},1,printRender,error)&&!planPrint(printRender,printMappings,ConductingValues{},printed,error),"unassigned row refused");
    Pattern silentPrint;silentPrint.rows[0].trackId="t";silentPrint.rows[0].steps[0].enabled=true;silentPrint.rows[0].steps[0].densityThreshold=100;
    require(compile(silentPrint,{"t"},1,printRender,error)&&!planPrint(printRender,{},ConductingValues{127,0},printed,error),"no notes at selected density");
    std::cout<<"O1–O10 ostinato core: "<<checks<<" checks passed\n";
}
