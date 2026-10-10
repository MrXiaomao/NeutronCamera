#ifndef DATACOMPRESSWINDOW_H
#define DATACOMPRESSWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QMutex>
#include <QObject>
#include <QFileInfo>
#include <QDir>

#include "QGoodWindowHelper"
#include <QQueue>
#include <QWaitCondition>
#include <QThreadPool>

class DataCompressWindow
{
public:

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
};

#endif // DATACOMPRESSWINDOW_H
