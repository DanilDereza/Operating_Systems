#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdint>

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    #include <io.h>
    #include <conio.h>
    #include <codecvt>
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/time.h>
    #include <sys/mman.h>
    #include <fcntl.h>
    #include <semaphore.h>
    #include <termios.h>
    #include <sys/select.h>
    #include <cstdint>
    #include <csignal>
    #include <pthread.h>
#endif
#include <limits>

// IPC Constants
#define SHM_NAME "my_shared_memory"
#define MUTEX_NAME "my_mutex"
#define LEADER_MUTEX_NAME "leader_mutex"

struct SharedData { // Shared data
    std::int64_t counter;
};

// Cross-platform shared memory & mutex functions
void* create_shared_memory(size_t size);
void* map_shared_memory(void* shm);
bool create_mutex(void** mutex);
bool lock_mutex(void* mutex);
void unlock_mutex(void* mutex);
void close_shared_memory(void* shm, void* sharedData);
void close_mutex(void* mutex);


void log_message(std::ofstream& logFile, const std::string& message); // Function to add message to log (Windows, Linux)
std::string get_current_time_ms(); // Get current time with milliseconds (Windows, Linux)

#ifdef _WIN32
    DWORD WINAPI timer_thread(LPVOID lpParam); // Timer function (Windows)
    std::wstring to_wstring(const std::string &str); // Function to convert ANSI string to Unicode string
    std::string wstring_to_string(const std::wstring& wstr); // Function to convert Unicode string to a UTF-8 encoded std::string (Windows)
    LARGE_INTEGER get_current_time_large_integer(); // Get the current time as a LARGE_INTEGER (Windows)
    double get_time_diff_seconds(LARGE_INTEGER start, LARGE_INTEGER end); // Function to calculate the time difference in seconds (Windows)
    BOOL WINAPI ConsoleHandler(DWORD dwCtrlType); // Handle a signal, clean up resources and exit the process (Windows)
#else
    void* timer_thread(void* arg); // Timer function (Linux)
    void signal_handler(int signum); // Handle a signal, clean up resources and exit the process (Linux)
    double get_time_diff_seconds(struct timeval start, struct timeval end); // Function to calculate the time difference in seconds (Linux)
    struct timeval get_current_time_timeval(); // Get the current time as a timeval structur (Linux)
#endif

void process_user_input(SharedData* sharedData, void* mutex); // Handle user input (Windows, Linux)

// Child Process spawning
#ifdef _WIN32
    bool spawn_process1(std::ofstream& logFile, const std::string& appPath, bool& child_running, HANDLE& hProcess);
    bool spawn_process2(std::ofstream& logFile, const std::string& appPath, bool& child_running, HANDLE& hProcess);
#else
    bool spawn_process1(std::ofstream& logFile, const std::string& appPath, bool& child_running);
    bool spawn_process2(std::ofstream& logFile, const std::string& appPath, bool& child_running);
#endif

// Leader Election
bool acquire_leader_mutex(void** mutex); // Function to acquire a leader mutex, returning true if successful
void release_leader_mutex(void* mutex); // Function to release and close a previously acquired leader mutex
bool is_leader(void* mutex); // Function to check if the current process holds the leader mutex

// Signal handling
#ifndef GLOBALS_H
#define GLOBALS_H

extern SharedData* sharedData_global;
extern void* shm_global;
extern void* mutex_global;
extern void* leaderMutex_global;
#endif // GLOBALS_H

SharedData* sharedData_global = nullptr;
void* shm_global = nullptr;
void* mutex_global = nullptr;
void* leaderMutex_global = nullptr;

struct ThreadData { // Struct to pass data to the thread
    SharedData* sharedData;
    void* mutex;
};


int main(int argc, char* argv[]) {
    // Get application path
    std::string appPath = argv[0];

    // Shared Memory
    void* shm = create_shared_memory(sizeof(SharedData));
    if (shm == nullptr) {
        std::cerr << "Failed to create shared memory" << std::endl;
        return 1;
    }

    SharedData* sharedData = static_cast<SharedData*>(map_shared_memory(shm));
    if (sharedData == nullptr) {
        std::cerr << "Failed to map shared memory" << std::endl;
        close_shared_memory(shm, nullptr);
        return 1;
    }
    // Mutex for counter access
    void* mutex = nullptr;
    if (!create_mutex(&mutex)) {
        std::cerr << "Failed to create mutex" << std::endl;
        close_shared_memory(shm, sharedData);
        return 1;
    }
    // Mutex for leader
    void* leaderMutex = nullptr;
    if (!acquire_leader_mutex(&leaderMutex)) {
        std::cerr << "Failed to acquire leader mutex" << std::endl;
        close_mutex(mutex);
        close_shared_memory(shm, sharedData);
        return 1;
    }
    // Set global variables for signal handler
    sharedData_global = sharedData;
    shm_global = shm;
    mutex_global = mutex;
    leaderMutex_global = leaderMutex;

    // Register signal handler
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#else
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = signal_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);
#endif
    // Initialize shared data
    sharedData->counter = 0;

    // Create log file
    std::ofstream logFile;
    std::string logFileName = "my_log.log";
    logFile.open(logFileName, std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Error opening log file!" << std::endl;
        release_leader_mutex(leaderMutex);
        close_mutex(mutex);
        close_shared_memory(shm, sharedData);
        return 1;
    }
    // Write start info to log
    std::stringstream startMessage;
    startMessage << "Process started. PID: " << getpid() << ", Time: " << get_current_time_ms() << std::endl;
    log_message(logFile, startMessage.str());

    // Timer Thread
#ifdef _WIN32
    HANDLE timer_handle;
    ThreadData* threadData = new ThreadData{ sharedData,mutex };
    timer_handle = CreateThread(NULL, 0, timer_thread, (LPVOID)threadData, 0, NULL);
    if (timer_handle == NULL) {
        std::cerr << "Failed to create timer thread" << std::endl;
        release_leader_mutex(leaderMutex);
        close_mutex(mutex);
        close_shared_memory(shm, sharedData);
        return 1;
    }
    CloseHandle(timer_handle);
#else
    pthread_t timer_thread_id;
    ThreadData* threadData = new ThreadData{ sharedData,mutex };
    int result = pthread_create(&timer_thread_id, NULL, timer_thread, (void*)threadData);
    if (result != 0) {
        std::cerr << "Failed to create timer thread" << std::endl;
        release_leader_mutex(leaderMutex);
        close_mutex(mutex);
        close_shared_memory(shm, sharedData);
        return 1;
    }
    pthread_detach(timer_thread_id);
#endif

    // Track child processes
    bool child1_running = false;
    bool child2_running = false;
#ifdef _WIN32
    HANDLE child1_hProcess = NULL;
    HANDLE child2_hProcess = NULL;
#endif

#ifdef _WIN32
    LARGE_INTEGER last_spawn_time = get_current_time_large_integer();
    LARGE_INTEGER last_log_time = get_current_time_large_integer();
#else
    struct timeval last_spawn_time = get_current_time_timeval();
    struct timeval last_log_time = get_current_time_timeval();
#endif
    // Main Loop
    while (true) {
        process_user_input(sharedData, mutex);
        if (is_leader(leaderMutex)) {
            // Write counter to log every 1 sec
#ifdef _WIN32
            LARGE_INTEGER now = get_current_time_large_integer();
            double duration = get_time_diff_seconds(last_log_time, now);
            if (duration >= 1.0) {
                std::stringstream log_message_str;
                log_message_str << get_current_time_ms() << " - PID: " << getpid() << " - Counter: " << sharedData->counter << std::endl;
                log_message(logFile, log_message_str.str());
                last_log_time = now;
            }
            // Launch child processes every 3 sec
            duration = get_time_diff_seconds(last_spawn_time, now);
            if (duration >= 3.0) {
#else
            struct timeval now = get_current_time_timeval();
            double duration = get_time_diff_seconds(last_log_time, now);
            if (duration >= 1.0) {
                std::stringstream log_message_str;
                log_message_str << get_current_time_ms() << " - PID: " << getpid() << " - Counter: " << sharedData->counter << std::endl;
                log_message(logFile, log_message_str.str());
                last_log_time = now;
            }

            duration = get_time_diff_seconds(last_spawn_time, now);
            if (duration >= 3.0) {
#endif
                bool child1_spawned = false;
                bool child2_spawned = false;
                if (!child1_running) {
#ifdef _WIN32
                    child1_spawned = spawn_process1(logFile, appPath, child2_running, child1_hProcess); 
#else
                    child1_spawned = spawn_process1(logFile, appPath, child1_running);
#endif
                    child1_running = child1_spawned;
                }
                else {
                    log_message(logFile, "Child 1 skipped (still running).");
                }

                if (!child2_running) {
#ifdef _WIN32
                    child2_spawned = spawn_process2(logFile, appPath, child2_running, child2_hProcess); 
#else
                    child2_spawned = spawn_process2(logFile, appPath, child2_running);
#endif
                    child2_running = child2_spawned;
                }
                else {
                    log_message(logFile, "Child 2 skipped (still running).");
                }

                last_spawn_time = now;
            }
        }
        // Check childs status
#ifdef _WIN32
        if (child1_running) {
                DWORD exitCode;
                if (WaitForSingleObject(child1_hProcess, 0) == WAIT_OBJECT_0) {
                    if (GetExitCodeProcess(child1_hProcess, &exitCode)) {
                        child1_running = false;
                    }
                    CloseHandle(child1_hProcess);
                }

         }
        if (child2_running) {
           DWORD exitCode;
                if (WaitForSingleObject(child2_hProcess, 0) == WAIT_OBJECT_0) {
                   if (GetExitCodeProcess(child2_hProcess, &exitCode)) {
                        child2_running = false;
                     }
                    CloseHandle(child2_hProcess);
                }
        }
#else
        if (child1_running) {
            int status;
            pid_t pid = waitpid(-1, &status, WNOHANG);
            if (pid > 0) {
                child1_running = false;
            }
        }
        if (child2_running) {
            int status;
            pid_t pid = waitpid(-1, &status, WNOHANG);
            if (pid > 0) {
                child2_running = false;
            }
        }
#endif

#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif

    }
    return 0;
}


#ifdef _WIN32
    LARGE_INTEGER get_current_time_large_integer() {
        LARGE_INTEGER time;
        QueryPerformanceCounter(&time);
        return time;
    }

    double get_time_diff_seconds(LARGE_INTEGER start, LARGE_INTEGER end) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return static_cast<double>(end.QuadPart - start.QuadPart) / freq.QuadPart;
    }

    BOOL WINAPI ConsoleHandler(DWORD dwCtrlType) {
        switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
            std::cout << "Ctrl+C or Close event detected. Cleaning up..." << std::endl;
            // Release leader mutex
            if (leaderMutex_global != nullptr) {
                release_leader_mutex(leaderMutex_global);
                leaderMutex_global = nullptr;
            }

            // Release regular mutex
            if (mutex_global != nullptr) {
                close_mutex(mutex_global);
                mutex_global = nullptr;
            }

            // Unmap and close shared memory
            if (shm_global != nullptr && sharedData_global != nullptr) {
                close_shared_memory(shm_global, sharedData_global);
                shm_global = nullptr;
                sharedData_global = nullptr;
            }
            // Exit process
            exit(0);
            return TRUE;
        default:
            return FALSE;
        }
    }

    DWORD WINAPI timer_thread(LPVOID lpParam) {
        ThreadData* data = (ThreadData*)lpParam;
        SharedData* sharedData = data->sharedData;
        void* mutex = data->mutex;
        while (true) {
            Sleep(300);
            lock_mutex(mutex);
            sharedData->counter++;
            unlock_mutex(mutex);
        }
        return 0;
    }

    void process_user_input(SharedData* sharedData, void* mutex) {
        if (_kbhit()) {
            std::string input;
            std::cin >> input;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            try {
                std::int64_t new_count = std::stoll(input);
                lock_mutex(mutex);
                sharedData->counter = new_count;
                unlock_mutex(mutex);
                std::cout << "Counter set to: " << new_count << std::endl;
            }
            catch (std::invalid_argument const& ex) {
                std::cerr << "Invalid input" << std::endl;
            }
            catch (std::out_of_range const& ex) {
                std::cerr << "Input out of range" << std::endl;
            }
        }
    }

    std::wstring to_wstring(const std::string &str) {
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
        return wstr;
    }

    std::string wstring_to_string(const std::wstring& wstr) {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(wstr);
    }

    void* create_shared_memory(size_t size) {
        std::string shm_name = wstring_to_string(to_wstring(SHM_NAME));
        HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shm_name.c_str());

        if (hMapFile == NULL) {
            hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, to_wstring(SHM_NAME).c_str());
            if (hMapFile == NULL) {
                return nullptr;
            }
        }
        return hMapFile;
    }

    void* map_shared_memory(void* shm) {
        HANDLE hMapFile = static_cast<HANDLE>(shm);
        void* shmem_ptr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (shmem_ptr == NULL) {
            return nullptr;
        }
        return shmem_ptr;
    }

    bool create_mutex(void** mutex) {
        std::string mutex_name = wstring_to_string(to_wstring(MUTEX_NAME));
        HANDLE hMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, mutex_name.c_str());
        if (hMutex == NULL) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND) {
                hMutex = CreateMutexW(NULL, FALSE, to_wstring(MUTEX_NAME).c_str());
                if (hMutex == NULL) {
                    return false;
                }
                *mutex = hMutex;
                return true;
            }

            return false;
        }
        *mutex = hMutex;
        return true;
    }

    bool lock_mutex(void* mutex) {
        HANDLE hMutex = static_cast<HANDLE>(mutex);
        if (WaitForSingleObject(hMutex, INFINITE) == WAIT_FAILED) {
            return false;
        }
        return true;
    }

    void unlock_mutex(void* mutex) {
        HANDLE hMutex = static_cast<HANDLE>(mutex);
        ReleaseMutex(hMutex);
    }

    void close_shared_memory(void* shm, void* sharedData) {
        HANDLE hMapFile = static_cast<HANDLE>(shm);
        if (sharedData != nullptr) UnmapViewOfFile(sharedData);
        CloseHandle(hMapFile);
    }

    void close_mutex(void* mutex) {
        HANDLE hMutex = static_cast<HANDLE>(mutex);
        CloseHandle(hMutex);
    }

    bool acquire_leader_mutex(void** mutex) {
        HANDLE hMutex = CreateMutexW(NULL, TRUE, to_wstring(LEADER_MUTEX_NAME).c_str());
        if (hMutex == NULL) {
            return false;
        }
        *mutex = hMutex;
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            *mutex = nullptr;
            return true;
        }
        return true;
    }

    void release_leader_mutex(void* mutex) {
        if (mutex != nullptr) {
            ReleaseMutex(static_cast<HANDLE>(mutex));
            CloseHandle(static_cast<HANDLE>(mutex));
        }
    }

    bool is_leader(void* mutex) {
        return mutex != nullptr;
    }

    bool spawn_process1(std::ofstream& logFile, const std::string& appPath, bool& child_running, HANDLE& hProcess) {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        std::string childPath = "child1.exe";
        if (!CreateProcessW(to_wstring(childPath).c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            std::stringstream errMessage;
            errMessage << "CreateProcess failed for child1 (" << GetLastError() << ")" << std::endl;
            log_message(logFile, errMessage.str());
            return false;
        }
        std::stringstream logMessage;
        log_message(logFile, logMessage.str());

        hProcess = pi.hProcess;
        CloseHandle(pi.hThread);
        return true;
    }

    bool spawn_process2(std::ofstream& logFile, const std::string& appPath, bool& child_running, HANDLE& hProcess) {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        std::string childPath = "child2.exe";
        if (!CreateProcessW(to_wstring(childPath).c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            std::stringstream errMessage;
            errMessage << "CreateProcess failed for child2 (" << GetLastError() << ")" << std::endl;
            log_message(logFile, errMessage.str());
            return false;
        }
        std::stringstream logMessage;
        log_message(logFile, logMessage.str());
        hProcess = pi.hProcess;
        CloseHandle(pi.hThread);
        return true;
    }
#else
    void signal_handler(int signum) {
        // Release leader mutex
        if (leaderMutex_global != nullptr) {
            release_leader_mutex(leaderMutex_global);
            leaderMutex_global = nullptr;
        }

        // Release regular mutex
        if (mutex_global != nullptr) {
            close_mutex(mutex_global);
            mutex_global = nullptr;
        }

        // Unmap and close shared memory
        if (shm_global != nullptr && sharedData_global != nullptr) {
            close_shared_memory(shm_global, sharedData_global);
            shm_global = nullptr;
            sharedData_global = nullptr;
        }

        // Exit process
        std::cerr << "Signal " << signum << " received. Resources cleaned up, exiting." << std::endl;
        exit(signum);
    }

    double get_time_diff_seconds(struct timeval start, struct timeval end) {
        return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    }

    struct timeval get_current_time_timeval() {
        struct timeval time;
        gettimeofday(&time, NULL);
        return time;
    }

    void* timer_thread(void* arg) {
        ThreadData* data = (ThreadData*)arg;
        SharedData* sharedData = data->sharedData;
        void* mutex = data->mutex;
        while (true) {
            usleep(300000);
            lock_mutex(mutex);
            sharedData->counter++;
            unlock_mutex(mutex);
        }
        return nullptr;
    }

    void process_user_input(SharedData* sharedData, void* mutex) {
        termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        int retval = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);

        if (retval > 0) {
            std::string input;
            std::cin >> input;

            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            try {
                std::int64_t new_count = std::stoll(input);
                lock_mutex(mutex);
                sharedData->counter = new_count;
                unlock_mutex(mutex);
                std::cout << "Counter set to: " << new_count << std::endl;
            }
            catch (std::invalid_argument const& ex) {
                std::cerr << "Invalid input" << std::endl;
            }
            catch (std::out_of_range const& ex) {
                std::cerr << "Input out of range" << std::endl;
            }
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }

    void* create_shared_memory(size_t size) {
        int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            return nullptr;
        }
        if (ftruncate(shm_fd, size) == -1) {
            close(shm_fd);
            return nullptr;
        }
        return reinterpret_cast<void*>(shm_fd);
    }

    void* map_shared_memory(void* shm) {
        intptr_t shm_fd = reinterpret_cast<intptr_t>(shm);
        void* shmem_ptr = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shmem_ptr == MAP_FAILED) {
            return nullptr;
        }
        return shmem_ptr;
    }

    bool create_mutex(void** mutex) {
        sem_t* sem = sem_open(MUTEX_NAME, O_CREAT, 0666, 1);
        if (sem == SEM_FAILED) {
            return false;
        }
        *mutex = sem;
        return true;
    }

    bool lock_mutex(void* mutex) {
        sem_t* sem = static_cast<sem_t*>(mutex);
        if (sem_wait(sem) == -1) {
            return false;
        }
        return true;
    }

    void unlock_mutex(void* mutex) {
        sem_t* sem = static_cast<sem_t*>(mutex);
        sem_post(sem);
    }

    void close_shared_memory(void* shm, void* sharedData) {
        intptr_t shm_fd = reinterpret_cast<intptr_t>(shm);
        if (sharedData != nullptr) munmap(sharedData, sizeof(SharedData));
        close(shm_fd);
        shm_unlink(SHM_NAME);
    }

    void close_mutex(void* mutex) {
        sem_t* sem = static_cast<sem_t*>(mutex);
        sem_close(sem);
        sem_unlink(MUTEX_NAME);
    }

    bool acquire_leader_mutex(void** mutex) {
        sem_t* sem = sem_open(LEADER_MUTEX_NAME, O_CREAT | O_EXCL, 0666, 1);
        if (sem == SEM_FAILED) {
            if (errno == EEXIST) {
                *mutex = nullptr;
                return true;
            }
            return false;
        }
        *mutex = sem;
        return true;
    }

    void release_leader_mutex(void* mutex) {
        if (mutex != nullptr) {
            sem_t* sem = static_cast<sem_t*>(mutex);
            sem_post(sem);
            sem_close(sem);
            sem_unlink(LEADER_MUTEX_NAME);
        }
    }

    bool is_leader(void* mutex) {
        return mutex != nullptr;
    }
    bool spawn_process1(std::ofstream& logFile, const std::string& appPath, bool& child_running) {
        pid_t pid = fork();
        if (pid == 0) { // Child process
            std::string childPath = appPath.substr(0, appPath.find_last_of("/\\")) + "/child1";
            const char* argv[] = { childPath.c_str(), NULL };
            execv(argv[0], (char**)argv);
            std::stringstream errMessage;
            errMessage << "execv failed for child1 (" << errno << ")" << std::endl;
            log_message(logFile, errMessage.str());
            exit(1);
        }
        else if (pid > 0) { // Parent process
            // int status;
            // waitpid(pid, &status, 0);
            // child_running=false;
            return true;
        }
        else {
            std::stringstream errMessage;
            errMessage << "fork failed for child1 (" << errno << ")" << std::endl;
            log_message(logFile, errMessage.str());
            return false;
        }
    }

    bool spawn_process2(std::ofstream& logFile, const std::string& appPath, bool& child_running) {
        pid_t pid = fork();
        if (pid == 0) { // Child process
            std::string childPath = appPath.substr(0, appPath.find_last_of("/\\")) + "/child2";
            const char* argv[] = { childPath.c_str(), NULL };
            execv(argv[0], (char**)argv);
            std::stringstream errMessage;
            errMessage << "execv failed for child2 (" << errno << ")" << std::endl;
            log_message(logFile, errMessage.str());
            exit(1);
        }
        else if (pid > 0) { // Parent process
            // int status;
            // waitpid(pid, &status, 0);
            // child_running=false;
            return true;
        }
        else {
            std::stringstream errMessage;
            errMessage << "fork failed for child2 (" << errno << ")" << std::endl;
            log_message(logFile, errMessage.str());
            return false;
        }
    }
#endif


void log_message(std::ofstream& logFile, const std::string& message) {
    logFile << message;
    logFile.flush();
}

std::string get_current_time_ms() {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto value = now_ms.time_since_epoch().count();
    std::time_t time_sec = value / 1000;
    std::tm time_info;
#ifdef _WIN32
    std::tm* time_ptr = localtime(&time_sec);
    if (time_ptr) {
        time_info = *time_ptr;
    }
    else {
        throw std::runtime_error("Failed to get local time.");
    }
#else
    localtime_r(&time_sec, &time_info);
#endif
    std::stringstream ss;
    ss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << (value % 1000);
    return ss.str();
}