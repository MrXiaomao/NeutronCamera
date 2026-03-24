#include "netsettingwindow.h"
#include "ui_netsettingwindow.h"
#include "globalsettings.h"

NetSettingWindow::NetSettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::NetSettingWindow)
{
    ui->setupUi(this);

    GlobalSettings settings(DEVICE_CONFIG_FILE);
    ui->lineEdit_ipRemote->setText(settings.value("Net/ipRemote", "192.168.1.212").toString());
    ui->spinBox_portRemote->setValue(settings.value("Net/portRemote", 8000).toUInt());
    ui->spinBox_portLocal->setValue(settings.value("Net/portLocal", 1000).toUInt());
    ui->spinBox_broadcastPort->setValue(settings.value("Net/broadcastPort", 12100).toUInt());
}

NetSettingWindow::~NetSettingWindow()
{
    delete ui;
}

void NetSettingWindow::on_pushButton_save_clicked()
{
    GlobalSettings settings(DEVICE_CONFIG_FILE);
    settings.setValue("Net/ipRemote", ui->lineEdit_ipRemote->text());
    settings.setValue("Net/portRemote", ui->spinBox_portRemote->value());
    settings.setValue("Net/portLocal", ui->spinBox_portLocal->value());
    settings.setValue("Net/broadcastPort", ui->spinBox_broadcastPort->value());
    this->close();
}


void NetSettingWindow::on_pushButton_cancel_clicked()
{
    this->close();
}

