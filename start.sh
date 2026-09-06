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

  # A restart can inherit a stale X lock while the X server itself is still
  # usable. Reuse the existing display; remove only a stale lock/socket pair.
  if [ ! -S /tmp/.X11-unix/X99 ]; then
    rm -f /tmp/.X99-lock
    mkdir -p /tmp/.X11-unix
    Xvfb "$DISPLAY" -screen 0 1365x900x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
    xvfb_pid=$!
    retries=0
    until [ -S /tmp/.X11-unix/X99 ]; do
      retries=$((retries + 1))
      if [ "$retries" -ge 20 ]; then
        cat /tmp/xvfb.log >&2 || true
        echo "Xvfb did not become ready" >&2
        exit 1
      fi
      sleep 1
    done
  fi

  openbox >/tmp/openbox.log 2>&1 &
  openbox_pid=$!
  x11vnc -display "$DISPLAY" -forever -shared -rfbport 5900 -nopw >/tmp/x11vnc.log 2>&1 &
  vnc_pid=$!
  websockify --web=/usr/share/novnc 127.0.0.1:6080 127.0.0.1:5900 >/tmp/websockify.log 2>&1 &
  websockify_pid=$!
  node /app/manual_browser.js >/tmp/manual-browser.log 2>&1 &
  manual_pid=$!

  password_hash="$(caddy hash-password --plaintext "$REMOTE_BROWSER_PASSWORD")"
  cat > /tmp/Caddyfile <<EOF
:{$PUBLIC_PORT} {
  basic_auth /* {
    custo $password_hash
  }
  reverse_proxy 127.0.0.1:6080
}
EOF
else
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
