# Linux SDL audio subsystem lifetime

The CM0 application once aborted during startup in PulseAudio's `pa_mutex_free`
mutex-destruction assertion. Ordinary repeated launches and debugger launches
initially passed, so a passing launch alone was not an adequate reproducer.

The notification worker retains PCM input/output owners. Music and voice workers
also construct output owners and transient input capability probes. Previously,
each owner called `SDL_InitSubSystem(SDL_INIT_AUDIO)` and
`SDL_QuitSubSystem(SDL_INIT_AUDIO)` independently, without synchronization across
the workers. A three-thread lifecycle harness crashed on CM0. A variant that
serialized only initialization/release completed 100 rounds. The same pressure
test using the actual `SdlPcmInput` and `SdlPcmOutput` classes failed with exit 139
on its first run against the original implementation and the real PulseAudio
backend (SDL 2.32.4, libpulse 17.0).

SDL documents [reference-counted subsystem initialization](https://wiki.libsdl.org/SDL2/SDL_Init).
The Linux adapter preserves one successful SDL reference per PCM owner, but all
owners now acquire and release that reference through one shared lifecycle
mutex in the internal `sdl_audio_lifetime.h` helper. It does not introduce another
audio service or change media focus ownership. A PCM owner closes its device
before releasing its subsystem reference. The gate does not cover sample reads,
playback writes or SDL's capture callback. It contains no board name, hardware
path, mixer setting or protocol policy.

Two regressions exercise the actual PCM classes with three threads and 100
rounds of construction, enumeration and destruction:

- `linux_audio_lifetime` runs the real selected SDL backend. CTest selects dummy
  audio for portable host verification; the assertion-enabled armhf executable
  also runs against the real CM0 PulseAudio server.
- `linux_audio_lifetime_gate` interposes the SDL lifecycle/enumeration seam to
  expose overlapping lifecycle calls deterministically. Its maximum-entry
  assertion failed on the old implementation and passed with the shared gate.
  This checks cross-owner synchronization even when dummy audio tolerates the
  otherwise unsafe interleaving.

After the change both host regressions passed, the full rebuilt native suite
passed 96/96, and the real CM0 owner stress executable completed three runs of
100 rounds with three workers. These probes enumerate devices without recording
or playing audio. The standard armhf deb was rebuilt and passed extracted-app
bootstrap, store creation, rendering and package resource checks. The replacement
was installed on CM0 and passed package verification. Its full application then
completed ten ordinary startup/exit cycles. A compositor-confirmed active,
fullscreen window also closed through Alt+F4 with exit code 0 in 0.939 seconds.
The installed binary hash matched the extracted package binary.

Debian artifact SHA-256:
`8f3115d7a0f007e8543e95dc389ad511c97bcc720832e3e2ff4406425ae8b176`.

This fixes the demonstrated concurrent subsystem-lifecycle defect. Microphone
gain, acoustic response, speaker wiring and radio/audio interoperability are
different requirements; successful lifecycle tests do not establish them.
