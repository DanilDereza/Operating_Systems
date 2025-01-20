#include "chart.h"
#include <QDebug>

#include <QwtPlotLayout>
#include <QwtPlotCanvas>
#include <QwtPlotRenderer>
#include <QwtText>
#include <QwtPlotGrid>
#include <QwtPlotMarker>
#include <QwtSymbol>
#include <QwtTextLabel>
#include <QwtLegend>

Chart::Chart(QWidget *parent)
: QWidget(parent)
{
    plot = new QwtPlot(this);
    curve = new QwtPlotCurve("Temperature");

    // Настройка сетки
    QwtPlotGrid *grid = new QwtPlotGrid();
    grid->attach(plot);

    plot->setAxisTitle(QwtPlot::xBottom, "Time");
    plot->setAxisTitle(QwtPlot::yLeft, "Temperature");
    plot->setCanvasBackground(QColor(Qt::white));

    // Настройка заголовка
    QwtText title("Temperature Chart");
    plot->setTitle(title);
}

void Chart::setData(const QVector<double> &xData, const QVector<double> &yData)
{
    // Привязка данных к кривой
    curve->detach();  // Отключаем текущую кривую
    curve->setSamples(xData.data(), yData.data(), xData.size());  // Устанавливаем новые данные
    curve->attach(plot);  // Привязываем кривую к графику
    plot->replot();  // Перерисовываем график
}

void Chart::setChartTitle(const QString &title){
    QwtText qwtTitle(title);
    plot->setTitle(qwtTitle);  // Устанавливаем новый заголовок графика
}

Chart::~Chart()
{
    delete plot;  // Освобождаем ресурсы
    delete curve;  // Освобождаем ресурсы
}
