"""The issue behind each algorithm: what goes wrong in a call without it,
why it happens, and why the obvious fix is not enough. Rendered as the
"The issue" subsection of each algorithm document by tools/gen_docs.py.
"""

ISSUES = {
    # ------------------------------------------------------------------ 3
    "3.1": """\
Video is split into many RTP packets per frame (a 720p keyframe can be 50-100 packets), and a frame can only be
decoded when every one of its packets has arrived. On a link with 1-5% random loss, a frame of 8 packets is
incomplete 8-34% of the time, so without repair the picture freezes or breaks up constantly. The sender still has
the packet, so the cheapest repair is to ask for exactly the missing sequence numbers and resend them.

Doing this naively causes new problems. Asking as soon as a gap appears produces spurious requests when packets
are only reordered. Asking again before the retransmission could have arrived doubles the traffic. Retransmitting
on the original SSRC corrupts the receiver's loss and jitter statistics. Retrying forever keeps requesting packets
that are already too old to be played. The design has to bound retries by RTT and age, put retransmissions on a
separate RTX stream, and give up (and ask for a keyframe) when repair is hopeless.""",
    "3.2": """\
Retransmission costs one round trip: the receiver must notice the gap, the request must reach the sender, and the
resent packet must come back. With a 300 ms RTT and a 250 ms playout budget the repaired packet always arrives too
late, so on long-distance, satellite or cellular paths NACK alone cannot protect interactive video.

Forward error correction sends redundancy before any loss happens, so the receiver can rebuild a lost packet
immediately. The cost is bandwidth that is spent whether or not packets are lost. A parity packet can only rebuild
one missing packet from the set it covers, so which packets each parity covers decides whether random losses or
bursts of consecutive losses are recoverable.""",
    "3.3": """\
Audio packets are small (20 ms, 20-100 bytes) and sent continuously. A lost packet is a 20 ms hole that the
listener hears as a click or a dropout, and retransmission is usually too slow for audio's tight playout delay.
Packet headers (IP/UDP/RTP, about 40 bytes) often outweigh the payload. Repeating the previous frame inside the
next packet therefore adds less than one extra packet's worth of bitrate and repairs isolated losses with only one
frame of extra delay. The receiver must then recognise, by timestamp, which frames it already has and use each
frame exactly once.""",
    "3.4": """\
Some audio will be missing even with FEC, RED and retransmission: a burst longer than the redundancy, or a packet
that arrives after its playout time. The audio device still needs 10 ms of samples every 10 ms. Playing silence
causes an audible gap and clicks at both edges; repeating the last frame verbatim produces a metallic buzz with a
discontinuity at each repetition.

Speech is locally periodic (voiced sounds repeat with the pitch period), so the most natural continuation repeats
the last pitch period with smooth joins. That only sounds right for a short time: after a few tens of milliseconds
the synthetic tone becomes obviously artificial and must fade out. When real audio resumes, the joint must be
crossfaded or it clicks. Opus has its own concealment and FEC, but G.711, raw PCM and many embedded decoders do
not.""",
    "3.5": """\
Video frames depend on earlier frames: a P-frame only stores the difference from its reference. If a frame is lost
for good, every later frame that depends on it, directly or indirectly, is undecodable until the next keyframe.
Most encoders send keyframes rarely or only on request, so the picture freezes indefinitely, or a decoder that
decodes anyway shows smeared, corrupted video.

The receiver must ask for a keyframe (PLI, or FIR when joining a stream), but timing is delicate. Asking at the
first gap wastes a large keyframe when NACK would have repaired the packet a few milliseconds later. Asking too
late lengthens the freeze. Asking repeatedly while the keyframe is in flight causes a request storm of oversized
frames that congests the link further.""",
    "3.6": """\
With a simple reference chain (each frame references the previous one) any single lost frame breaks all following
frames until a keyframe. This is fragile on lossy links, and it is wasteful in group calls: an SFU cannot reduce
the frame rate for a slow receiver, because dropping any frame breaks the chain for everyone downstream.

Temporal scalability arranges references in layers so some frames are never referenced by others. Those frames can
be lost or dropped without consequence. Switching a receiver up to a higher layer is only safe at frames that do
not depend on frames the receiver never got.""",
    "3.7": """\
NACK and FEC protect against loss in different regimes, and a fixed choice is wrong most of the time. NACK costs
almost nothing when there is no loss but adds one round trip of delay per repair, which is useless when the RTT is
long. FEC repairs instantly but always costs bandwidth, which wastes capacity on clean links and steals bitrate
from the video itself.

The right mix depends on the current RTT, the loss rate and whether losses come in bursts, and all three change
during a call (Wi-Fi to LTE, a congested hop). Choosing too little FEC leaves frames broken; choosing too much
reduces quality and can even cause the congestion that creates the loss.""",
    # ------------------------------------------------------------------ 4
    "4.1": """\
UDP gives no ordering or uniqueness guarantee. Packets of one frame can arrive out of order (per-packet load
balancing, Wi-Fi retransmissions, multi-path), duplicated (link-layer retries, mirroring) or interleaved with
packets of other frames. A depacketizer that assumes in-order delivery will build corrupt frames or drop good
ones. Sequence numbers are 16 bits and wrap every 65 536 packets (a few minutes of video), so "older" and "newer"
must be decided on a circle, not by plain comparison.""",
    "4.2": """\
Every adaptive decision needs measurements of the network: the round-trip time (to time retransmissions and
choose FEC), the loss rate (for protection and bandwidth estimation) and the jitter (for buffer sizing). Neither
side can measure these alone. The receiver sees loss and jitter but not RTT. The sender knows when it sent but not
when packets arrived. Clocks on the two machines are not synchronized, so one-way delay cannot be measured
directly at all.

RTCP sender and receiver reports solve this by echoing timestamps. Their fixed-point formats, 32-bit wrap-around
and the rule that "fraction lost" refers only to the interval since the previous report are easy to get subtly
wrong.""",
    "4.3": """\
An audio jitter buffer must hold enough audio to absorb variation in packet arrival time, but every millisecond
buffered is a millisecond of conversational delay. Above about 150 ms one-way delay, people start talking over
each other. A fixed size is wrong in both directions: too small and late packets cause dropouts, too large and
the call feels sluggish even on a good network.

The right size is a statistic of recent network behaviour, such as the delay that 95% of packets beat. It must rise
quickly when jitter starts (or dropouts begin at once) and fall slowly (or the buffer oscillates and audio speeds
up and slows down audibly). Jitter must also be measured relative to the fastest recent packet, since the clocks
are not synchronized and only delay variation is observable.""",
    "4.4": """\
When the target playout delay changes, the audio already buffered must be played faster or slower to reach the
new level. After a network stall, for example, half a second of packets arrives at once. Simply dropping or
repeating whole frames causes audible clicks and stutters. Resampling plays everything faster or slower and
shifts the pitch (chipmunk voices) for as long as the correction lasts.

Speech can instead be shortened or lengthened by whole pitch periods, which listeners do not notice, as long as
the cut points line up with the waveform's periodicity and the signal is actually periodic there. Without this, a
jitter buffer that grew during a spike stays large for the rest of the call.""",
    "4.5": """\
The audio device pulls exactly 10 ms of samples every 10 ms, at a rate the network does not control, while packets
arrive late, early, out of order, duplicated or not at all. Something has to bridge the two clocks: always
produce sound on time, play every packet that arrives in time, conceal the ones that do not, and keep the delay
as small as the network allows.

That requires several cooperating decisions on every 10 ms pull: normal playout, concealment when the next packet
is missing, merging back when it arrives, speeding up when too much is buffered, and slowing down when too little
is. A naive FIFO either underruns on every jitter spike or accumulates latency forever.""",
    "4.6": """\
Video frames vary hugely in size (a keyframe can be 10x a normal frame), and a large frame takes longer to cross a
bottleneck, so its last packet arrives late even on a perfectly smooth network. Network jitter adds random delay
on top. If each frame is shown as soon as it is complete, playback stutters. If a large fixed delay is added, the
call lags.

The receiver must estimate how much extra delay frames can experience: one component that scales with frame size
(link capacity) and one that is random. From that it decides a render time per frame that most frames will make,
and changes that delay slowly enough that playback speed does not visibly jump.""",
    "4.7": """\
The sender's and receiver's audio clocks are separate crystals with tolerances of tens to hundreds of ppm. At
200 ppm, the sender produces 12 ms more (or less) audio per minute than the receiver plays. Over a long call the
jitter buffer slowly fills until latency becomes unacceptable, or drains until it underruns every few seconds.
The drift is far smaller than network jitter, so it cannot be seen packet by packet. It only shows up as a slow
trend that must be estimated robustly and compensated without audible pitch changes.""",
    # ------------------------------------------------------------------ 5
    "5.1": """\
A congestion controller needs to know, for each packet, when it arrived, because queue build-up first shows as
increasing delay well before any loss. Classic RTCP receiver reports give only averages once per second and are
per media stream, so they are too coarse and too slow for delay-based control. Feedback is sent by the receiver
many times per second, so it must encode hundreds of arrivals compactly. The sender must then match it with its own
send times, for every stream sharing the connection.""",
    "5.2": """\
Nobody tells the sender how much bandwidth is available, and it changes during a call (cross traffic, Wi-Fi rate
changes, handovers). Sending too slowly wastes quality. Sending too fast fills router queues: delay grows by
hundreds of milliseconds (bufferbloat) long before packets are dropped, and by the time loss appears the call is
already lagging. Loss-based TCP-style control therefore reacts too late for real-time media.

Rising one-way delay is the early signal, but it is buried in noise (jitter, burst sending, clock offset). A
controller must extract the trend, decide overuse versus normal versus underuse without overreacting to single
spikes, and adjust the rate: probing upward carefully, backing off decisively but not repeatedly for the same
event.""",
    "5.3": """\
Delay is not the only congestion signal. Some links drop packets without first building a queue: shallow buffers,
policers, wireless links with random loss. Others have so much competing traffic that queue delay never stabilizes.
If the sender ignores loss, it can keep sending into a link that discards a large share of its packets.

Not every loss means congestion, though: wireless links lose packets randomly at any rate. A controller that backs
off on every loss starves itself on a lossy-but-fast link. It must distinguish light loss (ignore), moderate loss
(hold) and heavy loss (back off), using enough packets that the measured loss rate means something.""",
    "5.4": """\
Several estimates of the available bandwidth exist at once: delay-based, loss-based, the receiver's REMB hint and
probe results. Each also has configured limits. The encoders need one target rate, and the pacer needs a rate to
send at. Combining them incorrectly makes the system either ignore a real limit (overshooting into congestion) or
let one stale estimate cap everything (stuck at low quality). The combination also has to be wired to the right
inputs: transport feedback, RTT updates and periodic ticks.""",
    "5.5": """\
Encoders produce a whole frame at once, and a keyframe can be tens of kilobytes. Writing it to the socket
instantly sends a line-rate burst that briefly exceeds the bottleneck's capacity. Router queues fill (adding
delay for everything behind the burst), packets are dropped, and the congestion controller sees self-inflicted
congestion and lowers the rate for no good reason.

Audio packets queued behind a video burst arrive late, which is worse because audio dropouts hurt more than video
stutter. Retransmissions are also time-critical. Packets must be spread out at a controlled rate, important
traffic must go first, and the queue must never grow so large that video is seconds old when it is sent.""",
    "5.6": """\
A delay-based controller only learns about capacity it is using: it raises the rate slowly (about 8% per second)
and backs off at the first sign of queueing. Starting a call at 300 kbps on a 5 Mbps link therefore takes tens of
seconds to reach good quality. When the application sends less than the estimate (a static screen share, a
paused video), the estimate is never tested and can become stale in either direction.

Probing sends short bursts at a higher rate (with padding if there is no media) and measures how fast they arrive.
A real probe result lets the estimate jump in one step. The probes must be short, and their results must reject
measurements that are too sparse to trust.""",
    "5.7": """\
A call usually carries several streams over one connection: audio, camera video, perhaps a screen share and data.
They compete for one bandwidth estimate. Splitting it evenly gives audio far more than it can use and starves
video. Giving everything to video can starve audio, which is the one stream that must never stop.

Streams have different minimum useful rates (video below about 100 kbps is not worth sending), maximums and
importance. When bandwidth drops, the least important stream should pause rather than everything degrading
together. When it recovers, streams should not flap on and off around the threshold.""",
    "5.8": """\
Encoders do not hit their target bitrate exactly. Scene changes, keyframes and complex content overshoot, and some
hardware encoders run persistently 20-50% over target. The congestion controller then sees more traffic than it
allowed, queues build, and the controller lowers the target, which the encoder overshoots again. Real
bandwidth is wasted on the overshoot while video quality is cut. Something must enforce the budget by skipping
frames when the encoder has spent too much, and correct the encoder's systematic error, without reacting to the
single large keyframe every encoder legitimately produces.""",
    "5.9": """\
When bandwidth falls, sending 720p at 30 fps with too few bits gives a blocky, unreadable picture. It is better to
send fewer pixels or fewer frames at an adequate quality. Which to sacrifice depends on the content: a sports or
camera feed should keep its frame rate, a slide deck should keep its resolution. The device's CPU can be the
limit too: a phone or wearable that cannot encode 720p in real time drops frames, heats up and drains the battery
regardless of the network.

Without adaptation, quality collapses under constraint. With naive adaptation, the format oscillates around a
threshold, visibly switching resolution every few seconds.""",
    "5.11": """\
Links drop out entirely: a tunnel, an elevator, a Wi-Fi to LTE handover, a NAT rebinding. During the outage nothing
comes back from the far end. A sender that keeps encoding fills its queues (and the network's) with media that will
be seconds old when the link returns. Its bandwidth estimate is meaningless. The far end's decoder state is lost,
so delta frames sent after recovery are undecodable. Recovery needs a deliberate sequence: pause, keep the
connection alive, restart ICE if the path is gone, then resume with a keyframe and a cautious rate on what may now
be a different path.""",
    # ------------------------------------------------------------------ 6
    "6.1": """\
On a speakerphone, laptop or conference device, the far end's voice comes out of the loudspeaker, bounces around
the room and enters the microphone. Without cancellation the far-end talker hears themselves 100-500 ms later,
which makes conversation nearly impossible. At high gain the loop can turn into howling feedback.

The echo is not a simple delayed copy: the room filters it through tens to hundreds of milliseconds of
reflections, which change when someone moves. The canceller must learn this response continuously from the
signal itself. It must not learn from the local talker's voice when both people speak at once (double talk),
or it will cancel the local voice instead. Whatever the linear model cannot capture (loudspeaker distortion,
model error) must be suppressed without muting the local talker.""",
    "6.2": """\
Between the moment far-end audio is handed to the speaker and the moment its echo reaches the microphone there is a
large, device-dependent delay: OS audio buffers, DAC and ADC, Bluetooth links. It ranges from 20 ms to over
300 ms, and it changes when the user switches from the speaker to a Bluetooth headset. An echo canceller with an
80 ms filter cannot model a 200 ms delay. If its far-end reference is misaligned it converges poorly or not at all.
The delay must be measured from the audio itself, robustly against room reflections and the local talker, and
re-measured when the route changes.""",
    "6.3": """\
Microphones pick up fans, traffic, keyboards, cafes and car noise. The noise masks speech, tires listeners, and wastes
bitrate because the encoder spends bits coding noise faithfully. The noise overlaps speech in frequency, so a fixed
filter cannot remove it.

Suppression must estimate the noise spectrum while people are talking, from moments and frequencies where speech
is absent, and attenuate each frequency according to how much speech it contains at that moment. Doing this
crudely produces "musical noise" (random warbling tones) or muffles and chops the speech, which can sound worse
than the original noise.""",
    "6.4": """\
Talkers differ by 30 dB or more in level: a quiet speaker far from a laptop versus someone leaning into a headset
microphone. Without gain control, listeners constantly adjust their volume, quiet speakers are lost under noise,
and loud ones clip, which causes harsh distortion that no later processing can undo.

Fixed gain cannot serve both. Adaptive gain must follow the speech level, not the noise (or it amplifies silence into
loud hiss). It must change slowly enough not to "pump", and always guarantee that the output never exceeds full
scale, even on a sudden shout.""",
    "6.5": """\
Many microphones and ADCs add a DC offset, and the environment adds low-frequency energy below the speech band:
mains hum at 50/60 Hz, handling noise, wind, desk vibration. Speech carries almost no useful information below
about 100 Hz, but this energy dominates level measurements. It confuses VAD and AGC (which react to rumble as if
it were speech), wastes encoder bits, and can drive the echo canceller's adaptation. It must be removed before
everything else, without colouring the speech band.""",
    "6.6": """\
Keyboard clicks are very common in calls (people take notes while talking) and very distracting: loud, sharp,
broadband transients a few milliseconds long. Noise suppression does not remove them, because it relies on noise
being steady over time and clicks are the opposite. The challenge is telling a keystroke from the start of a
spoken syllable, especially a plosive like "p" or "t". Wrong decisions either let clicks through or clip the
beginning of words.""",
    "6.7": """\
In a conversation each participant is silent about half the time, yet a naive sender keeps encoding and sending
background noise at full packet rate. That wastes bandwidth, radio power and battery (important on mobile and
wearables) and network capacity in large calls.

Simply not sending during silence creates a new problem: the receiver plays digital silence, which sounds like
the call dropped, and the background noise "pumps" on and off around each word. The receiver needs a compact
description of the background noise (level and spectral shape), updated only when it changes, to synthesize
comfort noise that sounds like the real room.""",
    "6.8": """\
Several components need to know whether someone is speaking. DTX transmits only during speech, AGC measures level
only on speech, the transient suppressor protects speech, and the audio level extension carries its result. Energy alone
is not enough: a loud fan has more energy than a soft voice, and the background level changes during a call. A
detector that confuses noise with speech wastes bandwidth and amplifies noise. One that misses speech clips the
starts and ends of words.""",
    "6.9": """\
A receiver often wants to know how loud each speaker is, or whether the packet holds speech, without decoding
it: for a level meter, or when the audio is encrypted end to end and only the sender can measure it. The sender
therefore attaches a one-byte level and voice flag to each packet (the RFC 6464 header extension).

The level has to be computed the same way on every device (RMS in -dBov, 127 for digital silence) and packed
into seven bits next to the voice flag, so that any receiver, including libwebrtc, reads it correctly.""",
    "6.11": """\
Audio hardware runs at whatever rate the device prefers: 44.1 kHz on many Macs and USB devices, 48 kHz on most
phones, 16 kHz on some embedded codecs and Bluetooth headsets. Processing runs at 16 or 48 kHz, and codecs at their
own rates. Converting between rates is unavoidable.

Crude conversion (dropping samples or linear interpolation) folds high frequencies back into the audible band
(aliasing) and dulls the sound. Conversion between rates that are not simple multiples, like 44.1 kHz and 48 kHz,
needs fractional-ratio resampling that keeps exact sample counts over time.""",
    "6.12": """\
Microphones and browsers capture at 32 or 48 kHz, but the speech-specific work in audio processing (echo
cancellation, noise suppression) is designed for and cheapest at 16 kHz, where most speech energy is. libwebrtc
therefore splits each 10 ms frame into 16 kHz bands, runs AEC3 and NS on the lowest one, applies a simple gain to the
upper ones and merges them again.

The split has to keep the bands apart (so the suppression of one band does not leak into another as aliasing) and
put the signal back together at its own level without audible artefacts, with as little added delay as possible.""",
    # ------------------------------------------------------------------ 7
    "7.1": """\
When a frame cannot be decoded, the receiver keeps showing the last good picture, and the user sees frozen
video. Freezes are the most noticeable video quality problem, yet average frame-rate statistics hide them: one
2-second freeze in a minute barely moves the average fps. Operators need a consistent definition of what counts as
a freeze (versus normal frame-rate variation or a deliberate pause), plus counts and durations, to detect and
debug bad calls.""",
    "7.2": """\
Hardware encoders and decoders (MediaCodec, VideoToolbox, V4L2) are essential for battery life and performance,
but they fail in practice. Some refuse certain resolutions, fail to initialize when another app holds the
hardware, return errors under memory pressure, or have device-specific bugs. If the application simply stops when
the hardware codec fails, the call goes black. Switching to software must happen quickly, but not on a single
transient error. The new codec has no reference state, so a keyframe is needed immediately. Hardware should
perhaps be retried later without flapping back and forth.""",
    "7.3": """\
Phone and tablet camera sensors are mounted in landscape. When the user holds the device upright, or turns it during
the call, the captured image is rotated relative to what should be displayed. Rotating every frame before encoding
costs CPU and memory bandwidth, and it changes the encoded resolution (portrait versus landscape) mid-stream, which
forces new keyframes and encoder reconfiguration on every turn. Front cameras are also mirrored. The receiver needs
to know the capture orientation for each frame to display it upright.""",
    "7.4": """\
Not all video is the same. For a camera feed, smooth motion matters most, so under constraint the frame rate should
be kept. For a shared document or code, legible text matters most: halving the resolution or raising the quantizer
makes text unreadable, while a lower frame rate is barely noticed. Screen content is also static most of the time.
Encoding 30 identical frames per second wastes CPU and bandwidth. The encoder pipeline needs to know what kind of
content it has, adapt in the right direction, and skip frames where nothing changed, while still sending a changed
frame immediately.""",
    "7.5": """\
Every endpoint supports a different set of codecs (VP8, VP9, H.264, AV1, H.265), each in profiles and levels with
different capabilities. Some are hardware accelerated on a given device. Two endpoints must agree on what to use,
in a single offer/answer exchange. Some rules are codec specific: two H.264 entries with different profiles are
different codecs, but different levels just limit resolution and frame rate. Getting this wrong means black video
(sending a profile the decoder cannot handle), poor battery (picking software when hardware was available) or
mismatched payload types (packets sent with numbers the other side maps to a different codec).""",
    "7.6": """\
Adaptation changes the encoded resolution mid-call (for bandwidth, CPU, quality or simulcast layer), and displays
come in arbitrary sizes and aspect ratios: a 4:3 camera in a 16:9 tile, a portrait phone in a landscape grid.
Frames must be scaled on both sides. Cheap scaling (nearest-neighbour or skipping pixels) aliases fine detail into
moiré and jagged edges, and mismatched aspect handling stretches faces. The scaler is also one of the most
CPU-intensive steps on small devices, so quality and cost both matter.""",
    "7.7": """\
Decoded frames become ready at irregular times because of network jitter, frame-size differences and decode time.
Showing each frame the moment it is ready makes motion judder. Playback should follow the capture timing, which
means deciding a render time per frame. When a frame misses its render time anyway (a delay spike), showing it late
only delays the frames behind it. After a stall, a backlog of frames must not be played in slow motion. The
renderer must stay smooth while dropping only what cannot be shown in time.""",
    "7.8": """\
The bandwidth estimate alone does not tell the sender whether its resolution is right. Content complexity matters:
a talking head at 300 kbps looks fine at 640x360, while a busy scene at the same bitrate falls apart. The encoder
reports how hard it is quantizing (QP). A consistently high QP means the picture is blocky and a lower resolution
would look better; a consistently low QP means bits are to spare and resolution could rise. QP is noisy from frame to
frame, its scale differs per codec, and decisions must not oscillate.""",
    # ------------------------------------------------------------------ 8
    "8.1": """\
Each RTP stream has its own timestamp clock with a random starting value (48 kHz for Opus audio, 90 kHz for video),
unrelated to any wall clock and unrelated to the other streams. To synchronize streams or measure latency, the
receiver must convert RTP timestamps into a common time base. RTCP sender reports provide pairs of (wall clock, RTP
timestamp), but they arrive only about once per second, the media clock may drift relative to the sender's wall
clock, and a sender can restart its RTP clock. Between reports, any RTP timestamp must still map accurately.""",
    "8.2": """\
Audio and video travel separately and are buffered differently: the audio jitter buffer might hold 60 ms, while
video needs decode time and a render queue, and the two may even take different network paths. If each stream
simply plays as soon as possible, video typically lags audio by tens to hundreds of milliseconds and lips visibly
miss the words. People notice when audio leads video by more than about 45 ms, or lags it by more than about
125 ms (ITU-R BT.1359).

Delays change during a call (jitter buffer growth, route changes), so synchronization must be continuous. It must
only apply to streams from the same source (same CNAME), and its corrections must be small and gradual so they are
not themselves noticeable.""",
    "8.3": """\
Glass-to-glass latency (from camera capture to display on the far side) is the number that matters for
interactivity, but it is hard to measure. RTP timestamps start at random values and are rewritten by mixers and
SFUs. Sender and receiver clocks are not synchronized, often by seconds. A capture-time field that travels with
the media, plus an estimate of the clock offset between machines, makes latency measurable through middleboxes.
Sending the field in every packet costs bandwidth, so it should only be sent when the receiver cannot work it out
itself.""",
    "8.4": """\
With BUNDLE, all audio, video, simulcast layers and retransmission streams of a call share one UDP port. The receiver
must route every incoming packet to the right decoder. SSRCs are chosen randomly by the sender, can change mid-call
(encoder restart, simulcast reconfiguration) and are not known in advance. The identity of a stream is instead
carried in header extensions (MID for the media section, RID for the simulcast layer). These extensions are only
included in the first packets of each SSRC to save bytes, may be reordered, and use two different encodings
depending on their size.""",
    # ------------------------------------------------------------------ 9
    "9.1": """\
A call needs a steady side channel next to the media. The sender must tell the receiver how its RTP timestamps map
to wall-clock time (sender reports) and the receiver must report loss, jitter and delay so the sender can measure the
round trip. When something goes wrong the receiver must say so at once: which packets are missing, that a keyframe is
needed, which packets arrived and when. These rules are easy to get subtly wrong: reports sent too often waste
bandwidth, too rarely leave RTT stale, and every compound packet must start with a report and carry the CNAME that
lets the receiver group audio and video for lip-sync.""",
    "9.2": """\
Without a sender component, every application turns encoded frames into packets itself: sequence numbers, payload
types, timestamps, marker bits, RED wrapping for audio, a history for retransmission and the pacing that keeps a keyframe
from leaving as one burst. Mistakes here are silent: a wrong sequence number breaks NACK, a packet missing from the
history cannot be repaired, and an unpaced burst fills the bottleneck queue. The congestion controller also needs
every packet numbered and timed in the same order in which it is sent, so packetizing and pacing have to agree.""",
    "9.3": """\
The receive path has to do many things in a fixed order with packets that arrive late, twice, out of order or not at
all: parse, restore retransmitted packets, recover with FEC, track gaps for NACK, assemble frames, decide when a keyframe
is needed and schedule each decodable frame for playout. An application that wires these modules by hand tends to get
the order or the feedback throttles wrong, which shows up as extra freezes and as keyframe storms.""",
    "9.4": """\
The library provides the building blocks, but an app that passes frames and datagrams wants one object per call, not a
wiring diagram. Without it, each integration repeats the wiring (and its bugs), and the test bench tests glue code
that no real app shares. A session facade makes the tested path the same path an application uses, and
lets every packet cross the simulated network as bytes.""",
    "9.5": """\
Video must be carried in RTP packets small enough to fit the path MTU. A keyframe is far larger than one packet, so the
payload format decides how an access unit is split and how the receiver finds where a frame starts and ends. H.264
(RFC 6184) defines single NAL unit packets, aggregation of small NAL units (STAP-A, typically SPS and PPS) and
fragmentation of large ones (FU-A). A packetizer or depacketizer that disagrees with libwebrtc's makes the call fail
to decode, so the format is checked against libwebrtc's own parser.""",
}
