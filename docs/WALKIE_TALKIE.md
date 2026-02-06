# Walkie Talkie Implementation (FSK + Codec2)

This document describes the walkie talkie implementation on T-LoRa-Pager (SX1262).
It covers signal flow, protocol framing, buffering, and tuning knobs.

## Scope and Goals
- Device: LilyGo T-LoRa-Pager (SX1262 + ES8311).
- Mode: FSK voice over LoRa radio, half-duplex (TX or RX).
- Target latency: ~200ms when RF is good, adapt to ~300ms when RF is poor.
- Emphasis: stability and intelligibility over absolute minimum latency.

## File Locations
- Core service: `src/walkie/walkie_service.cpp`
- UI: `src/ui/ui_walkie_talkie.cpp`
- Input hook: `src/ui/LV_Helper_v9.cpp`

## Radio Configuration (FSK)
Radio is switched from LoRa to FSK when entering walkie mode.

- Bitrate: 9.6 kbps
- Frequency deviation: 5.0 kHz
- RX bandwidth: 156.2 kHz
- Preamble: 16
- Sync word: 0x2D 0x01
- CRC: 2 bytes

These values are set in `configure_fsk()` in `walkie_service.cpp`.

## Audio Configuration
- Codec: ES8311 (I2S)
- I2S format: 16-bit, 2 channels
- Sample rate: 8000 Hz
- Codec2 mode: 3200 bps

We read I2S stereo, mix to mono for Codec2, then duplicate mono to stereo for playback.

## Packet Framing
Each RF packet contains 5 Codec2 frames (about 100 ms of audio).

Header (14 bytes):
- 0: 'W'
- 1: 'T'
- 2: version (2)
- 3: type (0x01 for voice)
- 4..7: self_id (u32, LE)
- 8..9: session_id (u16, LE)
- 10..11: seq (u16, LE)
- 12..13: frame0_index (u16, LE)

Payload:
- 5 Codec2 frames, each `bytes_per_frame` bytes.

### Airtime Budget Note
At 9.6 kbps FSK:
- Codec2 3200 bps, 20 ms/frame, 8 bytes/frame (typical) → 5 frames ≈ 40 bytes
- Header ≈ 9 bytes → ~50 bytes per 100 ms
- 50 bytes ≈ 400 bits per 100 ms ≈ 4 kbps (payload + header only)

This fits 9.6 kbps in clean RF. However, preamble, sync, CRC,
state transitions, and losses will reduce effective throughput.
So it can be “smooth” at short range and still become choppy in noisy RF.

## TX Pipeline (Stable and Buffered)
1) Capture I2S audio (stereo).
2) Mix L/R -> mono, apply TX gain (`kTxPcmGain`).
3) Codec2 encode (3200) into frame.
4) Push frames into TX frame queue (max 20 frames).
5) When TX is idle and >=5 frames are queued:
   - Build a packet with 5 frames.
   - Send using `startTransmit()`.
   - Poll `TX_DONE` to allow next packet.

Why this design:
- Sampling is decoupled from radio transmission.
- Radio TX no longer blocks audio capture.
- Stable packet rate and predictable timing.

### TX Timing (ASCII)
```
20ms frames (Codec2) ----> group 5 frames ----> 1 RF packet (~100ms audio)

Mic/I2S  : [F1][F2][F3][F4][F5][F6][F7][F8][F9][F10]...
Codec2   :  C1  C2  C3  C4  C5  C6  C7  C8  C9  C10
TX queue :  C1  C2  C3  C4  C5 | C6  C7  C8  C9  C10 | ...
RF TX    :       [PKT1]              [PKT2]          ...
```

## RX Pipeline (Jitter Buffer + Adaptive Prebuffer)
1) Radio RX ISR/poll reads packet, validates header and self_id.
2) Frames are enqueued into RX jitter buffer (max 25 frames).
3) Playback runs on a fixed 20ms cadence:
   - Wait until prebuffer is reached before starting.
   - Pop one frame per 20ms, decode, play.
   - If under-run, repeat last frame (simple PLC).
   - If long silence, pause playback and output silence.

Adaptive prebuffer:
- Good RF: prebuffer = 10 frames (~200ms)
- Poor RF: prebuffer = 15 frames (~300ms)
- If underruns > 1 in last second -> go to 300ms
- If 3 consecutive seconds with no underrun -> go back to 200ms

Key parameters:
- `kJitterMinPrebufferFrames` = 10
- `kJitterMaxPrebufferFrames` = 15
- `kJitterMaxFrames` = 25
- `kTxQueueMaxFrames` (default 20, ~400ms)
- `kCodecFramesPerPacket` (5 frames, ~100ms)

### RX Timing (ASCII)
```
RF packets (100ms audio each)

RF in     :    [PKT1]   [PKT2]   [PKT3]   [PKT4] ...
Frame FIFO:   F1..F5 | F6..F10 | F11..F15 | ...
Playback :         [F1][F2][F3][F4][F5][F6][F7]...
Prebuffer:       ^ start after N frames (N=10..15)
```

If RF is unstable, underruns happen:
```
Frame FIFO:  F1..F5 | (gap) | F6..F10
Playback :  [F1][F2][F3][F4][F5][F5][F5]... (repeat last frame)
```

## How We Improve Audio Quality (Comprehensive)
This is the full, end-to-end strategy used to improve clarity and stability.

### 1) Stable Timing (TX + RX)
- TX uses a frame queue so encoding is not blocked by radio sends.
- RX uses a jitter buffer and plays at a fixed 20ms cadence.
- This eliminates bursty playback and “stutter”.

### 2) Jitter Buffer with Adaptive Depth
- Good RF: 200ms prebuffer (lower latency).
- Poor RF: 300ms prebuffer (more stability).
- Automatic switching based on underrun count.

### 3) Packetization Discipline
- Each RF packet contains exactly 5 Codec2 frames (100ms audio).
- Fixed packet sizing makes RX buffering predictable.

### 4) PLC (Packet Loss Concealment)
- If a frame is missing, replay the last decoded frame.
- This avoids hard dropouts and clicks.

Potential improvement (low cost):
- Short loss (<=2 frames): apply a linear decay on the repeated frame.
- Longer loss: fade to silence or comfort noise.

### 5) Gain Staging (TX + RX)
- TX mic gain (`kDefaultGainDb`) for intelligibility.
- TX PCM gain (`kTxPcmGain`) to avoid low-level audio.
- RX PCM gain (`kRxPcmGain`) to bring playback to audibly useful levels.

### 6) Half-Duplex Safety
- TX and RX are mutually exclusive to avoid RF self-interference.
- Clean transitions reduce glitching.

Optional upgrade:
- TX tail/hang time (100–200 ms) to send buffered frames before RX resumes.
- RX squelch to ignore very short noise bursts after switching to RX.

### 7) Practical Tuning Path
If audio is choppy → increase prebuffer or buffer depth.
If audio is weak → increase mic gain or RX PCM gain.
If audio distorts → decrease gain or enable hard limit.

## End-to-End Timing Diagram (ASCII)
```
PTT press
  |
  v
Mic capture (20ms) -> Codec2 encode -> TX queue -> RF TX (100ms pkt)
                                                       |
                                                       v
                                                RF RX -> Jitter buffer
                                                       |
                                                       v
                                          Decode (20ms cadence) -> DAC out
```

## Gains and Volume
These are tuned for clarity and stability:
- Mic gain: `kDefaultGainDb` (36 dB)
- TX PCM gain: `kTxPcmGain` (1.6x)
- RX PCM gain: `kRxPcmGain` (2.0x)
- Default volume: 80

Rotary adjusts volume by 1 step per tick during walkie mode.

## Half-Duplex Behavior
- TX and RX are mutually exclusive.
- While PTT pressed: radio in TX, RX halted.
- On release: radio returns to RX.

## Tuning Guidelines
If audio is choppy:
- Increase `kJitterMaxPrebufferFrames` to 20 (~400ms).
- Increase `kJitterMaxFrames` if you see frequent overflow.

If latency is too high:
- Reduce `kJitterMinPrebufferFrames` to 8 (~160ms),
  but expect more underruns in poor RF.

If audio is too quiet:
- Increase `kDefaultGainDb` (mic gain).
- Increase `kRxPcmGain` for playback.

If audio is distorted:
- Decrease `kTxPcmGain` or `kRxPcmGain`.
- Lower `kDefaultGainDb`.

## Known Limitations
- Not full duplex: TX and RX cannot happen at the same time.
- RF performance depends on CN470 rules and local conditions.
- Codec2 3200 bps prioritizes intelligibility over fidelity.

## Design Review Notes (Further Improvements)
These are structural improvements to reduce breakups and improve clarity.

### 1) Sequence and Session Robustness
Implemented in the current header:
- `session_id` (u16): new talkspurt identifier (PTT press).
- `seq` (u16): packet sequence.
- `frame0_index` (u16): 20ms frame index of the first frame in this packet.

Benefits:
- Drop late/old packets after wrap.
- Distinguish a new talkspurt from old packets.
- PLC knows exactly how many frames were lost.

### 2) Adaptive Prebuffer Hysteresis
Current adaptive logic is simple. To avoid oscillation:
- Track loss/underrun rate over a sliding window (e.g., 5s).
- Use hysteresis thresholds:
  - If loss > X% → deep buffer
  - If loss < Y% → shallow buffer (Y < X)

### 3) RX Bandwidth Tuning
RX BW is 156.2 kHz (robust but noisy).
Consider a configurable BW:
- Narrower BW (58/83 kHz) for cleaner RF → better SNR.
- Wider BW (156 kHz) if frequency error/clock drift is high.

### 4) Observability (Critical for Tuning)
Expose these metrics in logs or UI:
- RX jitter buffer occupancy (current/max)
- Underrun count (1s / 5s)
- Overflow/drop count
- RSSI/SNR (if available in FSK mode)
- Packet loss estimate (seq/frame index based)
