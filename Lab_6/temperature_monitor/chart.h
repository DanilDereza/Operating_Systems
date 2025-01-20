#ifndef CHART_H
#define CHART_H

#include <QWidget>
#include <QwtPlot>
#include <QwtPlotCurve>
#include <QVector>
#include <QColor>

class Chart : public QWidget
{
    Q_OBJECT

public:
    Chart(QWidget *parent = nullptr);
    void setData(const QVector<double>& xData, const QVector<double>& yData);
    void setChartTitle(const QString& title);
    ~Chart();
private:
    QwtPlot *plot;
    QwtPlotCurve *curve;
};

#endif // CHART_H