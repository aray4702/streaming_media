// SmCall (C14, #1116): a small calling app on the streaming_media library. The screens are SwiftUI; the call runs in
// the C++ controller behind SMCallBridge (CallBridge.h). Group calls through LiveKit (C20, #1172): Group joins the
// room code's LiveKit room and the screen shows the grid.
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
    @Published var muted = false { didSet { bridge.muted = muted } }
    @Published var video = true { didSet { bridge.videoEnabled = video } }
    @Published var relay = false { didSet { bridge.relayOnly = relay } }  // TURN only (#1121).
    // A group call (C20): the remote participants and this app's speaker state, refreshed with each poll.
    @Published var inGroup = false
    @Published var participants: [SMParticipant] = []
    @Published var localIdentity = ""
    @Published var localSpeaking = false
    private var timer: Timer?
    private var autoHangupAt: Date?

    init() {
        // Launch arguments for the simulator smoke runs: -SMServer URL -SMAutoCall ROOM | -SMAutoGroup ROOM
        // [-SMAutoHangup SECONDS] [-SMVideo NO] [-SMRelay YES].
        let defaults = UserDefaults.standard
        server = defaults.string(forKey: "SMServer") ?? CallModel.defaultServer
        if defaults.object(forKey: "SMVideo") != nil { video = defaults.bool(forKey: "SMVideo") }
        relay = defaults.bool(forKey: "SMRelay")
        bridge.relayOnly = relay
        // The stats only; the videos draw themselves at the display refresh (RemoteVideoView.h, #1133).
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.poll() }
        let seconds = defaults.double(forKey: "SMAutoHangup")
        if let auto = defaults.string(forKey: "SMAutoCall") {
            room = auto
            if seconds > 0 { autoHangupAt = Date().addingTimeInterval(seconds) }
            start(room: auto)
        } else if let auto = defaults.string(forKey: "SMAutoGroup") {
            room = auto
            if seconds > 0 { autoHangupAt = Date().addingTimeInterval(seconds) }
            bridge.joinGroup(withServer: server, room: auto, video: video)
            bridge.muted = muted
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

    // The room code's LiveKit room, with everyone in it (C20).
    func joinGroup() {
        let code = room.trimmingCharacters(in: .whitespaces)
        if code.isEmpty { return }
        bridge.joinGroup(withServer: server, room: code, video: video)
        bridge.muted = muted
    }

    func hangup() {
        bridge.hangup()
    }

    private func start(room: String) {
        bridge.start(withServer: server, room: room, video: video)
        bridge.muted = muted
    }

    private func poll() {
        state = bridge.stateText
        stats = bridge.statsText
        active = bridge.active
        inGroup = bridge.inGroup
        if inGroup {
            participants = bridge.participants
            localIdentity = bridge.localIdentity
            localSpeaking = bridge.localSpeaking
        } else if !participants.isEmpty {
            participants = []
        }
        if let at = autoHangupAt, Date() >= at {
            autoHangupAt = nil
            NSLog("SmCall stats at hang-up:\n%@", stats)
            hangup()
        }
    }
}
