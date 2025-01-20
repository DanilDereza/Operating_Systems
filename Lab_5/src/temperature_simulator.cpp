#include <iostream>
#include <string>
#include <random>
#include <chrono>
#include <stdexcept>
#include <sstream>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <termios.h>
    #include <unistd.h>
    #include <pthread.h>
#endif

#include "serial_port.h"

#ifdef _WIN32
    constexpr const char* PORT_WR = "COM1";
#else
    constexpr const char* PORT_WR = "/dev/pts/1";
#endif

constexpr const int LOW_TEMP = 5, HIGH_TEMP = 25;
constexpr const double LOW_CHANGE = -0.3, HIGH_CHANGE = 0.3;


double init_rand_temp(int min, int max); // Function to generate a random number in the range [min, max]
double rand_temp_change(double min, double max); // Function to generate a random temperature change in the range [min, max]
std::string format_temperature(double temp); // Function for formatting a floating-point number to one decimal place


#ifdef _WIN32
    DWORD WINAPI thread_function(LPVOID param); // Thread function (Windows)
#else
    void* thread_function(void* param); // Thread function (Linux)
#endif


int main() {
#ifdef _WIN32
    HANDLE hSerial = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
    pthread_t thread;
#endif

    try {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));

#ifdef _WIN32
        hSerial = open_and_configure_port(PORT_WR, BaudRate::BAUDRATE_115200);

        HANDLE hThread = CreateThread(NULL, 0, thread_function, hSerial, 0, NULL);

        if (hThread == NULL) {
            throw std::runtime_error("Failed to create thread.");
        }

        WaitForSingleObject(hThread, INFINITE);

#else
        fd = open_and_configure_port(PORT_WR, BaudRate::BAUDRATE_115200);

        if (pthread_create(&thread, NULL, thread_function, &fd) != 0) {
            throw std::runtime_error("Failed to create thread.");
        }

        pthread_join(thread, NULL);
#endif
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

#ifdef _WIN32
    close_port(hSerial);
#else
    close_port(fd);
#endif

    return 0;
}


double init_rand_temp(int min, int max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> distrib(min, max);
    return distrib(gen);
}

double rand_temp_change(double min, double max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> distrib(min, max);
    return distrib(gen);
}

std::string format_temperature(double temp) {
    std::ostringstream oss;
    oss.precision(1);
    oss << std::fixed << temp;
    return oss.str();
}

#ifdef _WIN32
    DWORD WINAPI thread_function(LPVOID param) {
        HANDLE hSerial = static_cast<HANDLE>(param);
        double temp = init_rand_temp(LOW_TEMP, HIGH_TEMP);
        std::string data = format_temperature(temp);

        while (true) {
            DWORD bytes_written;
            if (!WriteFile(hSerial, data.c_str(), static_cast<DWORD>(data.size()), &bytes_written, NULL)) {
                std::cerr << "WriteFile failed." << std::endl;
                return 1;
            }

            temp += rand_temp_change(LOW_CHANGE, HIGH_CHANGE);
            data = format_temperature(temp);
            Sleep(PORT_SPEED_MS);
        }

        return 0;
    }
#else
    void* thread_function(void* param) {
        int fd = *static_cast<int*>(param);
        double temp = init_rand_temp(LOW_TEMP, HIGH_TEMP);
        std::string data = format_temperature(temp);

        while (true) {
            if (write(fd, data.c_str(), data.size()) == -1) {
                std::cerr << "write failed." << std::endl;
                return nullptr;
            }

            temp += rand_temp_change(LOW_CHANGE, HIGH_CHANGE);
            data = format_temperature(temp);
            usleep(PORT_SPEED_MS * 1000);
        }

        return nullptr;
    }
#endif