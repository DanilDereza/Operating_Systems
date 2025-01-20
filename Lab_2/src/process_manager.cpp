#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
    #define sleep(seconds) Sleep((seconds) * 1000)

    using Semaphore = HANDLE;
    using Thread = HANDLE;

    #define init_semaphore(semaphore, count) semaphore = CreateSemaphore(nullptr, count, 1, nullptr)
    #define delete_semaphore(semaphore) CloseHandle(semaphore)
    #define lock_semaphore(semaphore) WaitForSingleObject(semaphore, INFINITE)
    #define unlock_semaphore(semaphore) ReleaseSemaphore(semaphore, 1, nullptr)

#else
    #include <pthread.h>
    #include <unistd.h>
    #include <sys/wait.h>
    #include <semaphore.h>
    #include <limits.h>

    using Semaphore = sem_t;
    using Thread = pthread_t;

    #define init_semaphore(semaphore, count) sem_init(&semaphore, 0, count)
    #define delete_semaphore(semaphore) sem_destroy(&semaphore)
    #define lock_semaphore(semaphore) sem_wait(&semaphore)
    #define unlock_semaphore(semaphore) sem_post(&semaphore)

    Semaphore global_semaphore;
#endif

struct ProgramConfig { // Holds configuration details for a program
    std::string executable; 
    std::vector<std::string> arguments;
};

int duration = 4; // Number of iterations or time period for which the main process will run
int completed_processes = 0; // Number of child processes that have finished execution


void demonstrate_main_process_running(int duration_time) { // Simulates the main process running for duration_time (Windows, Linux)
    for (int i = 0; i < duration_time; ++i) {
        std::cout << std::endl << "!!!THE MAIN PROCESS IS RUNNING!!!\n" << std::endl;
        sleep(1);
    }
}

#ifdef _WIN32
    void monitor_process(PROCESS_INFORMATION &process_info, Semaphore &semaphore) { // Monitors a child process (Windows)
        std::cout << "Monitoring child processes..." << std::endl;

        while (true) {
            DWORD wait_result = WaitForSingleObject(process_info.hProcess, INFINITE);

            if (wait_result == WAIT_OBJECT_0) {
                DWORD exit_code;
                if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
                    std::cerr << "Failed to retrieve exit code (" << GetLastError() << ").\n";
                } else {
                    std::cout << "Child process " << process_info.dwProcessId << " exited with code " << exit_code << std::endl;
                }
                CloseHandle(process_info.hProcess);
                CloseHandle(process_info.hThread);
                lock_semaphore(semaphore);
                ++completed_processes;
                unlock_semaphore(semaphore);
                break;
            } else {
                std::cerr << "Error waiting for process " << process_info.dwProcessId << ": " << GetLastError() << std::endl;
                break;
            }
        }
    }

    std::wstring to_wstring(const std::string &str) { // Function to convert ANSI string to Unicode string (Windows)
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
        return wstr;
    }

    void start_processes(std::vector<ProgramConfig> &program_configs, int process_count, bool wait_for_children) { // Creates and manages multiple child processes (Windows)
        Semaphore semaphore;
        if (!wait_for_children)
            init_semaphore(semaphore, 1);

        STARTUPINFOW startup_info = {};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info[process_count];

        std::cout << "Starting to create child processes..." << std::endl;

        for (int i = 0; i < process_count; ++i) {
            std::string command = program_configs[i].executable;
            for (const auto &arg : program_configs[i].arguments) {
                command += " " + arg;
            }

            std::wstring command_wstr = to_wstring(command);
            std::cout << "Creating process " << i + 1 << " with command: " << command << std::endl;

            if (!CreateProcessW(nullptr, &command_wstr[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup_info, &process_info[i])) {
                std::cerr << "Failed to create process (" << GetLastError() << "). Command: " << command << std::endl;
                exit(1);
            } else {
                std::cout << "Parent created child process with PID " << process_info[i].dwProcessId << std::endl;
            }

            if (wait_for_children) {
                WaitForSingleObject(process_info[i].hProcess, INFINITE);
                DWORD exit_code;
                if (!GetExitCodeProcess(process_info[i].hProcess, &exit_code)) {
                    std::cerr << "Failed to retrieve exit code for process " << process_info[i].dwProcessId << " (" << GetLastError() << ")." << std::endl;
                    exit(1);
                }
                std::cout << "Child process " << process_info[i].dwProcessId << " exited with code " << exit_code << std::endl;
            } else {
                Thread thread;
                thread = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)monitor_process, &process_info[i], 0, nullptr);
                if (!thread) {
                    std::cerr << "Error creating thread for monitoring child process " << process_info[i].dwProcessId << ": " << GetLastError() << std::endl;
                    exit(1);
                }
                CloseHandle(thread);
                std::cout << "Started monitoring thread for child process " << process_info[i].dwProcessId << std::endl;
            }
        }

        demonstrate_main_process_running(duration);

        if (!wait_for_children) {
            for (int i = 0; i < process_count; ++i) {
                WaitForSingleObject(process_info[i].hThread, INFINITE);
            }
            delete_semaphore(semaphore);
        }

        std::cout << "Finished process creation and monitoring setup." << std::endl;
    }
#else
    void *monitor_process(void *) { // Monitors a child process (Linux)
        std::cout << "Monitoring child processes..." << std::endl;

        while (true) {
            int status;
            pid_t pid = waitpid(-1, &status, 0);

            if (pid > 0) {
                if (WIFEXITED(status)) {
                    std::cout << "Child process " << pid << " exited with code " << WEXITSTATUS(status) << std::endl;
                    lock_semaphore(global_semaphore);
                    ++completed_processes;
                    unlock_semaphore(global_semaphore);
                }
            } else if (pid == -1) {
                sleep(1);
                if (completed_processes > 0) {
                    break;
                }
            }
        }
        return nullptr;
    }

    void start_processes(std::vector<ProgramConfig> &program_configs, int process_count, bool wait_for_children) { // Creates and manages multiple child processes (Linux)
        init_semaphore(global_semaphore, 1);

        std::cout << "Starting to create child processes..." << std::endl;

        for (int i = 0; i < process_count; ++i) {
            pid_t pid = fork();
            if (pid == 0) {
                std::vector<char *> args;
                for (const auto &arg : program_configs[i].arguments) {
                    args.push_back(const_cast<char *>(arg.c_str()));
                }
                args.push_back(nullptr);
                std::cout << "Creating process " << i + 1 << " with command: " << program_configs[i].executable << std::endl;
                for (const auto &arg : program_configs[i].arguments) {
                    std::cout << arg << " ";
                }
                std::cout << std::endl;
                execv(program_configs[i].executable.c_str(), args.data());
                std::cerr << "Failed to execute command in child process." << std::endl;
                exit(1);
            } else if (pid > 0) {
                std::cout << "Parent created child process with PID " << pid << std::endl;
                if (wait_for_children) {
                    int status;
                    waitpid(pid, &status, 0);
                    std::cout << "Child process " << pid << " exited with code " << WEXITSTATUS(status) << std::endl;
                } else {
                    Thread thread;
                    int result = pthread_create(&thread, nullptr, monitor_process, nullptr);
                    if (result != 0) {
                        std::cerr << "Error creating thread: " << strerror(result) << std::endl;
                        exit(1);
                    }           
                    pthread_detach(thread);
                    std::cout << "Started monitoring thread for child process " << pid << std::endl;
                }
            } else {
                std::cerr << "Fork error: " << std::strerror(errno) << std::endl;
                exit(1);
            }
        }

        demonstrate_main_process_running(duration);

        if (!wait_for_children) {
            while (completed_processes < process_count) {
                sleep(1);
            }
            delete_semaphore(global_semaphore);
        }
        std::cout << "Finished process creation and monitoring setup." << std::endl;
    }
#endif