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
    AppConfig::instance().enableBoard(1, PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(1)));
    ui->checkBox_board->setEnabled(PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(1)));
    ui->checkBox_board->setChecked(ui->checkBox_board->isEnabled() ? (AppConfig::instance().isEnableCapture(1, true) || AppConfig::instance().isEnableCapture(1, false)) : false);

    AppConfig::instance().enableBoard(2, PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(2)));
    ui->checkBox_board_2->setEnabled(PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(2)));
    ui->checkBox_board_2->setChecked(ui->checkBox_board_2->isEnabled() ? (AppConfig::instance().isEnableCapture(2, true) || AppConfig::instance().isEnableCapture(2, false)) : false);

    AppConfig::instance().enableBoard(3, PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(3)));
    ui->checkBox_board_3->setEnabled(PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(3)));
    ui->checkBox_board_3->setChecked(ui->checkBox_board_3->isEnabled() ? (AppConfig::instance().isEnableCapture(3, true) || AppConfig::instance().isEnableCapture(3, false)) : false);
}

void DeviceManagerWindow::on_pushButton_disable_clicked()
{
    PCIeCommSdk::setBoardEnable(PCIeCommSdk::physicalNoToBoardIndex(1), false);
    if (!PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(1)))
        qInfo() << "禁用采集卡#1";

    updataUi();
}



void DeviceManagerWindow::on_pushButton_enable_clicked()
{
    PCIeCommSdk::setBoardEnable(PCIeCommSdk::physicalNoToBoardIndex(1), true);
    if (PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(1)))
        qInfo() << "启用采集卡#1";

    updataUi();
}


void DeviceManagerWindow::on_checkBox_board_clicked(bool checked)
{
    AppConfig::instance().setBoardCaptureState(1, checked);
}


void DeviceManagerWindow::on_pushButton_disable_2_clicked()
{
    PCIeCommSdk::setBoardEnable(PCIeCommSdk::physicalNoToBoardIndex(2), false);
    if (!PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(2)))
        qInfo() << "禁用采集卡#2";

    updataUi();
}


void DeviceManagerWindow::on_pushButton_enable_2_clicked()
{
    PCIeCommSdk::setBoardEnable(PCIeCommSdk::physicalNoToBoardIndex(2), true);
    if (PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(2)))
        qInfo() << "启用采集卡#2";

    updataUi();
}


void DeviceManagerWindow::on_checkBox_board_2_clicked(bool checked)
{
    AppConfig::instance().setBoardCaptureState(2, checked);
}

void DeviceManagerWindow::on_pushButton_disable_3_clicked()
{
    PCIeCommSdk::setBoardEnable(PCIeCommSdk::physicalNoToBoardIndex(3), false);
    if (!PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(3)))
        qInfo() << "禁用采集卡#3";

    updataUi();
}


void DeviceManagerWindow::on_pushButton_enable_3_clicked()
{
    PCIeCommSdk::setBoardEnable(PCIeCommSdk::physicalNoToBoardIndex(3), true);
    if (PCIeCommSdk::boardIsEnable(PCIeCommSdk::physicalNoToBoardIndex(3)))
        qInfo() << "启用采集卡#3";

    updataUi();
}


void DeviceManagerWindow::on_checkBox_board_3_clicked(bool checked)
{
    AppConfig::instance().setBoardCaptureState(3, checked);
}


void DeviceManagerWindow::on_pushButton_clicked()
{
    this->close();
}

