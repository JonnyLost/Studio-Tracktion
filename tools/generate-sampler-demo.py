#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a small, reproducible starter kit; no recordings or third-party assets."""
import io
import math
from pathlib import Path
import struct
import wave
import zipfile
import xml.etree.ElementTree as ET

root = ET.Element('STUDIO_SAMPLER', version='2', name='Synthetic Studio Keys', preset='First Instrument', dirty='0',
                  attack='0.005', decay='0.12', sustain='1', release='0.12', gain='-9', tune='0', bend='2', voices='32')
ET.SubElement(root, 'GROUP', id='0', name='Piano Soft')
ET.SubElement(root, 'GROUP', id='1', name='Piano Loud')
output = Path(__file__).resolve().parents[1] / 'docs/examples/Studio-First-Instrument.zip'
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as kit:
    def add(name, data):
        info = zipfile.ZipInfo('Studio First Instrument/' + name, (2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        kit.writestr(info, data)
    for folder, layer, amplitude in [(0, 'Soft', .25), (1, 'Loud', .5)]:
        for note, frequency, low, high in [('C4', 261.625565, 0, 63), ('G4', 391.995436, 64, 127)]:
            name = f'{layer}_{note}.wav'
            data = io.BytesIO()
            with wave.open(data, 'wb') as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(24000)
                wav.writeframes(b''.join(struct.pack('<h', round(32767 * amplitude * min(1, i / 120) * math.exp(-i / 24000)
                    * math.sin(2 * math.pi * frequency * i / 24000))) for i in range(18000)))
            add('Samples/' + name, data.getvalue())
            ET.SubElement(root, 'ZONE', relative='Samples/' + name, folder=str(folder), root='60' if note == 'C4' else '67',
                          low=str(low), high=str(high), soft='1' if folder == 0 else '64', hard='63' if folder == 0 else '127',
                          gain='0', cents='0', oneShot='0', loop='1', loopStart='2400', loopEnd='14400', start='0', end='18000', group='0')
    add('First Instrument.studiosampler', ET.tostring(root, encoding='utf-8', xml_declaration=True))
    add('READ ME.txt', 'Synthetic Studio Keys\n\nFour mathematically generated WAV tones, not piano recordings. No third-party audio.\n\nIn Studio, create a Sampler track and Load First Instrument.studiosampler to hear the example.\nTo build your own version, start with an empty Sampler and import the four WAV files in Samples.\nAutomatic mapping detects C4 and G4. In Mapping, set Soft samples to velocity 1-63 and Loud samples to 64-127.\nName your instrument and Save it beside Samples. Keep this folder together when moving it.\n\nGenerated reproducibly by tools/generate-sampler-demo.py in JonnyLost/Studio-Tracktion.\n')
print(output)
