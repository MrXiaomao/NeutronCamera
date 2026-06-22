#include "devicemanagerwindow.h"
#include "ui_devicemanagerwindow.h"
#include "AppConfig.h"
#include "pciecommsdk.h"

DeviceManagerWindow::DeviceManagerWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DeviceManagerWindow)
{
    ui->setupUi(this);
}

DeviceManagerWindow::~DeviceManagerWindow()
{
    delete ui;
}

void DeviceManagerWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    // 在这里添加你的界面参数刷新逻辑
    updataUi();
}

void DeviceManagerWindow::updataUi()
{
    ui->checkBox_board->setEnabled(PCIeCommSdk::boardIsEnable(1));
    ui->checkBox_board->setChecked(ui->checkBox_board->isEnabled() ? (AppConfig::instance().enableCapture(1, true) || AppConfig::instance().enableCapture(1, false)) : false);
    AppConfig::instance().setBoardState(1, ui->checkBox_board->isEnabled());

    ui->checkBox_board_2->setEnabled(PCIeCommSdk::boardIsEnable(2));
    ui->checkBox_board_2->setChecked(ui->checkBox_board_2->isEnabled() ? (AppConfig::instance().enableCapture(2, true) || AppConfig::instance().enableCapture(2, false)) : false);
    AppConfig::instance().setBoardState(2, ui->checkBox_board_2->isEnabled());

    ui->checkBox_board_3->setEnabled(PCIeCommSdk::boardIsEnable(3));
    ui->checkBox_board_3->setChecked(ui->checkBox_board_3->isEnabled() ? (AppConfig::instance().enableCapture(3, true) || AppConfig::instance().enableCapture(3, false)) : false);
    AppConfig::instance().setBoardState(3, ui->checkBox_board_3->isEnabled());
}

void DeviceManagerWindow::on_pushButton_disable_clicked()
{
    PCIeCommSdk::setBoardState(1, false);
    updataUi();
}


void DeviceManagerWindow::on_pushButton_enable_clicked()
{
    PCIeCommSdk::setBoardState(1, true);
    updataUi();
}


void DeviceManagerWindow::on_checkBox_board_clicked(bool checked)
{
    AppConfig::instance().setBoardState(1, checked);
}


void DeviceManagerWindow::on_pushButton_disable_2_clicked()
{
    PCIeCommSdk::setBoardState(2, false);
    updataUi();
}


void DeviceManagerWindow::on_pushButton_enable_2_clicked()
{
    PCIeCommSdk::setBoardState(2, true);
    updataUi();
}


void DeviceManagerWindow::on_checkBox_board_2_clicked(bool checked)
{
    AppConfig::instance().setBoardState(2, checked);
}

void DeviceManagerWindow::on_pushButton_disable_3_clicked()
{
    PCIeCommSdk::setBoardState(3, false);
    updataUi();
}


void DeviceManagerWindow::on_pushButton_enable_3_clicked()
{
    PCIeCommSdk::setBoardState(3, true);
    updataUi();
}


void DeviceManagerWindow::on_checkBox_board_3_clicked(bool checked)
{
    AppConfig::instance().setBoardState(3, checked);
}

void DeviceManagerWindow::on_pushButton_clicked()
{
    this->close();
}

