#include "AiEngine.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
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

AiEngine::AiEngine(
    std::string apiKey,
    std::string baseUrl,
    std::string model,
    std::string systemPrompt,
    int timeoutSeconds
)
    : apiKey_(std::move(apiKey)),
      baseUrl_(
          trimTrailingSlash(
              std::move(baseUrl)
          )
      ),
      model_(std::move(model)),
      systemPrompt_(std::move(systemPrompt)),
      timeoutSeconds_(timeoutSeconds) {

    if (timeoutSeconds_ <= 0) {
        timeoutSeconds_ = 30;
    }
}

bool AiEngine::enabled() const {
    return !apiKey_.empty() &&
           !model_.empty() &&
           !baseUrl_.empty();
}

std::string AiEngine::trimTrailingSlash(
    std::string value
) {
    while (
        !value.empty() &&
        value.back() == '/'
    ) {
        value.pop_back();
    }

    return value;
}

std::string AiEngine::limitUtf8(
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

std::string AiEngine::generateReply(
    const std::vector<MessageRecord>& history
) const {
    if (!enabled()) {
        return "";
    }

    json messages = json::array();

    messages.push_back({
        {"role", "system"},
        {"content", systemPrompt_}
    });

    const std::size_t maxHistory =
        std::min<std::size_t>(
            history.size(),
            20
        );

    const std::size_t start =
        history.size() - maxHistory;

    for (
        std::size_t i = start;
        i < history.size();
        ++i
    ) {
        const MessageRecord& record =
            history[i];

        if (record.text.empty()) {
            continue;
        }

        messages.push_back({
            {
                "role",
                record.incoming
                    ? "user"
                    : "assistant"
            },
            {
                "content",
                limitUtf8(
                    record.text,
                    6000
                )
            }
        });
    }

    json request = {
        {"model", model_},
        {"messages", messages},
        {"temperature", 0.7}
    };

    CURL* curl = curl_easy_init();

    if (!curl) {
        throw std::runtime_error(
            "Unable to initialize CURL for AI"
        );
    }

    std::string responseBody;

    const std::string url =
        baseUrl_ +
        "/chat/completions";

    const std::string requestBody =
        request.dump();

    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(
        headers,
        "Content-Type: application/json"
    );

    const std::string authHeader =
        "Authorization: Bearer " +
        apiKey_;

    headers = curl_slist_append(
        headers,
        authHeader.c_str()
    );

    headers = curl_slist_append(
        headers,
        "HTTP-Referer: https://openrouter.ai/"
    );

    headers = curl_slist_append(
        headers,
        "X-Title: Telegram CRM"
    );

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
        requestBody.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDSIZE,
        static_cast<long>(requestBody.size())
    );

    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        headers
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        writeCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &responseBody
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
            timeoutSeconds_
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

    curl_easy_setopt(
        curl,
        CURLOPT_HTTP_VERSION,
        CURL_HTTP_VERSION_1_1
    );

    const CURLcode result =
        curl_easy_perform(curl);

    long httpCode = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &httpCode
    );

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        throw std::runtime_error(
            std::string(
                "AI network error: "
            ) +
            curl_easy_strerror(result)
        );
    }

    if (
        httpCode < 200 ||
        httpCode >= 300
    ) {
        std::cerr
            << "OpenRouter API returned HTTP "
            << httpCode
            << ": "
            << responseBody
            << std::endl;

        throw std::runtime_error(
            "OpenRouter API request failed"
        );
    }

    try {
        const json response =
            json::parse(responseBody);

        if (
            !response.contains("choices") ||
            !response["choices"].is_array() ||
            response["choices"].empty()
        ) {
            throw std::runtime_error(
                "AI response has no choices"
            );
        }

        const auto& choice =
            response["choices"][0];

        if (
            !choice.contains("message") ||
            !choice["message"].is_object()
        ) {
            throw std::runtime_error(
                "AI response has no message"
            );
        }

        const auto& message =
            choice["message"];

        if (
            !message.contains("content") ||
            !message["content"].is_string()
        ) {
            throw std::runtime_error(
                "AI response has no content"
            );
        }

        std::string reply =
            message["content"].get<std::string>();

        if (reply.empty()) {
            throw std::runtime_error(
                "AI returned an empty reply"
            );
        }

        return limitUtf8(
            reply,
            4096
        );

    } catch (const json::exception& error) {
        std::cerr
            << "AI JSON parse error: "
            << error.what()
            << std::endl;

        throw std::runtime_error(
            "Unable to parse AI response"
        );
    }
}
