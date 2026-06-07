#include "settingwindow.h"
#include "ui_settingwindow.h"
#include "AppConfig.h"
#include <QtnProperty/PropertyWidget.h>
#include <QtnProperty/PropertyView.h>

SettingWindow::SettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingWindow)
{
    ui->setupUi(this);

    // 直接设置统一行高
    ui->widget_container->setParts(QtnPropertyWidgetPartsDescriptionPanel);//显示描述面板
    ui->widget_container->setPropertySet(AppConfig::instance().propertySet());//layout()->addWidget(propWidget);
    ui->widget_container->propertyView()->setItemHeightSpacing(8);
    ui->widget_container->propertyView()->setActiveProperty(0, true);
}

SettingWindow::~SettingWindow()
{
    delete ui;
}

void SettingWindow::on_pushButton_exit_clicked()
{
    QMetaObject::invokeMethod(this, "reportSettingFinished", Qt::QueuedConnection);
    this->close();
}

