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

Storage starts when the user opens Downloaded or Published, or saves a cache.
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
