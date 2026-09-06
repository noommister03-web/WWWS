#!/bin/sh
set -eu

: "${BROWSER_WORKER_SHARED_SECRET:?BROWSER_WORKER_SHARED_SECRET is required}"
PUBLIC_PORT="${PORT:-8080}"
MANUAL_MODE="${CJ_MANUAL_BROWSER_MODE:-false}"

worker_pid=""
manual_pid=""
xvfb_pid=""
openbox_pid=""
vnc_pid=""
websockify_pid=""

if [ "$MANUAL_MODE" = "true" ]; then
  : "${REMOTE_BROWSER_PASSWORD:?REMOTE_BROWSER_PASSWORD is required when CJ_MANUAL_BROWSER_MODE=true}"
  export DISPLAY=:99
  Xvfb "$DISPLAY" -screen 0 1365x900x24 -nolisten tcp &
  xvfb_pid=$!
  openbox >/tmp/openbox.log 2>&1 &
  openbox_pid=$!
  x11vnc -display "$DISPLAY" -forever -shared -rfbport 5900 -nopw >/tmp/x11vnc.log 2>&1 &
  vnc_pid=$!
  websockify --web=/usr/share/novnc 127.0.0.1:6080 127.0.0.1:5900 >/tmp/websockify.log 2>&1 &
  websockify_pid=$!

  # The headed browser has exclusive access to the persistent CustoJusto
  # profile while the account owner completes Turnstile and login.
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
else
  # Normal mode: only the private worker uses the saved profile. Railway's
  # public endpoint returns a harmless health response and cannot reach it.
  node /app/browser_worker.js >/tmp/browser-worker.log 2>&1 &
  worker_pid=$!
  cat > /tmp/Caddyfile <<EOF
:{$PUBLIC_PORT} {
  respond "CustoJusto bridge is running" 200
}
EOF
fi

caddy run --config /tmp/Caddyfile --adapter caddyfile >/tmp/caddy.log 2>&1 &
caddy_pid=$!

cleanup() {
  kill "$caddy_pid" "$manual_pid" "$worker_pid" "$websockify_pid" "$vnc_pid" "$openbox_pid" "$xvfb_pid" 2>/dev/null || true
  wait 2>/dev/null || true
}
trap cleanup INT TERM EXIT

if [ "$MANUAL_MODE" != "true" ]; then
  retries=0
  until node -e 'fetch("http://127.0.0.1:3001/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))'; do
    retries=$((retries + 1))
    if [ "$retries" -ge 30 ]; then
      echo "CustoJusto browser worker did not become ready" >&2
      exit 1
    fi
    sleep 1
  done
fi

exec /app/tg_bot
