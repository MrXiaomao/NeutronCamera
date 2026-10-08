#ifndef PDUCONTROLWIDGET_H
#define PDUCONTROLWIDGET_H

#include <QWidget>

namespace Ui {
class PDUControlWidget;
}

class QSnmpClient;
class PDUControlWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDUControlWidget(QWidget *parent = nullptr);
    ~PDUControlWidget();

private:
    Ui::PDUControlWidget *ui;    
    QSnmpClient* mSnmpClient = nullptr;
};

#endif // PDUCONTROLWIDGET_H
