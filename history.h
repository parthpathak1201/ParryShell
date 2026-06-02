//
// Created by Parth Pathak on 01/06/26.
//

#ifndef PARRYSHELL_HISTORY_H
#define PARRYSHELL_HISTORY_H

#include <QDateTime>
#include <string>
#include <vector>
extern std::vector<std::pair<std::string, std::string>> cmd_history;

namespace HIS {
    void addHistory(const std::string &command);
}

#endif //PARRYSHELL_HISTORY_H
