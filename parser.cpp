//
// Created by Parth Pathak on 17/05/26.
//

#include "parser.h"


#include <string>
#include <vector>

using string = std::string;
using vecS = std::vector<string>;

namespace {

constexpr size_t kMaxRawInputLength = 4096;
constexpr size_t kMaxTokens = 64;
constexpr size_t kMaxTokenLength = 1024;

vecS make_error(const string &message) {
    return {"ERR", message};
}

bool is_var_start(unsigned char c) {
    return std::isalpha(c) != 0 || c == '_';
}

bool is_var_char(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
}

bool has_unquoted_syntax_error(const string &input, string &error_message) {
    bool in_single = false;
    bool in_double = false;
    bool escape_next = false;
    size_t pipe_count = 0;

    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];

        if (escape_next) {
            escape_next = false;
            continue;
        }

        if (c == '\\' && !in_single) {
            escape_next = true;
            continue;
        }

        if (c == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }

        if (c == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }

        if (in_single || in_double) {
            continue;
        }

        if (c == '`') {
            error_message = "Error: Command chaining (&&, ||, ;) is not supported in ParryShell.";
            return true;
        }

        if (c == '$' && i + 1 < input.size() && input[i + 1] == '(') {
            error_message = "Error: Command chaining (&&, ||, ;) is not supported in ParryShell.";
            return true;
        }

        if (c == ';') {
            error_message = "Error: Command chaining (&&, ||, ;) is not supported in ParryShell.";
            return true;
        }

        if (c == '&' && i + 1 < input.size() && input[i + 1] == '&') {
            error_message = "Error: Command chaining (&&, ||, ;) is not supported in ParryShell.";
            return true;
        }

        if (c == '|' && i + 1 < input.size() && input[i + 1] == '|') {
            error_message = "Error: Command chaining (&&, ||, ;) is not supported in ParryShell.";
            return true;
        }

        if (c == '|') {
            ++pipe_count;
            if (pipe_count > 1) {
                error_message = "Error: ParryShell supports only a single pipe.";
                return true;
            }
        }
    }

    return false;
}

string expand_environment(const string &input) {
    string output;
    output.reserve(input.size());

    bool in_single = false;
    bool in_double = false;
    bool escape_next = false;

    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];

        if (escape_next) {
            output.push_back(c);
            escape_next = false;
            continue;
        }

        if (c == '\\' && !in_single) {
            output.push_back(c);
            escape_next = true;
            continue;
        }

        if (c == '\'' && !in_double) {
            in_single = !in_single;
            output.push_back(c);
            continue;
        }

        if (c == '"' && !in_single) {
            in_double = !in_double;
            output.push_back(c);
            continue;
        }

        if (c == '$' && !in_single) {
            if (i + 1 < input.size() && input[i + 1] == '{') {
                size_t j = i + 2;
                if (j < input.size() && is_var_start(static_cast<unsigned char>(input[j]))) {
                    ++j;
                    while (j < input.size() && is_var_char(static_cast<unsigned char>(input[j]))) {
                        ++j;
                    }

                    if (j < input.size() && input[j] == '}') {
                        const string var_name = input.substr(i + 2, j - (i + 2));
                        const char *value = std::getenv(var_name.c_str());
                        if (value) {
                            output.append(value);
                        } else {
                            output.append(input.substr(i, j - i + 1));
                        }
                        i = j;
                        continue;
                    }
                }
            } else if (i + 1 < input.size() && is_var_start(static_cast<unsigned char>(input[i + 1]))) {
                size_t j = i + 2;
                while (j < input.size() && is_var_char(static_cast<unsigned char>(input[j]))) {
                    ++j;
                }

                const string var_name = input.substr(i + 1, j - (i + 1));
                const char *value = std::getenv(var_name.c_str());
                if (value) {
                    output.append(value);
                } else {
                    output.append(input.substr(i, j - i));
                }
                i = j - 1;
                continue;
            }
        }

        output.push_back(c);
    }

    return output;
}

bool maybe_emit_operator(const string &input, size_t &i, string &current, vecS &tokens, bool &token_started) {
    auto flush_current = [&]() {
        if (token_started) {
            tokens.push_back(current);
            current.clear();
            token_started = false;
        }
    };

    if (i >= input.size()) {
        return false;
    }

    const char c = input[i];

    if (c == '2' && token_started == false) {
        if (i + 3 < input.size() && input.compare(i, 4, "2>&1") == 0) {
            flush_current();
            tokens.emplace_back("2>&1");
            i += 3;
            return true;
        }

        if (i + 2 < input.size() && input.compare(i, 3, "2>>") == 0) {
            flush_current();
            tokens.emplace_back("2>>");
            i += 2;
            return true;
        }

        if (i + 1 < input.size() && input.compare(i, 2, "2>") == 0) {
            flush_current();
            tokens.emplace_back("2>");
            ++i;
            return true;
        }
    }

    if (c == '>' || c == '<' || c == '|' || c == '&') {
        flush_current();

        if (c == '>' && i + 1 < input.size() && input[i + 1] == '>') {
            tokens.emplace_back(">>");
            ++i;
        } else {
            tokens.emplace_back(string(1, c));
        }

        return true;
    }

    return false;
}

vecS tokenize(const string &input) {
    vecS tokens;
    string current;
    bool in_single = false;
    bool in_double = false;
    bool escape_next = false;
    bool token_started = false;

    auto flush_current = [&]() {
        if (token_started) {
            tokens.push_back(current);
            current.clear();
            token_started = false;
        }
    };

    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];

        if (escape_next) {
            current.push_back(c);
            token_started = true;
            escape_next = false;
            continue;
        }

        if (!in_single && c == '\\') {
            escape_next = true;
            token_started = true;
            continue;
        }

        if (!in_double && c == '\'') {
            in_single = !in_single;
            token_started = true;
            continue;
        }

        if (!in_single && c == '"') {
            in_double = !in_double;
            token_started = true;
            continue;
        }

        if (!in_single && !in_double && std::isspace(static_cast<unsigned char>(c)) != 0) {
            flush_current();
            continue;
        }

        if (!in_single && !in_double) {
            if (c == '~' && !token_started) {
                const char *home = std::getenv("HOME");
                current.append(home ? home : "~");
                token_started = true;
                continue;
            }

            size_t op_index = i;
            if (maybe_emit_operator(input, op_index, current, tokens, token_started)) {
                i = op_index;
                continue;
            }
        }

        current.push_back(c);
        token_started = true;
    }

    if (escape_next) {
        current.push_back('\\');
        token_started = true;
    }

    flush_current();
    return tokens;
}

bool validate_tokens(const vecS &tokens, string &error_message) {
    if (tokens.size() > kMaxTokens) {
        error_message = "Error: Too many arguments. Maximum 64 allowed.";
        return false;
    }

    for (const auto &token : tokens) {
        if (token.size() > kMaxTokenLength) {
            error_message = "Error: Argument too long. Maximum 1024 characters per argument.";
            return false;
        }
    }

    return true;
}

} // namespace

vecS parse_(string command) {
    if (command.size() > kMaxRawInputLength) {
        return make_error("Error: Command too long. Maximum 4096 characters.");
    }

    const string expanded = expand_environment(command);

    string syntax_error;
    if (has_unquoted_syntax_error(expanded, syntax_error)) {
        return make_error(syntax_error);
    }

    vecS tokens = tokenize(expanded);

    if (tokens.empty()) {
        return tokens;
    }

    if (!validate_tokens(tokens, syntax_error)) {
        return make_error(syntax_error);
    }

    return tokens;
}
