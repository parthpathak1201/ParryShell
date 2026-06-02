#include <iostream>
#include "window.h"
#include "colors.h"
#include "shell.h"





int main() {
    std::cout<< COLOR::RGB::INFO << "ParryShell loaded successfully." << COLOR::RESET << std::endl;
    init();
    init_env_vars();
    WIN();

    std::cout<< COLOR::RGB::SUCCESS << "ParryShell exited successfully." << COLOR::RESET << std::endl;

    return 0;
}
