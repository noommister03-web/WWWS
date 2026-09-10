#include "CustoJustoClient.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <stdexcept>
#include <utility>

using json = nlohmann::json;

namespace {
size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const auto total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<const char*>(contents), total);
    return total;
}

bool validCustoJustoUrl(const std::string& value) {
    static const std::regex expression(
        R"(^https://([A-Za-z0-9-]+\.)*custojusto\.pt(?::443)?(?:/|$))",
        std::regex::icase
    );
    return std::regex_search(value, expression);
}

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}
}

CustoJustoClient::CustoJustoClient() {
    if (const char* value = std::getenv("BROWSER_WORKER_URL"); value && *value) {
        workerBaseUrl_ = trimTrailingSlash(value);
    }
    if (const char* value = std::getenv("BROWSER_WORKER_SHARED_SECRET"); value && *value) {
        workerSharedSecret_ = value;
    }
}

// libcurl is initialized once by the process owner (TelegramBot). A client must
// never call curl_global_cleanup while other parts of the process still use it.
CustoJustoClient::~CustoJustoClient() = default;

void CustoJustoClient::setAccountId(long long accountId) { accountId_ = accountId; }

void CustoJustoClient::setBaseUrl(const std::string& baseUrl) {
    if (!baseUrl.empty() && !validCustoJustoUrl(baseUrl)) {
        baseUrl_.clear();
        setError("Разрешены только HTTPS-ссылки custojusto.pt.");
        return;
    }
    baseUrl_ = trimTrailingSlash(baseUrl);
}

CustoJustoLoginResult CustoJustoClient::login(const std::string& email, const std::string& password) {
    CustoJustoLoginResult result;
    lastError_.clear();
    loggedIn_ = false;
    if (accountId_ <= 0) { result.state="invalid_account"; result.message="Не задан ID аккаунта."; setError(result.message); return result; }
    if (email.empty()) { result.state="invalid_email"; result.message="Не указан email."; setError(result.message); return result; }
    if (password.empty()) { result.state="invalid_password"; result.message="Не указан пароль."; setError(result.message); return result; }
    json request={{"accountId",accountId_},{"email",email},{"password",password},{"baseUrl",baseUrl_}};
    std::string response;
    if (!post("/login", request.dump(), response)) { result.state="worker_unavailable"; result.message=lastError_; return result; }
    try {
        const auto data=json::parse(response);
        result.loggedIn=data.value("loggedIn",false);
        result.state=data.value("state","");
        result.message=data.value("message","");
        result.requiresCaptcha=data.value("requiresCaptcha",false);
        result.requiresTwoFactor=data.value("requiresTwoFactor",false);
    } catch (...) { result.state="invalid_response"; result.message="Browser worker вернул некорректный ответ."; setError(result.message); return result; }
    if (result.message.empty()) result.message=result.loggedIn?"Вход выполнен.":"Не удалось подтвердить вход.";
    loggedIn_=result.loggedIn;
    return result;
}

bool CustoJustoClient::checkSession() {
    lastError_.clear(); loggedIn_=false;
    if (accountId_<=0) { setError("Не задан ID аккаунта."); return false; }
    std::string response;
    if (!post("/status",json({{"accountId",accountId_},{"baseUrl",baseUrl_}}).dump(),response)) return false;
    try { loggedIn_=json::parse(response).value("loggedIn",false); } catch (...) { setError("Browser worker вернул некорректный статус."); }
    return loggedIn_;
}

void CustoJustoClient::logout() { loggedIn_=false; }
bool CustoJustoClient::isLoggedIn() const { return loggedIn_; }

std::vector<CustoJustoConversation> CustoJustoClient::getConversations(bool fullScan) {
    lastError_.clear(); std::vector<CustoJustoConversation> result;
    if (accountId_<=0) { setError("Не задан ID аккаунта."); return result; }
    std::string response;
    if (!post("/conversations",json({{"accountId",accountId_},{"baseUrl",baseUrl_},{"fullScan",fullScan}}).dump(),response)) return result;
    try {
        const auto data=json::parse(response);
        if (!data.is_array()) throw std::runtime_error("not an array");
        for (const auto& item:data) {
            CustoJustoConversation value;
            value.id=item.value("id",""); value.url=item.value("url",""); value.title=item.value("title","");
            value.listingUrl=item.value("listingUrl",""); value.listingTitle=item.value("listingTitle",""); value.buyerName=item.value("buyerName","");
            value.lastMessage=item.value("lastMessage",""); value.lastMessageId=item.value("lastMessageId",""); value.lastMessageAt=item.value("lastMessageAt",""); value.unread=item.value("unread",false);
            if (!value.url.empty() && validCustoJustoUrl(value.url)) result.push_back(std::move(value));
        }
    } catch (...) { setError("Browser worker вернул некорректный список диалогов."); }
    return result;
}

std::vector<CustoJustoMessage> CustoJustoClient::getMessages(const std::string& conversationUrl, bool fullHistory) {
    lastError_.clear(); std::vector<CustoJustoMessage> result;
    if (accountId_<=0) { setError("Не задан ID аккаунта."); return result; }
    if (!validCustoJustoUrl(conversationUrl)) { setError("Разрешены только HTTPS-ссылки диалогов custojusto.pt."); return result; }
    std::string response;
    if (!post("/messages",json({{"accountId",accountId_},{"conversationUrl",conversationUrl},{"fullHistory",fullHistory}}).dump(),response)) return result;
    try {
        const auto data=json::parse(response);
        if (!data.is_array()) throw std::runtime_error("not an array");
        for (const auto& item:data) {
            CustoJustoMessage value;
            value.id=item.value("id",""); value.conversationId=item.value("conversationId",""); value.sender=item.value("sender","");
            value.text=item.value("text",""); value.timestamp=item.value("timestamp",""); value.incoming=item.value("incoming",true);
            if (!value.id.empty()&&!value.text.empty()) result.push_back(std::move(value));
        }
    } catch (...) { setError("Browser worker вернул некорректные сообщения."); }
    return result;
}

bool CustoJustoClient::sendMessage(const std::string& conversationUrl,const std::string& text) {
    lastError_.clear();
    if (accountId_<=0) { setError("Не задан ID аккаунта."); return false; }
    if (!validCustoJustoUrl(conversationUrl)) { setError("Разрешены только HTTPS-ссылки диалогов custojusto.pt."); return false; }
    if (text.empty()) { setError("Не указан текст сообщения."); return false; }
    std::string response;
    if (!post("/send",json({{"accountId",accountId_},{"conversationUrl",conversationUrl},{"text",text}}).dump(),response)) return false;
    try { if (json::parse(response).value("ok",false)) return true; } catch (...) {}
    setError("Browser worker не подтвердил отправку сообщения."); return false;
}

bool CustoJustoClient::getListing(const std::string& listingUrl,CustoJustoListing& listing) {
    listing={}; lastError_.clear();
    if (!validCustoJustoUrl(listingUrl)) { setError("Разрешены только HTTPS-ссылки объявлений custojusto.pt."); return false; }
    std::string response;
    if (!post("/listing",json({{"listingUrl",listingUrl}}).dump(),response)) return false;
    try {
        const auto data=json::parse(response);
        listing.url=data.value("url",""); listing.title=data.value("title",""); listing.price=data.value("price","");
        listing.sellerName=data.value("sellerName",""); listing.location=data.value("location","");
        return !listing.url.empty()&&validCustoJustoUrl(listing.url);
    } catch (...) { setError("Browser worker вернул некорректные данные объявления."); return false; }
}

bool CustoJustoClient::openListing(const std::string& listingUrl) { CustoJustoListing listing; return getListing(listingUrl,listing); }
std::string CustoJustoClient::getLastError() const { return lastError_; }
void CustoJustoClient::setError(const std::string& error) { lastError_=error; }

bool CustoJustoClient::request(const std::string& method,const std::string& endpoint,const std::string& body,std::string& response) {
    response.clear();
    if (workerBaseUrl_.empty()) { setError("BROWSER_WORKER_URL не настроен."); return false; }
    CURL* curl=curl_easy_init();
    if (!curl) { setError("Не удалось инициализировать HTTP-клиент."); return false; }
    const std::string url=workerBaseUrl_+endpoint;
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str()); curl_easy_setopt(curl,CURLOPT_CUSTOMREQUEST,method.c_str());
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L); curl_easy_setopt(curl,CURLOPT_TIMEOUT,360L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writeCallback); curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"CustoJustoCRM/1.0"); curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_slist* headers=nullptr; headers=curl_slist_append(headers,"Content-Type: application/json"); headers=curl_slist_append(headers,"Accept: application/json");
    if (!workerSharedSecret_.empty()) { const std::string header="X-Worker-Secret: "+workerSharedSecret_; headers=curl_slist_append(headers,header.c_str()); }
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    if (method=="POST") { curl_easy_setopt(curl,CURLOPT_POST,1L); curl_easy_setopt(curl,CURLOPT_POSTFIELDS,body.c_str()); curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,static_cast<long>(body.size())); }
    const CURLcode code=curl_easy_perform(curl); long status=0; curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    curl_slist_free_all(headers); curl_easy_cleanup(curl);
    if (code!=CURLE_OK) { setError(std::string("Browser worker: ")+curl_easy_strerror(code)); return false; }
    if (status<200||status>=300) { setError("Browser worker HTTP "+std::to_string(status)+": "+response.substr(0,1000)); return false; }
    return true;
}

bool CustoJustoClient::get(const std::string& endpoint,std::string& response) { return request("GET",endpoint,"",response); }
bool CustoJustoClient::post(const std::string& endpoint,const std::string& body,std::string& response) { return request("POST",endpoint,body,response); }
bool CustoJustoClient::parseBool(const std::string& value,const std::string& key) const { try { return json::parse(value).value(key,false); } catch (...) { return false; } }
std::string CustoJustoClient::parseString(const std::string& value,const std::string& key) const { try { return json::parse(value).value(key,""); } catch (...) { return ""; } }
std::string CustoJustoClient::jsonEscape(const std::string& value) const { return json(value).dump().substr(1,json(value).dump().size()-2); }
