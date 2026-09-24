# Browser Geocaching

This page is a live Reticulum/LXMF client. The protocol stack, visitor identity,
author signature verification, application requests and GPX generation run in a
dedicated Web Worker. Pages serves static program assets; it does not hold a
periodically refreshed cache database or translate HTTP queries into LXMF.

## Build and run locally

The UI uses the pinned Animal Island UI React components (`Button`, `Card`,
`Title`, `Background`, `Divider`, and `Icon`) and their shipped styles/fonts.
The page shell is mounted once; the existing map controller updates its live
regions and button states without rerendering the Leaflet map.

```sh
cd site
npm ci --ignore-scripts
npm run build:geocaching
cd ..
python -m http.server 8080 --directory site --bind 127.0.0.1
```

Open `/geocaching/`. Assets and the worker also work below the GitHub Pages
`/trail-mate/geocaching/` prefix. The Pages workflow builds the browser bundles
after installing the pinned packages; no firmware rebuild is needed locally.

The page automatically connects using the operator-owned `network.json` file.
Visitors have no endpoint input or connection settings. Set its `endpoint` to a
verified raw-packet WSS access point before deployment. A null value
shows a service-unavailable message without asking visitors to configure anything.
The current deployment uses a temporary Cloudflare Quick Tunnel for public
acceptance. It depends on the operator computer remaining online and is not a
permanent production endpoint. `discoverySeeds` contains up to three discovery
destination hashes; clients request signed announcements from these hints at
connection time and still verify identity binding and directory capabilities.
For localhost testing, `ws://127.0.0.1:8787` is accepted.
The current interface uses an independent temporary identity per tab; it does
not promise background reception, persistent identities or offline saved-cache
recovery yet. Private keys never leave the worker.

## Packet access point

Run a Reticulum TCP server interface on a trusted loopback port, then:

```sh
python -m pip install -r tools/geocaching/bridge-requirements.txt
python tools/geocaching/packet_bridge.py --tcp-port 44242 --port 8787 --origin http://127.0.0.1:8080
```

Each WebSocket binary message is one raw RNS packet (20..500 bytes), converted
to/from HDLC framing on its own TCP connection. The bridge imports no LXMF,
directory, identity or application-protocol code. Text messages, oversized
packets and unrelated browser origins are rejected. Connections, queues and
browser packet rates are bounded. Do not point it at an unrelated TCP service.

For HTTPS Pages, put TLS on the bridge with `--tls-cert` / `--tls-key`, or use a
TLS reverse proxy forwarding to its loopback listener. Allow the exact Pages
origin (for example `https://vicliu624.github.io`), not the project URL path.
The proxy must preserve binary WebSocket messages and the Origin header. The
bridge's unencrypted listener refuses non-loopback binds. The directory and
bridge are separate roles even when hosted together.

## Behaviour and limits

- Directory announces must be signed and bind discovery/delivery to one RNS
  identity. Public role and read capabilities are checked before querying.
- At most 32 candidates, 3 sources per visible-region query, one active request,
  20 summaries per page and 500 displayed IDs. A moved map doesn't send requests;
  the user searches the new area explicitly. Date-line views split into two
  bounding boxes; stale query generations cannot replace the latest results.
- Summaries remain directory claims until exact-hash get, outer LXMF signature,
  original author signature and all summary fields have been checked.
- Compressed RNS Resources are accepted, bounded to 16 KiB of LXMF framing and
  8192 bytes of application payload. The bzip2 adapter bounds decoded output
  before writing. No server-side decompression or GPX rendering is substituted.
- Downloading 1–20 selected caches verifies each exact object. Partial failure
  requires an explicit choice to save successful items. GPX retains original
  signed bytes, Groundspeak 1.0.1 fields and normal GPX 1.1 waypoint content.
- The map uses Leaflet and visible-viewport OpenStreetMap tiles, with attribution
  and ordinary browser HTTP caching. Tile failure and Reticulum failure are
  separate. No automated tile prefetch or offline tile export is provided.

## Verification

```sh
node --test tests/site/geocaching-protocol.test.mjs
python -m unittest discover -s tools/geocaching -p test_packet_bridge.py -v
node tests/site/geocaching-browser.mjs --work .codex-build/browser-run --python /path/to/venv/python
node tests/site/geocaching-browser.mjs --multi-upstream --reconnect --work .codex-build/browser-recovery-run --python /path/to/venv/python
node tests/site/geocaching-browser.mjs --late-bridge --reconnect --work .codex-build/browser-startup-run --python /path/to/venv/python
```

Install the pinned Playwright browser for the final command, or pass
`--channel msedge` on a machine with Edge. Use a new work directory each run.
The browser test starts bounded, loopback-only directory and packet-bridge
processes. It loads the actual bundled page/worker, retrieves a signed record
large enough to require Resource transport, downloads GPX, checks XML and text
escaping, and captures desktop/mobile screenshots. Public map tiles are mocked
in automated tests to avoid using community tile servers for bot traffic.

`--multi-upstream` inserts a native Python Reticulum gateway with two TCP
uplinks. Two independently cuttable loopback TCP proxies reach the same test
directory; the test closes the proxy carrying the gateway's current directory
path and refuses its reconnections. It does not rewrite the route table or
restart the browser/directory. The test requires a new signed detail and GPX
after the cut, and checks that RNS selected the surviving uplink. `--reconnect`
then restarts only the WSS packet bridge and requires the existing browser tab
to revalidate its directory and query again without a reload or another
scheduled directory announcement. This is controlled interface-loss coverage,
not evidence of geographic/public-backbone redundancy or wireless performance.

`--late-bridge` first opens the page against a closed port and requires the
service-unavailable state with no RNS packets exchanged. It then starts the
bridge on that same port, without reloading the page. The directory fixture's
startup announcement is delayed to allow the browser's automatic reconnect.
This covers a failed initial dial, separately from losing an established
connection. The RNS interface is attached before dialing, so later reconnects
can use the same protocol stack and identity even if the initial dial rejected.
Automatic socket reconnection currently stops after five failed attempts.

Verified 2026-09-24: `.codex-build/geocaching-startup-recovery-20260924-a/`
passed initial-dial recovery, exact author verification, 20,422-byte GPX export,
and a subsequent bridge restart without a page reload. The earlier multi-uplink
case is recorded in `.codex-build/geocaching-multi-upstream-20260924-h/`; its
controlled recovery took 39,061 ms. These logs are local acceptance artifacts,
not a declaration that the public endpoint is deployed.

Browser requests retain one application request ID and a 45-second deadline,
with at most three sends. After a stalled response or failed handshake the
client discards the cached LXMF delivery link and requests a fresh path. A
transit node can briefly reply from a stale path cache, so one failed recovery
handshake may be followed by another bounded attempt. An unanswered request
still ends in an error; receipt of a path announcement alone is not success.
Reconnected WSS sessions actively recheck known directory capabilities rather
than waiting for the next six-hour discovery announcement. Reused links receive
only one pair of application listeners.

For a diagnostic bundle only, set `GEOCACHING_LOG_LEVEL=DEBUG` during the build;
normal builds use `ERROR`. `GEOCACHING_NETWORK_DEBUG=1` enables Python logs in
the local browser test fixtures. Diagnostic logs are not needed by visitors.

Remaining delivery work includes persistent identity/cache UX, broader source
failure and reconnection acceptance, resource/concurrency stress testing,
operator deployment with valid WSS certificates, and physical-device acceptance.
Successful local tests alone do not establish public service availability.

## Default network research — 2026-09-24

The live [Reticulum Interface Directory](https://directory.rns.recipes/) returned
96 online submitted entries and 446 discovered entries during this check.
Neither response contained a WebSocket/WSS interface. Three advertised TCP
listeners accepted connections from the development machine:

| Candidate | TCP port | Observed handshake time |
| --- | --- | --- |
| ZHULONG1 Hong Kong, `103.195.4.226` | 4242 | 32 ms |
| Sydney RNS, `sydney.reticulum.au` | 4242 | 279 ms |
| RMAP, `rmap.world` | 4242 | 458 ms |

These are upstream candidates, not browser WSS endpoints. Reachability is a
point-in-time TCP check, not proof of availability, trusted operation, or a
compatible Trail Mate Geocaching directory. A production default still needs
our TLS packet bridge, a hostname/certificate, and a reachable Geocaching
directory service. Do not invent a WSS URL by changing a TCP address's scheme.
Keep this operator setup out of the visitor interface.
