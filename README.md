# Telegram CRM Bot with CustoJusto browser sessions

The Railway service runs the Telegram bot, a Playwright Chromium worker, and a protected mobile noVNC gateway in one service. The SQLite database and browser profiles must be stored on a Railway Volume mounted at `/app/data`.

## Required Railway variables

```text
DB_PATH=/app/data/bot.sqlite3
BROWSER_WORKER_URL=http://127.0.0.1:3001
BROWSER_WORKER_SHARED_SECRET=<long-random-secret>
CJ_PROFILE_ROOT=/app/data/custojusto/profiles
REMOTE_BROWSER_URL=https://<your-current-railway-domain>
REMOTE_BROWSER_PASSWORD=<one-strong-password>
```

Keep the existing `TG_BOT_TOKEN`, `OWNER_TELEGRAM_ID`, `AI_API_KEY`, `AI_BASE_URL`, and `AI_MODEL` variables. Do not set `CJ_MANUAL_ACCOUNT_ID`: the Telegram button selects the correct account automatically.

## One-time CustoJusto login

1. Deploy the service and confirm Railway reports `Success`.
2. In Telegram, open the required CustoJusto account and press **«Войти в CustoJusto»**. Always use the newly generated link; do not reopen an old Safari tab.
3. Safari asks once for the browser gateway credentials:
   - username: `custo`
   - password: the full value of `REMOTE_BROWSER_PASSWORD`
4. The page then opens noVNC directly, without a second password screen. Use Chromium inside noVNC to sign in or register at CustoJusto and complete any CAPTCHA yourself.
5. The browser profile is automatically saved at `/app/data/custojusto/profiles/<account-id>` and survives redeploys when `/app/data` is a Railway Volume.

## Security model

- `REMOTE_BROWSER_PASSWORD` is used once by the gateway link and is never sent to CustoJusto.
- The worker API stays on `127.0.0.1`; it is not publicly reachable.
- noVNC and its WebSocket connection use a secure, short-lived browser session cookie created after the one gateway login. This avoids the repeated mobile Safari password prompt.
- Use a unique strong value for `REMOTE_BROWSER_PASSWORD`; never reuse a Telegram, GitHub, or CustoJusto password.
