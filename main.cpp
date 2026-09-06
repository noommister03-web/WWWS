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

        Database db(config.dbPath);

        TelegramBot bot(
            token,
            config.telegramPollTimeout,
            config.privateChatsOnly
        );

        AiEngine ai(
            config.aiApiKey,
            config.aiBaseUrl,
            config.aiModel,
            config.aiSystemPrompt,
            config.aiTimeout
        );

        std::unordered_map<long long, int>
            addState;

        std::unordered_map<long long, std::string>
            pendingName;

        std::unordered_map<long long, std::string>
            pendingEmail;

        std::unordered_map<long long, std::string>
            pendingLoginUrl;

        std::unordered_map<long long, std::string>
            pendingPassword;

        std::unordered_map<long long, long long>
            pendingListingAccount;

        std::unordered_map<long long, std::string>
            pendingListingUrl;

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
                            "🔐 Войти в аккаунт",
                            "cj_login:" +
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
                    bot.answerCallbackQuery(callback.id);
                    bot.sendMessage(callback.chatId, "⛔ Нет доступа.");
                    return true;
                }

                const std::string data = callback.data;
                bot.answerCallbackQuery(callback.id);

                if (data == "cj_add") {
                    addState[callback.chatId] = 1;
                    pendingName.erase(callback.chatId);
                    pendingEmail.erase(callback.chatId);
                    pendingLoginUrl.erase(callback.chatId);
                    pendingPassword.erase(callback.chatId);
                    pendingListingAccount.erase(callback.chatId);
                    pendingListingUrl.erase(callback.chatId);

                    bot.sendMessage(
                        callback.chatId,
                        "➕ Добавление CustoJusto\n\n"
                        "Шаг 1 из 3\n"
                        "Отправь название аккаунта."
                    );
                    return true;
                }

                if (data == "cj_accounts") {
                    addState[callback.chatId] = 0;
                    bot.sendMessageWithKeyboard(
                        callback.chatId,
                        "👥 Аккаунты CustoJusto",
                        accountsKeyboard()
                    );
                    return true;
                }

                if (data.rfind("cj_account:", 0) == 0) {
                    const long long id = std::stoll(data.substr(11));
                    const auto account = db.getCustoJustoAccount(id);

                    if (!account) {
                        bot.sendMessage(callback.chatId, "❌ Аккаунт не найден.");
                        return true;
                    }

                    std::string text = "👤 " + account->name + "\n\n";
                    text += "Email: " + account->email + "\n";
                    text += "Ссылка: " + account->loginUrl + "\n";
                    text += "Статус: " + accountStatus(*account);

                    bot.sendMessageWithKeyboard(
                        callback.chatId, text, accountKeyboard(id)
                    );
                    return true;
                }

                if (data.rfind("cj_login:", 0) == 0) {
                    const long long id = std::stoll(data.substr(9));
                    const auto account = db.getCustoJustoAccount(id);

                    if (!account) {
                        bot.sendMessage(callback.chatId, "❌ Аккаунт не найден.");
                        return true;
                    }

                    addState[callback.chatId] = 4;
                    pendingListingAccount[callback.chatId] = id;
                    pendingListingUrl.erase(callback.chatId);

                    bot.sendMessage(
                        callback.chatId,
                        "🔐 Вход в «" + account->name + "»\n\n"
                        "Отправь пароль от CustoJusto. Пароль используется только для входа "
                        "в текущую сессию и не сохраняется в базе."
                    );
                    return true;
                }

                if (data.rfind("cj_write:", 0) == 0) {
                    const long long id = std::stoll(data.substr(9));
                    const auto account = db.getCustoJustoAccount(id);

                    if (!account) {
                        bot.sendMessage(callback.chatId, "❌ Аккаунт не найден.");
                        return true;
                    }

                    if (!account->loggedIn) {
                        bot.sendMessage(
                            callback.chatId,
                            "🔐 Сначала войди в аккаунт кнопкой «Войти в аккаунт»."
                        );
                        return true;
                    }

                    addState[callback.chatId] = 5;
                    pendingListingAccount[callback.chatId] = id;
                    pendingListingUrl.erase(callback.chatId);

                    bot.sendMessage(
                        callback.chatId,
                        "📤 Отправка продавцу\n\n"
                        "Пришли ссылку на объявление CustoJusto."
                    );
                    return true;
                }

                if (data.rfind("cj_dialogs:", 0) == 0) {
                    bot.sendMessage(
                        callback.chatId,
                        "💬 Диалоги\n\n"
                        "Модуль получения диалогов будет подключён следующим этапом."
                    );
                    return true;
                }

                if (data.rfind("cj_ads:", 0) == 0) {
                    bot.sendMessage(
                        callback.chatId,
                        "📋 Объявления\n\n"
                        "Модуль объявлений будет подключён следующим этапом."
                    );
                    return true;
                }

                if (data.rfind("cj_delete:", 0) == 0) {
                    const long long id = std::stoll(data.substr(10));
                    const auto account = db.getCustoJustoAccount(id);

                    if (!account) {
                        bot.sendMessage(callback.chatId, "❌ Аккаунт не найден.");
                        return true;
                    }

                    db.deleteCustoJustoAccount(id);
                    custoClients.erase(id);
                    bot.sendMessageWithKeyboard(
                        callback.chatId,
                        "🗑 Аккаунт «" + account->name + "» удалён.",
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

                if (message.chatId != ownerTelegramId) {
                    bot.sendMessage(message.chatId, "⛔ Нет доступа.");
                    return true;
                }

                const int state = addState[message.chatId];

                if (state == 4) {
                    const auto account = db.getCustoJustoAccount(
                        pendingListingAccount[message.chatId]
                    );

                    if (!account || message.text.empty()) {
                        addState[message.chatId] = 0;
                        pendingPassword.erase(message.chatId);
                        bot.sendMessage(message.chatId, "❌ Не удалось получить пароль или аккаунт.");
                        return true;
                    }

                    auto* client = getClient(account->id);
                    client->setBaseUrl(account->loginUrl);
                    const auto login = client->login(account->email, message.text);
                    pendingPassword.erase(message.chatId);
                    addState[message.chatId] = 0;
                    db.setCustoJustoAccountLoggedIn(account->id, login.loggedIn);

                    std::string result = login.loggedIn
                        ? "🟢 Вход выполнен. Сессия сохранена для этого аккаунта."
                        : "🔴 Войти не удалось: " + login.message;

                    if (login.requiresCaptcha || login.requiresTwoFactor) {
                        result += "\n\nОткрой CustoJusto в браузере и пройди проверку; затем нажми вход снова.";
                    }

                    bot.sendMessageWithKeyboard(message.chatId, result, accountKeyboard(account->id));
                    return true;
                }

                if (state == 5) {
                    if (!looksLikeUrl(message.text)) {
                        bot.sendMessage(message.chatId, "❌ Нужна полная ссылка, начинающаяся с https://.");
                        return true;
                    }

                    pendingListingUrl[message.chatId] = message.text;
                    addState[message.chatId] = 6;
                    bot.sendMessage(
                        message.chatId,
                        "Теперь пришли текст для продавца. Я переведу его на португальский "
                        "и отправлю после твоего следующего сообщения."
                    );
                    return true;
                }

                if (state == 6) {
                    const auto account = db.getCustoJustoAccount(
                        pendingListingAccount[message.chatId]
                    );
                    const std::string listingUrl = pendingListingUrl[message.chatId];
                    const std::string text = message.text;
                    addState[message.chatId] = 0;
                    pendingListingAccount.erase(message.chatId);
                    pendingListingUrl.erase(message.chatId);

                    if (!account || listingUrl.empty() || text.empty()) {
                        bot.sendMessage(message.chatId, "❌ Отправка отменена: не хватает ссылки или текста.");
                        return true;
                    }

                    auto* client = getClient(account->id);
                    client->setBaseUrl(account->loginUrl);

                    if (!ai.enabled()) {
                        bot.sendMessage(
                            message.chatId,
                            "🔴 Перевод недоступен: настрой AI_API_KEY и AI_MODEL в Railway. "
                            "Сообщение продавцу не отправлено."
                        );
                        return true;
                    }

                    MessageRecord translationRequest;
                    translationRequest.incoming = true;
                    translationRequest.text =
                        "Переведи следующий текст с русского на европейский португальский. "
                        "Верни только готовый перевод без комментариев, кавычек и пояснений:\n\n" + text;

                    const std::string translatedText = ai.generateReply(
                        std::vector<MessageRecord>{translationRequest}
                    );

                    if (translatedText.empty()) {
                        bot.sendMessage(message.chatId, "🔴 Перевод не получен. Сообщение продавцу не отправлено.");
                        return true;
                    }

                    if (!client->sendMessage(listingUrl, translatedText)) {
                        db.setCustoJustoAccountLoggedIn(account->id, client->isLoggedIn());
                        bot.sendMessage(
                            message.chatId,
                            "🔴 Не удалось отправить сообщение: " + client->getLastError()
                        );
                        return true;
                    }

                    bot.sendMessage(
                        message.chatId,
                        "✅ Сообщение переведено на португальский и отправлено продавцу."
                    );
                    return true;
                }

                if (state == 1) {
                    if (message.text.empty()) {
                        bot.sendMessage(message.chatId, "❌ Название пустое.\nОтправь название ещё раз.");
                        return true;
                    }

                    pendingName[message.chatId] = message.text;
                    addState[message.chatId] = 2;
                    bot.sendMessage(
                        message.chatId,
                        "Шаг 2 из 3\n\nТеперь отправь email этого аккаунта CustoJusto."
                    );
                    return true;
                }

                if (state == 2) {
                    if (!looksLikeEmail(message.text)) {
                        bot.sendMessage(
                            message.chatId,
                            "❌ Похоже, это не email.\n\nПример:\nname@example.com"
                        );
                        return true;
                    }

                    pendingEmail[message.chatId] = message.text;
                    addState[message.chatId] = 3;
                    bot.sendMessage(message.chatId, "Шаг 3 из 3\n\nОтправь ссылку CustoJusto.");
                    return true;
                }

                if (state == 3) {
                    if (!looksLikeUrl(message.text)) {
                        bot.sendMessage(
                            message.chatId,
                            "❌ Неверная ссылка.\n\nНужна ссылка, начинающаяся с http:// или https://."
                        );
                        return true;
                    }

                    const auto name = pendingName[message.chatId];
                    const auto email = pendingEmail[message.chatId];
                    const long long id = db.addCustoJustoAccount(name, email, message.text);
                    addState[message.chatId] = 0;
                    pendingName.erase(message.chatId);
                    pendingEmail.erase(message.chatId);
                    pendingLoginUrl.erase(message.chatId);

                    bot.sendMessageWithKeyboard(
                        message.chatId,
                        "✅ Аккаунт добавлен.\n\n"
                        "Название: " + name + "\n"
                        "Email: " + email + "\n"
                        "ID: " + std::to_string(id) + "\n\n"
                        "Статус: 🔴 Не вошёл",
                        accountKeyboard(id)
                    );
                    return true;
                }

                if (!message.text.empty()) {
                    db.saveMessage(
                        message.chatId,
                        message.senderId,
                        message.username,
                        message.text,
                        true,
                        message.updateId
                    );

                    if (!aiKey || std::string(aiKey).empty()) {
                        bot.sendMessage(message.chatId, "Сообщение получено.");
                        return true;
                    }

                    const std::string answer = ai.generateReply(
                        db.getHistory(message.chatId, config.aiHistoryLimit)
                    );

                    if (!answer.empty()) {
                        bot.sendMessage(message.chatId, answer);
                    }
                }

                return true;
            }
        );

        bot.sendMessageWithKeyboard(
            ownerTelegramId,
            "🤖 CRM запущена.\n\nУправление аккаунтами CustoJusto:",
            accountsKeyboard()
        );

        std::cout << "Telegram CRM started\n";
        bot.run();

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
