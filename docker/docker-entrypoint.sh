#!/bin/sh
# Resolve the SR mesh bootstrap and launch the relay.
#
# Precedence: an operator override wins, otherwise the image's baked pinned
# default. The bootstrap is a SIGNED RC list and the relay verifies its signature
# when it loads add-node; this script only selects which file to point at, it does
# not vouch for the contents.
set -eu

SR_CONFIG="${SR_CONFIG:-/etc/xeqm-session-router/snode1.ini}"
SR_BOOTSTRAP_BAKED="/opt/xeqm/share/session-router/bootstrap.signed"
# Override: set SR_BOOTSTRAP, or bind-mount a file at this default override path.
SR_BOOTSTRAP_OVERRIDE="${SR_BOOTSTRAP:-/etc/xeqm-session-router/bootstrap.signed}"
RUNTIME_INI="/tmp/session-router.runtime.ini"

boot_src=""
if [ -s "$SR_BOOTSTRAP_OVERRIDE" ]; then
  boot_src="$SR_BOOTSTRAP_OVERRIDE"
  echo "session-router: bootstrap = operator override ($SR_BOOTSTRAP_OVERRIDE)"
elif [ -s "$SR_BOOTSTRAP_BAKED" ]; then
  boot_src="$SR_BOOTSTRAP_BAKED"
  echo "session-router: bootstrap = baked image default"
fi

# The mounted ini may be read-only, so render a writable copy. Drop any add-node the
# template carries (active or commented) and let the entrypoint own it, so the key can
# never end up active under a commented [bootstrap] header.
cp "$SR_CONFIG" "$RUNTIME_INI"
sed -i -E '/^[[:space:]]*#?[[:space:]]*add-node=/d' "$RUNTIME_INI"
if [ -n "$boot_src" ]; then
  printf '\n[bootstrap]\nadd-node=%s\n' "$boot_src" >> "$RUNTIME_INI"
else
  # No signed bootstrap is published yet (see README, "Bootstrap list"). SR is
  # NON-ENFORCING in 1.1.4, so the local-socket adoption signal still works without
  # a mesh bootstrap; the relay just will not form the mesh.
  # TODO(enforcement HF): make this fail-fast once the SR mesh is required.
  echo "session-router: WARNING no bootstrap available; SR mesh will not form (acceptable while non-enforcing)"
fi

exec /opt/xeqm/bin/session-router -r -c "$RUNTIME_INI" "$@"
