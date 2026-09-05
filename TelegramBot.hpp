#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>

struct IncomingMessage {
    long long updateId = 0;
    long long chatId = 0;

    std::string senderId;
    std::string username;
    std::string text;
};

enum class SendStatus {
    Success,
    PermanentFailure,
    TemporaryFailure
};

class TelegramBot {
public:
    using MessageHandler =
        std::function<bool(const IncomingMessage&)>;

    TelegramBot(
        std::string token,
        int pollTimeout,
        bool privateChatsOnly
    );

    void setMessageHandler(
        MessageHandler handler
    );

    void setStopChecker(
        std::function<bool()> checker
    );

    void run();

    void stop();

    SendStatus sendMessage(
        long long chatId,
        const std::string& text
    );

private:
    struct HttpResponse {
        long httpCode = 0;
        std::string body;
        bool networkError = false;
    };

    std::string token_;

    int pollTimeout_;

    bool privateChatsOnly_;

    MessageHandler handler_;

    std::function<bool()> stopChecker_;

    std::atomic<bool> running_{true};

    std::unordered_map<
        long long,
        std::chrono::steady_clock::time_point
    > lastSend_;

    std::string apiUrl(
        const std::string& method
    ) const;

    HttpResponse postForm(
        const std::string& method,
        const std::string& form
    );

    bool deleteWebhook();

    bool getMe(
        std::string& botUsername
    );

    bool stopRequested() const;

    SendStatus sendSingleMessage(
        long long chatId,
        const std::string& text
    );

    static std::string urlEncode(
        const std::string& value
    );

    static std::string limitUtf8(
        const std::string& text,
        std::size_t maxBytes
    );
};
