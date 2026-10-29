# Linux HostLink IP publication confirmation

Operations 97 and 98 connect the production web client's signed IP publication
receipt and history restoration workflows to a native HostLink worker owner.
The implementation uses the shared `IpConfirmationWork`; its manifest, author,
receipt, reservation and head predicates remain common to the ESP implementation.
This feature requires neither an active RF protocol nor a board-specific device.

## Ownership and durability

`HostlinkTrackConfirmation` owns one bounded request and heap workspace. The
request binds the management session, current author generation, signing operation,
sealed receipt upload, directory public key and delivery identity, directory epoch,
network request, object and signed revision. History import additionally binds a
sealed manifest upload and its author signature. No private key crosses HostLink.

Only this session's sealed receipt or manifest upload with the exact identity,
hash and transfer binding can release its transient upload lease. Shared work
then authenticates the durable upload journals and content again under the
exclusive native storage lease. Unrelated uploads remain busy. Successful retries
can release a newly resealed matching proof so the production client can finish
explicit upload cleanup.

Confirmation of a locally signed revision requires its reservation. History import
can restore the same author's independently verified manifest and directory receipt
without a local signing reservation. Before creating a missing object head, native
admission rejects unknown files, directories, symlinks and unrelated evidence.
The signed manifest and receipt must persist before a committed head is created.
An imported history entry does not imply that its KML or attachments are installed.

Every step checks current storage and author generation. Completed retries also
validate the exact durable head under a newly acquired lease; contention cannot
turn a cached response into success. Disconnect, session close and a 30-second
idle limit release transient owners. A fresh session can recover durable evidence.

## Contract scope

Operation 84 remains a reserved enum value in the current formal implementation;
neither the production browser nor the ESP management dispatcher implements a
device history-list request at that number. Browser publication history and
device job lookup are existing separate contracts. Linux does not advertise 84.

## Verification

Ten native confirmation cases cover local confirmation, V1/V2 import, invalid
receipts, stale identity, unknown files, interrupted import, cached-head contention,
storage replacement and corrupted durable heads. Production browser code performs
real native TCP signing, receipt upload, confirmation/import, reconnect and cleanup.
An independent Python/OpenSSL TCP client also signs the directory receipts and
checks both workflows. These checks establish receipt and storage interoperability;
they do not establish connectivity to a live external directory server.

The focused native integration gate passed 42/42. The full native run passed
301/302, followed by a passing rerun of the test whose exact capability expectation
needed 97/98. The compact suite passed 19/19. All ten confirmation scenarios passed
on CM0. The independent TCP/OpenSSL checks passed against both the ARM fixture and
the exact installed main application, including reconnect and upload cleanup.
The standard deb passed Lintian, installation, `dpkg -V`, original-logo verification
and isolated startup/timed exit. Current artifact hashes are recorded in the
[CM0 acceptance audit](cm0-adaptation-acceptance.md).
