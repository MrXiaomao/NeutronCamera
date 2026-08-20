#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "pciecommsdk.h"
#include "settingwindow.h"
#include "devicemanagerwindow.h"
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
    void updateTableRowHidden();
    void recordExternalCommand(const QString&);

    qint64 getDiskFreeSpace(const QString& disk = "C:/");
    QPixmap maskPixmap(QPixmap, QSize sz, QColor clrMask);
    QPixmap roundPixmap(QSize sz, QColor clrOut = Qt::gray);//单圆
    QPixmap dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut = Qt::gray);//双圆

public:
    virtual void closeEvent(QCloseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    void onWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志
    void onNeutronSpectrum(quint8, QPair<QVector<double>,QVector<double>>&);//中子能谱
    void onGammaSpectrum(quint8, QPair<QVector<double>,QVector<double>>&);//伽马能谱
    void onStartMeasure();//开始测量

signals:
    void writeLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    void showNeutronSpectrum(quint8, QPair<QVector<double>,QVector<double>>&);
    void showGammaSpectrum(quint8, QPair<QVector<double>,QVector<double>>&);
    void startMeasure();//开始测量
    void rebootAsAdmin();

private slots:
    void on_action_exit_triggered();

    void on_action_data_compress_triggered();

    void on_action_startMeasure_triggered();

    void on_action_stopMeasure_triggered();

    void on_action_about_triggered();

    void on_action_aboutQt_triggered();

    void on_action_lightTheme_triggered();

    void on_action_darkTheme_triggered();

    void on_action_colorTheme_triggered();

    void on_pushButton_openPower_clicked();

    void on_pushButton_closePower_clicked();

    void on_pushButton_openVoltage_clicked();

    void on_pushButton_closeVoltage_clicked();

    void on_pushButton_openPower_2_clicked();

    void on_pushButton_closePower_2_clicked();

    void on_pushButton_openVoltage_2_clicked();

    void on_pushButton_closeVoltage_2_clicked();

    void on_action_cfgParam_triggered();

    void on_pushButton_selChannel1_clicked();

    void on_pushButton_selChannel2_clicked();

    void on_action_typeLSD_triggered(bool checked);

    void on_action_typePSD_triggered(bool checked);

    void on_action_typeLBD_triggered(bool checked);

    void on_pushButton_preview_clicked();

    void on_action_init_triggered();

    void on_action_status_triggered(bool checked);

    void on_action_reset_triggered();

    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

    void on_action_cps_statistics_triggered();

    void on_action_deviceManager_triggered();

private:
    Ui::MainWindow *ui;
    CommHelper* mCommHelper = nullptr;
    PCIeCommSdk mPCIeCommSdk;
    SettingWindow *mSettingWindow = nullptr;// 系统参数配置界面
    DeviceManagerWindow* mDeviceManagerWindow = nullptr; // 设备管理器界面

    std::atomic<bool> mIsAlarm = false;// 性能监测是否出现异常
    bool mIsMeasuring = false;// 测量是否正在进行
    DetectorType mCurrentDetectorType = dtLSD; // 当前探测器类型：1-LSD 2-PSD 3-LBD
    QString mCurrentSavePath;// 本次实验数据存储路径

    bool mIsDarkTheme = false;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);

    class QGoodWindowHelper *mainWindow = nullptr;
    bool mEnableContinueMeasuer = false; // 启用连续测量
    bool mExternalTriggerMode = false;// 启用外触发
    bool mExternalSignalTriggered = false;// 外触发信号是否已经触发
    int mCurrentMeasuerCount = 0;
    int mContinueMeasuerCount = 0;
    int mContinueMeasuerFailCount = 0;
    bool mVoltageSwitcherOpened = false;// 48V电压开关是否打开

private:
    struct LogItem {
        QString text;
        QColor color;
    };
    QVector<LogItem> m_logBuffer; // 日志缓存队列
    QTimer* m_logFlushTimer;      // 定时刷新定时器
    QMutex m_bufferMutex;         // 多线程防护（如果日志来自后台线程）
    void appendColoredText(const QString &text, const QColor &color);

    // 新增：自动恢复跟随的状态变量
    QTimer* m_autoResumeScrollTimer; // 闲置超时计时器
    // 手动查看模式相关状态
    bool m_manualViewMode = false;
    int m_manualViewStayCounter = 0;
    // 这里可以自定义停留阈值，比如5秒
    const int m_resumeAutoScrollThreshold = 5;
    // 新增：鼠标按下状态标记
    bool m_isMouseHeldDown = false;

    void initLogEvent();
};

#endif // MAINWINDOW_H
