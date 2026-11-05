# LiveKit protocol (vendored)

These are the `.proto` files of LiveKit's client signaling, which the native LiveKit client in `apps/call`
(`livekit_proto.h`, C19, #1164) follows. They are copied unchanged from
[livekit/protocol](https://github.com/livekit/protocol) at the commit that LiveKit server v1.13.11 pins
(`go.mod`: `github.com/livekit/protocol v1.53.1-0.20261023175527-46332a036e25`).

| | |
|---|---|
| Commit | `46332a036e252b59880320f00376273630232005` |
| Source tarball | `https://codeload.github.com/livekit/protocol/tar.gz/46332a036e252b59880320f00376273630232005`, sha256 `4e5afba3d99a9b557d581236efec284ce1da3d8de2f63d003b40abd560a98e4e` |
| Files | `protobufs/livekit_rtc.proto` (SignalRequest, SignalResponse), `protobufs/livekit_models.proto` (Room, ParticipantInfo, TrackInfo, VideoLayer), and the two files they import: `protobufs/livekit_metrics.proto` and `protobufs/logger/options.proto` |
| License | Apache-2.0 ([LICENSE](LICENSE), the repository's own) |

`SHA256SUMS` lists the files. Nothing compiles them: there is no protoc in the build and no protobuf runtime. The
client encodes and decodes only the fields it uses, with a small hand-written codec (`apps/call/livekit_proto.cc`),
and cites the field numbers from these files. The server's own copy of the protocol at that commit is the reference
for its behaviour (`pkg/rtc`, `pkg/service` in livekit/livekit v1.13.11).

When the deployed LiveKit server is upgraded (`docs/deploy/livekit.md`), take these files from the commit that its
`go.mod` pins. Check that the fields used are unchanged (protobuf keeps field numbers stable) and rerun
`unit.LiveKitProto` and `interop.WebApp.group_native`.
