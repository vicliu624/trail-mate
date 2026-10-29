# Native Linux MeshCore direct routing

Private text and application-data sends start with flood routing. An encrypted
PATH acknowledgement can advertise the outbound path for subsequent sends. The
Linux adapter learns it only after authentication with the known peer public key
and successful matching against a pending message acknowledgement. The outer
packet path and legacy short acknowledgements never populate the route cache.

The radio worker owns a fixed 32-entry cache, keyed by the full peer public key
and payload profile. Entries expire after ten minutes; capacity eviction removes
the oldest entry. Zero-hop direct paths are valid. An acknowledgement timeout
invalidates that peer's routes so a subsequent send falls back to flood. Learning
does not change the encrypted payload or its acknowledgement signature.
Protocol changes, radio-parameter changes, and cancellation/suspension also clear
the cache. The adapter integration test learns a route through an encrypted
acknowledgement, checks the next application's direct frame, changes modulation,
and checks that the next send returns to flood routing.

Tests cover V1/V2 encrypted PATH extraction, independent outer and authenticated
paths, direct-frame decoding, unchanged message acknowledgements, zero-hop paths,
invalid inputs, profile and peer isolation, timer wraparound, expiration,
invalidation, and bounded eviction. Peer-to-peer RF validation is deferred at the
user's request; these tests establish the software behavior, not an on-air result.

## Package verification (2026-10-21)

The native suite passed 109/109 tests. The armhf application was rebuilt with
the target Raspbian C++ ABI overlay, packaged, and installed on the CM0. Package
integrity verification passed and the P4 interface rendered at 1232 by 568.
The packaged logo matches `images/logo.png`. The deb SHA-256 is
`6a2dd0ab51a310b144b00200a66a8cec5790203515774cef7fa98094ebc334d9`.
The first fullscreen-exit check encountered a compositor-control timeout; its
validation process subsequently exited. After confirming no application remained
and desktop control responded, the repeat check confirmed the focused fullscreen
window exited with status zero in 1.189 seconds after Alt+F4. The RTC service
remained active throughout package validation.

The lifecycle follow-up first reproduced stale direct routing after changing
MeshCore SF, then passed all six native MeshCore tests after clearing the cache
at radio/protocol changes and cancellation. The updated deb SHA-256 is
`9dd4666db14fd7dd7391ce7a5501e378861fbfb619a9744c953f7495c0a5ecf8`.
It was installed and verified on the CM0; the P4 interface rendered at 1232 by
568, Alt+F4 exited with status zero in 0.856 seconds, and RTC availability,
validity and kernel system-clock-source checks remained successful. No additional
reboot was performed for this follow-up.
