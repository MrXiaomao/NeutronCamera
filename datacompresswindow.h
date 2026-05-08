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
#include <QQueue>
#include <QWaitCondition>
#include <QThreadPool>
#include <functional>
#include <atomic>
#include <thread>

#include "dataanalysisworker.h"

namespace Ui {
class DataCompressWindow;
}

class QCustomPlot;
class QProgressIndicator;
class DataCompressWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DataCompressWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~DataCompressWindow();

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

    Q_SIGNAL void doWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SLOT void onWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志

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

    QProgressIndicator *mProgressIndicator = nullptr;
    void applyColorTheme();
};

#endif // DATACOMPRESSWINDOW_H
