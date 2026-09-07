# Telegram CRM Bot with CustoJusto bridge

The service runs the C++ Telegram bot and a local Playwright worker in one Railway service. SQLite and CustoJusto profiles must live on a Railway Volume mounted at `/app/data`.

## Required Railway variables
to bridge

The service runs the C++ Telegram bot and a local Playwright worker in one Railway service. SQLite and CustoJusto profiles must live on a Railway Volume mounted at `/app/data`.

## Required Railway varia
Keep the existing with CustoJusto# Telegram CRM Bot wi# Telegram CRM# Telegram CRM  andlegram CRM Bvariables.

## One-time manual CustoJusto login

CustoJusto requires Cloudflare Turnstile, so passwords are not sent through Telegram or automatically submitted by the service.

1. Deploy the service and generate a Railway public domain for it.
2. On a computer, openH=/app/data/bot.sqlite3

BROWSER_WORKER_URL=http://127.0.0.1:3001

BROWSE
3. On a phone, openith CustoJusto bridge

The service runs  The remote desktop will fit your phone screen.
4. Browser authentication is required. Use usernameight workand the value ofot with CustoJusto bridge


5. The noVNC page shows a real Chromium browser. Complete the CustoJusto login and Cloudflare check yourself.
6. On a phone, use the noVNC side panel for touch controls and the on-screen keyboard.
7. Leave the session running. Cookies and browser profile are stored at/app/data/bot.sqlite3

BROWSER_WORKER_URL=http://127.0.0and survive deployments.

For a different CustoJusto account, set++ Telegram bot and a loto that account's ID in the Telegram bot, deploy once, complete login in noVNC, then change it back only when you need to connect another account. The CustoJusto bridge uses the same persistent account profile.

## Security

- The worker API is bound to``text

DB_PA it is never exposed publicly.
- The public noVNC screen and theC++ Telegrashortcut are protected with the same password.
- Use a new unique value form bot and a local Playwrigh do not reuse a Telegram or CustoJusto password.
- Do not put secrets in this repository.
