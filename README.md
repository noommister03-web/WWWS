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

## CustoJusto in the existing Railway service

The default `Dockerfile` now runs both components in the **same Railway service**:

- the C++ Telegram CRM process;
- the local Playwright worker at `http://127.0.0.1:3001`.

This means no second Railway service is necessary for the first full end-to-end test. Add one persistent Volume mounted at `/app/data`; it stores the SQLite database and the independent CustoJusto browser profiles in `/app/data/custojusto/profiles/<account-id>`.

Set these Railway variables on the existing service:

```text
BROWSER_WORKER_URL=http://127.0.0.1:3001
BROWSER_WORKER_SHARED_SECRET=<long-random-secret>
CJ_PROFILE_ROOT=/app/data/custojusto/profiles
DB_PATH=/app/data/bot.sqlite3
```

Keep the existing `TG_BOT_TOKEN`, `OWNER_TELEGRAM_ID`, `AI_API_KEY`, `AI_BASE_URL`, and `AI_MODEL` variables. The worker will not start unless `BROWSER_WORKER_SHARED_SECRET` is set.

`Dockerfile.worker` remains available for splitting the browser worker into a separate service later, when needed for scaling.
