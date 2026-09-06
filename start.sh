#!/bin/sh
set -eu

: "${BROWSER_WORKER_SHARED_SECRET:?BROWSER_WORKER_SHARED_SECRET is required}"
: "${REMOTE_BROWSER_PASSWORD:?REMOTE_BROWSER_PASSWORD is required}"

# Railway supplies PORT. The public listener is deliberately only the Caddy
# gateway; the Telegram bot and browser-worker stay on localhost.
PUBLIC_PORT="${PORT:-8080}"
DISPLAY=:99
export DISPLAY

Xvfb "$DISPLAY" -screen 0 1365x900x24 -nolisten tcp &
xvfb_pid=$!
openbox >/tmp/openbox.log 2>&1 &
openbox_pid=$!
x11vnc -display "$DISPLAY" -forever -shared -rfbport 5900 -nopw >/tmp/x11vnc.log 2>&1 &
vnc_pid=$!
websockify --web=/usr/share/novnc 127.0.0.1:6080 127.0.0.1:5900 >/tmp/websockify.log 2>&1 &
websockify_pid=$!

node /app/browser_worker.js >/tmp/browser-worker.log 2>&1 &
worker_pid=$!

# Opens exactly one selected persistent profile on the graphical display.
# Set CJ_MANUAL_ACCOUNT_ID to the CustoJusto account ID being connected.
node /app/manual_browser.js >/tmp/manual-browser.log 2>&1 &
manual_pid=$!

password_hash="$(caddy hash-password --plaintext "$REMOTE_BROWSER_PASSWORD")"
cat > /tmp/Caddyfile <<EOF
:{$PUBLIC_PORT} {
  basicauth /* {
    custo $password_hash
  }
  reverse_proxy 127.0.0.1:6080
}
EOF
caddy run --config /tmp/Caddyfile --adapter caddyfile >/tmp/caddy.log 2>&1 &
caddy_pid=$!

cleanup() {
  kill "$caddy_pid" "$manual_pid" "$worker_pid" "$websockify_pid" "$vnc_pid" "$openbox_pid" "$xvfb_pid" 2>/dev/null || true
  wait 2>/dev/null || true
}
trap cleanup INT TERM EXIT

retries=0
until node -e 'fetch("http://127.0.0.1:3001/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))'; do
  retries=$((retries + 1))
  if [ "$retries" -ge 30 ]; then
    echo "CustoJusto browser worker did not become ready" >&2
    exit 1
  fi
  sleep 1
done

exec /app/tg_bot
