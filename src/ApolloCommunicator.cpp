#include "ApolloCommunicator.h"

#include <iostream>
#include <string>
#include <future>
#include <charconv>
#include <cassert>

namespace ApolloCommunicator {
    bool shuttingDown = false;

    std::string_view consumeNextToken(std::string_view& sv) {
        auto pos = sv.find_first_of(' ');
        if (pos == std::string_view::npos) {
            auto token = sv;
            sv = {};
            return token;
        }

        auto token = sv.substr(0, pos);
        sv = sv.substr(token.size() + 1);
        return token;
    }

    int parseInt(std::string_view str) {
        int num;
        auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), num);
        assert(ec == std::errc());
        return num;
    }

    void handleCommand(std::string_view command) {
        auto commandName = consumeNextToken(command);

        if (commandName == "STATE") {
            auto state = consumeNextToken(command);

            if (state == "PLAYING") {
                apollo_commands.push(play{});
            } else if (state == "PAUSED") {
                apollo_commands.push(pause{});
            } else {
                std::cerr << "Received command requested unsupported state: " << state << std::endl;
                std::exit(1);
            }
            return;
        }

        if (commandName == "SEEK") {
            auto seekTime = parseInt(consumeNextToken(command));
            apollo_commands.push(seek{seekTime});
            std::cout << "Received seek command: " << seekTime << std::endl;
            return;
        }

        std::cerr << "Received unknown command: " << commandName << std::endl;
        std::exit(1);
    }

    std::future<void> startThread() {
        return std::async(std::launch::async, []() {
            while (!shuttingDown) {
                std::string command;
                std::getline(std::cin, command);
                if (std::cin.eof()) {
                    break;
                }

                if (command.empty()) {
                    continue;
                }
//                std::cout << "Received command: '" << command << "'" << std::endl;
                handleCommand(command);
            }
//            char buffer[4096]{};
//            while (!shuttingDown) {
//                auto count = std::cin.get(buffer, sizeof(buffer)).gcount();
//
//                std::string_view sv{buffer, buffer + count};
//
//                consume_whitespace(sv);
//                if (consume(sv, "STATE")) {
//                    consume_whitespace(sv);
//                    if (consume(sv, "PLAYING")) {
//                        apollo_commands.push(play{});
//                        std::cerr << "Received PLAYING" << std::endl;   // TODO: remove debug
//                    } else if (consume(sv, "PAUSED")) {
//                        apollo_commands.push(pause{});
//                        std::cerr << "Received PAUSED" << std::endl;    // TODO: remove debug
//                    }
//                } else if (consume(sv, "SEEK")) {
//                    consume_whitespace(sv);
//                    auto num = consume_next_token(sv);
//                    apollo_commands.push(seek{str_to_num(num)});
//                }
//            }
        });
    }

    void requestShutdown() {
        shuttingDown = true;
    }

    void sendSimple(const char* command, const std::vector<std::string>& data) {
        std::cout << "::" << command << ':';

        for (const auto& value: data) {
            std::cout << value << ':';
        }
        if (data.empty()) {
            std::cout << ':';
        }

        std::cout << std::endl;
    }
}
