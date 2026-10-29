# Shared LXMF resource framing

Resource preparation now lives in `core_chat` beside the existing canonical
resource state/window helpers. The ESP adapter calls `prepareOutgoingResource`
instead of owning a second encryption/hash-map implementation. Its output
descriptor is constructed directly in the session's resource list, avoiding an
automatic resource object on the ESP task stack. Advertisement transmission and
receipt registration remain with the adapter.

The shared preparation preserves the existing wire representation: a four-byte
random data prefix, an authenticated encrypted token, MDU-sized parts, four-byte
part hashes, a random resource hash, and a payload/resource-hash completion
proof. Randomness is injected by the platform and failure is reported without a
usable output descriptor. Collision retries and hashmap segment capacity use the
same policy as the embedded implementation. Protocol allocation guards remain
the existing three-megabyte advertisement and 8192-part ceilings.

`reticulum::fullHashJoined` hashes both spans incrementally with Mbed TLS or
OpenSSL. Resource preparation and the ESP receive/proof helpers use that single
implementation, so hashing does not allocate another resource-sized copy.
Parsing-only crypto configurations fail closed.

`decodeCompletedResource` validates the assembled encrypted token, exact data
size and full resource hash before returning bytes and a completion proof.
It accepts encrypted, unsplit data without metadata. Optional platform callbacks
compress before encryption and expand after authentication, with an exact
advertised output size. Hashes and completion proofs bind the original data.
Linux supplies the repository's vendored bzip2 implementation; embedded callers
retain their existing uncompressed preparation unless they supply a callback.
Decoding does not send a proof or imply application acceptance.

The regression prepares 40 KB across a hashmap extension, records parts in
reverse order through the real resource-state helpers, decrypts/reassembles the
data, and checks resource/proof digests against independently calculated Python
hashlib values. Corrupt encrypted parts, wrong keys, wrong resource hashes,
invalid MDU and RNG failure are rejected. Host code runs under ASan/UBSan.

The native loop now connects advertisement, request, hashmap extension, part
scheduling and completion proofs. Short messages can progress while a resource
is in flight. Incoming resource confirmation waits for durable application
commit; a failed commit retains the encrypted transfer for a bounded retry.
Compressed resources use the same commit boundary. Split resources, metadata,
propagation and application attachments remain separate unfinished work.

The earlier shared-codec baseline passed 94/94 native tests and 14/14 compact tests. The resource
roundtrip runs under ASan/UBSan, and its assertion-enabled armhf executable
passed under QEMU alongside the existing peer-text sender/worker regressions.
Portable token/runtime-state tests passed 2/2. Twenty-two P4 translation units,
including the adapter and both shared wire implementations, passed installed
cross-compiler syntax checks. The rebuilt Debian package passed extracted-app
bootstrap/rendering and Logo/README comparison. No complete P4 firmware link,
upload or physical resource-transfer result is implied.

The subsequent Linux resource/compression change passed the assertion-enabled
`linux_resource_wire` and `linux_reticulum_send` host tests. The former compares
the decrypted compressed stream with a Python `bz2` fixture and rejects both
undersized and oversized declared expansion lengths. The latter sends 60 KB of
pseudorandom printable text while another short message progresses, then
receives a compressed reply through a real SQLite write-failure/recovery gate.
The full rebuilt native suite subsequently passed 94/94 tests. Assertion-enabled
armhf resource-codec, message-sender and mesh-worker executables passed under
QEMU. Twenty-two relevant P4 translation units passed installed cross-compiler
syntax checks. The replacement `trailmate-cm0_0.1.38~alpha_armhf.deb` passed
extracted ARM application bootstrap, persistent identity/Agenda/waypoint-store
creation, 1232 x 568 rendering, and Logo/document comparisons. Its bzip2 license
matches the vendored source. Artifact SHA-256:
`b3e9699d1075bb8d9ec1212afe3341a853500910c90cf1ed4f8d78eba2301731`
(subsequently rebuilt with the confirmed baseline without GNSS/IMU expansions
and the Linux SDL audio-lifecycle/routing corrections). The compact-target rebuild and
14/14 compact tests also passed. These software results do not establish an
on-air peer result.
