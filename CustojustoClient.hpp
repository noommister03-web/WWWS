#pragma once
#include <string>
#include <vector>
struct CustoJustoLoginResult {
    bool loggedIn = false;
    bool requiresCaptcha = false;
    bool requiresTwoFactor = false;
    std::string state;
    std::string message;
};
struct CustoJustoConversation {
    std::string id;
    std::string title;
    std::string url;
    std::string lastMessage;
};
struct CustoJustoMessage {
    std::string id;
    std::string sender;
    std::string text;
    std::string timestamp;
};
class CustoJustoClient {
public:
    CustoJustoClient();
    ~CustoJustoClient();
    void setAccountId(long long accountId);
    void setBaseUrl(const std::string& baseUrl);
    CustoJustoLoginResult login(
        const std::string& email,
        const std::string& password
    );
    bool checkSession();
    void logout();
    bool isLoggedIn() const;
    std::vector<CustoJustoConversation> getConversations();
    std::vector<CustoJustoMessage> getMessages(
        const std::string& conversationId
    );
    bool sendMessage(
        const std::string& conversationId,
        const std::string& text
    );
    bool openListing(
        const std::string& listingUrl
    );
    std::string getLastError() const;
private:
    bool request(
        const std::string& method,
        const std::string& endpoint,
        const std::string& body,
        std::string& response
    );
    bool get(
        const std::string& endpoint,
        std::string& response
    );
    bool post(
        const std::string& endpoint,
        const std::string& body,
        std::string& response
    );
    bool parseBool(
        const std::string& json,
        const std::string& key
    ) const;
    std::string parseString(
        const std::string& json,
        const std::string& key
    ) const;
    std::string jsonEscape(
        const std::string& value
    ) const;
    void setError(
        const std::string& error
    );
private:
    long long accountId_ = 0;
    std::string baseUrl_;
    std::string browserWorkerUrl_;
    std::string lastError_;
    bool loggedIn_ = false;
};
