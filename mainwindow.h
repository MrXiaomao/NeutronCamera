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
class PDUControlWidget;
class ToastManager;

// 探测器数据结构
struct DetectorData {
    // double countRate;                 // 当前计数率
    QVector<double> countRateHistory;    // 计数率历史，cps，每秒钟更新一次
    quint32 spectrum[8192];              // 累积能谱 (固定长度8192)
    quint32 lastSpectrumID;              // 上次测量累积时间的能谱序号
    quint32 lastAccumulateCount;         // 上次测量累积时间的计数率,暂时不考虑丢包带来的计数率修复
    double lastAccuCountFixed;     // 上次测量累积时间的计数率，考虑死时间修复后的值
    /** 计数率曲线已画到的最右 X（秒）；重测时叠墙钟缺口 deltaT 后再作为新段 offset */
    double lastCountRatePlotTimeSec;
    /** 当前测量段的时间偏置：plotTime = offset + sequence*measureTime/1000 */
    double countRateTimeOffsetSec;
    /** 掉线/停收时的墙钟 ms，用于计算重测缺口 */
    qint64 disconnectWallMs;
    // QDateTime lastUpdate;

    DetectorData()
        : lastSpectrumID(0)
        , lastAccumulateCount(0)
        , lastAccuCountFixed(0.0)
        , lastCountRatePlotTimeSec(0.0)
        , countRateTimeOffsetSec(0.0)
        , disconnectWallMs(0)
    {
        countRateHistory.clear();
        for(int i=0; i<8192; i++) spectrum[i] = 0;// 初始化能谱为全0
    }
};

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
    void onStartMeasure();//开始测量(软触发)
    void onPrepareMeasure();//开始测量
    // 处理数据解析后的能谱数据（解析线程）：累积数据；绘图投递到主线程
    void onFullSpectrum_Thread(const FullSpectrum& fullSpectrum);
    // 解析线程上报温度，仅写缓存
    void onReportDetectorTemperature(float temperature);

signals:
    void writeLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    void showNeutronSpectrum(quint8, QPair<QVector<double>,QVector<double>>&);
    void showGammaSpectrum(quint8, QPair<QVector<double>,QVector<double>>&);
    void startMeasure();//开始测量
    void prepareMeasure();//开始空采
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
    QElapsedTimer mLastAlarmTime;// 模组最后一次报警时间
    std::atomic<bool> mIsMeasuring = false;// 测量是否正在进行
    DetectorType mCurrentDetectorType = dtLSD; // 当前探测器类型：1-LSD 2-PSD 3-LBD
    QString mCurrentSavePath;// 本次实验数据存储路径
#ifdef SINGLE_BOARD_MODE
    quint8 mCurrentAuroraNo = 0;// 当前测量链路
    quint8 mCurrentChannelNo = 0;// 当前测量通道
    // 连续测量计时器
    quint32 mCountdownDurationMs = 0;
    QElapsedTimer mMeasureTimer;
#endif //SINGLE_BOARD_MODE
    bool mReconnecting = false; // 自动重连标识

    std::atomic<bool> mModuleOccurred[20];// 模组通讯异常
    std::atomic<quint8> mModuleOccurredCount[20];// 模组通讯连续出现异常次数
    QElapsedTimer mLastCommunicationTime;// 模组最后一次通讯时间

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

    PDUControlWidget *mPDUControlWidget = nullptr;
    ToastManager* mToastManager = nullptr;
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

    // 固定的24个通道能谱图像属性设置：是否刻度，X/Y坐标轴范围自动与否，坐标轴范围
    struct SpectrumPlotSettings {
        //多道道数
        int multiChannel = 8192;
        double xMin = 0.0;
        double xMax = 8192.0;
        double yMin = 0.0;
        double yMax = 100.0;

        int fitType = 1;
        double c0 = 1.0;
        double c1 = 0.0;
        double c2 = 0.0;
    };

    // 能谱图像属性
    std::atomic<bool> mEnScale = false;// 是否勾选能量刻度
    SpectrumPlotSettings m_spectrumPlotSettings;
    std::atomic<bool> mCountRatePlotReady = false; // 计数率曲线待刷新
    std::atomic<bool> mSpectrumPlotReady = false;  // 能谱曲线待刷新
    qint64 mTempLastUpdateTime = 0;
    DetectorData m_detectorData;
    mutable QMutex mDetectorDataMutex;       // 保护 m_detectorData（解析线程写、主线程读/绘图）
    // 更新能谱显示
    void updateSpecPlotData(const quint32 spectrum[]);

    /**
     * @brief 计数率显示，最快更新时间间隔不小于1000ms
     * @param FPGATime FPGA内部时钟，当前的计数率对应的FPGA时钟，单位s，能谱数据中单位是ms，这里采用向下取整
     * @param countRate 计数率，单位cps
     */
    void updateCountRatePlotData(double FPGATime, double countRate);
};

#endif // MAINWINDOW_H
