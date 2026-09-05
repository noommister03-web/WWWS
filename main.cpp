#include "AiEngine.hpp"
#include "Config.hpp"
#include "Database.hpp"
#include "TelegramBot.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void signalHandler(int) {
    g_stop = true;
}

std::string makeWhatsAppLink(
    const std::string& number
) {
    std::string digits;

    for (char c : number) {
        if (c >= '0' && c <= '9') {
            digits += c;
        }
    }

    if (digits.empty()) {
        return "";
    }

    return "https://wa.me/" + digits;
}

bool startsWith(
    const std::string& text,
    const std::string& prefix
) {
    return text.rfind(prefix, 0) == 0;
}

std::string getEnv(
    const char* name
) {
    const char* value =
        std::getenv(name);

    if (!value) {
        return "";
    }

    return value;
}

bool isOwner(
    const IncomingMessage& message,
    const std::string& ownerId
) {
    if (ownerId.empty()) {
        return false;
    }

    return message.senderId == ownerId;
}

bool isOwner(
    const CallbackQuery& callback,
    const std::string& ownerId
) {
    if (ownerId.empty()) {
        return false;
    }

    return std::to_string(
        callback.senderId
    ) == ownerId;
}

using Keyboard =
    std::vector<
        std::vector<
            std::pair<
                std::string,
                std::string
            >
        >
    >;

Keyboard mainMenu() {
    return {
        {
            {"💬 Диалоги", "menu_dialogues"},
            {"🇵🇹 CustoJusto", "menu_custojusto"}
        },
        {
            {"🤖 AI", "menu_ai"},
            {"👨‍💼 Оператор", "menu_operator"}
        },
        {
            {"⚙️ Настройки", "menu_settings"}
        }
    };
}

Keyboard custoJustoMenu() {
    return {
        {
            {
                "➕ Добавить аккаунт",
                "cj_add"
            }
        },
        {
            {
                "📋 Мои аккаунты",
                "cj_accounts"
            }
        },
        {
            {
                "📤 Написать по объявлению",
                "cj_write"
            }
        },
        {
            {
                "💬 Диалоги",
                "cj_dialogues"
            }
        },
        {
            {
                "⬅️ Назад",
                "menu_main"
            }
        }
    };
}

Keyboard accountMenu(
    long long accountId
) {
    const std::string id =
        std::to_string(accountId);

    return {
        {
            {
                "📤 Написать по объявлению",
                "cj_write:" + id
            }
        },
        {
            {
                "💬 Диалоги",
                "cj_dialogues:" + id
            }
        },
        {
            {
                "📋 Объявления",
                "cj_ads:" + id
            }
        },
        {
            {
                "🗑 Удалить аккаунт",
                "cj_delete:" + id
            }
        },
        {
            {
                "⬅️ Назад",
                "cj_accounts"
            }
        }
    };
}

Keyboard accountListKeyboard(
    const std::vector<
        CustoJustoAccount
    >& accounts
) {
    Keyboard keyboard;

    for (const auto& account : accounts) {
        std::string title =
            "🇵🇹 " +
            account.name;

        if (!account.enabled) {
            title += " ⛔";
        }

        keyboard.push_back(
            {
                {
                    title,
                    "cj_account:" +
                    std::to_string(account.id)
                }
            }
        );
    }

    keyboard.push_back(
        {
            {
                "➕ Добавить аккаунт",
                "cj_add"
            }
        }
    );

    keyboard.push_back(
        {
            {
                "⬅️ Назад",
                "menu_custojusto"
            }
        }
    );

    return keyboard;
}

std::string accountListText(
    const std::vector<
        CustoJustoAccount
    >& accounts
) {
    if (accounts.empty()) {
        return
            "🇵🇹 CustoJusto\n\n"
            "Аккаунтов пока нет.\n\n"
            "Нажми «➕ Добавить аккаунт», "
            "чтобы создать первый профиль.";
    }

    std::string text =
        "🇵🇹 Мои аккаунты\n\n";

    for (const auto& account : accounts) {
        text +=
            "• " +
            account.name;

        if (!account.email.empty()) {
            text +=
                "\n  " +
                account.email;
        }

        text +=
            "\n  Статус: " +
            std::string(
                account.enabled
                    ? "активен"
                    : "отключён"
            );

        text += "\n\n";
    }

    text +=
        "Выбери аккаунт ниже.";

    return text;
}

std::string accountText(
    const CustoJustoAccount& account
) {
    std::string text =
        "🇵🇹 CustoJusto\n\n";

    text +=
        "Аккаунт: " +
        account.name +
        "\n";

    if (!account.email.empty()) {
        text +=
            "Email: " +
            account.email +
            "\n";
    }

    text +=
        "Статус: " +
        std::string(
            account.enabled
                ? "активен"
                : "отключён"
        ) +
        "\n\n";

    text +=
        "Выбери действие:";

    return text;
}

}

int main() {
    try {
        std::signal(
            SIGINT,
            signalHandler
        );

        std::signal(
            SIGTERM,
            signalHandler
        );

        const Config config =
            Config::load();

        Database database(
            config.dbPath
        );

        AiEngine ai(
            config.aiApiKey,
            config.aiBaseUrl,
            config.aiModel,
            config.aiSystemPrompt,
            config.aiTimeout
        );

        TelegramBot bot(
            config.telegramToken,
            config.telegramPollTimeout,
            config.privateChatsOnly
        );

        const std::string ownerId =
            getEnv(
                "OWNER_TELEGRAM_ID"
            );

        if (ownerId.empty()) {
            std::cerr
                << "Warning: "
                   "OWNER_TELEGRAM_ID is not configured."
                << std::endl;
        }

        /*
         * CALLBACKS
         */
        bot.setCallbackHandler(
            [&](const CallbackQuery& callback) -> bool {
                try {
                    if (!isOwner(
                            callback,
                            ownerId
                        )) {
                        bot.sendMessage(
                            callback.chatId,
                            "⛔ Доступ запрещён."
                        );

                        return true;
                    }

                    const std::string& data =
                        callback.data;

                    if (data == "menu_main") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "🤖 CRM\n\n"
                            "Главное меню:",
                            mainMenu()
                        );

                        return true;
                    }

                    if (data == "menu_dialogues") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "💬 Диалоги\n\n"
                            "Раздел диалогов пока пуст.\n\n"
                            "Здесь будут отображаться "
                            "входящие обращения и переписки.",
                            {
                                {
                                    {
                                        "⬅️ Назад",
                                        "menu_main"
                                    }
                                }
                            }
                        );

                        return true;
                    }

                    if (data == "menu_custojusto") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "🇵🇹 CustoJusto\n\n"
                            "Управление аккаунтами "
                            "и диалогами:",
                            custoJustoMenu()
                        );

                        return true;
                    }

                    if (data == "menu_ai") {
                        std::string text =
                            "🤖 AI\n\n"
                            "Статус: ";

                        text +=
                            ai.enabled()
                                ? "включён"
                                : "выключен";

                        text +=
                            "\n\n"
                            "AI используется для "
                            "обработки сообщений и "
                            "подготовки ответов.";

                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            text,
                            {
                                {
                                    {
                                        "⬅️ Назад",
                                        "menu_main"
                                    }
                                }
                            }
                        );

                        return true;
                    }

                    if (data == "menu_operator") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "👨‍💼 Оператор\n\n"
                            "Режим оператора:\n"
                            "готов к ручной обработке "
                            "диалогов.",
                            {
                                {
                                    {
                                        "💬 Диалоги",
                                        "menu_dialogues"
                                    }
                                },
                                {
                                    {
                                        "⬅️ Назад",
                                        "menu_main"
                                    }
                                }
                            }
                        );

                        return true;
                    }

                    if (data == "menu_settings") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "⚙️ Настройки\n\n"
                            "Основные настройки CRM "
                            "будут добавлены следующим этапом.",
                            {
                                {
                                    {
                                        "⬅️ Назад",
                                        "menu_main"
                                    }
                                }
                            }
                        );

                        return true;
                    }

                    if (data == "cj_add") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "➕ Добавление аккаунта\n\n"
                            "Сейчас введи данные "
                            "для создания профиля.\n\n"
                            "Формат:\n"
                            "Название аккаунта | Email\n\n"
                            "Например:\n"
                            "Основной | example@email.com\n\n"
                            "На этом этапе пароль "
                            "не запрашивается.",
                            {
                                {
                                    {
                                        "⬅️ Назад",
                                        "menu_custojusto"
                                    }
                                }
                            }
                        );

                        return true;
                    }

                    if (data == "cj_accounts") {
                        const auto accounts =
                            database
                                .getCustoJustoAccounts();

                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            accountListText(
                                accounts
                            ),
                            accountListKeyboard(
                                accounts
                            )
                        );

                        return true;
                    }

                    if (data == "cj_write") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "📤 Написать по объявлению\n\n"
                            "Сначала выбери аккаунт CustoJusto.",
                            accountListKeyboard(
                                database
                                    .getCustoJustoAccounts()
                            )
                        );

                        return true;
                    }

                    if (data == "cj_dialogues") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "💬 Диалоги CustoJusto\n\n"
                            "Здесь будут отображаться "
                            "диалоги с привязкой к "
                            "конкретному аккаунту.",
                            {
                                {
                                    {
                                        "📋 Мои аккаунты",
                                        "cj_accounts"
                                    }
                                },
                                {
                                    {
                                        "⬅️ Назад",
                                        "menu_custojusto"
                                    }
                                }
                            }
                        );

                        return true;
                    }

                    if (
                        data.rfind(
                            "cj_account:",
                            0
                        ) == 0
                    ) {
                        const long long accountId =
                            std::stoll(
                                data.substr(
                                    std::string(
                                        "cj_account:"
                                    ).size()
                                )
                            );

                        const auto account =
                            database
                                .getCustoJustoAccount(
                                    accountId
                                );

                        if (!account.has_value()) {
                            bot.sendMessage(
                                callback.chatId,
                                "❌ Аккаунт не найден."
                            );

                            return true;
                        }

                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            accountText(
                                *account
                            ),
                            accountMenu(
                                accountId
                            )
                        );

                        return true;
                    }

                    if (
                        data.rfind(
                            "cj_delete:",
                            0
                        ) == 0
                    ) {
                        const long long accountId =
                            std::stoll(
                                data.substr(
                                    std::string(
                                        "cj_delete:"
                                    ).size()
                                )
                            );

                        const auto account =
                            database
                                .getCustoJustoAccount(
                                    accountId
                                );

                        if (!account.has_value()) {
                            bot.sendMessage(
                                callback.chatId,
                                "❌ Аккаунт не найден."
                            );

                            return true;
                        }

                        const bool deleted =
                            database
                                .deleteCustoJustoAccount(
                                    accountId
                                );

                        if (deleted) {
                            bot.sendMessageWithKeyboard(
                                callback.chatId,
                                "🗑 Аккаунт «" +
                                account->name +
                                "» удалён.",
                                {
                                    {
                                        {
                                            "📋 Мои аккаунты",
                                            "cj_accounts"
                                        }
                                    },
                                    {
                                        {
                                            "🇵🇹 CustoJusto",
                                            "menu_custojusto"
                                        }
                                    }
                                }
                            );
                        }
                        else {
                            bot.sendMessage(
                                callback.chatId,
                                "❌ Не удалось удалить аккаунт."
                            );
                        }

                        return true;
                    }

                    if (
                        data.rfind(
                            "cj_write:",
                            0
                        ) == 0
                    ) {
                        const long long accountId =
                            std::stoll(
                                data.substr(
                                    std::string(
                                        "cj_write:"
                                    ).size()
                                )
                            );

                        const auto account =
                            database
                                .getCustoJustoAccount(
                                    accountId
                                );

                        if (!account.has_value()) {
                            bot.sendMessage(
                                callback.chatId,
                                "❌ Аккаунт не найден."
                            );

                            return true;
                        }

                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "📤 Написать по объявлению\n\n"
                            "Выбран аккаунт:\n"
                            "🇵🇹 " +
                            account->name +
                            "\n\n"
                            "Следующим этапом здесь "
                            "будет ввод ссылки или ID "
                            "объявления.",
                            {
                                {
                                    {
                                        "⬅️ Назад",
                                        "cj_account:" +
                                        std::to_string(
                                            accountId
                                        )
                                    }
                                }
                            }
                        );

                        return true;
                    }

                    if (
                        data.rfind(
                            "cj_dialogues:",
                            0
                        ) == 0
                    ) {
                        const long long accountId =
                            std::stoll(
                                data.substr(
                                    std::string(
                                        "cj_dialogues:"
                                    ).size()
                                )
                            );

                        const auto account =
                            database
                                .getCustoJustoAccount(
                                    accountId
                                );

                        if (!account.has_value()) {
                            bot.sendMessage(
                                callback.chatId,
                                "❌ Аккаунт не найден."
                            );

                            return true;
                        }

                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "💬 Диалоги\n\n"
                            "Аккаунт:\n"
                            "🇵🇹 " +
                            account->name +
                            "\n\n"
                            "Диалогов пока нет.",
                            {
                                {
                                    {
                                        "⬅️ Назад",
                                        "cj_account:" +
                                        std::to_string(
                                            accountId
                                        )
                                    }
                                }
                            }
                        );

                        return true;
                    }

                    if (
                        data.rfind(
                            "cj_ads:",
                            0
                        ) == 0
                    ) {
                        const long long accountId =
                            std::stoll(
                                data.substr(
                                    std::string(
                                        "cj_ads:"
                                    ).size()
                                )
                            );

                        const auto account =
                            database
                                .getCustoJustoAccount(
                                    accountId
                                );

                        if (!account.has_value()) {
                            bot.sendMessage(
                                callback.chatId,
                                "❌ Аккаунт не найден."
                            );

                            return true;
                        }

                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "📋 Объявления\n\n"
                            "Аккаунт:\n"
                            "🇵🇹 " +
                            account->name +
                            "\n\n"
                            "Список объявлений "
                            "будет подключён "
                            "на следующем этапе.",
                            {
                                {
                                    {
                                        "⬅️ Назад",
                                        "cj_account:" +
                                        std::to_string(
                                            accountId
                                        )
                                    }
                                }
                            }
                        );

                        return true;
                    }

                    bot.sendMessage(
                        callback.chatId,
                        "Неизвестная кнопка."
                    );

                    return true;
                }
                catch (
                    const std::exception& error
                ) {
                    std::cerr
                        << "Callback handler error: "
                        << error.what()
                        << std::endl;

                    bot.sendMessage(
                        callback.chatId,
                        "❌ Произошла ошибка."
                    );

                    return false;
                }
            }
        );

        /*
         * MESSAGES
         */
        bot.setMessageHandler(
            [&](const IncomingMessage& incoming) -> bool {
                try {
                    if (
                        database.isUpdateProcessed(
                            incoming.updateId
                        )
                    ) {
                        return true;
                    }

                    /*
                     * OWNER CHECK
                     */
                    if (!isOwner(
                            incoming,
                            ownerId
                        )) {
                        bot.sendMessage(
                            incoming.chatId,
                            "⛔ Доступ запрещён."
                        );

                        database.markUpdateProcessed(
                            incoming.updateId
                        );

                        return true;
                    }

                    const std::string& text =
                        incoming.text;

                    std::cout
                        << "Incoming message from "
                        << incoming.chatId
                        << ": "
                        << text
                        << std::endl;

                    /*
                     * CRM MENU
                     */
                    if (text == "/start") {
                        bot.sendMessageWithKeyboard(
                            incoming.chatId,
                            "🤖 CRM\n\n"
                            "Добро пожаловать в "
                            "панель управления.",
                            mainMenu()
                        );

                        database.markUpdateProcessed(
                            incoming.updateId
                        );

                        return true;
                    }

                    if (text == "/menu") {
                        bot.sendMessageWithKeyboard(
                            incoming.chatId,
                            "🤖 CRM\n\n"
                            "Главное меню:",
                            mainMenu()
                        );

                        database.markUpdateProcessed(
                            incoming.updateId
                        );

                        return true;
                    }

                    if (text == "/help") {
                        bot.sendMessageWithKeyboard(
                            incoming.chatId,
                            "Доступные команды:\n\n"
                            "/start — открыть CRM\n"
                            "/menu — главное меню\n"
                            "/help — помощь\n"
                            "/status — состояние системы\n"
                            "/whatsapp — WhatsApp",
                           
