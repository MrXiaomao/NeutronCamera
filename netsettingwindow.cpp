#include "netsettingwindow.h"
#include "ui_netsettingwindow.h"
#include "globalsettings.h"
#include <QSerialPortInfo>

NetSettingWindow::NetSettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::NetSettingWindow)
{
    ui->setupUi(this);

    GlobalSettings settings(CONFIG_FILENAME);
    ui->lineEdit_ip->setText(settings.value("Net/ip", "192.168.1.100").toString());
    ui->spinBox_port->setValue(settings.value("Net/port", 6000).toUInt());
    ui->spinBox_broadcastPort->setValue(settings.value("Net/broadcastPort", 12100).toUInt());

    QStringList m_serialPortName;
    foreach(const QSerialPortInfo &info,QSerialPortInfo::availablePorts())
    {
        ui->comboBox_serialPortName->addItem(info.portName());
    }
}

NetSettingWindow::~NetSettingWindow()
{
    delete ui;
}

void NetSettingWindow::on_pushButton_save_clicked()
{
    GlobalSettings settings(CONFIG_FILENAME);
    settings.setValue("Net/ip", ui->lineEdit_ip->text());
    settings.setValue("Net/port", ui->spinBox_port->value());
    settings.setValue("Net/broadcastPort", ui->spinBox_broadcastPort->value());
    this->close();
}


void NetSettingWindow::on_pushButton_cancel_clicked()
{
    this->close();
}

