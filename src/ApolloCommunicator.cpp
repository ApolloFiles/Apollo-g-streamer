#include "ApolloCommunicator.h"

#include <iostream>
#include <string>
#include <future>
#include <charconv>
#include <cassert>

namespace ApolloCommunicator {

    int str_to_num(std::string_view str) {
        int num;
        auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), num);
        assert(ec == std::errc());
        return num;
    }

    bool consume(std::string_view& sv, std::string_view token) {
        if (sv.starts_with(token)) {
            sv = sv.substr(token.size());
            return true;
        }
        return false;
    }

    void consume_whitespace(std::string_view& sv) {
        while (!sv.empty() && std::isspace(sv[0])) {
            sv = sv.substr(1);
        }
    }

    std::string_view consume_next_token(std::string_view& sv) {
        consume_whitespace(sv);
        auto token = sv.substr(0, sv.find_first_of(" \t\n\r"));
        sv = sv.substr(token.size());
        return token;
    }

    std::string_view consume_to_delim(std::string_view& sv, char delim) {
        auto pos = sv.find_first_of(delim);
        if (pos == std::string_view::npos) {
            return {};
        }
        auto token = sv.substr(0, pos);
        sv = sv.substr(token.size() + 1);
        return token;
    }

    std::future<void> startThread() {
        // STATE PLAYING\n
        // SEEK 120\n
        // TARGET_TIME 420\n
        return std::async(std::launch::async, []() {
            char buffer[4096]{};

            while (true) {
                auto count = std::cin.get(buffer, sizeof(buffer)).gcount();

                std::string_view sv{buffer, buffer + count};

                consume_whitespace(sv);
                if (consume(sv, "STATE")) {
                    consume_whitespace(sv);
                    if (consume(sv, "PLAYING")) {
                        apollo_commands.push(start{});
                    }
                } else if (consume(sv, "SEEK")) {
                    consume_whitespace(sv);
                    auto num = consume_next_token(sv);
                    apollo_commands.push(seek{str_to_num(num)});
                }
            }
        });
    }
}
