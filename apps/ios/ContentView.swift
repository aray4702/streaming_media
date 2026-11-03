// The one screen (#1116): server URL, room code, Call AI / Join room, video and mute switches, hang-up, the call's
// state, the remote video and the live stats.
import SwiftUI

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
                    Spacer()
                    Button("Hang up", role: .destructive) { model.hangup() }
                        .buttonStyle(.borderedProminent)
                        .disabled(!model.active)
                }
                HStack {
                    Toggle("Video", isOn: $model.video)
                    Toggle("Mute", isOn: $model.muted)
                }
                Text("State: \(model.state)").font(.headline)
                if let frame = model.remote {
                    Image(uiImage: frame)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(maxWidth: .infinity)
                        .background(Color.black)
                }
                Text(model.stats)
                    .font(.system(.caption, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .padding()
        }
    }
}
