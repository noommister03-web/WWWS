#include "TelegramBot.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

using json = nlohmann::json;

namespace {

size_t writeCallback(
    void* contents,
    size_t size,
    size_t nmemb,
    void* userp
) {
    const size_t totalSize = size * nmemb;

    auto* output =
        static_cast<std::string*>(userp);

    output->append(
        static_cast<char*>(contents),
        totalSize
    );

    return totalSize;
}

}

TelegramBot::TelegramBot(
    std::string token,
    int pollTimeout,
    bool privateChatsOnly
)
    : token_(std::move(token)),
      pollTimeout_(pollTimeout),
      privateChatsOnly_(privateChatsOnly) {

    if (token_.empty()) {
        throw std::invalid_argument(
            "Telegram token is empty"
        );
    }

    if (pollTimeout_ < 0) {
        pollTimeout_ = 0;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void TelegramBot::setMessageHandler(
    MessageHandler handler
) {
    handler_ = std::move(handler);
}

void TelegramBot::setStopChecker(
    std::function<bool()> checker
) {
    stopChecker_ = std::move(checker);
}

void TelegramBot::stop() {
    running_ = false;
}

bool TelegramBot::stopRequested() const {
    if (!running_) {
        return true;
    }

    if (stopChecker_ && stopChecker_()) {
        return true;
    }

    return false;
}

std::string TelegramBot::apiUrl(
    const std::string& method
) const {
    return "https://api.telegram.org/bot" +
           token_ +
           "/" +
           method;
}

std::string TelegramBot::urlEncode(
    const std::string& value
) {
    CURL* curl = curl_easy_init();

    if (!curl) {
        throw std::runtime_error(
            "Unable to initialize CURL"
        );
    }

    char* encoded =
        curl_easy_escape(
            curl,
            value.c_str(),
            static_cast<int>(value.size())
        );

    if (!encoded) {
        curl_easy_cleanup(curl);

        throw std::runtime_error(
            "Unable to URL encode value"
        );
    }

    std::string result(encoded);

    curl_free(encoded);
    curl_easy_cleanup(curl);

    return result;
}

TelegramBot::HttpResponse TelegramBot::postForm(
    const std::string& method,
    const std::string& form
) {
    CURL* curl = curl_easy_init();

    if (!curl) {
        return {
            0,
            "",
            true
        };
    }

    HttpResponse response;

    const std::string url =
        apiUrl(method);

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_POST,
        1L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDS,
        form.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDSIZE,
        static_cast<long>(form.size())
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        writeCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response.body
    );

    curl_easy_setopt(
        curl,
        CURLOPT_CONNECTTIMEOUT,
        15L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_TIMEOUT,
        static_cast<long>(
            std::max(30, pollTimeout_ + 20)
        )
    );

    curl_easy_setopt(
        curl,
        CURLOPT_FOLLOWLOCATION,
        1L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_NOSIGNAL,
        1L
    );

    const CURLcode result =
        curl_easy_perform(curl);

    if (result != CURLE_OK) {
        response.networkError = true;

        std::cerr
            << "Telegram network error: "
            << curl_easy_strerror(result)
            << std::endl;
    }

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &response.httpCode
    );

    curl_easy_cleanup(curl);

    return response;
}

bool TelegramBot::deleteWebhook() {
    const HttpResponse response =
        postForm("deleteWebhook", "");

    if (response.networkError) {
        return false;
    }

    try {
        const json data =
            json::parse(response.body);

        return data.value("ok", false);

    } catch (...) {
        return false;
    }
}

bool TelegramBot::getMe(
    std::string& botUsername
) {
    const HttpResponse response =
        postForm("getMe", "");

    if (response.networkError) {
        return false;
    }

    try {
        const json data =
            json::parse(response.body);

        if (!data.value("ok", false)) {
            return false;
        }

        const json result =
            data.value(
                "result",
                json::object()
            );

        botUsername =
            result.value(
                "username",
                ""
            );

        return true;

    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "getMe parse error: "
            << error.what()
            << std::endl;

        return false;
    }
}

std::string TelegramBot::limitUtf8(
    const std::string& text,
    std::size_t maxBytes
) {
    if (text.size() <= maxBytes) {
        return text;
    }

    std::size_t position = maxBytes;

    while (
        position > 0 &&
        (
            static_cast<unsigned char>(
                text[position]
            ) & 0xC0
        ) == 0x80
    ) {
        --position;
    }

    return text.substr(0, position);
}

SendStatus TelegramBot::sendSingleMessage(
    long long chatId,
    const std::string& text
) {
    constexpr std::size_t MAX_TELEGRAM_TEXT =
        4096;

    const std::string safeText =
        limitUtf8(
            text,
            MAX_TELEGRAM_TEXT
        );

    const auto now =
        std::chrono::steady_clock::now();

    constexpr auto MIN_SEND_INTERVAL =
        std::chrono::milliseconds(350);

    auto it = lastSend_.find(chatId);

    if (it != lastSend_.end()) {
        const auto elapsed =
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(now - it->second);

        if (elapsed < MIN_SEND_INTERVAL) {
            std::this_thread::sleep_for(
                MIN_SEND_INTERVAL - elapsed
            );
        }
    }

    const std::string form =
        "chat_id=" +
        urlEncode(
            std::to_string(chatId)
        ) +
        "&text=" +
        urlEncode(safeText);

    const HttpResponse response =
        postForm(
            "sendMessage",
            form
        );

    if (response.networkError) {
        return SendStatus::TemporaryFailure;
    }

    try {
        const json data =
            json::parse(response.body);

        if (data.value("ok", false)) {
            lastSend_[chatId] =
                std::chrono::steady_clock::now();

            return SendStatus::Success;
        }

        const int errorCode =
            data.value("error_code", 0);

        std::cerr
            << "Telegram sendMessage failed. "
            << "HTTP="
            << response.httpCode
            << " error="
            << errorCode
            << std::endl;

        if (
            errorCode == 429 ||
            errorCode >= 500
        ) {
            return SendStatus::TemporaryFailure;
        }

        return SendStatus::PermanentFailure;

    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "sendMessage parse error: "
            << error.what()
            << std::endl;

        return SendStatus::TemporaryFailure;
    }
}

SendStatus TelegramBot::sendMessage(
    long long chatId,
    const std::string& text
) {
    if (text.empty()) {
        return SendStatus::PermanentFailure;
    }

    return sendSingleMessage(
        chatId,
        text
    );
}

void TelegramBot::run() {
    std::string botUsername;

    if (!deleteWebhook()) {
        std::cerr
            << "Warning: unable to delete Telegram webhook."
            << std::endl;
    }

    if (!getMe(botUsername)) {
        throw std::runtime_error(
            "Telegram getMe failed. "
            "Check TG_BOT_TOKEN."
        );
    }

    std::cout
        << "Telegram bot started";

    if (!botUsername.empty()) {
        std::cout
            << " as @" << botUsername;
    }

    std::cout
        << std::endl;

    long long offset = 0;

    while (!stopRequested()) {
        std::string form =
            "timeout=" +
            urlEncode(
                std::to_string(pollTimeout_)
            ) +
            "&allowed_updates=" +
            urlEncode("[\"message\"]");

        if (offset > 0) {
            form +=
                "&offset=" +
                urlEncode(
                    std::to_string(offset)
                );
        }

        const HttpResponse response =
            postForm(
                "getUpdates",
                form
            );

        if (response.networkError) {
            std::this_thread::sleep_for(
                std::chrono::seconds(3)
            );

            continue;
        }

        try {
            const json data =
                json::parse(response.body);

            if (!data.value("ok", false)) {
                std::cerr
                    << "getUpdates failed: "
                    << response.body
                    << std::endl;

                std::this_thread::sleep_for(
                    std::chrono::seconds(3)
                );

                continue;
            }

            const json updates =
                data.value(
                    "result",
                    json::array()
                );

            for (const auto& update : updates) {
                if (stopRequested()) {
                    break;
                }

                const long long updateId =
                    update.value(
                        "update_id",
                        0LL
                    );

                offset = updateId + 1;

                if (
                    !update.contains("message") ||
                    !update["message"].is_object()
                ) {
                    continue;
                }

                const auto& message =
                    update["message"];

                if (
                    !message.contains("chat") ||
                    !message["chat"].is_object()
                ) {
                    continue;
                }

                const long long chatId =
                    message["chat"].value(
                        "id",
                        0LL
                    );

                const std::string chatType =
                    message["chat"].value(
                        "type",
                        ""
                    );

                if (
                    privateChatsOnly_ &&
                    chatType != "private"
                ) {
                    continue;
                }

                if (
                    !message.contains("text") ||
                    !message["text"].is_string()
                ) {
                    continue;
                }

                IncomingMessage incoming;

                incoming.updateId =
                    updateId;

                incoming.chatId =
                    chatId;

                incoming.text =
                    message["text"].get<
                        std::string
                    >();

                if (
                    message.contains("from") &&
                    message["from"].is_object()
                ) {
                    incoming.senderId =
                        std::to_string(
                            message["from"].value(
                                "id",
                                0LL
                            )
                        );

                    incoming.username =
                        message["from"].value(
                            "username",
                            ""
                        );
                }

                if (handler_) {
                    const bool handled =
                        handler_(incoming);

                    if (!handled) {
                        std::cerr
                            << "Message handler returned "
                               "false for update "
                            << updateId
                            << std::endl;
                    }
                }
            }

        } catch (
            const std::exception& error
        ) {
            std::cerr
                << "getUpdates parse error: "
                << error.what()
                << std::endl;

            std::this_thread::sleep_for(
                std::chrono::seconds(2)
            );
        }
    }

    std::cout
        << "Telegram bot stopped."
        << std::endl;
}
