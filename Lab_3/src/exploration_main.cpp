#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <sstream> // классы для работы с потоками в памяти
#include <iomanip> // управление форматом ввода и вывода, в частности для чисел, строк и других типов данных
#include <ctime>
#include <cstdint> // для получения целочисленных типов определённого размера

#ifdef _WIN32
    #include <windows.h>
    #include <process.h> // функции для работы с процессами, такими как создание новых процессов, управление потоками и взаимодействие с операционной системой
    #include <io.h> // операции с файлами и потоками, которые не входят в стандартные потоки ввода/вывода
    #include <conio.h> // для получения символов с клавиатуры
#else
    #include <unistd.h> // функции, которые помогают управлять процессами, файловыми дескрипторами, директориями, а также некоторые системные вызовы, такие как управление сигналами
    #include <sys/wait.h> // макросы и функции для работы с процессами, в частности для ожидания завершения дочерних процессов
    #include <sys/time.h>
    #include <sys/mman.h> // функции и макросы для управления памятью в процессе
    #include <fcntl.h> // функции и макросы для работы с файловыми дескрипторами, включая открытие, закрытие файлов, изменение режима работы с файлами и управление флагами файловых дескрипторов
    #include <semaphore.h>
    #include <termios.h> // настройка терминала, управления режимами ввода-вывода и обработка сигналов, которые могут быть посланы в процессе работы терминала
    #include <sys/select.h> // механизмы для мониторинга множества файловых дескрипторов, с целью ожидания событий ввода-вывода или сигналов (замена conio.h)
    #include <csignal> // обработка сигналов в программном обеспечении
    #include <pthread.h>
#endif
#include <limits> // границы числовых типов данных, таких как минимальные и максимальные значения для целых чисел

// IPC Constants
#define SHM_NAME "my_shared_memory"
#define MUTEX_NAME "my_mutex" // используется для синхронизации доступа к SharedData
#define LEADER_MUTEX_NAME "leader_mutex" // используется для реализации лидерства между процессами

// Shared data
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

// Logging func
void log_message(std::ofstream& logFile, const std::string& message);
std::string get_current_time_ms();

// Timer and counter
#ifdef _WIN32
DWORD WINAPI timer_thread(LPVOID lpParam);
std::wstring to_wstring(const std::string &str);

#else
void* timer_thread(void* arg);
#endif
void process_user_input(SharedData* sharedData, void* mutex);

// Child Process spawning
bool spawn_process1(std::ofstream& logFile, const std::string& appPath, bool& child_running);
bool spawn_process2(std::ofstream& logFile, const std::string& appPath, bool& child_running);

// Leader Election
bool acquire_leader_mutex(void** mutex);
void release_leader_mutex(void* mutex);
bool is_leader(void* mutex);

// Signal handling

// Директивы препроцессора, которые помогают предотвратить повторное включение этого заголовочного файла в одном исходном файле
#ifndef GLOBALS_H
#define GLOBALS_H

// В других исходных файлах можно использовать эти переменные, но их фактическое размещение в памяти и создание будет происходить в другом месте программы
extern SharedData* sharedData_global;
extern void* shm_global;
extern void* mutex_global;
extern void* leaderMutex_global;
#endif // GLOBALS_H

void signal_handler(int signum) { // отвечает за освобождение всех связанных с процессом ресурсов, таких как мьютексы и общая память, перед тем, как процесс завершится
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

SharedData* sharedData_global = nullptr;
void* shm_global = nullptr;
void* mutex_global = nullptr;
void* leaderMutex_global = nullptr;

#ifdef _WIN32
LARGE_INTEGER get_current_time_large_integer() {
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time); // возвращает значение внутреннего счётчика производительности, которое представляет собой количество тиков с момента старта системы
    return time;
}

double get_time_diff_seconds(LARGE_INTEGER start, LARGE_INTEGER end) { // вычисляет разницу между двумя моментами времени (начальным и конечным) в секундах
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return static_cast<double>(end.QuadPart - start.QuadPart) / freq.QuadPart;
}
#else

double get_time_diff_seconds(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
}

struct timeval get_current_time_timeval() {
    struct timeval time;
    gettimeofday(&time, NULL); // заполняет структуру time текущим временем (с точностью до микросекунд)
    return time;
}
#endif

// Helper struct to pass data to the thread
struct ThreadData {
    SharedData* sharedData;
    void* mutex; //  указатель на мьютекс, который используется для синхронизации доступа потоков к общим данным
};


int main(int argc, char* argv[]) {
    // Get application path
    std::string appPath = argv[0]; // получение пути к приложению

    // Shared Memory
    void* shm = create_shared_memory(sizeof(SharedData)); // создание и отображение разделяемой памяти
    if (shm == nullptr) {
        std::cerr << "Failed to create shared memory" << std::endl;
        return 1;
    }

    SharedData* sharedData = static_cast<SharedData*>(map_shared_memory(shm)); // программа создаёт разделяемую память для структуры SharedData
    if (sharedData == nullptr) {
        std::cerr << "Failed to map shared memory" << std::endl;
        close_shared_memory(shm, nullptr);
        return 1;
    }

    // Mutex for counter access
    void* mutex = nullptr; // создание мьютекса для доступа к счётчику
    if (!create_mutex(&mutex)) {
        std::cerr << "Failed to create mutex" << std::endl;
        close_shared_memory(shm, sharedData);
        return 1;
    }

    // Mutex for leader
    void* leaderMutex = nullptr; // создание мьютекса лидера
    if (!acquire_leader_mutex(&leaderMutex)) { // попытка захвата мьютекса лидера
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
    // Windows specific signal handling is not directly supported
    // Implement Windows-specific cleanup at the end of program execution
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
    logFile.open(logFileName, std::ios::app); // файл открывается в режиме добавления (std::ios::app); это означает, что новые записи будут добавляться в конец файла, не перезаписывая его содержимое
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
                    child1_spawned = spawn_process1(logFile, appPath, child1_running);
                    child1_running = child1_spawned;
                }
                else {
                    log_message(logFile, "Child 1 skipped (still running).");
                }

                if (!child2_running) {
                    child2_spawned = spawn_process2(logFile, appPath, child2_running);
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
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, _getpid() + 1);
            if (GetExitCodeProcess(hProcess, &exitCode)) {
                if (exitCode != STILL_ACTIVE) {
                    child1_running = false;
                    CloseHandle(hProcess);
                }
            }
        }
        if (child2_running) {
            DWORD exitCode;
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, _getpid() + 2);
            if (GetExitCodeProcess(hProcess, &exitCode)) {
                if (exitCode != STILL_ACTIVE) {
                    child2_running = false;
                    CloseHandle(hProcess);
                }
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
    // Code should not reach here
    return 0;
}

// Timer function
#ifdef _WIN32
DWORD WINAPI timer_thread(LPVOID lpParam) {
    ThreadData* data = (ThreadData*)lpParam;
    SharedData* sharedData = data->sharedData;
    void* mutex = data->mutex;
    while (true) {
        Sleep(300); // Windows sleep in milliseconds
        lock_mutex(mutex);
        sharedData->counter++;
        unlock_mutex(mutex);
    }
    return 0;
}
#else
void* timer_thread(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    SharedData* sharedData = data->sharedData;
    void* mutex = data->mutex;
    while (true) {
        usleep(300000); // Linux/POSIX sleep in microseconds
        lock_mutex(mutex);
        sharedData->counter++;
        unlock_mutex(mutex);
    }
    return nullptr;
}
#endif

void process_user_input(SharedData* sharedData, void* mutex) {
#ifdef _WIN32
    // Код для Windows
    if (_kbhit()) {
        std::string input;
        std::cin >> input;
        // Очистка потока ввода
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
#else
    // Код для POSIX
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt); // Сохраняем старые настройки терминала
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); // Отключаем канонический ввод и эхо
    tcsetattr(STDIN_FILENO, TCSANOW, &newt); // Применяем новые настройки
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
        // Очистка потока ввода
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
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // Восстанавливаем старые настройки
#endif
}

// Function to add message to log
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
    localtime(&time_info, &time_sec);
#else
    localtime_r(&time_sec, &time_info);
#endif
    std::stringstream ss;
    ss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << (value % 1000);
    return ss.str();
}

#ifdef _WIN32
    std::wstring to_wstring(const std::string &str) {
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
        return wstr;
    }

    void* create_shared_memory(size_t size) { // используется для отображения (или маппинга) области разделяемой памяти в адресное пространство процесса
        HANDLE hMapFile = CreateFileMappingW(
            INVALID_HANDLE_VALUE,   // 1. Указывает, что это не привязанный файл
            NULL,                   // 2. Атрибуты безопасности (NULL, значит стандартные)
            PAGE_READWRITE,         // 3. Права доступа (чтение/запись)
            0,                      // 4. Высокий порядок (не используется)
            size,                   // 5. Размер памяти
            to_wstring(SHM_NAME).c_str() // 6. Имя объекта для разделяемой памяти
        );
        if (hMapFile == NULL) {
            return nullptr; // Если создание объекта не удалось, возвращается nullptr
        }
        return hMapFile;  // Если все прошло успешно, возвращается дескриптор объекта
    }

    void* map_shared_memory(void* shm) {
        HANDLE hMapFile = static_cast<HANDLE>(shm);
        void* shmem_ptr = MapViewOfFile(
            hMapFile,          // Дескриптор объекта разделяемой памяти
            FILE_MAP_ALL_ACCESS,  // Права доступа к отображаемой памяти
            0,                  // Смещение старшей части (по умолчанию 0)
            0,                  // Смещение младшей части (по умолчанию 0)
            0                  // Размер отображаемой области (0 означает отображение всей памяти)
        );
        if (shmem_ptr == NULL) { // eсли MapViewOfFile не смогла успешно отобразить память
            return nullptr; // функция возвращает nullptr для обозначения неудачи
        }
        return shmem_ptr;
    }

    bool create_mutex(void** mutex) { // в функции используется *mutex, чтобы изменить саму переменную указателя, на который был передан адрес в качестве аргумента
        HANDLE hMutex = CreateMutexW(NULL, FALSE, to_wstring(MUTEX_NAME).c_str());
        if (hMutex == NULL) { // возникла ошибка при создании мьютекса
            return false;
        }
        *mutex = hMutex; // изменение значения указателя на мьютекс
        return true;
    }

    bool lock_mutex(void* mutex) { // пытается захватить мьютекс, чтобы синхронизировать доступ к общим ресурсам в многозадачной среде
        HANDLE hMutex = static_cast<HANDLE>(mutex); // функция получает указатель на мьютекс типа void*, который затем приводится к типу HANDLE с помощью static_cast. HANDLE — это дескриптор, который используется в Windows для работы с объектами операционной системы
        if (WaitForSingleObject(hMutex, INFINITE) == WAIT_FAILED) { // пытается захватить мьютекc
            return false;
        }
        return true;
    }

    void unlock_mutex(void* mutex) {
        HANDLE hMutex = static_cast<HANDLE>(mutex);
        ReleaseMutex(hMutex); // ReleaseMutex используется для освобождения мьютекса
    }

    void close_shared_memory(void* shm, void* sharedData) { // закрывает разделяемую память и связанные с ней ресурсы
    // первый аргумент - указатель на объект, представляющий дескриптор файлового отображения разделяемой памяти
    // второй аргумент - указатель на область памяти, которая была отображена в пространство адресов процесса с помощью MapViewOfFile, указывает на данные в разделяемой памяти
        HANDLE hMapFile = static_cast<HANDLE>(shm); // приводит указатель void* shm к типу HANDLE, так как HANDLE используется для работы с объектами операционной системы
        if (sharedData != nullptr) UnmapViewOfFile(sharedData); // если указатель sharedData не равен nullptr, то это означает, что отображение разделяемой памяти было выполнено с помощью MapViewOfFile, и теперь необходимо отменить это отображение
    // UnmapViewOfFile - удаляет область данных из пространства адресов процесса
        CloseHandle(hMapFile); // закрывает дескриптор отображения памяти
    }

void close_mutex(void* mutex) {
    HANDLE hMutex = static_cast<HANDLE>(mutex);
    CloseHandle(hMutex);
}

bool acquire_leader_mutex(void** mutex) { // acquire_leader_mutex выполняет несколько действий, связанных с созданием и захватом мьютекса для синхронизации между различными экземплярами программы
    HANDLE hMutex = CreateMutexW(NULL, TRUE, to_wstring(LEADER_MUTEX_NAME).c_str()); // функция CreateMutexW пытается создать мьютекс с именем LEADER_MUTEX_NAME. Если мьютекс с таким именем уже существует, то программа захватывает его, иначе создается новый
    if (hMutex == NULL) { // если при создании мьютекса произошла ошибка (например, нет прав на создание), функция возвращает false
        return false;
    }
    *mutex = hMutex;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { // проверяет, является ли мьютекс уже существующим
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        *mutex = nullptr;
        return true;
    }
    return true;
}

void release_leader_mutex(void* mutex) { // функция освобождает лидерский мьютекс и закрывает его дескриптор
    if (mutex != nullptr) {
        ReleaseMutex(static_cast<HANDLE>(mutex));
        CloseHandle(static_cast<HANDLE>(mutex));
    }
}

bool is_leader(void* mutex) { // функция проверяет, является ли текущий процесс лидером, то есть был ли успешно захвачен лидерский мьютекс
    return mutex != nullptr;
}

bool spawn_process1(std::ofstream& logFile, const std::string& appPath, bool& child_running) {
    // структуры STARTUPINFOW и PROCESS_INFORMATION - используются для хранения информации о процессе, который будет создан
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); // очищает память для этих структур, чтобы избежать использования мусора в полях
    si.cb = sizeof(si); //  устанавливает корректный размер структуры STARTUPINFOW, что требуется для корректной работы API
    ZeroMemory(&pi, sizeof(pi));

    std::string childPath = appPath.substr(0, appPath.find_last_of("\\/")) + "\\child1.exe"; // формирование пути для дочернего процесса
    if (!CreateProcessW(to_wstring(childPath).c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        // to_wstring(childPath).c_str() — путь к исполняемому файлу дочернего процесса в формате wstring, так как функция требует широких символов
        std::stringstream errMessage;
        errMessage << "CreateProcess failed for child1 (" << GetLastError() << ")" << std::endl;
        log_message(logFile, errMessage.str());
        return false;
    }
    CloseHandle(pi.hThread);
    // WaitForSingleObject(pi.hProcess, INFINITE);
    // CloseHandle(pi.hProcess);
    // child_running=false;
    return true;
}

bool spawn_process2(std::ofstream& logFile, const std::string& appPath, bool& child_running) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    std::string childPath = appPath.substr(0, appPath.find_last_of("\\/")) + "\\child2.exe";
    if (!CreateProcessW(to_wstring(childPath).c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        std::stringstream errMessage;
        errMessage << "CreateProcess failed for child2 (" << GetLastError() << ")" << std::endl;
        log_message(logFile, errMessage.str());
        return false;
    }
    CloseHandle(pi.hThread); // после того как процесс создан, дескриптор потока (pi.hThread) закрывается, потому что он уже не нужен
    // WaitForSingleObject(pi.hProcess, INFINITE);
    // CloseHandle(pi.hProcess);
    // child_running=false;
    return true;
}
#else
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
    if (pid == 0) {
        // Child process
        std::string childPath = appPath.substr(0, appPath.find_last_of("/\\")) + "/child1";
        const char* argv[] = { childPath.c_str(), NULL };
        execv(argv[0], (char**)argv);
        std::stringstream errMessage;
        errMessage << "execv failed for child1 (" << errno << ")" << std::endl;
        log_message(logFile, errMessage.str());
        exit(1);
    }
    else if (pid > 0) {
        // Parent process
        //   int status;
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
    if (pid == 0) {
        // Child process
        std::string childPath = appPath.substr(0, appPath.find_last_of("/\\")) + "/child2";
        const char* argv[] = { childPath.c_str(), NULL };
        execv(argv[0], (char**)argv);
        std::stringstream errMessage;
        errMessage << "execv failed for child2 (" << errno << ")" << std::endl;
        log_message(logFile, errMessage.str());
        exit(1);
    }
    else if (pid > 0) {
        // Parent process
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