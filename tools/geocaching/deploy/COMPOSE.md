# Synology DDNS deployment on TCP 18433

Use `vicliu.i234.me` with public TCP **18433** forwarded to NAS TCP **18433**. This replaces the previous Cloudflare setup. No Cloudflare account, tunnel token or NAS ports 80/443 are needed. A dynamic public IP is supported through DDNS; the router must have a reachable public IP and allow port forwarding.

```text
Browser -> wss://vicliu.i234.me:18433/
        -> router TCP 18433 -> NAS TCP 18433
        -> bridge TLS listener, container port 8787
        -> loopback Reticulum TCP 44242 + persistent directory
```

Two containers use the same locally built Python image. Bridge shares directory's private network namespace, so its published port is declared on `directory`. Port 44242 remains loopback-only and unpublished. Neither container uses host networking or the Docker socket. All cache queries are live.

## Requirements

- Linux Docker Engine and Docker Compose v2.17 or later.
- An amd64 or arm64 NAS able to pull the Python image and install pinned Python packages.
- Working DDNS and a router forwarding TCP 18433 to a reserved/stable NAS LAN address on port 18433.
- A valid browser-trusted certificate covering `vicliu.i234.me`, its full chain and matching private key.
- Outbound DNS, HTTPS for installation and TCP 4242 for the configured Reticulum peers.

A certificate covers the hostname, not the port. Use the appropriate DSM-managed certificate or establish issuance/renewal supported by your DSM setup. Compose does not issue certificates and does not assume HTTP-01 can use blocked port 80. Do not use a self-signed certificate or disable browser verification.

## Get the files

```sh
git clone --branch codex/geocaching --single-branch https://github.com/vicliu624/trail-mate.git
cd trail-mate/tools/geocaching/deploy
cp .env.example .env
```

For an existing checkout, preserve local changes and update the branch. The complete repository is required because the Dockerfile uses the service source files. Its ignore file excludes certificates, databases and firmware output from the build context.

Configure `.env`:

```dotenv
WSS_HOST=vicliu.i234.me
WSS_PORT=18433
TLS_CERT_DIR=./certs
DIRECTORY_NAME=Trail Mate public directory
SITE_ORIGIN=https://vicliu624.github.io
```

Origin must not include the website path. Directory name is limited to 40 UTF-8 bytes. `WSS_HOST` controls certificate health verification; DDNS and the website endpoint are configured separately. Remove the unused `TUNNEL_TOKEN` from an earlier deployment's `.env`.

## Prepare the certificate

Export or copy the correct DSM-managed certificate into a dedicated deployment directory:

```text
certs/
  fullchain.pem
  privkey.pem
```

`fullchain.pem` contains the leaf certificate followed by its intermediates. `privkey.pem` is its matching unencrypted private key. Inspect the actual DSM export rather than assuming its file names. Confirm the subject alternative names include `vicliu.i234.me`, expiry is valid, and certificate/private-key public keys match.

The image runs as UID/GID **10001:10001**. Set permissions on the deployment copy only:

```sh
sudo chown -R 10001:10001 ./certs
sudo chmod 700 ./certs
sudo chmod 600 ./certs/fullchain.pem ./certs/privkey.pem
```

Do not change DSM's system certificate-store ownership or mount all NAS keys. The deployment certificate directory is ignored by Git, excluded from the image and mounted read-only. `TLS_CERT_DIR` can instead name an absolute dedicated directory. The bind mount refuses to create a missing source directory.

## Router and firewall

Forward **TCP 18433 external -> NAS LAN IP:18433 internal** and allow it in the NAS firewall. No UDP forwarding is required. Do not forward 44242 or 8787 directly. Verify existing services are not using NAS TCP 18433.

If an AAAA record exists, verify its IPv6 routing/firewall too; an unreachable IPv6 destination can disrupt access even when IPv4 works. Check DDNS after WAN address changes. Test externally because LAN NAT loopback behavior varies.

## Build and start

```sh
docker compose config --quiet
docker compose build directory
docker compose up -d --no-build --remove-orphans
docker compose ps
docker compose logs --tail=60 directory bridge
```

Only Python services are built, not device firmware. `--remove-orphans` removes the previous `tunnel` container from this same Compose project, if present, without deleting named volumes. Check the project name before using it.

Directory emits a `ready` event containing `delivery`, **`discovery`**, epoch and sequence. Bridge emits `bridge_ready` with `tls: true`. Both processes restart after exit and their logs rotate at 5 MB with three retained files.

Directory health verifies its local TCP listener. Bridge health verifies a local TLS handshake using the real hostname, system trust roots and expiry checks. These do not prove public port forwarding or application queries. An unhealthy running container is not automatically restarted by Docker.

## External verification and handoff

Test `wss://vicliu.i234.me:18433/` from another Internet connection with Origin **`https://vicliu624.github.io`**. Other Origins are intentionally rejected. An ordinary HTTPS GET may be rejected because this is a WebSocket endpoint, not a homepage.

Return the WSS URL, the **discovery** hash from the ready log, service status and external connectivity results to the website operator. Never send the private key or database. The operator then updates website connection configuration and any required device discovery hints, and verifies spatial queries, signed details and GPX.

Only that initial connection configuration needs a website deployment. New cache publications and revisions do not. A new directory starts empty with a new identity and cannot automatically recover data whose only source is offline. Existing identity/data migration must be deliberate and performed with the relevant service stopped.

## Certificate renewal

The bridge loads TLS material at startup. DSM renewal alone does not refresh the deployment copy or reload the process. Configure a privileged DSM scheduled task or supported renewal hook to:

1. Select the correct certificate from the actual DSM certificate configuration.
2. Stage the renewed chain and key privately; verify hostname, expiry and key match before replacing anything.
3. Copy both files into the existing deployment `certs` directory with UID/GID 10001 and the permissions above. Keep the mounted directory itself in place; replace its files rather than swapping its directory inode.
4. Once both files are ready, execute `docker compose restart bridge` from the deployment directory.
5. Check bridge health and external TLS. Retain the previous working pair if staging validation fails.

Run this when the certificate changes. Restart briefly interrupts browser sessions. Preserve the renewal task across NAS upgrades; never log private-key contents. The NAS operator must implement the hook using the actual DSM certificate paths, which this repository does not guess.

## Persistence and upgrades

Named volumes `trail-mate-geocaching_directory-data` and `trail-mate-geocaching_reticulum-data` retain identity, SQLite, LXMF and Reticulum state. Back up consistently while services are stopped; identity material is private. Bind-mount replacements require write access for UID 10001.

Upgrade the services together because they share a network namespace:

```sh
docker compose down
git pull --ff-only
docker compose build directory
docker compose up -d --no-build --remove-orphans
```

`down` preserves data. **Do not add `-v` or `--volumes`.** Preserve certificate copies and the renewal procedure separately from the directory data volumes.

## Verification boundary

The Compose model is checked locally for exactly one host-port mapping and no tunnel service. The development machine's Linux Docker engine is unavailable, so image build and container startup still need NAS verification. The bridge's existing TLS options are reused without protocol code changes.

The bridge limit is 32 concurrent clients, not a tested NAS capacity guarantee. Acceptance requires a publication and a later revision appearing in fresh browser queries without website deployment, plus identity/data persistence across service restart.
