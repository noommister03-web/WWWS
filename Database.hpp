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
    std::string loginUrl;
    bool enabled = true;
    bool loggedIn = false;
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
        const std::string& loginUrl
    );

    std::vector<CustoJustoAccount>
    getCustoJustoAccounts();

    std::optional<CustoJustoAccount>
    getCustoJustoAccount(long long id);

    bool deleteCustoJustoAccount(long long id);

    bool setCustoJustoAccountEnabled(
        long long id,
        bool enabled
    );

    bool setCustoJustoAccountLoggedIn(
        long long id,
        bool loggedIn
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
    );
};
