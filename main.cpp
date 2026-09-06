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

bool isOwner(
    const std::string& senderId,
    const std::string& ownerId
) {
    return !ownerId.empty() &&
           senderId == ownerId;
}

std::vector<std::vector<std::pair<std::string, std::string>>>
mainKeyboard() {
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

std::vector<std::vector<std::pair<std::string, std::string>>>
custoJustoKeyboard() {
    return {
        {
            {"➕ Добавить аккаунт", "cj_add"}
        },
        {
            {"📋 Мои аккаунты", "cj_accounts"}
        },
        {
            {"📤 Написать по объявлению", "cj_write"}
        },
        {
            {"💬 Диалоги", "cj_dialogues"}
        },
        {
            {"⬅️ Назад", "menu_main"}
        }
    };
}

std::vector<std::vector<std::pair<std::string, std::string>>>
accountKeyboard(
    long long accountId
) {
    const std::string id =
        std::to_string(accountId);

    return {
        {
            {"📤 Написать по объявлению",
             "cj_write:" + id}
        },
        {
            {"💬 Диалоги",
             "cj_dialogues:" + id}
        },
        {
            {"📋 Объявления",
             "cj_ads:" + id}
        },
        {
            {"🗑 Удалить аккаунт",
             "cj_delete:" + id}
        },
        {
            {"⬅️ Назад", "cj_accounts"}
        }
    };
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

        const char* ownerEnv =
            std::getenv("OWNER_TELEGRAM_ID");

        const std::string ownerTelegramId =
            ownerEnv != nullptr
                ? ownerEnv
                : "";

        bot.setStopChecker([] {
            return g_stop.load();
        });

        bot.setCallbackHandler(
            [&](const CallbackQuery& callback) -> bool {
                try {
                    if (
                        !isOwner(
                            std::to_string(
                                callback.senderId
                            ),
                            ownerTelegramId
                        )
                    ) {
                        bot.answerCallbackQuery(
                            callback.id
                        );

                        bot.sendMessage(
                            callback.chatId,
                            "⛔ Доступ запрещён."
                        );

                        return true;
                    }

                    bot.answerCallbackQuery(
                        callback.id
                    );

                    const std::string& data =
                        callback.data;

                    if (data == "menu_main") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "🤖 CRM\n\n"
                            "Главное меню:",
                            mainKeyboard()
                        );

                        return true;
                    }

                    if (data == "menu_dialogues") {
                        bot.sendMessage(
                            callback.chatId,
                            "💬 Диалоги\n\n"
                            "Здесь будут отображаться "
                            "входящие диалоги."
                        );

                        return true;
                    }

                    if (data == "menu_custojusto") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "🇵🇹 CustoJusto\n\n"
                            "Выбери действие:",
                            custoJustoKeyboard()
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

                        bot.sendMessage(
                            callback.chatId,
                            text
                        );

                        return true;
                    }

                    if (data == "menu_operator") {
                        bot.sendMessage(
                            callback.chatId,
                            "👨‍💼 Оператор\n\n"
                            "Режим оператора готов. "
                            "Здесь можно будет управлять "
                            "передачей диалога человеку."
                        );

                        return true;
                    }

                    if (data == "menu_settings") {
                        bot.sendMessage(
                            callback.chatId,
                            "⚙️ Настройки\n\n"
                            "Основные настройки CRM."
                        );

                        return true;
                    }

                    if (data == "cj_add") {
                        bot.sendMessage(
                            callback.chatId,
                            "➕ Добавление аккаунта\n\n"
                            "Отправь название и email "
                            "в одном сообщении:\n\n"
                            "Название | Email\n\n"
                            "Например:\n"
                            "CustoJusto 1 | example@email.com"
                        );

                        return true;
                    }

                    if (data == "cj_accounts") {
                        const auto accounts =
                            database.getCustoJustoAccounts();

                        if (accounts.empty()) {
                            bot.sendMessageWithKeyboard(
                                callback.chatId,
                                "📋 Мои аккаунты\n\n"
                                "Аккаунтов пока нет.",
                                custoJustoKeyboard()
                            );

                            return true;
                        }

                        std::string text =
                            "📋 Мои аккаунты\n\n";

                        for (
                            const auto& account :
                            accounts
                        ) {
                            text +=
                                "🇵🇹 " +
                                account.name +
                                "\n";

                            if (
                                !account.email.empty()
                            ) {
                                text +=
                                    "📧 " +
                                    account.email +
                                    "\n";
                            }

                            text +=
                                account.enabled
                                    ? "🟢 Включён\n\n"
                                    : "🔴 Выключен\n\n";
                        }

                        std::vector<
                            std::vector<
                                std::pair<
                                    std::string,
                                    std::string
                                >
                            >
                        > buttons;

                        for (
                            const auto& account :
                            accounts
                        ) {
                            buttons.push_back(
                                {
                                    {
                                        "🇵🇹 " +
                                        account.name,
                                        "cj_account:" +
                                        std::to_string(
                                            account.id
                                        )
                                    }
                                }
                            );
                        }

                        buttons.push_back(
                            {
                                {
                                    "⬅️ Назад",
                                    "menu_custojusto"
                                }
                            }
                        );

                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            text,
                            buttons
                        );

                        return true;
                    }

                    if (data == "cj_write") {
                        bot.sendMessage(
                            callback.chatId,
                            "📤 Написать по объявлению\n\n"
                            "Сначала выбери аккаунт, "
                            "через который будет идти "
                            "диалог."
                        );

                        return true;
                    }

                    if (data == "cj_dialogues") {
                        bot.sendMessage(
                            callback.chatId,
                            "💬 Диалоги CustoJusto\n\n"
                            "Диалоги появятся здесь "
                            "после подключения разрешённого "
                            "канала обмена сообщениями."
                        );

                        return true;
                    }

                    if (startsWith(data, "cj_account:")) {
                        const std::string idText =
                            data.substr(
                                std::string(
                                    "cj_account:"
                                ).size()
                            );

                        try {
                            const long long id =
                                std::stoll(idText);

                            const auto account =
                                database
                                    .getCustoJustoAccount(
                                        id
                                    );

                            if (!account) {
                                bot.sendMessage(
                                    callback.chatId,
                                    "❌ Аккаунт не найден."
                                );

                                return true;
                            }

                            std::string text =
                                "🇵🇹 " +
                                account->name +
                                "\n\n";

                            text +=
                                "Email: " +
                                account->email +
                                "\n";

                            text +=
                                account->enabled
                                    ? "Статус: 🟢 включён"
                                    : "Статус: 🔴 выключен";

                            bot.sendMessageWithKeyboard(
                                callback.chatId,
                                text,
                                accountKeyboard(id)
                            );
                        }
                        catch (...) {
                            bot.sendMessage(
                                callback.chatId,
                                "❌ Некорректный ID аккаунта."
                            );
                        }

                        return true;
                    }

                    if (startsWith(data, "cj_delete:")) {
                        const std::string idText =
                            data.substr(
                                std::string(
                                    "cj_delete:"
                                ).size()
                            );

                        try {
                            const long long id =
                                std::stoll(idText);

                            const bool deleted =
                                database
                                    .deleteCustoJustoAccount(
                                        id
                                    );

                            bot.sendMessageWithKeyboard(
                                callback.chatId,
                                deleted
                                    ? "🗑 Аккаунт удалён."
                                    : "❌ Не удалось удалить "
                                      "аккаунт.",
                                custoJustoKeyboard()
                            );
                        }
                        catch (...) {
                            bot.sendMessage(
                                callback.chatId,
                                "❌ Некорректный ID аккаунта."
                            );
                        }

                        return true;
                    }

                    if (startsWith(data, "cj_write:")) {
                        const std::string idText =
                            data.substr(
                                std::string(
                                    "cj_write:"
                                ).size()
                            );

                        try {
                            const long long id =
                                std::stoll(idText);

                            const auto account =
                                database
                                    .getCustoJustoAccount(
                                        id
                                    );

                            if (!account) {
                                bot.sendMessage(
                                    callback.chatId,
                                    "❌ Аккаунт не найден."
                                );

                                return true;
                            }

                            bot.sendMessage(
                                callback.chatId,
                                "📤 Аккаунт: " +
                                account->name +
                                "\n\n"
                                "Отправь ссылку или ID "
                                "объявления.\n\n"
                                "После этого можно будет "
                                "привязать диалог именно "
                                "к этому аккаунту."
                            );
                        }
                        catch (...) {
                            bot.sendMessage(
                                callback.chatId,
                                "❌ Некорректный ID аккаунта."
                            );
                        }

                        return true;
                    }

                    if (
                        startsWith(
                            data,
                            "cj_dialogues:"
                        )
                    ) {
                        bot.sendMessage(
                            callback.chatId,
                            "💬 Диалоги выбранного "
                            "аккаунта.\n\n"
                            "Пока подключение транспорта "
                            "сообщений не настроено."
                        );

                        return true;
                    }

                    if (
                        startsWith(
                            data,
                            "cj_ads:"
                        )
                    ) {
                        bot.sendMessage(
                            callback.chatId,
                            "📋 Объявления\n\n"
                            "Список объявлений появится "
                            "после подключения разрешённого "
                            "источника данных."
                        );

                        return true;
                    }

                    bot.sendMessage(
                        callback.chatId,
                        "Неизвестное действие."
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

                    return false;
                }
            }
        );

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

                    if (
                        !isOwner(
                            incoming.senderId,
                            ownerTelegramId
                        )
                    ) {
                        bot.sendMessage(
                            incoming.chatId,
                            "⛔ Доступ запрещён."
                        );

                        database.markUpdateProcessed(
                            incoming.updateId
                        );

                        return true;
                    }

                    database.saveMessage(
                        incoming.chatId,
                        incoming.senderId,
                        incoming.username,
                        incoming.text,
                        true,
                        incoming.updateId
                    );

                    const std::string& text =
                        incoming.text;

                    std::cout
                        << "Incoming message from "
                        << incoming.chatId
                        << ": "
                        << text
                        << std::endl;

                    std::string reply;

                    if (
                        text == "/start" ||
                        text == "/menu"
                    ) {
                        bot.sendMessageWithKeyboard(
                            incoming.chatId,
                            "🤖 CRM\n\n"
                            "Добро пожаловать. "
                            "Выбери нужный раздел:",
                            mainKeyboard()
                        );

                        database.markUpdateProcessed(
                            incoming.updateId
                        );

                        return true;
                    }

                    if (text == "/help") {
                        reply =
                            "Доступные команды:\n"
                            "/start — открыть CRM\n"
                            "/menu — открыть CRM\n"
                            "/help — помощь\n"
                            "/status — состояние системы\n"
                            "/whatsapp — WhatsApp, если настроен";
                    }
                    else if (text == "/status") {
                        reply =
                            "Система работает.\n"
                            "AI: ";

                        reply +=
                            ai.enabled()
                                ? "включён"
                                : "выключен";
                    }
                    else if (text == "/whatsapp") {
                        const std::string link =
                            makeWhatsAppLink(
                                config.whatsappNumber
                            );

                        if (link.empty()) {
                            reply =
                                "WhatsApp пока не настроен.";
                        }
                        else {
                            reply =
                                "WhatsApp:\n" +
                                link;
                        }
                    }
                    else if (
                        startsWith(
                            text,
                            "/"
                        )
                    ) {
                        reply =
                            "Неизвестная команда. "
                            "Используй /help.";
                    }
                    else if (
                        text.find('|') !=
                        std::string::npos
                    ) {
                        const std::size_t separator =
                            text.find('|');

                        std::string name =
                            text.substr(
                                0,
                                separator
                            );

                        std::string email =
                            text.substr(
                                separator + 1
                            );

                        while (
                            !name.empty() &&
                            name.front() == ' '
                        ) {
                            name.erase(
                                name.begin()
                            );
                        }

                        while (
                            !name.empty() &&
                            name.back() == ' '
                        ) {
                            name.pop_back();
                        }

                        while (
                            !email.empty() &&
                            email.front() == ' '
                        ) {
                            email.erase(
                                email.begin()
                            );
                        }

                        while (
                            !email.empty() &&
                            email.back() == ' '
                        ) {
                            email.pop_back();
                        }

                        if (name.empty()) {
                            reply =
                                "❌ Не указано название "
                                "аккаунта.";
                        }
                        else {
                            const long long id =
                                database
                                    .addCustoJustoAccount(
                                        name,
                                        email
                                    );

                            reply =
                                "✅ Аккаунт добавлен.\n\n"
                                "ID: " +
                                std::to_string(id) +
                                "\n"
                                "Название: " +
                                name;

                            if (!email.empty()) {
                                reply +=
                                    "\nEmail: " +
                                    email;
                            }
                        }
                    }
                    else {
                        if (!ai.enabled()) {
                            reply =
                                "AI сейчас не настроен. "
                                "Сообщение получено.";
                        }
                        else {
                            const auto history =
                                database.getHistory(
                                    incoming.chatId,
                                    config.aiHistoryLimit
                                );

                            try {
                                reply =
                                    ai.generateReply(
                                        history
                                    );
                            }
                            catch (
                                const std::exception& error
                            ) {
                                std::cerr
                                    << "AI error: "
                                    << error.what()
                                    << std::endl;

                                reply =
                                    "Сообщение получено. "
                                    "Оператор сможет ответить позже.";
                            }
                        }
                    }

                    if (reply.empty()) {
                        reply =
                            "Сообщение получено.";
                    }

                    const SendStatus status =
                        bot.sendMessage(
                            incoming.chatId,
                            reply
                        );

                    if (
                        status ==
                        SendStatus::TemporaryFailure
                    ) {
                        return false;
                    }

                    if (
                        status ==
                        SendStatus::Success
                    ) {
                        database.saveMessage(
                            incoming.chatId,
                            "bot",
                            "",
                            reply,
                            false
                        );
                    }

                    database.markUpdateProcessed(
                        incoming.updateId
                    );

                    return true;
                }
                catch (
                    const std::exception& error
                ) {
                    std::cerr
                        << "Message handler error: "
                        << error.what()
                        << std::endl;

                    return false;
                }
            }
        );

        std::cout
            << "Application started."
            << std::endl;

        bot.run();

        std::cout
            << "Application stopped."
            << std::endl;

        return 0;
    }
    catch (
        const std::exception& error
    ) {
        std::cerr
            << "Fatal error: "
            << error.what()
            << std::endl;

        return 1;
    }
}
