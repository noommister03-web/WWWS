#include "Config.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::string trim(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return "";
    }

    const auto last = input.find_last_not_of(" \t\r\n");

    return input.substr(first, last - first + 1);
}

std::string unquote(std::string value) {
    value = trim(value);

    if (value.size() >= 2) {
        if ((value.front() == '"' && value.back() == '"') ||
            (value.front() == '\'' && value.back() == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }

    return value;
}

void loadDotEnv() {
    std::ifstream file(".env");

    if (!file) {
        return;
    }

    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto pos = line.find('=');

        if (pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, pos));
        std::string value = unquote(line.substr(pos + 1));

        if (!key.empty()) {
            setenv(key.c_str(), value.c_str(), 0);
        }
    }
}

std::string envString(
    const char* name,
    const std::string& fallback = ""
) {
    const char* value = std::getenv(name);

    if (!value) {
        return fallback;
    }

    return value;
}

int envInt(const char* name, int fallback) {
    const char* value = std::getenv(name);

    if (!value) {
        return fallback;
    }

    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

bool envBool(const char* name, bool fallback) {
    const char* value = std::getenv(name);

    if (!value) {
        return fallback;
    }

    std::string v = value;

    for (char& c : v) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }

    if (v == "true" || v == "1" || v == "yes" || v == "on") {
        return true;
    }

    if (v == "false" || v == "0" || v == "no" || v == "off") {
        return false;
    }

    return fallback;
}

}

Config Config::load() {
    loadDotEnv();

    Config config;

    config.telegramToken = envString("TG_BOT_TOKEN");

    if (config.telegramToken.empty()) {
        throw std::runtime_error(
            "TG_BOT_TOKEN is not configured."
        );
    }

    config.dbPath = envString(
        "DB_PATH",
        "/app/data/bot.sqlite3"
    );

    config.aiApiKey = envString("AI_API_KEY");

    config.aiBaseUrl = envString(
        "AI_BASE_URL",
        "https://api.openai.com/v1"
    );

    config.aiModel = envString("AI_MODEL");

    config.aiSystemPrompt = envString(
        "AI_SYSTEM_PROMPT",
        config.aiSystemPrompt
    );

    config.whatsappNumber = envString(
        "WHATSAPP_NUMBER"
    );

    config.telegramPollTimeout = envInt(
        "TELEGRAM_POLL_TIMEOUT",
        30
    );

    config.aiHistoryLimit = envInt(
        "AI_HISTORY_LIMIT",
        20
    );

    config.aiTimeout = envInt(
        "AI_TIMEOUT",
        30
    );

    config.privateChatsOnly = envBool(
        "PRIVATE_CHATS_ONLY",
        true
    );

    return config;
}
