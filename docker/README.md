# XEQM Session Router relay in Docker

Runs the Session Router relay as a second container next to a Dockerized XEQM
**service node**. The relay reads the node's service node keys over the node's
control socket and the node then reports the relay's version in its uptime proofs.
Running the relay is optional: stake, rewards and uptime checks do not depend on it.

For a relay installed directly on the host with systemd, use the release package and
the guide at https://explorer.xeqmlabs.com/guides/exiomnet instead.

Files:

- `docker-compose.sr.yml` - the node and the relay as two containers
- `Dockerfile.session-router` - builds the relay image from the release package
- `docker-entrypoint.sh` - prepares the relay config and starts the relay
- `session-router.ini.example` - relay config, copy to `session-router.ini`

## When this applies

The relay belongs with a service node. A seed node or a plain full node has no
service node identity, so the relay has nothing to announce there.

The compose file keeps the node's data in `./data` next to it. To use it with a node
you already run, put the files in the directory that holds that node's `data`
folder, stop and remove the old node container, and start both from this file.
Registration and staking are not covered here.

## Set up

Needs Docker with the compose plugin, on a Linux server (amd64 or arm64).

```bash
# 1. Relay config: set public-ip to your server's public IPv4 address.
cp session-router.ini.example session-router.ini
nano session-router.ini

# 2. Node config, next to the compose file. At least:
cat > xeqm.conf <<'EOF'
service-node=1
service-node-public-ip=YOUR_PUBLIC_IPV4
EOF

# 3. Build the relay image and start both containers.
docker compose -f docker-compose.sr.yml up -d --build
```

Open these ports in your firewall: TCP 9230 (p2p), TCP 9232 (quorumnet) and
UDP 1190 (relay).

The build downloads the release package named in the Dockerfile and checks it against
the sha256 pinned there, then checks the package's own `SHA256SUMS`.

## Check that it works

```bash
docker compose -f docker-compose.sr.yml ps
docker logs xeqm-session-router 2>&1 | grep -E "identity|listening"
docker logs xeqm-node 2>&1 | grep "ping from Session Router"
```

Both containers should be `Up`. The relay log shows `Obtained service node identity`
and `relay listening on`. The node log shows `Received ping from Session Router 1.1.2`
once, when the relay first connects. The relay version appears on the network with the
node's next uptime proof.

Warnings about UDP buffer sizes and `Network ID is xeqm` are normal.

## How the two containers connect

Both containers mount `./data` at `/data`. The node creates its control socket there
(`--lmq-local-control=ipc:///data/oxend.sock`) and the relay connects to it. That
socket gives full control of the node, so keep it a socket file in that directory and
never publish it as a TCP port.

Things that are easy to get wrong:

1. **Keep `restart: unless-stopped` on the relay.** Without it the relay does not come
   back after a reboot, and the node goes back to reporting no relay.
2. **The relay runs as root inside its container.** The node image runs as root and
   its socket is readable by root only, and Docker gives the `NET_ADMIN` capability,
   which the relay needs for its TUN device, to root only. The container has Docker's
   default capabilities plus `NET_ADMIN`, nothing more.
3. **`public-ip` is required.** The relay does not start without it.
4. **Leave `listen=0.0.0.0:1190` as it is.** The entrypoint replaces `0.0.0.0` with the
   container's own address, because the public address is not on the container's
   interface and binding it fails with `Cannot assign requested address`.
5. **Quorumnet is TCP.** Publish 9232 as TCP, as the compose file does.

## Bootstrap list

The relay joins the network through a signed list of relays. The release package
ships it (`contrib/bootstrap/xeqm.signed` in this repository), the image carries that
copy, and the relay verifies its signature when it loads it. Nothing has to be placed
by hand. To use a different list, mount it at
`/etc/xeqm-session-router/bootstrap.signed` (see the commented line in the compose
file) or set `SR_BOOTSTRAP` to its path.

## Updating

For a new relay release, change `SR_TAG` and the two checksums in
`Dockerfile.session-router` (they are on the release page), then:

```bash
docker compose -f docker-compose.sr.yml up -d --build session-router
```

## Tested

On Docker 28 (amd64), with xeqm-node v1.1.4 and the `sidecar-v1.1.2` package: the image
builds, the relay reads the node's identity, creates its TUN device and the node
records its pings; the relay reconnects by itself after the node restarts, and both
come back after a stop and start. The node in that test was offline and not
registered, and its data directory was a Docker volume rather than `./data`. The arm64
build was not run.
