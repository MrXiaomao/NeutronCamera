#include "detsettingwindow.h"
#include "ui_detsettingwindow.h"
#include "qcomboboxdelegate.h"
#include "globalsettings.h"
#include <QStandardItemModel>

DetSettingWindow::DetSettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DetSettingWindow)
{
    ui->setupUi(this);

    GlobalSettings settings(CONFIG_FILENAME);
    ui->spinBox_deathTime->setValue(settings.value("Base/deathTime", 30).toUInt());
    ui->spinBox_triggerThold->setValue(settings.value("Base/triggerThold", 100).toUInt());

    ui->spinBox_spectrumRefreshTime->setValue(settings.value("Spectrum/refreshTime", 1000).toUInt());

    ui->comboBox_waveformTriggerMode->setCurrentIndex(settings.value("Waveform/triggerMode", 0).toUInt());
    ui->comboBox_waveformLength->setCurrentIndex(settings.value("Waveform/length", 0).toUInt());
}

DetSettingWindow::~DetSettingWindow()
{
    delete ui;
}

void DetSettingWindow::on_pushButton_save_clicked()
{
    GlobalSettings settings(CONFIG_FILENAME);
    settings.setValue("Base/deathTime", ui->spinBox_deathTime->value());
    settings.setValue("Base/triggerThold", ui->spinBox_triggerThold->value());

    settings.setValue("Spectrum/refreshTime", ui->spinBox_spectrumRefreshTime->value());

    settings.setValue("Waveform/triggerMode", ui->comboBox_waveformTriggerMode->currentIndex());
    settings.setValue("Waveform/length", ui->comboBox_waveformLength->currentIndex());

    QMetaObject::invokeMethod(this, "reportSettingFinished", Qt::QueuedConnection);
    this->close();
}


void DetSettingWindow::on_pushButton_cancel_clicked()
{
    QMetaObject::invokeMethod(this, "reportSettingFinished", Qt::QueuedConnection);
    this->close();
}
