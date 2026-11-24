# XEQM Session Router - Docker (two-container variant)

Draft for review. Adds the Session Router (SR) sidecar to a Dockerized XEQM
**service node**, so the node reports `session_router_version` the way our
systemd-managed fleet does. SR is **non-enforcing** in 1.1.4 (an adoption signal
only; it does not gate proofs or consensus).

Files here:
- `docker-compose.sr.yml` - the two-container stack (daemon + SR sidecar)
- `Dockerfile.session-router` - builds the SR runtime image (not yet published)
- `docker-entrypoint.sh` - resolves the bootstrap and launches the relay
- `session-router.ini.example` - SR relay config (copy to `session-router.ini`)

## When this applies

SR is a **service-node** sidecar. It self-fetches the SN private keys from the
daemon over the admin socket and pings as that service node. A **seed node** or
plain **pubnode** has no SN identity, so SR does nothing there - do not run it on
those. The current public quickstart (`ghcr.io/xeqmlabs/xeqm-node:v1.1.4` in the
seed layout) therefore has no routing layer at all, which is expected for a seed;
SR belongs with a registered service node.

This compose assumes an **already-registered** service node whose keys live in the
mounted data volume. Registration and staking are out of scope here.

## How the two containers connect

The daemon exposes its `lmq-local-control` admin socket as an IPC socket on a
**shared data volume** (`ipc:///data/oxend.sock`). The SR container mounts the same
volume and connects to that socket. That local connection is the **only** wiring the
non-enforcing adoption signal needs - the SR UDP mesh port is optional until SR
enforcement is flipped (a future HF). OCI hosts that cannot form the outbound mesh
still report `[1,1,0]` for exactly this reason.

The admin socket grants privileged control, so keep it an `ipc://` socket on the
private shared volume. Never publish it on a TCP port.

## Bootstrap list

The SR mesh bootstrap is a **signed RC list**: the relay verifies its signature when
it loads `add-node`, so the file is safe to distribute over any channel. The image
handles it so operators do not hand-place a blob:

- **Baked pinned default.** The Dockerfile fetches the published bootstrap at build
  time and verifies it against a pinned `sha256` before baking it into the image at
  `/opt/xeqm/share/session-router/bootstrap.signed`. Every image then carries a
  known-good default and starts with no manual step.
- **Runtime verification.** The relay checks the signed RC list's signature on load;
  the build-time `sha256` pins exactly which release the image carries.
- **Optional override.** To run a different list, set `SR_BOOTSTRAP` or bind-mount a
  file at `/etc/xeqm-session-router/bootstrap.signed`. The entrypoint prefers an
  override and otherwise uses the baked default. This is an escape hatch, not the
  normal path.

> **TODO (release owner):** the signed bootstrap is **not published yet** - the
> convention URL `https://bootstrap.xeqmlabs.com/session-router-bootstrap.signed`
> returns 404. Until it is published, build with the bootstrap ARGs empty: the image
> ships **without** a baked default, which is fine while SR is non-enforcing (the
> local-socket adoption signal needs no mesh). Once the file is published, build with
> `--build-arg SR_BOOTSTRAP_URL=... --build-arg SR_BOOTSTRAP_SHA256=...` so every
> image carries a verified default, and the entrypoint's "no bootstrap" warning path
> should be tightened to fail-fast when SR enforcement lands.

## Two things that are easy to get wrong

1. **Persistence = `restart: unless-stopped`.** This is the Docker equivalent of
   `systemctl enable`. Without it the sidecar does not return after a reboot or a
   daemon restart, and the daemon's `session_router_version` silently resets to
   `[0,0,0]` with no recovery. This is the exact failure that decayed our fleet
   from 68 to 52 reporting nodes. The compose sets it; keep it.

2. **Socket ownership across containers.** The SR image runs as user `xeqm`; it must
   be able to connect to the daemon's socket on the shared volume. If your daemon
   image runs as root and the socket is root-only, either run both services as the
   same `user:` UID or relax the socket ownership. This is the single integration
   point - verify it once.

## Build and run

```bash
# 1. Build + publish the SR image (x86-64; add --platform linux/arm64 for arm).
#    Once the signed bootstrap is published, add the two bootstrap ARGs so the image
#    bakes a pinned, verified default (see "Bootstrap list"):
#      --build-arg SR_BOOTSTRAP_URL=https://bootstrap.xeqmlabs.com/session-router-bootstrap.signed \
#      --build-arg SR_BOOTSTRAP_SHA256=<sha256>
docker build -f Dockerfile.session-router -t ghcr.io/xeqmlabs/xeqm-session-router:v1.1.4 .
docker push ghcr.io/xeqmlabs/xeqm-session-router:v1.1.4

# 2. Prepare config next to the compose file
cp session-router.ini.example session-router.ini   # edit public-ip if you want the mesh
#   place your service-node xeqm.conf here too (must include --service-node)
#   bootstrap: nothing to place - the image uses its baked default. Only bind-mount
#   your own bootstrap.signed (see the compose override line) to override it.

# 3. Launch
docker compose -f docker-compose.sr.yml up -d
```

## Verify

```bash
# daemon admin RPC (adjust port to your node); last_session_router_ping should go
# nonzero within ~15s and increment every ~30s:
curl -s http://127.0.0.1:9231/get_info -H 'Content-Type: application/json' \
  | grep -o '"last_session_router_ping":[0-9]*'
```

The network-visible `session_router_version` flips `[0,0,0] -> [1,1,0]` at the node's
next uptime proof (~1h), which is when the public dashboard count ticks up.

## Open items for the release owner

- **Publish the signed bootstrap** to `bootstrap.xeqmlabs.com` (the convention URL
  still 404s), then rebuild the image with the `SR_BOOTSTRAP_URL` / `SR_BOOTSTRAP_SHA256`
  ARGs so it carries a verified default. See "Bootstrap list".
- **Publish the SR image.** The Dockerfile is VALIDATED (built on fernando 2026-11-21,
  x86-64, 102MB; runs as non-root `xeqm`; `--version` = `session-router-1.1.0-7d6931f9`
  matching the fork branch HEAD; all three binaries present). The `find` resolves the
  binary at `/src/build/session-router` (build root, not `src/daemon/`). Only the ghcr
  tag + push remains (owner creds). BOTH arches are validated (built on fernando
  2026-11-21): amd64 `xeqm-session-router:v1.1.4-test` (102MB) and arm64
  `xeqm-session-router:v1.1.4-test-arm64` (124MB, built under qemu, `uname -m`=aarch64,
  same `--version`). For a single multi-arch tag that community pulls arch-correct,
  publish with buildx rather than two separate tags:
  `docker buildx build --platform linux/amd64,linux/arm64 -f Dockerfile.session-router -t ghcr.io/xeqmlabs/xeqm-session-router:v1.1.4 --push .`
- **Alternative packaging.** Instead of two containers, a single container with a
  supervisor (s6 or tini + a small script) can run daemon + SR together. The
  two-container split is cleaner (independent restart, independent logs, the SR image
  stays slim) and is the recommended default; the supervisor option is noted for
  completeness.
- **Decide 1.1.4's community routing layer** (ship SR vs classic lokinet vs neither).
  This is the open strategic call; the packaging here does not presume the outcome.
- **xeqm.conf for a service node.** The seed-layout quickstart `xeqm.conf` is not a
  service-node config; a service-node sample (with `--service-node`, quorumnet, public
  SN IP, key persistence) should ship alongside this.
