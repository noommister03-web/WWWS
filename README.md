# Telegram CRM Bot with CustoJusto bridge

The service runs the C++ Telegram bot and a local Playwright worker in one Railway service. SQLite and CustoJusto profiles must live on a Railway Volume mounted at `/app/data`.

## Required Railway variables

```text
DB_PATH=/app/data/bot.sqlite3
BROWSER_WORKER_URL=http://127.0.0.1:3001
BROWSER_WORKER_SHARED_SECRET=<long-random-secret>
CJ_PROFILE_ROOT=/app/data/custojusto/profiles
REMOTE_BROWSER_PASSWORD=<new-strong-password>
CJ_MANUAL_ACCOUNT_ID=1
```

Keep the existing `TG_BOT_TOKEN`, `OWNER_TELEGRAM_ID`, `AI_API_KEY`, `AI_BASE_URL`, and `AI_MODEL` variables.

## One-time manual CustoJusto login

CustoJusto requires Cloudflare Turnstile, so passwords are not sent through Telegram or automatically submitted by the service.

1. Deploy the service and generate a Railway public domain for it.
2. Open `https://<your-railway-domain>/vnc.html?autoconnect=true&resize=remote`.
3. Browser authentication is required. Use username `custo` and the value of `REMOTE_BROWSER_PASSWORD`.
4. The noVNC page shows a real Chromium browser. Complete the CustoJusto login and Cloudflare check yourself.
5. Leave the session running. Cookies and browser profile are stored at `/app/data/custojusto/profiles/<CJ_MANUAL_ACCOUNT_ID>` and survive deployments.

For a different CustoJusto account, set `CJ_MANUAL_ACCOUNT_ID` to that account's ID in the Telegram bot, deploy once, complete login in noVNC, then change it back only when you need to connect another account. The CustoJusto bridge uses the same persistent account profile.

## Security

- The worker API is bound to `127.0.0.1`; it is never exposed publicly.
- The only public endpoint is the password-protected noVNC screen.
- Use a new unique value for `REMOTE_BROWSER_PASSWORD`; do not reuse a Telegram or CustoJusto password.
- Do not put secrets in this repository.
