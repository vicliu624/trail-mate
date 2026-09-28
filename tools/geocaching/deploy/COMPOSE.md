# NAS deployment without inbound ports

This Compose project runs a persistent geocaching directory, a raw Reticulum WebSocket bridge, and a named Cloudflare Tunnel. It publishes no NAS ports. There is no host networking, Docker socket mount, certificate server or static cache export.

The directory owns a private container network namespace. Bridge and tunnel share it, so `127.0.0.1:44242` and `127.0.0.1:8787` are internal to these containers, not the NAS host. Cloudflare terminates public TLS. Browser queries and responses pass through the tunnel in real time.

## Prerequisites

- Linux Docker Engine with Docker Compose v2.17 or later (for dependency restart propagation). Use the complete project; deploying the services separately loses their shared-network relationship.
- An amd64 or arm64 NAS able to pull the Python and Cloudflare images and install the pinned Python dependencies.
- A Cloudflare account and a domain that can be configured for a published application hostname. A DDNS hostname you cannot manage in Cloudflare is not automatically sufficient.
- Outbound DNS, HTTPS for image/dependency installation, TCP 7844 to Cloudflare, and TCP 4242 to the configured Reticulum peers. No inbound 80, 443 or other router forwarding is required.

The tunnel uses HTTP/2 over outbound TCP 7844, so UDP is not required. This is the connector transport; browser-facing WebSocket traffic still passes through it. Availability and latency must be checked from the NAS and actual visitors.

## 1. Get the files

For a new checkout on your NAS:

```sh
git clone --branch codex/geocaching --single-branch https://github.com/vicliu624/trail-mate.git
cd trail-mate/tools/geocaching/deploy
cp .env.example .env
```

For an existing checkout, update `codex/geocaching` and enter the same deployment directory. The build context is the repository root; copying only `compose.yaml` is insufficient. The Dockerfile-specific ignore file sends only the service files and requirement files to the builder, excluding local keys, databases and firmware output.

## 2. Create a named tunnel

In Cloudflare's tunnel dashboard, create a remotely managed Cloudflared tunnel. Obtain its tunnel token and put it in `.env` as `TUNNEL_TOKEN=...`. This is the tunnel token, not an API key. Keep it local.

Add a published application route:

| Setting | Value |
|---|---|
| Public hostname | A hostname you control, such as `geocaching.your-domain.example` |
| Path | Leave empty |
| Service type | **HTTP** |
| Service URL | **127.0.0.1:8787** |

Use HTTP for the internal service, not Cloudflare's arbitrary TCP application mode. The bridge already speaks WebSocket over HTTP. The resulting browser endpoint is `wss://geocaching.your-domain.example/`. Do not put an interactive Access login in front of this public endpoint: the map worker cannot complete that login flow.

Leave `SITE_ORIGIN=https://vicliu624.github.io` for the current website. It is the origin, not the full `/trail-mate/geocaching/` URL. `DIRECTORY_NAME` must fit in 40 UTF-8 bytes. The existing `reticulum/config` provides three editable upstream TCP entries and one loopback-only listener.

## 3. Build and start

```sh
docker compose config --quiet
docker compose build directory
docker compose up -d --no-build
docker compose ps
docker compose logs --tail=60 directory bridge tunnel
```

The first build installs the service dependencies; it does not compile Trail Mate firmware. Both Python services use the same local image. Container logs rotate at 5 MB, retaining at most three files per service.

Expected milestones:

- Directory emits JSON with `event: "ready"`, `delivery`, and **`discovery`** addresses.
- Bridge emits `event: "bridge_ready"`.
- Cloudflared reports a registered tunnel connection, and the dashboard shows the connector online.

The directory health check establishes a local TCP connection. The bridge check performs a local WebSocket handshake with the allowed Origin. These checks establish local readiness only; they do not prove public directory discovery, author verification or successful spatial queries. Docker restarts exited services, but does not restart a running process merely because its health status changes.

Send the operator the public **WSS URL** and the **discovery** value from the directory's ready log. Do not send the tunnel token, private identity file or database. The operator then updates `site/geocaching/network.json` and, where needed, device discovery seeds. That initial configuration deployment is separate from publishing cache data. Later cache publications and updates require no website deployment.

## Persistence and upgrades

The named volumes `trail-mate-geocaching_directory-data` and `trail-mate-geocaching_reticulum-data` preserve the directory identity, database, LXMF state and Reticulum state. A new empty deployment creates a new directory identity and an empty catalogue. It does not automatically restore previously published records whose only source is offline.

The database, requests, snapshots and records are not shared with a browser build. To retain an existing directory identity and content, migrate its complete stopped service state deliberately before starting this deployment; do not overwrite a running volume.

Upgrade all three services together because bridge and tunnel share the directory container's network namespace:

```sh
docker compose down
git pull --ff-only
docker compose build directory
docker compose pull tunnel
docker compose up -d --no-build
```

`down` retains named volumes. **Do not add `--volumes` or `-v`** unless intentionally deleting all saved directory data and identity. Use NAS volume backup tooling while services are stopped for a consistent backup; identity material is private. The Python service user is UID/GID 10001. Replacing named volumes with NAS bind mounts requires granting that user write access.

## Verification and limits

Compose configuration is validated before release. A live Docker build/run and end-to-end public query still need validation on the NAS; the development machine's Linux Docker engine was unavailable when this deployment was prepared.

After startup, verify a live query, exact signed detail and GPX download. Then publish a new revision and query again without rebuilding or deploying the website. Restart the NAS containers and verify the directory identity and data survive. The bridge admits 32 concurrent clients; this is a connection limit, not a tested NAS capacity guarantee.

Reference: [Cloudflare setup](https://developers.cloudflare.com/tunnel/get-started/), [outbound firewall requirements](https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/configure-tunnels/tunnel-with-firewall/), [tunnel run parameters](https://developers.cloudflare.com/tunnel/reference/run-parameters/).
