//
// Created by Parth Pathak on 17/05/26.
//

#include "shell.h"
#include <QCoreApplication>
#include <algorithm>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>
#include <pwd.h>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <sys/wait.h>

#include "history.h"

/*
Certain commands/tools might be missing from this file, the purpose of this repo is not to create a very good shell
The purpose was to learn how a shell works, how to implement certain features, and maybe get some exposure to GUIs
Some things might look hardcoded, and it's fine because I am not used to doing complex projects alone
 */

using string = std::string;
using vecS = std::vector<string>;
using CommandFunc = void(*)(const std::vector<std::string> &, LogCallback);

std::string current_directory;
std::string root = "/";
pid_t active_child_pid = 0;
bool pending_exit_confirmation = false;
std::mutex output_mutex;

void request_interrupt() {
    if (active_child_pid > 0) {
        kill(active_child_pid, SIGINT);
    }
}

void PRINT_ARGS_FOR_DEBUGGING_(const vecS &tokens) {
    for (const auto &token: tokens) {
        std::cout << token << "\n";
    }
}


std::string expand_tilde(const std::string &path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }


    if (path.size() == 1 || path[1] == '/') {
        struct passwd *pw = getpwuid(getuid());

        if (!pw) return path;

        return std::string(pw->pw_dir) + path.substr(1);
    }


    size_t slash_pos = path.find('/');

    std::string username;
    if (slash_pos == std::string::npos) {
        username = path.substr(1);
    } else {
        username = path.substr(1, slash_pos - 1);
    }

    struct passwd *pw = getpwnam(username.c_str());

    if (!pw) return path;

    if (slash_pos == std::string::npos) {
        return std::string(pw->pw_dir);
    }

    return std::string(pw->pw_dir) + path.substr(slash_pos);
}

void update_current_directory() {
    char *cwd = getcwd(nullptr, 0);
    if (cwd) {
        current_directory = std::string(cwd);
        free(cwd);
    } else {
        current_directory = ".";
    }
}

string error_handling(const vecS &tokens, const size_t arg_len) {
    if (arg_len > 4096) {
        return "Error: Command too long. Maximum 4096 characters.\n";
    }

    if (tokens.size() > 64) {
        return "Error: Too many arguments. Maximum 64 allowed.\n";
    }


    return "";
}


std::unordered_map<string, string> env_vars;

void init_env_vars() {
    extern char **environ;

    for (size_t i = 0; environ[i] != nullptr; ++i) {
        std::string entry(environ[i]);
        size_t equals_pos = entry.find('=');

        if (equals_pos != std::string::npos) {
            std::string key = entry.substr(0, equals_pos);
            std::string val = entry.substr(equals_pos + 1);
            env_vars[key] = val;
        }
    }
}


std::unordered_map<string, CommandFunc> command_map;

void EXIT(const vecS &tokens, LogCallback &append_output) {
    if (active_child_pid > 0) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("Warning: A process is still running. Exit anyway? (y/n): ", 2);
        }
        pending_exit_confirmation = true;
        return;
    }


    if (tokens.size() == 1) {
        QCoreApplication::exit(0);
        return;
    }


    std::string code_str = tokens[1];


    bool is_numeric = !code_str.empty() && std::all_of(code_str.begin(), code_str.end(), [](unsigned char c) {
        return std::isdigit(c);
    });

    if (!is_numeric) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("Error: Invalid exit code '" + code_str + "'. Must be an integer.\n", 1);
        }
        return;
    }

    int exit_code = std::stoi(code_str);
    QCoreApplication::exit(exit_code);
}

void ECHO(vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() < 2) {
        append_output("\n", 0);
        return;
    }


    size_t redirect_idx = 0;
    for (size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i] == ">") {
            redirect_idx = i;
            break;
        }
    }


    if (redirect_idx > 0) {
        if (redirect_idx == tokens.size() - 1) {
            append_output("Error: No output file specified.\n", 1);
            return;
        }

        const std::string &filename = tokens[redirect_idx + 1];


        std::string content = "";
        for (size_t i = 1; i < redirect_idx; ++i) {
            content += tokens[i] + (i == redirect_idx - 1 ? "" : " ");
        }
        content += "\n";


        std::filesystem::path abs_path = std::filesystem::current_path() / filename;


        std::ofstream file(abs_path);
        if (!file.is_open()) {
            append_output("Error: Could not open or create file '" + filename + "'\n", 1);
            return;
        }

        file << content;
        append_output("Successfully wrote to '" + filename + "'\n", 0);
    } else {
        std::string out = "";
        for (size_t i = 1; i < tokens.size(); ++i) {
            out += tokens[i] + (i == tokens.size() - 1 ? "" : " ");
        }
        append_output(out + "\n", 0);
    }
}

void CLEAR(vecS &tokens, LogCallback append_output) {
    {
        std::lock_guard<std::mutex> lock(output_mutex);


        append_output("\n", 3);
    }
}

void CD(vecS &tokens, LogCallback append_output) {
    update_current_directory();

    if (tokens.size() > 2) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("cd: too many arguments\n", 1);
        }
        return;
    }

    using namespace std::filesystem;
    std::string target_path;
    if (tokens.size() == 1) {
        target_path = expand_tilde("~");
    } else {
        target_path = expand_tilde(tokens[1]);
    }

    std::error_code ec;
    if (!exists(target_path, ec)) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("cd: no such file or directory: " + tokens[1] + "\n", 1);
        }
        return;
    }

    if (!is_directory(target_path, ec)) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("cd: not a directory: " + tokens[1] + "\n", 1);
        }
        return;
    }


    if (chdir(target_path.c_str()) != 0) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("cd: permission denied: " + tokens[1] + "\n", 1);
        }
        return;
    }


    update_current_directory();
}

void PWD(vecS &tokens, LogCallback append_output) {
    char *cwd = getcwd(nullptr, 0);
    std::string cwd_str = cwd ? std::string(cwd) : std::string(".");
    if (cwd) free(cwd);

    {
        std::lock_guard<std::mutex> lock(output_mutex);
        append_output(cwd_str + "\n", 0);
    }
}

void LS(vecS &tokens, LogCallback append_output) {
    using namespace std::filesystem;
    std::error_code ec;


    std::string target_dir = current_directory;
    std::string target_name = ".";

    if (tokens.size() > 1) {
        target_dir = expand_tilde(tokens[1]);
        target_name = tokens[1];
    } else {
        target_dir = expand_tilde(target_dir);
    }


    std::error_code abs_ec;
    std::filesystem::path abs_path = std::filesystem::absolute(std::filesystem::path(target_dir), abs_ec);
    if (!abs_ec) {
        target_dir = abs_path.string();
    }


    if (!exists(target_dir, ec)) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("ls: cannot access '" + target_name + "': No such file or directory\n", 1);
        }
        return;
    }

    if (!is_directory(target_dir, ec)) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("ls: '" + target_name + "' is not a directory\n", 1);
        }
        return;
    }


    try {
        std::priority_queue<std::string, std::vector<std::string>, std::greater<> > pq_nonhidden;
        std::priority_queue<std::string, std::vector<std::string>, std::greater<> > pq_hidden;
        size_t max_hidden_length = 0;

        for (const auto &entry: directory_iterator(target_dir, ec)) {
            std::string filename = entry.path().filename().string();


            if (filename[0] == '.') {
                pq_hidden.push(filename);
                max_hidden_length = std::max(max_hidden_length, filename.length());
            } else {
                pq_nonhidden.push(filename);
            }
        }

        size_t max_width = max_hidden_length + 5;
        size_t A = pq_nonhidden.size();
        size_t B = pq_hidden.size();

        {
            std::lock_guard<std::mutex> lock(output_mutex);
            for (size_t i = 0; i < A; ++i) {
                append_output(pq_nonhidden.top() + "\n", 0);
                pq_nonhidden.pop();
            }

            for (size_t i = 0; i < B; ++i) {
                std::string hidden_item = pq_hidden.top();
                size_t padding = max_width - hidden_item.length();
                std::string padded_output = hidden_item + std::string(padding, ' ') + "{Hidden}\n";
                append_output(padded_output, 0);
                pq_hidden.pop();
            }
        }
    } catch (const std::exception &e) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("ls: permission denied\n", 1);
        }
    }
}

void ENV(vecS &tokens, const LogCallback &append_output) {
    std::vector<std::pair<std::string, std::string> > entries;
    entries.reserve(env_vars.size());
    for (const auto &p: env_vars) entries.emplace_back(p.first, p.second);

    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    size_t max_key_len = 0;
    for (const auto &e: entries) max_key_len = std::max(max_key_len, e.first.size());

    const size_t pad = 3;
    size_t col_width = max_key_len + pad;

    std::lock_guard<std::mutex> lock(output_mutex);
    for (const auto &e: entries) {
        const std::string &k = e.first;
        const std::string &v = e.second;


        const size_t key_len = k.size();
        if (v.find(':') != std::string::npos) {
            append_output(k + "\n", 5);
            size_t start = 0;
            while (start <= v.size()) {
                size_t pos = v.find(':', start);
                std::string comp;
                if (pos == std::string::npos) {
                    comp = v.substr(start);
                    start = v.size() + 1;
                } else {
                    comp = v.substr(start, pos - start);
                    start = pos + 1;
                }
                if (!comp.empty()) {
                    std::string space = "";
                    space.append(key_len, ' ');
                    append_output(space + std::string("  [") + comp + "]\n", 0);
                }
            }
        } else {
            std::string keyPart = k;
            if (k.size() < col_width) keyPart += std::string(col_width - k.size(), ' ');
            append_output(keyPart, 4);
            append_output(v + "\n", 0);
        }
    }
}

bool is_valid_env_var(const std::string &key) {
    if (key.empty()) return false;
    return std::ranges::all_of(key, [](unsigned char c) {
        return std::isupper(c) || std::isdigit(c);
    });
}

std::unordered_map<string, string> set_vars;

void SET(const vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() == 1) {
        if (set_vars.empty()) {
            append_output("Warning: No environment variables set\n", 2);
            return;
        }
        for (auto &[k,v]: set_vars) {
            append_output(k, 5);
            append_output("=", 0);
            append_output(v, 0);
            append_output("\n", 0);
        }
        return;
    }


    if (tokens.size() > 2) {
        append_output("Usage: set VAR=value\n", 0);
        return;
    }
    const string &temp = tokens[1];
    const size_t pos = temp.find('=');
    const string &key = temp.substr(0, pos);
    const string &value = temp.substr(pos + 1);

    const bool ERR = !is_valid_env_var(key);

    try {
        setenv(key.c_str(), value.c_str(), 1);
        env_vars[key] = value;
        set_vars[key] = value;
        append_output(std::format("Set {}={}", key, value), 4);

        if (ERR) {
            append_output("Warning: Non-standard variable name.\n", 2);
        }
    } catch (...) {
        append_output("Invalid key or value\n", 1);
    }
}

void UNSET(const vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() != 2) {
        append_output("Usage: unset VAR", 1);
        return;
    }

    const string &key = tokens[1];

    if (!set_vars.contains(key)) {
        append_output("Warning: Variable 'VAR' is not set.", 2);
        return;
    }

    unsetenv(key.c_str());
    env_vars.erase(key);
    set_vars.erase(key);
    append_output(std::format("Unset {}", key), 4);
}


void HISTORY_cmd(vecS &tokens, const LogCallback &append_output) {
    if (cmd_history.empty()) {
        append_output("No commands in history.\n", 2);
        return;
    }

    if (tokens.size() == 1) {
        for (auto &cmd: cmd_history) {
            append_output(cmd.first, 5);
            append_output(": ", 5);
            append_output(cmd.second, 0);
            append_output("\n", 0);
        }
        return;
    }

    if (tokens[1] == "-c") {
        cmd_history.clear();
        return;
    }


    append_output("Usage: history\n", 0);
}

std::unordered_map<std::string, std::string> aliases;

void ALIAS_cmd(vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() == 1) {
        if (aliases.empty()) {
            append_output(std::format("Warning: No aliases with key = {} is set\n", ""), 2);
            return;
        }
        for (auto &[k,v]: aliases) {
            append_output(k, 5);
            append_output("=", 0);
            append_output(v, 0);
            append_output("\n", 0);
        }
        return;
    }


    if (tokens.size() > 2) {
        append_output("Usage: alias NAME=\"COMMAND\"\n", 1);
        return;
    }
    const string &temp = tokens[1];
    const size_t pos = temp.find('=');
    const string &key = temp.substr(0, pos);
    const string &value = temp.substr(pos + 1);

    try {
        aliases[key] = value;
        append_output(std::format("Set {}={}", key, value), 4);
    } catch (...) {
        append_output("Invalid key or value\n", 1);
    }
}

void UNALIAS_cmd(vecS &tokens, const LogCallback &append_output) {
    if (aliases.empty()) {
        append_output("Warning: No aliases set\n", 2);
        return;
    }

    if (tokens.size() > 2) {
        append_output("Usage: unalias NAME\n", 0);
        return;
    }

    if (!aliases.contains(tokens[1])) {
        append_output("Warning: No alias exists\n", 2);
        return;
    }

    aliases.erase(tokens[1]);
    append_output(std::format("Unset {}", tokens[1]), 4);
}

void TYPE(vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() < 2) {
        append_output("Usage: type command_name\n", 0);
        return;
    }

    for (size_t i = 1; i < tokens.size(); ++i) {
        const std::string &command = tokens[i];

        if (command_map.contains(command)) {
            append_output(
                command + " is a shell built-in command" + ((command == "ls") ? "(for my project shell only)" : "") +
                "\n", 6);
            continue;
        }

        if (aliases.contains(command)) {
            append_output(command + " is an alias for '" + aliases[command] + "'\n", 4);
            continue;
        }


        std::string path_env = env_vars["PATH"];
        std::istringstream path_stream(path_env);
        std::string path_dir;
        bool found = false;
        while (std::getline(path_stream, path_dir, ':')) {
            std::filesystem::path executable_path = std::filesystem::path(path_dir) / command;
            if (std::filesystem::exists(executable_path) && !std::filesystem::is_directory(executable_path)) {
                append_output(command + " is " + executable_path.string() + "\n", 0);
                found = true;
                break;
            }
        }

        if (!found) {
            append_output("type: " + command + ": not found\n", 1);
        }
    }
}


void WHICH(vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() < 2) {
        append_output("Usage: which command_name\n", 0);
        return;
    }

    for (size_t i = 1; i < tokens.size(); ++i) {
        const std::string &command = tokens[i];
        std::string path_env = env_vars["PATH"];
        std::istringstream path_stream(path_env);
        std::string path_dir;
        bool found = false;

        while (std::getline(path_stream, path_dir, ':')) {
            std::filesystem::path executable_path = std::filesystem::path(path_dir) / command;
            if (std::filesystem::exists(executable_path) && !std::filesystem::is_directory(executable_path)) {
                append_output(executable_path.string() + "\n", 0);
                found = true;
                break;
            }
        }

        if (!found) {
            append_output("which: no " + command + " in (" + path_env + ")\n", 1);
        }
    }
}

void MKDIR(vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() < 2) {
        append_output("Error: mkdir requires a directory name.\n", 1);
        return;
    }

    const std::string &dirname = tokens[1];


    if (std::filesystem::exists(dirname)) {
        append_output("Error: Directory '" + dirname + "' already exists.\n", 1);
        return;
    }


    try {
        if (std::filesystem::create_directory(dirname)) {
            append_output("Directory '" + dirname + "' created successfully!\n", 0);
        } else {
            append_output("Error creating directory\n", 1);
        }
    } catch (const std::filesystem::filesystem_error &e) {
        append_output("System Error: " + std::string(e.what()) + "\n", 1);
    }
}

void RMDIR(vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() < 2) {
        append_output("Error: rmdir requires a directory name.\n", 1);
        return;
    }

    const std::string &dirname = tokens[1];


    if (!std::filesystem::exists(dirname)) {
        append_output("Error: Directory '" + dirname + "' does not exist.\n", 1);
        return;
    }


    if (!std::filesystem::is_directory(dirname)) {
        append_output("Error: '" + dirname + "' is a file, not a directory.\n", 1);
        return;
    }


    try {
        if (std::filesystem::remove(dirname)) {
            append_output("Directory '" + dirname + "' removed successfully!\n", 0);
        } else {
            append_output("Error: Could not remove directory (it might not be empty).\n", 1);
        }
    } catch (const std::filesystem::filesystem_error &e) {
        append_output("System Error: " + std::string(e.what()) + "\n", 1);
    }
}

void TOUCH(vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() < 2) {
        append_output("Error: touch requires a filename.\n", 1);
        return;
    }

    const std::string &filename = tokens[1];


    std::ofstream file(filename, std::ios::app);

    if (file.is_open()) {
        append_output("File '" + filename + "' touched successfully.\n", 0);
    } else {
        append_output("Error: Could not create or open file.\n", 1);
    }
}

void RM(vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() < 2) {
        append_output("Error: rm requires a filename.\n", 1);
        return;
    }

    const std::string &filename = tokens[1];

    if (!std::filesystem::exists(filename)) {
        append_output("Error: File '" + filename + "' does not exist.\n", 1);
        return;
    }

    if (std::filesystem::is_directory(filename)) {
        append_output("Error: '" + filename + "' is a directory. Use rmdir instead.\n", 1);
        return;
    }

    try {
        if (std::filesystem::remove(filename)) {
            append_output("File '" + filename + "' deleted successfully.\n", 0);
        } else {
            append_output("Error deleting file.\n", 1);
        }
    } catch (const std::filesystem::filesystem_error &e) {
        append_output("System Error: " + std::string(e.what()) + "\n", 1);
    }
}

void CAT(vecS &tokens, const LogCallback &append_output) {
    if (tokens.size() < 2) {
        append_output("Error: cat requires a filename.\n", 1);
        return;
    }

    const std::string &filename = tokens[1];

    if (!std::filesystem::exists(filename) || std::filesystem::is_directory(filename)) {
        append_output("Error: File '" + filename + "' does not exist or is a directory.\n", 1);
        return;
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        append_output("Error: Could not open file.\n", 1);
        return;
    }

    std::string line;
    std::string content = "";
    while (std::getline(file, line)) {
        content += line + "\n";
    }

    append_output(content, 0);
}

void HELP(vecS &tokens, const LogCallback &append_output) {
    append_output("ParryShell - A simple custom shell implementation\n", 4);
    append_output("Built-in commands:\n", 4);
    append_output(" exit [code]       ", 2);
    append_output("Exit the shell with an optional exit code.\n", 0);
    append_output(" echo [args...]   ", 2);
    append_output("Print arguments to the console. Supports output redirection with '>'.\n", 0);
    append_output(" cls, clear       ", 2);
    append_output("Clear the console output.\n", 0);
    append_output(" cd [dir]         ", 2);
    append_output("Change the current directory. Defaults to home if no directory is specified.\n", 0);
    append_output(" pwd               ", 2);
    append_output("Print the current working directory.\n", 0);
    append_output(" ls [dir]         ", 2);
    append_output("List files & folders in the specified directory (default: current directory).\n", 0);
    append_output(" env               ", 2);
    append_output("Display all environment variables.\n", 0);
    append_output(" set VAR=value     ", 2);
    append_output("Set an environment variable.\n", 0);
    append_output(" unset VAR         ", 2);
    append_output("Unset an environment variable.\n", 0);
    append_output(" alias NAME=\"CMD\" ", 2);
    append_output("Create an alias for a command.\n", 0);
    append_output(" unalias NAME      ", 2);
    append_output("Remove an alias.\n", 0);
    append_output(" history           ", 2);
    append_output("Show command history. Use 'history -c' to clear history.\n", 0);
    append_output(" which CMD         ", 2);
    append_output("Show the full path of a command if it's an executable in PATH.\n", 0);
    append_output(" type CMD          ", 2);
    append_output("Show whether CMD is a built-in, alias, or external command.\n", 0);
    //add more
    //append_output("External commands are also supported and will be executed if found in the system PATH.\n", 4);
}

void init() {
    update_current_directory();

    command_map["exit"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(EXIT);
    command_map["echo"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(ECHO);
    command_map["cls"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(CLEAR);
    command_map["clear"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(CLEAR);
    command_map["cd"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(CD);
    command_map["pwd"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(PWD);
    command_map["ls"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(LS);
    command_map["env"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(ENV);
    command_map["set"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(SET);
    command_map["unset"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(UNSET);
    command_map["alias"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(ALIAS_cmd);
    command_map["history"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(HISTORY_cmd);
    command_map["unalias"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(UNALIAS_cmd);
    command_map["which"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(WHICH);
    command_map["type"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(TYPE);
    command_map["mkdir"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(MKDIR);
    command_map["rmdir"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(RMDIR);
    command_map["touch"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(TOUCH);
    command_map["rm"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(RM);
    command_map["cat"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(CAT);
    command_map["help"] = reinterpret_cast<std::unordered_map<string, CommandFunc>::mapped_type>(HELP);
}

std::string handle_tab_completion(const std::string &currentInput) {
    std::string rawText = currentInput;
    size_t lastSpace = rawText.find_last_of(" \t");
    std::string partialWord = (lastSpace == std::string::npos) ? rawText : rawText.substr(lastSpace + 1);
    return get_tab_completion(partialWord);
}

string get_tab_completion(const std::string &partial_input) {
    using namespace std::filesystem;
    if (partial_input.empty()) return "";

    std::string search_dir = current_directory.empty() ? std::string(".") : current_directory;
    std::string prefix = partial_input;


    size_t last_slash = partial_input.find_last_of('/');
    if (last_slash != std::string::npos) {
        search_dir = partial_input.substr(0, last_slash);
        if (search_dir.empty()) search_dir = "/";
        search_dir = expand_tilde(search_dir);

        prefix = partial_input.substr(last_slash + 1);
    }


    search_dir = expand_tilde(search_dir);
    std::error_code ec;
    std::filesystem::path abs_path = std::filesystem::absolute(std::filesystem::path(search_dir), ec);
    if (!ec) search_dir = abs_path.string();

    if (!exists(search_dir, ec) || !is_directory(search_dir, ec)) return "";

    std::vector<std::string> matches;
    for (const auto &entry: directory_iterator(search_dir, ec)) {
        std::string name = entry.path().filename().string();

        if (prefix.empty() || name.rfind(prefix, 0) == 0) {
            if (is_directory(entry.path(), ec)) {
                name += "/";
            }
            matches.push_back(name);
        }
    }

    if (matches.size() == 1) {
        return matches[0].substr(prefix.length());
    }

    return "";
}

std::vector<char *> build_argv(const std::vector<std::string> &tokens) {
    std::vector<char *> argv;

    argv.reserve(tokens.size());

    for (const auto &s: tokens) {
        argv.push_back(const_cast<char *>(s.c_str()));
    }

    argv.push_back(nullptr);

    return argv;
}


void handle_single(const vecS &tokens, LogCallback append_output) {
    const string &cmd = tokens[0];
    append_output("\n", 0);

    const auto it_cmd = command_map.find(cmd);
    if (it_cmd != command_map.end()) {
        it_cmd->second(tokens, std::move(append_output));
    } else {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("Command not found: " + cmd, 1);
        }
    }

    append_output("\n", 0);
}

void handle_(const vecS &tokens, const size_t arg_len, const LogCallback &append_output) {
    if (arg_len > 4096) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("Error: Command too long. Maximum 4096 characters.\n", 1);
        }
        return;
    }


    if (tokens.size() > 64) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output("Error: Too many arguments. Maximum 64 allowed.\n", 1);
        }
        return;
    }


    for (const auto &token: tokens) {
        if (token == "&&" || token == "||" || token == ";") {
            {
                std::lock_guard<std::mutex> lock(output_mutex);
                append_output("Error: Command chaining (&&, ||, ;) is not supported in ParryShell.\n", 1);
            }
            return;
        }
    }

    if (tokens.empty()) return;

    if (pending_exit_confirmation) {
        pending_exit_confirmation = false;
        if (!tokens.empty() && (tokens[0] == "y" || tokens[0] == "Y")) {
            if (active_child_pid > 0) {
                kill(active_child_pid, SIGKILL);
            }
            QCoreApplication::exit(0);
        } else {
            {
                std::lock_guard<std::mutex> lock(output_mutex);
                append_output("Exit cancelled.\n", 0);
            }
            return;
        }
    }

    if (string err = error_handling(tokens, arg_len); !err.empty()) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            append_output(err, 1);
        }
        return;
    }

    const auto it = std::find(tokens.begin(), tokens.end(), "|");

    if (it == tokens.end()) {
        handle_single(tokens, append_output);
    } else {
        vecS left(tokens.begin(), it);
        vecS right(it + 1, tokens.end());

        if (left.empty() || right.empty()) {
            {
                std::lock_guard<std::mutex> lock(output_mutex);
                append_output("Error: Invalid pipe command.\n", 1);
            }
            return;
        }


        std::thread([left, right, append_output]() {
            int fd[2];
            int out[2];

            if (pipe(fd) < 0 || pipe(out) < 0) {
                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    append_output("Error: Failed to create system pipes.\n", 1);
                }
                return;
            }

            pid_t pid1 = fork();
            if (pid1 == 0) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
                close(out[0]);
                close(out[1]);

                const auto argv = build_argv(left);
                execvp(argv[0], argv.data());
                _exit(127);
            }

            pid_t pid2 = fork();
            if (pid2 == 0) {
                dup2(fd[0], STDIN_FILENO);
                dup2(out[1], STDOUT_FILENO);
                dup2(out[1], STDERR_FILENO);

                close(fd[0]);
                close(fd[1]);
                close(out[0]);
                close(out[1]);

                const auto argv = build_argv(right);
                execvp(argv[0], argv.data());
                _exit(127);
            }

            active_child_pid = pid2;


            close(fd[0]);
            close(fd[1]);
            close(out[1]);

            char buf[4096];
            ssize_t bytes;
            size_t totalBytes = 0;
            bool truncated = false;


            while ((bytes = read(out[0], buf, sizeof(buf))) > 0) {
                totalBytes += bytes;
                if (totalBytes > 10 * 1024 * 1024) {
                    kill(pid2, SIGKILL);
                    kill(pid1, SIGKILL);
                    truncated = true;
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    append_output(std::string(buf, bytes), 0);
                }
            }

            close(out[0]);

            int status1, status2;
            waitpid(pid1, &status1, 0);

            int term_status = waitpid(pid2, &status2, WNOHANG);
            if (term_status == 0) {
                usleep(2000000);
                term_status = waitpid(pid2, &status2, WNOHANG);
                if (term_status == 0) {
                    kill(pid2, SIGKILL);
                    waitpid(pid2, &status2, 0);
                    std::lock_guard<std::mutex> lock(output_mutex);
                    append_output("\n^C", 1);
                    append_output("Process terminated.\n", 2);
                }
            }


            active_child_pid = 0;

            if (truncated) {
                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    append_output("\nWarning: Output truncated. Process output exceeded 10 MB limit.\n", 2);
                }
            } else if (WIFEXITED(status2) && WEXITSTATUS(status2) != 0) {
                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    append_output("\nProcess exited with code: " + std::to_string(WEXITSTATUS(status2)) + "\n", 2);
                }
            }


            {
                std::lock_guard<std::mutex> lock(output_mutex);
                append_output("\n", 0);
            }
        }).detach();
    }
}