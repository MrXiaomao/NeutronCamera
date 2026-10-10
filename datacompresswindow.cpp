#include "datacompresswindow.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QRandomGenerator>
#include <QMap>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include "globalsettings.h"

#include <QVector>
#include <cstring>
#include <QThread>
#include <QMutexLocker>

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

        if (numSamples != H5_DATA_COLS) {
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
        if (fileName.contains(prefix, Qt::CaseInsensitive)) {
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
