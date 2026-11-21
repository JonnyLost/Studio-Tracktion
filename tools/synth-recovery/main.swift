// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

struct Parameter: Encodable {
    let id: Int32
    let name: String
    let minimum: Float
    let maximum: Float
    let initial: Float
    let logarithmic: Bool
    init(_ id: Int32, _ name: String, _ range: ClosedRange<Float>, _ initial: Float, _ logarithmic: Bool = false) {
        self.id = id
        self.name = name
        self.minimum = range.lowerBound
        self.maximum = range.upperBound
        self.initial = initial
        self.logarithmic = logarithmic
    }
}
struct Sound: Encodable {
    let id, name, family, category, summary: String
    let patch: StudioSynthPatch
}
struct Catalog: Encodable {
    let source: String
    let sounds: [Sound]
    let parameters: [String: [Parameter]]
}

let parameters: [String: [Parameter]] = [
    "analog": StudioAnalogParameter.allCases.map { .init($0.rawValue, $0.displayName, $0.range, $0.defaultValue, $0.isLogarithmic) },
    "fm": StudioFMParameter.allCases.map { .init($0.rawValue, $0.name, $0.range, $0.defaultValue, $0.logarithmic) },
    "wavetable": StudioWavetableParameter.allCases.map { .init($0.rawValue, $0.name, $0.range, $0.defaultValue, $0.logarithmic) },
    "additive": StudioAdditiveParameter.allCases.map { .init($0.rawValue, $0.name, $0.range, $0.defaultValue, $0.logarithmic) },
    "granular": StudioGranularParameter.allCases.map { .init($0.rawValue, $0.name, $0.range, $0.defaultValue, $0.logarithmic) },
    "physical": StudioPhysicalParameter.allCases.map { .init($0.rawValue, $0.name, $0.range, $0.defaultValue) }
]
let catalog = StudioSynthFactoryCatalog.sounds.enumerated().map { index, sound in
    var patch = sound.patch
    let prefix = index * 1000
    func stableID(_ n: Int) -> UUID {
        UUID(uuidString: String(format: "00000000-0000-4000-8000-%012d", prefix + n))!
    }
    var remap: [UUID: UUID] = [:]
    for index in patch.layers.indices {
        remap[patch.layers[index].id] = stableID(index + 1)
        patch.layers[index].id = stableID(index + 1)
    }
    if var modulation = patch.modulation {
        for index in modulation.routes.indices {
            modulation.routes[index].id = stableID(100 + index)
            modulation.routes[index].layerID = remap[modulation.routes[index].layerID]!
        }
        patch.modulation = modulation
    }
    if var controls = patch.midiControls {
        for index in controls.mappings.indices {
            controls.mappings[index].id = stableID(300 + index)
            if case let .parameter(layer, destination) = controls.mappings[index].target {
                controls.mappings[index].target = .parameter(layerID: remap[layer]!, destination: destination)
            }
        }
        patch.midiControls = controls
    }
    precondition(patch.validate(), sound.id)
    return Sound(id: sound.id, name: sound.name, family: String(sound.id.split(separator: ":")[0]),
                 category: sound.category, summary: sound.summary, patch: patch)
}
let counts = Dictionary(grouping: catalog, by: \.family).mapValues(\.count)
precondition(counts == ["analog":34, "fm":42, "wavetable":30, "additive":26, "granular":22, "physical":25, "hybrid":22])
precondition(Set(catalog.map(\.id)).count == 201)
let encoder = JSONEncoder()
encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
FileHandle.standardOutput.write(try encoder.encode(Catalog(source: "3787b60263b0f8d0188f82e626d12c40d17dc86f", sounds: catalog, parameters: parameters)))
