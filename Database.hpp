#pragma once

#include <optional>
#include <string>
#include <vector>

struct sqlite3;

struct MessageRecord {
    long long id = 0;
    long long chatId = 0;

    std::string senderId;
    std::string username;
    std::string text;

    bool incoming = true;

    long long timestamp = 0;

    std::optional<long long> updateId;
};

struct CustoJustoAccount {
    long long id = 0;

    std::string name;
    std::string email;
    std::string loginUrl = "https://www.custojusto.pt";

    bool enabled = true;
    bool loggedIn = false;

    long long createdAt = 0;
};

struct CustoJustoConversationRecord {
    long long id = 0;
    long long accountId = 0;

    std::string conversationUrl;
    std::string listingUrl;
    std::string listingTitle;

    std::string contactName;

    std::string lastMessageId;
    std::string lastMessageText;
    long long lastMessageAt = 0;

    bool unread = false;

    long long createdAt = 0;
    long long updatedAt = 0;
};

struct CustoJustoMessageRecord {
    long long id = 0;
    long long accountId = 0;
    long long conversationId = 0;

    std::string externalMessageId;
    std::string senderName;

    std::string originalText;
    std::string translatedText;

    bool incoming = true;

    long long createdAt = 0;
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool isUpdateProcessed(long long updateId);

    void markUpdateProcessed(long long updateId);

    void saveMessage(
        long long chatId,
        const std::string& senderId,
        const std::string& username,
        const std::string& text,
        bool incoming,
        std::optional<long long> updateId = std::nullopt
    );

    std::vector<MessageRecord> getHistory(
        long long chatId,
        int limit
    );

    long long addCustoJustoAccount(
        const std::string& name,
        const std::string& email,
        const std::string& loginUrl = "https://www.custojusto.pt"
    );

    int getCustoJustoAccountCount();

    std::vector<CustoJustoAccount> getCustoJustoAccounts();

    std::optional<CustoJustoAccount> getCustoJustoAccount(
        long long id
    );

    bool deleteCustoJustoAccount(long long id);

    bool setCustoJustoAccountEnabled(
        long long id,
        bool enabled
    );

    bool setCustoJustoAccountLoggedIn(
        long long id,
        bool loggedIn
    );

    std::optional<CustoJustoConversationRecord>
    getCustoJustoConversationByUrl(
        long long accountId,
        const std::string& conversationUrl
    );

    long long upsertCustoJustoConversation(
        long long accountId,
        const std::string& conversationUrl,
        const std::string& listingUrl,
        const std::string& listingTitle,
        const std::string& contactName,
        const std::string& lastMessageId,
        const std::string& lastMessageText,
        long long lastMessageAt,
        bool unread
    );

    std::optional<CustoJustoConversationRecord>
    getCustoJustoConversation(
        long long conversationId
    );

    bool hasCustoJustoExternalMessage(
        long long accountId,
        const std::string& externalMessageId
    );

    long long saveCustoJustoMessage(
        long long accountId,
        long long conversationId,
        const std::string& externalMessageId,
        const std::string& senderName,
        const std::string& originalText,
        const std::string& translatedText,
        bool incoming
    );

private:
    sqlite3* db_ = nullptr;

    void initialize();

    void execute(
        const std::string& sql
    );

    bool hasColumn(
        const std::string& table,
        const std::string& column
