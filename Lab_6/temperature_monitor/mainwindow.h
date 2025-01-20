#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QTableWidget>
#include <QPushButton>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextBrowser>
#include <QVector>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

using namespace QtCharts;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onCurrentButtonClicked();
    void onHourlyButtonClicked();
    void onDailyButtonClicked();
    void processData(const QJsonArray& data, const QString& endpoint);
    void fetchData(const QString& endpoint);
    void fetchStatsData(const QString& endpoint);

private:
    QTimer *timer;
    QNetworkAccessManager *networkManager;

    QTableWidget *dataTable;
    QChartView *chartView;
    QPushButton *currentButton;
    QPushButton *hourlyButton;
    QPushButton *dailyButton;
    QTextBrowser *htmlViewer;

    QChart *chart;  // График

    void displayDataInTable(const QJsonArray& data);
    QJsonArray processHourlyData(const QJsonArray& data);
    QJsonArray processDailyData(const QJsonArray& data);
    QVector<QPointF> convertDataToPoints(const QJsonArray& data);
    void setupChart();
    void clearTable();
    void displayChart(const QVector<QPointF>& points, const QString& endpoint);
    QString getCurrentTimeForDisplay();
    QString formatTime(const QDateTime& dateTime);
    QString formatTimeFromISO(const QString& isoTime);
    QString makeQueryUrl(const QString& endpoint);
    QString getFormattedTime(const QDateTime& time);

    void displayHtmlContent(const QString& htmlContent);
};

#endif // MAINWINDOW_H
