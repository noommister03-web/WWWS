#include "AiEngine.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

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
      baseUrl_(std::move(baseUrl)),
      model_(std::move(model)),
      systemPrompt_(std::move(systemPrompt)),
      timeoutSeconds_(timeoutSeconds) {

    if (timeoutSeconds_ <= 0) {
        timeoutSeconds_ = 30;
    }
}

bool AiEngine::enabled() const {
    return !apiKey_.empty() &&
           !model_.empty();
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

    std::string prompt =
        systemPrompt_ +
        "\n\nИстория разговора:\n";

    for (const auto& record : history) {
        if (record.text.empty()) {
            continue;
        }

        prompt +=
            record.incoming
                ? "Пользователь: "
                : "Ассистент: ";

        prompt +=
            limitUtf8(
                record.text,
                6000
            );

        prompt += "\n";
    }

    prompt +=
        "\nОтветь на последнее сообщение пользователя.";

    json request = {
        {
            "contents",
            json::array({
                {
                    {
                        "role",
                        "user"
                    },
                    {
                        "parts",
                        json::array({
                            {
                                {
                                    "text",
                                    prompt
                                }
                            }
                        })
                    }
                }
            })
        },
        {
            "generationConfig",
            {
                {
                    "temperature",
                    0.7
                }
            }
        }
    };

    CURL* curl = curl_easy_init();

    if (!curl) {
        throw std::runtime_error(
            "Unable to initialize CURL for Gemini"
        );
    }

    std::string responseBody;

    std::string model = model_;

    if (
        model.rfind(
            "models/",
            0
        ) == 0
    ) {
        model =
            model.substr(
                7
            );
    }

    const std::string url =
        "https://generativelanguage.googleapis.com/"
        "v1beta/models/" +
        model +
        ":generateContent";

    const std::string requestBody =
        request.dump();

    struct curl_slist* headers =
        nullptr;

    headers =
        curl_slist_append(
            headers,
            "Content-Type: application/json"
        );

    const std::string apiKeyHeader =
        "x-goog-api-key: " +
        apiKey_;

    headers =
        curl_slist_append(
            headers,
            apiKeyHeader.c_str()
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
        static_cast<long>(
            requestBody.size()
        )
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
                "Gemini network error: "
            ) +
            curl_easy_strerror(result)
        );
    }

    if (
        httpCode < 200 ||
        httpCode >= 300
    ) {
        std::cerr
            << "Gemini API returned HTTP "
            << httpCode
            << ": "
            << responseBody
            << std::endl;

        throw std::runtime_error(
            "Gemini API request failed"
        );
    }

    try {
        const json response =
            json::parse(responseBody);

        if (
            !response.contains(
                "candidates"
            ) ||
            !response["candidates"].is_array() ||
            response["candidates"].empty()
        ) {
            throw std::runtime_error(
                "Gemini response has no candidates"
            );
        }

        const auto& candidate =
            response["candidates"][0];

        if (
            !candidate.contains("content") ||
            !candidate["content"].is_object()
        ) {
            throw std::runtime_error(
                "Gemini response has no content"
            );
        }

        const auto& content =
            candidate["content"];

        if (
            !content.contains("parts") ||
            !content["parts"].is_array()
        ) {
            throw std::runtime_error(
                "Gemini response has no parts"
            );
        }

        std::string reply;

        for (
            const auto& part :
            content["parts"]
        ) {
            if (
                part.contains("text") &&
                part["text"].is_string()
            ) {
                reply +=
                    part["text"].get<
                        std::string
                    >();
            }
        }

        if (reply.empty()) {
            throw std::runtime_error(
                "Gemini returned an empty reply"
            );
        }

        return limitUtf8(
            reply,
            4096
        );

    } catch (
        const json::exception& error
    ) {
        std::cerr
            << "Gemini JSON parse error: "
            << error.what()
            << std::endl;

        throw std::runtime_error(
            "Unable to parse Gemini response"
        );
    }
}
