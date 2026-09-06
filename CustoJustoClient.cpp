#include "CustoJustoClient.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

size_t writeCallback(
    void* contents,
    size_t size,
    size_t nmemb,
    void* userp
) {
    const auto totalSize = size * nmemb;

    auto* output = static_cast<std::string*>(userp);

    output->append(
        static_cast<const char*>(contents),
        totalSize
    );

    return totalSize;
}

std::string trim(std::string value) {
    const auto notSpace = [] (unsigned char ch) {
        return !std::isspace(ch);
    };

    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            notSpace
        )
    );

    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            notSpace
        ).base(),
        value.end()
    );

    return value;
}

}

CustoJustoClient::CustoJustoClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const char* baseUrl = std::getenv("BROWSER_WORKER_URL");

    if (baseUrl != nullptr && baseUrl[0] != '\0') {
        workerBaseUrl_ = baseUrl;
    }

    const char* secret =
        std::getenv("BROWSER_WORKER_SHARED_SECRET");

    if (secret != nullptr && secret[0] != '\0') {
        workerSharedSecret_ = secret;
    }
}

CustoJustoClient::~CustoJustoClient() {
    curl_global_cleanup();
}

void CustoJustoClient::setAccountId(long long accountId) {
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
        parseBool(
            response,
            "requiresCaptcha"
        );

    result.requiresTwoFactor =
        parseBool(
            response,
            "requiresTwoFactor"
        );

    if (result.message.empty()) {
        if (result.loggedIn) {
            result.message = "Вход выполнен.";
        }
        else {
            result.message = "Не удалось подтвердить вход.";
        }
    }

    loggedIn_ = result.loggedIn;

    return result;
}

bool CustoJustoClient::checkSession() {
    lastError_.clear();
    loggedIn_ = false;

    if (accountId_ <= 0) {
        setError("Не задан ID аккаунта.");
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
            "/status",
            body.str(),
            response
        )) {
        return false;
    }

    loggedIn_ = parseBool(response, "loggedIn");

    return loggedIn_;
}

void CustoJustoClient::logout() {
    loggedIn_ = false;
}

bool CustoJustoClient::isLoggedIn() const {
    return loggedIn_;
}

std::vector<CustoJustoConversation>
CustoJustoClient::getConversations() {
    lastError_.clear();

    std::vector<CustoJustoConversation> result;

    if (accountId_ <= 0) {
        setError("Не задан ID аккаунта.");
        return result;
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
            "/conversations",
            body.str(),
            response
        )) {
        return result;
    }

    try {
        const auto json = nlohmann::json::parse(response);

        if (!json.is_array()) {
            setError("Browser worker вернул некорректный список диалогов.");
            return result;
        }

        for (const auto& item : json) {
            CustoJustoConversation conversation;

            conversation.id =
                item.value("id", "");

            conversation.url =
                item.value("url", "");

            conversation.title =
                item.value("title", "");

            conversation.listingUrl =
                item.value("listingUrl", "");

            conversation.listingTitle =
                item.value("listingTitle", "");

            conversation.buyerName =
                item.value("buyerName", "");

            conversation.lastMessage =
                item.value("lastMessage", "");

            conversation.lastMessageId =
                item.value("lastMessageId", "");

            conversation.lastMessageAt =
                item.value("lastMessageAt", "");

            conversation.unread =
                item.value("unread", false);

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
    const std::string& conversationUrl
) {
    lastError_.clear();

    std::vector<CustoJustoMessage> result;

    if (accountId_ <= 0) {
        setError("Не задан ID аккаунта.");
        return result;
    }

    if (conversationUrl.empty()) {
        setError("Не указан URL диалога.");
        return result;
    }

    std::ostringstream body;

    body
        << "{"
        << "\"accountId\":"
        << accountId_
        << ","
        << "\"conversationUrl\":\""
        << jsonEscape(conversationUrl)
        << "\""
        << "}";

    std::string response;

    if (!post(
            "/messages",
            body.str(),
            response
        )) {
        return result;
    }

    try {
        const auto json = nlohmann::json::parse(response);

        if (!json.is_array()) {
            setError("Browser worker вернул некорректные сообщения.");
            return result;
        }

        for (const auto& item : json) {
            CustoJustoMessage message;

            message.id =
                item.value("id", "");

            message.conversationId =
                item.value("conversationId", "");

            message.sender =
                item.value("sender", "");

            message.text =
                item.value("text", "");

            message.timestamp =
                item.value("timestamp", "");

            message.incoming =
                item.value("incoming", true);

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
    const std::string& conversationUrl,
    const std::string& text
) {
    lastError_.clear();

    if (accountId_ <= 0) {
        setError("Не задан ID аккаунта.");
        return false;
    }

    if (conversationUrl.empty()) {
        setError("Не указан URL диалога.");
        return false;
    }

    if (text.empty()) {
        setError("Не указан текст сообщения.");
        return false;
    }

    std::ostringstream body;

    body
        << "{"
        << "\"accountId\":"
        << accountId_
        << ","
        << "\"conversationUrl\":\""
        << jsonEscape(conversationUrl)
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

    if (!parseBool(response, "ok")) {
        setError("Browser worker не подтвердил отправку сообщения.");
        return false;
    }

    return true;
}

bool CustoJustoClient::getListing(
    const std::string& listingUrl,
    CustoJustoListing& listing
) {
    listing = {};
    lastError_.clear();

    if (listingUrl.empty()) {
        setError("Не указана ссылка на объявление.");
        return false;
    }

    std::ostringstream body;

    body
        << "{"
        << "\"listingUrl\":\""
        << jsonEscape(listingUrl)
        << "\""
        << "}";

    std::string response;

    if (!post(
            "/listing",
            body.str(),
            response
        )) {
        return false;
    }

    try {
        const auto json = nlohmann::json::parse(response);

        listing.url = json.value("url", "");
        listing.title = json.value("title", "");
        listing.price = json.value("price", "");
        listing.sellerName = json.value("sellerName", "");
        listing.location = json.value("location", "");

        return !listing.url.empty();
    } catch (const std::exception&) {
        setError("Browser worker вернул некорректные данные объявления.");
        return false;
    }
}

bool CustoJustoClient::openListing(
    const std::string& listingUrl
) {
    CustoJustoListing listing;

    return getListing(listingUrl, listing);
}

std::string CustoJustoClient::getLastError() const {
    return lastError_;
}

void CustoJustoClient::setError(const std::string& error) {
    lastError_ = error;
}

bool CustoJustoClient::request(
    const std::string& method,
    const std::string& endpoint,
    const std::string& body,
    std::string& response
) {
    response.clear();

    if (workerBaseUrl_.empty()) {
        setError("BROWSER_WORKER_URL не настроен.");
        return false;
    }

    CURL* curl = curl_easy_init();

    if (curl == nullptr) {
        setError("Не удалось инициализировать HTTP-клиент.");
        return false;
    }

    const std::string url = workerBaseUrl_ + endpoint;

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_CUSTOMREQUEST,
        method.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_CONNECTTIMEOUT,
        10L
    );

    // Chromium's first launch on a small Railway instance can take more than
    // one minute, especially immediately after deployment. Keep the bot's
    // request alive long enough for the browser worker to complete login.
    curl_easy_setopt(
        curl,
        CURLOPT_TIMEOUT,
        180L
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
    return request("GET", endpoint, "", response);
}

bool CustoJustoClient::post(
    const std::string& endpoint,
    const std::string& body,
    std::string& response
) {
    return request("POST", endpoint, body, response);
}

bool CustoJustoClient::parseBool(
    const std::string& json,
    const std::string& key
) const {
    const std::regex expression(
        "\\\"" + key + "\\\"\\s*:\\s*(true|false)"
    );

    std::smatch match;

    if (!std::regex_search(json, match, expression)) {
        return false;
    }

    return match[1] == "true";
}

std::string CustoJustoClient::parseString(
    const std::string& json,
    const std::string& key
) const {
    const std::regex expression(
        "\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\""
    );

    std::smatch match;

    if (!std::regex_search(json, match, expression)) {
        return "";
    }

    return match[1];
}

std::string CustoJustoClient::jsonEscape(
    const std::string& value
) const {
    std::ostringstream escaped;

    for (const auto ch : value) {
        switch (ch) {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default: escaped << ch; break;
        }
    }

    return escaped.str();
}
