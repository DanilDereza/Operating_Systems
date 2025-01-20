
#include "serial.h"
#include <iostream>
#include <fstream>
#include <sstream> // библиотека для работы с потоками строк
#include <vector>
#include <ctime> // библиотека для работы с датой и временем
#include <iomanip> // библиотека для форматирования ввода и вывода
#include <chrono> // библиотека для работы с высокоточным временем и измерением времени выполнения

#ifdef _WIN32
#    include <windows.h> // библиотека для взаимодействия с операционной системой Windows
#else
#    include <termios.h> // библиотека, которая используется для настройки параметров терминала, таких как скорость передачи данных, паритет, количество стоп-битов и другие параметры для последовательного ввода-вывода
#    include <pthread.h> // библиотека, которая предоставляет функции для работы с потоками
#    include <semaphore.h> // библиотека для работы с семафорами (механизм синхронизации)
#    include <sys/time.h> // библиотека для работы с системным временем
#    include <cstring> // библиотека для работы со строками в стиле C
#    include <csignal> // библиотека для обработки сигналов позволяет управлять сигналами, такими как прерывание программы, запрос завершения программы, сегментационная ошибка
#    include <fcntl.h> // библиотека для управления файлами и дескрипторами
#    include <unistd.h> // библиотека, которая содержит функции для управления процессами и взаимодействия с ОС
#endif

#ifdef _WIN32
#    define PORT_RD "COM9" // макрос, определяющий последовательный порт, почему не COM1?
#    define SemaphoreWait(sem) WaitForSingleObject(sem, INFINITE);
#    define SemaphorePost(sem) ReleaseSemaphore(sem, 1, NULL)
#    define sleep(x) Sleep(x * 1000)
#else
#    define PORT_RD "/dev/pts/4" // виртуальная файловая система для псевдотерминалов, почему не /dev/ttyS0?
#    define SemaphoreWait(sem) sem_wait(sem)
#    define SemaphorePost(sem) sem_post(sem)
#    define sleep(x) sleep(x)
#endif

#define LOG_FILE_NAME "log.txt" // log_file
#define LOG_FILE_NAME_HOUR "log_hour.txt" // имя файла для логирования данных за послений час
#define LOG_FILE_NAME_DAY "log_day.txt" // имя файла для логирования данных за день
#define FILE_LAST_RECORD "tmp/tmp.txt" // имя временного файла, в который записывается последняя обратная запись: для восстановления состояния после сбоя программы

#define SEMAPHORE_OBJECT_NAME "/my_semaphore" // имя семафора, который используется для синхронизации потоков или процессов

#define RECORD_LENGTH 30 // для одной записи в логе (в символах)

#define SEC_IN_HOUR 3600 // количество секунд в одном часе
#define SEC_IN_DAY 86400 // количество секунд в одном дне
#define HOURS_IN_MONTH 720 // количество часов в 30-дневном месяце

struct thr_data { // структура для хранения данных, которые будут использоваться в потоках или функциях, связанных с обработкой данных, синхронизацией и записью в файл
    std::ofstream *file; // указатель наобъект std::ofstream, который используется для записи данных в файл
    time_t *next; // указатель на объект time_t - переменную, использующуюся для определения времени следующего обновления лог-файла
    double *avg; // указатель на еременную, которая хранит среденее значение, вычисляемое на основе данных
    int *counter; // указатель на счётсик, который использется для подсчёта количества записанных данных или обработанных событий.
    int *last_record_pos; // указатель на позицию последней записи в файл
#ifdef _WIN32
    HANDLE thr_sem; // семафор, использующийся для синхронизации доступа к общим ресурсам
#else
    sem_t *thr_sem;
#endif
};

volatile unsigned char need_exit = 0; // глобальная переменная, которая сигнализирует потокам о необходимости завершения работы, volatile - переменная может быть изменена другим потоком

#ifdef _WIN32
BOOL WINAPI sig_handler(DWORD signal) // определяет обработчик сигналов, предназначенный для корректного завершения программы при получении сигнала прерывания, DWORD - аналог unsigned int
{
    if (signal == CTRL_C_EVENT) { // CTRL_C_EVENT - прерывание ctrl+c
        need_exit = 1; // устанавливаем флаг завершения
        return TRUE; // сообщение системе, что сигнал обработан
    }
    return FALSE; // если сигнал не обработан, возвращаем FALSE
}
#else
void sig_handler(int sig)
{
    if (sig == SIGINT)
        need_exit = 1;
}
#endif

// get current time in YYYY-MM-DD hh:mm:ss.sss
std::string get_time() // функция генерирует строку с текущими датой и временем в формате YYYY-MM-DD HH:MM:SS.mmm
{
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st); // получение текущего времени
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << st.wYear << "-" // строка заполнена нулями и выравнена по ширине
        << std::setw(2) << st.wMonth << "-" 
        << std::setw(2) << st.wDay << " "
        << std::setw(2) << st.wHour << ":"
        << std::setw(2) << st.wMinute << ":"
        << std::setw(2) << st.wSecond << "."
        << std::setw(3) << st.wMilliseconds;
    return oss.str();
#else
    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now(); // получение текущего времени
    std::time_t now_c = std::chrono::system_clock::to_time_t(now); // std::time_t удобен для работы с функциями из стандартной библиотеки C++
    std::tm* tmp = std::localtime(&now_c); // получение удобного для человека представление времени (std::tm) с указанием года, месяца, дня и тд

    if (tmp == nullptr) {
        perror("localtime");
        exit(EXIT_FAILURE); // стандартный код завершения программы при ошибке, в основном 1
    }

    std::ostringstream oss;
    oss << std::put_time(tmp, "%Y-%m-%d %H:%M:%S"); // манипулятор из <iomanip> для форматированного вывода даты и времени
    long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000; // продолжительность времени от начала эпохи UNIX преобразует в миллисекунды, извлекает остаток от деления на 1000, оставляя только миллисекунды от текущей секунды
    oss << "." << std::setfill('0') << std::setw(3) << ms;
    return oss.str();
#endif
}

#ifdef _WIN32
DWORD get_offset(int line_number) // рассчитывает или устанавливает смещение для записи в файл на основе номера строки (номер строки в файлах начинается с 1, а не с 0)
{
    if (line_number <= 0)
        return 0; // возвращает начало файла
    return (line_number - 1) * RECORD_LENGTH; // offset=(line_number−1)×RECORD_LENGTH
}
#else
int get_offset(std::ofstream& file, int line_number)
{
    if (line_number == 0)
        return 0;
    
    long offset = (line_number - 1) * RECORD_LENGTH;
    file.seekp(offset, std::ios::beg); // перемещает указатель позиции записи (write pointer) в файл на рассчитанное смещение от начала (std::ios::beg)
    return file.tellp();  // возвращает текущее положение указателя записи в файле (обычно это будет то же самое offset)
}
#endif

#ifdef _WIN32
void write_log(HANDLE file, const char* record, int size, bool first_opened, int *last_record_pos, const int CYCLE, int index, HANDLE sem)
{
    DWORD offset;
    if (first_opened && last_record_pos[index] < CYCLE) {
        offset = get_offset(last_record_pos[index] + 1);
        SetFilePointer(file, offset, NULL, FILE_BEGIN);
    } else if (last_record_pos[index] >= CYCLE || last_record_pos[index] == 0) {
        SemaphoreWait(sem);
        last_record_pos[index] = 0;
        SemaphorePost(sem);

        SetFilePointer(file, 0, NULL, FILE_BEGIN);
    }
    WriteFile(file, record, size, NULL, NULL);

    SemaphoreWait(sem);
    last_record_pos[index] = last_record_pos[index] + 1;
    SemaphorePost(sem);
}
#else
void write_log(std::ofstream& file, const std::string& record, bool first_opened, int *last_record_pos, const int CYCLE, int index, sem_t *sem) // функция записи данных в файл
{
    int offset;
    if (first_opened && last_record_pos[index] < CYCLE) { // если файл открыт впервые и текущая позиция в файле меньше значения CYCLE, то функция устанавливает указатель на запись после последней записи, 
        offset = get_offset(file, last_record_pos[index] + 1);
         file.seekp(offset, std::ios::beg);
    }
    else if (last_record_pos[index] >= CYCLE || last_record_pos[index] == 0) { // если количество записей достигло CYCLE, или файл пуст, то функция сбрасывает указатель на начало файла
        SemaphoreWait(sem);
        last_record_pos[index] = 0; // last_record_pos[index] будет отслеживать позицию последней записи для конкретного потока
        SemaphorePost(sem);

        file.seekp(0, std::ios::beg);
    }
    file.write(record.c_str(), record.size());
    

    SemaphoreWait(sem);
    last_record_pos[index] = last_record_pos[index] + 1;
    SemaphorePost(sem);
}
#endif

#ifdef _WIN32
bool is_file_empty(HANDLE hFile) // возвращает true, если файл пуст, и false в противном случае.
{
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) { // получает размер файла в байтах
        perror("GetFileSizeEx (tmp_file)");
        exit(EXIT_FAILURE);
    }
    return fileSize.QuadPart == 0; // сравнение на размер файла в 0 байт
}
#else
bool is_file_empty(std::ifstream& file)
{
    file.seekg(0, std::ios::end); // перемещает указатель чтения в конец файла
    long size = file.tellg(); // получает позицию указателя в файле, которая равна размеру файла, если укатель находится в конце
    file.seekg(0, std::ios::beg); // возвращает в начало указатель на файл
    return size == 0;
}
#endif

void make_fixed_record(std::string& fixed_record, const std::string& record) // создаёт строку фиксированной длины на основе входной строки record
{
    fixed_record.assign(RECORD_LENGTH - 1, ' '); // инициализирует переменную fixed_record длиной RECORD_LENGTH-1 и заполняет её пробелами
    fixed_record[RECORD_LENGTH - 1] = '\n'; // добавления символа новой строки
    size_t copy_len = std::min(record.size(), static_cast<size_t>(RECORD_LENGTH - 1)); // определяем, сколько символов из исходной строки record можно скопировать в fixed_record
    fixed_record.replace(0, copy_len, record.substr(0, copy_len)); // заменяем первые copy_len символов в строке fixed_record на подстроку из record той же длины
}

#ifndef _WIN32
void free_resources(std::ofstream* file1, std::ofstream* file2, std::ofstream* file3, std::ifstream* file4, int* fd) // передаютя: указатель на потоки для записи в файлы, а также указатель на файловый дескриптор
{
    if (file1 != nullptr) file1->close();
    if (file2 != nullptr) file2->close();
    if (file3 != nullptr) file3->close();
    if (file4 != nullptr) file4->close();
    if (fd != nullptr) close(*fd);
    sem_unlink(SEMAPHORE_OBJECT_NAME); // удаляет семафор с именем, заданным в SEMAPHORE_OBJECT_NAME
}
#endif

#ifdef _WIN32
void read_last_records(HANDLE file, int *value1, int *value2) // чтение последних записей из файла, открытого на платформе Windows
    // HANDLE file — дескриптор открытого файла, из которого будет выполняться чтение
    // int *value1, int *value2 — указатели на целые числа, в которые будут записаны значения, считанные из файла
{
    char buffer[256] = {0}; // создается массив buffer размером 256 байт для хранения прочитанных данных
    DWORD bytesRead; // хранение количества фактически считанных байт
    if (!ReadFile(file, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) { // ReadFile для чтения данных из файла; sizeof(buffer) - 1 - оставить место для завершающего символа
        perror("ReadFile (tmp_file)");
        exit(EXIT_FAILURE);
    }
    buffer[bytesRead] = '\0'; // добавление нулевого символа в конец строки

    if (sscanf(buffer, "%d %d", value1, value2) != 2) { // функция sscanf пытается извлечь два целых числа из строки buffer и записать их в переменные, на которые указывают value1 и value2;
        perror("sscanf (tmp_file)");
        exit(EXIT_FAILURE); // если из строки не удается извлечь два целых числа (возвращается значение, отличное от 2), программа вызывает ошибку и завершает выполнение
    }
}
#endif
#ifdef _WIN32
DWORD WINAPI thr_routine_hour(void *args) // запись данных в файл каждый час
{
    struct thr_data *params = (struct thr_data*)args; // получение параметров потока
    std::string curr_time; // строка для хранения текущего времени
    std::string record; // строка для записи данных (время и среднее время)
    bool first_opened = true; // флаг, который указывает, открывался ли файл раньше
    while (!need_exit) {
        time_t current_time = time(nullptr); // возвращает общее количество секунд с начала эпохи UNIX
        if (current_time >= *params->next) { // сравнивается с моментом, когда следует записать данные, * - разыменование указателя на структуру

            // change next - обновление времени для следующей записи
            SemaphoreWait(params->thr_sem); // "ожидание", если семафор доступен (его значение больше 0), поток захватывает его, и значение семафора уменьшается на 1
            *params->next += SEC_IN_HOUR; // количество секунд в часе
            SemaphorePost(params->thr_sem); // увеличивает значение семафора, что позволяет другим потокам, ожидающим захвата семафора с помощью SemaphoreWait, продолжить выполнение

            SemaphoreWait(params->thr_sem);
            double avg = *params->avg; // получение среднего значения
            SemaphorePost(params->thr_sem);

            // get record
            curr_time = get_time();
            record = curr_time + " " + std::to_string(avg); // текущее время формируется с помощью функции get_time(), а затем оно комбинируется со средним значением в одну строку record

            // get fixed-sized record
            std::string fixed_record;
            make_fixed_record(fixed_record, record); // make_fixed_record обеспечивает, чтобы строка record имела фиксированную длину, соответствующую RECORD_LENGTH

            write_log(*params->file, fixed_record, first_opened, params->last_record_pos, HOURS_IN_MONTH, 1, params->thr_sem); // выполняет запись подготовленной строки в файл
            first_opened = false; // флаг устанавливается в false, чтобы на следующих итерациях не открывать файл заново

            SemaphoreWait(params->thr_sem);
            *params->avg = 0.0; // сбрасывается значение avg
            SemaphorePost(params->thr_sem);

            SemaphoreWait(params->thr_sem);
            *params->counter = 0; // сбрасывается счётчик counter
            SemaphorePost(params->thr_sem);
        }
        sleep(1); // задержка, чтобы уменьшить нагрузку на процессор, можно вывести в переменную
    }
    return 0;
}
#else
void* thr_routine_hour(void *args)
{
    struct thr_data *params = (struct thr_data*)args;
    std::string curr_time;
    std::string record;
    bool first_opened = true;
    while (!need_exit) {
        time_t current_time = time(NULL);
        if (current_time >= *params->next) {
            // change next
            SemaphoreWait(params->thr_sem);
            *params->next += SEC_IN_HOUR;
            SemaphorePost(params->thr_sem);

            SemaphoreWait(params->thr_sem);
            double avg = *params->avg;
            SemaphorePost(params->thr_sem);

            // get record
            curr_time = get_time();
            record = curr_time + " " + std::to_string(avg);

            // get fixed-sized record
            std::string fixed_record;
            make_fixed_record(fixed_record, record);

            write_log(*params->file, fixed_record, first_opened, params->last_record_pos, HOURS_IN_MONTH, 1, params->thr_sem);
            first_opened = false;

            SemaphoreWait(params->thr_sem);
            *params->avg = 0.0;
            SemaphorePost(params->thr_sem);

            SemaphoreWait(params->thr_sem);
            *params->counter = 0;
            SemaphorePost(params->thr_sem);
        }
        sleep(1);
    }
    return 0;
}
#endif
#ifdef _WIN32
DWORD WINAPI thr_routine_day(void *args) // выполняет периодическую задачу записи данных в лог-файл, при этом обновляя файл каждый раз, когда наступает новый день
{
    struct thr_data *params = (struct thr_data*)args; // получение параметров потока
    std::string curr_time; // строка для хранения текущего времени
    std::string record; // строка для записи данных (время и среднее время)
    bool first_opened = true; // флаг, который указывает, открывался ли файл раньше
    int log_year;
    while (!need_exit) {
        time_t current_time = time(NULL); // возвращает общее количество секунд с начала эпохи UNIX
        if (current_time >= *params->next) {// сравнивается с моментом, когда следует записать данные, * - разыменование указателя на структуру
            // get current_year
            struct tm local_time;
            localtime_s(&local_time, &current_time); // используется для преобразования текущего времени в структуру tm, которая содержит информацию о времени
            int curr_year = local_time.tm_year + 1900; // текущий год

            // обработка изменнения года, если год изменился
            if (first_opened) {
                log_year = curr_year;
            } else if (curr_year != log_year) {
               
                CloseHandle((HANDLE)params->file); // закрытие дескриптора файла

                
                ((HANDLE)params->file) = CreateFile(
                    LOG_FILE_NAME_DAY, // имя файла, который будет создан
                    GENERIC_WRITE, // разрешение на запись в файл
                    FILE_SHARE_READ, // разрешение на совместный доступ к файлу для чтения другими процессами
                    NULL, // атрибуты безопасности (в данном случае не используются)
                    CREATE_ALWAYS, // указывает, что файл будет создан, если его не существует, или перезаписан, если он существует
                    FILE_ATTRIBUTE_NORMAL, // атрибуты файла (в данном случае обычный файл)
                    NULL // шаблон файла
                ); // создние файла

                if (((HANDLE)params->file) == INVALID_HANDLE_VALUE) { // если файл не был открыт или создан успешно, то CreateFile возвращает INVALID_HANDLE_VALUE
                    perror("Error reopening log_file_day");
                    exit(EXIT_FAILURE);
                }

                CloseHandle((HANDLE)params->file); // необходимо сделать перед тем, как попытаться открыть файл заново
                ((HANDLE)params->file) = CreateFile(
                    LOG_FILE_NAME_DAY, // имя файла
                    FILE_APPEND_DATA, // режим добавления
                    FILE_SHARE_READ, // разрешение на совместный доступ к файлу для чтения другими процессами
                    NULL, // атрибуты безопасности (в данном случае не используются)
                    OPEN_ALWAYS, // открывает файл, если он существует, или создает его, если он не существует
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // указывает обычные атрибуты файла и флаг для записи данных сразу на диск (без кэширования)
                    NULL
                );
                if (((HANDLE)params->file) == INVALID_HANDLE_VALUE) { // если файл не был открыт или создан успешно, то CreateFile возвращает INVALID_HANDLE_VALUE
                    perror("Error reopening log_file_day");
                    exit(EXIT_FAILURE);
                }
                log_year = curr_year;
            }

            // change next
            SemaphoreWait(params->thr_sem);
            *params->next += SEC_IN_DAY; // количество секунд в дне
            SemaphorePost(params->thr_sem);

            SemaphoreWait(params->thr_sem);
            double avg = *params->avg; // получение среднего значения
            SemaphorePost(params->thr_sem);

            // get record
            curr_time = get_time();
            record = curr_time + " " + std::to_string(avg);


            // get fixed-sized record
            std::string fixed_record;
            make_fixed_record(fixed_record, record);
            
            
            WriteFile(
                (HANDLE)params->file, // Дескриптор файла
                fixed_record.c_str(), // Указатель на буфер с данными для записи
                fixed_record.size(), // Количество байт для записи
                NULL, // Количество реально записанных байт
                NULL // Указатель на структуру для асинхронной записи
            );
            first_opened = false;

            SemaphoreWait(params->thr_sem);
            *params->avg = 0.0;
            SemaphorePost(params->thr_sem);

            SemaphoreWait(params->thr_sem);
            *params->counter = 0;
            SemaphorePost(params->thr_sem);
        }
        sleep(1);
    }
    return 0;
}
#else
void* thr_routine_day(void *args)
{
    struct thr_data *params = (struct thr_data*)args;
    std::string curr_time;
    std::string record;
    bool first_opened = true;
    int log_year;
    while (!need_exit) {
        time_t current_time = time(NULL);
        if (current_time >= *params->next) {
            // get current_year
            std::tm* local_time = std::localtime(&current_time);
            int curr_year = local_time->tm_year + 1900;

            if (first_opened) {
                log_year = curr_year;
            } else if (curr_year != log_year) {
                params->file->close();
                params->file->open(LOG_FILE_NAME_DAY, std::ios::trunc | std::ios::out);
                if (!params->file->is_open()) {
                    perror("Error reopening log_file_day (w)");
                    exit(EXIT_FAILURE);
                }
                 params->file->close();
                 params->file->open(LOG_FILE_NAME_DAY, std::ios::app | std::ios::out);
                if (!params->file->is_open()) {
                    perror("Error reopening log_file_day (a+)");
                    exit(EXIT_FAILURE);
                }
                log_year = curr_year;
            }
            // change next
            SemaphoreWait(params->thr_sem);
            *params->next += SEC_IN_DAY;
            SemaphorePost(params->thr_sem);

            SemaphoreWait(params->thr_sem);
            double avg = *params->avg;
            SemaphorePost(params->thr_sem);

            // get record
             curr_time = get_time();
             record = curr_time + " " + std::to_string(avg);


            // get fixed-sized record
            std::string fixed_record;
            make_fixed_record(fixed_record, record);
           
            
            params->file->write(fixed_record.c_str(), fixed_record.size());
            first_opened = false;

            SemaphoreWait(params->thr_sem);
            *params->avg = 0.0;
            SemaphorePost(params->thr_sem);

            SemaphoreWait(params->thr_sem);
            *params->counter = 0;
            SemaphorePost(params->thr_sem);
        }
        sleep(1);
    }
    return 0;
}
#endif

int main(int argc, char *argv[])
{
    // signal SIGINT
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(sig_handler, TRUE)) { // используется для установки обработчиков событий, связанных с консолью, таких как запросы на завершение программы
        perror("Error setting handler");
        exit(EXIT_FAILURE);
    }
#else
    struct sigaction act; // структура sigaction используется для указания того, как должен быть обработан сигнал
    memset(&act, 0, sizeof(act)); // вначале она инициализируется нулями с помощью memset, чтобы избежать неопределенного поведения
    act.sa_handler = sig_handler; // поле sa_handler структуры sigaction указывается функция sig_handler, которая будет вызываться при получении сигнала
    sigset_t set;
    sigemptyset(&set); // создается пустой набор сигналов set с помощью функции sigemptyset()
    sigaddset(&set, SIGINT);
    // SIGINT - сигнал, который посылается процессу, когда пользователь нажимает комбинацию клавиш Ctrl+C в терминале или консоли
    act.sa_mask = set; // указывает, какие сигналы должны быть заблокированы в момент выполнения обработчика сигнала; блокировка сигнала в момент выполнения обработчика помогает избежать "рекурсивного" или нежелательного повторного выполнения того же обработчика
    if (sigaction(
        SIGINT, // идентификатор сигнала, который мы хотим обработать
        &act, // указатель на структуру типа struct sigaction, которая описывает, как должен быть обработан сигнал
        NULL // не сохранять старое действие, можно передать NULL
        ) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
#endif
    // stores last record position in log_file and log_file_hour
#ifdef _WIN32
    HANDLE tmp_file = CreateFile(
        FILE_LAST_RECORD // имя временного файла, в который записывается последняя обратная запись
        FILE_SHARE_READ, // разрешение на совместный доступ к файлу для чтения другими процессами
        NULL, // атрибуты безопасности (в данном случае не используются)
        OPEN_ALWAYS, // открывает файл, если он существует, или создает его, если он не существует
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // указывает обычные атрибуты файла и флаг для записи данных сразу на диск (без кэширования)
        NULL // шаблон файла
        );
    if (tmp_file == INVALID_HANDLE_VALUE) { // если файл не был открыт или создан успешно, то CreateFile возвращает INVALID_HANDLE_VALUE
        perror("CreateFile (tmp_file)");
        exit(EXIT_FAILURE);
    }
#else
    std::ifstream tmp_file(FILE_LAST_RECORD);
    if (!tmp_file.is_open()) {
        perror("fopen (tmp_file)");
        exit(EXIT_FAILURE);
    }
#endif
    // get last record position
    int last_record_pos[2] = {0, 0}; // создается массив из двух целых чисел, инициализированный нулями; этот массив будет использоваться для хранения двух значений, которые извлекаются из файла
    if (!is_file_empty(tmp_file)) {
#ifdef _WIN32
        read_last_records(tmp_file, &last_record_pos[0], &last_record_pos[1]);
#else
        tmp_file >> last_record_pos[0];
        tmp_file >> last_record_pos[1];
#endif
    }
    // open log files
#ifdef _WIN32
    HANDLE log_file = CreateFile(
        LOG_FILE_NAME, // log_file
        GENERIC_WRITE, // разрешение на запись в файл
        FILE_SHARE_READ, // разрешение на совместный доступ к файлу для чтения другими процессами
        NULL, // атрибуты безопасности (в данном случае не используются)
        OPEN_ALWAYS, // открывает файл, если он существует, или создает его, если он не существует
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // указывает обычные атрибуты файла и флаг для записи данных сразу на диск (без кэширования)
        NULL // шаблон файла
    );
    if (log_file == INVALID_HANDLE_VALUE) { // если файл не был открыт или создан успешно, то CreateFile возвращает INVALID_HANDLE_VALUE
        perror("CreateFile (log_file)");
        exit(EXIT_FAILURE);
    }

    HANDLE log_file_hour = CreateFile(
        LOG_FILE_NAME_HOUR, // имя файла для логирования данных за послений час
        GENERIC_WRITE, // разрешение на запись в файл
        FILE_SHARE_READ, // разрешение на совместный доступ к файлу для чтения другими процессами
        NULL, // атрибуты безопасности (в данном случае не используются)
        OPEN_ALWAYS, // открывает файл, если он существует, или создает его, если он не существует
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // указывает обычные атрибуты файла и флаг для записи данных сразу на диск (без кэширования)
        NULL // шаблон файла
    );
    if (log_file_hour == INVALID_HANDLE_VALUE) { // если файл не был открыт или создан успешно, то CreateFile возвращает INVALID_HANDLE_VALUE
        perror("CreateFile (log_file_hour)");
        exit(EXIT_FAILURE);
    }
    
    HANDLE log_file_day = CreateFile(
        LOG_FILE_NAME_DAY, // имя файла для логирования данных за день
        FILE_APPEND_DATA, // данные, записываемые в файл, будут автоматически добавляться в его конец, независимо от текущего указателя позиции записи
        FILE_SHARE_READ, // разрешение на совместный доступ к файлу для чтения другими процессами
        NULL, // атрибуты безопасности (в данном случае не используются)
        OPEN_ALWAYS, // открывает файл, если он существует, или создает его, если он не существует
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // указывает обычные атрибуты файла и флаг для записи данных сразу на диск (без кэширования)
        NULL // шаблон файла
    );
    if (log_file_day == INVALID_HANDLE_VALUE) { // если файл не был открыт или создан успешно, то CreateFile возвращает INVALID_HANDLE_VALUE
        perror("CreateFile (log_file_day)");
        exit(EXIT_FAILURE);
    }
#else
    std::ofstream log_file(LOG_FILE_NAME, std::ios::out | std::ios::app); // указывает, что файл будет открыт для записи + указывает, что данные будут добавляться в конец файла
    /*
    Комбинация std::ios::out | std::ios::app обеспечивает:
        Если файл существует, записи добавляются в его конец.
        Если файл не существует, он создается.
    */
    if (!log_file.is_open()) {
        perror("fopen (log_file)");
        exit(EXIT_FAILURE);
    }
    
    std::ofstream log_file_hour(LOG_FILE_NAME_HOUR, std::ios::out | std::ios::app); // указывает, что файл будет открыт для записи + указывает, что данные будут добавляться в конец файла
    if (!log_file_hour.is_open()) {
        perror("fopen (log_file_hour)");
        exit(EXIT_FAILURE);
    }
    
     std::ofstream log_file_day(LOG_FILE_NAME_DAY, std::ios::app); // указывает, что файл будет открыт исключительно для добавления данных
      if (!log_file_day.is_open()) {
        perror("fopen (log_file_day)");
        exit(EXIT_FAILURE);
    }
#endif

    // open or create new semaphore
#ifdef _WIN32
    HANDLE sem = CreateSemaphore( // создает объект семафора, который используется для синхронизации потоков или процессов
        NULL, // указывает, что объект семафора будет использоваться только внутри текущего процесса
        1, // начальное значение семафора (количество потоков, которые могут одновременно получить доступ к ресурсу)
        1, // аксимальное значение семафора (здесь также 1, что ограничивает доступ одним потоком)
        SEMAPHORE_OBJECT_NAME // имя объекта семафора
    );
#else
    sem_t *sem = sem_open(
        SEMAPHORE_OBJECT_NAME, // имя объекта семафора
        O_CREAT, // флаг, указывающий, что семафор должен быть создан, если он еще не существует
        0777, // права доступа для нового семафора (пользователь, группа и другие могут читать и писать)
        1 // начальное значение семафора
    );
#endif
    // configure port
#ifdef _WIN32
      HANDLE fd = CreateFile(
        PORT_RD, // LPCSTR (указатель на строку, содержащую имя файла или устройства
        GENERIC_READ, // предоставляет доступ на чтение
        0, // запрещает другим процессам открывать этот объект до его закрытия
        NULL, // атрибуты безопасности (в данном случае не используются)
        OPEN_EXISTING, // открывает существующий объект; если объекта нет, вызов завершится с ошибкой
        0, // используется поведение по умолчанию
        NULL // шаблон файла не используется
    );
    if (fd == INVALID_HANDLE_VALUE) { // если файл не был открыт или создан успешно, то CreateFile возвращает INVALID_HANDLE_VALUE
        perror("CreateFile (pd)");
        exit(EXIT_FAILURE);
    }

    if (!configure_port(fd, BAUDRATE_115200)) { // настраивает параметры последовательного порта
        // BAUDRATE_115200 - скорость передачи данных
        perror("configure_port (pd)");
        CloseHandle(fd);
        exit(EXIT_FAILURE);
    }
#else
    const char* port_rd = PORT_RD; // строка объявляет указатель port_rd, который будет указывать на строку (массив символов), представляющую путь к последовательному порту
    speed_t baud_rate = BAUDRATE_115200;
    configure_port(port_rd, baud_rate); // настраивает параметры последовательного порта
    // open port
    int fd = open( // открывает последовательный порт для чтения
        port_rd, // путь к порту,
        O_RDONLY | O_NOCTTY | O_NDELAY
        /*
        O_RDONLY - флаг, указывающий, что порт будет открыт для чтения
        O_NOCTTY - флаг, указывающий, что открытие порта не приведет к его назначению терминалом (не захватывать порт как управляющее устройство терминала)
        O_NDELAY - флаг, указывающий, что операции ввода-вывода с портом не должны блокировать выполнение программы, если данные не могут быть немедленно получены
        */
    );
    if (fd == -1) { // fd == -1, значит, открытие порта не удалось
        perror("open port");
        free_resources(&log_file, &log_file_hour, &log_file_day, &tmp_file, nullptr); // в данном случае, в функции free_resources() не нужно освобождать ресурсы, связанные с дескриптором файла, поэтому передается nullptr
        exit(EXIT_FAILURE);
    }
    // flush port
    if (tcflush(fd, TCIFLUSH) == -1) { // системный вызов в Linux, который используется для управления буфером ввода/вывода терминала или последовательного порта, -1 значает, что произошла ошибка при попытке сбросить данные из буфера
        /*
        fd — дескриптор файла, указывающий на открытый последовательный порт
        TCIFLUSH — это флаг, который указывает на очистку (сброс) буфера ввода (без буфера вывода)
        */
        perror("tcflush");
        free_resources(&log_file, &log_file_hour, &log_file_day, &tmp_file, &fd);
        exit(EXIT_FAILURE);
    }
#endif
    int count_hour = 0, count_day = 0;
    double avg_hour = 0.0, avg_day = 0.0;

    // set timer for log_hour and log_day
    time_t start_time = time(NULL); // возвращает общее количество секунд с начала эпохи UNIX
    time_t next_hour = start_time + SEC_IN_HOUR;
    time_t next_day = start_time + SEC_IN_DAY;

    // create new thread (hour logger)
#ifdef _WIN32
    struct thr_data params_hour = {
        reinterpret_cast<std::ofstream*>(&log_file_hour), // приведение типа в указатель на ofstream
        &next_hour, // время для следующей записи в секундах
        &avg_hour, // среднее значение
        &count_hour, // количество часов для вычисления среднего значения
        last_record_pos, // позиция последней записи
        sem // семафор
    };
    HANDLE thr_hour = CreateThread(
        NULL, // Указатель на атрибуты потока (по умолчанию)
        0, // Размер стека (по умолчанию)
        thr_routine_hour, // Функция, которая будет выполнена в новом потоке
        &params_hour, // Указатель на структуру данных, которая будет передана в поток
        0, // Параметры управления потоком (по умолчанию)
        NULL // Идентификатор потока (не используется)
    );
    if (thr_hour == NULL) { // если поток не был успешно создан
        perror("CreateThread (thr_hour)");
        exit(EXIT_FAILURE);
    }
#else
    struct thr_data params_hour = {&log_file_hour, &next_hour, &avg_hour, &count_hour, last_record_pos, sem};
    pthread_t thr_hour;
    int status = pthread_create(
        &thr_hour // Указатель на идентификатор потока
        NULL, // Атрибуты потока (по умолчанию)
        thr_routine_hour, // Функция, которая будет выполнена в новом потоке
        &params_hour // Аргумент, передаваемый в функцию потока
    );
    if (status != 0) {
        perror("pthread_create (thr_hour)");
        free_resources(&log_file, &log_file_hour, &log_file_day, &tmp_file, &fd);
        exit(EXIT_FAILURE);
    }
#endif
    // create new thread (day logger)
#ifdef _WIN32
    struct thr_data params_day = {reinterpret_cast<std::ofstream*>(&log_file_day), &next_day, &avg_day, &count_day, NULL, sem};
    HANDLE thr_day = CreateThread(
        NULL,
        0,
        thr_routine_day,
        &params_day,
        0,
        NULL
    );
    if (thr_day == NULL) {
        perror("CreateThread (thr_day)");
        exit(EXIT_FAILURE);
    }
#else
    struct thr_data params_day = {&log_file_day, &next_day, &avg_day, &count_day, nullptr, sem};
    pthread_t thr_day;
     status = pthread_create(&thr_day, NULL, thr_routine_day, &params_day);
    if (status != 0) {
        perror("pthread_create (thr_day)");
        free_resources(&log_file, &log_file_hour, &log_file_day, &tmp_file, &fd);
        exit(EXIT_FAILURE);
    }
#endif
    std::string curr_time;
    std::vector<char> buffer(255);
    std::string record;
    double cur_temp;
    bool first_opened = true;
#ifdef _WIN32
    while (!need_exit) {
        DWORD bytesRead;
        if (ReadFile(
            fd, // дескриптор файла или устройства, из которого нужно читать (по серийному порту)
            buffer.data(), // указатель на буфер, в который будут записаны данные, считанные из файла
            buffer.size(), // размер буфера, в котором будет храниться прочитанная информация.
            &bytesRead, // указатель на переменную типа DWORD, в которую будет записано количество байт, фактически считанных из файла
            NULL // NULL означает, что операция чтения будет синхронной (ждать завершения операции до получения результата)
        )) {
           if (bytesRead > 0) {
               // get record
                curr_time = get_time();
                record = curr_time + " " + std::string(buffer.data(), bytesRead);
                /*
                Указатель на данные (buffer.data()), который указывает на начало строки
                Количество символов (или байтов) для использования в строке (bytesRead), определяя, сколько данных будет считано для формирования строки
                */

                // get fixed-sized record
                std::string fixed_record;
                make_fixed_record(fixed_record, record);

                write_log(
                    log_file, // объект или дескриптор файла, куда нужно записать информацию
                    fixed_record.c_str(), // строка, которую нужно записать в файл
                    fixed_record.size(), // размер строки, которую нужно записать
                    first_opened, // флаг, который указывает, был ли файл уже открыт
                    last_record_pos, // переменная хранит информацию о позиции последней записи в журнале
                    SEC_IN_DAY * 1000 / PORT_SPEED_MS, // записис обновляются раз в заданные ms
                    0, // index выполняет роль индекса для массива last_record_pos, который хранит позиции записи в файле для различных индексов
                    sem // семафор
                );
                first_opened = false;

                // find avg_hour
                cur_temp = atof(buffer.data()); // преобразует строку, содержащуюся в буфере buffer, в число с плавающей запятой (температура)
                count_hour++;
                avg_hour += (cur_temp - avg_hour) / count_hour; // средняя температура за час

                // find avg_day
                cur_temp = atof(buffer.data());
                count_day++;
                avg_day += (cur_temp - avg_day) / count_day; // средняя температура за день
            }
        } else {
            perror("ReadFile (pd)");
            break;
        }
    }
#else
    while (!need_exit) {
        ssize_t bytesRead = read(fd, buffer.data(), buffer.size());
        if (bytesRead > 0) {
             // get record
            curr_time = get_time();
            record = curr_time + " " + std::string(buffer.data(), bytesRead);
           
            // get fixed-sized record
            std::string fixed_record;
            make_fixed_record(fixed_record, record);
            
            write_log(log_file, fixed_record, first_opened, last_record_pos, SEC_IN_DAY * 1000 / PORT_SPEED_MS, 0, sem);
            first_opened = false;
           
            // find avg_hour
             cur_temp = atof(buffer.data());
            count_hour++;
            avg_hour += (cur_temp - avg_hour) / count_hour;

            // find avg_day
              cur_temp = atof(buffer.data());
            count_day++;
            avg_day += (cur_temp - avg_day) / count_day;
        }
    }
#endif
#ifdef _WIN32
    WaitForSingleObject(thr_hour, INFINITE); // WaitForSingleObject приостанавливает выполнение текущего потока (то есть основного потока программы), пока не завершится указанный поток thr_hour
    CloseHandle(thr_hour); // после того как поток thr_hour завершил выполнение, необходимо закрыть его
    WaitForSingleObject(thr_day, INFINITE);
    CloseHandle(thr_day);
#else
    pthread_join(thr_hour, NULL); // ункция pthread_join блокирует основной поток до тех пор, пока не завершится поток thr_hour
    // NULL - указатель на переменную, в которой будет храниться возвращаемое значение потока, если это необходимо
    pthread_join(thr_day, NULL);
#endif
    // save last record position
#ifdef _WIN32
    SetFilePointer( // перемещает указатель на позицию в файле, с которой будет производиться чтение или запись
        tmp_file, // дескриптор файла
        0, // позиция в файле, куда нужно переместить указатель (начало файла)
        NULL, FILE_BEGIN // говорят о том, что смещение будет относительно начала файла
    );
    std::string tmp_buffer = std::to_string(last_record_pos[0]) + "\n" + std::to_string(last_record_pos[1]) + "\n";
    WriteFile(
        tmp_file, // Дескриптор файла
        tmp_buffer.c_str(), // Указатель на буфер с данными для записи
        tmp_buffer.size(), // Количество байт для записи
        NULL, // Количество реально записанных байт
        NULL // Указатель на структуру для асинхронной записи
    );
#else
    tmp_file.seekg(0, std::ios::beg); // перемещает указатель чтения в начало файла
    /*
    stream.seekg(offset, direction);
        offset — смещение, на которое нужно переместить указатель.
        direction — указывает откуда отсчитывать смещение.
    */
    tmp_file << last_record_pos[0] << "\n";
    tmp_file << last_record_pos[1] << "\n";
#endif
#ifdef _WIN32
    CloseHandle(tmp_file);
    CloseHandle(log_file);
    CloseHandle(log_file_hour);
    CloseHandle(log_file_day);
    CloseHandle(sem);
    CloseHandle(fd);
#else
    free_resources(&log_file, &log_file_hour, &log_file_day, &tmp_file, &fd);
#endif
    return 0;
}