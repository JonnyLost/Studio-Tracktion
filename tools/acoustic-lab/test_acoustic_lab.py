# SPDX-License-Identifier: GPL-3.0-or-later
import csv
import json
import math
import pathlib
import struct
import tempfile
import unittest
import wave
import zipfile
import acoustic_lab as lab

def chunk(tag,body,order):
    return tag+struct.pack(order+"I",len(body))+body+(b"\0" if len(body)%2 else b"")

def float_wav(samples,rate=8000,ch=2):
    raw=struct.pack("<"+"f"*len(samples),*samples)
    data=b"WAVE"+chunk(b"fmt ",struct.pack("<HHIIHH",3,ch,rate,rate*ch*4,ch*4,32),"<")+chunk(b"data",raw,"<")
    return b"RIFF"+struct.pack("<I",len(data))+data

def aiff24():
    rate=8000
    exp=rate.bit_length()-1
    ext=struct.pack(">HQ",16383+exp,rate<<(63-exp))
    comm=struct.pack(">hIh",2,2,24)+ext
    raw=b"".join(x.to_bytes(3,"big",signed=True) for x in (0,4194304,-4194304,0))
    body=b"AIFF"+chunk(b"COMM",comm,">")+chunk(b"SSND",struct.pack(">II",0,0)+raw,">")
    return b"FORM"+struct.pack(">I",len(body))+body

class AcousticLabTest(unittest.TestCase):
    def test_audio(self):
        rate,bits,ch=lab.decode(aiff24())
        self.assertEqual((rate,bits,len(ch)),(8000,24,2))
        self.assertEqual(list(ch[0]),[0,-.5]);self.assertEqual(list(ch[1]),[.5,0])
        rate,bits,ch=lab.decode(float_wav([.1,.2,.3,.4],ch=4))
        self.assertEqual((rate,bits,len(ch)),(8000,32,4))
        self.assertAlmostEqual(ch[3][0],.4)
        with self.assertRaises(ValueError):
            lab.decode(b"RIFF"+struct.pack("<I",4)+b"PWAV")
        with self.assertRaises(ValueError):
            lab.decode(float_wav([math.nan,0]))
        with self.assertRaises(ValueError):
            lab.decode(float_wav([.1,.2])[:-1])

    def test_decay(self):
        rate=8000;rt=.9
        values=[]
        for i in range(rate*3):
            x=.25*math.exp(-math.log(1000)*i/(rt*rate))*math.cos(2*math.pi*500*i/rate)
            values.extend((x,x))
        row,env=lab.metrics(float_wav(values),0)
        self.assertAlmostEqual(row["rt60_t20_s"],rt,delta=.04)
        self.assertGreater(row["fit_r2"],.99)
        self.assertAlmostEqual(row["stereo_correlation"],1)
        self.assertGreater(len(env),100)
        self.assertEqual(lab.metrics(float_wav([0.,0.]))[0]["warning"],"silent")

    def test_workflow(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp)
            lab.generate(root)
            with wave.open(str(root/"inputs"/"impulse_L.wav"),"rb") as f:
                self.assertEqual((f.getnchannels(),f.getframerate()),(2,44100))
                f.setpos(44100)
                self.assertEqual(struct.unpack("<hh",f.readframes(1)),(8192,0))
            with self.assertRaises(ValueError):lab.generate(root)
            (root/"references"/"sample.SDIR").write_bytes(aiff24())
            with zipfile.ZipFile(root/"references"/"proprietary.zip","w") as z:
                z.writestr("x.pwav",b"RIFF"+struct.pack("<I",4)+b"PWAV")
            lab.analyze(root)
            summary=json.loads((root/"report"/"summary.json").read_text())
            self.assertEqual(summary["decoded"],1);self.assertEqual(summary["skipped"],1)
            self.assertEqual(len(summary["missing_cases"]),30)
            with (root/"report"/"inventory.csv").open() as f:
                self.assertEqual(len(list(csv.DictReader(f))),2)

if __name__=="__main__":unittest.main()
