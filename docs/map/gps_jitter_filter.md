# GPS Jitter Filtering With 6-Axis IMU (Proposal)

## Goal
Reduce large GPS jumps in team track sampling by rejecting fixes that are
physically implausible, using 6-axis IMU data as a motion constraint.

This is intended for long-range LoRa where packets are sparse and we want to
avoid recording or transmitting obviously bad points.

## Feasibility Summary
Using a 6-axis IMU to estimate *absolute speed* is not reliable without
complex fusion (drift grows quickly). However, the IMU is very useful to:
- detect **stationary vs moving** states, and
- provide an **upper bound on acceleration** to limit plausible speed changes.

So the IMU should be used as a **gating / plausibility filter**, not as the
primary speed source.

## Implementation Note (v1)
The current implementation uses the IMU **any-motion** event as a
stationary/moving signal (based on time since last motion). It does **not**
yet compute accel/gyro RMS windows. This keeps the filter lightweight while
still leveraging the IMU as a physical plausibility gate.

## Data Inputs
- GPS fixes: `lat, lon, timestamp`, optional `speed`/`hdop`.
- IMU samples: accelerometer + gyroscope at higher rate.

## Core Idea
On each GPS fix, compute the implied speed between the new fix and the last
accepted fix. Reject the new fix if it violates IMU-derived motion bounds.

### Step 1: IMU Preprocessing (lightweight)
Compute motion metrics over a short window (e.g., last 2–5 seconds):

1. **Gravity removal**
   - Low-pass accel to estimate gravity `g`.
   - Linear accel = `a_lin = a_raw - g`.

2. **Motion energy**
   - `a_rms` = RMS of `|a_lin|` over window.
   - `gyro_rms` = RMS of `|gyro|` over window.

3. **State estimate**
   - `is_stationary = (a_rms < A_STILL) && (gyro_rms < G_STILL)`

### Step 2: GPS Plausibility Check
Given last accepted fix `(p0, t0)` and new fix `(p1, t1)`:

```
dt = t1 - t0
d  = distance(p0, p1)
v_gps = d / dt
```

Define a conservative speed bound `v_max` using IMU:

- If stationary:
  - `v_max = V_STILL_MAX` (e.g., 0.5–1.0 m/s)
- Else moving:
  - Use last accepted speed `v_prev` and accel bound:
    `v_max = v_prev + a_max * dt + V_MARGIN`

Where:
- `a_max` can be derived from IMU peak linear accel (clamped) or a fixed safe
  value for your use case (e.g., 3–5 m/s^2 for walking, 8–12 for fast motion).

Decision:
- If `v_gps > v_max` -> **reject** (likely GPS jump).
- Else -> **accept** and update `v_prev = min(v_gps, v_max)`.

### Step 3: Failsafe / Recovery
To avoid rejecting everything in poor conditions:
- If N consecutive rejections occur, accept the next fix but mark as "low trust".
- Or gradually increase `V_MARGIN` after repeated rejections.

## Parameters (suggested starting points)
These are conservative; tune with field logs:
- `A_STILL` = 0.05–0.08 g (linear accel RMS)
- `G_STILL` = 2–4 deg/s (gyro RMS)
- `V_STILL_MAX` = 1.0 m/s
- `a_max` = 4.0 m/s^2 (walking/hiking), 8.0 if fast movement
- `V_MARGIN` = 0.5–1.0 m/s
- `N` (rejects before accept) = 3

## Why This Works
Large GPS jumps require **large implied speed**. If the IMU shows low motion,
those speeds are physically impossible, so we can safely reject them.
Even if IMU is noisy, the bounds are conservative and mostly act on
extreme outliers, which is exactly what we want.

## Limitations
- IMU integration is not used; thus no absolute velocity, only bounds.
- If the IMU is poorly calibrated or mounted, thresholds must be adjusted.
- If GPS fixes are very sparse, `dt` is large; keep bounds conservative.

## Minimal Implementation Checklist
1. Add a small IMU motion state estimator (RMS + still/moving).
2. Add a `last_accepted_fix` and `v_prev`.
3. On each GPS fix, compute `v_gps` and gate with the rules above.
4. Log accept/reject decisions with metrics for tuning.

## Suggested Logs
```
[TRACK] gps dt=120s d=180m v=1.5m/s
[TRACK] imu a_rms=0.03g gyro=1.2dps state=STILL v_max=1.0
[TRACK] reject: v_gps>v_max
```
