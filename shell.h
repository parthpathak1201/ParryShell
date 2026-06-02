//
// Created by Parth Pathak on 17/05/26.
//
#pragma once
#ifndef PARRYSHELL_SHELL_H
#define PARRYSHELL_SHELL_H

#include <functional>
#include <vector>
#include <string>


using LogCallback = std::function<void(const std::string& MESSAGE, int COLOR)>;
void init_env_vars();
extern pid_t active_child_pid;
std::string handle_tab_completion(const std::string &currentInput);
std::string get_tab_completion(const std::string& partial_input);
void handle_(const std::vector<std::string> &tokens, size_t arg_len, const LogCallback& append_output);
void init();
void request_interrupt();

extern std::unordered_map<std::string, std::string> aliases;


#endif //PARRYSHELL_SHELL_H