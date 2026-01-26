#include "datacompresswindow.h"
#include "ui_datacompresswindow.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QRandomGenerator>
#include <QMap>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include "globalsettings.h"
#include "qprogressindicator.h"

#include <array>
#include <QVector>
#include <cstring>
#include <QThread>
#include <QMutexLocker>

// ========== DataAnalysisWorker 实现 ==========

DataAnalysisWorker::DataAnalysisWorker(QObject *parent)
    : QObject(parent)
    , mThreshold(200)
    , mTimePerFile(50)
    , mStartTime(0)
    , mEndTime(0)
    , mCancelled(false)
{
}

DataAnalysisWorker::~DataAnalysisWorker()
{
}

void DataAnalysisWorker::setParameters(const QString& dataDir,
                                      const QStringList& fileList,
                                      const QString& outfileName,
                                      int threshold,
                                      int timePerFile,
                                      int startTime,
                                      int endTime)
{
    QMutexLocker locker(&mMutex);
    mDataDir = dataDir;
    mFileList = fileList;
    mOutfileName = outfileName;
    mThreshold = threshold;
    mTimePerFile = timePerFile;
    mStartTime = startTime;
    mEndTime = endTime;
    mCancelled = false;
}

void DataAnalysisWorker::cancelAnalysis()
{
    QMutexLocker locker(&mMutex);
    mCancelled = true;
}

void DataAnalysisWorker::startAnalysis()
{
    getValidWave();
}

/**
 * @brief 快速读取4通道二进制波形数据文件
 * 
 * 文件格式说明：
 * - 单个文件总大小固定
 * - 文件头：128 bit = 16 字节（公共部分，当前跳过，headBytes=0）
 * - 文件尾：128 bit = 16 字节（当前跳过，tailBytes=0）
 * - 中间全部是有效数据
 * - 每个数据点：16 bit（2 字节，无符号整数）
 * 
 * 数据排列方式（逐点交织）：
 * - 原始数据顺序：ch3, ch3, ch2, ch2, ch0, ch0, ch1, ch1, ch3, ch3, ch2, ch2, ch0, ch0, ch1, ch1, ...
 * - 每8个16位数据为一个周期，包含4个通道各2个采样点
 * - 每个通道的2个连续采样点存储在一起，然后按 ch3->ch2->ch0->ch1 的顺序排列
 * 
 * @param path 二进制文件路径
 * @param ch0 输出参数：通道0的数据向量（有符号16位整数）
 * @param ch1 输出参数：通道1的数据向量（有符号16位整数）
 * @param ch2 输出参数：通道2的数据向量（有符号16位整数）
 * @param ch3 输出参数：通道3的数据向量（有符号16位整数）
 * @param littleEndian 字节序标志，true表示小端序（默认），false表示大端序
 * @return true 读取成功，false 读取失败（文件不存在、格式错误、映射失败等）
 */
bool DataAnalysisWorker::readBin4Ch_fast(const QString& path,
    QVector<qint16>& ch0,
    QVector<qint16>& ch1,
    QVector<qint16>& ch2,
    QVector<qint16>& ch3,
    bool littleEndian /*= true*/)
{
    // 打开文件进行只读访问
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    // 文件头和文件尾字节数
    const qint64 headBytes = 16;
    const qint64 tailBytes = 16;

    // 获取文件总大小，并检查是否小于头尾字节数之和（避免负数）
    const qint64 fileSize = f.size();
    if (fileSize < headBytes + tailBytes) return false;

    // 计算有效数据负载的字节数（总大小减去文件头和文件尾）
    const qint64 payloadBytes = fileSize - headBytes - tailBytes;
    // 检查负载字节数是否为2的倍数（每个采样点占2字节）
    if (payloadBytes % 2 != 0) return false;

    // 计算总采样点数（每2字节为一个采样点）
    const qint64 totalSamples = payloadBytes / 2;
    // 检查总采样点数是否为4的倍数（4个通道，每个通道采样点数必须相等）
    if (totalSamples % 4 != 0) return false;
    // 计算每个通道的采样点数（总采样点数除以4）
    const qint64 samplesPerChannel = totalSamples / 4;

    // 预分配各通道数据向量的容量，避免读取过程中反复扩容，提高性能
    ch0.resize(samplesPerChannel);
    ch1.resize(samplesPerChannel);
    ch2.resize(samplesPerChannel);
    ch3.resize(samplesPerChannel);

    // 使用内存映射方式读取文件，从headBytes位置开始，映射payloadBytes长度的数据
    // 这种方式比逐字节读取效率更高，特别是在大文件的情况下
    uchar* p = f.map(headBytes, payloadBytes);
    if (!p) return false;

    // 将映射的内存区域转换为16位无符号整数数组指针，方便后续读取
    const quint16* src = reinterpret_cast<const quint16*>(p);

    if (littleEndian) {
        // 小端序（Little Endian）处理
        // 逐点解交织：将原始数据按通道分离
        // 原始数据顺序：src = [ch3_0,ch3_1,ch2_0,ch2_1,ch0_0,ch0_1,ch1_0,ch1_1, ...]
        // i: 源数据索引（每8个16位数据为一个周期）
        // j: 目标通道数据索引（每个通道每次处理2个采样点）
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 8) {
            // 从源数据中读取8个16位无符号整数（一个周期）
            quint16 v0 = src[i + 0];  // ch3 的第一个采样点
            quint16 v1 = src[i + 1];  // ch3 的第二个采样点
            quint16 v2 = src[i + 2];  // ch2 的第一个采样点
            quint16 v3 = src[i + 3];  // ch2 的第二个采样点
            quint16 v4 = src[i + 4];  // ch0 的第一个采样点
            quint16 v5 = src[i + 5];  // ch0 的第二个采样点
            quint16 v6 = src[i + 6];  // ch1 的第一个采样点
            quint16 v7 = src[i + 7];  // ch1 的第二个采样点

            // 将小端序的16位无符号整数转换为系统字节序（如果系统不是小端序则进行字节交换）
            v0 = qFromLittleEndian(v0); v1 = qFromLittleEndian(v1);
            v2 = qFromLittleEndian(v2); v3 = qFromLittleEndian(v3);
            v4 = qFromLittleEndian(v4); v5 = qFromLittleEndian(v5);
            v6 = qFromLittleEndian(v6); v7 = qFromLittleEndian(v7);

            // 将无符号整数转换为有符号整数，并除以4进行数据缩放
            // 除以4的原因可能是原始数据进行了放大（左移2位），这里恢复原始范围
            ch3[j] = static_cast<qint16>(v0)/4; ch3[j+1] = static_cast<qint16>(v1)/4;
            ch2[j] = static_cast<qint16>(v2)/4; ch2[j+1] = static_cast<qint16>(v3)/4;
            ch0[j] = static_cast<qint16>(v4)/4; ch0[j+1] = static_cast<qint16>(v5)/4;
            ch1[j] = static_cast<qint16>(v6)/4; ch1[j+1] = static_cast<qint16>(v7)/4;
        }
    } else {
        // 大端序（Big Endian）处理
        // 如果文件是大端序，需要将字节顺序转换为系统字节序
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 8) {
            // 从源数据中读取8个16位无符号整数（一个周期）
            quint16 v0 = src[i + 0];  // ch3 的第一个采样点
            quint16 v1 = src[i + 1];  // ch3 的第二个采样点
            quint16 v2 = src[i + 2];  // ch2 的第一个采样点
            quint16 v3 = src[i + 3];  // ch2 的第二个采样点
            quint16 v4 = src[i + 4];  // ch0 的第一个采样点
            quint16 v5 = src[i + 5];  // ch0 的第二个采样点
            quint16 v6 = src[i + 6];  // ch1 的第一个采样点
            quint16 v7 = src[i + 7];  // ch1 的第二个采样点

            // 将大端序的16位无符号整数转换为系统字节序（如果系统不是大端序则进行字节交换）
            v0 = qFromBigEndian(v0); v1 = qFromBigEndian(v1); 
            v2 = qFromBigEndian(v2); v3 = qFromBigEndian(v3); 
            v4 = qFromBigEndian(v4); v5 = qFromBigEndian(v5); 
            v6 = qFromBigEndian(v6); v7 = qFromBigEndian(v7);

            // 将无符号整数转换为有符号整数，并除以4进行数据缩放
            ch3[j] = static_cast<qint16>(v0)/4; ch3[j+1] = static_cast<qint16>(v1)/4;
            ch2[j] = static_cast<qint16>(v2)/4; ch2[j+1] = static_cast<qint16>(v3)/4;
            ch0[j] = static_cast<qint16>(v4)/4; ch0[j+1] = static_cast<qint16>(v5)/4;
            ch1[j] = static_cast<qint16>(v6)/4; ch1[j+1] = static_cast<qint16>(v7)/4;
        }
    }

    // 取消内存映射，释放资源
    f.unmap(p);
    return true;
}

bool DataAnalysisWorker::readBin3Ch_fast(const QString& path,
                                         QVector<qint16>& ch0,
                                         QVector<qint16>& ch1,
                                         QVector<qint16>& ch2,
                                         bool littleEndian /*= true*/)
{
    // 打开文件进行只读访问
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    // 文件头和文件尾字节数
    const qint64 headBytes = 16;
    const qint64 tailBytes = 16;

    // 获取文件总大小，并检查是否小于头尾字节数之和（避免负数）
    const qint64 fileSize = f.size();
    if (fileSize < headBytes + tailBytes) return false;

    // 计算有效数据负载的字节数（总大小减去文件头和文件尾）
    const qint64 payloadBytes = fileSize - headBytes - tailBytes;
    // 检查负载字节数是否为2的倍数（每个采样点占2字节）
    if (payloadBytes % 2 != 0) return false;

    // 计算总采样点数（每2字节为一个采样点）
    const qint64 totalSamples = payloadBytes / 2;
    // 检查总采样点数是否为3的倍数（3个通道，每个通道采样点数必须相等）
    if (totalSamples % 3 != 0) return false;
    // 计算每个通道的采样点数（总采样点数除以3）
    const qint64 samplesPerChannel = totalSamples / 3;

    // 预分配各通道数据向量的容量，避免读取过程中反复扩容，提高性能
    ch0.resize(samplesPerChannel);
    ch1.resize(samplesPerChannel);
    ch2.resize(samplesPerChannel);

    // 使用内存映射方式读取文件，从headBytes位置开始，映射payloadBytes长度的数据
    // 这种方式比逐字节读取效率更高，特别是在大文件的情况下
    uchar* p = f.map(headBytes, payloadBytes);
    if (!p) return false;

    // 将映射的内存区域转换为16位无符号整数数组指针，方便后续读取
    const quint16* src = reinterpret_cast<const quint16*>(p);

    if (littleEndian) {
        // 小端序（Little Endian）处理
        // 逐点解交织：将原始数据按通道分离
        // 原始数据顺序：src = [ch2_0,ch2_1,ch0_0,ch0_1,ch1_0,ch1_1, ...]
        // i: 源数据索引（每6个16位数据为一个周期）
        // j: 目标通道数据索引（每个通道每次处理2个采样点）
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 6) {
            // 从源数据中读取6个16位无符号整数（一个周期）
            quint16 v0 = src[i + 0];  // ch2 的第一个采样点
            quint16 v1 = src[i + 1];  // ch2 的第二个采样点
            quint16 v2 = src[i + 2];  // ch0 的第一个采样点
            quint16 v3 = src[i + 3];  // ch0 的第二个采样点
            quint16 v4 = src[i + 4];  // ch1 的第一个采样点
            quint16 v5 = src[i + 5];  // ch1 的第二个采样点

            // 将小端序的16位无符号整数转换为系统字节序（如果系统不是小端序则进行字节交换）
            v0 = qFromLittleEndian(v0); v1 = qFromLittleEndian(v1);
            v2 = qFromLittleEndian(v2); v3 = qFromLittleEndian(v3);
            v4 = qFromLittleEndian(v4); v5 = qFromLittleEndian(v5);

            // 将无符号整数转换为有符号整数，并除以4进行数据缩放
            // 除以4的原因可能是原始数据进行了放大（左移2位），这里恢复原始范围
            ch2[j] = static_cast<qint16>(v0)/4; ch2[j+1] = static_cast<qint16>(v1)/4;
            ch0[j] = static_cast<qint16>(v2)/4; ch0[j+1] = static_cast<qint16>(v3)/4;
            ch1[j] = static_cast<qint16>(v4)/4; ch1[j+1] = static_cast<qint16>(v5)/4;
        }
    } else {
        // 大端序（Big Endian）处理
        // 如果文件是大端序，需要将字节顺序转换为系统字节序
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 6) {
            // 从源数据中读取6个16位无符号整数（一个周期）
            quint16 v0 = src[i + 0];  // ch2 的第一个采样点
            quint16 v1 = src[i + 1];  // ch2 的第二个采样点
            quint16 v2 = src[i + 2];  // ch0 的第一个采样点
            quint16 v3 = src[i + 3];  // ch0 的第二个采样点
            quint16 v4 = src[i + 4];  // ch1 的第一个采样点
            quint16 v5 = src[i + 5];  // ch1 的第二个采样点

            // 将大端序的16位无符号整数转换为系统字节序（如果系统不是大端序则进行字节交换）
            v0 = qFromBigEndian(v0); v1 = qFromBigEndian(v1);
            v2 = qFromBigEndian(v2); v3 = qFromBigEndian(v3);
            v4 = qFromBigEndian(v4); v5 = qFromBigEndian(v5);

            // 将无符号整数转换为有符号整数，并除以4进行数据缩放
            ch2[j] = static_cast<qint16>(v0)/4; ch2[j+1] = static_cast<qint16>(v1)/4;
            ch0[j] = static_cast<qint16>(v2)/4; ch0[j+1] = static_cast<qint16>(v3)/4;
            ch1[j] = static_cast<qint16>(v4)/4; ch1[j+1] = static_cast<qint16>(v5)/4;
        }
    }

    // 取消内存映射，释放资源
    f.unmap(p);
    return true;
}

bool DataAnalysisWorker::readBin4Ch_fast(QByteArray& fileData,
                            QVector<qint16>& ch0,
                            QVector<qint16>& ch1,
                            QVector<qint16>& ch2,
                            QVector<qint16>& ch3,
                            bool littleEndian/* = true*/)
{
    // 文件头和文件尾字节数（当前设置为0，表示不使用文件头尾）
    // 注释掉的代码显示原始格式可能有16字节的文件头和文件尾
    const qint64 headBytes = 16;
    const qint64 tailBytes = 16;
    // const qint64 headBytes = 0;
    // const qint64 tailBytes = 0;

    // 获取文件总大小，并检查是否小于头尾字节数之和（避免负数）
    const qint64 fileSize = fileData.size();
    if (fileSize < headBytes + tailBytes) return false;

    // 计算有效数据负载的字节数（总大小减去文件头和文件尾）
    const qint64 payloadBytes = fileSize - headBytes - tailBytes;
    // 检查负载字节数是否为2的倍数（每个采样点占2字节）
    if (payloadBytes % 2 != 0) return false;

    // 计算总采样点数（每2字节为一个采样点）
    const qint64 totalSamples = payloadBytes / 2;
    // 检查总采样点数是否为4的倍数（4个通道，每个通道采样点数必须相等）
    if (totalSamples % 4 != 0) return false;
    // 计算每个通道的采样点数（总采样点数除以4）
    const qint64 samplesPerChannel = totalSamples / 4;

    // 预分配各通道数据向量的容量，避免读取过程中反复扩容，提高性能
    ch0.resize(samplesPerChannel);
    ch1.resize(samplesPerChannel);
    ch2.resize(samplesPerChannel);
    ch3.resize(samplesPerChannel);

    // 使用内存映射方式读取文件，从headBytes位置开始，映射payloadBytes长度的数据
    // 这种方式比逐字节读取效率更高，特别是在大文件的情况下
    uchar* p = (uchar *)fileData.constData() + headBytes;

    // 将映射的内存区域转换为16位无符号整数数组指针，方便后续读取
    const quint16* src = reinterpret_cast<const quint16*>(p);

    if (littleEndian) {
        // 小端序（Little Endian）处理
        // 逐点解交织：将原始数据按通道分离
        // 原始数据顺序：src = [ch3_0,ch3_1,ch2_0,ch2_1,ch0_0,ch0_1,ch1_0,ch1_1, ...]
        // i: 源数据索引（每8个16位数据为一个周期）
        // j: 目标通道数据索引（每个通道每次处理2个采样点）
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 8) {
            // 从源数据中读取8个16位无符号整数（一个周期）
            quint16 v0 = src[i + 0];  // ch3 的第一个采样点
            quint16 v1 = src[i + 1];  // ch3 的第二个采样点
            quint16 v2 = src[i + 2];  // ch2 的第一个采样点
            quint16 v3 = src[i + 3];  // ch2 的第二个采样点
            quint16 v4 = src[i + 4];  // ch0 的第一个采样点
            quint16 v5 = src[i + 5];  // ch0 的第二个采样点
            quint16 v6 = src[i + 6];  // ch1 的第一个采样点
            quint16 v7 = src[i + 7];  // ch1 的第二个采样点

            // 将小端序的16位无符号整数转换为系统字节序（如果系统不是小端序则进行字节交换）
            v0 = qFromLittleEndian(v0); v1 = qFromLittleEndian(v1);
            v2 = qFromLittleEndian(v2); v3 = qFromLittleEndian(v3);
            v4 = qFromLittleEndian(v4); v5 = qFromLittleEndian(v5);
            v6 = qFromLittleEndian(v6); v7 = qFromLittleEndian(v7);

            // 将无符号整数转换为有符号整数，并除以4进行数据缩放
            // 除以4的原因可能是原始数据进行了放大（左移2位），这里恢复原始范围
            ch3[j] = static_cast<qint16>(v0)/4; ch3[j+1] = static_cast<qint16>(v1)/4;
            ch2[j] = static_cast<qint16>(v2)/4; ch2[j+1] = static_cast<qint16>(v3)/4;
            ch0[j] = static_cast<qint16>(v4)/4; ch0[j+1] = static_cast<qint16>(v5)/4;
            ch1[j] = static_cast<qint16>(v6)/4; ch1[j+1] = static_cast<qint16>(v7)/4;
        }
    } else {
        // 大端序（Big Endian）处理
        // 如果文件是大端序，需要将字节顺序转换为系统字节序
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 8) {
            // 从源数据中读取8个16位无符号整数（一个周期）
            quint16 v0 = src[i + 0];  // ch3 的第一个采样点
            quint16 v1 = src[i + 1];  // ch3 的第二个采样点
            quint16 v2 = src[i + 2];  // ch2 的第一个采样点
            quint16 v3 = src[i + 3];  // ch2 的第二个采样点
            quint16 v4 = src[i + 4];  // ch0 的第一个采样点
            quint16 v5 = src[i + 5];  // ch0 的第二个采样点
            quint16 v6 = src[i + 6];  // ch1 的第一个采样点
            quint16 v7 = src[i + 7];  // ch1 的第二个采样点

            // 将大端序的16位无符号整数转换为系统字节序（如果系统不是大端序则进行字节交换）
            v0 = qFromBigEndian(v0); v1 = qFromBigEndian(v1);
            v2 = qFromBigEndian(v2); v3 = qFromBigEndian(v3);
            v4 = qFromBigEndian(v4); v5 = qFromBigEndian(v5);
            v6 = qFromBigEndian(v6); v7 = qFromBigEndian(v7);

            // 将无符号整数转换为有符号整数，并除以4进行数据缩放
            ch3[j] = static_cast<qint16>(v0)/4; ch3[j+1] = static_cast<qint16>(v1)/4;
            ch2[j] = static_cast<qint16>(v2)/4; ch2[j+1] = static_cast<qint16>(v3)/4;
            ch0[j] = static_cast<qint16>(v4)/4; ch0[j+1] = static_cast<qint16>(v5)/4;
            ch1[j] = static_cast<qint16>(v6)/4; ch1[j+1] = static_cast<qint16>(v7)/4;
        }
    }

    return true;
}

bool DataAnalysisWorker::readBin3Ch_fast(QByteArray& fileData,
                                         QVector<qint16>& ch0,
                                         QVector<qint16>& ch1,
                                         QVector<qint16>& ch2,
                                         bool littleEndian/* = true*/)
{
    // 文件头和文件尾字节数（当前设置为0，表示不使用文件头尾）
    // 注释掉的代码显示原始格式可能有16字节的文件头和文件尾
    const qint64 headBytes = 16;
    const qint64 tailBytes = 16;
    // const qint64 headBytes = 0;
    // const qint64 tailBytes = 0;

    // 获取文件总大小，并检查是否小于头尾字节数之和（避免负数）
    const qint64 fileSize = fileData.size();
    if (fileSize < headBytes + tailBytes) return false;

    // 计算有效数据负载的字节数（总大小减去文件头和文件尾）
    const qint64 payloadBytes = fileSize - headBytes - tailBytes;
    // 检查负载字节数是否为2的倍数（每个采样点占2字节）
    if (payloadBytes % 2 != 0) return false;

    // 计算总采样点数（每2字节为一个采样点）
    const qint64 totalSamples = payloadBytes / 2;
    // 检查总采样点数是否为3的倍数（3个通道，每个通道采样点数必须相等）
    if (totalSamples % 3 != 0) return false;
    // 计算每个通道的采样点数（总采样点数除以3）
    const qint64 samplesPerChannel = totalSamples / 3;

    // 预分配各通道数据向量的容量，避免读取过程中反复扩容，提高性能
    ch0.resize(samplesPerChannel);
    ch1.resize(samplesPerChannel);
    ch2.resize(samplesPerChannel);

    // 使用内存映射方式读取文件，从headBytes位置开始，映射payloadBytes长度的数据
    // 这种方式比逐字节读取效率更高，特别是在大文件的情况下
    uchar* p = (uchar *)fileData.constData() + headBytes;

    // 将映射的内存区域转换为16位无符号整数数组指针，方便后续读取
    const quint16* src = reinterpret_cast<const quint16*>(p);

    if (littleEndian) {
        // 小端序（Little Endian）处理
        // 逐点解交织：将原始数据按通道分离
        // 原始数据顺序：src = [ch2_0,ch2_1,ch0_0,ch0_1,ch1_0,ch1_1, ...]
        // i: 源数据索引（每6个16位数据为一个周期）
        // j: 目标通道数据索引（每个通道每次处理2个采样点）
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 6) {
            // 从源数据中读取6个16位无符号整数（一个周期）
            quint16 v0 = src[i + 0];  // ch2 的第一个采样点
            quint16 v1 = src[i + 1];  // ch2 的第二个采样点
            quint16 v2 = src[i + 2];  // ch0 的第一个采样点
            quint16 v3 = src[i + 3];  // ch0 的第二个采样点
            quint16 v4 = src[i + 4];  // ch1 的第一个采样点
            quint16 v5 = src[i + 5];  // ch1 的第二个采样点

            // 将小端序的16位无符号整数转换为系统字节序（如果系统不是小端序则进行字节交换）
            v0 = qFromLittleEndian(v0); v1 = qFromLittleEndian(v1);
            v2 = qFromLittleEndian(v2); v3 = qFromLittleEndian(v3);
            v4 = qFromLittleEndian(v4); v5 = qFromLittleEndian(v5);

            // 将无符号整数转换为有符号整数，并除以4进行数据缩放
            // 除以4的原因可能是原始数据进行了放大（左移2位），这里恢复原始范围
            ch2[j] = static_cast<qint16>(v0)/4; ch2[j+1] = static_cast<qint16>(v1)/4;
            ch0[j] = static_cast<qint16>(v2)/4; ch0[j+1] = static_cast<qint16>(v3)/4;
            ch1[j] = static_cast<qint16>(v4)/4; ch1[j+1] = static_cast<qint16>(v5)/4;
        }
    } else {
        // 大端序（Big Endian）处理
        // 如果文件是大端序，需要将字节顺序转换为系统字节序
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 6) {
            // 从源数据中读取8个16位无符号整数（一个周期）
            quint16 v0 = src[i + 0];  // ch2 的第一个采样点
            quint16 v1 = src[i + 1];  // ch2 的第二个采样点
            quint16 v2 = src[i + 2];  // ch0 的第一个采样点
            quint16 v3 = src[i + 3];  // ch0 的第二个采样点
            quint16 v4 = src[i + 4];  // ch1 的第一个采样点
            quint16 v5 = src[i + 5];  // ch1 的第二个采样点

            // 将大端序的16位无符号整数转换为系统字节序（如果系统不是大端序则进行字节交换）
            v0 = qFromBigEndian(v0); v1 = qFromBigEndian(v1);
            v2 = qFromBigEndian(v2); v3 = qFromBigEndian(v3);
            v4 = qFromBigEndian(v4); v5 = qFromBigEndian(v5);

            // 将无符号整数转换为有符号整数，并除以4进行数据缩放
            ch2[j] = static_cast<qint16>(v0)/4; ch2[j+1] = static_cast<qint16>(v1)/4;
            ch0[j] = static_cast<qint16>(v2)/4; ch0[j+1] = static_cast<qint16>(v3)/4;
            ch1[j] = static_cast<qint16>(v4)/4; ch1[j+1] = static_cast<qint16>(v5)/4;
        }
    }

    return true;
}

// 计算基线值：使用直方图方法，找到出现频率最高的值作为基线
qint16 DataAnalysisWorker::calculateBaseline(const QVector<qint16>& data_ch)
{
    if (data_ch.isEmpty()) {
        return 0;
    }

    // 每个线程一份，避免多线程争用；每次调用清零即可
    thread_local std::array<int, 65536> hist;

    // 清零（65536 个 int，成本可接受；比 QMap 快很多）
    std::fill(hist.begin(), hist.end(), 0);

    // qint16 映射到 [0,65535]
    // 将 value 转成 uint16_t 后直接作为索引（等价于 +32768 的映射）
    for (qint16 v : data_ch) {
        const uint16_t idx = static_cast<uint16_t>(v);
        ++hist[idx];
    }

    // 找最大计数
    int maxCount = -1;
    uint16_t bestIdx = 0;
    for (uint32_t i = 0; i < hist.size(); ++i) {
        const int c = hist[i];
        if (c > maxCount) {
            maxCount = c;
            bestIdx = static_cast<uint16_t>(i);
        }
    }

    // bestIdx 还原成 qint16（位级别 reinterpret 等价）
    return static_cast<qint16>(bestIdx);
}


// 根据基线调整数据：根据板卡编号和通道号对数据进行不同的调整
// boardNum: 板卡编号 1-6，根据编号的奇偶性判断（1,3,5为奇数板卡，2,4,6为偶数板卡）
// ch: 1-4 (通道号)
void DataAnalysisWorker::adjustDataWithBaseline(QVector<qint16>& data_ch, qint16 baseline_ch, int boardNum, int ch)
{
    if (data_ch.isEmpty()) {
        return;
    }

    // 当前通道中波形信号有两类，部分为负脉冲信号，部分为正脉冲信号
    // ch1和ch2：板卡编号、通道号共同决定调整方式
    // ch3和ch4：通道号决定调整方式，与板卡编号无关
    
    bool isPositive = false; //是否为正脉冲信号
    
    if (ch == 3) {
        // 第3通道：data - baseline
        isPositive = true;
    }
    else if (ch == 4) {
        // 第4通道：baseline - data
        isPositive = false;
    }
    else {
        // ch1和ch2：根据板卡编号的奇偶性和通道号的组合来决定
        // 奇数板卡(1,3,5)ch1 或 偶数板卡(2,4,6)ch2: data - baseline
        // 其他情况: baseline - data
        bool isOddBoard = (boardNum % 2 == 1);  // 判断是否为奇数板卡
        isPositive = (isOddBoard && ch == 1) || (!isOddBoard && ch == 2);
    }
    
    for (int i = 0; i < data_ch.size(); ++i) {
        if (isPositive) {
            data_ch[i] = data_ch[i] - baseline_ch;
        } else {
            data_ch[i] = baseline_ch - data_ch[i];
        }
    }
}

// 提取超过阈值的有效波形数据
QVector<std::array<qint16, 512>> DataAnalysisWorker::overThreshold(const QVector<qint16>& data, int ch, int threshold, int pre_points, int post_points)
{
    QVector<std::array<qint16, 512>> wave_ch;
    QVector<int> cross_indices;
    constexpr int WAVEFORM_LENGTH = 512;

    // 丢弃可能不完整的包（比如半个波形）
    // 从索引21开始（因为需要检查i-20到i的均值）
    for (int i = pre_points; i < data.size(); ++i) {
        // 条件：从低于阈值跨越到高于阈值，且前21个点（i-20到i）的均值小于阈值
        if (data[i - 1] <= threshold && data[i] > threshold) {
            // 计算从i-20到i（包括i）的21个点的均值
            qint64 sum = 0;
            for (int j = i - pre_points; j <= i; ++j) {
                sum += data[j];
            }
            double mean_value = static_cast<double>(sum) / 21.0;
            
            if (mean_value < threshold) {
                cross_indices.append(i);
            }
        }
    }

    if (cross_indices.isEmpty()) {
        // 注意：这是静态函数，不能直接访问 ui，所以这里保留 qDebug
        // 如果需要日志，应该通过参数传递日志回调函数
        qDebug() << QString("CH%1 don't have valid wave").arg(ch);
        return wave_ch;
    }

    // 提取每个交叉点前后的波形数据，每个波形固定长度为512
    wave_ch.reserve(cross_indices.size());  // 预分配空间
    for (int a = 0; a < cross_indices.size(); ++a) {
        try {
            int cross_idx = cross_indices[a];
            int start_idx = cross_idx - pre_points;
            
            // 检查边界，确保能提取完整的512点
            if (start_idx < 0 || start_idx + WAVEFORM_LENGTH > data.size()) {
                // 注意：这是静态函数，不能直接访问 ui
                qDebug() << QString("skip第%1个数据：边界不足").arg(a + 1);
                continue;
            }
            
            // 创建固定大小的波形数组
            std::array<qint16, WAVEFORM_LENGTH> segment_data;
            
            // 提取波形段（正好512点）
            for (int i = 0; i < WAVEFORM_LENGTH; ++i) {
                segment_data[i] = data[start_idx + i];
            }
            
            wave_ch.append(segment_data);
        }
        catch (...) {
            // 注意：这是静态函数，不能直接访问 ui
            qDebug() << QString("skip第%1个数据").arg(a + 1);
        }
    }

    return wave_ch;
}

/**
 * @brief 筛选出6个光纤的有效波形（对应24通道数据）,并存储到HDF5文件
 * 6根光纤，每根光纤4个通道，每根光纤的全采样数据分别存储为一个文件
 * 也就是每单位时间(50ms)产生6份文件。
 * 先进行基线扣除后再进行阈值判断
 */
void DataAnalysisWorker::getValidWave()
{
    QString dataDir, outfileName;
    QStringList fileList;
    int threshold, timePerFile, startTime, endTime;
    
    {
        QMutexLocker locker(&mMutex);
        dataDir = mDataDir;
        fileList = mFileList;
        outfileName = mOutfileName;
        threshold = mThreshold;
        timePerFile = mTimePerFile;
        startTime = mStartTime;
        endTime = mEndTime;
    }

    emit logMessage(QString("开始处理波形数据，阈值: %1").arg(threshold), QtInfoMsg);

    // 设置触发阈值前后波形点数
    int pre_points = 20;
    int post_points = 512 - pre_points - 1;

    if (dataDir.isEmpty()) {
        emit logMessage("数据目录路径为空", QtWarningMsg);
        emit analysisFinished(false, "数据目录路径为空");
        return;
    }

    //读取界面的起止时间
    int startFile = startTime / timePerFile + 1;
    int endFile = endTime / timePerFile + 1;

    emit logMessage(QString("处理时间范围: %1 ms - %2 ms (文件范围: %3 - %4)").arg(
        startTime).arg(endTime).arg(startFile).arg(endFile-1), QtInfoMsg);

    // 创建HDF5文件路径（在数据目录下）
    QString hdf5FilePath = QDir(dataDir).filePath(outfileName);
    emit logMessage(QString("输出文件路径: %1").arg(hdf5FilePath), QtInfoMsg);

    //读取文件，提取有效波形
    //6个光纤口，每个光纤口4个通道
    int totalBoards = 6;
    int processedBoards = 0;
    
    for(int deviceIndex=1; deviceIndex<=6; ++deviceIndex) {
        {
            QMutexLocker locker(&mMutex);
            if (mCancelled) {
                emit logMessage("分析已取消", QtWarningMsg);
                emit analysisFinished(false, "分析已取消");
                return;
            }
        }

        emit logMessage(QString("开始处理板卡%1的数据...").arg(deviceIndex), QtInfoMsg);

        //对每个通道的有效波形数据进行合并
        QVector<std::array<qint16, 512>> wave_ch0_all;
        QVector<std::array<qint16, 512>> wave_ch1_all;
        QVector<std::array<qint16, 512>> wave_ch2_all;
        QVector<std::array<qint16, 512>> wave_ch3_all;

        int totalFiles = endFile - startFile;
        int processedFiles = 0;
        
#if 1
        emit logMessage(QString("正在提取板卡%1的波形数据（读盘-计算流水线）...").arg(deviceIndex), QtInfoMsg);

        // 读盘线程：顺序读文件，尽量让磁盘持续满载
        // 队列容量建议 2~4（每个文件约256MB，容量越大占用内存越多）
        const int queueCapacity = 3;
        BoundedFileQueue queue(queueCapacity);

        QThreadPool* pool = QThreadPool::globalInstance();
        pool->setMaxThreadCount(qMax(1, QThread::idealThreadCount()));

        QMutex mergeMutex;
        std::atomic<int> processedFilesAtomic{0};

        // 用于等待所有解析任务结束（不影响读盘线程）
        std::atomic<int> pendingTasks{0};
        QMutex pendingMutex;
        QWaitCondition pendingCond;

        // 生产者：读文件 -> push 到队列
        std::atomic_bool producerDone{false};
        std::thread producer([&]() {
            for (int fileID = startFile; fileID < endFile; ++fileID) {
                {
                    QMutexLocker locker(&mMutex);
                    if (mCancelled) break;
                }

                const QString fileName = QString("%1data%2.bin").arg(deviceIndex).arg(fileID);
                const QString filePath = QDir(dataDir).filePath(fileName);

                QFile f(filePath);
                if (!f.open(QIODevice::ReadOnly)) {
                    emit logMessage(QString("板卡%1 文件%2: 打开失败").arg(deviceIndex).arg(fileName), QtWarningMsg);
                    continue;
                }
                // 大文件：增大缓冲，减少 read 系统调用次数
                // f.setReadBufferSize(16 * 1024 * 1024);

                const qint64 size = f.size();
                if (size <= 0) {
                    emit logMessage(QString("板卡%1 文件%2: 文件大小异常").arg(deviceIndex).arg(fileName), QtWarningMsg);
                    continue;
                }

                QByteArray buf = f.readAll();;
                if (buf.isEmpty()) {
                    emit logMessage(QString("板卡%1 文件%2: 读取不完整 (%3/%4)")
                                    .arg(deviceIndex).arg(fileName), QtWarningMsg);
                    continue;
                }

                FileJob job;
                job.filePath = filePath;
                job.deviceIndex = static_cast<quint8>(deviceIndex);
                job.packerStartTime = static_cast<quint32>((fileID - 1) * timePerFile);
                job.data = std::move(buf);

                queue.push(std::move(job));
            }

            producerDone = true;
            queue.stop();
        });

        // 消费者：从队列取出 buffer，丢到线程池做解交织+基线+阈值提取
        FileJob job;
        while (queue.pop(job)) {
            {
                QMutexLocker locker(&mMutex);
                if (mCancelled) break;
            }

            pendingTasks.fetch_add(1, std::memory_order_relaxed);

            auto onFinished = [&]() {
                const int left = pendingTasks.fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (left == 0) {
                    QMutexLocker lk(&pendingMutex);
                    pendingCond.wakeAll();
                }
            };

            auto cb = [&](quint32 /*packerCurrentTime*/, quint8 channelIndex, QVector<std::array<qint16, 512>>& wave_ch) {
                QMutexLocker locker(&mergeMutex);
                if (channelIndex == 1)
                    wave_ch0_all.append(wave_ch);
                else if (channelIndex == 2)
                    wave_ch1_all.append(wave_ch);
                else if (channelIndex == 3)
                    wave_ch2_all.append(wave_ch);
                else if (channelIndex == 4)
                    wave_ch3_all.append(wave_ch);
            };

            // 注意：这里 cameraIndex=0 表示 4个通道都处理一次（对应本板卡）
            auto *task = new ExtractValidWaveformFromBufferTask(
                std::move(job),
                0,
                threshold,
                pre_points,
                post_points,
                cb,
                [&, onFinished]() {
                    // 以“文件”为粒度更新进度（而不是以通道为粒度）
                    const int pf = processedFilesAtomic.fetch_add(1, std::memory_order_relaxed) + 1;

                    const int totalProgress = totalBoards * totalFiles;
                    const int currentProgress = (processedBoards * totalFiles) + pf;
                    emit progressUpdated(currentProgress, totalProgress);

                    onFinished();
                });

            pool->start(task);
        }

        if (producer.joinable()) producer.join();

        // 等待线程池任务全部结束（仅等待本次提交的任务）
        {
            QMutexLocker lk(&pendingMutex);
            while (pendingTasks.load(std::memory_order_acquire) > 0) {
                pendingCond.wait(&pendingMutex, 200);
                {
                    QMutexLocker locker(&mMutex);
                    if (mCancelled) break;
                }
            }
        }

        processedFiles = processedFilesAtomic.load(std::memory_order_relaxed);

        if (mCancelled) {
            emit logMessage("分析已取消", QtWarningMsg);
            emit analysisFinished(false, "分析已取消");
            return;
        }
#else
        for (int fileID = startFile; fileID < endFile; ++fileID) {
            {
                QMutexLocker locker(&mMutex);
                if (mCancelled) {
                    emit logMessage("分析已取消", QtWarningMsg);
                    emit analysisFinished(false, "分析已取消");
                    return;
                }
            }

            QString fileName = QString("%1data%2.bin").arg(deviceIndex).arg(fileID);
            QString filePath = QDir(dataDir).filePath(fileName);
            QVector<qint16> ch0, ch1, ch2, ch3;
            if (!readBin4Ch_fast(filePath, ch0, ch1, ch2, ch3, true)) {
                emit logMessage(QString("板卡%1 文件%2: 读取失败或文件不存在").arg(deviceIndex).arg(fileName), QtWarningMsg);
                continue;
            }
            processedFiles++;

            //扣基线，调整数据
            qint16 baseline_ch = calculateBaseline(ch0);
            adjustDataWithBaseline(ch0, baseline_ch, deviceIndex, 1);
            qint16 baseline_ch2 = calculateBaseline(ch1);
            adjustDataWithBaseline(ch1, baseline_ch2, deviceIndex, 2);
            qint16 baseline_ch3 = calculateBaseline(ch2);
            adjustDataWithBaseline(ch2, baseline_ch3, deviceIndex, 3);
            qint16 baseline_ch4 = calculateBaseline(ch3);
            adjustDataWithBaseline(ch3, baseline_ch4, deviceIndex, 4);

            //提取有效波形
            QVector<std::array<qint16, 512>> wave_ch0 = overThreshold(ch0, 1, threshold, pre_points, post_points);
            QVector<std::array<qint16, 512>> wave_ch1 = overThreshold(ch1, 2, threshold, pre_points, post_points);
            QVector<std::array<qint16, 512>> wave_ch2 = overThreshold(ch2, 3, threshold, pre_points, post_points);
            QVector<std::array<qint16, 512>> wave_ch3 = overThreshold(ch3, 4, threshold, pre_points, post_points);

            //合并有效波形
            wave_ch0_all.append(wave_ch0);
            wave_ch1_all.append(wave_ch1);
            wave_ch2_all.append(wave_ch2);
            wave_ch3_all.append(wave_ch3);

            // 发送进度更新
            int totalProgress = totalBoards * totalFiles;
            int currentProgress = (processedBoards * totalFiles) + processedFiles;
            emit progressUpdated(currentProgress, totalProgress);
        }
#endif

        emit logMessage(QString("板卡%1: 已处理 %2/%3 个文件").arg(deviceIndex).arg(processedFiles).arg(totalFiles), QtInfoMsg);

        // 存储有效波形数据到HDF5文件
        emit logMessage(QString("正在写入板卡%1的波形数据...").arg(deviceIndex), QtInfoMsg);
        if (!writeWaveformToHDF5(hdf5FilePath, deviceIndex, wave_ch0_all, wave_ch1_all, wave_ch2_all, wave_ch3_all)) {
            emit logMessage(QString("写入板卡%1的波形数据失败，请检查文件路径和权限").arg(deviceIndex), QtCriticalMsg);
            emit analysisFinished(false, QString("写入板卡%1的波形数据失败").arg(deviceIndex));
            return;
        } else {
            emit logMessage(QString("板卡%1写入成功: 通道0=%2个波形, 通道1=%3个波形, 通道2=%4个波形, 通道3=%5个波形")
                        .arg(deviceIndex)
                        .arg(wave_ch0_all.size())
                        .arg(wave_ch1_all.size())
                        .arg(wave_ch2_all.size())
                        .arg(wave_ch3_all.size()), QtInfoMsg);
        }

        processedBoards++;
    }

    emit logMessage(QString("所有波形数据处理完成，已保存到: %1").arg(hdf5FilePath), QtInfoMsg);
    emit analysisFinished(true, "数据分析完成");
}

// 将波形数据按板卡分组写入HDF5文件
bool DataAnalysisWorker::writeWaveformToHDF5(const QString& filePath, int boardNum,
                                              const QVector<std::array<qint16, 512>>& wave_ch0,
                                              const QVector<std::array<qint16, 512>>& wave_ch1,
                                              const QVector<std::array<qint16, 512>>& wave_ch2,
                                              const QVector<std::array<qint16, 512>>& wave_ch3)
{
    try {
        // 检查文件是否存在，决定打开方式
        bool fileExists = QFileInfo::exists(filePath);
        H5::H5File file(filePath.toStdString(),
                       fileExists ? H5F_ACC_RDWR : H5F_ACC_TRUNC);

        // 创建或打开板卡组
        QString boardGroupName = QString("Board%1").arg(boardNum);

        H5::Group boardGroup;
        htri_t exists = H5Lexists(file.getId(), boardGroupName.toStdString().c_str(), H5P_DEFAULT);

        if (exists > 0) {
            boardGroup = file.openGroup(boardGroupName.toStdString());
        } else {
            boardGroup = file.createGroup(boardGroupName.toStdString());
        }

        // 辅助函数：写入单个通道的数据集
        auto writeChannel = [&](const QString& datasetName, const QVector<std::array<qint16, 512>>& data) {
             std::string ds = datasetName.toUtf8().constData();

            if (data.isEmpty()) {
                // 如果没有数据，创建一个空数据集
                hsize_t dims[2] = {0, 512};
                H5::DataSpace dataspace(2, dims);
                H5::DataSet dataset = boardGroup.createDataSet(datasetName.toStdString(),
                                                               H5::PredType::NATIVE_INT16,
                                                               dataspace);
                dataset.close();
                return;
            }

            // 准备数据：将 QVector<std::array<qint16, 512>> 转换为连续内存
            hsize_t dims[2] = {static_cast<hsize_t>(data.size()), 512};
            H5::DataSpace dataspace(2, dims);
            QVector<qint16> flatData;
            flatData.resize(static_cast<int>(data.size() * 512));
            int offset = 0;
            for (const auto& wave : data) {
                std::memcpy(flatData.data() + offset,
                            wave.data(),
                            sizeof(qint16) * wave.size());
                offset += static_cast<int>(wave.size());
            }

            // 存在才删（不 open，不抛异常）
            if (H5Lexists(boardGroup.getId(), ds.c_str(), H5P_DEFAULT) > 0) {
                boardGroup.unlink(ds);
            }

            H5::DataSet dataset = boardGroup.createDataSet(
                ds, H5::PredType::NATIVE_INT16, dataspace);

            // 保险起见：显式 memspace/fileSpace rank 一致
            H5::DataSpace fileSpace = dataset.getSpace();
            H5::DataSpace memSpace(2, dims);

            dataset.write(flatData.data(),
                          H5::PredType::NATIVE_INT16,
                          memSpace,
                          fileSpace);

            dataset.close();
        };

        // 写入4个通道的数据
        writeChannel("wave_ch0", wave_ch0);
        writeChannel("wave_ch1", wave_ch1);
        writeChannel("wave_ch2", wave_ch2);
        writeChannel("wave_ch3", wave_ch3);

        boardGroup.close();
        file.close();

        return true;
    } catch (H5::FileIException& error) {
        // 注意：这是静态函数，不能直接访问 ui，异常信息通过返回值或参数传递
        qDebug() << "HDF5 File Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::DataSetIException& error) {
        qDebug() << "HDF5 DataSet Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::DataSpaceIException& error) {
        qDebug() << "HDF5 DataSpace Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::GroupIException& error) {
        qDebug() << "HDF5 Group Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (...) {
        qDebug() << "Unknown HDF5 Exception";
        return false;
    }
}

DataCompressWindow::DataCompressWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::DataCompressWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper*>(parent))
    , mAnalysisThread(nullptr)
    , mAnalysisWorker(nullptr)
{
    ui->setupUi(this);
    applyColorTheme();

    mProgressIndicator = new QProgressIndicator(this);
    ui->tableWidget_file->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    ui->tableWidget_file->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    ui->toolButton_start->setDefaultAction(ui->action_analyze);
    ui->toolButton_start->setText(tr("开始压缩"));
    ui->toolButton_start->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    {
        QActionGroup *themeActionGroup = new QActionGroup(this);
        ui->action_lightTheme->setActionGroup(themeActionGroup);
        ui->action_darkTheme->setActionGroup(themeActionGroup);
        ui->action_lightTheme->setChecked(!mIsDarkTheme);
        ui->action_darkTheme->setChecked(mIsDarkTheme);
    }

    QStringList args = QCoreApplication::arguments();
    this->setWindowTitle(QApplication::applicationName()+" - "+APP_VERSION + " [" + args[4] + "]");

    connect(this, SIGNAL(reporWriteLog(const QString&,QtMsgType)), this, SLOT(replyWriteLog(const QString&,QtMsgType)));

    // 连接时间范围验证信号
    connect(ui->spinBox_startT, &QSpinBox::editingFinished, this, &DataCompressWindow::validateTimeRange);
    connect(ui->spinBox_endT, &QSpinBox::editingFinished, this, &DataCompressWindow::validateTimeRange);
    
    // 当最大测量时间改变时，更新 spinBox 的最大值
    connect(ui->line_measure_endT, &QLineEdit::textChanged, this, [this](const QString& text) {
        bool ok;
        int maxTime = text.toInt(&ok);
        if (ok && maxTime > 0) {
            ui->spinBox_startT->setMaximum(maxTime);
            ui->spinBox_endT->setMaximum(maxTime);
            // 验证当前值
            validateTimeRange();
        }
    });

    QTimer::singleShot(0, this, [&](){
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
    });

    QTimer::singleShot(0, this, [&](){
        if(mainWindow) {
            mainWindow->fixMenuBarWidth();
        }
    });
}

DataCompressWindow::~DataCompressWindow()
{
    // 清理工作线程
    if (mAnalysisWorker && mAnalysisThread) {
        mAnalysisWorker->cancelAnalysis();
        mAnalysisThread->quit();
        mAnalysisThread->wait(5000); // 等待最多5秒
        if (mAnalysisThread->isRunning()) {
            mAnalysisThread->terminate();
            mAnalysisThread->wait();
        }
        mAnalysisWorker->deleteLater();
        mAnalysisThread->deleteLater();
    }
    delete ui;
}

void DataCompressWindow::closeEvent(QCloseEvent *event) {
    if ( QMessageBox::Yes == QMessageBox::question(this, tr("系统退出提示"), tr("确定要退出软件系统吗？"),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes))
        event->accept();
    else
        event->ignore();
}

void DataCompressWindow::replyWriteLog(const QString &msg, QtMsgType msgType/* = QtDebugMsg*/)
{
    // 创建一个 QTextCursor
    QTextCursor cursor = ui->textEdit_log->textCursor();
    // 将光标移动到文本末尾
    cursor.movePosition(QTextCursor::End);

    // 先插入时间
    QString color = "black";
    if (mIsDarkTheme)
        color = "white";
    cursor.insertHtml(QString("<span style='color:%1;'>%2</span>").arg(color, QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz >> ")));
    // 再插入文本
    if (msgType == QtDebugMsg || msgType == QtInfoMsg)
        cursor.insertHtml(QString("<span style='color:%1;'>%2</span>").arg(color, msg));
    else if (msgType == QtCriticalMsg || msgType == QtFatalMsg)
        cursor.insertHtml(QString("<span style='color:red;'>%1</span>").arg(msg));
    else
        cursor.insertHtml(QString("<span style='color:green;'>%1</span>").arg(msg));

    // 最后插入换行符
    cursor.insertHtml("<br>");

    // 确保 QTextEdit 显示了光标的新位置
    ui->textEdit_log->setTextCursor(cursor);

    //限制行数
    QTextDocument *document = ui->textEdit_log->document(); // 获取文档对象，想象成打开了一个TXT文件
    int rowCount = document->blockCount(); // 获取输出区的行数
    int maxRowNumber = 2000;//设定最大行
    if(rowCount > maxRowNumber){//超过最大行则开始删除
        QTextCursor cursor = QTextCursor(document); // 创建光标对象
        cursor.movePosition(QTextCursor::Start); //移动到开头，就是TXT文件开头

        for (int var = 0; var < rowCount - maxRowNumber; ++var) {
            cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor); // 向下移动并选中当前行
        }
        cursor.removeSelectedText();//删除选择的文本
    }
}

// 测试连接数据库
void DataCompressWindow::on_pushButton_test_clicked()
{
    //连接数据库
    if (ui->lineEdit_host->text().isEmpty())
        return;

    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");
    db.setHostName(ui->lineEdit_host->text());
    db.setPort(ui->spinBox_port->value());
    db.setDatabaseName("mysql");
    db.setUserName(ui->lineEdit_username->text());
    db.setPassword(ui->lineEdit_password->text());
    db.open();
    if (!db.isOpen()) {
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("连接MySQL数据库失败！"));
        return;
    }

    db.close();
    QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("数据库连接成功！"));
}

// 读取波形数据，这里是有效波形
// 读取 wave_CH1.h5 中的数据集 data（行 = 脉冲数, 列 = 采样点数）
// 返回 QVector<QVector<float>>，尺寸为 [numPulses x numSamples] = [N x 512]
QVector<QVector<qint16>> DataCompressWindow::readWave(const std::string &fileName,
                                                 const std::string &dsetName)
{
    QVector<QVector<qint16>> wave_CH1;

    try {
        H5::H5File file(fileName, H5F_ACC_RDONLY);
        H5::DataSet dataset = file.openDataSet(dsetName);
        H5::DataSpace dataspace = dataset.getSpace();

        const int RANK = dataspace.getSimpleExtentNdims();
        if (RANK != 2) {
            return {};
        }

        hsize_t dims[2];
        dataspace.getSimpleExtentDims(dims, nullptr);
        // 约定：dims[0] = 脉冲数（35679），dims[1] = 采样点数（512）
        hsize_t numPulses  = dims[0];
        hsize_t numSamples = dims[1];

        if (numSamples != 512) {
            return {};
        }

        // 先读到一维 buffer（int16）
        std::vector<short> buffer(numPulses * numSamples);
        dataset.read(buffer.data(), H5::PredType::NATIVE_SHORT);

        // 填到 QVector<QVector<float>>：wave_CH1[pulse][sample]
        wave_CH1.resize(static_cast<int>(numPulses));
        for (int p = 0; p < static_cast<int>(numPulses); ++p) {
            wave_CH1[p].resize(static_cast<int>(numSamples));
            for (int s = 0; s < static_cast<int>(numSamples); ++s) {
                short v = buffer[p * numSamples + s];      // 行主序：第 p 行第 s 列
                wave_CH1[p][s] = v;
            }
        }
    }
    catch (const H5::FileIException &e) {
        e.printErrorStack();
        return {};
    }
    catch (const H5::DataSetIException &e) {
        e.printErrorStack();
        return {};
    }
    catch (const H5::DataSpaceIException &e) {
        e.printErrorStack();
        return {};
    }

    return wave_CH1;
}

void DataCompressWindow::on_pushButton_startUpload_clicked()
{
    if (ui->lineEdit_host->text().isEmpty()){
        QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("请填写远程数据库信息！"));
        return;
    }

    // 读取 HDF5 中的 N x 512 波形数据
    QString outfileName = ui->lineEdit_outputFile->text().trimmed();
    // 检查是否已有.h5后缀（区分大小写）
    if (!outfileName.endsWith(".h5", Qt::CaseSensitive)) {
        outfileName += ".h5";
    }
    if (!QFileInfo::exists(outfileName)){
        QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("压缩文件不存在！"));
        return;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");
    db.setHostName(ui->lineEdit_host->text());
    db.setPort(ui->spinBox_port->value());
    db.setDatabaseName("mysql");
    db.setUserName(ui->lineEdit_username->text());
    db.setPassword(ui->lineEdit_password->text());
    db.open();
    if (!db.isOpen()) {
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("连接MySQL数据库失败！"));
        return;
    }

    // 先判断数据库是否存在
    QSqlQuery query;
    if (!query.exec("CREATE DATABASE IF NOT EXISTS NeutronCamera")) {
        QString errorMsg = QString("Failed to create database: %1").arg(query.lastError().text());
        replyWriteLog(errorMsg, QtCriticalMsg);
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据库创建失败！") + query.lastError().text());
        return;
    }

    // 关闭当前连接，重新连接到数据库 NeutronCamera
    db.close();
    db.setDatabaseName("NeutronCamera");
    if (!db.open()) {
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("连接数据库失败！"));
        return;
    }

    //创建用户表
    QString sql = "CREATE TABLE IF NOT EXISTS Spectrum ("
                  "id INT PRIMARY KEY AUTO_INCREMENT,"
                  "shotNum char(5),"
                  "timestamp datetime,";
    for (int i=1; i<=512; ++i){
        if (i==512)
            sql += QString("data%1 smallint)").arg(i);
        else
            sql += QString("data%1 smallint,").arg(i);
    }
    replyWriteLog(QString("创建数据表SQL: %1").arg(sql), QtDebugMsg);
    if (!query.exec(sql)){
        db.close();
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据表格创建失败！") + query.lastError().text());
        return;
    }

    QVector<QVector<qint16>> wave_CH1 = readWave(outfileName.toStdString(), "data");
    ui->pushButton_startUpload->setEnabled(false);
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->setMaximum(wave_CH1.size());

    mProgressIndicator->startAnimation();
    // 保存到数据库
    {
        QString sql = "INSERT INTO Spectrum (shotNum, timestamp,";
        for (int i=1; i<=512; ++i){
            if (i==512)
                sql += QString("data%1)").arg(i);
            else
                sql += QString("data%1,").arg(i);
        }
        sql += " VALUES (:shotNum, :timestamp,";
        for (int i=1; i<=512; ++i){
            if (i==512)
                sql += QString(":data%1)").arg(i);
            else
                sql += QString(":data%1,").arg(i);
        }

        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        query.prepare(sql);

        //批量插入，效率高
        QVariantList shotNumList;
        QVariantList timestampList;
        QVariantList dataList[512];

        // 单次最大插入512条记录，这里每次插入100条记录吧
        int count = 0;
        for (int i=0; i<wave_CH1.size(); ++i){
            shotNumList << mShotNum;
            timestampList << timestamp;

            for (int j=0; j<512; ++j){
                dataList[j] << (qint16)wave_CH1[i][j];
                QApplication::processEvents();
            }

            if (++count == 100){
                query.prepare(sql);
                query.bindValue(":shotNum", shotNumList);
                query.bindValue(":timestamp", timestampList);
                for (int i=0; i<512; ++i){
                    query.bindValue(QString(":data%1").arg(i+1), dataList[i]);
                }

                if (!query.execBatch()){
                    QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据上传失败！") + query.lastError().text());
                    db.close();
                    ui->pushButton_startUpload->setEnabled(true);
                    return;
                }

                ui->progressBar_2->setValue(i);
                count = 0;
                shotNumList.clear();
                timestampList.clear();
                for (int j=0; j<512; ++j)
                    dataList[j].clear();
            }
        }

        if (count != 0){
            query.prepare(sql);
            query.bindValue(":shotNum", shotNumList);
            query.bindValue(":timestamp", timestampList);
            for (int i=0; i<512; ++i){
                query.bindValue(QString(":data%1").arg(i+1), dataList[i]);
            }

            //执行预处理命令
            if (!query.execBatch()){
                QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据上传失败！") + query.lastError().text());
                db.close();
                ui->pushButton_startUpload->setEnabled(true);
                return;
            }
        }

        ui->progressBar_2->setValue(100);
        db.close();
        QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("数据上传完成！"));
        ui->pushButton_startUpload->setEnabled(true);
    }

    mProgressIndicator->stopAnimation();
}

void DataCompressWindow::on_action_choseDir_triggered()
{
    GlobalSettings settings;
    QString lastPath = settings.value("Global/Offline/LastFileDir", QDir::homePath()).toString();
    QString filePath = QFileDialog::getExistingDirectory(this, tr("选择测量数据存放目录"), lastPath);

    // 目录格式：炮号+日期+[cardIndex]测量数据[packIndex].bin
    if (filePath.isEmpty())
        return;

    settings.setValue("Global/Offline/LastFileDir", filePath);
    ui->textBrowser_filepath->setText(filePath);
    if (!QFileInfo::exists(filePath+"/Settings.ini")){
        QMessageBox::information(this, tr("提示"), tr("路径无效，缺失\"Settings.ini\"文件！"));
        return;
    }
    else {
        GlobalSettings settings(filePath+"/Settings.ini");
        mShotNum = settings.value("Global/ShotNum", "00000").toString();
        emit reporWriteLog("炮号：" + mShotNum);
    }


    //加载目录下所有文件，罗列在表格中，统计给出文件大小
    mfileList = loadRelatedFiles(filePath);
}


QStringList DataCompressWindow::loadRelatedFiles(const QString& dirPath)
{
    // 结果：按名称排序后的 .bin 文件名列表
    QStringList fileList;

    //对目录下的配置文件信息进行解析，里面包含了炮号、测试开始时间、测量时长、以及探测器类型
    QDir dir(dirPath);
    if (!dir.exists()) {
        replyWriteLog(QString("目录不存在: %1").arg(dirPath), QtWarningMsg);
        ui->tableWidget_file->clearContents();
        ui->tableWidget_file->setRowCount(0);
        ui->lineEdit_binCount->setText("0");
        ui->lineEdit_binTotal->setText("0 B");
        return fileList;
    }

    // 使用静态函数获取.bin文件列表
    QFileInfoList fileinfoList = getBinFileList(dirPath);
    mfileinfoList = fileinfoList;

    // 使用静态函数统计总大小
    qint64 totalSize = calculateTotalSize(fileinfoList);
    int fileCount = fileinfoList.size();
    
    // 使用静态函数提取文件名列表
    fileList = extractFileNames(fileinfoList);

    // ==== 填表 ====
    ui->tableWidget_file->setSortingEnabled(false);  // 填表时关闭排序避免抖动
    ui->tableWidget_file->clearContents();
    ui->tableWidget_file->setRowCount(fileCount);

    // 列设置：只需要设一次
    // 例：列0 文件名，列1 大小(bytes)，列2 可读大小，列3 最后修改时间
    if (ui->tableWidget_file->columnCount() != 4) {
        ui->tableWidget_file->setColumnCount(4);
        ui->tableWidget_file->setHorizontalHeaderLabels(
            {"File Name", "Size(bytes)", "Size", "Last Modified"}
            );
    }

    QLocale locale(QLocale::English);
    for (int i = 0; i < fileCount; ++i) {
        const QFileInfo& fi = fileinfoList.at(i);

        auto *itemName = new QTableWidgetItem(fi.fileName());
        itemName->setFlags(itemName->flags() ^ Qt::ItemIsEditable);

        auto *itemBytes = new QTableWidgetItem(locale.toString(fi.size()));
        itemBytes->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemBytes->setFlags(itemBytes->flags() ^ Qt::ItemIsEditable);

        auto *itemHuman = new QTableWidgetItem(humanReadableSize(fi.size()));
        itemHuman->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemHuman->setFlags(itemHuman->flags() ^ Qt::ItemIsEditable);

        auto *itemTime = new QTableWidgetItem(fi.lastModified().toString("yyyy-MM-dd HH:mm:ss"));
        itemTime->setFlags(itemTime->flags() ^ Qt::ItemIsEditable);

        ui->tableWidget_file->setItem(i, 0, itemName);
        ui->tableWidget_file->setItem(i, 1, itemBytes);
        ui->tableWidget_file->setItem(i, 2, itemHuman);
        ui->tableWidget_file->setItem(i, 3, itemTime);
    }

    // 表头美化（可选）
    ui->tableWidget_file->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidget_file->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableWidget_file->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget_file->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget_file->setAlternatingRowColors(true);

    replyWriteLog(QString("bin文件数量: %1, 总大小: %2").arg(fileCount).arg(humanReadableSize(totalSize)), QtDebugMsg);
    ui->lineEdit_binCount->setText(QString::number(fileCount));
    ui->lineEdit_binTotal->setText(humanReadableSize(totalSize));

    //统计测量时长，选取光纤口1数据来统计
    int count1data = countFilesByPrefix(fileList, "1data");
    
    int time_per = ui->spinBox_oneFileTime->value(); //单个文件包对应的时间长度，单位ms
    int measureTime = calculateMeasureTime(count1data, time_per);

    ui->line_measure_startT->setText("0");
    ui->line_measure_endT->setText(QString::number(measureTime));
    ui->spinBox_endT->setValue(time_per);
    
    // 更新 spinBox 的最大值
    ui->spinBox_startT->setMaximum(measureTime);
    ui->spinBox_endT->setMaximum(measureTime);
    
    // 验证当前值
    validateTimeRange();

    return fileList;
}

QString DataCompressWindow::humanReadableSize(qint64 bytes)
{
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    const double GB = MB * 1024.0;

    if (bytes >= GB) return QString::asprintf("%.2f GB", bytes / GB);
    if (bytes >= MB) return QString::asprintf("%.2f MB", bytes / MB);
    if (bytes >= KB) return QString::asprintf("%.2f KB", bytes / KB);
    return QString("%1 B").arg(bytes);
}

// 从目录获取所有.bin文件列表（按名称排序）
#include <QFileInfoList>
#include <QRegularExpression>
#include <algorithm>
#include <atomic>
#include <thread>
QFileInfoList DataCompressWindow::getBinFileList(const QString& dirPath)
{
    QFileInfoList fileinfoList;
    
    QDir dir(dirPath);
    if (!dir.exists()) {
        return fileinfoList;  // 返回空列表
    }

    // 仅过滤 .bin 文件，按名称排序
    QStringList filters;
    filters << "*.bin";
    fileinfoList = dir.entryInfoList(
        filters,
        QDir::Files | QDir::NoSymLinks,   // 只要文件
        QDir::Unsorted    // 不排序了，后面手动排序
    );

    // 过滤掉能谱文件
    QFileInfoList result;
    for (auto item : fileinfoList){
        if (item.fileName().contains("data"))
            result.append(item);
    }

    QCollator collator;
    collator.setNumericMode(true);
    auto compareFilename = [&](const QFileInfo& A, const QFileInfo& B){
        return collator.compare(A.fileName(), B.fileName()) < 0;
    };
    std::sort(result.begin(), result.end(), compareFilename);
    return result;
}

// 计算文件信息列表的总大小
qint64 DataCompressWindow::calculateTotalSize(const QFileInfoList& fileinfoList)
{
    qint64 totalSize = 0;
    for (const QFileInfo& fi : fileinfoList) {
        totalSize += fi.size();
    }
    return totalSize;
}

// 从文件信息列表提取文件名列表
QStringList DataCompressWindow::extractFileNames(const QFileInfoList& fileinfoList)
{
    QStringList fileList;
    fileList.reserve(fileinfoList.size());
    for (const QFileInfo& fi : fileinfoList) {
        fileList << fi.fileName();
    }
    return fileList;
}

// 统计以指定前缀开头的文件数量
int DataCompressWindow::countFilesByPrefix(const QStringList& fileList, const QString& prefix)
{
    int count = 0;
    for (const QString& fileName : fileList) {
        if (fileName.startsWith(prefix, Qt::CaseInsensitive)) {
            ++count;
        }
    }
    return count;
}

// 计算测量时长
int DataCompressWindow::calculateMeasureTime(int fileCount, int timePerFile)
{
    return fileCount * timePerFile;
}

void DataCompressWindow::on_action_analyze_triggered()
{
    // 如果已有分析在进行中，不重复启动
    if (mAnalysisThread && mAnalysisThread->isRunning()) {
        QMessageBox::information(this, "提示", "数据分析正在进行中，请等待完成。");
        return;
    }
    
    // 获取数据目录路径
    QString dataDir = ui->textBrowser_filepath->toPlainText();
    if (dataDir.isEmpty()) {
        replyWriteLog("数据目录路径为空", QtWarningMsg);
        QMessageBox::warning(this, "警告", "数据目录路径为空！");
        return;
    }

    //解析文件，提取有效波形数据
    int th = ui->spinBox_threshold->value();
    QString outfileName = ui->lineEdit_outputFile->text().trimmed();
    
    //根据用户输入，对文件名后缀进行追加.h5，如果存在.h5则不追加后缀，否则追加后缀
    if (outfileName.isEmpty()) {
        replyWriteLog("输出文件名不能为空", QtWarningMsg);
        QMessageBox::warning(this, "警告", "输出文件名不能为空！");
        return;
    }
    
    // 检查是否已有.h5后缀（区分大小写）
    if (!outfileName.endsWith(".h5", Qt::CaseSensitive)) {
        outfileName += ".h5";
    }

    replyWriteLog("========================================", QtInfoMsg);
    replyWriteLog("开始数据压缩分析", QtInfoMsg);
    replyWriteLog(QString("数据目录: %1").arg(dataDir), QtInfoMsg);

    mProgressIndicator->startAnimation();

    // 创建HDF5文件路径（在数据目录下） 
    QString hdf5FilePath = QDir(dataDir).filePath(outfileName);
    replyWriteLog(QString("输出文件: %1").arg(outfileName), QtInfoMsg);

    //检查文件是否存在，如果存在则询问用户是否删除
    if (QFileInfo::exists(hdf5FilePath)) {
        //弹窗提醒用户hdf5FilePath已存在，给出选项是否删除
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, 
            "警告", 
            QString("文件 \"%1\" 已存在，是否删除并重新生成？").arg(hdf5FilePath),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (reply == QMessageBox::No) {
            replyWriteLog("用户取消操作", QtInfoMsg);
            return;
        }
        QFile::remove(hdf5FilePath);
        replyWriteLog(QString("已删除已存在的文件: %1").arg(hdf5FilePath), QtInfoMsg);
    }

    // 创建并启动工作线程
    // 如果已有线程在运行，先停止并清理
    if (mAnalysisThread) {
        if (mAnalysisThread->isRunning()) {
            // 如果有worker，先取消分析
            if (mAnalysisWorker) {
                mAnalysisWorker->cancelAnalysis();
                disconnect(mAnalysisWorker, nullptr, this, nullptr);
            }
            mAnalysisThread->quit();
            mAnalysisThread->wait(2000); // 等待最多2秒
            if (mAnalysisThread->isRunning()) {
                mAnalysisThread->terminate();
                mAnalysisThread->wait();
            }
        }
        // 清理旧对象
        disconnect(mAnalysisThread, nullptr, nullptr, nullptr);
        if (mAnalysisWorker) {
            mAnalysisWorker->deleteLater();
            mAnalysisWorker = nullptr;
        }
        mAnalysisThread->deleteLater();
        mAnalysisThread = nullptr;
    }

    mAnalysisThread = new QThread(this);
    mAnalysisWorker = new DataAnalysisWorker();

    // 设置参数
    int timePerFile = ui->spinBox_oneFileTime->value();
    int startTime = ui->spinBox_startT->value();
    int endTime = ui->spinBox_endT->value();
    
    mAnalysisWorker->setParameters(dataDir, mfileList, outfileName, th, 
                                   timePerFile, startTime, endTime);

    // 将worker移动到工作线程
    mAnalysisWorker->moveToThread(mAnalysisThread);

    // 连接信号和槽（使用QueuedConnection确保线程安全）
    connect(mAnalysisThread, &QThread::started, mAnalysisWorker, &DataAnalysisWorker::startAnalysis);
    connect(mAnalysisWorker, &DataAnalysisWorker::logMessage, 
            this, &DataCompressWindow::onAnalysisLogMessage, Qt::QueuedConnection);
    connect(mAnalysisWorker, &DataAnalysisWorker::progressUpdated, 
            this, &DataCompressWindow::onAnalysisProgress, Qt::QueuedConnection);
    connect(mAnalysisWorker, &DataAnalysisWorker::analysisFinished, 
            this, &DataCompressWindow::onAnalysisFinished, Qt::QueuedConnection);
    connect(mAnalysisWorker, &DataAnalysisWorker::analysisError, 
            this, &DataCompressWindow::onAnalysisError, Qt::QueuedConnection);
    
    // 不在这里设置自动清理，由onAnalysisFinished统一处理

    // 禁用开始按钮，防止重复启动
    ui->action_analyze->setEnabled(false);
    ui->toolButton_start->setEnabled(false);
    
    // 初始化进度条
    ui->progressBar->setValue(0);
    ui->progressBar->setMaximum(0); // 初始值设为0，表示不确定

    // 启动工作线程
    mAnalysisThread->start();
}


void DataCompressWindow::on_action_exit_triggered()
{
    mainWindow->close();
}


void DataCompressWindow::on_action_lightTheme_triggered()
{
    if(!mIsDarkTheme) return;
    mIsDarkTheme = false;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","false");
}


void DataCompressWindow::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","true");
    applyColorTheme();
}


void DataCompressWindow::on_action_colorTheme_triggered()
{
    GlobalSettings settings;
    QColor color = QColorDialog::getColor(mThemeColor, this, tr("选择颜色"));
    if (color.isValid()) {
        mThemeColor = color;
        mThemeColorEnable = true;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
        settings.setValue("Global/Startup/themeColor",mThemeColor);
    } else {
        mThemeColorEnable = false;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    }
    settings.setValue("Global/Startup/themeColorEnable",mThemeColorEnable);
    applyColorTheme();
}

void DataCompressWindow::applyColorTheme()
{
    if (mIsDarkTheme)
    {
        // 创建一个 QTextCursor
        QTextCursor cursor = ui->textEdit_log->textCursor();
        QTextDocument *document = cursor.document();
        QString html = document->toHtml();
        qDebug() << html;
        html = html.replace("color:#000000", "color:#ffffff");
        document->setHtml(html);
    }
    else
    {
        QTextCursor cursor = ui->textEdit_log->textCursor();
        QTextDocument *document = cursor.document();
        QString html = document->toHtml();
        html = html.replace("color:#ffffff", "color:#000000");
        document->setHtml(html);
    }
}

// 验证时间范围输入
void DataCompressWindow::validateTimeRange()
{
    // 获取最大测量时间
    QString maxTimeStr = ui->line_measure_endT->text();
    bool ok;
    int maxTime = maxTimeStr.toInt(&ok);
    
    if (!ok || maxTime <= 0) {
        // 如果最大值无效，不进行验证
        return;
    }
    
    // 更新 spinBox 的最大值（QSpinBox会自动限制输入值不超过最大值）
    ui->spinBox_startT->setMaximum(maxTime);
    ui->spinBox_endT->setMaximum(maxTime);
    
    // 获取当前输入值（此时值已经被 spinBox 自动限制在最大值内）
    int startTime = ui->spinBox_startT->value();
    int endTime = ui->spinBox_endT->value();
    
    // 验证 startTime 不能大于 endTime
    if (startTime > endTime) {
        // 如果起始时间大于结束时间，自动调整结束时间
        ui->spinBox_endT->setValue(startTime);
        QMessageBox::warning(this, "输入警告", 
                            QString("起始时间不能大于结束时间，结束时间已自动调整为 %1 ms").arg(startTime));
    }
}

// 工作线程相关的槽函数实现
void DataCompressWindow::onAnalysisLogMessage(const QString& msg, QtMsgType msgType)
{
    // 这个槽函数在工作线程中通过信号调用，会自动切换到UI线程执行
    replyWriteLog(msg, msgType);
}

void DataCompressWindow::onAnalysisProgress(int current, int total)
{
    // 更新进度条（在UI线程中执行）
    if (total > 0) {
        ui->progressBar->setMaximum(total);
        ui->progressBar->setValue(current);
    }
}

void DataCompressWindow::onAnalysisFinished(bool success, const QString& message)
{
    // 恢复UI状态
    ui->action_analyze->setEnabled(true);
    ui->toolButton_start->setEnabled(true);

    if (success) {
        replyWriteLog("数据压缩分析完成", QtInfoMsg);
        
        // 更新文件大小显示
        QString dataDir = ui->textBrowser_filepath->toPlainText();
        QString outfileName = ui->lineEdit_outputFile->text().trimmed();
        if (!outfileName.endsWith(".h5", Qt::CaseSensitive)) {
            outfileName += ".h5";
        }
        QString hdf5FilePath = QDir(dataDir).filePath(outfileName);
        
        QFileInfo fi(hdf5FilePath);
        if (fi.exists()) {
            qint64 sizeBytes = fi.size();
            ui->lineEdit_fileSize->setText(humanReadableSize(sizeBytes));
            replyWriteLog(QString("压缩后文件大小: %1").arg(humanReadableSize(sizeBytes)), QtInfoMsg);
        }
        
        replyWriteLog("========================================", QtInfoMsg);
    } else {
        replyWriteLog(QString("数据分析失败: %1").arg(message), QtCriticalMsg);
        QMessageBox::critical(this, "错误", QString("数据分析失败: %1").arg(message));
    }

    // Worker已经完成工作，现在停止线程并清理
    // 注意：这里必须在UI线程中执行，确保线程安全
    if (mAnalysisThread) {
        // 先断开worker的信号连接，避免后续信号触发
        if (mAnalysisWorker) {
            disconnect(mAnalysisWorker, nullptr, this, nullptr);
        }
        
        // 停止线程
        if (mAnalysisThread->isRunning()) {
            mAnalysisThread->quit();
            // 等待线程结束（最多等待3秒）
            if (!mAnalysisThread->wait(3000)) {
                // 如果等待超时，强制终止
                replyWriteLog("警告：线程未能正常结束，强制终止", QtWarningMsg);
                mAnalysisThread->terminate();
                mAnalysisThread->wait();
            }
        }
        
        // 断开线程的所有连接
        disconnect(mAnalysisThread, nullptr, nullptr, nullptr);
        
        // 清理worker对象（线程已停止，可以安全删除）
        if (mAnalysisWorker) {
            mAnalysisWorker->deleteLater();
            mAnalysisWorker = nullptr;
        }
        
        // 清理线程对象
        mAnalysisThread->deleteLater();
        mAnalysisThread = nullptr;
    }

    mProgressIndicator->stopAnimation();
}

void DataCompressWindow::onAnalysisError(const QString& error)
{
    replyWriteLog(QString("错误: %1").arg(error), QtCriticalMsg);
}

