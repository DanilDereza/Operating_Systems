
#include "mainwindow.h"
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDebug>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QTextBrowser>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QJsonParseError>

MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent)
{
    // Инициализация менеджера сети
    networkManager = new QNetworkAccessManager(this);

    // Основной виджет для окна
    QWidget *mainWidget = new QWidget(this);
    setCentralWidget(mainWidget);

    // Кнопки
    currentButton = new QPushButton("Current", this);
    hourlyButton = new QPushButton("Hourly", this);
    dailyButton = new QPushButton("Daily", this);

    // Сетка для расположения элементов
    QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(currentButton);
    buttonLayout->addWidget(hourlyButton);
    buttonLayout->addWidget(dailyButton);

    mainLayout->addLayout(buttonLayout);

    // QTextBrowser для отображения HTML
    //htmlViewer = new QTextBrowser(this);
    //mainLayout->addWidget(htmlViewer);


    // table
    dataTable = new QTableWidget(this);
    mainLayout->addWidget(dataTable);


    // Инициализация для отображения графика
    chartView = new QChartView(this);
    chartView->setRenderHint(QPainter::Antialiasing);

    // Добавление chartView в layout для отображения
    mainLayout->addWidget(chartView);


     // Установка стилей и подключение файлов
     QFile styleFile("style.css");
     if (styleFile.exists() && styleFile.open(QFile::ReadOnly | QFile::Text)) {
         QTextStream styleStream(&styleFile);
         QString styleSheet = styleStream.readAll();
         setStyleSheet(styleSheet);
         styleFile.close();
     } else {
         qDebug() << "Failed to load stylesheet.";
     }

     // Загружаем корневую HTML страницу при запуске
     fetchData("/");


     connect(currentButton, &QPushButton::clicked, this, &MainWindow::onCurrentButtonClicked);
      connect(hourlyButton, &QPushButton::clicked, this, &MainWindow::onHourlyButtonClicked);
      connect(dailyButton, &QPushButton::clicked, this, &MainWindow::onDailyButtonClicked);
}


MainWindow::~MainWindow()
{
    // Очистка ресурсов
}

void MainWindow::onCurrentButtonClicked()
{
    fetchStatsData("/current");
}

void MainWindow::onHourlyButtonClicked()
{
    fetchStatsData("/stats/hourly");
}

void MainWindow::onDailyButtonClicked()
{
    fetchStatsData("/stats/daily");
}


void MainWindow::fetchData(const QString& endpoint)
{
    QString url = makeQueryUrl(endpoint);
    QNetworkRequest request(url);
    QNetworkReply* reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, [this, reply, endpoint]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();
            qDebug() << "Raw response:" << response;


             // Преобразуем полученные данные в JSON
             QJsonDocument doc = QJsonDocument::fromJson(response);
              QJsonArray jsonData;
             if (doc.isArray()) {
                jsonData = doc.array();
             } else if(doc.isObject()) {
               QJsonObject obj = doc.object();
               jsonData = QJsonArray({obj}); // Упаковываем объект в массив
              }

            // Обрабатываем полученные данные
           if (endpoint == "/") {
              displayHtmlContent(QString::fromUtf8(response));
            }

        } else {
            qDebug() << "Network error:" << reply->errorString();
            QMessageBox::critical(this, "Network Error", "Failed to fetch data from server.");
        }
        reply->deleteLater();
    });
}

void MainWindow::fetchStatsData(const QString& endpoint)
{
    QDateTime now = QDateTime::currentDateTime();
    QDateTime startTime = now;

    if(endpoint == "/stats/daily") {
         startTime = now.addDays(-365);
    } else if (endpoint == "/stats/hourly") {
         startTime = now.addMonths(-1);
    } else {
        startTime = now.addDays(-1);
    }

    QString formattedStartTime = startTime.toString(Qt::ISODate).replace("T", " ").mid(0,19);
    QString formattedEndTime = now.toString(Qt::ISODate).replace("T", " ").mid(0,19);

   QString url = makeQueryUrl("/stats?start=" + formattedStartTime + "&end=" + formattedEndTime);

   QNetworkRequest request(url);
   QNetworkReply* reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, [this, reply, endpoint]() {
        if (reply->error() == QNetworkReply::NoError) {
             QByteArray response = reply->readAll();
             qDebug() << "Raw response:" << response;
             QJsonParseError jsonError;
             QJsonDocument jsonDoc = QJsonDocument::fromJson(response, &jsonError);

             if (jsonError.error != QJsonParseError::NoError) {
                qDebug() << "JSON Parse error: " << jsonError.errorString();
                return;
              }

             QJsonArray jsonData;
              if (jsonDoc.isArray()) {
                   jsonData = jsonDoc.array();
              } else if (jsonDoc.isObject()) {
                QJsonObject obj = jsonDoc.object();
                jsonData = QJsonArray({obj});
              } else {
                   qDebug() << "Not array or object";
                    return;
              }
            processData(jsonData, endpoint);
        } else {
            qDebug() << "Network error:" << reply->errorString();
            QMessageBox::critical(this, "Network Error", "Failed to fetch data from server.");
        }
        reply->deleteLater();
    });

}


void MainWindow::displayHtmlContent(const QString& htmlContent)
{
  if(htmlViewer)
  {
        htmlViewer->setHtml(htmlContent);
  }
}

QString MainWindow::makeQueryUrl(const QString& endpoint)
{
    return "http://127.0.0.1:8080" + endpoint;
}

void MainWindow::processData(const QJsonArray& data, const QString& endpoint)
{
    qDebug() << "Processing data for endpoint:" << endpoint;

     QJsonArray processedData;

    if (endpoint == "/current") {
         if(data.isEmpty()){
             qDebug() << "No data in current endpoint.";
              displayChart({}, "Current Temperature");
              displayDataInTable(QJsonArray());
              return;
         }
           processedData = data;
            displayDataInTable(processedData);
            displayChart(convertDataToPoints(processedData), "Current Temperature");

    } else if(endpoint == "/stats/hourly") {
         processedData = processHourlyData(data);
         displayDataInTable(processedData);
         displayChart(convertDataToPoints(processedData), endpoint);

   } else if (endpoint == "/stats/daily") {
         processedData = processDailyData(data);
         displayDataInTable(processedData);
        displayChart(convertDataToPoints(processedData), endpoint);
    }
}

QJsonArray MainWindow::processHourlyData(const QJsonArray& data)
{
    QJsonObject groupedData;

    for (const auto& itemRef : data)
    {
        QJsonObject item = itemRef.toObject();
        if (!item.contains("timestamp") || !item.contains("temperature"))
        {
           qDebug() << "Invalid item format";
            continue;
        }

       QString timestamp = item["timestamp"].toString();
        QString hour = timestamp.mid(0,13); // YYYY-MM-DD HH
         if (!groupedData.contains(hour)) {
            QJsonObject hourObject;
            hourObject["sum"] = 0.0;
           hourObject["count"] = 0.0;
          groupedData[hour] = hourObject;
       }

    QJsonObject hourObject = groupedData[hour].toObject();
    hourObject["sum"] = hourObject["sum"].toDouble() +  item["temperature"].toString().toDouble();
   hourObject["count"] = hourObject["count"].toDouble() + 1;
    groupedData[hour] = hourObject;
 }

 QJsonArray result;
 for (auto it = groupedData.begin(); it != groupedData.end(); ++it) {
    QJsonObject hourObject = it.value().toObject();
    QJsonObject resultItem;
    resultItem["timestamp"] = it.key() + ":00";
    resultItem["temperature"] = hourObject["sum"].toDouble() / hourObject["count"].toDouble();
      result.append(resultItem);
  }
  if(result.size() > 30) {
         QJsonArray subArray;
       for(int i = result.size() - 30; i < result.size(); ++i) {
          subArray.append(result[i]);
         }
      return subArray;
   } else {
      return result;
  }

}


QJsonArray MainWindow::processDailyData(const QJsonArray& data)
{
    QJsonObject groupedData;

     for (const auto& itemRef : data) {
        QJsonObject item = itemRef.toObject();
        if (!item.contains("timestamp") || !item.contains("temperature"))
        {
            qDebug() << "Invalid item format";
            continue;
        }

        QString day = item["timestamp"].toString().mid(0,10); // YYYY-MM-DD

        if (!groupedData.contains(day)) {
             QJsonObject dayObject;
            dayObject["sum"] = 0.0;
            dayObject["count"] = 0.0;
           groupedData[day] = dayObject;
       }

     QJsonObject dayObject = groupedData[day].toObject();
     dayObject["sum"] = dayObject["sum"].toDouble() + item["temperature"].toString().toDouble();
    dayObject["count"] = dayObject["count"].toDouble() + 1;
     groupedData[day] = dayObject;
   }

 QJsonArray result;
 for (auto it = groupedData.begin(); it != groupedData.end(); ++it)
 {
   QJsonObject dayObject = it.value().toObject();
   QJsonObject resultItem;
   resultItem["timestamp"] = it.key();
   resultItem["temperature"] = dayObject["sum"].toDouble() / dayObject["count"].toDouble();
    result.append(resultItem);
 }

     if(result.size() > 366) {
         QJsonArray subArray;
           for(int i = result.size() - 366; i < result.size(); ++i) {
              subArray.append(result[i]);
            }
        return subArray;
      } else {
      return result;
  }
}


QVector<QPointF> MainWindow::convertDataToPoints(const QJsonArray& data)
{
    QVector<QPointF> points;
    for(const auto& itemRef: data){
          QJsonObject item = itemRef.toObject();

          if(!item.contains("timestamp") || !item.contains("temperature"))
           {
                qDebug() << "Invalid item";
                continue;
           }
        QDateTime time = QDateTime::fromString(item["timestamp"].toString(), "yyyy-MM-dd HH:mm:ss");
        if(!time.isValid())
        {
          time = QDateTime::fromString(item["timestamp"].toString(), "yyyy-MM-dd HH:mm:ss.zzz");
            if(!time.isValid())
             {
                 qDebug() << "Invalid time format";
                 continue;
             }
        }

      double temperature = item["temperature"].toString().toDouble();

       points.append(QPointF(time.toMSecsSinceEpoch(), temperature));
    }
    return points;
}


void MainWindow::displayDataInTable(const QJsonArray& data)
{
  if(!dataTable)
  {
    qDebug() << "Error: No table widget.";
    return;
  }
    if(data.isEmpty()) {
        qDebug() << "No data to display in table";
        dataTable->clear();
         dataTable->setRowCount(1);
        dataTable->setColumnCount(1);
        QTableWidgetItem *item = new QTableWidgetItem("No data available.");
        dataTable->setItem(0, 0, item);
        return;
    }


    dataTable->clear();
    dataTable->setRowCount(data.size());
    dataTable->setColumnCount(2);
    dataTable->setHorizontalHeaderLabels({"Time", "Temperature"});
    for (int i = 0; i < data.size(); ++i)
    {
        QJsonObject obj = data[i].toObject();
        if(obj.contains("timestamp") && obj.contains("temperature"))
        {
              QTableWidgetItem* item1 = new QTableWidgetItem(obj["timestamp"].toString());
            QTableWidgetItem* item2 = new QTableWidgetItem(QString::number(obj["temperature"].toString().toDouble(), 'f', 2));
              dataTable->setItem(i, 0, item1);
             dataTable->setItem(i, 1, item2);
         }
         else
            {
                qDebug() << "Incorrect format";
                dataTable->setItem(i, 0, new QTableWidgetItem("Invalid data"));
                 dataTable->setItem(i, 1, new QTableWidgetItem("Invalid data"));
            }
    }
}

void MainWindow::displayChart(const QVector<QPointF>& points, const QString& endpoint)
{
    qDebug() << "Displaying chart for endpoint:" << endpoint;
    if (points.isEmpty())
    {
        qDebug() << "No data to display for chart: " << endpoint;
        if(chartView && chartView->chart()){
          chartView->chart()->removeAllSeries();
         }
        return;
    }

       QVector<QPointF> limitedPoints;
        int numPoints = points.size();
        if(numPoints > 60) {
              for (int i = numPoints - 60; i < numPoints; ++i) {
                limitedPoints.append(points[i]);
              }
        } else {
            limitedPoints = points;
        }

    // Создаем новую серию
    QLineSeries *series = new QLineSeries();
      for (const QPointF &point : limitedPoints) {
         series->append(point);
     }


    // Получаем старый график
       QChart *oldChart = chartView->chart();


    // Создаём новый график
    QChart *chart = new QChart();
        chart->addSeries(series);
    chart->setTitle("Temperature Chart for " + endpoint);

     QDateTimeAxis *axisX = new QDateTimeAxis;
    axisX->setFormat("hh:mm:ss");
    axisX->setTitleText("Time");

    QValueAxis *axisY = new QValueAxis;
    axisY->setTitleText("Temperature (°C)");


    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);

    // Устанавливаем новый график
    chartView->setChart(chart);


   // Удаляем старый график, если он был
    if (oldChart) {
        oldChart->deleteLater();
    }

    qDebug() << "Chart displayed with " << limitedPoints.size() << " points";
}
