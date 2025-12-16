#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "pciecommsdk.h"
#include "detsettingwindow.h"
#include "netsettingwindow.h"
#include "QGoodWindowHelper"
#include "commhelper.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QCustomPlot;
class QCPItemText;
class QCPItemLine;
class QCPItemRect;
class QCPGraph;
class QCPAbstractPlottable;
class QCPItemCurve;
class OfflineDataAnalysisWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~MainWindow();

    enum DetectorType{
        dtLSD,
        dtPSD,
        dtLBD
    };

    /*
    初始化
    */
    void initUi();
    void restoreSettings();
    void initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel);
    void setCheckBoxHelper(QCustomPlot* customPlot);
    void applyColorTheme();

    qint64 getDiskFreeSpace(const QString& disk = "C:/");
    QPixmap maskPixmap(QPixmap, QSize sz, QColor clrMask);
    QPixmap roundPixmap(QSize sz, QColor clrOut = Qt::gray);//单圆
    QPixmap dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut = Qt::gray);//双圆

public:
    virtual void closeEvent(QCloseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志
    void replyNeutronSpectrum(quint8, quint8, QVector<QPair<double,double>>&);//中子能谱
    void replyGammaSpectrum(quint8, quint8, QVector<QPair<double,double>>&);//伽马能谱

signals:
    void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    void reportNeutronSpectrum(quint8, quint8, QVector<QPair<double,double>>&);
    void reportGammaSpectrum(quint8, quint8, QVector<QPair<double,double>>&);

private slots:
    void on_action_exit_triggered();

    void on_action_open_triggered();

    void on_action_data_compress_triggered();

    void on_action_startMeasure_triggered();

    void on_action_stopMeasure_triggered();

    void on_action_about_triggered();

    void on_action_aboutQt_triggered();

    void on_action_lightTheme_triggered();

    void on_action_darkTheme_triggered();

    void on_action_colorTheme_triggered();

    void on_action_linear_triggered(bool checked);

    void on_action_logarithm_triggered(bool checked);

    void on_pushButton_openPower_clicked();

    void on_pushButton_closePower_clicked();

    void on_pushButton_openVoltage_clicked();

    void on_pushButton_closeVoltage_clicked();

    void on_pushButton_openPower_2_clicked();

    void on_pushButton_closePower_2_clicked();

    void on_pushButton_openVoltage_2_clicked();

    void on_pushButton_closeVoltage_2_clicked();

    void on_action_cfgParam_triggered();

    void on_action_cfgNet_triggered();

    void on_pushButton_selChannel1_clicked();

    void on_pushButton_selChannel2_clicked();

    void on_action_typeLSD_triggered(bool checked);

    void on_action_typePSD_triggered(bool checked);

    void on_action_typeLBD_triggered(bool checked);

    void on_pushButton_preview_clicked();

    void on_action_init_triggered();

    void on_action_clock_triggered(bool checked);

    void on_action_shotNum_triggered(bool checked);

    void on_action_stop_triggered(bool checked);

    void on_action_status_triggered(bool checked);

private:
    Ui::MainWindow *ui;
    CommHelper* mCommHelper = nullptr;
    PCIeCommSdk mPCIeCommSdk;
    DetSettingWindow *mDetSettingWindow = nullptr;
    NetSettingWindow *mNetSettingWindow = nullptr;
    bool mIsAlarm[2] = {false, false};//0-温度 1-电压
    bool mIsMeasuring = false;
    quint8 mCurrentPageIndex = 1;
    DetectorType mCurrentDetectorType = dtLSD; // 1-LSD 2-PSD 3-LBD
    bool mEnableAutoUpdateShotnum = false;//是否启用自动更新炮号
    bool mEnableClockSynchronization = false;//是否启用时钟同步
    bool mEnableEmergencyStop = false;//是否启用紧急停机
    QString mCurrentSavePath;

    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    bool mIsOneLayout = false;
    QColor mThemeColor = QColor(255,255,255);

    class QGoodWindowHelper *mainWindow = nullptr;
};

#endif // MAINWINDOW_H
