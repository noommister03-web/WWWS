# Telegram CRM Bot

Telegram-бот на C++20 для обработки входящих сообщений, хранения истории в SQLite и генерации ответов через OpenAI-compatible API.

## Возможности

- Telegram Bot API через long polling
- SQLite для хранения истории
- AI-ответы с учётом истории диалога
- Команды `/start`, `/help`, `/status`, `/whatsapp`
- Защита от повторной обработки Telegram updates
- Persistent SQLite database
- Запуск в Docker
- Подходит для Railway

## Структура

```text
telegram-crm/
├── CMakeLists.txt
├── Dockerfile
├── .dockerignore
├── .gitignore
├── .env.example
├── README.md
├── main.cpp
├── Config.hpp
├── Config.cpp
├── Database.hpp
├── Database.cpp
├── TelegramBot.hpp
├── TelegramBot.cpp
├── AiEngine.hpp
└── AiEngine.cpp

## CustoJusto browser worker in Railway

The Telegram service and the Playwright worker run as **two Railway services** from this repository.

1. Create a second service from the same GitHub repository and set its Dockerfile path to `Dockerfile.worker`.
2. Add a persistent Railway Volume mounted at `/app/data` on the worker service. This keeps a separate persistent browser profile at `/app/data/custojusto/profiles/<account-id>` for every CustoJusto account.
3. On the worker set `BROWSER_WORKER_SHARED_SECRET` to a long random value. Use the same value on the Telegram service.
4. On the Telegram service set `BROWSER_WORKER_URL` to the worker's Railway private domain, for example `http://custojusto-worker.railway.internal:3001`.
5. Keep `DB_PATH=/app/data/bot.sqlite3` on the Telegram service and attach its own persistent `/app/data` Volume.

The worker exposes `/health` without authentication for Railway health checks. All operational endpoints require `BROWSER_WORKER_SHARED_SECRET`.
