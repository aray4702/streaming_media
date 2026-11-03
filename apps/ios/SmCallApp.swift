// SmCall (C14, #1116): a small calling app on the streaming_media library. The screens are SwiftUI; the call runs in
// the C++ controller behind SMCallBridge (CallBridge.h).
import SwiftUI

@main
struct SmCallApp: App {
    var body: some Scene {
        WindowGroup { ContentView() }
    }
}

final class CallModel: ObservableObject {
    static let defaultServer = "https://35.165.158.249.sslip.io:8443"

    let bridge = SMCallBridge()
    @Published var server: String
    @Published var room = ""
    @Published var state = "idle"
    @Published var stats = ""
    @Published var active = false
    @Published var remote: UIImage?
    @Published var muted = false { didSet { bridge.muted = muted } }
    @Published var video = true { didSet { bridge.videoEnabled = video } }
    @Published var relay = false { didSet { bridge.relayOnly = relay } }  // TURN only (#1121).
    private var timer: Timer?
    private var autoHangupAt: Date?

    init() {
        // Launch arguments for the simulator smoke run: -SMServer URL -SMAutoCall ROOM [-SMAutoHangup SECONDS]
        // [-SMVideo NO] [-SMRelay YES].
        let defaults = UserDefaults.standard
        server = defaults.string(forKey: "SMServer") ?? CallModel.defaultServer
        if defaults.object(forKey: "SMVideo") != nil { video = defaults.bool(forKey: "SMVideo") }
        relay = defaults.bool(forKey: "SMRelay")
        bridge.relayOnly = relay
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.poll() }
        if let auto = defaults.string(forKey: "SMAutoCall") {
            room = auto
            let seconds = defaults.double(forKey: "SMAutoHangup")
            if seconds > 0 { autoHangupAt = Date().addingTimeInterval(seconds) }
            start(room: auto)
        }
    }

    func callAI() {
        room = SMCallBridge.newAiRoom()
        start(room: room)
    }

    func joinRoom() {
        let code = room.trimmingCharacters(in: .whitespaces)
        if !code.isEmpty { start(room: code) }
    }

    func hangup() {
        bridge.hangup()
        remote = nil
    }

    private func start(room: String) {
        remote = nil
        bridge.start(withServer: server, room: room, video: video)
        bridge.muted = muted
    }

    private func poll() {
        state = bridge.stateText
        stats = bridge.statsText
        active = bridge.active
        if let frame = bridge.takeRemoteFrame() { remote = frame }
        if let at = autoHangupAt, Date() >= at {
            autoHangupAt = nil
            NSLog("SmCall stats at hang-up:\n%@", stats)
            hangup()
        }
    }
}
