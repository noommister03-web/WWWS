#pragma once

#include <string>

struct Config {
    std::string telegramToken;

    std::string dbPath = "/app/data/bot.sqlite3";

    std::string aiApiKey;
    std::string aiBaseUrl = "https://api.openai.com/v1";
    std::string aiModel;

    std::string aiSystemPrompt =
        "Ты помощник оператора. "
        "Отвечай естественно, кратко и по существу. "
        "Не выдумывай цены, наличие, сроки или факты, которых нет в истории. "
        "Не выдавай себя за конкретного человека. "
        "Если информации недостаточно, задай уточняющий вопрос. "
        "Не инициируй рассылку и не предлагай писать пользователю первым. "
        "Если пользователь явно просит перейти в WhatsApp, оператор может предоставить "
        "настроенный контакт.";

    std::string whatsappNumber;

    int telegramPollTimeout = 30;
    int aiHistoryLimit = 20;
    int aiTimeout = 30;

    bool privateChatsOnly = true;

    static Config load();

    bool aiEnabled() const {
        return !aiApiKey.empty() && !aiModel.empty();
    }
};
