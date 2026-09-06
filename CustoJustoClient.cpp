#include "CustoJustoClient.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace {

size_t writeCallback(
    void* contents,
    size_t size,
    size_t nmemb,
    void* userp
) {
    const size_t total = size * nmemb;

    if (userp == nullptr) {
        return 0;
    }

    auto* buffer =
        static_cast<std::string*>(userp);

    buffer->append(
        static_cast<char*>(contents),
        total
    );

    return total;
}

std::string getEnv(const char* name) {
    const char* value = std::getenv(name);

    if (value == nullptr) {
        return {};
    }

    return std::string(value);
}

std::string trim(std::string value) {
    while (
        !value.empty() &&
        std::isspace(
            static_cast<unsigned char>(
                value.front()
            )
        )
    ) {
        value.erase(value.begin());
    }

    while (
        !value.empty() &&
        std::isspace(
            static_cast<unsigned char>(
                value.back()
            )
        )
    ) {
        value.pop_back();
    }

    return value;
}

} // namespace

CustoJustoClient::CustoJustoClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    browserWorkerUrl_ =
        getEnv("BROWSER_WORKER_URL");

    workerSharedSecret_ =
        getEnv("BROWSER_WORKER_SHARED_SECRET");

    if (browserWorkerUrl_.empty()) {
        browserWorkerUrl_ =
            "http://localhost:3001";
    }

    while (
        !browserWorkerUrl_.empty() &&
        browserWorkerUrl_.back() == '/'
    ) {
        browserWorkerUrl_.pop_back();
    }

    baseUrl_ =
        getEnv("CJ_BASE_URL");

    if (baseUrl_.empty()) {
        baseUrl_ =
            "https://www.custojusto.pt";
    }

    while (
        !baseUrl_.empty() &&
        baseUrl_.back() == '/'
    ) {
        baseUrl_.pop_back();
    }
}

CustoJustoClient::~CustoJustoClient() {
    curl_global_cleanup();
}

void CustoJustoClient::setAccountId(
    long long accountId
) {
    accountId_ = accountId;
}

void CustoJustoClient::setBaseUrl(
    const std::string& baseUrl
) {
    baseUrl_ = baseUrl;

    while (
        !baseUrl_.empty() &&
        baseUrl_.back() == '/'
    ) {
        baseUrl_.pop_back();
    }
}

CustoJustoLoginResult
CustoJustoClient::login(
    const std::string& email,
    const std::string& password
) {
    CustoJustoLoginResult result;

    lastError_.clear();
    loggedIn_ = false;

    if (accountId_ <= 0) {
        result.state = "invalid_account";
        result.message =
            "Не задан ID аккаунта.";

        setError(result.message);
        return result;
    }

    if (email.empty()) {
        result.state = "invalid_email";
        result.message =
            "Не указан email.";

        setError(result.message);
        return result;
    }

    if (password.empty()) {
        result.state = "invalid_password";
        result.message =
            "Не указан пароль.";

        setError(result.message);
        return result;
    }

    std::ostringstream body;

    body
        << "{"
        << "\"accountId\":"
        << accountId_
        << ","
        << "\"email\":\""
        << jsonEscape(email)
        << "\","
        << "\"password\":\""
        << jsonEscape(password)
        << "\","
        << "\"baseUrl\":\""
        << jsonEscape(baseUrl_)
        << "\""
        << "}";

    std::string response;

    if (!post(
            "/login",
            body.str(),
            response
        )) {
        result.state =
            "worker_unavailable";

        result.message =
            lastError_;

        return result;
    }

    result.loggedIn =
        parseBool(
            response,
            "loggedIn"
        );

    result.state =
        parseString(
            response,
            "state"
        );

    result.message =
        parseString(
            response,
            "message"
        );

    result.requiresCaptcha =
        result.state ==
        "captcha_required";

    result.requiresTwoFactor =
        result.state ==
        "two_factor_required";

    loggedIn_ =
        result.loggedIn;

    if (result.message.empty()) {
        result.message =
            result.loggedIn
                ? "Авторизация успешна."
                : "Авторизация не подтверждена.";
    }

    if (!result.loggedIn) {
        setError(result.message);
    }

    return result;
}

bool CustoJustoClient::checkSession() {
    lastError_.clear();

    if (accountId_ <= 0) {
        loggedIn_ = false;

        setError(
            "Не задан ID аккаунта."
        );

        return false;
    }

    std::ostringstream body;

    body
        << "{"
        << "\"accountId\":"
        << accountId_
        << ","
        << "\"baseUrl\":\""
        << jsonEscape(baseUrl_)
        << "\""
        << "}";

    std::string response;

    if (!post(
            "/check",
            body.str(),
            response
        )) {
        loggedIn_ = false;
        return false;
    }

    loggedIn_ =
        parseBool(
            response,
            "loggedIn"
        );

    if (!loggedIn_) {
        const std::string state =
            parseString(
                response,
                "state"
            );

        if (
            state ==
            "captcha_required"
        ) {
            setError(
                "Требуется CAPTCHA."
            );
        }
        else if (
            state ==
            "two_factor_required"
        ) {
            setError(
                "Требуется подтверждение 2FA."
            );
        }
        else {
            setError(
                "Сессия CustoJusto "
                "не авторизована."
            );
        }
    }

    return loggedIn_;
}

void CustoJustoClient::logout() {
    loggedIn_ = false;
    lastError_.clear();
}

bool CustoJustoClient::isLoggedIn() const {
    return loggedIn_;
}

std::vector<CustoJustoConversation>
CustoJustoClient::getConversations() {
    std::vector<CustoJustoConversation> result;
    lastError_.clear();

    if (!checkSession()) {
        return result;
    }

    std::ostringstream body;
    body << "{\"accountId\":" << accountId_
         << ",\"baseUrl\":\"" << jsonEscape(baseUrl_) << "\"}";

    std::string response;
    if (!post("/conversations", body.str(), response)) {
        return result;
    }

    try {
        const json data = json::parse(response);
        if (!data.value("ok", false)) {
            setError(data.value("error", "Не удалось получить диалоги."));
            return result;
        }

        for (const auto& item : data.value("conversations", json::array())) {
            CustoJustoConversation conversation;
            conversation.id = item.value("id", "");
            conversation.url = item.value("url", conversation.id);
            conversation.title = item.value("title", "");
            conversation.unread = item.value("unread", false);
            if (!conversation.url.empty()) {
                result.push_back(std::move(conversation));
            }
        }
    } catch (const std::exception&) {
        setError("Browser worker вернул некорректный список диалогов.");
    }

    return result;
}
std::vector<CustoJustoMessage>
CustoJustoClient::getMessages(
    const std::string& conversationId
) {
    std::vector<CustoJustoMessage> result;
    lastError_.clear();

    if (accountId_ <= 0 || conversationId.empty()) {
        setError("Не задан аккаунт или URL диалога.");
        return result;
    }

    if (!checkSession()) {
        return result;
    }

    std::ostringstream body;
    body << "{\"accountId\":" << accountId_
         << ",\"conversationUrl\":\"" << jsonEscape(conversationId)
         << "\"}";

    std::string response;
    if (!post("/messages", body.str(), response)) {
        return result;
    }

    try {
        const json data = json::parse(response);
        if (!data.value("ok", false)) {
            setError(data.value("error", "Не удалось получить сообщения."));
            return result;
        }

        for (const auto& item : data.value("messages", json::array())) {
            CustoJustoMessage message;
            message.id = item.value("id", "");
            message.conversationId = conversationId;
            message.sender = item.value("sender", "");
            message.text = item.value("text", "");
            message.timestamp = item.value("timestamp", "");
            message.incoming = item.value("incoming", true);
            if (!message.id.empty() && !message.text.empty()) {
                result.push_back(std::move(message));
            }
        }
    } catch (const std::exception&) {
        setError("Browser worker вернул некорректные сообщения.");
    }

    return result;
}
bool CustoJustoClient::sendMessage(
    const std::string& conversationId,
    const std::string& text
) {
    lastError_.clear();

    if (accountId_ <= 0) {
        setError(
            "Не задан ID аккаунта."
        );

        return false;
    }

    if (conversationId.empty()) {
        setError(
            "Не задан conversationId."
        );

        return false;
    }

    if (text.empty()) {
        setError(
            "Сообщение пустое."
        );

        return false;
    }

    if (!checkSession()) {
        return false;
    }

    std::ostringstream body;

    body
        << "{"
        << "\"accountId\":"
        << accountId_
        << ","
        << "\"conversationUrl\":\""
        << jsonEscape(conversationId)
        << "\","
        << "\"text\":\""
        << jsonEscape(text)
        << "\""
        << "}";

    std::string response;

    if (!post(
            "/send",
            body.str(),
            response
        )) {
        return false;
    }

    const bool ok =
        parseBool(
            response,
            "ok"
        );

    if (!ok) {
        const std::string error =
            parseString(
                response,
                "error"
            );

        if (!error.empty()) {
            setError(error);
        }
        else {
            setError(
                "Browser worker "
                "не отправил сообщение."
            );
        }

        return false;
    }

    return true;
}

bool CustoJustoClient::openListing(
    const std::string& listingUrl
) {
    if (listingUrl.empty()) {
        setError(
            "Ссылка на объявление пустая."
        );

        return false;
    }

    if (
        listingUrl.rfind(
            "https://",
            0
        ) != 0 &&
        listingUrl.rfind(
            "http://",
            0
        ) != 0
    ) {
        setError(
            "Некорректная ссылка."
        );

        return false;
    }

    lastError_.clear();

    return true;
}

std::string
CustoJustoClient::getLastError() const {
    return lastError_;
}

void CustoJustoClient::setError(
    const std::string& error
) {
    lastError_ = error;
}

bool CustoJustoClient::request(
    const std::string& method,
    const std::string& endpoint,
    const std::string& body,
    std::string& response
) {
    response.clear();

    CURL* curl =
        curl_easy_init();

    if (curl == nullptr) {
        setError(
            "Не удалось "
            "инициализировать CURL."
        );

        return false;
    }

    std::string url =
        browserWorkerUrl_;

    if (
        !endpoint.empty() &&
        endpoint.front() != '/'
    ) {
        url += "/";
    }

    url += endpoint;

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_FOLLOWLOCATION,
        1L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_MAXREDIRS,
        5L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_CONNECTTIMEOUT,
        10L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_TIMEOUT,
        60L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        writeCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );

    curl_easy_setopt(
        curl,
        CURLOPT_USERAGENT,
        "CustoJustoCRM/1.0"
    );

    struct curl_slist* headers =
        nullptr;

    headers =
        curl_slist_append(
            headers,
            "Content-Type: application/json"
        );

    headers =
        curl_slist_append(
            headers,
            "Accept: application/json"
        );

    if (!workerSharedSecret_.empty()) {
        const std::string workerSecretHeader =
            "X-Worker-Secret: " + workerSharedSecret_;

        headers = curl_slist_append(
            headers,
            workerSecretHeader.c_str()
        );
    }

    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        headers
    );

    if (method == "POST") {
        curl_easy_setopt(
            curl,
            CURLOPT_POST,
            1L
        );

        curl_easy_setopt(
            curl,
            CURLOPT_POSTFIELDS,
            body.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_POSTFIELDSIZE,
            static_cast<long>(
                body.size()
            )
        );
    }

    const CURLcode code =
        curl_easy_perform(curl);

    long status = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &status
    );

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        setError(
            std::string(
                "Browser worker: "
            ) +
            curl_easy_strerror(code)
        );

        return false;
    }

    if (
        status < 200 ||
        status >= 300
    ) {
        setError(
            "Browser worker HTTP " +
            std::to_string(status) +
            ": " +
            response
        );

        return false;
    }

    return true;
}

bool CustoJustoClient::get(
    const std::string& endpoint,
    std::string& response
) {
    return request(
        "GET",
        endpoint,
        "",
        response
    );
}

bool CustoJustoClient::post(
    const std::string& endpoint,
    const std::string& body,
    std::string& response
) {
    return request(
        "POST",
        endpoint,
        body,
        response
    );
}

bool CustoJustoClient::parseBool(
    const std::string& json,
    const std::string& key
) const {
    const std::string marker =
        "\"" + key + "\":";

    const std::size_t pos =
        json.find(marker);

    if (
        pos ==
        std::string::npos
    ) {
        return false;
    }

    const std::size_t valuePos =
        pos + marker.size();

    return
        json.compare(
            valuePos,
            4,
            "true"
        ) == 0;
}

std::string
CustoJustoClient::parseString(
    const std::string& json,
    const std::string& key
) const {
    const std::string marker =
        "\"" + key + "\":\"";

    const std::size_t pos =
        json.find(marker);

    if (
        pos ==
        std::string::npos
    ) {
        return {};
    }

    const std::size_t begin =
        pos + marker.size();

    std::size_t end = begin;

    bool escaped = false;

    while (end < json.size()) {
        const char c =
            json[end];

        if (escaped) {
            escaped = false;
        }
        else if (c == '\\') {
            escaped = true;
        }
        else if (c == '"') {
            break;
        }

        ++end;
    }

    if (
        end >= json.size()
    ) {
        return {};
    }

    std::string value =
        json.substr(
            begin,
            end - begin
        );

    std::string decoded;

    for (
        std::size_t i = 0;
        i < value.size();
        ++i
    ) {
        if (
            value[i] == '\\' &&
            i + 1 < value.size()
        ) {
            ++i;

            switch (value[i]) {
                case '"':
                    decoded += '"';
                    break;

                case '\\':
                    decoded += '\\';
                    break;

                case 'n':
                    decoded += '\n';
                    break;

                case 'r':
                    decoded += '\r';
                    break;

                case 't':
                    decoded += '\t';
                    break;

                default:
                    decoded += value[i];
                    break;
            }
        }
        else {
            decoded += value[i];
        }
    }

    return decoded;
}

std::string
CustoJustoClient::jsonEscape(
    const std::string& value
) const {
    std::string result;

    result.reserve(
        value.size() + 16
    );

    for (unsigned char c : value) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;

            case '\\':
                result += "\\\\";
                break;

            case '\b':
                result += "\\b";
                break;

            case '\f':
                result += "\\f";
                break;

            case '\n':
                result += "\\n";
                break;

            case '\r':
                result += "\\r";
                break;

            case '\t':
                result += "\\t";
                break;

            default:
                if (c < 0x20) {
                    result += ' ';
                }
                else {
                    result +=
                        static_cast<char>(c);
                }
                break;
        }
    }

    return result;
}
