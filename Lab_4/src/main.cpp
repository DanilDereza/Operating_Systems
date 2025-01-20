#include <iostream>
#include <sstream>
#include <vector>
#include <ctime>
#include <iomanip>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <fstream>
    #include <termios.h>
    #include <pthread.h>
    #include <semaphore.h>
    #include <sys/time.h>
    #include <cstring>
    #include <csignal>
    #include <fcntl.h>
    #include <unistd.h>
#endif

#include "serial_port.h"

#ifdef _WIN32
    #define PORT_RD "COM12"
    #define SemaphoreWait(sem) WaitForSingleObject(sem, INFINITE);
    #define SemaphorePost(sem) ReleaseSemaphore(sem, 1, NULL)
#else
    #define PORT_RD "/dev/pts/6"
    #define SemaphoreWait(sem) sem_wait(sem)
    #define SemaphorePost(sem) sem_post(sem)
#endif

#define LOG_FILE_NAME "log.txt"
#define LOG_FILE_NAME_HOUR "log_hour.txt"
#define LOG_FILE_NAME_DAY "log_day.txt"
#define FILE_LAST_RECORD "last_record.txt"
#define SEMAPHORE_OBJECT_NAME "my_semaphore"
#define RECORD_LENGTH 30
#define SEC_IN_HOUR 3600
#define SEC_IN_DAY 24 * SEC_IN_HOUR
#define SEC_IN_MONTH SEC_IN_DAY * 30

struct ThreadData { // Structure for transferring data to threads
#ifdef _WIN32
    HANDLE logFile;
#else
    std::ofstream *logFile;
#endif
    time_t *nextLogTime;
    double *averageValue;
    int *recordCounter;

#ifdef _WIN32
    HANDLE threadSemaphore;
#else
    sem_t *threadSemaphore;
#endif
};

volatile unsigned char need_exit = 0; // Flag for program termination

void make_fixed_length_record(std::string& fixed_record, const std::string& record); // Function for creating a fixed length record (Windows, Linux)

#ifdef _WIN32
    BOOL WINAPI sig_handler(DWORD signal); // Ctrl+C signal handler (Windows)
    std::string get_current_time(); // Function for getting the current time in the format YYYY-MM-DD hh:mm:ss.sss (Windows)
    void write_log_to_file(HANDLE fileHandle, const char* record, int size, bool append); // Function for writing a log to a file (Windows)
    void free_resources(HANDLE* lastRecordFile, HANDLE* logFile, HANDLE* hourlyLogFile, HANDLE* dailyLogFile, HANDLE* sem, HANDLE* serialPort); // Resource release function (Windows)
    void read_last_records(HANDLE fileHandle, int *value1, int *value2); // Function for reading recent entries from a file (Windows)
    bool is_file_empty(HANDLE fileHandle); // Function to check if a file is empty (Windows)
    DWORD WINAPI hourly_log_thread(void *args); // Thread function for recording hourly logs (Windows)
    DWORD WINAPI daily_log_thread(void *args); // Thread function for recording daily logs (Linux)
    std::wstring to_wstring(const std::string& str); // Function to convert ANSI string to Unicode string
#else
    void sig_handler(int sig); // SIGINT signal handler (Linux)
    std::string get_current_time(); // Function for getting the current time in the format YYYY-MM-DD hh:mm:ss.sss (Linux)
    void write_log_to_file(std::ofstream& file, const std::string& record, bool append); // Function for writing a log to a file (Linux)
    void free_resources(std::fstream* lastRecordFile, std::ofstream* logFile, std::ofstream* hourlyLogFile, std::ofstream* dailyLogFile, int* fd); // Resource release function (Linux)
    bool is_file_empty(std::fstream& file); // Function to check if a file is empty (Linux)
    void* hourly_log_thread(void *args); // Thread function for recording hourly logs (Linux)
    void* daily_log_thread(void *args); // Thread function for recording daily logs (Linux)
#endif


int main(int argc, char *argv[]) {
    // Signal SIGINT
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(sig_handler, TRUE)) {
        perror("Error setting handler");
        exit(EXIT_FAILURE);
    }
#else
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = sig_handler;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    act.sa_mask = set;
    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
#endif

    // Stores last record position in log_file and log_file_hour
#ifdef _WIN32
    std::wstring lastRecordFile_path = to_wstring(FILE_LAST_RECORD);
    HANDLE lastRecordFile = CreateFileW(lastRecordFile_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (lastRecordFile == INVALID_HANDLE_VALUE) {
        perror("CreateFile (tmp_file)");
        exit(EXIT_FAILURE);
    }
#else
    std::fstream* lastRecordFile = new std::fstream(FILE_LAST_RECORD, std::ios::in | std::ios::out);
    if (!lastRecordFile->is_open()) {
        perror("fopen (tmp_file)");
        exit(EXIT_FAILURE);
    }
#endif

    // Get last record position
    int lastRecordPosition[2] = {0, 0};
#ifdef _WIN32
    if (!is_file_empty(lastRecordFile)) {
        read_last_records(lastRecordFile, &lastRecordPosition[0], &lastRecordPosition[1]);
#else
    if (!is_file_empty(*lastRecordFile)) {
        *lastRecordFile >> lastRecordPosition[0];
        *lastRecordFile >> lastRecordPosition[1];
#endif
    }
    // Open log files
#ifdef _WIN32
    std::wstring logFile_path = to_wstring(LOG_FILE_NAME);
    HANDLE logFile = CreateFileW(logFile_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (logFile == INVALID_HANDLE_VALUE) {
        perror("CreateFile (log_file)");
        exit(EXIT_FAILURE);
    }

    std::wstring logFileHour_path = to_wstring(LOG_FILE_NAME_HOUR);
    HANDLE logFileHour = CreateFileW(logFileHour_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (logFileHour == INVALID_HANDLE_VALUE) {
        perror("CreateFile (log_file_hour)");
        exit(EXIT_FAILURE);
    }

    std::wstring logFileDay_path = to_wstring(LOG_FILE_NAME_DAY);
    HANDLE logFileDay = CreateFileW(logFileDay_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (logFileDay == INVALID_HANDLE_VALUE) {
        perror("CreateFile (log_file_day)");
        exit(EXIT_FAILURE);
    }
#else
    std::ofstream* logFile = new std::ofstream(LOG_FILE_NAME, std::ios::trunc | std::ios::out);
    if (!logFile->is_open()) {
        perror("fopen (log_file)");
        exit(EXIT_FAILURE);
    }

    std::ofstream* logFileHour = new std::ofstream(LOG_FILE_NAME_HOUR, std::ios::trunc | std::ios::out);
    if (!logFileHour->is_open()) {
        perror("fopen (log_file_hour)");
        exit(EXIT_FAILURE);
    }

    std::ofstream* logFileDay = new std::ofstream(LOG_FILE_NAME_DAY, std::ios::trunc | std::ios::out);
    if (!logFileDay->is_open()) {
        perror("fopen (log_file_day)");
        exit(EXIT_FAILURE);
    }
#endif

    // Open or create new semaphore
#ifdef _WIN32
    HANDLE semaphore = CreateSemaphoreW(NULL, 1, 1, to_wstring(SEMAPHORE_OBJECT_NAME).c_str());
#else
    sem_t *semaphore = sem_open(SEMAPHORE_OBJECT_NAME, O_CREAT, 0777, 1);
#endif
    // Configure port
#ifdef _WIN32
    HANDLE serialPort = open_and_configure_port(PORT_RD, BaudRate::BAUDRATE_115200);
    if (serialPort == INVALID_HANDLE_VALUE) {
        perror("CreateFile (pd)");
        exit(EXIT_FAILURE);
    }
#else
    int serialPort = open_and_configure_port(PORT_RD, BaudRate::BAUDRATE_115200);
    if (serialPort == -1) {
        perror("open port");
        free_resources(lastRecordFile, logFile, logFileHour, logFileDay, &serialPort);
        exit(EXIT_FAILURE);
    }

    // flush port
    if (tcflush(serialPort, TCIFLUSH) == -1) {
      perror("tcflush");
      free_resources(lastRecordFile, logFile, logFileHour, logFileDay, &serialPort);
        exit(EXIT_FAILURE);
    }

#endif
    int hourlyRecordCounter = 0, dailyRecordCounter = 0;
    double hourlyAverageValue = 0.0, dailyAverageValue = 0.0;

    time_t startTime = time(NULL);
    time_t nextHourLogTime = startTime + SEC_IN_HOUR;
    time_t nextDayLogTime = startTime + SEC_IN_DAY;

    // Create new thread (hour logger)
#ifdef _WIN32
    ThreadData params_hour = {logFileHour, &nextHourLogTime, &hourlyAverageValue, &hourlyRecordCounter, semaphore};
    HANDLE threadHour = CreateThread(NULL, 0, hourly_log_thread, &params_hour, 0, NULL);
    if (threadHour == NULL) {
        perror("CreateThread (thr_hour)");
        exit(EXIT_FAILURE);
    }
#else
    ThreadData params_hour = {logFileHour, &nextHourLogTime, &hourlyAverageValue, &hourlyRecordCounter, semaphore};
    pthread_t threadHour;
    int status = pthread_create(&threadHour, NULL, hourly_log_thread, &params_hour);
    if (status != 0) {
        perror("pthread_create (thr_hour)");
        free_resources(lastRecordFile, logFile, logFileHour, logFileDay, &serialPort);
        exit(EXIT_FAILURE);
    }
#endif
    // Create new thread (day logger)
#ifdef _WIN32
    ThreadData params_day = {logFileDay, &nextDayLogTime, &dailyAverageValue, &dailyRecordCounter, semaphore};
    HANDLE threadDay = CreateThread(NULL, 0, daily_log_thread, &params_day, 0, NULL);
    if (threadDay == NULL) {
        perror("CreateThread (thr_day)");
        exit(EXIT_FAILURE);
    }
#else
    ThreadData params_day = {logFileDay, &nextDayLogTime, &dailyAverageValue, &dailyRecordCounter, semaphore};
    pthread_t threadDay;
    status = pthread_create(&threadDay, NULL, daily_log_thread, &params_day);
    if (status != 0) {
        perror("pthread_create (thr_day)");
        free_resources(lastRecordFile, logFile, logFileHour, logFileDay, &serialPort);
        exit(EXIT_FAILURE);
    }
#endif
    std::string currentTime;
    std::vector<char> buffer(255);
    std::string logRecord;
    double currentTemperature;
    time_t logStartTime = startTime;
    bool append_mode = false;
#ifdef _WIN32
    while (!need_exit) {
        DWORD bytesRead;
        if (ReadFile(serialPort, buffer.data(), buffer.size(), &bytesRead, NULL)) {
            if (bytesRead > 0) {

                currentTime = get_current_time();
                logRecord = currentTime + " " + std::string(buffer.data(), bytesRead);

                std::string fixed_record;
                make_fixed_length_record(fixed_record, logRecord);
                
                time_t currentTimeSec = time(NULL);
                if (currentTimeSec - logStartTime >= SEC_IN_DAY) {
                   logStartTime = currentTimeSec;
                   append_mode = false;
                }
                else append_mode = true;

                write_log_to_file(logFile, fixed_record.c_str(), fixed_record.size(), append_mode);

                currentTemperature = atof(buffer.data());
                hourlyRecordCounter++;
                hourlyAverageValue += (currentTemperature - hourlyAverageValue) / hourlyRecordCounter;

                currentTemperature = atof(buffer.data());
                dailyRecordCounter++;
                dailyAverageValue += (currentTemperature - dailyAverageValue) / dailyRecordCounter;
            }
        }
        else {
            perror("ReadFile (pd)");
            break;
        }
        Sleep(PORT_SPEED_MS);
    }
#else
    while (!need_exit) {
        ssize_t bytesRead = read(serialPort, buffer.data(), buffer.size());
        if (bytesRead > 0) {

            currentTime = get_current_time();
            logRecord = currentTime + " " + std::string(buffer.data(), bytesRead);

            std::string fixed_record;
            make_fixed_length_record(fixed_record, logRecord);
             
            time_t currentTimeSec = time(NULL);
            if (currentTimeSec - logStartTime >= SEC_IN_DAY) {
               logStartTime = currentTimeSec;
               append_mode = false;
            }
            else append_mode = true;
            
            write_log_to_file(*logFile, fixed_record, append_mode);

            currentTemperature = atof(buffer.data());
            hourlyRecordCounter++;
            hourlyAverageValue += (currentTemperature - hourlyAverageValue) / hourlyRecordCounter;

            currentTemperature = atof(buffer.data());
            dailyRecordCounter++;
            dailyAverageValue += (currentTemperature - dailyAverageValue) / dailyRecordCounter;
        }
        usleep(1000 * PORT_SPEED_MS);
    }
#endif
#ifdef _WIN32
    WaitForSingleObject(threadHour, INFINITE);
    CloseHandle(threadHour);
    WaitForSingleObject(threadDay, INFINITE);
    CloseHandle(threadDay);
#else
    pthread_join(threadHour, NULL);
    pthread_join(threadDay, NULL);
#endif

#ifdef _WIN32
    SetFilePointer(lastRecordFile, 0, NULL, FILE_BEGIN);
    std::string tmp_buffer = std::to_string(lastRecordPosition[0]) + "\n" + std::to_string(lastRecordPosition[1]) + "\n";
    WriteFile(lastRecordFile, tmp_buffer.c_str(), tmp_buffer.size(), NULL, NULL);
#else
    lastRecordFile->seekg(0, std::ios::beg);
    *lastRecordFile << lastRecordPosition[0] << "\n";
    *lastRecordFile << lastRecordPosition[1] << "\n";
#endif
#ifdef _WIN32
    close_port(serialPort);
    free_resources(&lastRecordFile, &logFile, &logFileHour, &logFileDay, &semaphore, &serialPort);
#else
    close_port(serialPort);
    free_resources(lastRecordFile, logFile, logFileHour, logFileDay, &serialPort);
#endif
    return 0;
}


void make_fixed_length_record(std::string& fixed_record, const std::string& record) {
    fixed_record.assign(RECORD_LENGTH - 1, ' ');
    fixed_record.append("\n");
    size_t copy_len = std::min(record.size(), static_cast<size_t>(RECORD_LENGTH - 1));
    fixed_record.replace(0, copy_len, record.substr(0, copy_len));
}

#ifdef _WIN32
    BOOL WINAPI sig_handler(DWORD signal) {
        if (signal == CTRL_C_EVENT) {
            need_exit = 1;
            return TRUE;
        }
        return FALSE;
    }

    std::string get_current_time() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << st.wYear << "-"
            << std::setw(2) << st.wMonth << "-"
            << std::setw(2) << st.wDay << " "
            << std::setw(2) << st.wHour << ":"
            << std::setw(2) << st.wMinute << ":"
            << std::setw(2) << st.wSecond << "."
            << std::setw(3) << st.wMilliseconds;
        return oss.str();
    }

    void write_log_to_file(HANDLE fileHandle, const char* record, int size, bool append) {
        DWORD bytesWritten;
        if(!append)
          SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN);
        WriteFile(fileHandle, record, size, &bytesWritten, NULL);
    }


    void free_resources(HANDLE* lastRecordFile, HANDLE* logFile, HANDLE* hourlyLogFile, HANDLE* dailyLogFile, HANDLE* sem, HANDLE* serialPort) {
        if (lastRecordFile != nullptr) { CloseHandle(*lastRecordFile); *lastRecordFile = nullptr; }
        if (logFile != nullptr) { CloseHandle(*logFile); *logFile = nullptr; }
        if (hourlyLogFile != nullptr) { CloseHandle(*hourlyLogFile); *hourlyLogFile = nullptr; }
        if (dailyLogFile != nullptr) { CloseHandle(*dailyLogFile); *dailyLogFile = nullptr; }
        if (sem != nullptr) { CloseHandle(*sem); *sem = nullptr; }
            if (serialPort != nullptr) {  *serialPort = nullptr; }
    }

    void read_last_records(HANDLE fileHandle, int *value1, int *value2) {
        char buffer[256] = {0};
        DWORD bytesRead;
        if (!ReadFile(fileHandle, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
            perror("ReadFile (tmp_file)");
            exit(EXIT_FAILURE);
        }
        buffer[bytesRead] = '\0';

        if (sscanf(buffer, "%d %d", value1, value2) != 2) {
            perror("sscanf (tmp_file)");
            exit(EXIT_FAILURE);
        }
    }

    bool is_file_empty(HANDLE fileHandle) {
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(fileHandle, &fileSize)) {
            perror("GetFileSizeEx (tmp_file)");
            exit(EXIT_FAILURE);
        }
        return fileSize.QuadPart == 0;
    }

    DWORD WINAPI hourly_log_thread(void *args) {
        ThreadData *params = (ThreadData*)args;
        std::string currentTime;
        std::string logRecord;
        time_t logStartTime = time(NULL);
        bool append_mode = false;
        while (!need_exit) {
            time_t current_time = time(NULL);
            if (current_time >= *params->nextLogTime) {

                SemaphoreWait(params->threadSemaphore);
                *params->nextLogTime += SEC_IN_HOUR;
                SemaphorePost(params->threadSemaphore);

                SemaphoreWait(params->threadSemaphore);
                double avg = *params->averageValue;
                SemaphorePost(params->threadSemaphore);

                currentTime = get_current_time();
                logRecord = currentTime + " " + std::to_string(avg);

                std::string fixed_record;
                make_fixed_length_record(fixed_record, logRecord);

                if (current_time - logStartTime >= SEC_IN_MONTH) {
                    append_mode = false;
                    logStartTime = current_time;
                } 
                else append_mode = true;

                write_log_to_file(params->logFile, fixed_record.c_str(), fixed_record.size(), append_mode);

                SemaphoreWait(params->threadSemaphore);
                *params->averageValue = 0.0;
                SemaphorePost(params->threadSemaphore);

                SemaphoreWait(params->threadSemaphore);
                *params->recordCounter = 0;
                SemaphorePost(params->threadSemaphore);
            }
            Sleep(PORT_SPEED_MS);
        }
        return 0;
    }

    DWORD WINAPI daily_log_thread(void *args) {
        ThreadData *params = (ThreadData*)args;
        std::string currentTime;
        std::string logRecord;
        int logYear;
        bool firstOpened = true;
        while (!need_exit) {
            time_t current_time = time(NULL);
            if (current_time >= *params->nextLogTime) {

                struct tm local_time;
                std::tm* time_ptr = localtime(&current_time);
                if (time_ptr) {
                    local_time = *time_ptr;
                }
                else throw std::runtime_error("Failed to get local time.");
                int currentYear = local_time.tm_year + 1900;

                if (firstOpened) {
                    logYear = currentYear;
                }
                else if (currentYear != logYear) {
                    CloseHandle(params->logFile);
                    std::wstring logFileDay_path = to_wstring(LOG_FILE_NAME_DAY);
                    params->logFile = CreateFileW(logFileDay_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

                    if (params->logFile == INVALID_HANDLE_VALUE) {
                        perror("Error reopening log_file_day");
                        exit(EXIT_FAILURE);
                    }

                    CloseHandle(params->logFile);
                    params->logFile = CreateFileW(logFileDay_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (params->logFile == INVALID_HANDLE_VALUE) {
                        perror("Error reopening log_file_day");
                        exit(EXIT_FAILURE);
                    }
                    logYear = currentYear;
                }
                firstOpened = false;

                SemaphoreWait(params->threadSemaphore);
                *params->nextLogTime += SEC_IN_DAY;
                SemaphorePost(params->threadSemaphore);

                SemaphoreWait(params->threadSemaphore);
                double avg = *params->averageValue;
                SemaphorePost(params->threadSemaphore);

                currentTime = get_current_time();
                logRecord = currentTime + " " + std::to_string(avg);

                std::string fixed_record;
                make_fixed_length_record(fixed_record, logRecord);

                write_log_to_file(params->logFile, fixed_record.c_str(), fixed_record.size(), true);


                SemaphoreWait(params->threadSemaphore);
                *params->averageValue = 0.0;
                SemaphorePost(params->threadSemaphore);

                SemaphoreWait(params->threadSemaphore);
                *params->recordCounter = 0;
                SemaphorePost(params->threadSemaphore);
            }
            Sleep(PORT_SPEED_MS);
        }
        return 0;
    }

    std::wstring to_wstring(const std::string& str)
    {
        int len;
        int slength = (int)str.length() + 1;
        len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), slength, 0, 0);
        std::wstring r(len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), slength, &r[0], len);
        return r;
    }
#else
    void sig_handler(int sig) {
        if (sig == SIGINT)
            need_exit = 1;
    }

    std::string get_current_time() {
        std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* tmp = std::localtime(&now_c);

        if (tmp == nullptr) {
            perror("localtime");
            exit(EXIT_FAILURE);
        }

        std::ostringstream oss;
        oss << std::put_time(tmp, "%Y-%m-%d %H:%M:%S");
        long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        oss << "." << std::setfill('0') << std::setw(3) << ms;
        return oss.str();
    }

    void write_log_to_file(std::ofstream& file, const std::string& record, bool append) {
        if (!append) // < 24
            file.seekp(0, std::ios::beg);
        file.write(record.c_str(), record.size());
        file.flush();
    }


    void free_resources(std::fstream* lastRecordFile, std::ofstream* logFile, std::ofstream* hourlyLogFile, std::ofstream* dailyLogFile, int* fd) {
        if (lastRecordFile != nullptr) { lastRecordFile->close(); delete lastRecordFile; }
        if (logFile != nullptr) { logFile->close(); delete logFile; }
        if (hourlyLogFile != nullptr) { hourlyLogFile->close(); delete hourlyLogFile; }
        if (dailyLogFile != nullptr) { dailyLogFile->close(); delete dailyLogFile; }
        if (fd != nullptr) { close_port(*fd); *fd = -1; }
        sem_unlink(SEMAPHORE_OBJECT_NAME);
    }

    bool is_file_empty(std::fstream& file) {
        file.seekg(0, std::ios::end);
        long size = file.tellg();
        file.seekg(0, std::ios::beg);
        return size == 0;
    }

    void* hourly_log_thread(void *args) {
        ThreadData *params = (ThreadData*)args;
        std::string currentTime;
        std::string logRecord;
        time_t logStartTime = time(NULL);
        bool append_mode = false;
        while (!need_exit) {
            time_t current_time = time(NULL);
            if (current_time >= *params->nextLogTime) {

                SemaphoreWait(params->threadSemaphore);
                *params->nextLogTime += SEC_IN_HOUR;
                SemaphorePost(params->threadSemaphore);

                SemaphoreWait(params->threadSemaphore);
                double avg = *params->averageValue;
                SemaphorePost(params->threadSemaphore);

                currentTime = get_current_time();
                logRecord = currentTime + " " + std::to_string(avg);

                std::string fixed_record;
                make_fixed_length_record(fixed_record, logRecord);

                if (current_time - logStartTime >= SEC_IN_MONTH) {
                    append_mode = false;
                    logStartTime = current_time;
                } 
                else append_mode = true;

                write_log_to_file(*params->logFile, fixed_record, append_mode);

                SemaphoreWait(params->threadSemaphore);
                *params->averageValue = 0.0;
                SemaphorePost(params->threadSemaphore);

                SemaphoreWait(params->threadSemaphore);
                *params->recordCounter = 0;
                SemaphorePost(params->threadSemaphore);
            }
            usleep(1000 * PORT_SPEED_MS);
        }
        return 0;
    }

    void* daily_log_thread(void *args) {
        ThreadData *params = (ThreadData*)args;
        std::string currentTime;
        std::string logRecord;
        int logYear;
        bool firstOpened = true;
        while (!need_exit) {
            time_t current_time = time(NULL);
            if (current_time >= *params->nextLogTime) {

                std::tm* local_time = std::localtime(&current_time);
                int currentYear = local_time->tm_year + 1900;

                if (firstOpened) {
                    logYear = currentYear;
                }
                else if (currentYear != logYear) {
                    params->logFile->close();
                    params->logFile->open(LOG_FILE_NAME_DAY, std::ios::trunc | std::ios::out);
                    if (!params->logFile->is_open()) {
                        perror("Error reopening log_file_day (w)");
                        exit(EXIT_FAILURE);
                    }
                     params->logFile->close();
                    params->logFile->open(LOG_FILE_NAME_DAY, std::ios::out);
                    if (!params->logFile->is_open()) {
                        perror("Error reopening log_file_day (a+)");
                        exit(EXIT_FAILURE);
                    }
                    logYear = currentYear;
                }
                firstOpened = false;

                SemaphoreWait(params->threadSemaphore);
                *params->nextLogTime += SEC_IN_DAY;
                SemaphorePost(params->threadSemaphore);

                SemaphoreWait(params->threadSemaphore);
                double avg = *params->averageValue;
                SemaphorePost(params->threadSemaphore);

                currentTime = get_current_time();
                logRecord = currentTime + " " + std::to_string(avg);

                std::string fixed_record;
                make_fixed_length_record(fixed_record, logRecord);

                write_log_to_file(*params->logFile, fixed_record, true);

                SemaphoreWait(params->threadSemaphore);
                *params->averageValue = 0.0;
                SemaphorePost(params->threadSemaphore);

                SemaphoreWait(params->threadSemaphore);
                *params->recordCounter = 0;
                SemaphorePost(params->threadSemaphore);
            }
            usleep(1000 * PORT_SPEED_MS);

        }
        return 0;
    }
#endif