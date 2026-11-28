#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline acoustic measurements. Python 3.9+, standard library only."""
import argparse
import array
import csv
import hashlib
import json
import math
import pathlib
import random
import struct
import sys
import wave
import zipfile

RATE = 44100
AMP = 0.25
BANDS = (125, 250, 500, 1000, 2000, 4000, 8000)
MAX_BYTES = 96 * 1024 * 1024
ROOMS = ("Concert Hall", "Smooth Space", "Chamber", "Reflective Hall", "Theater")
ARIA = ("Concert Hall 1", "Concert Hall 2", "Recital Hall 1", "Cathedral",
        "Film Score Space", "Modern Symphonic Concert Hall", "Traditional Symphonic Concert Hall",
        "Large Symphonic Concert Hall", "Small Chamber Space")

def write_wave(path, channels, rate, frames):
    with wave.open(str(path), "wb") as f:
        f.setnchannels(channels)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(frames)

def probe(path, mode, duration=18, rate=RATE, onset=1):
    n = int(duration * rate)
    buf = array.array("h", [0]) * (2*n)
    k = int(onset * rate)
    if mode in ("L", "M"):
        buf[2*k] = 8192
    if mode in ("R", "M"):
        buf[2*k+1] = 8192
    if sys.byteorder != "little":
        buf.byteswap()
    write_wave(path, 2, rate, buf.tobytes())

def generate(root):
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        raise ValueError("Generate into a new empty directory")
    for sub in ("inputs", "renders", "aria", "references"):
        (root/sub).mkdir()
    for mode in ("L", "R", "M"):
        probe(root/"inputs"/("impulse_"+mode+".wav"), mode)
    # Deterministic listening/noise-burst probe; do NOT treat as an IR.
    rng=random.Random(603)
    noise=array.array("h",[0])*(2*RATE*18)
    for i in range(RATE,2*RATE):
        x=int(4096*rng.uniform(-1,1));noise[2*i]=noise[2*i+1]=x
    if sys.byteorder!="little":noise.byteswap()
    write_wave(root/"inputs"/"noise_burst.wav",2,RATE,noise.tobytes())
    # One-frame excitation followed by silence; no copyrighted content.
    for mode in ("L", "R", "M"):
        probe(root/"aria"/("pulse_"+mode+".wav"), mode, duration=2, onset=.1)
    (root/"aria"/"Acoustic Probe.sfz").write_text(
        "<group> pitch_keytrack=0 ampeg_attack=0 ampeg_release=0.01\n"
        "<region> sample=pulse_L.wav lokey=60 hikey=60 pitch_keycenter=60\n"
        "<region> sample=pulse_R.wav lokey=62 hikey=62 pitch_keycenter=62\n"
        "<region> sample=pulse_M.wav lokey=64 hikey=64 pitch_keycenter=64\n",
        encoding="utf-8")
    rows = []
    for room in ROOMS:
        slug = room.lower().replace(" ", "_")
        for mode, take in (("M",1),("M",2),("L",1),("R",1)):
            rows.append(dict(engine="ChromaVerb", preset=room, mode=mode, take=take,
                input="inputs/impulse_"+mode+".wav",
                output="renders/chroma_"+slug+"_"+mode+str(take)+".wav",
                excitation_s=1, settings="Wet 100%; Dry 0%; Predelay 0; tempo sync off; record other controls"))
    rows.append(dict(engine="ARIA",preset="Dry calibration",mode="M",take=1,
        input="aria/Acoustic Probe.sfz",output="renders/aria_dry_M1.wav",
        excitation_s="",settings="All reverbs disabled, MIDI note 64"))
    for name in ARIA:
        slug = name.lower().replace(" ", "_")
        rows.append(dict(engine="ARIA",preset=name,mode="M",take=1,
            input="aria/Acoustic Probe.sfz",output="renders/aria_"+slug+"_M1.wav",
            excitation_s="",settings="Record dry reference and wet output; note instrument settings, MIDI note and send"))
    with (root/"cases.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    (root/"READ-ME.txt").write_text("""STUDIO ACOUSTIC LAB L3

Start with Concert Hall M1 and M2 in Logic (same settings, two separate bounces).
Import inputs/impulse_M.wav on a stereo audio track at project start.
Set project sample rate 44.1 kHz. Insert ChromaVerb; wet 100%, dry 0%.
Select room, disable tempo sync, set predelay zero, record every other control
or screenshot. Keep track, master and output faders at unity, no other effects.
Bounce exact 18-second region to stereo WAV, 24-bit PCM or 32-bit float.
Normalization OFF, no dither, no auto tail, no MP3. Preserve initial silence.
Name file as in cases.csv. For long tails, extend bounce range (<=60 s).
Repeat M1/M2 separately; then L1/R1 with corresponding isolated input.
A full first pass is optional: any subset is accepted.
ARIA: try loading aria/Acoustic Probe.sfz through your installed ARIA's
normal SFZ loading workflow. Use notes 60/62/64, hold >=2 seconds, record
dry output first. Disable all instrument processing you can. Record wet
with one reverb enabled, dry muted ONLY if ARIA provides a wet-only control.
If mixed dry/wet, label it as such; do not treat as a pure impulse response.
Measure dry onset and enter excitation_s in cases.csv before analysis.
SFZ playback in ARIA is not guaranteed; verify dry output before proceeding.
Copy readable WAV/AIFF/SDIR references (or their ZIP) into references.
Keep PWAV protected or unknown files untouched; inventory only.
Run analyze on this directory; results go into report/ and can be zipped.
Never redistribute commercial source impulse responses in the report.
""",encoding="utf-8")
    print("Created", root)

def chunks(data, start, endian, end):
    pos=start
    while pos+8<=end:
        tag=data[pos:pos+4]
        size=struct.unpack_from(endian+"I",data,pos+4)[0]
        pos+=8
        if size>end-pos:
            raise ValueError("Truncated chunk "+repr(tag))
        yield tag, memoryview(data)[pos:pos+size]
        pos+=size+(size&1)

def extended80(b):
    exponent,mantissa=struct.unpack(">HQ",b)
    if exponent==0 and mantissa==0:
        return 0
    if exponent&0x8000 or exponent==0x7fff:
        raise ValueError("Unsupported AIFF rate")
    return math.ldexp(mantissa,exponent-16383-63)

def decode(data):
    if len(data)>MAX_BYTES or len(data)<12:
        raise ValueError("File size outside laboratory limits")
    form=data[8:12]
    if data[:4]==b"RIFF" and form!=b"WAVE":
        raise ValueError("Unsupported RIFF subtype "+repr(form)+"; no payload decoding")
    if data[:4]==b"RIFF":
        end=struct.unpack_from("<I",data,4)[0]+8
        if end>len(data):
            raise ValueError("Truncated RIFF")
        c=dict(chunks(data,12,"<",end))
        fmt=c[b"fmt "]
        code,ch,rate,_,align,bits=struct.unpack_from("<HHIIHH",fmt)
        if code==65534 and len(fmt)>=40:
            # WAVEFORMATEXTENSIBLE: PCM or IEEE float only, standard GUID.
            guid=bytes(fmt[24:40])
            if guid[4:]!=bytes.fromhex("00001000800000aa00389b71"):
                raise ValueError("Unknown extensible subtype")
            code=struct.unpack_from("<I",guid)[0]
        if code not in (1,3):
            raise ValueError("Non-PCM/float WAV")
        payload=c[b"data"]
        endian="<"; floating=code==3; unsigned=bits==8
        if align!=ch*((bits+7)//8):
            raise ValueError("Unexpected WAV block alignment")
        declared=None
    elif data[:4]==b"FORM" and form in (b"AIFF",b"AIFC"):
        end=struct.unpack_from(">I",data,4)[0]+8
        if end>len(data):
            raise ValueError("Truncated FORM")
        c=dict(chunks(data,12,">",end))
        comm=c[b"COMM"]
        ch,declared,bits=struct.unpack_from(">hIh",comm)
        rate=round(extended80(comm[8:18]))
        encoding=bytes(comm[18:22]) if form==b"AIFC" else b"NONE"
        if encoding not in (b"NONE",b"twos",b"sowt",b"fl32",b"FL32",b"fl64",b"FL64"):
            raise ValueError("Unsupported AIFF encoding "+repr(encoding))
        endian="<" if encoding==b"sowt" else ">"
        floating=encoding.lower() in (b"fl32",b"fl64"); unsigned=False
        ssnd=c[b"SSND"];offset,_=struct.unpack_from(">II",ssnd)
        if 8+offset>len(ssnd):
            raise ValueError("Bad AIFF offset")
        payload=ssnd[8+offset:]
    else:
        raise ValueError("Unknown or proprietary audio container")
    if ch not in (1,2,4) or not 8000<=rate<=192000:
        raise ValueError("Unsupported channel count or rate")
    if (floating and bits not in (32,64)) or (not floating and bits not in (8,16,24,32)):
        raise ValueError("Unsupported bit depth")
    size=bits//8
    stride=ch*size
    n=len(payload)//stride if declared is None else declared
    if n<=0 or n>rate*60 or n*stride>len(payload) or (declared is None and len(payload)%stride):
        raise ValueError("Truncated or over-60-second audio")
    payload=payload[:n*stride]
    if bits==24:
        vals=array.array("f",(int.from_bytes(payload[i:i+3],
            "little" if endian=="<" else "big",signed=True)/8388608
            for i in range(0,len(payload),3)))
    else:
        kind=("f" if bits==32 else "d") if floating else {8:"B" if unsigned else "b",16:"h",32:"i"}[bits]
        raw=array.array(kind);raw.frombytes(payload)
        if size>1 and (endian=="<")!=(sys.byteorder=="little"):
            raw.byteswap()
        vals=array.array("f",raw) if floating else array.array("f",((x-(128 if unsigned else 0))/(2**(bits-1)) for x in raw))
    if any(not math.isfinite(x) or abs(x)>32 for x in vals):
        raise ValueError("Nonfinite or implausible audio payload")
    return rate,bits,[vals[i::ch] for i in range(ch)]

def db(x):
    return 10*math.log10(max(x,1e-30))

def fit_decay(energies,step):
    e=list(energies)
    for i in range(len(e)-2,-1,-1):
        e[i]+=e[i+1]
    if not e or e[0]<1e-20:
        return None,None
    points=[(i*step,db(v/e[0])) for i,v in enumerate(e) if -25<=db(v/e[0])<=-5]
    if len(points)<5:
        return None,None
    n=len(points);mx=sum(x for x,y in points)/n;my=sum(y for x,y in points)/n
    xx=sum((x-mx)**2 for x,y in points);yy=sum((y-my)**2 for x,y in points)
    xy=sum((x-mx)*(y-my) for x,y in points)
    if xx<=0 or yy<=0 or xy>=0:
        return None,None
    return -60*xx/xy,xy*xy/(xx*yy)

def energy_bins(channels, rate, start, hz=None):
    step=max(1,rate//100)
    bins=[0.0]*((len(channels[0])-start+step-1)//step)
    if hz is not None:
        # RBJ constant-peak bandpass, approximate one-octave Q.
        w=2*math.pi*hz/rate;alpha=math.sin(w)/(2*math.sqrt(2))
        b=alpha/(1+alpha);a1=-2*math.cos(w)/(1+alpha);a2=(1-alpha)/(1+alpha)
    for samples in channels:
        z1=z2=0.0
        for j,x in enumerate(samples[start:]):
            if hz is not None:
                y=b*x+z1;z1=z2-a1*y;z2=-b*x-a2*y;x=y
            bins[j//step]+=x*x
    return bins,step/rate

def metrics(data, excitation=None):
    rate,bits,channels=decode(data);n=len(channels[0])
    peak=max(max(map(abs,c)) for c in channels)
    threshold=max(peak*0.0001,1e-8)
    onset=min(next((i for i,x in enumerate(c) if abs(x)>=threshold),n) for c in channels)
    row=dict(rate=rate,bits=bits,channels=len(channels),duration_s=n/rate,peak=peak,
             onset_s=onset/rate if onset<n else None)
    if onset==n:
        row["warning"]="silent";return row,[]
    row["relative_onset_ms"]=(onset/rate-excitation)*1000 if excitation is not None else None
    e,step=energy_bins(channels,rate,onset)
    total=sum(e)
    row["energy"]=total
    row["rt60_t20_s"],row["fit_r2"]=fit_decay(e,step)
    row["end_100ms_db"]=db(sum(e[-10:])/total)
    row["early_80ms_fraction"]=sum(e[:8])/total
    row["late_80ms_fraction"]=1-row["early_80ms_fraction"]
    row["warning"]="tail may be truncated/noisy" if row["end_100ms_db"]>-45 else ""
    if len(channels)==2:
        l,r=channels
        ll=sum(x*x for x in l);rr=sum(x*x for x in r)
        row["stereo_correlation"]=sum(x*y for x,y in zip(l,r))/math.sqrt(ll*rr) if ll*rr>0 else None
    for hz in BANDS:
        if hz<rate*.4:
            band,_=energy_bins(channels,rate,onset,hz)
            row["rt60_"+str(hz)+"hz_s"],row["r2_"+str(hz)+"hz"]=fit_decay(band,step)
    # 20 ms local Gaussian-normalized density proxy; excludes leading silence.
    # 1.0 is approximately Gaussian; sparse echoes give much lower values.
    window=max(1,rate//50)
    density=[]
    for start in range(onset,min(n,onset+rate),window):
        stop=min(n,start+window)
        x=channels[0][start:stop]
        rms=math.sqrt(sum(v*v for v in x)/len(x))
        density.append(sum(abs(v)>rms for v in x)/len(x)/.3173105 if rms>1e-12 else 0)
    for ms in (20,100,200,500):
        i=ms//20-1
        row["density_"+str(ms)+"ms"]=density[i] if i<len(density) else None
    envelope=[dict(time_s=i*step,energy_db=db(v/total)) for i,v in enumerate(e)]
    return row,envelope

def analyze(root):
    report=root/"report";report.mkdir(exist_ok=True)
    sources=[];cases={}
    if (root/"cases.csv").exists():
        with (root/"cases.csv").open(newline="",encoding="utf-8") as f:
            for c in csv.DictReader(f):
                cases[c["output"]]=c
    for folder in ("renders","references"):
        base=root/folder
        for path in sorted(base.rglob("*")):
            if path.is_file() and path.suffix.lower() in (".wav",".aif",".aiff",".aifc",".sdir",".pwav",".wir",".zip"):
                if not path.name.startswith("._") and "__MACOSX" not in path.parts:
                    sources.append(path)
    results=[];errors=[];envelopes=[];inventory=[]
    def inspect(label,data):
        try:
            case=cases.get(label,{})
            if not case and label.startswith("renders/studio_"):
                stem=pathlib.PurePosixPath(label).stem
                name,mode=stem[7:].rsplit("_",1)
                case=dict(engine="Studio baseline",preset=name.replace("_"," "),mode=mode[0],take=mode[1:],excitation_s="1")
            inventory.append(dict(file=label,bytes=len(data),container=repr(data[:4]),subtype=repr(data[8:12]),sha256=hashlib.sha256(data).hexdigest()))
            excitation=float(case["excitation_s"]) if case.get("excitation_s") else None
            row,env=metrics(data,excitation)
            if case.get("engine")=="ARIA":
                row["warning"]=(row.get("warning","")+"; verify wet-only capture and dry calibration").strip("; ")
            row.update(file=label,sha256=hashlib.sha256(data).hexdigest(),
                       engine=case.get("engine","reference"),preset=case.get("preset",""),
                       mode=case.get("mode",""),take=case.get("take",""))
            results.append(row)
            envelopes.extend(dict(file=label,**x) for x in env)
            print("OK",label,flush=True)
        except (ValueError,KeyError,struct.error,OverflowError) as exc:
            errors.append(dict(file=label,reason=str(exc)))
            print("SKIP",label,str(exc),flush=True)
    for path in sources:
        label=path.relative_to(root).as_posix()
        try:
            if path.suffix.lower()==".zip":
                with zipfile.ZipFile(path) as z:
                    for entry in z.infolist():
                        if entry.is_dir() or "__MACOSX" in pathlib.PurePosixPath(entry.filename).parts or pathlib.PurePosixPath(entry.filename).name.startswith("._") or pathlib.PurePosixPath(entry.filename).suffix.lower() not in (".wav",".aif",".aiff",".aifc",".sdir",".pwav",".wir"):
                            continue
                        name=label+"::"+entry.filename
                        if entry.file_size>MAX_BYTES:
                            errors.append(dict(file=name,reason="size limit"));continue
                        inspect(name,z.read(entry))
            elif path.stat().st_size<=MAX_BYTES:
                inspect(label,path.read_bytes())
            else:
                errors.append(dict(file=label,reason="size limit"))
        except (OSError,zipfile.BadZipFile,RuntimeError) as exc:
            errors.append(dict(file=label,reason=str(exc)))
    def write_csv(name,rows):
        fields=sorted(set().union(*(r.keys() for r in rows))) if rows else ["file"]
        with (report/name).open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
    write_csv("measurements.csv",results);write_csv("envelopes.csv",envelopes);write_csv("errors.csv",errors)
    write_csv("inventory.csv",inventory)
    repeat=[]
    for a in results:
        if str(a.get("take"))!="1":continue
        for b in results:
            if str(b.get("take"))!="2" or any(a.get(k)!=b.get(k) for k in ("engine","preset","mode")):continue
            row=dict(file=a["file"],repeat=b["file"],identical_file=a["sha256"]==b["sha256"])
            for k in ("rt60_t20_s","relative_onset_ms","stereo_correlation","energy","density_100ms"):
                if a.get(k) is not None and b.get(k) is not None:row[k+"_difference"]=b[k]-a[k]
            repeat.append(row)
    write_csv("repeatability.csv",repeat)
    if (root/"native_performance.csv").is_file():
        (report/"native_performance.csv").write_bytes((root/"native_performance.csv").read_bytes())
    (report/"summary.json").write_text(json.dumps(dict(schema="studio.acoustic-lab.l3.v1",
        decoded=len(results),skipped=len(errors),missing_cases=sorted(set(cases)-{r["file"] for r in results}),
        note="Finite-window T20 extrapolations; not certified RT60. Four channels analyzed as aggregate, routing unknown."),indent=2),encoding="utf-8")
    print("Report:",report,"decoded",len(results),"skipped",len(errors))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command",choices=("generate","analyze"))
    parser.add_argument("directory",type=pathlib.Path)
    args=parser.parse_args()
    try:
        (generate if args.command=="generate" else analyze)(args.directory.expanduser())
    except (OSError,ValueError) as exc:
        parser.exit(1,str(exc)+"\n")

if __name__=="__main__":
    main()
