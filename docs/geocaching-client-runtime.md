# Geocaching client runtime

Device discovery and browsing use a disposable RAM session. Opening Discover,
checking directory capabilities, querying, refreshing and paging do not open SD
storage or recover previous queries. A response is displayed after the existing
transport authentication and query validation; no journal commit is required.

`LiveQueryPort` owns one request, one page and two response fingerprints. Its
object size is capped at 3 KiB, excluding the query client and transport stack.
An accepted transport submission retries the same request after 15 seconds;
a rejected submission retries after one second. The query client's existing
120-second deadline cancels further sends. Closing abandons the RAM request.
Exact accepted duplicates can be acknowledged without disk access.

Opening an item requests its exact signed revision and shows the full description
and hint after author verification. Only the open detail owns these text buffers;
closing it cancels further requests. Online detail reads do not require SD.
Saved items read the complete signed record already retained by the download
transaction, so descriptions and hints remain available offline after restart.
The GPX also includes both fields and the signed record; no second sidecar file
or background detail synchronization is introduced.

Storage starts when the user opens Downloaded, Published or the standalone Map,
or saves a cache. The standalone Map also displays local drafts with a selected
position, regardless of publication status, alongside downloaded caches. It
requests four metadata rows per asynchronous window, retains at most 32 markers
nearest the map center, and stops reading once that pass completes. Panning or
zooming selects a new nearby set. The shared map overlay capacity also includes
other layers, so dense views can show fewer than 32 geocaches. Drafts without a
selected position are excluded; an explicitly selected (0, 0) is valid. No GPX
parse or network discovery is performed by the map projection.
Downloaded rows may be displayed while unfinished downloads are being recovered;
creation and writes still wait for that recovery to finish. Draft pages read four
rows and one lookahead, expose committed names and positions before publication
history is resolved, and skip publication history entirely for unbound drafts.
Publication preparation still requires the complete history projection. Startup
validates request/task/attempt tables in the reference pass rather than scanning
the same tables a second time in the preceding integrity pass.
The draft editor uses the shared map location-selection route instead of latitude
and longitude text inputs. Confirming writes WGS84 E7 coordinates; cancellation
preserves the original position and all live editor fields. Existing coordinates
seed the map viewport, and the selected position is displayed read-only.
Reticulum starts when Discover is requested or publication readiness is checked.
Opening saved lists, saved details or the local draft list does not create a
network backend. Once started, the backend remains available to that session
and its pending network operations; closing the session releases its ownership.
Downloads and publications retain signature checks, durable transaction state,
verified GPX installation, restart recovery and durable reply receipts. Storage
failure or USB ownership does not prevent Discover from receiving a page.
Checkpoint rotation shares storage ownership with persistent operations but
does not delay network queries or pagination.

Installed downloads have a separate durable completion marker for file cleanup.
The optional seventh GpxInstall field is integer `1`, accepted only for the
Installed phase. Older six-field records remain eligible for one recovery pass.
The marker is committed only after target verification and successful history
retention; interrupted cleanup remains recoverable. Recovery also fills missing
list metadata from the retained signed record. Subsequent startup skips these
completed installations without reopening their GPX files. Downloaded pages use
the current head/object/install indexes, with four rows and one lookahead, rather
than scanning request history or hashing GPX files.

Legacy kind-3 browse tasks remain readable on existing cards. The indexed
dispatcher skips them without a startup scan that rewrites each task. Older
query storage adapters are retained for storage-format compatibility tests;
the device runtime no longer instantiates them.

Host verification uses the production runtime with simulated transport, SD and
clock: absent SD, USB ownership, busy sends, bounded retries, exact duplicate
checks, pagination during maintenance, saved downloads, publication updates,
index corruption, interrupted recovery and checkpoint reclamation. These checks
do not establish ESP peak heap usage or real network latency.
