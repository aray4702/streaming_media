// The Objective-C face of the C++ call controller (apps/call/call_client.h) for the SwiftUI screens (#1116).
//
// It owns one call at a time: GET <server>/turn (the TURN REST credentials; no secret in the app), the signaling
// WebSocket (<server>/ws, wss:// for https://, through URLSessionWebSocketTask), the call thread that runs the
// controller, the microphone and speaker (sm::device Audio Units) and the camera (sm::device AVFoundation).
// Every method is called on the main thread; the state and stats are polled by the UI, and the remote video view draws
// each decoded frame at the display's refresh (RemoteVideoView.h, #1133).
//
// Group calls (C20, #1172): joinGroupWithServer GETs <server>/lk-token (a LiveKit token for the room and this app's
// identity; no secret in the app) and opens LiveKit's /rtc WebSocket (the URL /lk-token returns, else the server's
// origin, where nginx routes /rtc to LiveKit) with binary frames; the controller runs LiveKit's two connections and
// receives every participant. The UI shows a grid: the local preview and one tile per participant, each with its own
// Metal view (videoViewForParticipant, the #1134 renderer), its speaker state, mute state and stats.
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

// A remote participant of a group call, as the grid shows it (a snapshot, refreshed by each poll).
@interface SMParticipant : NSObject
@property(nonatomic, readonly) NSString *identity;
@property(nonatomic, readonly) BOOL speaking;     // An active speaker (LiveKit's speaker updates, else the local meter).
@property(nonatomic, readonly) double audioLevel;  // 0 to 1.
@property(nonatomic, readonly) BOOL audioMuted;    // Its microphone is muted or not published.
@property(nonatomic, readonly) BOOL videoMuted;    // Its camera is off or not published.
@property(nonatomic, readonly) BOOL hasVideo;      // A video track of it is being received.
@property(nonatomic, readonly) NSString *statsText;  // Two short lines: audio and video receive stats.
@end

@interface SMCallBridge : NSObject
// `server` is the signaling service's base URL, e.g. https://35.165.158.249.sslip.io:8443.
- (void)startWithServer:(NSString *)server room:(NSString *)room video:(BOOL)video;
// A group call in the LiveKit room `room` (C20).
- (void)joinGroupWithServer:(NSString *)server room:(NSString *)room video:(BOOL)video;
- (void)hangup;
@property(nonatomic) BOOL muted;
@property(nonatomic) BOOL videoEnabled;
// TURN candidates only (iceTransportPolicy "relay", #1121); read when a call starts.
@property(nonatomic) BOOL relayOnly;
@property(nonatomic, readonly) BOOL active;          // A call is set up or running.
@property(nonatomic, readonly) NSString *stateText;  // "idle", "waiting for peer", "connecting", "connected", ...
@property(nonatomic, readonly) NSString *statsText;  // The live stats, several lines.
// The remote video (an MTKView drawing each new decoded frame on the GPU, as RTCMTLVideoView).
@property(nonatomic, readonly) UIView *remoteView;
// Group calls (C20): whether the call is one, its participants (in the order they joined), this app's identity and
// speaker state, a Metal view per participant (made on first use, kept for the call) and the local camera's preview.
@property(nonatomic, readonly) BOOL inGroup;
@property(nonatomic, readonly) NSArray<SMParticipant *> *participants;
@property(nonatomic, readonly) NSString *localIdentity;
@property(nonatomic, readonly) BOOL localSpeaking;
- (UIView *)videoViewForParticipant:(NSString *)identity;
@property(nonatomic, readonly) UIView *localView;
// A fresh room code for the AI server ("ai-" + 6 digits).
+ (NSString *)newAiRoom;
@end

NS_ASSUME_NONNULL_END
