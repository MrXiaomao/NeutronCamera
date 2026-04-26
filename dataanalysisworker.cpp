#include "dataanalysisworker.h"
#include "globalsettings.h"
#include <cstring> // std::memcpy

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
    const qint64 headBytes = 12;
    const qint64 tailBytes = 12;

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
    const qint64 headBytes = 12;
    const qint64 tailBytes = 12;
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
QVector<std::array<qint16, 516>> DataAnalysisWorker::overThreshold(quint32 packerStartTime, const QVector<qint16>& data, int ch, int threshold, int pre_points, int post_points)
{
    QVector<std::array<qint16, 516>> wave_ch;
    QVector<int> cross_indices;

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
            if (start_idx < 0 || start_idx + 512 > data.size()) {
                // 注意：这是静态函数，不能直接访问 ui
                qDebug() << QString("skip第%1个数据：边界不足").arg(a + 1);
                continue;
            }

            // 创建固定大小的波形数组
            std::array<qint16, 516> segment_data;

            // 前4个数据预留给波形触发时刻，0-1表示ms，2-3表示ns，正式数据从第5个开始
            quint64 currentPackerTime = (quint64)packerStartTime*1e6/*ms转ns*/ + start_idx * 2/*每个点表示2ns*/;
            segment_data[0] = (packerStartTime >> 16) & 0xFFFF;
            segment_data[1] = packerStartTime & 0xFFFF;
            segment_data[2] = ((start_idx*2) >> 16) & 0xFFFF;
            segment_data[3] = (start_idx*2) & 0xFFFF;
//            // 小端序拆分
//            for (int i = 0; i < 4; ++i) {
//                // 每次提取对应位置的16位，掩码0xFFFF保留低16位
//                segment_data[i] = static_cast<qint16>((currentPackerTime >> (i * 16)) & 0xFFFF);
//            }
            /*
                for (int i = 0; i < 4; ++i) {
                    // 1. 先把有符号qint16转成无符号quint16，截断保留低16位，清除符号位影响
                    quint16 unsigned16 = static_cast<quint16>(segment_data[i]);
                    // 2. 再转成quint64后左移对应位数，拼接到结果上
                    currentPackerTime |= static_cast<quint64>(unsigned16) << (i * 16);
                }
            */

            // 提取波形段（正好512点）
            for (int i = 0; i < 512; ++i) {
                segment_data[4+i] = data[start_idx + i];//前2位给触发时刻预留的
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
    //6个光纤口，每个光纤口3个通道
    int totalBoards = 6;
    int processedBoards = 0;

    QStringList tempFileList[6];
    for (auto& file : fileList){
        if (file.startsWith("1A"))
            tempFileList[0].append(file);
        else if (file.startsWith("1B"))
            tempFileList[0].append(file);
        else if (file.startsWith("2A"))
            tempFileList[0].append(file);
        else if (file.startsWith("2B"))
            tempFileList[0].append(file);
        else if (file.startsWith("3A"))
            tempFileList[0].append(file);
        else if (file.startsWith("3B"))
            tempFileList[0].append(file);
    }

    writeWaveformHeadToHDF5(hdf5FilePath, startTime, endTime, mThreshold);
    for(int deviceIndex=1; deviceIndex<=6; ++deviceIndex) {
        {
            QMutexLocker locker(&mMutex);
            if (mCancelled) {
                emit logMessage("分析已取消", QtWarningMsg);
                emit analysisFinished(false, "分析已取消");
                return;
            }
        }

        // 这里需要判断对应的板卡是否存在数据
        if (tempFileList[deviceIndex-1].size() == 0){
            const int totalProgress = 1;
            const int currentProgress = 1;
            emit progressUpdated(currentProgress, totalProgress);
            processedBoards++;
            emit logMessage(QString("板卡%1无数据...").arg(deviceIndex), QtInfoMsg);
            continue;
        }

        emit logMessage(QString("开始处理板卡%1的数据...").arg(deviceIndex), QtInfoMsg);

        //对每个通道的有效波形数据进行合并
        QVector<std::array<qint16, 516>> wave_ch0_all;
        QVector<std::array<qint16, 516>> wave_ch1_all;
        QVector<std::array<qint16, 516>> wave_ch2_all;

        int totalFiles = endFile - startFile;
        int processedFiles = 0;

        emit logMessage(QString("正在提取板卡%1的波形数据（读盘-计算流水线）...").arg(deviceIndex), QtInfoMsg);

        // 读盘线程：顺序读文件，尽量让磁盘持续满载
        // 队列容量建议 2~4（每个文件约120MB，容量越大占用内存越多）
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
            for (int i=0; i<tempFileList[deviceIndex-1].size(); ++i){
                {
                    QMutexLocker locker(&mMutex);
                    if (mCancelled) break;
                }

                const QString fileName = tempFileList[deviceIndex-1][i];// QString("%1data%2.bin").arg(deviceIndex).arg(fileID);
                const QString filePath = QDir(dataDir).filePath(fileName);
                int fileID = QFileInfo(fileName).baseName().mid(QFileInfo(fileName).baseName().indexOf("data")+4).toInt();

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
                job.packerStartTime = static_cast<quint32>(fileID * timePerFile);
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

            auto cb = [&](quint32 /*packerCurrentTime*/, quint8 channelIndex, QVector<std::array<qint16, 516>>& wave_ch) {
                QMutexLocker locker(&mergeMutex);
                if (channelIndex == 1)
                    wave_ch0_all.append(wave_ch);
                else if (channelIndex == 2)
                    wave_ch1_all.append(wave_ch);
                else if (channelIndex == 3)
                    wave_ch2_all.append(wave_ch);
            };

            // 注意：这里 cameraIndex=0 表示 3个通道都处理一次（对应本板卡）
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

        emit logMessage(QString("板卡%1: 已处理 %2/%3 个文件").arg(deviceIndex).arg(processedFiles).arg(totalFiles), QtInfoMsg);

        // 存储有效波形数据到HDF5文件
        emit logMessage(QString("正在写入板卡%1的波形数据...").arg(deviceIndex), QtInfoMsg);
        if (!writeWaveformToHDF5(hdf5FilePath, deviceIndex, wave_ch0_all, wave_ch1_all, wave_ch2_all)) {
            emit logMessage(QString("写入板卡%1的波形数据失败，请检查文件路径和权限").arg(deviceIndex), QtCriticalMsg);
            emit analysisFinished(false, QString("写入板卡%1的波形数据失败").arg(deviceIndex));
            return;
        } else {
            emit logMessage(QString("板卡%1写入成功: 通道%2=%3个波形, 通道%4=%5个波形, 通道%6=%7个波形")
                        .arg(deviceIndex)
                        .arg((deviceIndex-1)*3+1)
                        .arg(wave_ch0_all.size() > 4 ? wave_ch0_all.size()-4 : 0)
                        .arg((deviceIndex-1)*3+2)
                        .arg(wave_ch1_all.size() > 4 ? wave_ch1_all.size()-4 : 0)
                        .arg((deviceIndex-1)*3+3)
                        .arg(wave_ch2_all.size() > 4 ? wave_ch2_all.size()-4 : 0), QtInfoMsg);
        }

        processedBoards++;
    }

    emit logMessage(QString("所有波形数据处理完成，已保存到: %1").arg(hdf5FilePath), QtInfoMsg);
    emit analysisFinished(true, "数据分析完成");
}

// 将波形数据按板卡分组写入HDF5文件
#include <QTextCodec>
bool DataAnalysisWorker::writeWaveformToHDF5(const QString& filePath, int boardNum,
                                              const QVector<std::array<qint16, 516>>& wave_ch0,
                                              const QVector<std::array<qint16, 516>>& wave_ch1,
                                              const QVector<std::array<qint16, 516>>& wave_ch2)
{
    try {
        // 检查文件是否存在，决定打开方式
        bool fileExists = QFileInfo::exists(filePath);
        QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
        QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
        H5::H5File file(filePathBytes.toStdString(), fileExists ? H5F_ACC_RDWR : H5F_ACC_TRUNC);

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
        auto writeChannel = [&](const QString& datasetName, const QVector<std::array<qint16, 516>>& data) {
             std::string ds = datasetName.toUtf8().constData();

            if (data.isEmpty()) {
                // 如果没有数据，创建一个空数据集
                hsize_t dims[2] = {0, 514};
                H5::DataSpace dataspace(2, dims);
                H5::DataSet dataset = boardGroup.createDataSet(datasetName.toStdString(),
                                                               H5::PredType::NATIVE_INT16,
                                                               dataspace);
                dataset.close();
                return;
            }

            // 准备数据：将 QVector<std::array<qint16, 512>> 转换为连续内存
            hsize_t dims[2] = {static_cast<hsize_t>(data.size()), 516};
            H5::DataSpace dataspace(2, dims);
            QVector<qint16> flatData;
            flatData.resize(static_cast<int>(data.size() * 516));
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

bool DataAnalysisWorker::writeWaveformHeadToHDF5(const QString& filePath, quint32 packerStartTime, quint32 packerEndTime, quint32 threshold)
{
    try {
        // 检查文件是否存在，决定打开方式
        bool fileExists = QFileInfo::exists(filePath);
        QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
        QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
        H5::H5File file(filePathBytes.toStdString(), fileExists ? H5F_ACC_RDWR : H5F_ACC_TRUNC);

        // 创建或打开板卡组
        QString configGroupName = "Config";

        H5::Group configGroup;
        htri_t exists = H5Lexists(file.getId(), configGroupName.toStdString().c_str(), H5P_DEFAULT);

        if (exists > 0) {
            configGroup = file.openGroup(configGroupName.toStdString());
        } else {
            configGroup = file.createGroup(configGroupName.toStdString());
        }


        // 准备数据：将 QVector<std::array<qint16, 512>> 转换为连续内存
        hsize_t dims[2] = {1, 3};
        H5::DataSpace dataspace(2, dims);
        QVector<quint32> data = {packerStartTime, packerEndTime, threshold};

        // 存在才删（不 open，不抛异常）
        if (H5Lexists(configGroup.getId(), "configuration", H5P_DEFAULT) > 0) {
            configGroup.unlink("configuration");
        }

        H5::DataSet dataset = configGroup.createDataSet(
            "configuration", H5::PredType::NATIVE_UINT32, dataspace);

        // 保险起见：显式 memspace/fileSpace rank 一致
        H5::DataSpace fileSpace = dataset.getSpace();
        H5::DataSpace memSpace(2, dims);

        dataset.write(data.data(),
                      H5::PredType::NATIVE_UINT32,
                      memSpace,
                      fileSpace);
        fileSpace.close();
        dataset.close();
        configGroup.close();
        dataset.close();
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

bool DataAnalysisWorker::readWaveformHeadFromHDF5(const QString& filePath, quint32& packerStartTime, quint32& packerEndTime, quint32& threshold)
{
    try {
        // 检查文件是否存在，决定打开方式
        bool fileExists = QFileInfo::exists(filePath);
        QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
        QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
        H5::H5File file(filePathBytes.toStdString(), fileExists ? H5F_ACC_RDWR : H5F_ACC_TRUNC);

        // 创建或打开板卡组
        QString configGroupName = "Config";

        H5::Group configGroup;
        htri_t exists = H5Lexists(file.getId(), configGroupName.toStdString().c_str(), H5P_DEFAULT);

        if (exists > 0) {
            configGroup = file.openGroup(configGroupName.toStdString());
        } else {
            file.close();
            return false;
        }


        // 准备数据：将 QVector<std::array<qint16, 512>> 转换为连续内存
        hsize_t dims[2] = {1, 3};
        H5::DataSpace dataspace(2, dims);
        QVector<quint32> data = {packerStartTime, packerEndTime, threshold};

        // 存在才删（不 open，不抛异常）
        if (!H5Lexists(configGroup.getId(), "configuration", H5P_DEFAULT) > 0) {
            configGroup.close();
            file.close();
            return false;
        }

        H5::DataSet dataset = configGroup.openDataSet("configuration");
        H5::DataSpace fileSpace = dataset.getSpace();
        H5::DataSpace memSpace(2, dims);

        dataset.read(data.data(),
                      H5::PredType::NATIVE_UINT32,
                      memSpace,
                      fileSpace);

        packerStartTime = data[0];
        packerEndTime = data[1];
        threshold = data[2];

        fileSpace.close();
        dataset.close();
        configGroup.close();
        dataset.close();
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