#include "CustoJustoClient.hpp"

#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

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

    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(
        static_cast<char*>(contents),
        total
    );

    return total;
}

size_t headerCallback(
    void* contents,
    size_t size,
    size_t nmemb,
    void* userp
) {
    const size_t total = size * nmemb;

    if (userp == nullptr) {
        return 0;
    }

    auto* headers = static_cast<std::string*>(userp);
    headers->append(
        static_cast<char*>(contents),
        total
    );

    return total;
}

std::string lowerCopy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    return value;
}

} // namespace

CustoJustoClient::CustoJustoClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

CustoJustoClient::~CustoJustoClient() {
    curl_global_cleanup();
}

void CustoJustoClient::setAccountId(long long accountId) {
    accountId_ = accountId;

    /*
     * Каждому аккаунту — отдельный cookie-файл.
     *
     * Это важно:
     * аккаунт №1 не должен использовать сессию аккаунта №2.
     */
    std::filesystem::create_directories("data/custojusto");

    cookieFile_ =
        "data/custojusto/account_" +
        std::to_string(accountId_) +
        ".cookies";
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

CustoJustoLoginResult CustoJustoClient::login(
    const std::string& email,
    const std::string& password
) {
    CustoJustoLoginResult result;

    lastError_.clear();
    loggedIn_ = false;

    if (email.empty()) {
        result.message = "Не указан email.";
        setError(result.message);
        return result;
    }

    if (password.empty()) {
        result.message = "Не указан пароль.";
        setError(result.message);
        return result;
    }

    if (baseUrl_.empty()) {
        result.message =
            "Не настроен адрес CustoJusto.";

        setError(result.message);
        return result;
    }

    if (!ensureCookieStorage()) {
        result.message =
            "Не удалось создать хранилище сессии.";

        setError(result.message);
        return result;
    }

    /*
     * Здесь намеренно НЕ подставляем выдуманный
     * endpoint авторизации.
     *
     * Реальная форма входа CustoJusto должна быть
     * определена отдельно после проверки текущей
     * страницы входа.
     */

    std::string response;

    if (!performGet(
            baseUrl_,
            response
        )) {

        result.message =
            "Не удалось открыть CustoJusto: " +
            lastError_;

        return result;
    }

    if (looksLikeCaptcha(response)) {
        result.requiresCaptcha = true;
        result.message =
            "CustoJusto запросил CAPTCHA.";

        setError(result.message);
        return result;
    }

    if (looksLikeTwoFactor(response)) {
        result.requiresTwoFactor = true;
        result.message =
            "CustoJusto запросил дополнительную "
            "проверку.";

        setError(result.message);
        return result;
    }

    result.message =
        "Страница CustoJusto доступна. "
        "Реальный endpoint авторизации ещё не настроен.";

    setError(result.message);

    return result;
}

bool CustoJustoClient::checkSession() {
    lastError_.clear();

    if (baseUrl_.empty()) {
        setError(
            "Не настроен адрес CustoJusto."
        );

        loggedIn_ = false;
        return false;
    }

    if (!ensureCookieStorage()) {
        loggedIn_ = false;
        return false;
    }

    std::string response;

    if (!performGet(
            baseUrl_,
            response
        )) {

        loggedIn_ = false;
        return false;
    }

    if (looksLikeCaptcha(response)) {
        loggedIn_ = false;

        setError(
            "CustoJusto запросил CAPTCHA."
        );

        return false;
    }

    if (looksLikeTwoFactor(response)) {
        loggedIn_ = false;

        setError(
            "CustoJusto запросил дополнительную "
            "проверку."
        );

        return false;
    }

    loggedIn_ =
        looksLikeLoggedInPage(response);

    if (!loggedIn_) {
        setError(
            "Авторизованная сессия CustoJusto "
            "не подтверждена."
        );
    }

    return loggedIn_;
}

void CustoJustoClient::logout() {
    loggedIn_ = false;

    if (!cookieFile_.empty()) {
        std::error_code ec;

        std::filesystem::remove(
            cookieFile_,
            ec
        );
    }
}

bool CustoJustoClient::isLoggedIn() const {
    return loggedIn_;
}

std::vector<CustoJustoConversation>
CustoJustoClient::getConversations() {
    /*
     * Пока возвращаем пустой список.
     *
     * После определения официального/фактического
     * Chat endpoint подключим получение диалогов.
     */

    lastError_ =
        "Получение диалогов CustoJusto "
        "ещё не подключено.";

    return {};
}

std::vector<CustoJustoMessage>
CustoJustoClient::getMessages(
    const std::string& conversationId
) {
    (void)conversationId;

    lastError_ =
        "Получение сообщений CustoJusto "
        "ещё не подключено.";

    return {};
}

bool CustoJustoClient::sendMessage(
    const std::string& conversationId,
    const std::string& text
) {
    (void)conversationId;
    (void)text;

    lastError_ =
        "Отправка сообщений CustoJusto "
        "ещё не подключена.";

    return false;
}

bool CustoJustoClient::openListing(
    const std::string& listingUrl
) {
    lastError_.clear();

    if (listingUrl.empty()) {
        setError(
            "Ссылка на объявление пустая."
        );

        return false;
    }

    /*
     * Проверяем URL только как URL.
     * Никаких попыток использовать чужие
     * cookies, токены или обходить защиту сайта.
     */

    const std::string lower =
        lowerCopy(listingUrl);

    if (
        lower.rfind("https://", 0) != 0 &&
        lower.rfind("http://", 0) != 0
    ) {
        setError(
            "Ссылка должна начинаться с "
            "http:// или https://."
        );

        return false;
    }

    return true;
}

std::string CustoJustoClient::getLastError() const {
    return lastError_;
}

void CustoJustoClient::setError(
    const std::string& error
) {
    lastError_ = error;
}

bool CustoJustoClient::ensureCookieStorage() {
    if (accountId_ <= 0) {
        setError(
            "Не задан ID аккаунта CustoJusto."
        );

        return false;
    }

    std::filesystem::create_directories(
        "data/custojusto"
    );

    if (cookieFile_.empty()) {
        cookieFile_ =
            "data/custojusto/account_" +
            std::to_string(accountId_) +
            ".cookies";
    }

    std::ofstream file(
        cookieFile_,
        std::ios::app
    );

    if (!file.is_open()) {
        setError(
            "Не удалось открыть cookie-хранилище."
        );

        return false;
    }

    return true;
}

bool CustoJustoClient::performRequest(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    std::string& response
) {
    response.clear();

    CURL* curl = curl_easy_init();

    if (curl == nullptr) {
        setError(
            "Не удалось инициализировать CURL."
        );

        return false;
    }

    std::string headers;

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

    /*
     * Ограничиваем количество редиректов,
     * чтобы кривой URL не создал бесконечный цикл.
     */
    curl_easy_setopt(
        curl,
        CURLOPT_MAXREDIRS,
        10L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_CONNECTTIMEOUT,
        15L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_TIMEOUT,
        45L
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
        CURLOPT_HEADERFUNCTION,
        headerCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_HEADERDATA,
        &headers
    );

    curl_easy_setopt(
        curl,
        CURLOPT_COOKIEFILE,
        cookieFile_.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_COOKIEJAR,
        cookieFile_.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_USERAGENT,
        "TelegramCRM/1.0"
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
            static_cast<long>(body.size())
        );
    }

    CURLcode code =
        curl_easy_perform(curl);

    long httpCode = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &httpCode
    );

    char* effectiveUrl = nullptr;

    curl_easy_getinfo(
        curl,
        CURLINFO_EFFECTIVE_URL,
        &effectiveUrl
    );

    if (code != CURLE_OK) {
        setError(
            std::string("CURL: ") +
            curl_easy_strerror(code)
        );

        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_cleanup(curl);

    if (httpCode >= 400) {
        setError(
            "CustoJusto вернул HTTP " +
            std::to_string(httpCode)
        );

        return false;
    }

    if (effectiveUrl != nullptr) {
        const std::string finalUrl(
            effectiveUrl
        );

        if (
            finalUrl.empty()
        ) {
            setError(
                "CustoJusto вернул пустой URL."
            );

            return false;
        }
    }

    return true;
}

bool CustoJustoClient::performGet(
    const std::string& url,
    std::string& response
) {
    return performRequest(
        "GET",
        url,
        "",
        response
    );
}

bool CustoJustoClient::performPost(
    const std::string& url,
    const std::string& body,
    std::string& response
) {
    return performRequest(
        "POST",
        url,
        body,
        response
    );
}

std::string CustoJustoClient::urlEncode(
    const std::string& value
) {
    CURL* curl = curl_easy_init();

    if (curl == nullptr) {
        return "";
    }

    char* encoded =
        curl_easy_escape(
            curl,
            value.c_str(),
            static_cast<int>(value.size())
        );

    std::string result;

    if (encoded != nullptr) {
        result = encoded;
        curl_free(encoded);
    }

    curl_easy_cleanup(curl);

    return result;
}

std::string CustoJustoClient::trim(
    const std::string& value
) {
    std::size_t begin = 0;

    while (
        begin < value.size() &&
        std::isspace(
            static_cast<unsigned char>(
                value[begin]
            )
        )
    ) {
        ++begin;
    }

    std::size_t end = value.size();

    while (
        end > begin &&
        std::isspace(
            static_cast<unsigned char>(
                value[end - 1]
            )
        )
    ) {
        --end;
    }

    return value.substr(
        begin,
        end - begin
    );
}

bool CustoJustoClient::looksLikeLoginPage(
    const std::string& html
) {
    const std::string lower =
        lowerCopy(html);

    return
        lower.find("login") !=
            std::string::npos ||
        lower.find("entrar") !=
            std::string::npos ||
        lower.find("password") !=
            std::string::npos ||
        lower.find("palavra-passe") !=
            std::string::npos;
}

bool CustoJustoClient::looksLikeLoggedInPage(
    const std::string& html
) {
    const std::string lower =
        lowerCopy(html);

    /*
     * Временная консервативная проверка.
     *
     * Не считаем аккаунт вошедшим только потому,
     * что страница открылась.
     */
    const bool hasChat =
        lower.find("chat") !=
        std::string::npos;

    const bool hasLogout =
        lower.find("logout") !=
            std::string::npos ||
        lower.find("sair") !=
            std::string::npos;

    const bool hasAccount =
        lower.find("my account") !=
            std::string::npos ||
        lower.find("minha conta") !=
            std::string::npos;

    return hasChat &&
           (hasLogout || hasAccount);
}

bool CustoJustoClient::looksLikeCaptcha(
    const std::string& html
) {
    const std::string lower =
        lowerCopy(html);

    return
        lower.find("captcha") !=
            std::string::npos ||
        lower.find("recaptcha") !=
            std::string::npos ||
        lower.find("hcaptcha") !=
            std::string::npos;
}

bool CustoJustoClient::looksLikeTwoFactor(
    const std::string& html
) {
    const std::string lower =
        lowerCopy(html);

    return
        lower.find("two-factor") !=
            std::string::npos ||
        lower.find("2fa") !=
            std::string::npos ||
        lower.find("verification code") !=
            std::string::npos ||
        lower.find("codigo de verificacao") !=
            std::string::npos;
}
