#ifndef DATACOMPRESSWINDOW_H
#define DATACOMPRESSWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QMutex>
#include <QObject>
#include <QFileInfo>
#include <QDir>
#include <array>

#include "QGoodWindowHelper"
#include "pciecommsdk.h"

namespace Ui {
class DataCompressWindow;
}

// 数据分析工作线程类
class DataAnalysisWorker : public QObject
{
    Q_OBJECT

public:
    explicit DataAnalysisWorker(QObject *parent = nullptr);
    ~DataAnalysisWorker();

    void setParameters(const QString& dataDir, 
                      const QStringList& fileList,
                      const QString& outfileName,
                      int threshold,
                      int timePerFile,
                      int startTime,
                      int endTime);

    // 单个文件总大小：256 MB（整）
    // 文件头：128 bit = 16 字节（公共部分，跳过）
    // 文件尾：128 bit = 16 字节（跳过）
    // 中间全部是有效数据
    // 每个数据点 16 bit（2 字节，无符号）
    // 数据排列方式：ch3, ch3, ch2, ch2, ch0, ch0, ch1, ch1...（逐点交织）                      
    static bool readBin4Ch_fast(const QString& path,
        QVector<qint16>& ch0,
        QVector<qint16>& ch1,
        QVector<qint16>& ch2,
        QVector<qint16>& ch3,
        bool littleEndian = true);

    static bool readBin4Ch_fast(QByteArray& fileData,
                                QVector<qint16>& ch0,
                                QVector<qint16>& ch1,
                                QVector<qint16>& ch2,
                                QVector<qint16>& ch3,
                                bool littleEndian = true);

    // 计算基线值：使用直方图方法，找到出现频率最高的值作为基线
    // 当波形信号过多时，该方法明显会出现问题（暂时采用该方法）
    static qint16 calculateBaseline(const QVector<qint16>& data_ch);

    // 根据基线调整数据：根据板卡编号和通道号对数据进行不同的调整
    // boardNum: 板卡编号 1-6，根据编号的奇偶性判断（1,3,5为奇数板卡，2,4,6为偶数板卡）
    // ch: 1-4 (通道号)
    //     ch1和ch2：根据板卡编号的奇偶性决定调整方式
    //     ch3：data - baseline（与板卡编号无关）
    //     ch4：baseline - data（与板卡编号无关）
    static void adjustDataWithBaseline(QVector<qint16>& data_ch, qint16 baseline_ch, int boardNum, int ch);

    // 提取超过阈值的有效波形数据
    // data: 输入数据（已扣基线）
    // ch: 通道号（用于日志输出）
    // threshold: 触发阈值
    // pre_points: 触发点之前的点数
    // post_points: 触发点之后的点数
    // 返回: 所有提取的波形，每个波形是固定长度512的数组（优化内存使用）
    static QVector<std::array<qint16, 512>> overThreshold(const QVector<qint16>& data, int ch, int threshold, int pre_points, int post_points);

    void getValidWave();
    
    // 将波形数据按板卡分组写入HDF5文件
    // filePath: HDF5文件路径
    // boardNum: 板卡编号 (1-6)
    // wave_ch0, wave_ch1, wave_ch2, wave_ch3: 4个通道的波形数据
    // 返回: 是否成功写入
    static bool writeWaveformToHDF5(const QString& filePath, int boardNum,
                                     const QVector<std::array<qint16, 512>>& wave_ch0,
                                     const QVector<std::array<qint16, 512>>& wave_ch1,
                                     const QVector<std::array<qint16, 512>>& wave_ch2,
                                     const QVector<std::array<qint16, 512>>& wave_ch3);
public slots:
    void startAnalysis();
    void cancelAnalysis();

signals:
    void logMessage(const QString& msg, QtMsgType msgType);
    void progressUpdated(int current, int total);
    void analysisFinished(bool success, const QString& message);
    void analysisError(const QString& error);

private:

    QString mDataDir;
    QStringList mFileList;
    QString mOutfileName;
    int mThreshold;
    int mTimePerFile;
    int mStartTime;
    int mEndTime;
    
    QMutex mMutex;
    bool mCancelled;
};

class QCustomPlot;
class DataCompressWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DataCompressWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~DataCompressWindow();

    bool loadOfflineFilename(const QString&);
    void startAnalyze();

    // 加载出当前目录下的所有所有.bin文件，统计文件数目，及其文件总大小
    QStringList loadRelatedFiles(const QString& src);

    // 给出容量的最佳表示方法
    static QString humanReadableSize(qint64 bytes);

    // 从目录获取所有.bin文件列表（按名称排序）
    static QFileInfoList getBinFileList(const QString& dirPath);

    // 计算文件信息列表的总大小
    static qint64 calculateTotalSize(const QFileInfoList& fileinfoList);

    // 从文件信息列表提取文件名列表
    static QStringList extractFileNames(const QFileInfoList& fileinfoList);

    // 统计以指定前缀开头的文件数量
    static int countFilesByPrefix(const QStringList& fileList, const QString& prefix);

    // 计算测量时长
    static int calculateMeasureTime(int fileCount, int timePerFile);

    // 读取波形数据，这里是有效波形
    // 读取 wave_CH1.h5 中的数据集 data（行 = 波形数, 列 = 单个波形采样点数512）
    static QVector<QVector<qint16>> readWave(const std::string &fileName, const std::string &dsetName);

    Q_SIGNAL void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SLOT void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志

    virtual void closeEvent(QCloseEvent *event) override;

private slots:
    void on_pushButton_test_clicked();

    void on_pushButton_startUpload_clicked();

    void on_action_choseDir_triggered();

    void on_action_analyze_triggered();

    void on_action_exit_triggered();

    void on_action_lightTheme_triggered();

    void on_action_darkTheme_triggered();

    void on_action_colorTheme_triggered();

    // 验证时间范围输入
    void validateTimeRange();

private slots:
    // 工作线程相关的槽函数
    void onAnalysisLogMessage(const QString& msg, QtMsgType msgType);
    void onAnalysisProgress(int current, int total);
    void onAnalysisFinished(bool success, const QString& message);
    void onAnalysisError(const QString& error);

private:
    Ui::DataCompressWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;
    QFileInfoList mfileinfoList;
    QStringList mfileList;
    QString mShotNum;
    
    // 工作线程相关
    QThread* mAnalysisThread;
    DataAnalysisWorker* mAnalysisWorker;
};

#endif // DATACOMPRESSWINDOW_H
