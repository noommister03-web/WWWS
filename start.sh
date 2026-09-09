#!/bin/sh
set -eu
: "${BROWSER_WORKER_SHARED_SECRET:?BROWSER_WORKER_SHARED_SECRET is required}"
: "${REMOTE_BROWSER_PASSWORD:?REMOTE_BROWSER_PASSWORD is required}"
PORT="${PORT:-8080}"
echo "WWWS release 2026.09.09-r1 (Telegram UI + CustoJusto worker)"
PIDS=""
stop(){ kill $PIDS 2>/dev/null || true; wait 2>/dev/null || true; }; trap stop INT TERM EXIT
export DISPLAY=:99
mkdir -p /tmp/.X11-unix /app/data/custojusto/profiles
rm -f /tmp/.X99-lock
Xvfb "$DISPLAY" -screen 0 1365x900x24 -nolisten tcp >/tmp/xvfb.log 2>&1 & PIDS="$!"
for n in $(seq 1 20); do [ -S /tmp/.X11-unix/X99 ] && break; sleep 1; done
[ -S /tmp/.X11-unix/X99 ] || { cat /tmp/xvfb.log >&2; exit 1; }
openbox >/tmp/openbox.log 2>&1 & PIDS="$PIDS $!"
x11vnc -display "$DISPLAY" -forever -shared -rfbport 5900 -nopw >/tmp/x11vnc.log 2>&1 & PIDS="$PIDS $!"
websockify --web=/usr/share/novnc 127.0.0.1:6080 127.0.0.1:5900 >/tmp/websockify.log 2>&1 & PIDS="$PIDS $!"
node /app/browser_worker.js >/tmp/browser-worker.log 2>&1 & PIDS="$PIDS $!"
for n in $(seq 1 30); do node -e 'fetch("http://127.0.0.1:3001/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))' && break; sleep 1; done
node -e 'fetch("http://127.0.0.1:3001/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))' || { cat /tmp/browser-worker.log >&2; exit 1; }
node /app/gateway.js >/tmp/gateway.log 2>&1 & PIDS="$PIDS $!"
sleep 1
node -e 'fetch("http://127.0.0.1:"+(process.env.PORT||"8080")+"/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))' || { cat /tmp/gateway.log >&2; exit 1; }
exec /app/tg_bot
