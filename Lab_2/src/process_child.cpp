#include <iostream>
#include <cstdlib>
#include <string>

#ifdef _WIN32
    #include <windows.h>
    #define sleep(seconds) Sleep((seconds) * 1000)
#else
    #include <unistd.h>
#endif


int main(int argc, char *argv[]) {
    if (argc > 1) {
#ifdef _WIN32
        int process_number = std::atoi(argv[2]);
        std::cout << "Child process " << process_number << " (PID: " << GetCurrentProcessId() << ") started." << std::endl;
#else
        int process_number = std::atoi(argv[1]);
        std::cout << "Child process " << process_number << " (PID: " << getpid() << ") started." << std::endl;
#endif
        std::cout << "Child process " << process_number << " is sleeping for " << process_number << " seconds." << std::endl;
        sleep(process_number);
        std::cout << "Child process " << process_number << " finished." << std::endl;
        return process_number;
    }
    std::cerr << "No process number provided!" << std::endl;
    return -1;
}