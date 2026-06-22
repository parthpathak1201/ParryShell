//
// Created by Parth Pathak on 17/05/26.
//

#include "history.h"

#include <iostream>

std::vector<std::pair<std::string, std::string> > cmd_history; //{timestamp, command}
namespace HIS {
    void addHistory(const std::string &command) {
        if (cmd_history.size() && (cmd_history.back().second == command)) {
            return;
        }
        cmd_history.emplace_back(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString(), command);
        if (cmd_history.size() > 500) {
            cmd_history.erase(cmd_history.begin(), cmd_history.begin() + 150);
        }
    }
} // namespace HIS
