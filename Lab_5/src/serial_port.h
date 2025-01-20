#pragma once

#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <termios.h>
#    include <unistd.h>
#endif

constexpr int PORT_SPEED_MS = 1000;

#ifdef _WIN32
    enum class BaudRate { // Port speeds
        BAUDRATE_4800   = CBR_4800,
        BBAUDRATE_9600  = CBR_9600,
        BAUDRATE_19200  = CBR_19200,
        BBAUDRATE_38400 = CBR_38400,
        BAUDRATE_57600  = CBR_57600,
        BAUDRATE_115200 = CBR_115200
    };

    // Configure the DCB structure
    inline bool configure_dcb(DCB& dcb, BaudRate baud_rate) {
        dcb.DCBlength    = sizeof(dcb);
        dcb.BaudRate     = static_cast<DWORD>(baud_rate);
        dcb.ByteSize     = 8;
        dcb.StopBits     = ONESTOPBIT;
        dcb.Parity       = NOPARITY;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl  = DTR_CONTROL_ENABLE;
        dcb.fRtsControl  = RTS_CONTROL_ENABLE;
        return true;
    }
    
    // Configure serial port parameters
    bool configure_port(HANDLE hSerial, BaudRate baud_rate) {
        DCB dcbSerialParameters = {0};
        dcbSerialParameters.DCBlength = sizeof(dcbSerialParameters);

        if (!GetCommState(hSerial, &dcbSerialParameters)) {
            std::cerr << "Error: GetCommState failed." << std::endl;
            return false;
        }

        if (!configure_dcb(dcbSerialParameters, baud_rate))
        return false;

        if (!SetCommState(hSerial, &dcbSerialParameters)) {
            std::cerr << "Error: SetCommState failed." << std::endl;
            return false;
        }
        return true;
    }

    // Close the port file descriptor
    void close_port(HANDLE hSerial) {
        if (hSerial != INVALID_HANDLE_VALUE) {
            CloseHandle(hSerial);
        }
    }

    // Open and configure the serial port
    HANDLE open_and_configure_port(const char* port_name, BaudRate baud_rate) {
        HANDLE hSerial = CreateFileA(port_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hSerial == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("CreateFile (hSerial) failed.");
        }
        if (!configure_port(hSerial, baud_rate)) {
            CloseHandle(hSerial);
            throw std::runtime_error("configure_port (hSerial) failed.");
        }
        return hSerial;
    }

#else
    // Port speeds
    enum class BaudRate {
        BAUDRATE_4800   = B4800,
        BBAUDRATE_9600  = B9600,
        BAUDRATE_19200  = B19200,
        BBAUDRATE_38400 = B38400,
        BAUDRATE_57600  = B57600,
        BAUDRATE_115200 = B115200
    };

    // Configure the DCB structure
    inline void configure_termios(termios& options, speed_t baud_rate) {
        cfsetispeed(&options, baud_rate);
        cfsetospeed(&options, baud_rate);

        options.c_cflag |= (CLOCAL | CREAD);
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~CSTOPB;
        options.c_cflag &= ~CRTSCTS;
    }

    // Configure serial port parameters
    void configure_port(const std::string& port, BaudRate baud_rate) {
        int fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd == -1) {
            throw std::runtime_error("Error: Unable to open port " + port);
        }

        struct termios options;
        if (tcgetattr(fd, &options) < 0) {
            close(fd);
            throw std::runtime_error("Error: tcgetattr failed for port " + port);
        }

        configure_termios(options, static_cast<speed_t>(baud_rate));

        if (tcsetattr(fd, TCSANOW, &options) < 0) {
            close(fd);
            throw std::runtime_error("Error: tcsetattr failed for port " + port);
        }

        close(fd);
    }

    // Close the port file descriptor
    void close_port(int fd) {
        if (fd != -1) {
            close(fd);
        }
    }

    // Open and configure the serial port
    int open_and_configure_port(const char* port_name, BaudRate baud_rate) {
        int fd = open(port_name, O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd == -1) {
            throw std::runtime_error("open port failed.");
        }
        configure_port(port_name, baud_rate);
        return fd;
    }
#endif