#include "Database.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void checkSqlite(
    int result,
    sqlite3* db,
    const std::string& operation
) {
    if (result != SQLITE_OK &&
        result != SQLITE_DONE &&
        result != SQLITE_ROW) {

        std::string message = operation;

        if (db) {
            message += ": ";
            message += sqlite3_errmsg(db);
        }

        throw std::runtime_error(message);
    }
}

}

Database::Database(const std::string& path) {
    std::filesystem::path dbPath(path);

    if (dbPath.has_parent_path()) {
        std::filesystem::create_directories(
            dbPath.parent_path()
        );
    }

    int result = sqlite3_open(
        path.c_str(),
        &db_
    );

    if (result != SQLITE_OK) {
        std::string message =
            "Unable to open SQLite database";

        if (db_) {
            message += ": ";
            message += sqlite3_errmsg(db_);
        }

        throw std::runtime_error(message);
    }

    execute("PRAGMA journal_mode=WAL;");
    execute("PRAGMA synchronous=NORMAL;");
    execute("PRAGMA foreign_keys=ON;");
    execute("PRAGMA busy_timeout=5000;");

    initialize();
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::execute(const std::string& sql) {
    char* error = nullptr;

    int result = sqlite3_exec(
        db_,
        sql.c_str(),
        nullptr,
        nullptr,
        &error
    );

    if (result != SQLITE_OK) {
        std::string message =
            error ? error : "SQLite error";

        sqlite3_free(error);

        throw std::runtime_error(message);
    }
}

bool Database::hasColumn(
    const std::string& table,
    const std::string& column
) {
    sqlite3_stmt* statement = nullptr;

    const std::string sql =
        "PRAGMA table_info(" + table + ");";

    int result = sqlite3_prepare_v2(
        db_,
        sql.c_str(),
        -1,
        &statement,
        nullptr
    );

    checkSqlite(
        result,
        db_,
        "PRAGMA table_info"
    );

    bool found = false;

    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* name =
            sqlite3_column_text(statement, 1);

        if (name &&
            column == reinterpret_cast<const char*>(name)) {
            found = true;
            break;
        }
    }

    sqlite3_finalize(statement);

    return found;
}

void Database::initialize() {
    execute(R"SQL(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            update_id INTEGER,
            chat_id INTEGER NOT NULL,
            sender_id TEXT NOT NULL DEFAULT '',
            username TEXT NOT NULL DEFAULT '',
            text TEXT NOT NULL,
            incoming INTEGER NOT NULL,
            timestamp INTEGER NOT NULL
        );
    )SQL");

    if (!hasColumn("messages", "update_id")) {
        execute(
            "ALTER TABLE messages "
            "ADD COLUMN update_id INTEGER;"
        );
    }

    if (!hasColumn("messages", "sender_id")) {
        execute(
            "ALTER TABLE messages "
            "ADD COLUMN sender_id TEXT NOT NULL DEFAULT '';"
        );
    }

    if (!hasColumn("messages", "username")) {
        execute(
            "ALTER TABLE messages "
            "ADD COLUMN username TEXT NOT NULL DEFAULT '';"
        );
    }

    execute(R"SQL(
        CREATE UNIQUE INDEX IF NOT EXISTS
        idx_messages_update_id
        ON messages(update_id);
    )SQL");

    execute(R"SQL(
        CREATE INDEX IF NOT EXISTS
        idx_messages_chat_id
        ON messages(chat_id, id);
    )SQL");

    execute(R"SQL(
        CREATE TABLE IF NOT EXISTS processed_updates (
            update_id INTEGER PRIMARY KEY
        );
    )SQL");
}

bool Database::isUpdateProcessed(
    long long updateId
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql =
        "SELECT 1 FROM processed_updates "
        "WHERE update_id = ? LIMIT 1;";

    int result = sqlite3_prepare_v2(
        db_,
        sql,
        -1,
        &statement,
        nullptr
    );

    checkSqlite(
        result,
        db_,
        "prepare processed update query"
    );

    sqlite3_bind_int64(
        statement,
        1,
        updateId
    );

    result = sqlite3_step(statement);

    bool processed =
        result == SQLITE_ROW;

    sqlite3_finalize(statement);

    return processed;
}

void Database::markUpdateProcessed(
    long long updateId
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql =
        "INSERT OR IGNORE INTO "
        "processed_updates(update_id) VALUES(?);";

    int result = sqlite3_prepare_v2(
        db_,
        sql,
        -1,
        &statement,
        nullptr
    );

    checkSqlite(
        result,
        db_,
        "prepare processed update insert"
    );

    sqlite3_bind_int64(
        statement,
        1,
        updateId
    );

    result = sqlite3_step(statement);

    checkSqlite(
        result,
        db_,
        "insert processed update"
    );

    sqlite3_finalize(statement);
}

void Database::saveMessage(
    long long chatId,
    const std::string& senderId,
    const std::string& username,
    const std::string& text,
    bool incoming,
    std::optional<long long> updateId
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        INSERT OR IGNORE INTO messages
        (
            update_id,
            chat_id,
            sender_id,
            username,
            text,
            incoming,
            timestamp
        )
        VALUES (?, ?, ?, ?, ?, ?, strftime('%s','now'));
    )SQL";

    int result = sqlite3_prepare_v2(
        db_,
        sql,
        -1,
        &statement,
        nullptr
    );

    checkSqlite(
        result,
        db_,
        "prepare message insert"
    );

    if (updateId.has_value()) {
        sqlite3_bind_int64(
            statement,
            1,
            *updateId
        );
    } else {
        sqlite3_bind_null(
            statement,
            1
        );
    }

    sqlite3_bind_int64(
        statement,
        2,
        chatId
    );

    sqlite3_bind_text(
        statement,
        3,
        senderId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        4,
        username.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        5,
        text.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int(
        statement,
        6,
        incoming ? 1 : 0
    );

    result = sqlite3_step(statement);

    checkSqlite(
        result,
        db_,
        "insert message"
    );

    sqlite3_finalize(statement);
}

std::vector<MessageRecord> Database::getHistory(
    long long chatId,
    int limit
) {
    std::vector<MessageRecord> result;

    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        SELECT
            id,
            chat_id,
            sender_id,
            username,
            text,
            incoming,
            timestamp,
            update_id
        FROM (
            SELECT
                id,
                chat_id,
                sender_id,
                username,
                text,
                incoming,
                timestamp,
                update_id
            FROM messages
            WHERE chat_id = ?
            ORDER BY id DESC
            LIMIT ?
        )
        ORDER BY id ASC;
    )SQL";

    int rc = sqlite3_prepare_v2(
        db_,
        sql,
        -1,
        &statement,
        nullptr
    );

    checkSqlite(
        rc,
        db_,
        "prepare history query"
    );

    sqlite3_bind_int64(
        statement,
        1,
        chatId
    );

    sqlite3_bind_int(
        statement,
        2,
        limit
    );

    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        MessageRecord record;

        record.id =
            sqlite3_column_int64(statement, 0);

        record.chatId =
            sqlite3_column_int64(statement, 1);

        const auto* sender =
            sqlite3_column_text(statement, 2);

        const auto* username =
            sqlite3_column_text(statement, 3);

        const auto* text =
            sqlite3_column_text(statement, 4);

        record.senderId =
            sender
                ? reinterpret_cast<const char*>(sender)
                : "";

        record.username =
            username
                ? reinterpret_cast<const char*>(username)
                : "";

        record.text =
            text
                ? reinterpret_cast<const char*>(text)
                : "";

        record.incoming =
            sqlite3_column_int(statement, 5) != 0;

        record.timestamp =
            sqlite3_column_int64(statement, 6);

        if (sqlite3_column_type(statement, 7) != SQLITE_NULL) {
            record.updateId =
                sqlite3_column_int64(statement, 7);
        }

        result.push_back(std::move(record));
    }

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(statement);

        throw std::runtime_error(
            "Unable to read message history"
        );
    }

    sqlite3_finalize(statement);

    return result;
}
