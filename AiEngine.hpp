#pragma once

#include "Database.hpp"

#include <string>
#include <vector>

class AiEngine {
public:
    AiEngine(
        std::string apiKey,
        std::string baseUrl,
        std::string model,
        std::string systemPrompt,
        int timeoutSeconds
    );

    bool enabled() const;

    std::string generateReply(
        const std::vector<MessageRecord>& history
    ) const;

private:
    std::string apiKey_;
    std::string baseUrl_;
    std::string model_;
    std::string systemPrompt_;
    int timeoutSeconds_;

    static std::string trimTrailingSlash(
        std::string value
    );

    static std::string limitUtf8(
        const std::string& text,
        std::size_t maxBytes
    );
};
