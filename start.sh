#!/bin/sh
set -eu

if [ -z "${BROWSER_WORKER_SHARED_SECRET:-}" ]; then
  echo "BROWSER_WORKER_SHARED_SECRET is required" >&2
  exit 1
fi

node /app/browser_worker.js &
worker_pid=$!
trap 'kill "$worker_pid" 2>/dev/null || true; wait "$worker_pid" 2>/dev/null || true' INT TERM EXIT

# Do not start Telegram until the local worker is accepting health checks.
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
