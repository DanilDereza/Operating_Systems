#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
    #define sleep(x) Sleep(x * 1000)

    using rc_mutex = CRITICAL_SECTION;
    using rc_thread = HANDLE;

    #define init_mutex(mutex) InitializeCriticalSection(&(mutex))
    #define delete_mutex(mutex) DeleteCriticalSection(&(mutex))
    #define lock_mutex(mutex) EnterCriticalSection(&(mutex))
    #define unlock_mutex(mutex) LeaveCriticalSection(&(mutex))

#else
    #include <pthread.h>
    #include <unistd.h>
    #include <sys/wait.h>
    #include <limits.h>

    #define init_mutex(mutex) pthread_mutex_init(&(mutex), nullptr)
    #define delete_mutex(mutex) pthread_mutex_destroy(&(mutex))
    #define lock_mutex(mutex) pthread_mutex_lock(&(mutex))
    #define unlock_mutex(mutex) pthread_mutex_unlock(&(mutex))

    using rc_mutex = pthread_mutex_t;
    using rc_thread = pthread_t;

    rc_mutex global_mutex; // Синхронизация доступа к разделяемым данным между потоками или процессами
#endif

struct Program { // Структура Program используется для представления информации о каждом дочернем процессе, который будет запущен родительским процессом.
    std::string prog_name; 
    std::vector<std::string> args;
};

int children_exited = 0; // Количество завершившихся дочерних процессов

#ifdef _WIN32
    void handle_child(PROCESS_INFORMATION pi, rc_mutex &mutex) { // мониторинг состояния дочернего процесса, управляя его завершением и обеспечивая синхронизацию доступа к глобальным данным
    // PROCESS_INFORMATION pi - это структура, содержащая информацию о созданном дочернем процессе и его главном потоке. Она передаётся в функцию, чтобы: получить дескриптор процесса (pi.hProcess) для ожидания его завершения, узнать идентификатор процесса (pi.dwProcessId) для вывода в консоль.
    // pi создаётся в функции start_processes при вызове CreateProcess
    // rc_mutex &mutex - ссылка на объект мьютекса, который используется для синхронизации доступа к глобальной переменной children_exited
        std::cout << "THREAD is handling child processes...\n";

        while (true) {
            DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE); // ожидание завершения процесса, связанного с дескриптором pi.hProcess, DWORD - аналог unsigned int
            if (waitResult == WAIT_OBJECT_0) {
                DWORD exitCode;
                if (!GetExitCodeProcess(pi.hProcess, &exitCode)) { // получение кода завершения дочернего процесса, &exitCode - LPDWORD, код завершения будет записан в переменную exitCode
                    std::cerr << "GetExitCodeProcess failed (" << GetLastError() << ").\n";
                } else {
                    std::cout << "CHILD with PID " << pi.dwProcessId << " exited with code " << exitCode << "\n";
                }
                CloseHandle(pi.hProcess); // закрывает дескриптор pi.hProcess, который был создан функцией CreateProcess
                CloseHandle(pi.hThread); // закрывает дескриптор pi.hThread, который был создан функцией CreateProcess
                lock_mutex(mutex); // блокирует mutex - блокировка гарантирует, что только один поток сможет изменять children_exited
                ++children_exited;
                unlock_mutex(mutex); // разблокирует mutex
                break;
            } else {
                std::cerr << "Error waiting: " << GetLastError() << "\n"; // возвращает код в виде DWORD последней ошибки, произошедшей в текущем потоке выполнения
                break;
            }
        }
    }
#else
    void *handle_child(void *) { // ничего не передаётся, всё, что нужно для обработки, доступно глобально (например, global_mutex и вызовы waitpid)
        std::cout << "THREAD is handling child processes...\n";

        while (true) {
            int status; // хранит информацию о завершении дочернего процесса
            pid_t pid = waitpid(-1, &status, 0); // ожидание завершения одного из дочерних процессов и получение его статуса
            // -1 в waitpid - ждёт завершения любого дочернего процесса текущего процесса
            // options = 0 - стандартное поведение функции
            if (pid > 0) {
                if (WIFEXITED(status)) { // проверка, завершился ли дочерний процесс нормально, true - нормально, false - ненормально
                    std::cout << "CHILD with PID " << pid << " exited with code " << WEXITSTATUS(status) << "\n";
                    // WEXITSTATUS(status) - возвращает тот самый код, который был передан в exit()
                    lock_mutex(global_mutex);
                    ++children_exited;
                    unlock_mutex(global_mutex);
                }
            } else if (pid == -1) { // ошибка при создании дочернего процесса, например превышен лимит на количество дочерних процессов
                sleep(1);
                if (children_exited > 0) {
                    break;
                }
            }
        }
        return nullptr;
    }
#endif

#ifdef _WIN32
    std::wstring to_wstring(const std::string &str) { // используется для совместимости между системами, работающими в среде Windows, где строки часто передаются в виде std::wstring (Unicode C++), а не в формате UTF-8
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0); // функция из WinAPI, которая преобразует строку в многобайтовом формате в строку в широком формате
        // CP_UTF8 - кодировка, с которой будет работать функция
        // флаг преобразования 0 - означает, что дополнительных опций не используется
        // str.c_str() - указатель на C-строку (массив типа const char*)
        // -1 - указывает функции, что строка будет обработана до первого нулевого байта (\0), т.е. длина строки будет вычислена автоматически
        // nullptr - указатель на буфер, куда будет записан результат преобразования; nullptr, потому что в первую очередь хотим узнать размер результата.
        // 0 - размер буфера для вывода. Мы передаем 0, потому что хотим вычислить необходимый размер буфера
        std::wstring wstr(size_needed, 0); // создаёт пустую wide строку длиной size_needed
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
        return wstr;
    }

    void start_processes(int prog_count, std::vector<Program> &programs, bool block_parent) { // запуск нескольких дочерних процессов в родительском процессе и управления их выполнением
    // prog_count - количество дочерних процессов, которые нужно запустить
    // std::vector<Program> &programs - вектор структур Program, каждая из которых содержит имя программы и список её аргументов
    // bool block_parent - флаг, который определяет, должен ли родительский процесс ждать завершения каждого дочернего процесса
    // если true, то родительский процесс будет блокироваться и ждать завершения каждого дочернего процесса поочередно
    // если false, родительский процесс продолжит свою работу параллельно с дочерними процессами, а для мониторинга процессов будут создаваться отдельные потоки
        rc_mutex mutex;
        if (!block_parent)
            init_mutex(mutex); // переменная типа мьютекса, которая должна быть инициализирована перед использованием

        STARTUPINFO si = {}; // STARTUPINFO - cтруктура в Windows API, которая содержит информацию о том, как новый процесс должен быть запущен
        si.cb = sizeof(si); // размер самой структуры в байтах (DWORD); необходимо для правильной работы функций Windows API, таких как CreateProcess, которые используют эту структуру
        PROCESS_INFORMATION pi[prog_count]; // каждый элемент массива будет хранить данные о соответствующем дочернем процессе

        for (int i = 0; i < prog_count; ++i) {
            std::string command = programs[i].prog_name;
            for (const auto &arg : programs[i].args) {
                command += " " + arg;
            }

            std::wstring command_wstr = to_wstring(command);
            if (!CreateProcess(nullptr, &command_wstr[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi[i])) { // создаёт новый процесс и его основной поток
                // nullptr - указывает имя исполнимого файла, имя программы будет извлечено из командной строки (при помощи следующего параметра)
                // command_wstr - строка, которая включает полную команду (имя программы и все её аргументы); cтрока передается через адрес первого символа
                // nullptr - создается процесс с атрибутами по умолчанию
                // nullptr - создается поток с атрибутами по умолчанию
                // FALSE - дескрипторы, открытые в родительском процессе, не будут унаследованы дочерним процессом
                // 0 - никаких дополнительных флагов для создания процесса не используется
                // nullptr - для дочернего процесса будут использоваться переменные окружения родительского процесса
                // nullptr - рабочая директория будет унаследована от родительского процесса
                // &si - указатель на структуру STARTUPINFO
                // &pi[i] - указатель на структуру PROCESS_INFORMATION, в которой хранятся данные о процессе после его создания, такие как дескрипторы процесса и потока, их идентификаторы и т.д.
                std::cerr << "CreateProcess failed (" << GetLastError() << ").\n";
                exit(1);
            } 
            else {
                std::cout << "PARENT created CHILD with PID " << pi[i].dwProcessId << "\n"; // dwProcessId - уникальный идентификатор для каждого процесса в операционной системе
            }

            if (block_parent) {
                WaitForSingleObject(pi[i].hProcess, INFINITE); // блокирует выполнение текущего потока до тех пор, пока указанный процесс не станет в состоянии "сигнального" (то есть не завершится)
                DWORD exitCode;
                if (!GetExitCodeProcess(pi[i].hProcess, &exitCode)) { // получение кода завершения дочернего процесса
                    std::cerr << "GetExitCodeProcess failed (" << GetLastError() << ").\n";
                    exit(1);
                }
                std::cout << "CHILD exited with code " << exitCode << "\n";
            } 
            else {
                rc_thread thread;
                thread = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)handle_child, &pi[i], 0, nullptr); // cоздаёт новый поток (thread) в Windows с помощью функции CreateThread и передаёт ему задачу обработки завершения дочернего процесса, используя функцию handle_child
                // nullptr - стандартные атрибуты безопасности для потока
                // 0 - стандартный размер стека нового потока
                // (LPTHREAD_START_ROUTINE)handle_child - указатель на функцию, которая будет выполняться в новом потоке; (LPTHREAD_START_ROUTINE) — это приведение типа
                // &pi[i] - информация, которая будет использоваться в функции handle_child, чтобы следить за завершением соответствующего дочернего процесса
                // 0 - потоку не назначены специальные флаги
                // nullptr - указывает на переменную, в которую будет записан идентификатор потока; в данном случае идентификатор потока не нужен
                if (!thread) {
                    std::cerr << "Error creating thread: " << GetLastError() << "\n";
                    exit(1);
                }
            }
        }

        if (!block_parent)
            delete_mutex(mutex); // удаление ранее инициализированного мьютекса
    }

#else
    void start_processes(int prog_count, std::vector<Program> &programs, bool block_parent) {
        init_mutex(global_mutex);

        for (int i = 0; i < prog_count; ++i) {
            pid_t pid = fork(); // создание копии родительского процесса - дочерний процесс
            if (pid == 0) { // pid == 0 - текущий процесс является дочерним
                std::vector<char *> args; // создание вектора args, который будет хранить указатели на строки типа char*
                for (const auto &arg : programs[i].args) { // передача аргументов командной строки в вызываемую программу
                    args.push_back(const_cast<char *>(arg.c_str())); // c_str() возвращает const char*, так как std::string не позволяет напрямую изменять содержимое своей строки; однако execv принимает аргументы в виде char* (не const char*). Чтобы обойти это ограничение, используется const_cast<char *>, чтобы снять константность
                }
                args.push_back(nullptr);
                execv(programs[i].prog_name.c_str(), args.data()); // запуск новой программы в теущем процессе
                // первый аргумент - путь к исполняемому файлу, строка типа std::string, содержащая путь к программе, которую нужно запустить
                //  второй аргумент - аргументы командной строки, оканчивающиеся nullptr, args.data() возвращает указатель на первый элемент вектора args, который представляет массив char*
                exit(1); // execv failed
            } else if (pid > 0) { // родительский процесс
                std::cout << "PARENT created CHILD with PID " << pid << "\n";
                if (block_parent) {
                    int status;
                    waitpid(pid, &status, 0);
                    // используется в POSIX-системах (например, Linux, macOS) для ожидания завершения процесса с указанным PID и получения его статуса завершения
                    std::cout << "CHILD exited with code " << WEXITSTATUS(status) << "\n";
                } else {
                    rc_thread thread;
                    pthread_create(&thread, nullptr, handle_child, nullptr); // создание нового потока
                    // &thread - идентификатор нового потока
                    // nullptr - атрибуты по умолчанию
                    // handle_child - указатель на функцию, которую выполнит новый поток
                    // nullptr - указатель, который передаётся в функцию handle_child в качестве её аргумента
                    pthread_detach(thread); //  перевод потока в "отсоединённое" состояние, то есть после завершения потока его ресурсы будут автоматически освобождены операционной системой, и основной поток программы не сможет ожидать завершения этого потока с помощью pthread_join
                }
            } else {
                std::cerr << "Fork error: " << std::strerror(errno) << std::endl;
                exit(1);
            }
        }

        if (!block_parent) {
            while (children_exited < prog_count) {
                sleep(1);
            }
            delete_mutex(global_mutex);
        }
    }
#endif