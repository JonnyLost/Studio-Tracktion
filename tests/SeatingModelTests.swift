// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

private func require(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() {
        FileHandle.standardError.write(Data((message + "\n").utf8))
        exit(1)
    }
}

@main struct SeatingModelTests {
    static func main() throws {
        let instruments = SeatingCatalog.instruments
        require(Set(instruments.map(\.volume)) == Set(1...7), "All seven volumes must be represented")
        require(Set(instruments.map(\.id)).count == instruments.count, "Catalog IDs must be unique")
        require(instruments.filter { $0.volume == 7 }.count == 22, "Vol. 7 has 22 distinct instruments")
        require(SeatingCatalog.byID["v1-clarinet"]?.matches("b-flat vol 1") == true, "Search aliases and volume")
        require(SeatingCatalog.byID["v1-bosendorfer"]?.matches("bosendorfer") == true, "Diacritic-insensitive search")
        require(SeatingCatalog.byID["v1-bosendorfer"]?.matches("v2") == false, "Search excludes other volumes")

        let point = SeatingPoint.limited(x: 0.32, depth: 0.50)
        for (width, height) in [(610.0, 380.0), (920.0, 740.0), (285.0, 200.0)] {
            let projection = SeatingProjection(width: width, height: height)
            let screen = projection.location(x: point.x, depth: point.depth)
            let roundTrip = projection.model(at: screen)
            require(abs(roundTrip.x - point.x) < 1e-9 && abs(roundTrip.depth - point.depth) < 1e-9,
                    "Stage coordinates survive resize and the view transform")
            require(projection.contains(screen) && !projection.contains(.zero), "Drop hit-testing must follow the stage outline")
            require(projection.location(x: 0, depth: 0.70).y < projection.location(x: 0, depth: 0.25).y,
                    "Increasing depth moves a seat away from the conductor")
        }
        let edge = SeatingPoint.limited(x: 80, depth: -6)
        require(edge.depth >= SeatingPoint.front && hypot(edge.x, edge.depth) <= SeatingPoint.radius + 1e-9,
                "Out-of-stage coordinates must stay inside the marked boundary")
        require(SeatingPoint.limited(x: .nan, depth: .infinity) == SeatingPoint(x: 0, depth: SeatingPoint.front),
                "Non-finite coordinates must resolve to a usable position")

        var layout = SeatingCatalog.modernSymphony()
        require(layout.seats.count == 24 && layout.name == "Modern Symphony", "Modern symphony preset")
        require(layout.seats.first { $0.label == "Violin I" }?.section == "First Violins", "Preset distinguishes string sections")
        require(layout.seats.filter { $0.section == "Percussion" }.count == 4, "Preset includes percussion")
        let instrument = SeatingCatalog.byID["v4-soprano-solo"]!
        let first = layout.place(instrument, at: point)!
        let second = layout.place(instrument)!
        require(first != second && layout.seats.count == 26, "Repeated instruments must get separate IDs")
        layout.update(first) { seat in
            seat.label = " First Soloist "
            seat.section = "Guest Voices"
            seat.position = .init(x: -0.2, depth: 0.60)
        }
        require(layout.seats.first { $0.id == first }?.label == "First Soloist", "Placement edits trim metadata")
        let stored = layout.encoded()!
        let restored = try SeatingLayout.decode(stored)
        require(restored == layout, "Round-trip labels, coordinates, sections, IDs, and catalog metadata")
        layout.remove(first)
        require(!layout.seats.contains { $0.id == first } && layout.seats.contains { $0.id == second },
                "Removing one seat leaves the other placement intact")

        let future = stored.replacingOccurrences(of: "\"version\":1", with: "\"version\":2")
        require((try? SeatingLayout.decode(future)) == nil, "Unknown versions must be rejected")
        require((try? SeatingLayout.decode("invalid")) == nil, "Malformed layouts must be rejected")

        var full = SeatingLayout()
        for _ in 0..<SeatingLayout.capacity { full.place(instrument, at: point) }
        require(full.place(instrument) == nil && full.seats.count == SeatingLayout.capacity, "Bound the stage size")
        print("Seating library, coordinates, placement, and persistence passed.")
    }
}
