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
    #include <codecvt>
#else
    #include <unistd.h>
    #include <sys/time.h>
    #include <sys/mman.h>
    #include <fcntl.h>
    #include <semaphore.h>
#endif

// IPC Constants
#define SHM_NAME "my_shared_memory"
#define MUTEX_NAME "my_mutex"

//Shared data
struct SharedData {
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

//Logging func
void log_message(std::ofstream& logFile, const std::string& message); // Function to add message to log (Windows, Linux)
std::string get_current_time_ms(); // Get current time with milliseconds (Windows, Linux)

#ifdef _WIN32
    std::wstring to_wstring(const std::string &str); // Function to convert ANSI string to Unicode string (Windows)
    std::string wstring_to_string(const std::wstring& wstr); // Function to convert Unicode string to a UTF-8 encoded std::string (Windows)
#endif


int main() {
    //Shared memory
    void* shm = create_shared_memory(sizeof(SharedData));
    if(shm == nullptr){
        std::cerr << "Failed to create shared memory" << std::endl;
        return 1;
    }
    SharedData* sharedData = static_cast<SharedData*>(map_shared_memory(shm));
     if(sharedData == nullptr){
        std::cerr << "Failed to map shared memory" << std::endl;
        close_shared_memory(shm, nullptr);
        return 1;
    }

    //Mutex
    void* mutex = nullptr;
    if (!create_mutex(&mutex)) {
        std::cerr << "Failed to create mutex" << std::endl;
        close_shared_memory(shm, sharedData);
        return 1;
    }
    
    // Create log file
    std::ofstream logFile;
    std::string logFileName = "my_log.log";
    logFile.open(logFileName, std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Error opening log file!" << std::endl;
        close_mutex(mutex);
        close_shared_memory(shm, sharedData);
        return 1;
    }
    
    // Write start info to log
    std::stringstream startMessage;
    startMessage << "Child 1 started. PID: " << getpid() << ", Time: " << get_current_time_ms() << std::endl;
    log_message(logFile, startMessage.str());

    //Modify counter
    lock_mutex(mutex);
    sharedData->counter += 10;
    unlock_mutex(mutex);

    std::stringstream exitMessage;
    exitMessage << "Child 1 exiting. PID: " << getpid() << ", Time: " << get_current_time_ms() << std::endl;
    log_message(logFile,exitMessage.str());
    close_mutex(mutex);
    close_shared_memory(shm, sharedData);
    return 0;
}

//Function to add message to log
void log_message(std::ofstream& logFile, const std::string& message) {
    logFile << message;
    logFile.flush();
}

// Get current time with milliseconds
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
    } else {
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

#ifdef _WIN32
    std::string wstring_to_string(const std::wstring& wstr) {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(wstr);
    }

    std::wstring to_wstring(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
    return wstr;
    }

    void* create_shared_memory(size_t size) {
        HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, wstring_to_string(to_wstring(SHM_NAME)).c_str());
        if (hMapFile == NULL) {
            return nullptr;
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
    void close_shared_memory(void* shm, void* sharedData){
        HANDLE hMapFile = static_cast<HANDLE>(shm);
        if(sharedData != nullptr) UnmapViewOfFile(sharedData);
        CloseHandle(hMapFile);
    }

    void close_mutex(void* mutex){
        HANDLE hMutex = static_cast<HANDLE>(mutex);
        CloseHandle(hMutex);
    }
#else
    void* create_shared_memory(size_t size) {
        int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (shm_fd == -1) {
            return nullptr;
        }
        return reinterpret_cast<void*>(shm_fd);
    }
    void* map_shared_memory(void* shm) {
        int shm_fd = reinterpret_cast<intptr_t>(shm);
        void* shmem_ptr = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shmem_ptr == MAP_FAILED) {
        return nullptr;
        }
    return shmem_ptr;
    }
    bool create_mutex(void** mutex) {
        sem_t* sem = sem_open(MUTEX_NAME,  0);
        if (sem == SEM_FAILED) {
            return false;
        }
        *mutex = sem;
        return true;
    }
    bool lock_mutex(void* mutex) {
        sem_t* sem = static_cast<sem_t*>(mutex);
        if(sem_wait(sem) == -1){
        return false;
        }
        return true;
    }
    void unlock_mutex(void* mutex) {
        sem_t* sem = static_cast<sem_t*>(mutex);
        sem_post(sem);
    }
    void close_shared_memory(void* shm, void* sharedData){
        int shm_fd = reinterpret_cast<intptr_t>(shm);
        if (sharedData != nullptr) munmap(sharedData, sizeof(SharedData));
        close(shm_fd);

    }

    void close_mutex(void* mutex){
        sem_t* sem = static_cast<sem_t*>(mutex);
        sem_close(sem);
    }
#endif