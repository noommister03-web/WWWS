#include "AiEngine.hpp"
#include "Config.hpp"
#include "Database.hpp"
#include "TelegramBot.hpp"

#include <csignal>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

std::atomic<bool> g_stop{false};

void signalHandler(int) {
    g_stop = true;
}

std::string makeWhatsAppLink(
    const std::string& number
) {
    std::string digits;

    for (char c : number) {
        if (c >= '0' && c <= '9') {
            digits += c;
        }
    }

    if (digits.empty()) {
        return "";
    }

    return "https://wa.me/" + digits;
}

bool startsWith(
    const std::string& text,
    const std::string& prefix
) {
    return text.rfind(prefix, 0) == 0;
}

}

int main() {
    try {
        std::signal(
            SIGINT,
            signalHandler
        );

        std::signal(
            SIGTERM,
            signalHandler
        );

        const Config config =
            Config::load();

        Database database(
            config.dbPath
        );

        AiEngine ai(
            config.aiApiKey,
            config.aiBaseUrl,
            config.aiModel,
            config.aiSystemPrompt,
            config.aiTimeout
        );

        TelegramBot bot(
            config.telegramToken,
            config.telegramPollTimeout,
            config.privateChatsOnly
        );

        bot.setStopChecker([] {
            return g_stop.load();
        });

        bot.setMessageHandler(
            [&](const IncomingMessage& incoming) -> bool {
                try {
                    if (
                        database.isUpdateProcessed(
                            incoming.updateId
                        )
                    ) {
                        return true;
                    }

                    database.saveMessage(
                        incoming.chatId,
                        incoming.senderId,
                        incoming.username,
                        incoming.text,
                        true,
                        incoming.updateId
                    );

                    const std::string& text =
                        incoming.text;

                    std::cout
                        << "Incoming message from "
                        << incoming.chatId
                        << ": "
                        << text
                        << std::endl;

                    std::string reply;

                    if (text == "/start") {
                        reply =
                            "Привет! Я помощник оператора.\n\n"
                            "Напиши свой вопрос, и я постараюсь помочь.";
                    }
                    else if (text == "/help") {
                        reply =
                            "Доступные команды:\n"
                            "/start — начать работу\n"
                            "/help — помощь\n"
                            "/status — состояние системы\n"
                            "/whatsapp — получить WhatsApp, если он настроен";
                    }
                    else if (text == "/status") {
                        reply =
                            "Система работает.\n"
                            "AI: ";

                        reply +=
                            ai.enabled()
                                ? "включён"
                                : "выключен";
                    }
                    else if (text == "/whatsapp") {
                        const std::string link =
                            makeWhatsAppLink(
                                config.whatsappNumber
                            );

                        if (link.empty()) {
                            reply =
                                "WhatsApp пока не настроен.";
                        }
                        else {
                            reply =
                                "WhatsApp:\n" +
                                link;
                        }
                    }
                    else if (
                        startsWith(
                            text,
                            "/"
                        )
                    ) {
                        reply =
                            "Неизвестная команда. "
                            "Используй /help.";
                    }
                    else {
                        if (!ai.enabled()) {
                            reply =
                                "AI сейчас не настроен. "
                                "Сообщение получено.";
                        }
                        else {
                            const auto history =
                                database.getHistory(
                                    incoming.chatId,
                                    config.aiHistoryLimit
                                );

                            try {
                                reply =
                                    ai.generateReply(
                                        history
                                    );
                            }
                            catch (
                                const std::exception& error
                            ) {
                                std::cerr
                                    << "AI error: "
                                    << error.what()
                                    << std::endl;

                                reply =
                                    "Сообщение получено. "
                                    "Оператор сможет ответить позже.";
                            }
                        }
                    }

                    if (reply.empty()) {
                        reply =
                            "Сообщение получено.";
                    }

                    const SendStatus status =
                        bot.sendMessage(
                            incoming.chatId,
                            reply
                        );

                    if (
                        status ==
                        SendStatus::TemporaryFailure
                    ) {
                        return false;
                    }

                    if (
                        status ==
                        SendStatus::Success
                    ) {
                        database.saveMessage(
                            incoming.chatId,
                            "bot",
                            "",
                            reply,
                            false
                        );
                    }

                    database.markUpdateProcessed(
                        incoming.updateId
                    );

                    return true;

                }
                catch (
                    const std::exception& error
                ) {
                    std::cerr
                        << "Message handler error: "
                        << error.what()
                        << std::endl;

                    return false;
                }
            }
        );

        std::cout
            << "Application started."
            << std::endl;

        bot.run();

        std::cout
            << "Application stopped."
            << std::endl;

        return 0;
    }
    catch (
        const std::exception& error
    ) {
        std::cerr
            << "Fatal error: "
            << error.what()
            << std::endl;

        return 1;
    }
}
