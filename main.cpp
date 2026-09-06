#include "AiEngine.hpp"
#include "Config.hpp"
#include "Database.hpp"
#include "TelegramBot.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void signalHandler(int) {
    g_stop = true;
}

bool isOwner(
    const std::string& senderId,
    const std::string& ownerId
) {
    return !ownerId.empty() && senderId == ownerId;
}

bool startsWith(
    const std::string& text,
    const std::string& prefix
) {
    return text.rfind(prefix, 0) == 0;
}

std::string trim(std::string value) {
    while (!value.empty() &&
           (value.front() == ' ' ||
            value.front() == '\t' ||
            value.front() == '\n' ||
            value.front() == '\r')) {
        value.erase(value.begin());
    }

    while (!value.empty() &&
           (value.back() == ' ' ||
            value.back() == '\t' ||
            value.back() == '\n' ||
            value.back() == '\r')) {
        value.pop_back();
    }

    return value;
}

bool looksLikeUrl(const std::string& value) {
    return startsWith(value, "https://") ||
           startsWith(value, "http://");
}

std::vector<
    std::vector<
        std::pair<std::string, std::string>
    >
> mainKeyboard() {
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

std::vector<
    std::vector<
        std::pair<std::string, std::string>
    >
> custoJustoKeyboard() {
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

std::vector<
    std::vector<
        std::pair<std::string, std::string>
    >
> accountKeyboard(long long id) {
    const std::string idText =
        std::to_string(id);

    return {
        {
            {"🔄 Проверить вход",
             "cj_check:" + idText}
        },
        {
            {"📤 Написать по объявлению",
             "cj_write:" + idText}
        },
        {
            {"💬 Диалоги",
             "cj_dialogues:" + idText}
        },
        {
            {"📋 Объявления",
             "cj_ads:" + idText}
        },
        {
            {"🗑 Удалить аккаунт",
             "cj_delete:" + idText}
        },
        {
            {"⬅️ Назад",
             "cj_accounts"}
        }
    };
}

}

int main() {
    try {
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        const Config config = Config::load();

        Database database(config.dbPath);

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
            ownerEnv ? ownerEnv : "";

        bot.setStopChecker([] {
            return g_stop.load();
        });

        bot.setCallbackHandler(
            [&](const CallbackQuery& callback) -> bool {
                try {
                    if (!isOwner(
                            std::to_string(callback.senderId),
                            ownerTelegramId)) {

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

                    if (data == "menu_custojusto") {
                        bot.sendMessageWithKeyboard(
                            callback.chatId,
                            "🇵🇹 CustoJusto\n\n"
                            "Выбери действие:",
                            custoJustoKeyboard()
                        );

                        return true;
                    }

                    if (data == "menu_dialogues") {
                        bot.sendMessage(
                            callback.chatId,
                            "💬 Диалоги\n\n"
                            "Раздел готов."
                        );

                        return true;
                    }

                    if (data == "menu_ai") {
                        bot.sendMessage(
                            callback.chatId,
                            ai.enabled()
                                ? "🤖 AI: 🟢 включён"
                                : "🤖 AI: 🔴 выключен"
                        );

                        return true;
                    }

                    if (data == "menu_operator") {
                        bot.sendMessage(
                            callback.chatId,
                            "👨‍💼 Режим оператора готов."
                        );

                        return true;
                    }

                    if (data == "menu_settings") {
                        bot.sendMessage(
                            callback.chatId,
                            "⚙️ Настройки CRM."
                        );

                        return true;
                    }

                    /*
                     * ADD ACCOUNT
                     */

                    if (data == "cj_add") {
                        bot.sendMessage(
                            callback.chatId,
                            "➕ Добавление CustoJusto\n\n"
                            "1. Открой CustoJusto:\n"
                            "https://www.custojusto.pt\n\n"
                            "2. Войди в нужный аккаунт.\n\n"
                            "3. Отправь сюда ссылку, "
                            "которую хочешь привязать.\n\n"
                            "Формат:\n"
                            "Название | Email | Ссылка"
                        );

                        return true;
                    }

                    /*
                     * ACCOUNTS
                     */

                    if (data == "cj_accounts") {
                        const auto accounts =
                            database
                                .getCustoJustoAccounts();

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

                        std::vector<
                            std::vector<
                                std::pair<
                                    std::string,
                                    std::string
                                >
                            >
                        > buttons;

                        for (const auto& account : accounts) {
                            text +=
                                "🇵🇹 " +
                                account.name +
                                "\n";

                            if (!account.email.empty()) {
                                text +=
                                    "📧 " +
                                    account.email +
                                    "\n";
                            }

                            text +=
                                account.loggedIn
                                    ? "🟢 Вошёл\n"
                                    : "🔴 Не вошёл";

                            if (!account.enabled) {
