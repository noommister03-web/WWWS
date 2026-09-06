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

    // Миграция для уже существующей базы.
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
// Database.cpp — ЧАСТЬ 2 ИЗ 3
// Вставь этот текст В КОНЕЦ файла сразу после части 1.

std::vector<MessageRecord> Database::getHistory(
    long long chatId,
    int limit
) {
    std::vector<MessageRecord> result;

    if (limit <= 0) {
        return result;
    }

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
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(rc, db_, "prepare history query");

    sqlite3_bind_int64(statement, 1, chatId);
    sqlite3_bind_int(statement, 2, limit);

    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        MessageRecord record;

        record.id = sqlite3_column_int64(statement, 0);
        record.chatId = sqlite3_column_int64(statement, 1);
        record.senderId = columnText(statement, 2);
        record.username = columnText(statement, 3);
        record.text = columnText(statement, 4);
        record.incoming = sqlite3_column_int(statement, 5) != 0;
        record.timestamp = sqlite3_column_int64(statement, 6);

        if (sqlite3_column_type(statement, 7) != SQLITE_NULL) {
            record.updateId = sqlite3_column_int64(statement, 7);
        }

        result.push_back(std::move(record));
    }

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(statement);
        throw std::runtime_error("Unable to read message history");
    }

    sqlite3_finalize(statement);
    return result;
}

long long Database::addCustoJustoAccount(
    const std::string& name,
    const std::string& email,
    const std::string& loginUrl
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        INSERT INTO custojusto_accounts (
            name,
            email,
            login_url,
            enabled,
            logged_in,
            created_at
        )
        VALUES (?, ?, ?, 1, 0, strftime('%s','now'));
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(
        result,
        db_,
        "prepare CustoJusto account insert"
    );

    sqlite3_bind_text(
        statement, 1, name.c_str(), -1, SQLITE_TRANSIENT
    );
    sqlite3_bind_text(
        statement, 2, email.c_str(), -1, SQLITE_TRANSIENT
    );
    sqlite3_bind_text(
        statement, 3, loginUrl.c_str(), -1, SQLITE_TRANSIENT
    );

    result = sqlite3_step(statement);
    checkSqlite(result, db_, "insert CustoJusto account");

    sqlite3_finalize(statement);
    return sqlite3_last_insert_rowid(db_);
}

int Database::getCustoJustoAccountCount() {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        SELECT COUNT(*)
        FROM custojusto_accounts;
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(result, db_, "prepare account count");

    result = sqlite3_step(statement);
    checkSqlite(result, db_, "read account count");

    const int count = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);

    return count;
}

std::vector<CustoJustoAccount>
Database::getCustoJustoAccounts() {
    std::vector<CustoJustoAccount> result;
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        SELECT
            id,
            name,
            email,
            login_url,
            enabled,
            logged_in,
            created_at
        FROM custojusto_accounts
        ORDER BY id ASC;
    )SQL";

    int rc = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(rc, db_, "prepare CustoJusto accounts query");

    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        CustoJustoAccount account;

        account.id = sqlite3_column_int64(statement, 0);
        account.name = columnText(statement, 1);
        account.email = columnText(statement, 2);
        account.loginUrl = columnText(statement, 3);
        account.enabled = sqlite3_column_int(statement, 4) != 0;
        account.loggedIn = sqlite3_column_int(statement, 5) != 0;
        account.createdAt = sqlite3_column_int64(statement, 6);

        result.push_back(std::move(account));
    }

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(statement);
        throw std::runtime_error("Unable to read CustoJusto accounts");
    }

    sqlite3_finalize(statement);
    return result;
}

std::optional<CustoJustoAccount>
Database::getCustoJustoAccount(
    long long id
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        SELECT
            id,
            name,
            email,
            login_url,
            enabled,
            logged_in,
            created_at
        FROM custojusto_accounts
        WHERE id = ?
        LIMIT 1;
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(result, db_, "prepare CustoJusto account query");
    sqlite3_bind_int64(statement, 1, id);

    result = sqlite3_step(statement);

    if (result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return std::nullopt;
    }

    checkSqlite(result, db_, "read CustoJusto account");

    CustoJustoAccount account;

    account.id = sqlite3_column_int64(statement, 0);
    account.name = columnText(statement, 1);
    account.email = columnText(statement, 2);
    account.loginUrl = columnText(statement, 3);
    account.enabled = sqlite3_column_int(statement, 4) != 0;
    account.loggedIn = sqlite3_column_int(statement, 5) != 0;
    account.createdAt = sqlite3_column_int64(statement, 6);

    sqlite3_finalize(statement);
    return account;
}

bool Database::deleteCustoJustoAccount(
    long long id
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        DELETE FROM custojusto_accounts
        WHERE id = ?;
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(
        result,
        db_,
        "prepare CustoJusto account delete"
    );

    sqlite3_bind_int64(statement, 1, id);
    result = sqlite3_step(statement);

    checkSqlite(result, db_, "delete CustoJusto account");

    const bool deleted = sqlite3_changes(db_) > 0;
    sqlite3_finalize(statement);

    return deleted;
}

bool Database::setCustoJustoAccountEnabled(
    long long id,
    bool enabled
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        UPDATE custojusto_accounts
        SET enabled = ?
        WHERE id = ?;
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(result, db_, "prepare CustoJusto enabled update");
    sqlite3_bind_int(statement, 1, enabled ? 1 : 0);
    sqlite3_bind_int64(statement, 2, id);

    result = sqlite3_step(statement);
    checkSqlite(result, db_, "update CustoJusto enabled state");

    const bool updated = sqlite3_changes(db_) > 0;
    sqlite3_finalize(statement);

    return updated;
}

bool Database::setCustoJustoAccountLoggedIn(
    long long id,
    bool loggedIn
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        UPDATE custojusto_accounts
        SET logged_in = ?
        WHERE id = ?;
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(
        result,
        db_,
        "prepare CustoJusto login state update"
    );

    sqlite3_bind_int(statement, 1, loggedIn ? 1 : 0);
    sqlite3_bind_int64(statement, 2, id);

    result = sqlite3_step(statement);
    checkSqlite(result, db_, "update CustoJusto login state");

    const bool updated = sqlite3_changes(db_) > 0;
    sqlite3_finalize(statement);

    return updated;
}
// Database.cpp — ЧАСТЬ 3 ИЗ 3
// Вставь этот текст В КОНЕЦ файла после части 2.

std::optional<CustoJustoConversationRecord>
Database::getCustoJustoConversationByUrl(
    long long accountId,
    const std::string& conversationUrl
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        SELECT
            id,
            account_id,
            conversation_url,
            listing_url,
            listing_title,
            contact_name,
            last_message_id,
            last_message_text,
            last_message_at,
            unread,
            created_at,
            updated_at
        FROM custojusto_conversations
        WHERE account_id = ?
          AND conversation_url = ?
        LIMIT 1;
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(
        result,
        db_,
        "prepare conversation by URL query"
    );

    sqlite3_bind_int64(statement, 1, accountId);
    sqlite3_bind_text(
        statement,
        2,
        conversationUrl.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    result = sqlite3_step(statement);

    if (result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return std::nullopt;
    }

    checkSqlite(result, db_, "read conversation by URL");

    CustoJustoConversationRecord record;

    record.id = sqlite3_column_int64(statement, 0);
    record.accountId = sqlite3_column_int64(statement, 1);
    record.conversationUrl = columnText(statement, 2);
    record.listingUrl = columnText(statement, 3);
    record.listingTitle = columnText(statement, 4);
    record.contactName = columnText(statement, 5);
    record.lastMessageId = columnText(statement, 6);
    record.lastMessageText = columnText(statement, 7);
    record.lastMessageAt = sqlite3_column_int64(statement, 8);
    record.unread = sqlite3_column_int(statement, 9) != 0;
    record.createdAt = sqlite3_column_int64(statement, 10);
    record.updatedAt = sqlite3_column_int64(statement, 11);

    sqlite3_finalize(statement);
    return record;
}

long long Database::upsertCustoJustoConversation(
    long long accountId,
    const std::string& conversationUrl,
    const std::string& listingUrl,
    const std::string& listingTitle,
    const std::string& contactName,
    const std::string& lastMessageId,
    const std::string& lastMessageText,
    long long lastMessageAt,
    bool unread
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        INSERT INTO custojusto_conversations (
            account_id,
            conversation_url,
            listing_url,
            listing_title,
            contact_name,
            last_message_id,
            last_message_text,
            last_message_at,
            unread,
            created_at,
            updated_at
        )
        VALUES (
            ?, ?, ?, ?, ?, ?, ?, ?, ?,
            strftime('%s','now'),
            strftime('%s','now')
        )
        ON CONFLICT(account_id, conversation_url)
        DO UPDATE SET
            listing_url = excluded.listing_url,
            listing_title = excluded.listing_title,
            contact_name = excluded.contact_name,
            last_message_id = excluded.last_message_id,
            last_message_text = excluded.last_message_text,
            last_message_at = excluded.last_message_at,
            unread = excluded.unread,
            updated_at = strftime('%s','now');
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(result, db_, "prepare conversation upsert");

    sqlite3_bind_int64(statement, 1, accountId);
    sqlite3_bind_text(statement, 2, conversationUrl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, listingUrl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, listingTitle.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, contactName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, lastMessageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 7, lastMessageText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 8, lastMessageAt);
    sqlite3_bind_int(statement, 9, unread ? 1 : 0);

    result = sqlite3_step(statement);
    checkSqlite(result, db_, "upsert CustoJusto conversation");

    sqlite3_finalize(statement);

    const auto record = getCustoJustoConversationByUrl(
        accountId,
        conversationUrl
    );

    if (!record.has_value()) {
        throw std::runtime_error(
            "Unable to read saved CustoJusto conversation"
        );
    }

    return record->id;
}

std::optional<CustoJustoConversationRecord>
Database::getCustoJustoConversation(
    long long conversationId
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        SELECT
            id,
            account_id,
            conversation_url,
            listing_url,
            listing_title,
            contact_name,
            last_message_id,
            last_message_text,
            last_message_at,
            unread,
            created_at,
            updated_at
        FROM custojusto_conversations
        WHERE id = ?
        LIMIT 1;
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(result, db_, "prepare conversation query");
    sqlite3_bind_int64(statement, 1, conversationId);

    result = sqlite3_step(statement);

    if (result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return std::nullopt;
    }

    checkSqlite(result, db_, "read CustoJusto conversation");

    CustoJustoConversationRecord record;

    record.id = sqlite3_column_int64(statement, 0);
    record.accountId = sqlite3_column_int64(statement, 1);
    record.conversationUrl = columnText(statement, 2);
    record.listingUrl = columnText(statement, 3);
    record.listingTitle = columnText(statement, 4);
    record.contactName = columnText(statement, 5);
    record.lastMessageId = columnText(statement, 6);
    record.lastMessageText = columnText(statement, 7);
    record.lastMessageAt = sqlite3_column_int64(statement, 8);
    record.unread = sqlite3_column_int(statement, 9) != 0;
    record.createdAt = sqlite3_column_int64(statement, 10);
    record.updatedAt = sqlite3_column_int64(statement, 11);

    sqlite3_finalize(statement);
    return record;
}

bool Database::hasCustoJustoExternalMessage(
    long long accountId,
    const std::string& externalMessageId
) {
    if (externalMessageId.empty()) {
        return false;
    }

    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        SELECT 1
        FROM custojusto_messages
        WHERE account_id = ?
          AND external_message_id = ?
        LIMIT 1;
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(result, db_, "prepare external message check");

    sqlite3_bind_int64(statement, 1, accountId);
    sqlite3_bind_text(
        statement,
        2,
        externalMessageId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    result = sqlite3_step(statement);
    const bool exists = result == SQLITE_ROW;

    sqlite3_finalize(statement);
    return exists;
}

long long Database::saveCustoJustoMessage(
    long long accountId,
    long long conversationId,
    const std::string& externalMessageId,
    const std::string& senderName,
    const std::string& originalText,
    const std::string& translatedText,
    bool incoming
) {
    sqlite3_stmt* statement = nullptr;

    const char* sql = R"SQL(
        INSERT OR IGNORE INTO custojusto_messages (
            account_id,
            conversation_id,
            external_message_id,
            sender_name,
            original_text,
            translated_text,
            incoming,
            created_at
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, strftime('%s','now'));
    )SQL";

    int result = sqlite3_prepare_v2(
        db_, sql, -1, &statement, nullptr
    );

    checkSqlite(
        result,
        db_,
        "prepare CustoJusto message insert"
    );

    sqlite3_bind_int64(statement, 1, accountId);
    sqlite3_bind_int64(statement, 2, conversationId);
    sqlite3_bind_text(statement, 3, externalMessageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, senderName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, originalText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, translatedText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 7, incoming ? 1 : 0);

    result = sqlite3_step(statement);
    checkSqlite(result, db_, "insert CustoJusto message");

    sqlite3_finalize(statement);
    return sqlite3_last_insert_rowid(db_);
}
