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
