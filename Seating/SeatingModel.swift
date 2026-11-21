// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

enum SeatingFamily: String, CaseIterable, Codable, Identifiable {
    case strings = "Strings", winds = "Woodwinds", brass = "Brass", percussion = "Percussion"
    case keys = "Keyboards", voices = "Voices", historic = "Historic Instruments"
    var id: Self { self }
}

struct SeatingInstrument: Identifiable {
    let id: String
    let name: String
    let family: SeatingFamily
    let volume: Int
    let aliases: String

    func matches(_ query: String) -> Bool {
        let searchable = "\(name) \(family.rawValue) \(aliases)"
        let terms = query.lowercased().split(whereSeparator: \.isWhitespace)
        var index = 0
        while index < terms.count {
            let term = terms[index]
            if ["vol", "vol.", "volume"].contains(term), index + 1 < terms.count, let number = Int(terms[index + 1]) {
                if number != volume { return false }
                index += 2
                continue
            }
            if term.first == "v", let number = Int(term.dropFirst()) {
                if number != volume { return false }
            } else if searchable.range(of: String(term), options: [.caseInsensitive, .diacriticInsensitive]) == nil {
                return false
            }
            index += 1
        }
        return true
    }
}

struct SeatingPoint: Codable, Equatable {
    var x: Double
    var depth: Double
    // Leave space at the edge for the marker and its caption.
    static let radius = 0.91
    static let front = 0.08

    static func limited(x: Double, depth: Double) -> Self {
        let x = x.isFinite ? min(radius, max(-radius, x)) : 0
        let depth = depth.isFinite ? min(radius, max(front, depth)) : front
        let length = hypot(x, depth)
        if length <= radius { return Self(x: x, depth: depth) }
        let boundedDepth = max(front, depth * radius / length)
        let bound = sqrt(max(0, radius * radius - boundedDepth * boundedDepth))
        return Self(x: min(bound, max(-bound, x * radius / length)), depth: boundedDepth)
    }
}

struct SeatingProjection {
    var width: Double
    var height: Double

    private var halfWidth: Double { max(1, width * 0.46) }
    private var plotHeight: Double { max(1, height * 0.75) }
    var baseline: Double { height * 0.82 }

    func location(x: Double, depth: Double) -> CGPoint {
        CGPoint(x: width * 0.5 + x * halfWidth, y: baseline - depth * plotHeight)
    }

    func model(at position: CGPoint) -> SeatingPoint {
        .limited(x: (position.x - width * 0.5) / halfWidth, depth: (baseline - position.y) / plotHeight)
    }

    func contains(_ position: CGPoint) -> Bool {
        let x = (position.x - width * 0.5) / halfWidth
        let depth = (baseline - position.y) / plotHeight
        return depth >= 0 && hypot(x, depth) <= 1
    }
}

struct OrchestraSeat: Identifiable, Codable, Equatable {
    let id: UUID
    let instrumentID: String
    let name: String
    let family: SeatingFamily
    let volume: Int
    var label: String
    var section: String
    var position: SeatingPoint
}

struct SeatingLayout: Codable, Equatable {
    static let version = 1
    static let capacity = 256
    static let maximumBytes = 256 * 1024
    let version: Int
    var name: String
    private(set) var seats: [OrchestraSeat]

    init(name: String = "Orchestra") {
        version = Self.version
        self.name = name
        seats = []
    }

    static func decode(_ json: String) throws -> Self {
        guard json.utf8.count <= maximumBytes,
              var layout = try? JSONDecoder().decode(Self.self, from: Data(json.utf8)),
              layout.version == version,
              layout.seats.count <= capacity,
              Set(layout.seats.map(\.id)).count == layout.seats.count else {
            throw CocoaError(.fileReadCorruptFile)
        }
        layout.name = Self.text(layout.name)
        for index in layout.seats.indices {
            layout.seats[index].label = text(layout.seats[index].label)
            layout.seats[index].section = text(layout.seats[index].section)
            let point = layout.seats[index].position
            layout.seats[index].position = .limited(x: point.x, depth: point.depth)
        }
        return layout
    }

    func encoded() -> String? {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        guard let data = try? encoder.encode(self), data.count <= Self.maximumBytes else { return nil }
        return String(data: data, encoding: .utf8)
    }

    static func text(_ value: String) -> String {
        String(value.trimmingCharacters(in: .whitespacesAndNewlines).unicodeScalars.prefix(64))
    }

    @discardableResult
    mutating func place(_ instrument: SeatingInstrument, at point: SeatingPoint? = nil,
                        label: String? = nil, section: String? = nil) -> UUID? {
        guard seats.count < Self.capacity else { return nil }
        let point = point ?? suggested(for: instrument.family)
        let seat = OrchestraSeat(id: UUID(), instrumentID: instrument.id, name: instrument.name,
                                 family: instrument.family, volume: instrument.volume,
                                 label: Self.text(label ?? instrument.name), section: Self.text(section ?? instrument.family.rawValue),
                                 position: .limited(x: point.x, depth: point.depth))
        seats.append(seat)
        return seat.id
    }

    mutating func update(_ id: UUID, _ edit: (inout OrchestraSeat) -> Void) {
        guard let index = seats.firstIndex(where: { $0.id == id }) else { return }
        edit(&seats[index])
        let point = seats[index].position
        seats[index].position = .limited(x: point.x, depth: point.depth)
        seats[index].label = Self.text(seats[index].label)
        seats[index].section = Self.text(seats[index].section)
    }

    mutating func remove(_ id: UUID) { seats.removeAll { $0.id == id } }

    private func suggested(for family: SeatingFamily) -> SeatingPoint {
        let center: SeatingPoint
        switch family {
        case .strings: center = .init(x: -0.46, depth: 0.16)
        case .winds: center = .init(x: 0, depth: 0.42)
        case .brass: center = .init(x: 0.25, depth: 0.62)
        case .percussion: center = .init(x: 0.10, depth: 0.81)
        case .keys: center = .init(x: -0.65, depth: 0.38)
        case .voices: center = .init(x: -0.18, depth: 0.78)
        case .historic: center = .init(x: 0.58, depth: 0.35)
        }
        let occupied = seats.map(\.position)
        for ring in 0...10 {
            for slot in 0..<11 {
                let offset = slot % 2 == 0 ? -(slot / 2) : (slot + 1) / 2
                let candidate = SeatingPoint.limited(x: center.x + Double(offset) * 0.15, depth: center.depth + Double(ring) * 0.10)
                if occupied.allSatisfy({ hypot($0.x - candidate.x, $0.depth - candidate.depth) > 0.11 }) {
                    return candidate
                }
            }
        }
        return center
    }
}
