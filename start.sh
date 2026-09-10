#!/bin/sh
set -eu
: "${BROWSER_WORKER_SHARED_SECRET:?BROWSER_WORKER_SHARED_SECRET is required}"
: "${REMOTE_BROWSER_PASSWORD:?REMOTE_BROWSER_PASSWORD is required}"
PORT="${PORT:-8080}"
echo "WWWS release 2026.09.11-r1 (AI hardening + guarded CustoJusto worker)"
PIDS=""
stop(){ [ -n "$PIDS" ] && kill $PIDS 2>/dev/null || true; wait 2>/dev/null || true; }
terminate(){ trap - INT TERM EXIT; stop; exit 0; }
trap terminate INT TERM
trap stop EXIT
export DISPLAY=:99
mkdir -p /tmp/.X11-unix /app/data/custojusto/profiles
rm -f /tmp/.X99-lock
Xvfb "$DISPLAY" -screen 0 1365x900x24 -nolisten tcp >/tmp/xvfb.log 2>&1 & XVFB_PID=$!; PIDS="$XVFB_PID"
for n in $(seq 1 20); do [ -S /tmp/.X11-unix/X99 ] && break; sleep 1; done
[ -S /tmp/.X11-unix/X99 ] || { cat /tmp/xvfb.log >&2; exit 1; }
openbox >/tmp/openbox.log 2>&1 & OPENBOX_PID=$!; PIDS="$PIDS $OPENBOX_PID"
x11vnc -display "$DISPLAY" -forever -shared -rfbport 5900 -nopw >/tmp/x11vnc.log 2>&1 & VNC_PID=$!; PIDS="$PIDS $VNC_PID"
websockify --web=/usr/share/novnc 127.0.0.1:6080 127.0.0.1:5900 >/tmp/websockify.log 2>&1 & WEBSOCKIFY_PID=$!; PIDS="$PIDS $WEBSOCKIFY_PID"
BROWSER_WORKER_PORT=3002 node /app/browser_worker.js >/tmp/browser-worker.log 2>&1 & WORKER_PID=$!; PIDS="$PIDS $WORKER_PID"
for n in $(seq 1 30); do node -e 'fetch("http://127.0.0.1:3002/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))' && break; sleep 1; done
node -e 'fetch("http://127.0.0.1:3002/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))' || { cat /tmp/browser-worker.log >&2; exit 1; }
BROWSER_WORKER_PORT=3001 BROWSER_WORKER_UPSTREAM_PORT=3002 node /app/worker_guard.js >/tmp/worker-guard.log 2>&1 & GUARD_PID=$!; PIDS="$PIDS $GUARD_PID"
for n in $(seq 1 15); do node -e 'fetch("http://127.0.0.1:3001/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))' && break; sleep 1; done
node -e 'fetch("http://127.0.0.1:3001/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))' || { cat /tmp/worker-guard.log >&2; exit 1; }
node /app/gateway.js >/tmp/gateway.log 2>&1 & GATEWAY_PID=$!; PIDS="$PIDS $GATEWAY_PID"
sleep 1
node -e 'fetch("http://127.0.0.1:"+(process.env.PORT||"8080")+"/health").then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))' || { cat /tmp/gateway.log >&2; exit 1; }
/app/tg_bot >/tmp/tg-bot.log 2>&1 & BOT_PID=$!; PIDS="$PIDS $BOT_PID"
while :; do
  for pid in $XVFB_PID $OPENBOX_PID $VNC_PID $WEBSOCKIFY_PID $WORKER_PID $GUARD_PID $GATEWAY_PID $BOT_PID; do
    if ! kill -0 "$pid" 2>/dev/null; then
      echo "Required process $pid stopped; terminating service." >&2
      cat /tmp/browser-worker.log /tmp/worker-guard.log /tmp/gateway.log /tmp/tg-bot.log 2>/dev/null | tail -n 200 >&2 || true
      exit 1
    fi
  done
  sleep 5
done
