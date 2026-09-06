#include "Config.hpp"
#include "Database.hpp"
#include "TelegramBot.hpp"
#include "AiEngine.hpp"
#include "CustoJustoClient.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

bool looksLikeEmail(const std::string& value) {
    const auto at = value.find('@');
    const auto dot = value.rfind('.');

    return at != std::string::npos &&
           dot != std::string::npos &&
           at > 0 &&
           dot > at + 1 &&
           dot + 1 < value.size();
}

bool looksLikeUrl(const std::string& value) {
    return value.rfind("http://", 0) == 0 ||
           value.rfind("https://", 0) == 0;
}

std::string accountStatus(
    const CustoJustoAccount& account
) {
    if (account.loggedIn) {
        return "🟢 Вошёл";
    }

    if (!account.enabled) {
        return "⏸ Приостановлен";
    }

    return "🔴 Не вошёл";
}

} // namespace

int main() {
    try {
        Config config;

        const char* token =
            std::getenv("TG_BOT_TOKEN");

        const char* ownerId =
            std::getenv("OWNER_TELEGRAM_ID");

        const char* aiKey =
            std::getenv("AI_API_KEY");

        const char* aiBaseUrl =
            std::getenv("AI_BASE_URL");

        const char* aiModel =
            std::getenv("AI_MODEL");

        if (token == nullptr || *token == '\0') {
            std::cerr
                << "TG_BOT_TOKEN is missing\n";
            return 1;
        }

        if (ownerId == nullptr || *ownerId == '\0') {
            std::cerr
                << "OWNER_TELEGRAM_ID is missing\n";
            return 1;
        }

        const long long ownerTelegramId =
            std::stoll(ownerId);

        Database db("crm.sqlite3");

        TelegramBot bot(
            token,
            30,
            true
        );

        AiEngine ai(
            aiKey != nullptr ? aiKey : "",
            aiBaseUrl != nullptr
                ? aiBaseUrl
                : "https://openrouter.ai/api/v1",
            aiModel != nullptr
                ? aiModel
                : "openrouter/free"
        );

        /*
         * Состояния добавления аккаунта.
         *
         * 0 = ничего не ждём
         * 1 = ждём название
         * 2 = ждём email
         * 3 = ждём ссылку
         * 4 = ждём пароль
         */
        std::unordered_map<long long, int>
            addState;

        std::unordered_map<long long, std::string>
            pendingName;

        std::unordered_map<long long, std::string>
            pendingEmail;

        std::unordered_map<long long, std::string>
            pendingLoginUrl;

        /*
         * Клиенты CustoJusto.
         *
         * Один accountId = одна отдельная сессия.
         */
        std::unordered_map<
            long long,
            std::unique_ptr<CustoJustoClient>
        > custoClients;

        auto getClient =
            [&](long long accountId)
            -> CustoJustoClient* {

                auto it =
                    custoClients.find(accountId);

                if (it != custoClients.end()) {
                    return it->second.get();
                }

                auto client =
                    std::make_unique<CustoJustoClient>();

                client->setAccountId(accountId);

                client->setBaseUrl(
                    "https://www.custojusto.pt"
                );

                auto* result = client.get();

                custoClients.emplace(
                    accountId,
                    std::move(client)
                );

                return result;
            };

        auto accountsKeyboard =
            [&]() {

                std::vector<
                    std::vector<
                        std::pair<std::string, std::string>
                    >
                > keyboard;

                keyboard.push_back({
                    {"➕ Добавить аккаунт", "cj_add"}
                });

                const auto accounts =
                    db.getCustoJustoAccounts();

                for (const auto& account : accounts) {
                    keyboard.push_back({
                        {
                            account.name,
                            "cj_account:" +
                            std::to_string(account.id)
                        }
                    });
                }

                return keyboard;
            };

        auto accountKeyboard =
            [&](long long id) {

                return std::vector<
                    std::vector<
                        std::pair<std::string, std::string>
                    >
                >{
                    {
                        {
                            "🔄 Проверить вход",
                            "cj_check:" +
                            std::to_string(id)
                        }
                    },
                    {
                        {
                            "📤 Написать по объявлению",
                            "cj_write:" +
                            std::to_string(id)
                        }
                    },
                    {
                        {
                            "💬 Диалоги",
                            "cj_dialogs:" +
                            std::to_string(id)
                        }
                    },
                    {
                        {
                            "📋 Объявления",
                            "cj_ads:" +
                            std::to_string(id)
                        }
                    },
                    {
                        {
                            "🗑 Удалить аккаунт",
                            "cj_delete:" +
                            std::to_string(id)
                        }
                    },
                    {
                        {
                            "⬅️ Назад",
                            "cj_accounts"
                        }
                    }
                };
            };

        bot.setCallbackHandler(
            [&](const CallbackQuery& callback)
                -> bool {

                if (
                    callback.senderId !=
                    ownerTelegramId
                ) {
                    bot.answerCallbackQuery(
                        callback.id
                    );

                    bot.sendMessage(
                        callback.chatId,
                        "⛔ Нет доступа."
                    );

                    return true;
                }

                const std::string data =
                    callback.data;

                bot.answerCallbackQuery(
                    callback.id
                );

                if (data == "cj_add") {
                    addState[
                        callback.chatId
                    ] = 1;

                    pendingName.erase(
                        callback.chatId
                    );

                    pendingEmail.erase(
                        callback.chatId
                    );

                    pendingLoginUrl.erase(
                        callback.chatId
                    );

                    bot.sendMessage(
                        callback.chatId,
                        "➕ Добавление CustoJusto\n\n"
                        "Шаг 1 из 3\n"
                        "Отправь название аккаунта."
                    );

                    return true;
                }

                if (data == "cj_accounts") {
                    addState[
                        callback.chatId
                    ] = 0;

                    bot.sendMessageWithKeyboard(
                        callback.chatId,
                        "👥 Аккаунты CustoJusto",
                        accountsKeyboard()
                    );

                    return true;
                }

                if (
                    data.rfind(
                        "cj_account:",
                        0
                    ) == 0
                ) {
                    const long long id =
                        std::stoll(
                            data.substr(11)
                        );

                    const auto account =
                        db.getCustoJustoAccount(id);

                    if (!account) {
                        bot.sendMessage(
                            callback.chatId,
                            "❌ Аккаунт не найден."
                        );

                        return true;
                    }

                    std::string text =
                        "👤 " +
                        account->name +
                        "\n\n";

                    text +=
                        "Email: " +
                        account->email +
                        "\n";

                    text +=
                        "Ссылка: " +
                        account->loginUrl +
                        "\n";

                    text +=
                        "Статус: " +
                        accountStatus(*account);

                    bot.sendMessageWithKeyboard(
                        callback.chatId,
                        text,
                        accountKeyboard(id)
                    );

                    return true;
                }

                if (
                    data.rfind(
                        "cj_check:",
                        0
                    ) == 0
                ) {
                    const long long id =
                        std::stoll(
                            data.substr(9)
                        );

                    const auto account =
                        db.getCustoJustoAccount(id);

                    if (!account) {
                        bot.sendMessage(
                            callback.chatId,
                            "❌ Аккаунт не найден."
                        );

                        return true;
                    }

                    bot.sendMessage(
                        callback.chatId,
                        "🔄 Проверяю сессию "
                        "аккаунта «" +
                        account->name +
                        "»..."
                    );

                    auto* client =
                        getClient(id);

                    client->setBaseUrl(
                        account->loginUrl
                    );

                    const bool logged =
                        client->checkSession();

                    db.setCustoJustoAccountLoggedIn(
                        id,
                        logged
                    );

                    std::string result;

                    if (logged) {
                        result =
                            "🟢 Сессия подтверждена.";
                    } else {
                        result =
                            "🔴 Сессия не подтверждена.\n\n";

                        result +=
                            client->getLastError();
                    }

                    bot.sendMessageWithKeyboard(
                        callback.chatId,
                        result,
                        accountKeyboard(id)
                    );

                    return true;
                }

                if (
                    data.rfind(
                        "cj_write:",
                        0
                    ) == 0
                ) {
                    const long long id =
                        std::stoll(
                            data.substr(9)
                        );

                    const auto account =
                        db.getCustoJustoAccount(id);

                    if (!account) {
                        bot.sendMessage(
                            callback.chatId,
                            "❌ Аккаунт не найден."
                        );

                        return true;
                    }

                    if (!account->loggedIn) {
                        bot.sendMessage(
                            callback.chatId,
                            "🔴 Сначала нужно "
                            "подтвердить вход."
                        );

                        return true;
                    }

                    bot.sendMessage(
                        callback.chatId,
                        "📤 Отправка по объявлению\n\n"
                        "Функция будет подключена "
                        "после завершения модуля "
                        "авторизации."
                    );

                    return true;
                }

                if (
                    data.rfind(
                        "cj_dialogs:",
                        0
                    ) == 0
                ) {
                    bot.sendMessage(
                        callback.chatId,
                        "💬 Диалоги\n\n"
                        "Модуль получения диалогов "
                        "подключим следующим этапом."
                    );

                    return true;
                }

                if (
                    data.rfind(
                        "cj_ads:",
                        0
                    ) == 0
                ) {
                    bot.sendMessage(
                        callback.chatId,
                        "📋 Объявления\n\n"
                        "Модуль объявлений "
                        "подключим следующим этапом."
                    );

                    return true;
                }

                if (
                    data.rfind(
                        "cj_delete:",
                        0
                    ) == 0
                ) {
                    const long long id =
                        std::stoll(
                            data.substr(10)
                        );

                    const auto account =
                        db.getCustoJustoAccount(id);

                    if (!account) {
                        bot.sendMessage(
                            callback.chatId,
                            "❌ Аккаунт не найден."
                        );

                        return true;
                    }

                    db.deleteCustoJustoAccount(id);

                    custoClients.erase(id);

                    bot.sendMessageWithKeyboard(
                        callback.chatId,
                        "🗑 Аккаунт «" +
                        account->name +
                        "» удалён.",
                        accountsKeyboard()
                    );

                    return true;
                }

                return false;
            }
        );

        bot.setMessageHandler(
            [&](const IncomingMessage& message)
                -> bool {

                if (
                    message.chatId !=
                    ownerTelegramId
                ) {
                    bot.sendMessage(
                        message.chatId,
                        "⛔ Нет доступа."
                    );

                    return true;
                }

                const int state =
                    addState[message.chatId];

                /*
                 * ШАГ 1 — название
                 */
                if (state == 1) {
                    if (message.text.empty()) {
                        bot.sendMessage(
                            message.chatId,
                            "❌ Название пустое.\n"
                            "Отправь название ещё раз."
                        );

                        return true;
                    }

                    pendingName[
                        message.chatId
                    ] = message.text;

                    addState[
                        message.chatId
                    ] = 2;

                    bot.sendMessage(
                        message.chatId,
                        "Шаг 2 из 3\n\n"
                        "Теперь отправь email "
                        "этого аккаунта CustoJusto."
                    );

                    return true;
                }

                /*
                 * ШАГ 2 — email
                 */
                if (state == 2) {
                    if (
                        !looksLikeEmail(
                            message.text
                        )
                    ) {
                        bot.sendMessage(
                            message.chatId,
                            "❌ Похоже, это не email.\n\n"
                            "Пример:\n"
                            "name@example.com"
                        );

                        return true;
                    }

                    pendingEmail[
                        message.chatId
                    ] = message.text;

                    addState[
                        message.chatId
                    ] = 3;

                    bot.sendMessage(
                        message.chatId,
                        "Шаг 3 из 3\n\n"
                        "Отправь ссылку CustoJusto."
                    );

                    return true;
                }

                /*
                 * ШАГ 3 — ссылка
                 */
                if (state == 3) {
                    if (
                        !looksLikeUrl(
                            message.text
                        )
                    ) {
                        bot.sendMessage(
                            message.chatId,
                            "❌ Неверная ссылка.\n\n"
                            "Нужна ссылка, начинающаяся "
                            "с http:// или https://."
                        );

                        return true;
                    }

                    const auto name =
                        pendingName[
                            message.chatId
                        ];

                    const auto email =
                        pendingEmail[
                            message.chatId
                        ];

                    const long long id =
                        db.addCustoJustoAccount(
                            name,
                            email,
                            message.text
                        );

                    addState[
                        message.chatId
                    ] = 0;

                    pendingName.erase(
                        message.chatId
                    );

                    pendingEmail.erase(
                        message.chatId
                    );

                    pendingLoginUrl.erase(
                        message.chatId
                    );

                    bot.sendMessageWithKeyboard(
                        message.chatId,
                        "✅ Аккаунт добавлен.\n\n"
                        "Название: " +
                        name +
                        "\n"
                        "Email: " +
                        email +
                        "\n"
                        "ID: " +
                        std::to_string(id) +
                        "\n\n"
                        "Статус: 🔴 Не вошёл",
                        accountKeyboard(id)
                    );

                    return true;
                }

                /*
                 * Обычное сообщение.
                 */
                if (!message.text.empty()) {
                    db.saveMessage(
                        message.chatId,
                        message.senderId,
                        message.username,
                        message.text,
                        true,
                        message.updateId
                    );

                    if (!aiKey ||
                        std::string(aiKey).empty()) {
                        bot.sendMessage(
                            message.chatId,
                            "Сообщение получено."
                        );

                        return true;
                    }

                    const std::string answer =
                        ai.generateReply(
                            message.text
                        );

                    if (!answer.empty()) {
                        bot.sendMessage(
                            message.chatId,
                            answer
                        );
                    }
                }

                return true;
            }
        );

        bot.sendMessageWithKeyboard(
            ownerTelegramId,
            "🤖 CRM запущена.\n\n"
            "Управление аккаунтами CustoJusto:",
            accountsKeyboard()
        );

        std::cout
            << "Telegram CRM started\n";

        bot.run();

    } catch (const std::exception& e) {
        std::cerr
            << "Fatal error: "
            << e.what()
            << '\n';

        return 1;
    }

    return 0;
}
