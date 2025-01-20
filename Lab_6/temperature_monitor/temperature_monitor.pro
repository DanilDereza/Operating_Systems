QT += widgets
QT += network
QT += charts
QT += core gui

CONFIG += c++17

unix: {
INCLUDEPATH += .
INCLUDEPATH += /usr/include/qwt
}

win32: {
    INCLUDEPATH += $$PWD/include
    LIBS += -L$$PWD/lib -lqwt
}

LIBS += -L/usr/lib -lqwt

SOURCES += main.cpp \
    mainwindow.cpp \
    chart.cpp

HEADERS += mainwindow.h \
    chart.h

# install
TARGET = temperature_monitor
TEMPLATE = app
