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

private:
    sqlite3* db_ = nullptr;

    void initialize();

    void execute(const std::string& sql);

    bool hasColumn(
        const std::string& table,
        const std::string& column
    );
};
