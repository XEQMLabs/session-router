#!/bin/sh
# Pick the relay's bootstrap list and launch the relay.
#
# Precedence: an operator override wins, otherwise the list baked into the image.
# The list is signed and the relay verifies the signature when it loads it; this
# script only selects which file to point at.
set -eu

SR_CONFIG="${SR_CONFIG:-/etc/xeqm-session-router/snode1.ini}"
SR_BOOTSTRAP_BAKED="/opt/xeqm/share/session-router/bootstrap.signed"
# Override: set SR_BOOTSTRAP, or bind-mount a file at this default override path.
SR_BOOTSTRAP_OVERRIDE="${SR_BOOTSTRAP:-/etc/xeqm-session-router/bootstrap.signed}"
RUNTIME_INI="/tmp/session-router.runtime.ini"

if [ ! -r "$SR_CONFIG" ]; then
  echo "session-router: config not found at $SR_CONFIG (mount your session-router.ini there)" >&2
  exit 1
fi

if [ -s "$SR_BOOTSTRAP_OVERRIDE" ]; then
  boot_src="$SR_BOOTSTRAP_OVERRIDE"
  echo "session-router: bootstrap = operator override ($SR_BOOTSTRAP_OVERRIDE)"
elif [ -s "$SR_BOOTSTRAP_BAKED" ]; then
  boot_src="$SR_BOOTSTRAP_BAKED"
  echo "session-router: bootstrap = image default"
else
  echo "session-router: no bootstrap list found (expected $SR_BOOTSTRAP_BAKED)" >&2
  exit 1
fi

# The mounted ini may be read-only, so render a writable copy. Drop any add-node the
# file carries (active or commented) and let this script own the [bootstrap] section.
cp "$SR_CONFIG" "$RUNTIME_INI"
sed -i -E '/^[[:space:]]*#?[[:space:]]*add-node=/d' "$RUNTIME_INI"
printf '\n[bootstrap]\nadd-node=%s\n' "$boot_src" >> "$RUNTIME_INI"

# With listen=0.0.0.0 the relay binds its public-ip, which is not on a container's
# interface (Docker forwards the published port instead), and the bind fails with
# "Cannot assign requested address". Bind the container's own address in that case.
if grep -q -E '^[[:space:]]*listen=0\.0\.0\.0:' "$RUNTIME_INI"; then
  own_ip="$(hostname -i 2>/dev/null | tr ' ' '\n' | grep -E '^[0-9]+(\.[0-9]+){3}$' | grep -v '^127\.' | head -n 1 || true)"
  if [ -n "$own_ip" ]; then
    sed -i -E "s/^[[:space:]]*listen=0\.0\.0\.0:/listen=${own_ip}:/" "$RUNTIME_INI"
    echo "session-router: listen address = $own_ip (container address)"
  fi
fi

exec /opt/xeqm/bin/session-router -r -c "$RUNTIME_INI" "$@"
