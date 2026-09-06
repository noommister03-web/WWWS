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
    if (
        result != SQLITE_OK &&
        result != SQLITE_DONE &&
        result != SQLITE_ROW
    ) {
        std::string message = operation;

        if (db != nullptr) {
            message += ": ";
            message += sqlite3_errmsg(db);
        }

        throw std::runtime_error(message);
    }
}

std::string columnText(
    sqlite3_stmt* statement,
    int column
) {
    const auto* value = sqlite3_column_text(
        statement,
        column
    );

    return value
        ? reinterpret_cast<const char*>(value)
        : "";
}

} // namespace

Database::Database(
    const std::string& path
) {
    const std::filesystem::path dbPath(path);

    if (dbPath.has_parent_path()) {
        std::filesystem::create_directories(
            dbPath.parent_path()
        );
    }

    const int result = sqlite3_open(
        path.c_str(),
        &db_
    );

    if (result != SQLITE_OK) {
        std::string message =
            "Unable to open SQLite database";

        if (db_ != nullptr) {
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
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::execute(
    const std::string& sql
) {
    char* error = nullptr;

    const int result = sqlite3_exec(
        db_,
        sql.c_str(),
        nullptr,
        nullptr,
        &error
    );

    if (result != SQLITE_OK) {
        std::string message =
            error != nullptr
                ? error
                : "SQLite error";

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
        "prepare table_info"
    );

    bool found = false;

    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (columnText(statement, 1) == column) {
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

    execute(R"SQL(
        CREATE TABLE IF NOT EXISTS custojusto_accounts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            email TEXT NOT NULL DEFAULT '',
            login_url TEXT NOT NULL
                DEFAULT 'https://www.custojusto.pt',
            enabled INTEGER NOT NULL DEFAULT 1,
            logged_in INTEGER NOT NULL DEFAULT 0,
            created_at INTEGER NOT NULL
        );
    )SQL");

    // РњРёРіСЂР°С†РёСЏ РґР»СЏ СѓР¶Рµ СЃСѓС‰РµСЃС‚РІСѓСЋС‰РµР№ Р±Р°Р·С‹.
    if (!hasColumn("custojusto_accounts", "login_url")) {
        execute(
            "ALTER TABLE custojusto_accounts "
            "ADD COLUMN login_url TEXT NOT NULL "
            "DEFAULT 'https://www.custojusto.pt';"
        );
    }

    if (!hasColumn("custojusto_accounts", "logged_in")) {
        execute(
            "ALTER TABLE custojusto_accounts "
            "ADD COLUMN logged_in INTEGER NOT NULL "
            "DEFAULT 0;"
        );
    }

    execute(R"SQL(
        CREATE INDEX IF NOT EXISTS
        idx_custojusto_accounts_enabled
        ON custojusto_accounts(enabled);
    )SQL");

    execute(R"SQL(
        CREATE TABLE IF NOT EXISTS custojusto_conversations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            account_id INTEGER NOT NULL,
            conversation_url TEXT NOT NULL,
            listing_url TEXT NOT NULL DEFAULT '',
            listing_title TEXT NOT NULL DEFAULT '',
            contact_name TEXT NOT NULL DEFAULT '',
            last_message_id TEXT NOT NULL DEFAULT '',
            last_message_text TEXT NOT NULL DEFAULT '',
            last_message_at INTEGER NOT NULL DEFAULT 0,
            unread INTEGER NOT NULL DEFAULT 0,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,

            UNIQUE(account_id, conversation_url),

            FOREIGN KEY(account_id)
                REFERENCES custojusto_accounts(id)
                ON DELETE CASCADE
        );
    )SQL");

    execute(R"SQL(
        CREATE INDEX IF NOT EXISTS
        idx_custojusto_conversations_account
        ON custojusto_conversations(account_id, updated_at DESC);
    )SQL");

    execute(R"SQL(
        CREATE TABLE IF NOT EXISTS custojusto_messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            account_id INTEGER NOT NULL,
            conversation_id INTEGER NOT NULL,
            external_message_id TEXT NOT NULL DEFAULT '',
            sender_name TEXT NOT NULL DEFAULT '',
            original_text TEXT NOT NULL,
            translated_text TEXT NOT NULL DEFAULT '',
            incoming INTEGER NOT NULL,
            created_at INTEGER NOT NULL,

            UNIQUE(account_id, external_message_id),

            FOREIGN KEY(account_id)
                REFERENCES custojusto_accounts(id)
                ON DELETE CASCADE,

            FOREIGN KEY(conversation_id)
                REFERENCES custojusto_conversations(id)
                ON DELETE CASCADE
        );
    )SQL");

    execute(R"SQL(
        CREATE INDEX IF NOT EXISTS
        idx_custojusto_messages_conversation
        ON custojusto_messages(conversation_id, id);
    )SQL");
}

bool Database::isUpdateProcessed(
    long long updateId
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        SELECT 1
        FROM processed_updates
        WHERE update_id = ?
        LIMIT 1;
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
        "prepare processed update query"
    );

    sqlite3_bind_int64(
        statement,
        1,
        updateId
    );

    result = sqlite3_step(statement);

    const bool processed =
        result == SQLITE_ROW;

    sqlite3_finalize(statement);

    return processed;
}

void Database::markUpdateProcessed(
    long long updateId
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        INSERT OR IGNORE INTO processed_updates(update_id)
        VALUES(?);
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
        INSERT OR IGNORE INTO messages (
            update_id,
            chat_id,
            sender_id,
            username,
            text,
            incoming,
            timestamp
        )
        VALUES (
            ?,
            ?,
            ?,
            ?,
            ?,
            ?,
            strftime('%s','now')
        );
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
        sqlite3_bind_null(statement, 1);
    }

    sqlite3_bind_int64(statement, 2, chatId);

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
