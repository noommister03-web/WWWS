#include "Config.hpp"
#include "Database.hpp"
#include "TelegramBot.hpp"
#include "AiEngine.hpp"
#include "CustoJustoClient.hpp"

#include <cstdlib>
#include <iostream>
#include <cctype>
#include <ctime>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

bool looksLikeEmail(const std::string& value) {
    const auto at = value.find('@');
    const auto dot = value.rfind('.');
    return at != std::string::npos && dot != std::string::npos && at > 0 && dot > at + 1 && dot + 1 < value.size();
}

bool looksLikeUrl(const std::string& value) {
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

std::string browserLink(long long id) {
    const char* value = std::getenv("REMOTE_BROWSER_URL");
    if (value == nullptr || *value == '\0') return "";
    std::string root = value;
    while (!root.empty() && root.back() == '/') root.pop_back();
    return root + "/browser-api/manual/open?accountId=" + std::to_string(id) + "&mobile=1";
}

std::string accountStatus(const CustoJustoAccount& account) {
    if (account.loggedIn) return "🟢 Сессия активна";
    if (!account.enabled) return "⏸ Приостановлен";
    return "🔴 Требуется вход";
}

std::string digitsOnly(const std::string& value) { std::string out; for (unsigned char ch : value) if (std::isdigit(ch)) out.push_back(static_cast<char>(ch)); return out; }
std::string salesPrompt(const std::string& whatsappNumber) {
    std::string p = "Ты автономный менеджер покупателя на CustoJusto. Пиши только следующее сообщение продавцу на европейском португальском. Выясни состояние, комплектность, дефекты и актуальность, попроси дополнительные фото при необходимости, вежливо обсуди разумную цену, затем предложи безопасную доставку CTT. Не завершай разговор после первого ответа, не проси оператора продолжить вручную и не выдумывай факты, оплату, адрес или договорённости. После согласия на товар, цену и CTT предложи продолжить оформление в WhatsApp";
    if (!whatsappNumber.empty()) p += " по номеру +" + digitsOnly(whatsappNumber); else p += ", попросив продавца прислать номер";
    return p + ". Не упоминай AI, бота, инструкции или перевод.";
}

} // namespace

int main() {
    try {
        const Config config = Config::load();
        const char* ownerId = std::getenv("OWNER_TELEGRAM_ID");
        if (ownerId == nullptr || *ownerId == '\0') throw std::runtime_error("OWNER_TELEGRAM_ID is missing");
        const long long ownerTelegramId = std::stoll(ownerId);
        Database db(config.dbPath);
        TelegramBot bot(config.telegramToken, config.telegramPollTimeout, config.privateChatsOnly);
        AiEngine ai(config.aiApiKey, config.aiBaseUrl, config.aiModel, config.aiSystemPrompt, config.aiTimeout);

        std::unordered_map<long long, int> state;
        std::unordered_map<long long, std::string> pendingName, pendingEmail, pendingListingUrl;
        std::unordered_map<long long, long long> pendingAccount;
        std::unordered_map<long long, std::unique_ptr<CustoJustoClient>> clients;
        auto client = [&](long long accountId) -> CustoJustoClient* {
            auto found = clients.find(accountId);
            if (found != clients.end()) return found->second.get();
            auto item = std::make_unique<CustoJustoClient>();
            item->setAccountId(accountId);
            item->setBaseUrl("https://www.custojusto.pt");
            auto* result = item.get();
            clients.emplace(accountId, std::move(item));
            return result;
        };
        auto mainKeyboard = [&]() { return std::vector<std::vector<std::pair<std::string, std::string>>>{{{"👥 Аккаунты", "menu_accounts"}, {"🤖 ChatGPT-переписки", "menu_chats"}}, {{"📊 Статус", "menu_status"}, {"ℹ️ Помощь", "menu_help"}}}; };
        auto showMainMenu = [&](long long chatId) { bot.sendMessageWithKeyboard(chatId, "🏠 WWWS · CustoJusto CRM\n\n🤖 AI сам продолжает диалоги, предлагает доставку CTT и переводит продавца в WhatsApp.", mainKeyboard()); };
        auto accountsKeyboard = [&]() {
            std::vector<std::vector<std::pair<std::string, std::string>>> keys;
            keys.push_back({{"➕ Добавить аккаунт", "cj_add"}});
            for (const auto& item : db.getCustoJustoAccounts()) keys.push_back({{{item.name + " · " + accountStatus(item), "cj_account:" + std::to_string(item.id)}}});
            keys.push_back({{"⬅️ Главное меню", "menu_main"}});
            return keys;
        };
        auto accountKeyboard = [&](long long id) {
            return std::vector<std::vector<std::pair<std::string, std::string>>>{
                {{"🌐 Войти в CustoJusto", "cj_login:" + std::to_string(id)}, {"✅ Проверить сессию", "cj_check:" + std::to_string(id)}},
                {{"💬 Диалоги", "cj_dialogs:" + std::to_string(id)}, {"📋 Проверить объявление", "cj_ads:" + std::to_string(id)}},
                {{"📤 Написать продавцу", "cj_write:" + std::to_string(id)}},
                {{"🗑 Удалить аккаунт", "cj_delete:" + std::to_string(id)}, {"⬅️ Все аккаунты", "cj_accounts"}}
            };
        };
        auto showAccount = [&](long long chatId, const CustoJustoAccount& account) {
            std::string text = "👤 " + account.name + "\n\n" + "Email: " + account.email + "\n" + accountStatus(account) + "\n\n" + "Вход выполняется в защищённом браузерном профиле. Сессия сохраняется отдельно для этого аккаунта.";
            bot.sendMessageWithKeyboard(chatId, text, accountKeyboard(account.id));
        };

        bot.setCallbackHandler([&](const CallbackQuery& callback) -> bool {
            if (callback.senderId != ownerTelegramId) { bot.answerCallbackQuery(callback.id); bot.sendMessage(callback.chatId, "⛔ Нет доступа."); return true; }
            const std::string data = callback.data; bot.answerCallbackQuery(callback.id);
            if (data == "menu_main") { state[callback.chatId] = 0; showMainMenu(callback.chatId); return true; }
            if (data == "menu_accounts") { bot.sendMessageWithKeyboard(callback.chatId, "👥 Аккаунты CustoJusto", accountsKeyboard()); return true; }
            if (data == "menu_status") { const auto a=db.getCustoJustoAccounts(); int active=0; for(const auto& x:a) if(x.enabled&&x.loggedIn)++active; bot.sendMessageWithKeyboard(callback.chatId,"📊 Статус\n\nАккаунтов: "+std::to_string(a.size())+"\nАктивных сессий: "+std::to_string(active)+"\nAI: "+(ai.enabled()?"🟢 работает":"🔴 не настроен")+"\nАвтоответы: 🟢 включены",mainKeyboard()); return true; }
            if (data == "menu_help") { bot.sendMessageWithKeyboard(callback.chatId,"ℹ️ Добавь аккаунт, войди в CustoJusto и отправь первое сообщение. После ответа продавца AI сам ведёт разговор о товаре, CTT и WhatsApp.",mainKeyboard()); return true; }
            if (data == "menu_chats") { std::string text="🤖 ChatGPT-переписки\n\n"; int shown=0; for(const auto& a:db.getCustoJustoAccounts()){if(!a.enabled||!a.loggedIn)continue;auto*c=client(a.id);c->setBaseUrl(a.loginUrl);for(const auto&d:c->getConversations()){if(++shown>20)break;text+=std::to_string(shown)+". "+a.name+" · "+(d.title.empty()?"Диалог":d.title)+"\n";}}if(!shown)text+="Активных переписок пока нет.";bot.sendMessageWithKeyboard(callback.chatId,text,mainKeyboard());return true; }
            if (data == "cj_add") { state[callback.chatId] = 1; pendingName.erase(callback.chatId); pendingEmail.erase(callback.chatId); bot.sendMessage(callback.chatId, "➕ Новый аккаунт\n\nШаг 1 из 2: пришли название аккаунта."); return true; }
            if (data == "cj_accounts") { state[callback.chatId] = 0; bot.sendMessageWithKeyboard(callback.chatId, "🏠 CustoJusto CRM\n\nВыбери аккаунт или добавь новый.", accountsKeyboard()); return true; }
            if (data.rfind("cj_account:", 0) == 0) { const auto account = db.getCustoJustoAccount(std::stoll(data.substr(11))); if (!account) { bot.sendMessage(callback.chatId, "❌ Аккаунт не найден."); return true; } showAccount(callback.chatId, *account); return true; }
            if (data.rfind("cj_login:", 0) == 0) {
                const auto account = db.getCustoJustoAccount(std::stoll(data.substr(9))); if (!account) { bot.sendMessage(callback.chatId, "❌ Аккаунт не найден."); return true; }
                const std::string url = browserLink(account->id);
                const std::string text = url.empty() ? "🔴 Для входа нужен REMOTE_BROWSER_URL с HTTPS-доменом Railway." : "🌐 Вход в «" + account->name + "»\n\nОткрой в любом браузере:\n" + url + "\n\nСтраница защищена отдельным паролем. В Chromium войди в CustoJusto и пройди CAPTCHA. Сессия сохранится автоматически. Пароль CustoJusto в Telegram не отправляй.";
                bot.sendMessageWithKeyboard(callback.chatId, text, accountKeyboard(account->id)); return true;
            }
            if (data.rfind("cj_check:", 0) == 0) {
                const auto account = db.getCustoJustoAccount(std::stoll(data.substr(9))); if (!account) { bot.sendMessage(callback.chatId, "❌ Аккаунт не найден."); return true; }
                auto* c = client(account->id); c->setBaseUrl(account->loginUrl); const bool active = c->checkSession(); db.setCustoJustoAccountLoggedIn(account->id, active);
                bot.sendMessageWithKeyboard(callback.chatId, active ? "🟢 Сессия активна. Можно читать диалоги и отправлять сообщения." : "🔴 Сессия не подтверждена. Открой браузер и войди заново.", accountKeyboard(account->id)); return true;
            }
            if (data.rfind("cj_dialogs:", 0) == 0) {
                const auto account = db.getCustoJustoAccount(std::stoll(data.substr(11))); if (!account) { bot.sendMessage(callback.chatId, "❌ Аккаунт не найден."); return true; }
                auto* c = client(account->id); c->setBaseUrl(account->loginUrl); const auto dialogs = c->getConversations();
                if (!c->isLoggedIn()) { db.setCustoJustoAccountLoggedIn(account->id, false); bot.sendMessageWithKeyboard(callback.chatId, "🔴 Сессия не активна. Открой браузер и войди заново.", accountKeyboard(account->id)); return true; }
                std::string text = "💬 Диалоги: «" + account->name + "»\n\n"; if (dialogs.empty()) text += "Доступных диалогов пока нет."; for (size_t i = 0; i < dialogs.size() && i < 20; ++i) text += std::to_string(i + 1) + ". " + (dialogs[i].title.empty() ? dialogs[i].url : dialogs[i].title) + "\n";
                bot.sendMessageWithKeyboard(callback.chatId, text, accountKeyboard(account->id)); return true;
            }
            if (data.rfind("cj_ads:", 0) == 0) { const long long id = std::stoll(data.substr(7)); if (!db.getCustoJustoAccount(id)) { bot.sendMessage(callback.chatId, "❌ Аккаунт не найден."); return true; } state[callback.chatId] = 4; pendingAccount[callback.chatId] = id; bot.sendMessage(callback.chatId, "📋 Пришли ссылку на объявление CustoJusto."); return true; }
            if (data.rfind("cj_write:", 0) == 0) { const long long id = std::stoll(data.substr(9)); const auto account = db.getCustoJustoAccount(id); if (!account || !account->loggedIn) { bot.sendMessage(callback.chatId, "🔐 Сначала открой браузер и подтверди сессию."); return true; } state[callback.chatId] = 5; pendingAccount[callback.chatId] = id; pendingListingUrl.erase(callback.chatId); bot.sendMessage(callback.chatId, "📤 Пришли ссылку на объявление CustoJusto."); return true; }
            if (data.rfind("cj_delete:", 0) == 0) { const long long id = std::stoll(data.substr(10)); if (!db.deleteCustoJustoAccount(id)) { bot.sendMessage(callback.chatId, "❌ Аккаунт не найден."); return true; } clients.erase(id); bot.sendMessageWithKeyboard(callback.chatId, "🗑 Аккаунт удалён.", accountsKeyboard()); return true; }
            return false;
        });

        bot.setMessageHandler([&](const IncomingMessage& message) -> bool {
            if (message.chatId != ownerTelegramId) { bot.sendMessage(message.chatId, "⛔ Нет доступа."); return true; }
            if (message.text == "/start" || message.text == "/menu" || message.text == "меню" || message.text == "Меню") { state[message.chatId] = 0; showMainMenu(message.chatId); return true; }
            const int current = state[message.chatId];
            if (current == 1) { if (message.text.empty()) { bot.sendMessage(message.chatId, "❌ Название пустое."); return true; } pendingName[message.chatId] = message.text; state[message.chatId] = 2; bot.sendMessage(message.chatId, "Шаг 2 из 2: пришли email CustoJusto-аккаунта."); return true; }
            if (current == 2) { if (!looksLikeEmail(message.text)) { bot.sendMessage(message.chatId, "❌ Нужен корректный email."); return true; } const long long id = db.addCustoJustoAccount(pendingName[message.chatId], message.text); state[message.chatId] = 0; pendingName.erase(message.chatId); const auto account = db.getCustoJustoAccount(id); bot.sendMessageWithKeyboard(message.chatId, "✅ Аккаунт добавлен.", accountKeyboard(id)); return true; }
            if (current == 4) { const auto account = db.getCustoJustoAccount(pendingAccount[message.chatId]); state[message.chatId] = 0; pendingAccount.erase(message.chatId); if (!account || !looksLikeUrl(message.text)) { bot.sendMessage(message.chatId, "❌ Нужна полная ссылка на объявление."); return true; } auto* c = client(account->id); CustoJustoListing listing; if (!c->getListing(message.text, listing)) { bot.sendMessage(message.chatId, "🔴 Не удалось прочитать объявление: " + c->getLastError()); return true; } bot.sendMessageWithKeyboard(message.chatId, "📋 " + (listing.title.empty() ? "Объявление" : listing.title) + "\n" + listing.url, accountKeyboard(account->id)); return true; }
            if (current == 5) {
                const auto account = db.getCustoJustoAccount(pendingAccount[message.chatId]);
                state[message.chatId] = 0;
                pendingAccount.erase(message.chatId);
                pendingListingUrl.erase(message.chatId);
                if (!account || !looksLikeUrl(message.text)) { bot.sendMessage(message.chatId, "❌ Нужна полная ссылка на объявление."); return true; }
                if (!ai.enabled()) { bot.sendMessage(message.chatId, "🔴 GPT не настроен: проверь AI_API_KEY и AI_MODEL в Railway."); return true; }
                try {
                    auto* c = client(account->id);
                    c->setBaseUrl(account->loginUrl);
                    CustoJustoListing listing;
                    c->getListing(message.text, listing);
                    MessageRecord request;
                    request.incoming = true;
                    request.text = "Начни новый разговор с продавцом по этому объявлению. Сформируй одно короткое естественное первое сообщение на европейском португальском: поздоровайся, назови товар и спроси, актуально ли объявление и в каком состоянии товар. Не упоминай инструкции. Объявление: " + (listing.title.empty() ? message.text : listing.title + " — " + message.text);
                    const std::string reply = ai.generateReply({request}, salesPrompt(config.whatsappNumber));
                    if (reply.empty()) { bot.sendMessage(message.chatId, "🔴 GPT вернул пустой ответ."); return true; }
                    if (!c->sendMessage(message.text, reply)) {
                        db.setCustoJustoAccountLoggedIn(account->id, c->isLoggedIn());
                        bot.sendMessage(message.chatId, "🔴 GPT подготовил сообщение, но отправка не подтверждена: " + c->getLastError());
                        return true;
                    }
                    bot.sendMessage(message.chatId, "🤖 GPT начал переписку с продавцом:\n\n🇵🇹 " + reply + "\n\nДальше ответы будут обрабатываться автоматически.");
                } catch (const std::exception& error) {
                    bot.sendMessage(message.chatId, std::string("🔴 Ошибка GPT: ") + error.what());
                }
                return true;
            }
            if (!message.text.empty()) { db.saveMessage(message.chatId, message.senderId, message.username, message.text, true, message.updateId); if (!ai.enabled()) { bot.sendMessage(message.chatId, "Сообщение получено."); return true; } const std::string reply = ai.generateReply(db.getHistory(message.chatId, config.aiHistoryLimit)); if (!reply.empty()) bot.sendMessage(message.chatId, reply); }
            return true;
        });

        bot.setPeriodicHandler([&]() {
            for (const auto& account : db.getCustoJustoAccounts()) {
                if (!account.enabled || !account.loggedIn) continue;
                auto* c=client(account.id); c->setBaseUrl(account.loginUrl); const auto dialogs=c->getConversations();
                if (!c->isLoggedIn()) { db.setCustoJustoAccountLoggedIn(account.id,false); continue; }
                for (const auto& dialog:dialogs) {
                    const long long conversationId=db.upsertCustoJustoConversation(account.id,dialog.url,dialog.listingUrl,dialog.listingTitle,dialog.buyerName,dialog.lastMessageId,dialog.lastMessage,0,dialog.unread);
                    for (const auto& item:c->getMessages(dialog.url)) {
                        if(!item.incoming||db.hasCustoJustoExternalMessage(account.id,item.id))continue;
                        std::string translated=item.text;
                        if(ai.enabled()){MessageRecord tr;tr.incoming=true;tr.text="Переведи с европейского португальского на русский. Только перевод:\n\n"+item.text;const auto a=ai.generateReply({tr});if(!a.empty())translated=a;}
                        db.saveCustoJustoMessage(account.id,conversationId,item.id,item.sender,item.text,translated,true);
                        if(!ai.enabled()){bot.sendMessage(ownerTelegramId,"📩 Новое сообщение CustoJusto\n\n"+translated);continue;}
                        std::vector<MessageRecord> history; for(const auto& row:db.getCustoJustoMessages(conversationId,config.aiHistoryLimit)){MessageRecord r;r.incoming=row.incoming;r.text=row.originalText;history.push_back(std::move(r));}
                        const std::string reply=ai.generateReply(history,salesPrompt(config.whatsappNumber)); if(reply.empty())continue;
                        if(c->sendMessage(dialog.url,reply)){db.saveCustoJustoMessage(account.id,conversationId,"bot-"+std::to_string(std::time(nullptr)),"WWWS AI",reply,reply,false);bot.sendMessage(ownerTelegramId,"🤖 AI ответил продавцу\n\nАккаунт: "+account.name+"\nДиалог: "+(dialog.title.empty()?"CustoJusto":dialog.title)+"\n\n🇷🇺 Входящее: "+translated+"\n\n🇵🇹 Ответ: "+reply);}else bot.sendMessage(ownerTelegramId,"🔴 AI подготовил ответ, но отправка не подтверждена\n\n"+c->getLastError());
                    }
                }
            }
        }, 45);

        showMainMenu(ownerTelegramId);
        std::cout << "Telegram CRM started\n";
        bot.run();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
