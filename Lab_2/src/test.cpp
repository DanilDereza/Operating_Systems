#include <iostream>
#include <vector>
#include <string>

struct ProgramConfig { // Holds configuration details for a program
    std::string executable; 
    std::vector<std::string> arguments;
};

void start_processes(std::vector<ProgramConfig> &program_configs, int process_count, bool wait_for_children);


int main(int argc, char *argv[])
{
    const int Programs_count = 3;
    const bool Is_wait_for_children = true;

    std::vector<ProgramConfig> program_configs;
    for (int i = 0; i < Programs_count; ++i){
        ProgramConfig config;
        config.executable = "process_child";
        config.arguments = {"process_child", std::to_string(i+1), "NULL"};
        program_configs.push_back(config);
    }

    std::cout << "Starting with BLOCK_PARENT = 1 (Parent will wait for children)" << std::endl;
    start_processes(program_configs, Programs_count, Is_wait_for_children);

    std::cout << std::endl << "\nStarting with BLOCK_PARENT = 0 (Parent will not wait for children)" << std::endl;
    start_processes(program_configs, Programs_count, !Is_wait_for_children);

    return 0;
}