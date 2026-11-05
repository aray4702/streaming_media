// The one screen (#1116): server URL, room code, Call AI / Join room / Group, video and mute switches, hang-up, the
// call's state, the remote video (or, in a group call, the grid of up to 6) and the live stats.
import SwiftUI

// The bridge's Metal view (RemoteVideoView.h), which draws every received frame itself; SwiftUI only places it.
struct RemoteVideo: UIViewRepresentable {
    let view: UIView
    func makeUIView(context: Context) -> UIView { view }
    func updateUIView(_ uiView: UIView, context: Context) {}
}

// One tile of the group grid (C20, #1172): the participant's video (its own Metal view), or its initials when its
// camera is off; its name, a muted microphone, an audio level bar, and a green frame while it is an active speaker.
struct Tile: View {
    let name: String
    let view: UIView
    let speaking: Bool
    let level: Double
    let audioMuted: Bool
    let showVideo: Bool
    let stats: String

    var body: some View {
        ZStack(alignment: .bottomLeading) {
            RemoteVideo(view: view)
                .opacity(showVideo ? 1 : 0)
            if !showVideo {
                Text(String(name.prefix(2)).uppercased())
                    .font(.title).bold()
                    .foregroundColor(.white)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 4) {
                    if audioMuted { Image(systemName: "mic.slash.fill").foregroundColor(.red) }
                    Text(name).font(.caption2).bold().foregroundColor(.white).lineLimit(1)
                }
                if !stats.isEmpty {
                    Text(stats).font(.system(size: 8, design: .monospaced)).foregroundColor(.white).lineLimit(2)
                }
                GeometryReader { g in
                    Capsule().fill(Color.green).frame(width: g.size.width * min(1, max(0, level)), height: 3)
                }
                .frame(height: 3)
            }
            .padding(4)
        }
        .aspectRatio(4.0 / 3.0, contentMode: .fit)
        .background(Color(white: 0.15))
        .clipShape(RoundedRectangle(cornerRadius: 6))
        .overlay(RoundedRectangle(cornerRadius: 6).stroke(speaking ? Color.green : Color.clear, lineWidth: 3))
    }
}

struct ContentView: View {
    @StateObject private var model = CallModel()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text("SmCall").font(.largeTitle).bold()
                TextField("Server", text: $model.server)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .keyboardType(.URL)
                    .disabled(model.active)
                TextField("Room code", text: $model.room)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .disabled(model.active)
                HStack {
                    Button("Call AI") { model.callAI() }
                        .buttonStyle(.borderedProminent)
                        .disabled(model.active)
                    Button("Join room") { model.joinRoom() }
                        .buttonStyle(.bordered)
                        .disabled(model.active || model.room.isEmpty)
                    Button("Group") { model.joinGroup() }
                        .buttonStyle(.bordered)
                        .disabled(model.active || model.room.isEmpty)
                    Spacer()
                    Button("Hang up", role: .destructive) { model.hangup() }
                        .buttonStyle(.borderedProminent)
                        .disabled(!model.active)
                }
                HStack {
                    Toggle("Video", isOn: $model.video)
                    Toggle("Mute", isOn: $model.muted)
                    Toggle("Relay only", isOn: $model.relay).disabled(model.active)
                }
                Text("State: \(model.state)").font(.headline)
                if model.inGroup {
                    groupGrid
                } else if model.active {
                    RemoteVideo(view: model.bridge.remoteView)
                        .aspectRatio(4.0 / 3.0, contentMode: .fit)
                        .frame(maxWidth: .infinity)
                }
                Text(model.stats)
                    .font(.system(.caption, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .padding()
        }
    }

    // Up to 6 tiles: this app's camera first, then the others in the order they joined; one column for one tile,
    // two for more.
    private var groupGrid: some View {
        let others = Array(model.participants.prefix(5))
        let columns = Array(repeating: GridItem(.flexible(), spacing: 6), count: others.isEmpty ? 1 : 2)
        return VStack(alignment: .leading, spacing: 6) {
            Text("\(others.count + 1)/6 in the room").font(.caption)
            LazyVGrid(columns: columns, spacing: 6) {
                Tile(name: "You", view: model.bridge.localView, speaking: model.localSpeaking, level: 0,
                     audioMuted: model.muted, showVideo: model.video, stats: model.localIdentity)
                ForEach(others, id: \.identity) { p in
                    Tile(name: p.identity, view: model.bridge.videoView(forParticipant: p.identity),
                         speaking: p.speaking, level: p.audioLevel, audioMuted: p.audioMuted,
                         showVideo: p.hasVideo && !p.videoMuted, stats: p.statsText)
                }
            }
        }
    }
}
