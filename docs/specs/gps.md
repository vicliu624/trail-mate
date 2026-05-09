# GPS Specification

## Purpose

GPS is a positioning subsystem, not a privacy feature and not a single GPIO
toggle. Platform code may use different drivers, pins, power rails, and receiver
commands, but all runtimes must expose the same product semantics.

## Concepts

`gps_enabled` is user intent. When false, the GPS runtime must not power or poll
the receiver and must not report live GPS observations. When true, the runtime is
allowed to power and poll the receiver according to platform capability and power
policy.

`gps_mode` is receiver behavior. It selects the desired GNSS operating profile,
such as high accuracy, power save, or fix only. It must never be interpreted as
the GPS enable flag. Mode value `0` is a valid enabled mode.

`gps_sat_mask` is receiver constellation selection. It affects which satellite
systems are requested from the receiver. It must never be used as the GPS enable
flag.

`gps_strategy` is runtime power policy. It controls when an enabled GPS runtime
keeps the receiver powered, for example always on, motion aware, or fix only. It
must never be used as the GPS enable flag.

`gps_powered` is hardware state. It reports whether the runtime currently has the
receiver powered.

`gps_ready` is driver or receiver readiness. A receiver can be enabled but not
ready yet, and it can be ready without having a fix.

`gps_transport_ready` is board-level transport readiness. On UART-backed boards
this means the receiver power path and UART stream are prepared. It does not
require a UBX identity response, NMEA sentence, satellite view, or fix. Board
code must not block the runtime just because a receiver probe timed out.

`has_fix` is observation state. It reports whether the latest GPS sample contains
a valid position fix.

`internal_nmea_stream` is the receiver-to-firmware data path used by the runtime
to parse fixes, satellites, time, and diagnostics. It must remain available while
GPS is enabled. It must not be disabled by privacy settings or external export
settings.

`external_nmea_output` is user-visible NMEA export. It controls whether parsed or
forwarded NMEA should be exposed outside the firmware. It is independent from
the internal stream and must not change the receiver sentences needed by the
runtime.

## Required Runtime Contract

Every platform runtime must implement these operations with the same meaning:

`is_enabled()` returns whether GPS is enabled by user intent and supported by the
runtime. It does not mean the receiver is powered, ready, or fixed.

`is_powered()` returns the current receiver power state.

`set_enabled(enabled)` changes user intent. Setting it to false powers down or
stops the receiver as soon as practical and clears live observations. Setting it
to true allows the runtime to start according to `gps_strategy`.

`set_collection_interval(interval_ms)` changes how often observations are
published or sampled by policy. It must not enable or disable GPS.

`set_power_strategy(strategy)` changes power policy for an already enabled GPS
runtime. It must not overwrite `gps_enabled`.

`set_gnss_config(mode, sat_mask)` changes receiver behavior. It must not enable
or disable GPS.

`set_external_nmea_config(output_hz, sentence_mask)` changes only external NMEA
export policy. It must not disable receiver output required for internal GPS
operation.

## Configuration Ownership

GPS configuration belongs under the GPS domain:

- `gps_enabled`
- `gps_interval_ms`
- `gps_mode`
- `gps_sat_mask`
- `gps_strategy`
- `gps_alt_ref`
- `gps_coord_format`
- `motion_config`
- `external_nmea_output_hz`
- `external_nmea_sentence_mask`

Privacy configuration is limited to privacy behavior. It must not own receiver
I/O or runtime control.

## Startup Rules

On startup, a runtime must load configuration and apply it in this order:

1. Apply `gps_enabled`.
2. Apply collection interval.
3. Apply power strategy.
4. Apply GNSS receiver configuration.
5. Apply external NMEA export configuration.

The internal receiver stream must be configured by the runtime itself whenever
the receiver is powered. A default internal stream must include the minimum
sentences required for location and satellite diagnostics.

Board startup must only prepare the physical transport and publish transport
readiness. Receiver identity probes are diagnostics, not a condition for starting
the runtime. Platform HAL code must not assume a fixed UART instance or GPIO set;
the board owns UART open and close operations.

## Non-Goals

This specification does not require platforms to share the same driver
implementation or GPIO definitions. Board-specific hardware stays board-specific.
Only the product semantics and public runtime contract are unified.

This specification does not preserve legacy configuration keys whose names
placed NMEA export under privacy or implied that `gps_mode` was an enable flag.
