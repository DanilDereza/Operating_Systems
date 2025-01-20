#include <iostream>
#include <sstream>
#include <vector>
#include <ctime>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <sqlite3.h>

#ifdef _WIN32
    #include <windows.h>
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <fstream>
    #include <termios.h>
    #include <pthread.h>
    #include <semaphore.h>
    #include <mutex>
    #include <sys/time.h>
    #include <cstring>
    #include <csignal>
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif

#include "serial_port.h"

#ifdef _WIN32
    #define PORT_RD "COM12"
    #define SemaphoreWait(sem) WaitForSingleObject(sem, INFINITE);
    #define SemaphorePost(sem) ReleaseSemaphore(sem, 1, NULL)
#else
    #define PORT_RD "/dev/pts/3"
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
#define DB_NAME "temperature.db"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

struct ThreadData {
#ifdef _WIN32
    sqlite3 *db;
    HANDLE serialPort;
#else
    sqlite3 *db;
    int serialPort;
#endif
    time_t *nextLogTime;
    double *averageValue;
    int *recordCounter;
    std::string* portData;
    
#ifdef _WIN32
    HANDLE threadSemaphore;
    CRITICAL_SECTION dataMutex;
#else
    sem_t *threadSemaphore;
    std::mutex dataMutex;
#endif
};

volatile unsigned char need_exit = 0;

void make_fixed_length_record(std::string& fixed_record, const std::string& record); // Function for creating a fixed length record (Windows, Linux)
std::string http_response(const std::string& body, int status_code, const std::string& content_type); // Construct an HTTP response with a given body, status code, and content type (Windows, Linux)
std::string get_stats_from_db(sqlite3* db, const std::string& start_time, const std::string& end_time); // Fetch logs from the database within a specified time range and return them as a JSON-formatted string (Windows, Linux)
void handle_client(int client_socket, sqlite3* db, std::string& currentTemperatureStr); // Process HTTP requests from a client, serve files or data, and send appropriate responses (Windows, Linux)

#ifdef _WIN32
    BOOL WINAPI sig_handler(DWORD signal); // Ctrl+C signal handler (Windows)
    std::string get_current_time(); // Function for getting the current time in the format YYYY-MM-DD hh:mm:ss.sss (Windows)
    void write_log_to_db(sqlite3* db, const std::string& record); // Write a temperature record into the database (Windows)
    void free_resources(HANDLE* lastRecordFile, sqlite3** db, HANDLE* sem, HANDLE* serialPort); // Free allocated resources and close handles (Windows)
    void read_last_records(HANDLE fileHandle, int *value1, int *value2); // Read the last two integer records from a file (Windows)
    bool is_file_empty(HANDLE fileHandle); // Function to check if a file is empty (Windows)
    DWORD WINAPI hourly_log_thread(void *args); // Thread function for recording hourly logs (Windows)
    DWORD WINAPI daily_log_thread(void *args); // Thread function for recording daily logs (Linux)
    DWORD WINAPI data_processing_thread(void *args); // Thread function for processing serial port data (Windows)
    DWORD WINAPI web_server_thread(void* args); // Thread function for handling web server operations (Windows)
    std::wstring to_wstring(const std::string& str); // Function to convert ANSI string to Unicode string
#else
    void sig_handler(int sig); // SIGINT signal handler (Linux)
    std::string get_current_time(); // Function for getting the current time in the format YYYY-MM-DD hh:mm:ss.sss (Linux)
    void write_log_to_db(sqlite3* db, const std::string& record); // Write a temperature record into the database (Linux)
    void free_resources(std::fstream* lastRecordFile, sqlite3** db, int* fd); // Free allocated resources and close handles (Linux)
    bool is_file_empty(std::fstream& file); // Function to check if a file is empty (Linux)
    void* hourly_log_thread(void *args); // Thread function for recording hourly logs (Linux)
    void* daily_log_thread(void *args); // Thread function for recording daily logs (Linux)
    void* data_processing_thread(void *args); // Thread function for processing serial port data (Linux)
    void* web_server_thread(void* args); // Thread function for handling web server operations (Linux)
#endif

std::string program_start_time;


int main(int argc, char *argv[]) {
    program_start_time = get_current_time();
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
    // Open database
    sqlite3 *db;
    int rc = sqlite3_open(DB_NAME, &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        exit(EXIT_FAILURE);
    }

    // Create table if not exists
    const char* create_table_sql = "CREATE TABLE IF NOT EXISTS logs ("
                                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                   "timestamp TEXT NOT NULL,"
                                   "temperature TEXT NOT NULL"
                                   ");";
    char* error_message = nullptr;
    rc = sqlite3_exec(db, create_table_sql, nullptr, nullptr, &error_message);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << error_message << std::endl;
        sqlite3_free(error_message);
        sqlite3_close(db);
        exit(EXIT_FAILURE);
    }


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
        free_resources(lastRecordFile, &db, &serialPort);
        exit(EXIT_FAILURE);
    }

    // Flush port
    if (tcflush(serialPort, TCIFLUSH) == -1) {
        perror("tcflush");
        free_resources(lastRecordFile, &db, &serialPort);
        exit(EXIT_FAILURE);
    }

#endif
    int hourlyRecordCounter = 0, dailyRecordCounter = 0;
    double hourlyAverageValue = 0.0, dailyAverageValue = 0.0;
    std::string portData;


    time_t startTime = time(NULL);
    time_t nextHourLogTime = startTime + SEC_IN_HOUR;
    time_t nextDayLogTime = startTime + SEC_IN_DAY;

    // Create new thread (hour logger)
#ifdef _WIN32
    ThreadData params_hour = {db, serialPort, &nextHourLogTime, &hourlyAverageValue, &hourlyRecordCounter, &portData, semaphore};
    InitializeCriticalSection(&params_hour.dataMutex);
    HANDLE threadHour = CreateThread(NULL, 0, hourly_log_thread, &params_hour, 0, NULL);
    if (threadHour == NULL) {
        perror("CreateThread (thr_hour)");
        exit(EXIT_FAILURE);
    }
#else
    ThreadData params_hour = {db, serialPort, &nextHourLogTime, &hourlyAverageValue, &hourlyRecordCounter, &portData, semaphore};
    pthread_t threadHour;
    int status = pthread_create(&threadHour, NULL, hourly_log_thread, &params_hour);
    if (status != 0) {
        perror("pthread_create (thr_hour)");
        free_resources(lastRecordFile, &db, &serialPort);
        exit(EXIT_FAILURE);
    }
#endif
    // Create new thread (day logger)
#ifdef _WIN32
    ThreadData params_day = {db, serialPort, &nextDayLogTime, &dailyAverageValue, &dailyRecordCounter, &portData, semaphore};
    InitializeCriticalSection(&params_day.dataMutex);
    HANDLE threadDay = CreateThread(NULL, 0, daily_log_thread, &params_day, 0, NULL);
    if (threadDay == NULL) {
        perror("CreateThread (thr_day)");
        exit(EXIT_FAILURE);
    }
#else
    ThreadData params_day = {db, serialPort, &nextDayLogTime, &dailyAverageValue, &dailyRecordCounter, &portData, semaphore};
    pthread_t threadDay;
    status = pthread_create(&threadDay, NULL, daily_log_thread, &params_day);
    if (status != 0) {
        perror("pthread_create (thr_day)");
        free_resources(lastRecordFile, &db, &serialPort);
        exit(EXIT_FAILURE);
    }
#endif
    
    // Create new thread (data processing)
#ifdef _WIN32
    ThreadData params_data = {db, serialPort, &nextDayLogTime, &dailyAverageValue, &dailyRecordCounter, &portData, semaphore};
    InitializeCriticalSection(&params_data.dataMutex);
    HANDLE threadData = CreateThread(NULL, 0, data_processing_thread, &params_data, 0, NULL);
    if (threadData == NULL) {
        perror("CreateThread (thr_data)");
        exit(EXIT_FAILURE);
    }
#else
    ThreadData params_data = {db, serialPort, &nextDayLogTime, &dailyAverageValue, &dailyRecordCounter, &portData, semaphore};
    pthread_t threadData;
    status = pthread_create(&threadData, NULL, data_processing_thread, &params_data);
    if (status != 0) {
        perror("pthread_create (thr_data)");
        free_resources(lastRecordFile, &db, &serialPort);
        exit(EXIT_FAILURE);
    }
#endif

    std::string currentTemperatureStr; // Current temperature variable for web server
    
    // Web server setup
#ifdef _WIN32
    ThreadData params_web = {db, serialPort, &nextDayLogTime, &dailyAverageValue, &dailyRecordCounter, &portData, semaphore};
    InitializeCriticalSection(&params_web.dataMutex);
    HANDLE threadWeb = CreateThread(NULL, 0, web_server_thread, &params_web, 0, NULL);
    if (threadWeb == NULL) {
        perror("CreateThread (thr_web)");
        exit(EXIT_FAILURE);
    }
#else
    ThreadData params_web = {db, serialPort, &nextDayLogTime, &dailyAverageValue, &dailyRecordCounter, &portData, semaphore};
    pthread_t threadWeb;
    status = pthread_create(&threadWeb, NULL, web_server_thread, &params_web);
    if (status != 0) {
        perror("pthread_create (thr_web)");
        free_resources(lastRecordFile, &db, &serialPort);
        exit(EXIT_FAILURE);
    }
#endif

#ifdef _WIN32
    WaitForSingleObject(threadHour, INFINITE);
    CloseHandle(threadHour);
    WaitForSingleObject(threadDay, INFINITE);
    CloseHandle(threadDay);
    WaitForSingleObject(threadData, INFINITE);
    CloseHandle(threadData);
    WaitForSingleObject(threadWeb, INFINITE);
    CloseHandle(threadWeb);
#else
    pthread_join(threadHour, NULL);
    pthread_join(threadDay, NULL);
    pthread_join(threadData, NULL);
    pthread_join(threadWeb, NULL);
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
    free_resources(&lastRecordFile, &db, &semaphore, &serialPort);
#else
    close_port(serialPort);
    free_resources(lastRecordFile, &db, &serialPort);
#endif
    sqlite3_close(db);
    return 0;
}


void make_fixed_length_record(std::string& fixed_record, const std::string& record) {
    fixed_record.assign(RECORD_LENGTH - 1, ' ');
    size_t copy_len = std::min(record.size(), static_cast<size_t>(RECORD_LENGTH - 1));
    fixed_record.replace(0, copy_len, record.substr(0, copy_len));
}

std::string http_response(const std::string& body, int status_code, const std::string& content_type) {
    std::string status_line = "HTTP/1.1 " + std::to_string(status_code) + " ";
    if (status_code == 200) {
        status_line += "OK\r\n";
    } else if (status_code == 404) {
        status_line += "Not Found\r\n";
    }else {
        status_line += "Internal Server Error\r\n";
    }
    std::string headers = "Content-Type: " + content_type + "\r\n"
                            "Content-Length: " + std::to_string(body.size()) + "\r\n"
                            "Connection: close\r\n"
                            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                            "Pragma: no-cache\r\n"
                            "Expires: 0\r\n\r\n";
    return status_line + headers + body;
}

std::string get_stats_from_db(sqlite3* db, const std::string& start_time, const std::string& end_time) {
    std::string query = "SELECT timestamp, temperature FROM logs WHERE timestamp BETWEEN '" + start_time + "' AND '" + end_time + "';";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error: " << sqlite3_errmsg(db) << ", query: " << query << std::endl;
        return "{\"error\":\"Database error.\"}";
    }

    std::string response = "[";
    bool first = true;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (!first) response += ",";
        response += "{\"timestamp\":\"" + std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) + "\",";
        response += "\"temperature\":\"" + std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))) + "\"}";
        first = false;
    }

    if (rc != SQLITE_DONE && rc != SQLITE_OK) {
        std::cerr << "SQL step error: " << sqlite3_errmsg(db) << ", query: " << query << std::endl;
        sqlite3_finalize(stmt);
        return "{\"error\":\"Error fetching data from database.\"}";
    }
    sqlite3_finalize(stmt);
    return response + "]";
}

#ifdef _WIN32
    void handle_client(int client_socket, sqlite3* db, std::string& currentTemperatureStr) {
        char buffer[BUFFER_SIZE] = {0};
        
        int bytesRead = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesRead > 0) {
            buffer[bytesRead] = '0';
            std::string request(buffer);
            std::string response;
            
            std::cout << "Request: " << request << std::endl;
            
            if (request.find("GET /current") == 0) {
                std::cout << "1\n";
            response = http_response("{\"temperature\":\"" + currentTemperatureStr + "\"}", 200, "application/json");
            } else if (request.find("GET /stats") == 0) {
                std::cout << "1.2\n";
                std::size_t start_pos = request.find("start=");
                std::size_t end_pos = request.find("end=");
                std::string start_time, end_time;
                if (start_pos != std::string::npos && end_pos != std::string::npos) {
                    start_pos += 6;
                    end_pos += 4;
                    std::size_t next_amp = request.find("&", start_pos);
                    if (next_amp == std::string::npos){
                        start_time = request.substr(start_pos);
                    } else {
                        start_time = request.substr(start_pos, next_amp - start_pos);
                    }
                    next_amp = request.find("&", end_pos);
                    if (next_amp == std::string::npos){
                        end_time = request.substr(end_pos);
                    } else {
                        end_time = request.substr(end_pos, next_amp - end_pos);
                    }
                } else {
                    start_time = program_start_time;
                    end_time = get_current_time();
                }
                response = http_response(get_stats_from_db(db, start_time, end_time), 200, "application/json");
            } else if (request.find("GET /") == 0) {
                std::cout << "2\n";

                std::ifstream file("index.html");
                if (file.is_open()) {
                    std::cout << "2.1\n";
                    std::cout << "File index.html opened successfully" << std::endl;
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    response = http_response(buffer.str(), 200, "text/html");
                    file.close();
                } else {
                    std::cout << "Error opening index.html file" << std::endl;
                    response = http_response("<html><body><h1>Error: index.html not found</h1></body></html>", 404, "text/html");
                }

                if (request.find("GET /style.css") == 0) {
                    std::cout << "3\n";
                    std::ifstream file("style.css");
                    if (file.is_open()) {
                        std::cout << "File styles.css opened successfully" << std::endl;
                        std::stringstream buffer;
                        buffer << file.rdbuf();
                        response = http_response(buffer.str(), 200, "text/css");
                        file.close();
                    } else {
                        std::cout << "Error opening style.css file" << std::endl;
                        response = http_response("Error 404: style.css not found", 404, "text/plain");
                    }
                }

                if (request.find("GET /script.js") == 0) {
                    std::cout << "4\n";
                    std::ifstream file("script.js");
                    if (file.is_open()) {
                        std::cout << "File script.js opened successfully" << std::endl;
                        std::stringstream buffer;
                        buffer << file.rdbuf();
                        response = http_response(buffer.str(), 200, "application/javascript");
                        file.close();
                    } else {
                        std::cout << "Error opening script.js file" << std::endl;
                        response = http_response("Error 404: script.js not found", 404, "text/plain");
                    }
                }


            }
            else {
                response = http_response("<html><body><h1>Hello, World!</h1></body></html>", 200, "text/html");
            }
            send(client_socket, response.c_str(), response.size(), 0);
        }
    }

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

    void write_log_to_db(sqlite3* db, const std::string& record) {
        std::string query = "INSERT INTO logs (timestamp, temperature) VALUES ('" + get_current_time() + "', '" + record + "');";
        char* error_message = nullptr;
        int rc = sqlite3_exec(db, query.c_str(), nullptr, nullptr, &error_message);
        if (rc != SQLITE_OK) {
            std::cerr << "SQL error: " << error_message << std::endl;
            sqlite3_free(error_message);
            
        }
    }

    void free_resources(HANDLE* lastRecordFile, sqlite3** db, HANDLE* sem, HANDLE* serialPort) {
        if (lastRecordFile != nullptr) { CloseHandle(*lastRecordFile); *lastRecordFile = nullptr; }
        if (db != nullptr) {sqlite3_close(*db); *db = nullptr; }
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


    DWORD WINAPI data_processing_thread(void *args) {
        ThreadData *params = (ThreadData*)args;
        std::vector<char> buffer(255);
        std::string logRecord;
        std::string currentTime;
        double currentTemperature;
        while (!need_exit) {
            DWORD bytesRead;
            if (ReadFile(params->serialPort, buffer.data(), buffer.size(), &bytesRead, NULL)) {
                if (bytesRead > 0) {
                    currentTime = get_current_time();
                    std::string tempStr(buffer.data(), bytesRead);
                    size_t first = tempStr.find_first_not_of(" \t\n\r");
                    size_t last = tempStr.find_last_not_of(" \t\n\r");
                        if (first != std::string::npos && last != std::string::npos)
                        tempStr = tempStr.substr(first, (last - first + 1));
                    else
                        tempStr = "";
                    
                    try {
                            currentTemperature = std::stod(tempStr);
                        }
                        catch (const std::invalid_argument& e) {
                            std::cerr << "Invalid argument in temperature string: " << tempStr << std::endl;
                            currentTemperature = 0.0;
                        }

                    logRecord = std::to_string(currentTemperature);
                    std::string fixed_record;
                    make_fixed_length_record(fixed_record, logRecord);
                    write_log_to_db(params->db, fixed_record);
                    
                    EnterCriticalSection(&params->dataMutex);
                    *params->portData = std::to_string(currentTemperature);
                    *params->recordCounter = *params->recordCounter + 1;
                    *params->averageValue += (currentTemperature - *params->averageValue) / *params->recordCounter;
                    LeaveCriticalSection(&params->dataMutex);
                }
            }
            else {
                perror("ReadFile (pd)");
                break;
            }
            
            Sleep(PORT_SPEED_MS);
        }
        return 0;
    }

    DWORD WINAPI hourly_log_thread(void *args) {
        ThreadData *params = (ThreadData*)args;
        std::string currentTime;
        std::string logRecord;
        while (!need_exit) {
            time_t current_time = time(NULL);
            if (current_time >= *params->nextLogTime) {

                SemaphoreWait(params->threadSemaphore);
                *params->nextLogTime += SEC_IN_HOUR;
                SemaphorePost(params->threadSemaphore);

                double avg;
                EnterCriticalSection(&params->dataMutex);
                avg = *params->averageValue;
                LeaveCriticalSection(&params->dataMutex);

                currentTime = get_current_time();
                logRecord = currentTime + " " + std::to_string(avg);
                std::string fixed_record;
                make_fixed_length_record(fixed_record, logRecord);

                write_log_to_db(params->db, fixed_record);
                
                EnterCriticalSection(&params->dataMutex);
                *params->averageValue = 0.0;
                *params->recordCounter = 0;
                LeaveCriticalSection(&params->dataMutex);
            }
            Sleep(PORT_SPEED_MS);
        }
        return 0;
    }


    DWORD WINAPI daily_log_thread(void *args) {
        ThreadData *params = (ThreadData*)args;
        std::string currentTime;
        std::string logRecord;
        while (!need_exit) {
            time_t current_time = time(NULL);
            if (current_time >= *params->nextLogTime) {

                SemaphoreWait(params->threadSemaphore);
                *params->nextLogTime += SEC_IN_DAY;
                SemaphorePost(params->threadSemaphore);

                double avg;
                EnterCriticalSection(&params->dataMutex);
                avg = *params->averageValue;
                LeaveCriticalSection(&params->dataMutex);
                
                currentTime = get_current_time();
                logRecord = currentTime + " " + std::to_string(avg);

                std::string fixed_record;
                make_fixed_length_record(fixed_record, logRecord);
                write_log_to_db(params->db, fixed_record);
                
                EnterCriticalSection(&params->dataMutex);
                *params->averageValue = 0.0;
                *params->recordCounter = 0;
                LeaveCriticalSection(&params->dataMutex);
            }
            Sleep(PORT_SPEED_MS);
        }
        return 0;
    }

    DWORD WINAPI web_server_thread(void* args) {
        ThreadData *params = (ThreadData*)args;
        std::string currentTemperatureStr;
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return 1;
        }

        SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == INVALID_SOCKET) {
            std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
            WSACleanup();
            return 1;
        }

        sockaddr_in server_address;
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = INADDR_ANY;
        server_address.sin_port = htons(SERVER_PORT);

        if (bind(server_socket, (SOCKADDR*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
            std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
            closesocket(server_socket);
            WSACleanup();
            return 1;
        }

        if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
            closesocket(server_socket);
            WSACleanup();
            return 1;
        }

        std::cout << "Server listening on port " << SERVER_PORT << std::endl;
        while (!need_exit) {
            SOCKET client_socket = accept(server_socket, NULL, NULL);
            if (client_socket == INVALID_SOCKET) {
                std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
                continue;
            }
            
            EnterCriticalSection(&params->dataMutex);
            currentTemperatureStr = *params->portData;
            LeaveCriticalSection(&params->dataMutex);
            
            handle_client(client_socket, params->db, currentTemperatureStr);
            closesocket(client_socket);
        }
        closesocket(server_socket);
        WSACleanup();
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

    void handle_client(int client_socket, sqlite3* db, std::string& currentTemperatureStr) {
        char buffer[BUFFER_SIZE] = {0};
        
        ssize_t bytesRead = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesRead > 0) {
            buffer[bytesRead] = '0';
            std::string request(buffer);
            std::string response;
            
            std::cout << "Request BEFORE: \n" << request << std::endl;
            
            if (request.find("GET /current") == 0) {
            response = http_response("{\"temperature\":\"" + currentTemperatureStr + "\"}", 200, "application/json");
            } else if (request.find("GET /stats") == 0) {
                std::size_t start_pos = request.find("start=");
                std::size_t end_pos = request.find("end=");
                std::string start_time, end_time;
                if (start_pos != std::string::npos && end_pos != std::string::npos) {
                    start_pos += 6;
                    end_pos += 4;
                    std::size_t next_amp = request.find("&", start_pos);
                    if (next_amp == std::string::npos){
                        start_time = request.substr(start_pos);
                    } else {
                        start_time = request.substr(start_pos, next_amp - start_pos);
                    }
                    next_amp = request.find("&", end_pos);
                    if (next_amp == std::string::npos){
                        end_time = request.substr(end_pos);
                    } else {
                        end_time = request.substr(end_pos, next_amp - end_pos);
                    }
                } else {
                    start_time = program_start_time;
                    end_time = get_current_time();
                }
                response = http_response(get_stats_from_db(db, start_time, end_time), 200, "application/json");
            } else if (request.find("GET /") == 0) {
                std::cout << "2\n";

                std::ifstream file("index.html");
                if (file.is_open()) {
                    std::cout << "2.1\n";
                    std::cout << "File index.html opened successfully" << std::endl;
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    response = http_response(buffer.str(), 200, "text/html");
                    file.close();
                } else {
                    std::cout << "Error opening index.html file" << std::endl;
                    response = http_response("<html><body><h1>Error: index.html not found</h1></body></html>", 404, "text/html");
                }

                if (request.find("GET /style.css") == 0) {
                    std::cout << "3\n";
                    std::ifstream file("style.css");
                    if (file.is_open()) {
                        std::cout << "File styles.css opened successfully" << std::endl;
                        std::stringstream buffer;
                        buffer << file.rdbuf();
                        response = http_response(buffer.str(), 200, "text/css");
                        file.close();
                    } else {
                        std::cout << "Error opening style.css file" << std::endl;
                        response = http_response("Error 404: style.css not found", 404, "text/plain");
                    }
                }

                if (request.find("GET /script.js") == 0) {
                    std::cout << "4\n";
                    std::ifstream file("script.js");
                    if (file.is_open()) {
                        std::cout << "File script.js opened successfully" << std::endl;
                        std::stringstream buffer;
                        buffer << file.rdbuf();
                        response = http_response(buffer.str(), 200, "application/javascript");
                        file.close();
                    } else {
                        std::cout << "Error opening script.js file" << std::endl;
                        response = http_response("Error 404: script.js not found", 404, "text/plain");
                    }
                }
            }
            else {
                response = http_response("<html><body><h1>Hello, World!</h1></body></html>", 200, "text/html");
            }
            send(client_socket, response.c_str(), response.size(), 0);
        }
    }

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

    void write_log_to_db(sqlite3* db, const std::string& record) {
        std::string query = "INSERT INTO logs (timestamp, temperature) VALUES ('" + get_current_time() + "', '" + record + "');";
        char* error_message = nullptr;
        int rc = sqlite3_exec(db, query.c_str(), nullptr, nullptr, &error_message);
        if (rc != SQLITE_OK) {
            std::cerr << "SQL error: " << error_message << std::endl;
            sqlite3_free(error_message);
        
        }
    }

    void free_resources(std::fstream* lastRecordFile, sqlite3** db, int* fd) {
        if (lastRecordFile != nullptr) { lastRecordFile->close(); delete lastRecordFile; }
        if (fd != nullptr) { close_port(*fd); *fd = -1; }
        if (db != nullptr) {sqlite3_close(*db); *db = nullptr; }
        sem_unlink(SEMAPHORE_OBJECT_NAME);
    }

    bool is_file_empty(std::fstream& file) {
        file.seekg(0, std::ios::end);
        long size = file.tellg();
        file.seekg(0, std::ios::beg);
        return size == 0;
    }


    void* data_processing_thread(void *args) {
        ThreadData *params = (ThreadData*)args;
        std::vector<char> buffer(255);
        std::string logRecord;
        std::string currentTime;
        double currentTemperature;
        while (!need_exit) {
            ssize_t bytesRead = read(params->serialPort, buffer.data(), buffer.size());
            if (bytesRead > 0) {
                std::string tempStr(buffer.data(), bytesRead);
                size_t first = tempStr.find_first_not_of(" \t\n\r");
                size_t last = tempStr.find_last_not_of(" \t\n\r");
                if (first != std::string::npos && last != std::string::npos)
                    tempStr = tempStr.substr(first, (last - first + 1));
                else
                    tempStr = "";
                try {
                    currentTemperature = std::stod(tempStr);
                }
                catch (const std::invalid_argument& e) {
                        std::cerr << "Invalid argument in temperature string: " << tempStr << std::endl;
                        currentTemperature = 0.0;
                    }

                currentTime = get_current_time();
                logRecord = std::to_string(currentTemperature);
                std::string fixed_record;
                make_fixed_length_record(fixed_record, logRecord);
                write_log_to_db(params->db, fixed_record);
                
                params->dataMutex.lock();
                *params->portData = std::to_string(currentTemperature);
                *params->recordCounter = *params->recordCounter + 1;
                *params->averageValue += (currentTemperature - *params->averageValue) / *params->recordCounter;
                params->dataMutex.unlock();
            }
            usleep(1000 * PORT_SPEED_MS);
        }
        return 0;
    }

    void* hourly_log_thread(void *args) {
        ThreadData *params = (ThreadData*)args;
        std::string currentTime;
        std::string logRecord;
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
                write_log_to_db(params->db, fixed_record);

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
        while (!need_exit) {
            time_t current_time = time(NULL);
            if (current_time >= *params->nextLogTime) {

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
                write_log_to_db(params->db, fixed_record);

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

    void* web_server_thread(void* args) {
        ThreadData *params = (ThreadData*)args;
        std::string currentTemperatureStr;

        int server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == -1) {
            perror("socket creation failed");
            return nullptr;
        }

        sockaddr_in server_address;
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = INADDR_ANY;
        server_address.sin_port = htons(SERVER_PORT);

        if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
            perror("bind failed");
            close(server_socket);
            return nullptr;
        }

        if (listen(server_socket, SOMAXCONN) == -1) {
            perror("listen failed");
            close(server_socket);
            return nullptr;
        }
        std::cout << "Server listening on port " << SERVER_PORT << std::endl;
        while (!need_exit) {
            int client_socket = accept(server_socket, NULL, NULL);
            if (client_socket == -1) {
                perror("accept failed");
                continue;
            }
        
            params->dataMutex.lock();
            currentTemperatureStr = *params->portData;
            params->dataMutex.unlock();

            handle_client(client_socket, params->db, currentTemperatureStr);
            close(client_socket);
        }
        close(server_socket);
        return nullptr;
    }
#endif