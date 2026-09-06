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

struct CustoJustoListing {
    std::string url;
    std::string title;
    std::string price;
    std::string sellerName;
    std::string location;
};

struct CustoJustoConversation {
    std::string id;
    std::string url;

    std::string title;
    std::string listingUrl;
    std::string listingTitle;

    std::string buyerName;
    std::string lastMessage;
    std::string lastMessageId;
    std::string lastMessageAt;

    bool unread = false;
};

struct CustoJustoMessage {
    std::string id;
    std::string conversationId;

    std::string sender;
    std::string text;
    std::string timestamp;

    bool incoming = true;
};

class CustoJustoClient {
public:
    CustoJustoClient();
    ~CustoJustoClient();

    CustoJustoClient(const CustoJustoClient&) = delete;
    CustoJustoClient& operator=(const CustoJustoClient&) = delete;

    void setAccountId(long long accountId);

    bool checkSession();

    CustoJustoLoginResult login(
        const std::string& email,
        const std::string& password
    );

    void logout();

    bool isLoggedIn() const;

    std::vector<CustoJustoConversation> getConversations();

    std::vector<CustoJustoMessage> getMessages(
        const std::string& conversationUrl
    );

    bool sendMessage(
        const std::string& conversationUrl,
        const std::string& text
    );

    bool getListing(
        const std::string& listingUrl,
        CustoJustoListing& listing
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

    long long accountId_ = 0;

    std::string browserWorkerUrl_;
    std::string workerSharedSecret_;

    std::string lastError_;

    bool loggedIn_ = false;
};
