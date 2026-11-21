// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit
import SwiftUI

typealias SeatingCommit = @convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?, UnsafePointer<CChar>?) -> Void

@MainActor private final class SeatingHost {
    let state: SeatingEditorState
    let view: NSHostingView<SeatingEditor>

    init(context: UnsafeMutableRawPointer?, commit: SeatingCommit?) {
        let state = SeatingEditorState()
        self.state = state
        view = NSHostingView(rootView: SeatingEditor(state: state))
        view.sizingOptions = []
        state.onCommit = { json, name in
            json.withCString { data in name.withCString { action in commit?(context, data, action) } }
        }
    }

}

private func host(_ handle: UnsafeMutableRawPointer) -> SeatingHost {
    Unmanaged<SeatingHost>.fromOpaque(handle).takeUnretainedValue()
}

@MainActor @_cdecl("studioSeatingCreate")
func studioSeatingCreate(_ context: UnsafeMutableRawPointer?, _ commit: SeatingCommit?) -> UnsafeMutableRawPointer {
    Unmanaged.passRetained(SeatingHost(context: context, commit: commit)).toOpaque()
}

@MainActor @_cdecl("studioSeatingNativeView")
func studioSeatingNativeView(_ handle: UnsafeMutableRawPointer) -> UnsafeMutableRawPointer {
    Unmanaged.passUnretained(host(handle).view).toOpaque()
}

@MainActor @_cdecl("studioSeatingRelease")
func studioSeatingRelease(_ handle: UnsafeMutableRawPointer) {
    host(handle).state.onCommit = nil
    Unmanaged<SeatingHost>.fromOpaque(handle).release()
}

@MainActor @_cdecl("studioSeatingRestore")
func studioSeatingRestore(_ handle: UnsafeMutableRawPointer, _ data: UnsafePointer<CChar>?, _ available: Int32) {
    host(handle).state.restore(data.map { String(cString: $0) } ?? "", available: available != 0)
}

@MainActor @_cdecl("studioSeatingAppearance")
func studioSeatingAppearance(_ handle: UnsafeMutableRawPointer, _ scale: Double, _ text: Double) {
    let state = host(handle).state
    if state.interfaceScale != scale { state.interfaceScale = scale }
    if state.textScale != text { state.textScale = text }
}

#if STUDIO_SEATING_TESTS
@MainActor @_cdecl("studioSeatingExercise")
func studioSeatingExercise(_ handle: UnsafeMutableRawPointer, _ step: Int32,
                           _ screenshot: UnsafePointer<CChar>?) -> Int32 {
    let state = host(handle).state
    guard state.available else { return 0 }
    if step == 0 {
        guard state.layout.seats.isEmpty else { return 0 }
        state.change("Load Modern Symphony") { $0 = SeatingCatalog.modernSymphony() }
        state.selection = state.layout.seats.first?.id
        return state.layout.seats.count == 24 && capture(state, screenshot) ? 1 : 0
    }
    if step == 1 {
        guard let instrument = SeatingCatalog.byID["v4-soprano-solo"] else { return 0 }
        state.place(instrument, at: SeatingPoint(x: 0.53, depth: 0.48))
        guard let id = state.selection else { return 0 }
        state.change("Set seating section") { $0.update(id) { $0.section = "Guest Soloists" } }
        state.change("Move instrument") { $0.update(id) { $0.position = SeatingPoint(x: 0.58, depth: 0.46) } }
        return state.layout.seats.count == 25 ? 1 : 0
    }
    guard let seat = state.layout.seats.last,
          state.layout.seats.count == 25, seat.instrumentID == "v4-soprano-solo", seat.section == "Guest Soloists",
          seat.position == SeatingPoint(x: 0.58, depth: 0.46) else { return 0 }
    state.selection = seat.id
    return capture(state, screenshot) ? 1 : 0
}

@MainActor private func capture(_ state: SeatingEditorState, _ path: UnsafePointer<CChar>?) -> Bool {
    guard let path else { return true }
    // Render the production SwiftUI view at a predictable size for CI review.
    let image = ImageRenderer(content: SeatingEditor(state: state).frame(width: 1180, height: 560))
    image.scale = 1
    guard let cgImage = image.cgImage,
          let png = NSBitmapImageRep(cgImage: cgImage).representation(using: .png, properties: [:]) else { return false }
    return (try? png.write(to: URL(fileURLWithPath: String(cString: path)), options: .atomic)) != nil
}
#endif
