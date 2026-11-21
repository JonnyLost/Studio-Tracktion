// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit
import SwiftUI

private let canvas = Color(red: 0.082, green: 0.102, blue: 0.137)
private let panel = Color(red: 0.125, green: 0.157, blue: 0.204)
private let line = Color(red: 0.255, green: 0.302, blue: 0.369)
private let quiet = Color(red: 0.725, green: 0.753, blue: 0.792)
private let focus = Color(red: 0.569, green: 0.784, blue: 1)

private extension SeatingFamily {
    var color: Color {
        switch self {
        case .strings: return Color(red: 0.60, green: 0.75, blue: 0.91)
        case .winds: return Color(red: 0.56, green: 0.82, blue: 0.76)
        case .brass: return Color(red: 0.88, green: 0.75, blue: 0.52)
        case .percussion: return Color(red: 0.84, green: 0.64, blue: 0.73)
        case .keys: return Color(red: 0.73, green: 0.66, blue: 0.88)
        case .voices: return Color(red: 0.91, green: 0.69, blue: 0.60)
        case .historic: return Color(red: 0.70, green: 0.75, blue: 0.80)
        }
    }
}

private struct InstrumentIcon: View {
    let name: String
    let family: SeatingFamily

    var body: some View {
        let symbols = ["guitar", "pianokeys", "mic", "music.note"]
        let symbol = name.localizedCaseInsensitiveContains("guitar") ? symbols[0]
            : family == .keys ? symbols[1] : family == .voices ? symbols[2] : symbols[3]
        if let image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil) {
            Image(nsImage: image).accessibilityHidden(true)
        } else {
            Text(String(name.prefix(1)).uppercased()).font(.caption.bold()).accessibilityHidden(true)
        }
    }
}

@MainActor final class SeatingEditorState: ObservableObject {
    @Published private(set) var layout = SeatingLayout()
    @Published var selection: UUID?
    @Published private(set) var available = false
    @Published private(set) var message = ""
    @Published var interfaceScale: Double = 1
    @Published var textScale: Double = 1
    var onCommit: ((String, String) -> Void)?

    func restore(_ json: String, available: Bool) {
        self.available = available
        if !available || json.isEmpty {
            layout = SeatingLayout()
            message = ""
        } else {
            do {
                layout = try SeatingLayout.decode(json)
                message = ""
            } catch {
                layout = SeatingLayout()
                self.available = false
                message = "This project has a seating layout that this version of Studio can’t read."
            }
        }
        if !layout.seats.contains(where: { $0.id == selection }) { selection = nil }
    }

    func change(_ action: String, _ mutate: (inout SeatingLayout) -> Void) {
        guard available else { return }
        var next = layout
        mutate(&next)
        guard next != layout, let data = next.encoded() else { return }
        layout = next
        onCommit?(data, action)
    }

    func place(_ instrument: SeatingInstrument, at point: SeatingPoint? = nil) {
        if layout.seats.count == SeatingLayout.capacity {
            message = "The stage holds up to \(SeatingLayout.capacity) placements."
            return
        }
        var placed: UUID?
        change("Place instrument") { placed = $0.place(instrument, at: point) }
        if let placed { selection = placed }
    }

    func remove(_ id: UUID) {
        change("Remove instrument") { $0.remove(id) }
        if selection == id { selection = nil }
        message = ""
    }
}

struct SeatingEditor: View {
    @ObservedObject var state: SeatingEditorState
    @State private var search = ""
    @State private var family: SeatingFamily?
    @State private var showInspector = false
    @State private var confirmPreset = false

    private var selectedSeat: OrchestraSeat? { state.layout.seats.first { $0.id == state.selection } }
    private var scale: Double { state.interfaceScale }
    private func sized(_ value: CGFloat) -> CGFloat { value * min(1.27, scale) }
    private func font(_ value: CGFloat, weight: Font.Weight = .regular) -> Font {
        .system(size: value * min(1.65, scale * state.textScale), weight: weight)
    }

    var body: some View {
        GeometryReader { geometry in
            if !state.available {
                ContentUnavailableView("Orchestral Seating", systemImage: "music.note.list",
                    description: Text(state.message.isEmpty ? "Create or open a Studio project to design its seating." : state.message))
            } else {
                let width = geometry.size.width
                let minimum = sized(354)
                let height = max(geometry.size.height, minimum)
                if geometry.size.height < minimum {
                    ScrollView {
                        editor(width: width, height: height)
                    }
                } else {
                    editor(width: width, height: height)
                }
            }
        }
        .background(panel)
        .foregroundStyle(.white)
        .preferredColorScheme(.dark)
        .environment(\.colorScheme, .dark)
        .tint(focus)
    }

    private func editor(width: CGFloat, height: CGFloat) -> some View {
        let wide = width >= sized(950)
        return VStack(spacing: sized(8)) {
            HStack(spacing: sized(8)) {
                Text(state.layout.name.isEmpty ? "Orchestra" : state.layout.name)
                    .font(font(16, weight: .semibold)).lineLimit(1)
                Text("Reference only").font(font(11)).foregroundStyle(quiet)
                Spacer(minLength: 4)
                if !wide {
                    Button(showInspector ? "Library" : "Inspector") { showInspector.toggle() }
                        .help("Switch between the instrument library and layout inspector")
                }
                Button("Load Modern Symphony") {
                    if state.layout.seats.isEmpty { loadPreset() } else { confirmPreset = true }
                }
                .help("Load the standard modern symphony layout")
            }
            .font(font(12, weight: .medium))
            HStack(spacing: sized(8)) {
                if wide || !showInspector {
                    library.frame(width: sized(width < 740 ? 184 : 218))
                } else {
                    inspector.frame(width: sized(202))
                }
                SeatingStage(state: state)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                if wide { inspector.frame(width: sized(208)) }
            }
            if !state.message.isEmpty {
                Text(state.message).font(font(11)).foregroundStyle(quiet).frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .padding(sized(10))
        .frame(height: height)
        .alert("Replace the seating layout?", isPresented: $confirmPreset) {
            Button("Keep Current", role: .cancel) {}
            Button("Load Modern Symphony") { loadPreset() }
        } message: {
            Text("You can undo this change in Studio.")
        }
    }

    private func loadPreset() {
        state.change("Load Modern Symphony") { $0 = SeatingCatalog.modernSymphony() }
        state.selection = nil
    }

    private func sidebar<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: sized(8), content: content)
            .padding(sized(10))
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(canvas, in: RoundedRectangle(cornerRadius: 9))
            .overlay { RoundedRectangle(cornerRadius: 9).strokeBorder(line.opacity(0.65)) }
    }

    private var library: some View {
        let results = SeatingCatalog.instruments.filter { (family == nil || family == $0.family) && $0.matches(search) }
        return sidebar {
            Text("Instrument Library").font(font(14, weight: .semibold))
            HStack(spacing: 5) {
                Image(systemName: "magnifyingglass").foregroundStyle(quiet).accessibilityHidden(true)
                TextField("Search instruments", text: $search).textFieldStyle(.plain)
                if !search.isEmpty {
                    Button { search = "" } label: { Image(systemName: "xmark.circle.fill") }
                        .buttonStyle(.plain).accessibilityLabel("Clear search")
                }
            }
            .font(font(12)).padding(6)
            .background(panel, in: RoundedRectangle(cornerRadius: 5))
            Picker("Instrument family", selection: $family) {
                Text("All Families").tag(nil as SeatingFamily?)
                ForEach(SeatingFamily.allCases) { Text($0.rawValue).tag(Optional($0)) }
            }.labelsHidden().font(font(12))
            if results.isEmpty {
                Text("No matching instruments").font(font(12)).foregroundStyle(quiet)
            } else {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 4) {
                        ForEach(SeatingFamily.allCases) { group in
                            if results.contains(where: { $0.family == group }) {
                                Text(group.rawValue).font(font(11, weight: .semibold)).foregroundStyle(group.color)
                                    .padding(.top, 4).accessibilityAddTraits(.isHeader)
                                ForEach(results.filter { $0.family == group }) { instrument in
                                    HStack(spacing: 6) {
                                        HStack(spacing: 6) {
                                            InstrumentIcon(name: instrument.name, family: instrument.family)
                                                .frame(width: 15).foregroundStyle(group.color)
                                            VStack(alignment: .leading, spacing: 2) {
                                                Text(instrument.name).font(font(12, weight: .medium))
                                                Text("Vol. \(instrument.volume)").font(font(10)).foregroundStyle(quiet)
                                            }
                                            Spacer(minLength: 0)
                                        }
                                        .contentShape(Rectangle())
                                        .onDrag { NSItemProvider(object: ("studio-seating:" + instrument.id) as NSString) }
                                        Button { state.place(instrument) } label: {
                                            Image(systemName: "plus").frame(width: 22, height: 24)
                                        }
                                        .buttonStyle(.borderless)
                                        .accessibilityLabel("Place \(instrument.name)")
                                        .help("Place in a suggested position")
                                    }
                                    .padding(.horizontal, 4).padding(.vertical, 2)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    private var inspector: some View {
        sidebar {
            ScrollView {
                VStack(alignment: .leading, spacing: sized(9)) {
                    Text("Layout").font(font(14, weight: .semibold))
                    SeatingTextField("Layout name", value: state.layout.name) { name in
                        state.change("Rename seating layout") { $0.name = SeatingLayout.text(name) }
                    }
                    Divider()
                    Text("Selected Instrument").font(font(14, weight: .semibold))
                    if let seat = selectedSeat {
                        VStack(alignment: .leading, spacing: sized(8)) {
                            Text(seat.name).foregroundStyle(seat.family.color).fontWeight(.semibold)
                            Text("Vol. \(seat.volume) · \(seat.family.rawValue)").foregroundStyle(quiet)
                            SeatingTextField("Seat label", value: seat.label) { value in
                                state.change("Rename seat") { $0.update(seat.id) { $0.label = value } }
                            }
                            SeatingTextField("Section", value: seat.section) { value in
                                state.change("Set seating section") { $0.update(seat.id) { $0.section = value } }
                            }
                            if !knownSections(excluding: seat.section).isEmpty {
                                Menu("Use Existing Section") {
                                    ForEach(knownSections(excluding: seat.section), id: \.self) { value in
                                        Button(value) {
                                            state.change("Set seating section") { $0.update(seat.id) { $0.section = value } }
                                        }
                                    }
                                }
                            }
                            HStack(alignment: .top) {
                                axis("Left / right", seat.position.x) { value in
                                    state.change("Move instrument") { $0.update(seat.id) { $0.position.x = value } }
                                }
                                axis("Depth", seat.position.depth) { value in
                                    state.change("Move instrument") { $0.update(seat.id) { $0.position.depth = value } }
                                }
                            }
                            Button("Remove Seat", role: .destructive) { state.remove(seat.id) }
                        }.id(seat.id)
                    } else {
                        Text("Select a seat to edit its section and position.").foregroundStyle(quiet)
                    }
                }.font(font(12))
            }
        }
    }

    private func axis(_ label: String, _ coordinate: Double, apply: @escaping (Double) -> Void) -> some View {
        SeatingTextField(label, value: coordinate.formatted(.number.precision(.fractionLength(2)))) { text in
            if let value = Double(text) ?? (try? Double(text, format: .number)), value.isFinite { apply(value) }
        }
    }

    private func knownSections(excluding current: String) -> [String] {
        Set(state.layout.seats.map(\.section)).filter { !$0.isEmpty && $0 != current }.sorted()
    }
}

private struct SeatingTextField: View {
    let title: String
    let value: String
    let onCommit: (String) -> Void
    @State private var draft = ""
    @FocusState private var focused: Bool

    init(_ title: String, value: String, onCommit: @escaping (String) -> Void) {
        self.title = title
        self.value = value
        self.onCommit = onCommit
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title).fontWeight(.medium)
            TextField(title, text: $draft)
                .textFieldStyle(.roundedBorder)
                .focused($focused)
                .onSubmit { commit() }
        }
        .onAppear { draft = value }
        .onChange(of: value) { _, updated in draft = updated }
        .onChange(of: focused) { wasFocused, nowFocused in
            if wasFocused && !nowFocused { commit() }
        }
    }

    private func commit() {
        if draft != value { onCommit(draft) }
        draft = value
    }
}

private struct SeatingStage: View {
    @ObservedObject var state: SeatingEditorState
    @State private var dragged: UUID?
    @State private var preview: SeatingPoint?
    @State private var dropTarget = false

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Label("Concert Stage", systemImage: "music.note.list").font(.system(size: 13, weight: .semibold))
                Spacer()
                Text("\(state.layout.seats.count) placements").font(.caption).foregroundStyle(quiet)
            }
            .padding(10)
            GeometryReader { geometry in
                let projection = SeatingProjection(width: geometry.size.width, height: geometry.size.height)
                ZStack {
                    diagram(projection)
                        .accessibilityHidden(true)
                    if state.layout.seats.isEmpty {
                        Text("Drag an instrument here or use + in the library.")
                            .font(.callout).foregroundStyle(quiet)
                            .position(x: geometry.size.width / 2, y: geometry.size.height * 0.47)
                            .allowsHitTesting(false)
                    }
                    ForEach(state.layout.seats) { seat in
                        let p = dragged == seat.id ? preview ?? seat.position : seat.position
                        marker(seat, projection: projection)
                            .position(projection.location(x: p.x, depth: p.depth))
                            .zIndex(state.selection == seat.id ? 2 : 1)
                    }
                    Text("CONDUCTOR").tracking(2).font(.system(size: 9, weight: .semibold))
                        .foregroundStyle(quiet)
                        .position(x: geometry.size.width * 0.5, y: geometry.size.height * 0.93)
                }
                .contentShape(Rectangle())
                .coordinateSpace(name: "concertStage")
                .dropDestination(for: String.self) { items, location in
                    guard projection.contains(location), let item = items.first, item.hasPrefix("studio-seating:"),
                          let instrument = SeatingCatalog.byID[String(item.dropFirst("studio-seating:".count))] else { return false }
                    state.place(instrument, at: projection.model(at: location))
                    return true
                } isTargeted: { dropTarget = $0 }
            }
            if !state.layout.seats.isEmpty {
                ScrollView(.horizontal) {
                    HStack(spacing: 10) {
                        ForEach(SeatingFamily.allCases.filter { family in state.layout.seats.contains { $0.family == family } }) { family in
                            Label(family.rawValue, systemImage: "circle.fill")
                                .font(.system(size: 10)).foregroundStyle(family.color)
                        }
                    }
                    .padding(.horizontal, 10)
                }.scrollIndicators(.hidden).frame(height: 23)
            }
        }
        .background(canvas, in: RoundedRectangle(cornerRadius: 9))
        .overlay { RoundedRectangle(cornerRadius: 9).strokeBorder(dropTarget ? focus : line.opacity(0.65)) }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Concert Stage, from the conductor")
    }

    private func marker(_ seat: OrchestraSeat, projection: SeatingProjection) -> some View {
        let selected = state.selection == seat.id
        let scale = min(1.17, max(0.67, projection.width / 660))
        let size = 26 * scale
        return Button { state.selection = seat.id } label: {
            InstrumentIcon(name: seat.name, family: seat.family)
                .font(.system(size: 12 * scale, weight: .semibold))
                .frame(width: size, height: size)
                .foregroundStyle(selected ? canvas : seat.family.color)
                .background(selected ? seat.family.color : canvas, in: Circle())
                .overlay { Circle().strokeBorder(selected ? .white : seat.family.color.opacity(0.75), lineWidth: selected ? 2 : 1) }
                .overlay(alignment: .bottom) {
                    Text(seat.label.isEmpty ? seat.name : seat.label)
                        .font(.system(size: max(8, 10 * scale), weight: selected ? .bold : .medium))
                        .lineLimit(1).frame(width: 72 * scale).offset(y: 13 * scale)
                        .foregroundStyle(.white).allowsHitTesting(false)
                }
        }
        .buttonStyle(.plain)
        .help("\(seat.name) · \(seat.section)")
        .accessibilityLabel(seat.label.isEmpty ? seat.name : seat.label)
        .accessibilityValue(seat.section)
        .accessibilityAddTraits(selected ? .isSelected : [])
        .contextMenu {
            Button("Remove Seat", role: .destructive) { state.remove(seat.id) }
        }
        .simultaneousGesture(DragGesture(minimumDistance: 3, coordinateSpace: .named("concertStage"))
            .onChanged { event in
                dragged = seat.id
                if state.selection != seat.id { state.selection = seat.id }
                preview = translated(seat.position, by: event.translation, projection: projection)
            }
            .onEnded { event in
                if dragged == seat.id {
                    let point = translated(seat.position, by: event.translation, projection: projection)
                    state.change("Move instrument") { $0.update(seat.id) { $0.position = point } }
                }
                dragged = nil
                preview = nil
            })
    }

    private func translated(_ position: SeatingPoint, by delta: CGSize, projection: SeatingProjection) -> SeatingPoint {
        let origin = projection.location(x: position.x, depth: position.depth)
        return projection.model(at: CGPoint(x: origin.x + delta.width, y: origin.y + delta.height))
    }

    private func diagram(_ projection: SeatingProjection) -> some View {
        func arc(_ radius: Double) -> Path {
            var path = Path()
            for i in 0...48 {
                let angle = Double(i) * .pi / 48
                let point = projection.location(x: cos(angle) * radius, depth: sin(angle) * radius)
                if i == 0 { path.move(to: point) } else { path.addLine(to: point) }
            }
            return path
        }
        var outline = arc(1)
        outline.closeSubpath()
        var center = Path()
        center.move(to: projection.location(x: 0, depth: 0))
        center.addLine(to: projection.location(x: 0, depth: 1))
        // Native shapes keep the floor in SwiftUI's normal view compositor.
        return ZStack {
            outline.fill(LinearGradient(colors: [panel, Color(red: 0.17, green: 0.21, blue: 0.27)],
                                        startPoint: .top, endPoint: .bottom))
            outline.stroke(line, lineWidth: 1.5)
            ForEach([0.28, 0.49, 0.70, 0.89], id: \.self) { band in
                arc(band).stroke(line.opacity(0.65), lineWidth: 0.8)
            }
            center.stroke(line, style: StrokeStyle(lineWidth: 0.75, dash: [3, 5]))
            Circle().fill(focus).frame(width: 6, height: 6).position(projection.location(x: 0, depth: -0.03))
        }
        .allowsHitTesting(false)
    }
}
