// The Objective-C face of the C++ call controller (apps/call/call_client.h) for the SwiftUI screens (#1116).
//
// It owns one call at a time: GET <server>/turn (the TURN REST credentials; no secret in the app), the signaling
// WebSocket (<server>/ws, wss:// for https://, through URLSessionWebSocketTask), the call thread that runs the
// controller, the microphone and speaker (sm::device Audio Units) and the camera (sm::device AVFoundation).
// Every method is called on the main thread; the state, stats and frames are polled by the UI.
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface SMCallBridge : NSObject
// `server` is the signaling service's base URL, e.g. https://35.165.158.249.sslip.io:8443.
- (void)startWithServer:(NSString *)server room:(NSString *)room video:(BOOL)video;
- (void)hangup;
@property(nonatomic) BOOL muted;
@property(nonatomic) BOOL videoEnabled;
@property(nonatomic, readonly) BOOL active;          // A call is set up or running.
@property(nonatomic, readonly) NSString *stateText;  // "idle", "waiting for peer", "connecting", "connected", ...
@property(nonatomic, readonly) NSString *statsText;  // The live stats, several lines.
// The latest remote video frame since the last call, or nil.
- (nullable UIImage *)takeRemoteFrame;
// A fresh room code for the AI server ("ai-" + 6 digits).
+ (NSString *)newAiRoom;
@end

NS_ASSUME_NONNULL_END
