# Reticulum Mode User Guide

This document describes the user-facing behavior of Trail Mate when the active
protocol is `Reticulum`. It focuses on the parts that a field user needs to
configure, especially shared group destinations stored on the SD card.

## Current Product Model

Trail Mate treats `Reticulum` as a product protocol at the same level as
`Meshtastic` and `MeshCore`.

Reticulum mode can use more than one carrier interface at the same time:

- LoRa through the device's RNode-compatible raw LoRa carrier.
- Wi-Fi through a Reticulum TCP gateway using the upstream TCPInterface HDLC
  framing on port `4242` by default.

The Wi-Fi carrier is not MQTT, HTTP, or a separate Trail Mate protocol. It is
just another Reticulum interface under the same Reticulum runtime. Contacts,
Nearby, Groups, announces, path requests, proofs, links, and LXMF packets all
flow through the same Reticulum product layer, then fan out through the enabled
carrier interfaces.

By default, LoRa is enabled and the Wi-Fi gateway is disabled. To use Wi-Fi as
an additional Reticulum carrier, configure normal Wi-Fi credentials first, then
open Settings > Network while the active protocol is `Reticulum` and set:

- `Reticulum LoRa`: keep enabled when LoRa should remain active.
- `Wi-Fi Gateway`: enable the TCP gateway carrier.
- `Gateway Host`: the hostname or IP address of a Reticulum TCP server or
  backbone node.
- `TCP Entry`: choose one of the three TCP slots before editing its host or
  port. Fresh configurations use `sydney.reticulum.au`, `node.reticulumnet.nl`
  and `rmap.world`, all on port 4242. Existing custom configurations are kept.
  Clear a host to remove an entry; later entries shift forward. Changes are
  saved through the device's TMS configuration flow. See
  [network configuration](reticulum_network_config.md) for selection evidence
  and board-specific limits.
- `Gateway Port`: usually `4242`.
- `Auto Wi-Fi`: when enabled, Reticulum will ask the Wi-Fi runtime to connect
  before opening the gateway socket.

Trail Mate prevents disabling both Reticulum LoRa and Reticulum Wi-Fi gateway
from the device UI, because that would leave Reticulum selected with no carrier
interface.

## Wi-Fi Gateway Operating Model

Trail Mate uses the Reticulum Wi-Fi gateway as a low-frequency terminal ingest
path. It is not trying to behave like a desktop Reticulum node, a public router,
or a propagation server.

In practical terms, the device prioritizes:

- direct and group LXMF chat
- proofs and link/resource traffic needed for local conversations
- Trail Mate appdata such as team position and realtime track messages
- enough announce/path discovery to keep Nearby, Network, and the SD-backed
  address book useful

Public Wi-Fi Reticulum traffic can be much noisier than a handheld device should
process at full rate. Trail Mate therefore samples public discovery, summarizes
background RX logs, keeps the Network view bounded to the newest announce
entries, and avoids turning unrelated Wi-Fi path/announce traffic into LoRa
transmissions or high-frequency SD writes.

While the screen is awake, Reticulum Wi-Fi background work is intentionally
minimal. The device keeps direct/group chat, active local link traffic,
path responses for messages you send, and local Trail Mate business traffic
working; public announce discovery and non-urgent storage are deferred until the
screen is sleeping.

This is expected behavior. Seeing only a partial or slightly stale public
announce view is preferable to making chat, the keypad UI, LoRa, or SD storage
unreliable.

## Reticulum Calls

On Pager-class hardware with a microphone, speaker, ES8311 audio codec, and
Wi-Fi gateway connectivity, Reticulum mode supports Sideband-compatible LXST
telephony links on `lxst.telephony`.

Calls are Wi-Fi-gateway only. Trail Mate does not place call audio on LoRa,
because the LoRa channel is too slow and too precious for realtime voice. The
Contacts action menu shows `Call` for Reticulum peers when the peer has a known
LXMF/Reticulum identity and the active adapter reports call support.

During a call, Trail Mate enters Reticulum call realtime mode:

- LXST call audio, LinkIdentify, signalling, and hangup packets keep running through the
  Reticulum Wi-Fi gateway
- Codec2 and the ES8311 audio path run at the call's realtime cadence
- MQTT, GPS collection, HTTP/downloads, OTA,
  pack/language/image fetches, public discovery, periodic announces, LoRa
  polling, and non-urgent SD writes are paused or deferred

Incoming calls wake the device into the Call Page even if the screen was
sleeping. The page lets you answer, decline, adjust speaker volume, or hang up.
The interruption navigator restores the previous page after call cleanup.

## Reticulum Identity

When the active protocol is `Reticulum`, Settings > Network shows the local
Reticulum identity facts that are useful when exchanging addresses with another
person:

- `Identity Hash`: the local Reticulum identity hash.
- `LXMF Address`: the local LXMF delivery destination hash. Share this when
  someone needs to address this Trail Mate directly over LXMF.

The 16-byte Reticulum hashes are shown as 32 hexadecimal characters. On small
screens Trail Mate wraps them after 16 characters; the line break is only a
display wrap, not part of the address.

When the active protocol is `Reticulum`, the main menu also shows a `Network`
app with the Nomad-style network icon. That app is the Reticulum network view,
not the source of truth for local identity addresses.

Current boundary: the `Network` app renders Reticulum status, the latest
SD-backed announce directory entries, and a small Nomad/Micron page browser.
It shows at most the newest 100 announces so the UI stays responsive on the
device. Use the search shortcut (`/` or `s`) or the search button to filter by
announce display name, destination hash, identity hash, or announce aspect.

The browser address bar accepts a Reticulum destination hash followed by a
Nomad page path such as `<destination>:/page/index.mu`. Hyperlinks in cached
Micron pages are navigable, and relative links such as `/page/about.mu` resolve
against the current destination. Page bodies are loaded from the SD cache under
`/trailmate/reticulum/pages/<destination>/...`; if a page is not cached yet,
the browser first shows an explicit cache-miss/loading state instead of falling
back to the node summary. On ESP Reticulum builds, a cache miss can start a
bounded, best-effort Nomad page request through the Reticulum runtime; the page
is rendered only after the response is saved back into the same SD cache. If
the runtime is not ready, the request times out, or the build has no request
handler, the browser keeps the failure state visible.

Trail Mate intentionally supports a bounded static Micron subset here, not a
complete NomadNet desktop browser. The table below compares the upstream
NomadNet Micron surface with Trail Mate's device browser boundary.

| Upstream Micron area | Upstream syntax / behavior | Trail Mate today | Boundary decision |
| --- | --- | --- | --- |
| Page color headers | `#!fg=<color>` and `#!bg=<color>` before the first non-header line | Supported | Keep |
| Color values | `RGB`, `RRGGBB`, and grayscale-style values used by the parser | Supported as `RGB`, `RRGGBB`, and `gNN` | Keep |
| Comments | Lines beginning with `#` | Supported after page color headers are parsed | Keep |
| Blank lines | Empty line | Supported as a small spacer | Keep |
| Literal blocks | A line containing only `` `= `` toggles literal mode; <code>\`=</code> inside a literal emits `` `= `` | Supported | Keep |
| Section headings | Leading `>`, `>>`, `>>>`, and deeper levels set section depth and render heading rows; empty headings can create indented blocks | Supported up to eight levels | Keep |
| Section reset | `<` at the start of a line resets section depth | Supported | Keep |
| Dividers | `-` renders a divider; `-x` uses `x` as the divider glyph | Supported for `-` and `-x` | Keep |
| Tables | `` `t ``, optionally with alignment `l`/`c`/`r` and max width, buffers following Markdown-style rows and renders a formatted table | Partially supported: toggles table mode, parses opening-tag alignment/max-width intent, splits Markdown-style rows into up to six cells, treats the first data row as a visual header, and honors separator-row left/center/right alignment; no desktop-grade table layout | Keep the bounded cell renderer; do not add full table layout unless a real device use case appears |
| Bold | `` `! `` toggles bold | Supported | Keep |
| Underline | `` `_ `` toggles underline | Supported | Keep |
| Italic | `` `* `` toggles italics | Parsed, but no separate italic font rendering | Keep as muted visual support |
| Foreground color | `` `Fabc `` and `` `FTaabbcc `` | Supported | Keep |
| Background color | `` `Babc `` and `` `BTaabbcc `` | Supported | Keep |
| Color reset | `` `f `` and `` `b `` reset foreground/background | Supported | Keep |
| Alignment | `` `l ``, `` `c ``, `` `r ``, and `` `a `` set left, center, right, or default alignment | Supported | Keep |
| Inline reset | Two consecutive backticks reset formatting, colors, and alignment | Supported | Keep |
| Escaping | `\x` emits `x` literally inside inline content | Supported | Keep |
| Links | <code>`[target]</code> and <code>`[label`target]</code> | Supported until the page link budget is exhausted, including nested Micron links inside rendered rows; exhausted pages show `link limit reached` | Keep the 32-link budget and explicit degradation |
| Link request fields | <code>`[label`target`fields]</code>, where `fields` can include `*`, field names, or `key=value` request variables | Target navigation works; `anchor=...` is treated as a display/navigation hint; submit-field links are detected and marked `[no submit]` because fields are not sent | Do not submit fields on this device browser |
| Relative links | Paths such as `/page/name.mu` and same-destination targets such as `:/page/name.mu` | Supported against the current destination hash | Keep |
| Link schemes | `nomadnetwork://...` and `lxmf://...` | Scheme is stripped before navigation | Keep |
| Page resource links | `/file/...`, attachment/resource paths, and non-page remote paths | Rendered as static text with `[resource unsupported]` and counted in the page notice area | Keep page-only navigation |
| Anchors | `` `:name `` declares an anchor; headings get automatic anchors; `#name`, `#`, and external `anchor=...` links can jump within pages | Partially supported: explicit anchors and heading auto-anchors are registered locally; `#name` jumps within the current page; bare `#` jumps to the next heading; `anchor=...` appends `#anchor` to the opened page and scrolls after render | Keep local/static anchor navigation; do not add node-side anchor request semantics |
| Text fields | <code>`&lt;name`data&gt;</code> and <code>`&lt;width&#124;name`data&gt;</code> | Rendered as non-focusable local LVGL text areas; pages with fields show `fields are display-only` once | Keep rendered-only |
| Masked fields | <code>`&lt;!&#124;name`data&gt;</code> or <code>`&lt;!width&#124;name`data&gt;</code> | Rendered as non-focusable local password-style fields | Keep rendered-only |
| Checkboxes | <code>`&lt;?&#124;name&#124;value`label&gt;</code>, optional trailing `&#124;*` for prechecked | Rendered as non-focusable local checkboxes | Keep rendered-only |
| Radio groups | <code>`&lt;^&#124;name&#124;value`label&gt;</code>, optional trailing `&#124;*` for prechecked | Rendered as non-focusable local radio controls | Keep rendered-only |
| Form submission | Link request fields submit local field values to node-side pages | Not supported | Do not implement on this device browser |
| Partials | A line starting with `` `{ `` embeds a partial: `url`, optional refresh seconds, optional submitted fields, optional `pid=` identity | Renders `[unsupported partial: <url>]` when the URL can be extracted, otherwise `[unsupported block]`; partials are counted in the page notice area | Do not implement dynamic partials |
| Unsupported inline blocks | Inline `` `{...} `` style constructs in parser-compatible surfaces | Renders `[unsupported inline]` | Keep as explicit fallback |
| Unknown Micron tags | Any inline Micron tag outside this bounded renderer | Skipped and counted as `unsupported Micron tags: N` at the bottom of the page | Keep as explanatory degradation |
| UTF-8 / glyph output | Upstream expects UTF-8 and recommends Nerd Font glyph support | UTF-8 text is chunked safely; glyph support depends on available LVGL fonts | Keep best-effort |
| Browser/page resources | NomadNet can browse pages, files, dynamic node-side pages, and cached content | Trail Mate renders Micron page bodies from SD cache and can start bounded ESP page requests | Keep page-only, cache-first |

This table is the support ceiling for the device browser unless the product
boundary is explicitly revised. Trail Mate should not silently grow toward full
NomadNet browser behavior just because upstream Micron supports a feature.

The browser does not support form submission, scripts, images, attachments,
embedded resources, complex table layout, dynamic partials, large streaming
pages, or unlimited links. The current device budget is intentionally small:
page bodies are rendered from a 4096-byte buffer, each visible page can claim
at most 32 clickable links, 32 local anchors, 180 rendered rows, and 520
renderer-created LVGL objects. Tables render at most six cells per row.
Longer cached page bodies render with a `page truncated` notice; pages that hit
the renderer row/object budget render `Micron page truncated by device render
limit`. Unsupported Micron constructs render an explicit placeholder or notice
so the user can tell the page asked for something this device does not
implement.

## Anonymous Peer

Settings > Network includes `Anonymous Peer` while the active protocol is
`Reticulum`.

When enabled, Trail Mate keeps its local Reticulum identity and can still use
configured destinations, but it does not send local delivery or propagation
announces. It also suppresses local announce responses used for path discovery.
This makes the device less discoverable, but other nodes will not learn the
LXMF Address from normal Reticulum announce traffic. Disable `Anonymous Peer`
when you want ordinary peers to discover and message this device from Reticulum
announces.

In Reticulum mode, the Contacts page uses these filters:

- `Contacts`: saved Reticulum peers or contacts.
- `Nearby`: Reticulum peers learned from announces and path traffic.
- `Groups`: locally configured Reticulum shared group destinations.
- `Ignored`: nodes hidden from the normal contact lists.

For Reticulum peers, the list title follows the same model MeshChat exposes:
the LXMF announce display name first, then a local nickname/remark if no
announce display name is known, then the short hash fallback. Identity hash and
LXMF address are identity/address details, not the primary contact-list name.

`Groups` are not discovered from the Reticulum network. They are explicit local
configuration. This matters because a Reticulum shared destination must be known
before Trail Mate can send to it or accept inbound group packets for it.

Contacts also supports the same floating search interaction used by the Network
view. Press `/` or `s` while focus is on the Contacts page to search the current
Contacts, Nearby, Groups, or Ignored list. Meshtastic and MeshCore searches
match node short names; Reticulum searches also match the visible display name,
LXMF destination hash, and identity hash. The search only changes the visible
list; it does not modify the SD-backed node store, contacts, groups, or ignored
state.

Press `f` in Contacts or Chat to hide or show the filter column. This is useful
on the small screen when Reticulum display names are longer than the default
list column can comfortably show.

In Reticulum mode, press `a` in Contacts to add a peer by LXMF Address. The
dialog accepts either a plain 32-character destination hash or the MeshChat-style
`lxmf@...` form; spaces, `:`, `-`, and `_` inside the hash are ignored. Because
this manual entry only contains the delivery destination hash, Trail Mate stores
it as a local contact/node projection first. If the address already exists in
`lxmf_addresses.tsv`, the Add flow synchronously marks that address as a saved
contact so it survives reboot. If the full address is not known yet, a complete
`lxmf_addresses.tsv` row is written only after a real LXMF delivery announce or
path response provides the identity hash and public keys needed for a
verifiable address-book record.

In Chat, press `/` or `s` to search conversation/contact names in the current
Direct, Broadcast, or Team list. The Chat search is a conversation-list filter;
it does not search message body history.

In Reticulum Contacts, selecting a peer opens the action menu. `Chat` opens text
chat, `Info` shows Reticulum identity/address details, and `Call` starts a
Sideband-compatible LXST call when the peer identity and Wi-Fi gateway path are
known. If the device needs a path first, it sends a Reticulum path request and
shows `Path requested`; retry after the peer's announce/path response arrives.

## SD Card Requirement

Reticulum directory data lives on the SD card.

The firmware ships with zero preconfigured Reticulum groups. Every group shown
on the device must come from this SD-card file or from the device Add workflow.
Reticulum groups are not stored in ESP NVS or the normal app settings store;
that prevents hidden groups from surviving outside the SD-card source of truth.

Discovered Reticulum announces and LXMF delivery addresses are also persisted
to the SD card with streaming line-based reads and atomic temp-file replacement.
Runtime discovery persistence is deferred until a stable screen-off maintenance
window. If the user wakes the device or opens Contacts, Network, Map, Chat, or
another normal foreground page while a background directory transaction is
pending, Trail Mate keeps the original TSV file intact, requeues the pending
record, and waits for the next maintenance window instead of continuing to
write or remove directory files in the foreground. This lets the device keep
Reticulum network discovery and address-book state even when the runtime peer
cache is rebuilt, without letting public discovery traffic contend with the UI
for the SD bus.

If no SD card is available, Trail Mate must not silently use built-in default
groups. The Groups list will show an Add row, but tapping Add reports that an
SD card is required. Internally, the runtime group table is cleared when the SD
configuration cannot be loaded.

The SD card Reticulum directory paths are:

```text
/trailmate/reticulum/announces.tsv
/trailmate/reticulum/lxmf_addresses.tsv
/trailmate/reticulum/groups.tsv
```

When editing the card from a computer, create this path relative to the SD card
root:

```text
trailmate/reticulum/
```

## TSV File Format

The file is a UTF-8 text file using tab-separated values. The parser is small on
purpose so it remains reliable on ESP-class devices.

### Announces

`announces.tsv` is written by the Reticulum/LXMF runtime after an announce has
been parsed, destination-hash checked, and signature verified. It is a network
discovery cache, not a user-authored group list.

Each announce line has these fields:

```text
destination_hash<TAB>identity_hash<TAB>aspect<TAB>source<TAB>first_seen<TAB>last_seen<TAB>hops<TAB>path_response<TAB>local<TAB>delivery<TAB>propagation<TAB>display_name<TAB>raw_packet_hex<TAB>app_data_hex
```

`aspect` is usually `lxmf.delivery`, `lxmf.propagation`, `lxst.telephony`,
`nomadnetwork.node`, or `unknown`. Legacy `call.audio` announces may remain in
the diagnostic cache but are not a selectable product call protocol.
`source` is usually `runtime_rx` or `path_response`. `raw_packet_hex` stores
the verified raw Reticulum announce packet so future tooling can inspect what
was actually received. The runtime keeps the file bounded and updates an
existing destination line instead of appending duplicates. The Network page
loads this file as a bounded latest-100 projection rather than treating the
entire file as UI state. The SD cache itself is larger than the UI projection
so older entries can still be found by search.

### LXMF Addresses

`lxmf_addresses.tsv` is the Reticulum LXMF address book. It is populated from
valid LXMF delivery announces and can later be used by UI flows such as
Contacts, Nearby, Favorites, Ignored, and Network.

Each LXMF address line has these fields:

```text
destination_hash<TAB>identity_hash<TAB>enc_pub<TAB>sig_pub<TAB>alias<TAB>favorite<TAB>ignored<TAB>trusted<TAB>source<TAB>first_seen<TAB>last_seen
```

`destination_hash` is the 16-byte LXMF delivery destination hash shown as 32
hexadecimal characters. `enc_pub` and `sig_pub` are 32-byte Reticulum identity
public keys shown as 64 hexadecimal characters. `favorite`, `ignored`, and
`trusted` are user-side address-book flags. When the runtime refreshes an
address line from a new announce, it preserves those three flags.

The runtime keeps the file bounded and updates an existing destination line
instead of appending duplicates. On startup, Reticulum mode loads
`lxmf_addresses.tsv` back into the LXMF peer cache before also loading the
legacy ESP Preferences peer cache. Contacts and Nearby project a bounded latest
view of this file, and active search scans the file streamingly so older saved
or discovered peers can still be found without mounting the entire address book
as LVGL objects. Ignored address-book rows are not projected into Nearby.

### Groups

Each group line has exactly three fields:

```text
enabled<TAB>name<TAB>destination_hash
```

Do not type the literal string `<TAB>`. Press the Tab key or insert an actual
tab character between fields.

### Field Rules

`enabled`

- `1`, `true`, `yes`, and `enabled` mean the group is active.
- Any other value is treated as disabled.
- Disabled groups remain valid file entries, but they are not shown in the
  Groups chat list.

`name`

- This is the display name shown in Contacts > Groups.
- Maximum length is 31 bytes.
- ASCII names are safest. Short UTF-8 names may work, but remember that Chinese
  characters usually use 3 bytes each.
- The name must not contain tabs.

`destination_hash`

- This is a 16-byte Reticulum destination hash written as 32 hexadecimal
  characters.
- Uppercase and lowercase hex are both accepted.
- Spaces, `:`, `-`, and `_` inside the hash are ignored by the parser, but the
  clean 32-character form is recommended.
- This must be the shared group destination hash that the other Reticulum node
  is also listening on. A normal LXMF peer delivery hash belongs in Nearby or
  Contacts single-peer chat, not in the Groups TSV unless that host has
  explicitly configured the same hash as a shared group destination.

## Example

```text
# Trail Mate Reticulum groups
version	1
1	Kunming Team	0123456789ABCDEF0123456789ABCDEF
1	Home Node	FEDCBA9876543210FEDCBA9876543210
0	Test Lab	00112233445566778899AABBCCDDEEFF
```

Notes:

- Lines starting with `#` are comments.
- Blank lines are ignored.
- `version<TAB>1` is optional, but recommended.
- Trail Mate currently supports up to 4 configured Reticulum groups.
- Invalid group lines are skipped and logged as `[RTGroupConfig] skip invalid line`.

## Import Workflow

1. Power off Trail Mate or safely expose/remove the SD card.
2. On a computer, create the folder:

   ```text
   trailmate/reticulum
   ```

3. Create or edit:

   ```text
   trailmate/reticulum/groups.tsv
   ```

4. Write one group per line using the TSV format above.
5. Insert the SD card and boot Trail Mate.
6. Switch the active protocol to `Reticulum`.
7. Open Contacts > Groups.

The Groups list refreshes from the SD file. Re-entering the Contacts page, changing
protocol, or applying mesh config also reloads the SD group table.

## Adding From The Device UI

Contacts > Groups contains an Add row above the configured groups.

Tapping Add opens a Reticulum group configuration page with:

- `Name`
- `Destination hash`

Saving writes the SD-card TSV file immediately and reapplies the Reticulum mesh
configuration. This UI is useful for small edits, but entering a 32-character
hash on-device can be tedious. For repeatable setup, editing the TSV file on a
computer is the preferred workflow.

## Location And Realtime Sharing

Reticulum mode supports Trail Mate's team location business through LXMF
appdata. This is different from Meshtastic's native `POSITION_APP` packets.

When the active protocol is `Reticulum` and Team keys are available:

- Contacts > Team can send a current-location message.
- The team realtime track sampler can send `TEAM_TRACK_APP` updates.
- Incoming `TEAM_POSITION_APP` and `TEAM_TRACK_APP` packets are decoded by the
  Team service and shown through the Team UI/map stores.

Those packets are encrypted with the Trail Mate Team keys and carried inside
Reticulum/LXMF appdata. Other Trail Mate devices that know the same Team keys
can consume them. Generic Reticulum clients such as NomadNet or MeshChat may
see ordinary LXMF traffic, but they will not understand Trail Mate team
position payloads unless they implement the same Trail Mate appdata subset.

Reticulum Groups are LXMF shared destination chats. They currently carry text
messages. They do not currently send Trail Mate rich-location appdata to a
specific Reticulum group destination. That requires a future appdata send path
that can address a full Reticulum destination hash, or an explicit text-based
location format such as a `geo:` URI. This boundary avoids making Team
location sharing look like it was sent to a Reticulum group when it actually
uses the Team appdata channel.

## Troubleshooting

`SD card required`

Trail Mate could not see a mounted SD card. Insert a card and re-enter Contacts
> Groups or reboot.

`No Reticulum groups`

The SD card is present, but `/trailmate/reticulum/groups.tsv` does not exist or
contains no enabled valid groups.

`Destination must be 32 hex chars`

The destination field did not decode to exactly 16 bytes. Remove extra text and
make sure the field contains 32 hexadecimal characters.

`Group already exists`

The same destination hash is already present in the current group table.

Messages sent from Groups are not received by another node

Make sure the other node is listening on the same shared group destination hash.
Ordinary LXMF delivery peers are handled by Nearby or Contacts single-peer chat.
Groups require shared destination configuration on both sides.

## Current Boundary

This file configures Trail Mate's supported Reticulum group subset. It is not a
complete Reticulum configuration file and does not configure full upstream
Reticulum propagation node policy.

Trail Mate's Wi-Fi carrier currently implements the standard Reticulum
TCPInterface packet framing so it can exchange raw Reticulum packets with a
configured Reticulum TCP gateway. It does not use the Meshtastic MQTT proxy
path.
