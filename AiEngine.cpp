#include "AiEngine.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace {
size_t writeCb(void* content, size_t size, size_t count, void* userData) {
    static_cast<std::string*>(userData)->append(
        static_cast<char*>(content),
        size * count
    );
    return size * count;
}

std::string detail(const std::string& body) {
    try {
        auto parsed = json::parse(body);
        if (parsed.contains("error")) {
            auto& error = parsed["error"];
            if (error.is_object() && error.contains("message")) {
                return error["message"].get<std::string>();
            }
            if (error.is_string()) {
                return error.get<std::string>();
            }
        }
        if (parsed.contains("message")) {
            return parsed["message"].get<std::string>();
        }
    } catch (...) {
    }
    return body.substr(0, 500);
}

std::string safeToken(std::string value) {
    if (value.size() > 160) {
        value.resize(160);
    }
    for (char& character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!(std::isalnum(byte) || character == '.' || character == '-' ||
              character == '_' || character == '/' || character == ':')) {
            character = '_';
        }
    }
    return value.empty() ? "unknown" : value;
}

std::string providerHost(const std::string& url) {
    const auto scheme = url.find("://");
    const auto start = scheme == std::string::npos ? 0 : scheme + 3;
    const auto end = url.find_first_of("/?#", start);
    std::string authority = url.substr(start, end - start);
    if (const auto at = authority.rfind('@'); at != std::string::npos) {
        authority.erase(0, at + 1);
    }
    if (!authority.empty() && authority.front() == '[') {
        if (const auto closing = authority.find(']'); closing != std::string::npos) {
            authority.resize(closing + 1);
        }
    } else if (const auto colon = authority.rfind(':'); colon != std::string::npos) {
        authority.resize(colon);
    }
    std::transform(
        authority.begin(),
        authority.end(),
        authority.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); }
    );
    return safeToken(authority);
}

std::string fingerprint(const std::string& value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

void logAiDiagnostic(
    const std::string& provider,
    const std::string& model,
    int attempt,
    long status,
    long long latencyMs,
    const std::string& outcome,
    bool retry,
    std::size_t historyMessages,
    std::size_t historyBytes,
    const std::string& payloadKind,
    const std::string& payload
) {
    std::clog
        << "[ai-diagnostic] provider=" << provider
        << " model=" << safeToken(model)
        << " attempt=" << attempt
        << " status=" << status
        << " latency_ms=" << latencyMs
        << " outcome=" << safeToken(outcome)
        << " retry=" << (retry ? "true" : "false")
        << " history_messages=" << historyMessages
        << " history_bytes=" << historyBytes
        << " payload_kind=" << safeToken(payloadKind)
        << " payload_bytes=" << payload.size()
        << " payload_fp=" << (payload.empty() ? "-" : fingerprint(payload))
        << '\n';
}
}

AiEngine::AiEngine(
    std::string key,
    std::string base,
    std::string model,
    std::string prompt,
    int timeout
) :
    apiKey_(std::move(key)),
    baseUrl_(trimTrailingSlash(std::move(base))),
    model_(std::move(model)),
    systemPrompt_(std::move(prompt)),
    timeoutSeconds_(timeout > 0 ? timeout : 45) {
}

bool AiEngine::enabled() const {
    return !apiKey_.empty() && !model_.empty() && !baseUrl_.empty();
}

std::string AiEngine::trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
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
        (static_cast<unsigned char>(text[position]) & 0xC0) == 0x80
    ) {
        --position;
    }
    return text.substr(0, position);
}

std::string AiEngine::generateReply(
    const std::vector<MessageRecord>& history,
    const std::string& overridePrompt
) const {
    if (!enabled()) {
        return "";
    }

    std::string effectivePrompt = systemPrompt_;
    if (!overridePrompt.empty()) {
        if (!effectivePrompt.empty()) {
            effectivePrompt +=
                "\n\n--- Специализированная инструкция текущей задачи ---\n";
        }
        effectivePrompt += overridePrompt;
    }

    json messages = json::array();
    messages.push_back({
        {"role", "system"},
        {"content", effectivePrompt}
    });

    constexpr std::size_t maxMessages = 200;
    constexpr std::size_t maxHistoryBytes = 48000;
    std::vector<std::pair<std::string, std::string>> selected;
    std::size_t historyBytes = 0;
    for (
        auto item = history.rbegin();
        item != history.rend() && selected.size() < maxMessages;
        ++item
    ) {
        std::string text = limitUtf8(item->text, 6000);
        if (text.empty()) {
            continue;
        }
        if (!selected.empty() && historyBytes + text.size() > maxHistoryBytes) {
            break;
        }
        historyBytes += text.size();
        selected.emplace_back(
            item->incoming ? "user" : "assistant",
            std::move(text)
        );
    }
    std::reverse(selected.begin(), selected.end());
    for (const auto& item : selected) {
        messages.push_back({
            {"role", item.first},
            {"content", item.second}
        });
    }

    json request = {
        {"model", model_},
        {"messages", messages},
        {"temperature", 0.35},
        {"max_tokens", 700}
    };
    std::string url = baseUrl_;
    if (
        url.size() < 17 ||
        url.substr(url.size() - 17) != "/chat/completions"
    ) {
        url += "/chat/completions";
    }

    const std::string provider = providerHost(baseUrl_);
    for (int attempt = 1; attempt <= 3; ++attempt) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Не удалось запустить HTTP-клиент AI");
        }

        std::string responseBody;
        const std::string requestBody = request.dump();
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        const std::string authorization = "Authorization: Bearer " + apiKey_;
        headers = curl_slist_append(headers, authorization.c_str());
        if (baseUrl_.find("openrouter.ai") != std::string::npos) {
            headers = curl_slist_append(
                headers,
                "HTTP-Referer: https://github.com/WWWS12341/WWWS"
            );
            headers = curl_slist_append(
                headers,
                "X-Title: WWWS CustoJusto CRM"
            );
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
        curl_easy_setopt(
            curl,
            CURLOPT_POSTFIELDSIZE,
            static_cast<long>(requestBody.size())
        );
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSeconds_));
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        const auto startedAt = std::chrono::steady_clock::now();
        const CURLcode code = curl_easy_perform(curl);
        const auto latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt
        ).count();
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (code != CURLE_OK) {
            const bool retry = attempt < 3;
            logAiDiagnostic(
                provider,
                model_,
                attempt,
                status,
                latencyMs,
                "network_error_" + std::to_string(static_cast<int>(code)),
                retry,
                selected.size(),
                historyBytes,
                "response",
                responseBody
            );
            if (retry) {
                std::this_thread::sleep_for(std::chrono::seconds(attempt));
                continue;
            }
            throw std::runtime_error(
                std::string("Сеть AI: ") + curl_easy_strerror(code)
            );
        }

        if ((status == 429 || status >= 500) && attempt < 3) {
            logAiDiagnostic(
                provider,
                model_,
                attempt,
                status,
                latencyMs,
                "retryable_http",
                true,
                selected.size(),
                historyBytes,
                "response",
                responseBody
            );
            std::this_thread::sleep_for(std::chrono::seconds(attempt * 2));
            continue;
        }

        if (status < 200 || status >= 300) {
            logAiDiagnostic(
                provider,
                model_,
                attempt,
                status,
                latencyMs,
                "http_error",
                false,
                selected.size(),
                historyBytes,
                "response",
                responseBody
            );
            throw std::runtime_error(
                "AI HTTP " + std::to_string(status) + ": " + detail(responseBody)
            );
        }

        try {
            const auto parsed = json::parse(responseBody);
            const auto content = parsed
                .at("choices")
                .at(0)
                .at("message")
                .at("content");
            std::string output;
            if (content.is_string()) {
                output = content.get<std::string>();
            } else if (content.is_array()) {
                for (const auto& item : content) {
                    if (
                        item.is_object() &&
                        item.value("type", "") == "text"
                    ) {
                        output += item.value("text", "");
                    }
                }
            }
            if (output.empty()) {
                throw std::runtime_error("AI вернул пустой ответ");
            }
            output = limitUtf8(output, 4096);
            logAiDiagnostic(
                provider,
                model_,
                attempt,
                status,
                latencyMs,
                "success",
                false,
                selected.size(),
                historyBytes,
                "output",
                output
            );
            return output;
        } catch (const std::exception& error) {
            logAiDiagnostic(
                provider,
                model_,
                attempt,
                status,
                latencyMs,
                "invalid_response",
                false,
                selected.size(),
                historyBytes,
                "response",
                responseBody
            );
            throw std::runtime_error(
                std::string("Некорректный ответ AI: ") + error.what()
            );
        }
    }

    throw std::runtime_error("AI недоступен после повторных попыток");
}
