#include "pciecommsdk.h"
#include <math.h>
#include <strsafe.h>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include "datacompresswindow.h"
PCIeCommSdk::PCIeCommSdk(QObject *parent)
    : QObject{parent}
{
    mDevices = enumDevices();
    qInfo() << "搜索到采集卡张数：" << mDevices.count();
}


PCIeCommSdk::~PCIeCommSdk()
{
    for (int index = 1; index <= numberOfDevices(); ++index){
        if (mMapDevice.contains(index))
            CloseHandle(mMapDevice[index]);

        if (mMapUser.contains(index))
            CloseHandle(mMapUser[index]);
    }
}

QStringList PCIeCommSdk::enumDevices()
{
#ifdef QT_DEBUG
    return QStringList() << tr("模拟设备#1") << tr("模拟设备#2") << tr("模拟设备#3") << tr("模拟设备#4");
#endif

    const GUID guid = {0x74c7e4a9, 0x6d5d, 0x4a70, {0xbc, 0x0d, 0x20, 0x69, 0x1d, 0xff, 0x9e, 0x9d}};
    QStringList lstDevices;
    HDEVINFO device_info = SetupDiGetClassDevsA((LPGUID)&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info == INVALID_HANDLE_VALUE) {
        qCritical() << "GetDevices INVALID_HANDLE_VALUE";
        return lstDevices;
    }

    SP_DEVICE_INTERFACE_DATA device_interface;
    device_interface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    DWORD index;
    for (index = 0; SetupDiEnumDeviceInterfaces(device_info, NULL, &guid, index, &device_interface); ++index) {
        ULONG detailLength = 0;
        if (!SetupDiGetDeviceInterfaceDetailA(device_info, &device_interface, NULL, 0, &detailLength, NULL) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            qDebug() << "SetupDiGetDeviceInterfaceDetail - get length failed";
            break;
        }

        PSP_DEVICE_INTERFACE_DETAIL_DATA_A dev_detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, detailLength);
        if (!dev_detail) {
            qDebug() << "HeapAlloc failed";
            break;
        }
        dev_detail->cbSize = sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (!SetupDiGetDeviceInterfaceDetailA(device_info, &device_interface, dev_detail, detailLength, NULL, NULL)) {
            qDebug() << "SetupDiGetDeviceInterfaceDetail - get detail failed";
            HeapFree(GetProcessHeap(), 0, dev_detail);
            break;
        }

        lstDevices.append(QString::fromLatin1(dev_detail->DevicePath, strlen(dev_detail->DevicePath)));
        HeapFree(GetProcessHeap(), 0, dev_detail);
    }

    SetupDiDestroyDeviceInfoList(device_info);
    return lstDevices;
}


quint32 PCIeCommSdk::numberOfDevices()
{
    return mDevices.count();
}

#include <processtopologyapi.h> //SetThreadGroupAffinity
void PCIeCommSdk::startCapture(quint32 cardIndex, QString fileSavePath, quint32 captureTimeSeconds, QString shotNum/*炮号*/)
{
    if (cardIndex > (quint32)mDevices.count()) {
        return ;
    }

    //QString devicePath = mDevices.at(index - 1) + XDMA_FILE_C2H_0;
    // if (i==0)
    //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&B189E7&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";
    // else if (i==1)
    //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&10D89B97&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";
    // else if (i==2)
    //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&18ec5e6b&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";

    CaptureThread *captureThread = new CaptureThread(cardIndex,
                                                     mMapDevice[cardIndex],
                                                     mMapUser[cardIndex],
                                                     mMapBypass[cardIndex],
                                                     fileSavePath,
                                                     captureTimeSeconds);
    captureThread->setPriority(QThread::Priority::HighestPriority);
    connect(captureThread, &CaptureThread::reportCaptureFinished, this, [=](quint32 index){
        mMapCaptureThread.remove(index);
        mThreadRunning[index] = false;
        if (!mThreadRunning[1] && !mThreadRunning[2] && !mThreadRunning[3])
            emit reportCaptureFinished();
    });
    connect(captureThread, &CaptureThread::reportFileReadElapsedtime, this, &PCIeCommSdk::reportFileReadElapsedtime);
    connect(captureThread, &CaptureThread::reportFileWriteElapsedtime, this, &PCIeCommSdk::reportFileWriteElapsedtime);
    //connect(captureThread, QOverload<quint8,quint32,QByteArray&>::of(&CaptureThread::reportCaptureWaveformData), this, &PCIeCommSdk::replyCaptureWaveformData);
    connect(captureThread, QOverload<quint8,quint32,QByteArray&>::of(&CaptureThread::reportCaptureSpectrumData), this, &PCIeCommSdk::replyCaptureSpectrumData);

    // 设置线程亲和性(小于64核)
    SetThreadAffinityMask(captureThread->currentThreadId(), cardIndex << 2);
    // 设置线程亲和性(大于64核)
    // 处理器组亲和性结构
    // GROUP_AFFINITY group_affinity;
    // group_affinity.Reserved[0] = 0;
    // group_affinity.Reserved[1] = 0;
    // group_affinity.Reserved[2] = 0;
    // group_affinity.Mask = index << 2;
    // group_affinity.Group = 0x01;//处理器组（相同进程下所有线程必须绑定到同一个处理器中，但是可以是该组下的不同处理器）

    // // 设置线程亲和性
    // SetThreadGroupAffinity(captureThread->currentThreadId(), &group_affinity, nullptr);

    //启动写文件线程
    mThreadRunning[cardIndex] = true;
    captureThread->start();
    mMapCaptureThread[cardIndex] = captureThread;
}

void PCIeCommSdk::stopCapture(quint32 cardIndex)
{
    auto iter = mMapCaptureThread.find(cardIndex);
    if (iter != mMapCaptureThread.end()){
        CaptureThread *captureThread = iter.value();
        captureThread->requestInterruption();
        captureThread->quit();
        captureThread = nullptr;
        mMapCaptureThread.remove(cardIndex);
    }
}

void PCIeCommSdk::startAllCapture(QString fileSavePath, quint32 captureTimeSeconds,  QString shotNum/*炮号*/)
{
    for (int cardIndex=1; cardIndex<=mDevices.count(); ++cardIndex){
        startCapture(cardIndex, fileSavePath, captureTimeSeconds, shotNum);
    }
}

void PCIeCommSdk::stopAllCapture()
{
    for (int index = 1; index <= mDevices.size(); ++index){
        stopCapture(index);
    }

    mMapCaptureThread.clear();
}

#include <QtEndian>
/* 在线数据分析
 *
*/
void PCIeCommSdk::replyCaptureWaveformData(quint8 cardIndex/*PCIe卡序号*/, quint32 currentPackIndex/*包时间ms*/, QByteArray& waveformData)
{
    // 根据相机序号，计算起始相机通道号
    quint8 cameraFrom = (cardIndex-1) * CAMNUMBER_DDR_PER + 1;
    quint8 cameraTo = cardIndex * CAMNUMBER_DDR_PER;
    quint8 cameraOrientation = 0;
    quint8 cameraNo = 0;//当前通道序号(0开始)
    quint8 cameraIndex = 0;
    if (mHorCameraIndex >= cameraFrom && mHorCameraIndex <= cameraTo){
        cameraNo = mHorCameraIndex - (cardIndex-1)*4;
        cameraIndex = mHorCameraIndex;
    }
    else if (mVerCameraIndex >= cameraFrom && mVerCameraIndex <= cameraTo){
        cameraNo = mVerCameraIndex - (cardIndex-1)*4;
        cameraIndex = mVerCameraIndex;
    }
    else
        return;

    /* 原始数据格式(总大小200000000B)
     * 包头              FFAB
     * 数据类型           00D3
     * 包序号             0000
     * 测量时间           0000
     * 包头时间戳         0000 0000 0000 0000
     * 原始数据(B  )      200,000,000-32
     * 包尾时间戳         0000 0000 0000 0000
     * 保留位            0000 0000 0000
     * 包尾              FFCD
    */
    QByteArray waveformHead = waveformData.left(16);
    std::reverse(waveformHead.begin(), waveformHead.end());
    QByteArray waveformTai = waveformData.right(16);
    std::reverse(waveformTai.begin(), waveformTai.end());

    if (waveformHead.startsWith(QByteArray::fromHex("FFAB00D3"))){
        //判断时间戳范围
        quint64 mTimestampMs[] = {mTimestampMs1, mTimestampMs2, mTimestampMs3};
        for (int i=0; i<=2; ++i){
            quint32 packIndex = mTimestampMs[i] / 50 + 1;
            //quint8 pac kPos = mTimestampMs[i] % 50;
            if (currentPackIndex == packIndex){
                bool ok;
                QByteArray head = waveformHead.left(4);
                quint16 serialNumber = qbswap(waveformHead.mid(4, 2).toHex().toUShort(&ok, 16));//包序号
                quint16 time = qbswap(waveformHead.mid(6, 2).toHex().toUShort(&ok, 16));//测量时间
                quint16 headTimeStamp = qbswap(waveformHead.mid(8, 8).toHex().toULongLong(&ok, 16));//包头时间戳
                quint16 tailTimeStamp = qbswap(waveformTai.left(8).toHex().toULongLong(&ok, 16));//包头时间戳
                QByteArray reserve = waveformTai.mid(8, 6);
                QByteArray tail = waveformTai.right(2);

                QByteArray chunk = waveformData.mid(16, 200000000-32);
                QVector<qint16> ch[4];
                QVector<qint16> targetdata;
                if (!DataAnalysisWorker::readBin4Ch_fast(chunk, ch[0], ch[1], ch[2], ch[3], true)) {
                    return;
                }

                // 这里借用解析历史数据的代码
                quint32 timeLength = 50;
                quint32 captureTime = 50;
                quint32 remainTime = mTimestampMs[i] % captureTime;

                //计算出是当前文件波形的第几个数据点
                int index = remainTime * 1000 * 1000/ 2;
                int point_num = timeLength * 1000 * 1000 / 2;

                //提取通道号的数据cameraNo
                QVector<qint16> waveform;
                if (cameraNo == 0) {
                    //扣基线，调整数据
                    qint16 baseline_ch = DataCompressWindow::calculateBaseline(ch[0]);
                    //根据相机序号计算出是第几块光纤卡
                    int board_index = (cameraIndex-1)/4+1;
                    DataCompressWindow::adjustDataWithBaseline(ch[0], baseline_ch, board_index, 1);
                    //提取数据
                    waveform = ch[0].mid(index, point_num);
                } else if (cameraNo == 1) {
                    //扣基线，调整数据
                    qint16 baseline_ch = DataCompressWindow::calculateBaseline(ch[1]);
                    //根据相机序号计算出是第几块光纤卡
                    int board_index = (cameraIndex-1)/4+1;
                    DataCompressWindow::adjustDataWithBaseline(ch[1], baseline_ch, board_index, 2);
                    //提取数据
                    waveform = ch[1].mid(index, point_num);
                } else if (cameraNo == 2) {
                    //扣基线，调整数据
                    qint16 baseline_ch = DataCompressWindow::calculateBaseline(ch[2]);
                    //根据相机序号计算出是第几块光纤卡
                    int board_index = (cameraIndex-1)/4+1;
                    DataCompressWindow::adjustDataWithBaseline(ch[2], baseline_ch, board_index, 3);
                    //提取数据
                    waveform = ch[2].mid(index, point_num);
                } else if (cameraNo == 3) {
                    //扣基线，调整数据
                    qint16 baseline_ch = DataCompressWindow::calculateBaseline(ch[3]);
                    //根据相机序号计算出是第几块光纤卡
                    int board_index = (cameraIndex-1)/4+1;
                    DataCompressWindow::adjustDataWithBaseline(ch[3], baseline_ch, board_index, 4);
                    //提取数据
                    waveform = ch[3].mid(index, point_num);
                }

                //数据流类型是int16
                QVector<QPair<double, double>> waveformPair;
                for (int k=0; k<ch[cameraNo].size(); ++k){
                    waveformPair.append(qMakePair(k*2, ch[cameraNo][k]));
                }

                emit reportWaveform(i+1, cameraIndex, waveformPair);
            }
        }
    }
}

void PCIeCommSdk::replyCaptureSpectrumData(quint8 cardIndex/*PCIe卡序号*/, quint32 currentPackIndex/*包时间ms*/, QByteArray& spectrumData)
{
    // 根据相机序号，计算起始相机通道号
    quint8 cameraFrom = (cardIndex-1) * CAMNUMBER_DDR_PER + 1;
    quint8 cameraTo = cardIndex * CAMNUMBER_DDR_PER;
    quint8 cameraOrientation = 0;
    quint8 currentNo = 0;//当前通道序号(0开始)
    if (mHorCameraIndex >= cameraFrom && mHorCameraIndex <= cameraTo){
        currentNo = mHorCameraIndex - (cardIndex-1)*4;
        cameraOrientation = CameraOrientation::Horizontal;
    }
    else if (mVerCameraIndex >= cameraFrom && mVerCameraIndex <= cameraTo){
        currentNo = mVerCameraIndex - (cardIndex-1)*4;
        cameraOrientation = CameraOrientation::Vertical;
    }
    else
        return;

    //根据通道号计算对应采集卡的第几通道
    //解析能谱数据
    /* 能谱数据（总大小512*2B）
     * 包头           FFAB
     * 数据类型        00D2
     * 能谱序号        0000 00
     * 测量时间        0000
     * 保留位          0000 0000 0000 00
     * 伽马能谱数据     248*2
     * 中子能谱数据     248*2
     * 绝对时间        0000 0000 0000 0000
     * 保留位          0000 0000 0000
     * 包尾            FFCD
    */
    //if (chunkHead.startsWith(QByteArray::fromHex("FFAB00D2")))
    {
        //数据包的其实时间戳/ns
        quint64 mTimestampMs[] = {mTimestampMs1, mTimestampMs2, mTimestampMs3};
        for (quint8 i=0; i<=2; ++i){
            quint32 packIndex = mTimestampMs[i] / 50 + 1;
            quint32 packPos = mTimestampMs[i] % 50;
            if (currentPackIndex == packIndex){
                QByteArray chunk = spectrumData.mid(packPos*1024, 1024);

                QByteArray chunkHead = chunk.left(16);
                std::reverse(chunkHead.begin(), chunkHead.end());
                QByteArray chunkTail = chunk.right(16);
                std::reverse(chunkTail.begin(), chunkTail.end());

                bool ok;
                //能谱序号 高位16bi表示这是第几个50ms的数据，低位8bit表示，在每一个50ms里，这是第几个能谱数据（范围是1-50）
                QByteArray head = chunkHead.left(4);
                quint32 serialNumber = qbswap(chunkHead.mid(4, 2).toHex().toUInt(&ok, 16));
                quint32 serialNumberRef = chunkHead.mid(6, 1).toInt();
                quint16 time = qbswap(chunkHead.mid(7, 2).toHex().toUShort(&ok, 16));//测量时间
                QByteArray reserve1 = chunkHead.mid(9, 7);

                QByteArray chunkGamma = chunk.mid(16, 496);  //62*4*2
                QByteArray chunkNeutron = chunk.mid(512, 496);

                quint64 absoluteTime = qbswap(chunkTail.left(8).toHex().toUInt(&ok, 16));//绝对时间
                QByteArray reserve2 = chunkTail.mid(1016, 6);
                QByteArray tail = chunkTail.right(2);

                //数据包的其实时间戳/ns
                //quint64 timestamp = (serialNumber - 1) * 50 + serialNumberRef;

                QByteArray gamma = chunkGamma.mid(currentNo*124, 124);
                QByteArray neutron = chunkNeutron.mid(currentNo*124, 124);
                {
                    QVector<QPair<double, double>> data;
                    for (int j=0; j<gamma.size(); j+=2){
                        bool ok;
                        quint16 amplitude = gamma.mid(j, 2).toHex().toUShort(&ok, 16);
                        data.append(qMakePair(j/2, qbswap(amplitude)));
                    }

                    emit reportGammaSpectrum(i+1, cameraOrientation, data);
                }

                {
                    QVector<QPair<double, double>> data;
                    for (int j=0; j<neutron.size(); j+=2){
                        bool ok;
                        quint16 amplitude = neutron.mid(j, 2).toHex().toUShort(&ok, 16);
                        data.append(qMakePair(j/2, qbswap(amplitude)));
                    }

                    emit reportNeutronSpectrum(i+1, cameraOrientation, data);
                }
            }
        }
    }
}


/**
 * @brief PCIeCommSdk::analyzeHistorySpectrumData 离线分析能谱历史数据
 * @param cameraIndex 相机序号
 * @param remainTime 剩余时间，用于计算采集时刻 
 * @param filePath 历史数据文件路径
 */
void PCIeCommSdk::analyzeHistorySpectrumData(quint8 cameraIndex, quint32 remainTime, QString filePath)
{
    //根据通道号计算对应采集卡的第几通道
    // quint8 cameraNo = cameraIndex % 4 - 1;

    /* 能谱数据（总大小512*2B）
        * 包头           FFAB
        * 数据类型        00D2
        * 能谱序号        000000
        * 测量时间        0000
        * 保留位          0000 0000 0000
        * 伽马能谱数据     248*2
        * 中子能谱数据     248*2
        * 绝对时间        0000 0000 0000 0000
        * 保留位          0000 0000 0000
        * 包尾            FFCD
    */
    /*if (data.startsWith(QByteArray::fromHex("FFAB00D2"))){
        if (data.size() >= 1024){
            QByteArray chunk = data.left(1024);

            bool ok;
            //能谱序号 高位16bi表示这是第几个50ms的数据，低位8bit表示，在每一个50ms里，这是第几个能谱数据（范围是1-50）
            QByteArray head = chunk.left(4);
            quint32 serialNumber = qbswap(chunk.mid(4, 2).toHex().toUInt(&ok, 16));
            quint32 serialNumberRef = chunk.mid(6, 1).toInt();
            quint16 time = qbswap(chunk.mid(7, 2).toHex().toUShort(&ok, 16));//测量时间
            QByteArray reserve1 = chunk.mid(9, 7);
            QByteArray gamma = chunk.mid(16, 496);  //62*4*2
            QByteArray neutron = chunk.mid(512, 496);
            quint64 absoluteTime = qbswap(chunk.mid(1008, 8).toHex().toUInt(&ok, 16));//绝对时间
            QByteArray reserve2 = chunk.mid(1016, 6);
            QByteArray tail = chunk.right(2);

            //数据包的起始时间戳/ns
            quint64 timestamp = (serialNumber - 1) * 50 + serialNumberRef;
            if (timestamp == mTimestampMs1){
                QByteArray gamma = chunk.mid(cameraNo*124, 124);
                QByteArray neutron = chunk.mid(cameraNo*124, 124);
                {
                    QVector<QPair<double, double>> data;
                    for (int i=0; i<gamma.size(); i+=2){
                        bool ok;
                        //每个通道的每个数据点还要除以4，才得到最后的全采样的数据值
                        quint16 amplitude = gamma.mid(i, 2).toHex().toUShort(&ok, 16);
                        data.append(qMakePair(i/2, qbswap(amplitude)));
                    }

                    emit reportGammaSpectrum(1, cameraIndex, data);
                }

                {
                    QVector<QPair<double, double>> data;
                    for (int i=0; i<neutron.size(); i+=2){
                        bool ok;
                        //每个通道的每个数据点还要除以4，才得到最后的全采样的数据值
                        quint16 amplitude = neutron.mid(i, 2).toHex().toUShort(&ok, 16);
                        data.append(qMakePair(i/2, qbswap(amplitude)/4));
                    }

                    emit reportNeutronSpectrum(1, cameraIndex, data);
                }
            }
        }
    }*/
}

/**
 * @brief PCIeCommSdk::analyzeHistoryWaveformData 离线分析波形历史数据，兼容旧版和新版FPGA数据协议
 * 波形数据排列方式：ch3, ch3, ch2, ch2, ch0, ch0, ch1, ch1...（逐点交织）
 * @param cameraIndex 相机序号
 * @param timeLength 要提取的波形时间长度，单位ms
 * @param remainTime 剩余时间，用于计算采集时刻 
 * @param filePath 历史数据文件路径
 */
void PCIeCommSdk::analyzeHistoryWaveformData(quint8 cameraIndex, quint32 timeLength, quint32 remainTime, QString filePath)
{
    //根据通道号计算对应采集卡的第几通道
    quint8 cameraNo = (cameraIndex - 1) % 4;

    QVector<qint16> ch0, ch1, ch2, ch3;
    QVector<qint16> targetdata;
    if (!DataAnalysisWorker::readBin4Ch_fast(filePath, ch0, ch1, ch2, ch3, true)) {
        // emit logMessage(QString("文件%1: 读取失败或文件不存在").arg(filePath), QtWarningMsg);
        qDebug() << "文件" << filePath << "读取失败或文件不存在";
        return;
    }

    //计算出是当前文件波形的第几个数据点
    int index = remainTime * 1000 * 1000/ 2;
    int point_num = timeLength * 1000 * 1000 / 2;

    //提取通道号的数据cameraNo
    QVector<qint16> waveform;
    if (cameraNo == 0) {
        //扣基线，调整数据
        qint16 baseline_ch = DataCompressWindow::calculateBaseline(ch0);
        //根据相机序号计算出是第几块光纤卡
        int board_index = (cameraIndex-1)/4+1;
        DataCompressWindow::adjustDataWithBaseline(ch0, baseline_ch, board_index, 1);
        //提取数据
        waveform = ch0.mid(index, point_num);
    } else if (cameraNo == 1) {
        //扣基线，调整数据
        qint16 baseline_ch = DataCompressWindow::calculateBaseline(ch1);
        //根据相机序号计算出是第几块光纤卡
        int board_index = (cameraIndex-1)/4+1;
        DataCompressWindow::adjustDataWithBaseline(ch1, baseline_ch, board_index, 2);
        //提取数据
        waveform = ch1.mid(index, point_num);
    } else if (cameraNo == 2) {
        //扣基线，调整数据
        qint16 baseline_ch = DataCompressWindow::calculateBaseline(ch2);
        //根据相机序号计算出是第几块光纤卡
        int board_index = (cameraIndex-1)/4+1;
        DataCompressWindow::adjustDataWithBaseline(ch2, baseline_ch, board_index, 3);
        //提取数据
        waveform = ch2.mid(index, point_num);
    } else if (cameraNo == 3) {
        //扣基线，调整数据
        qint16 baseline_ch = DataCompressWindow::calculateBaseline(ch3);
        //根据相机序号计算出是第几块光纤卡
        int board_index = (cameraIndex-1)/4+1;
        DataCompressWindow::adjustDataWithBaseline(ch3, baseline_ch, board_index, 4);
        //提取数据
        waveform = ch3.mid(index, point_num);
    }

    QVector<QPair<double,double>> waveformPair;
    for (int i=0;i<waveform.size();++i)
        waveformPair.push_back(qMakePair(i*2, waveform[i]));
    emit reportWaveform(1, cameraIndex, waveformPair);
    
    /* 原始数据格式(总大小200000000B)
        * 包头              FFAB
        * 数据类型           00D3
        * 包序号             0000
        * 测量时间           0000
        * 包头时间戳         0000 0000 0000 0000
        * 原始数据           200,000,000-256
        * 包尾时间戳         0000 0000 0000 0000
        * 保留位            0000 0000 0000
        * 包尾              FFCD
    */
    /*if (data.startsWith(QByteArray::fromHex("FFAB00D3"))){
        if (data.size() >= 200000000){
            QByteArray chunk = data.left(200000000);

            bool ok;
            QByteArray head = chunk.left(4);
            quint16 serialNumber = qbswap(chunk.mid(4, 2).toHex().toUShort(&ok, 16));//包序号
            quint16 time = qbswap(chunk.mid(6, 2).toHex().toUShort(&ok, 16));//测量时间
            quint16 headTimeStamp = qbswap(chunk.mid(8, 8).toHex().toULongLong(&ok, 16));//包头时间戳
            QByteArray raw = chunk.mid(16, 200000000-256);
            quint16 tailTimeStamp = qbswap(chunk.mid(200000000-256, 8).toHex().toULongLong(&ok, 16));//包头时间戳
            QByteArray reserve = chunk.mid(200000000-248, 6);
            QByteArray tail = chunk.right(2);
        }
    }


    quint32 pack_head_size = 128/8;//包头字节数
    quint32 pack_tail_size = 128/8;//包尾字节数
    
    //旧的数据包没有包头包尾
    if(mCaptureTime == oldCaptureTime)
    {
        pack_head_size = 0;
        pack_tail_size = 0;
    }
    */
}

#include "globalsettings.h"
void PCIeCommSdk::replySettingFinished()
{
    GlobalSettings settings(CONFIG_FILENAME);
    quint16 deathTime = settings.value("Base/deathTime", 30).toUInt();
    writeDeathTime(deathTime);

    quint16 triggerThold = settings.value("Base/triggerThold", 100).toUInt();
    writeTriggerThold(triggerThold);

    quint32 refreshTime = settings.value("Spectrum/refreshTime", 1000).toUInt();
    writeSpectnumRefreshTimelength(refreshTime);

    quint16 triggerMode = settings.value("Waveform/triggerMode", 0).toUInt();
    quint16 length = settings.value("Waveform/length", 0).toUInt();
    writeWaveformMode((TriggerMode)triggerMode, (WaveformLength)length);
}


/**
 * @brief PCIeCommSdk::setCaptureParamter 设置离线分析时刻参数
 * @param captureTime 单个文件对应采集时间
 * @param cameraIndex 相机序号
 * @param timeLength 要提取的波形时间长度，单位ms
 * @param time1 提取波形起始时刻，单位ms
 */
void PCIeCommSdk::setCaptureParamter(CaptureTime captureTime, quint8 cameraIndex, quint32 timeLength, quint32 time1)
{
    mCaptureTime = captureTime;
    mCameraIndex = cameraIndex;
    mTimestampMs1 = time1;
    mTimeLength = timeLength;
    mRemainTime = time1 % captureTime;
}

/**
 * @brief PCIeCommSdk::setCaptureParamter 设置在线分析时刻参数
 * @param horCameraIndex 水平相机序号
 * @param verCameraIndex 垂直相机序号
 * @param time1 提取能谱起始时刻，单位ms
 * @param time2 提取能谱起始时刻，单位ms
 * @param time3 提取能谱起始时刻，单位ms
 */
void PCIeCommSdk::setCaptureParamter(quint8 horCameraIndex, quint8 verCameraIndex, quint32 time1, quint32 time2, quint32 time3)
{
    mHorCameraIndex = horCameraIndex;
    mVerCameraIndex = verCameraIndex;
    mTimestampMs1 = time1;
    mTimestampMs2 = time2;
    mTimestampMs3 = time3;
}

#include <QRegularExpression>
bool PCIeCommSdk::openHistoryFile(QString filename)
{
    /*
    // 从文件名中提取卡号和包序号
    quint32 cardIndex =1;
    quint32 packIndex = 1;
    QRegularExpression re("\\d+"); // \d+ 匹配一个或多个数字
    auto matches = re.globalMatch(QFileInfo(filename).baseName());
    QStringList numbers;

    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        numbers.append(match.captured(0));
    }
    cardIndex = numbers.at(0).toUInt();
    packIndex = numbers.at(1).toUInt();
    */
    analyzeHistoryWaveformData(mCameraIndex, mTimeLength, mRemainTime, filename);
    // analyzeHistorySpectrumData(mCameraIndex, mRemainTime, filename);
    return true;

}

bool PCIeCommSdk::switchPower(quint32 channel, bool on)
{
    mMapPower[channel] = on;

    emit reportPowerStatus(channel, on);
    return true;
}

bool PCIeCommSdk::switchVoltage(quint32 channel, bool on)
{
    mMapVoltage[channel] = on;

    emit reportVoltageStatus(channel, on);
    return true;
}

bool PCIeCommSdk::switchBackupPower(quint32 channel, bool on)
{
    mMapBackupPower[channel] = on;

    emit reportBackupPowerStatus(channel, on);
    return true;
}

bool PCIeCommSdk::switchBackupVoltage(quint32 channel, bool on)
{
    mMapBackupVoltage[channel] = on;

    emit reportBackupVoltageStatus(channel, on);
    return true;
}

bool PCIeCommSdk::switchBackupChannel(quint32 channel, bool on)
{
    mMapChannel[channel] = on;

    emit reportBackupChannelStatus(channel, on);
    return true;
}

void PCIeCommSdk::writeDeathTime(quint16 deathTime)
{
    QByteArray askCurrentCmd = QByteArray::fromHex("12 34 00 0F FA 11 00 00 00 00 AB CD");
    askCurrentCmd[8] = ((deathTime >> 8) & 0xFF);
    askCurrentCmd[9] = (deathTime & 0xFF);
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeTriggerThold(quint16 triggerThold)
{
    QByteArray askCurrentCmd = QByteArray::fromHex("12 34 00 0F FA 12 00 00 00 00 AB CD");
    askCurrentCmd[8] = ((triggerThold >> 8) & 0xFF);
    askCurrentCmd[9] = (triggerThold & 0xFF);
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeWaveformMode(TriggerMode triggerMode, quint16 waveformLength)
{
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FC 10 00 00 00 00 AB CD").toUtf8());
    askCurrentCmd[7] = triggerMode;
    switch (waveformLength) {
    case 64:
        askCurrentCmd[9] = wl64;
        break;
    case 128:
        askCurrentCmd[9] = wl128;
        break;
    case 256:
        askCurrentCmd[9] = wl256;
        break;
    case 512:
        askCurrentCmd[9] = wl512;
        break;
    default:
        break;
    }
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeSpectnumRefreshTimelength(quint32 spectrumRefreshTime){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FD 10 00 00 00 00 AB CD").toUtf8());
    askCurrentCmd[6] = (spectrumRefreshTime >> 24) & 0xFF;
    askCurrentCmd[7] = (spectrumRefreshTime >> 16) & 0xFF;
    askCurrentCmd[8] = (spectrumRefreshTime >> 8)  & 0xFF;
    askCurrentCmd[9] = (spectrumRefreshTime)       & 0xFF;
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeWorkMode(WorkMode workMode){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FF 10 00 00 00 00 AB CD").toUtf8());
    askCurrentCmd[9] = workMode;
    writeCommand(askCurrentCmd);
}

/*********************************************************
     控制类指令
    ***********************************************************/
//开始测量
void PCIeCommSdk::writeStartMeasure(){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 01 AB CD").toUtf8());
    writeCommand(askCurrentCmd);
};

//停止测量
void PCIeCommSdk::writeStopMeasure(){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 00 AB CD").toUtf8());
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeCommand(QByteArray& data)
{
    for (int index = 1; index <= numberOfDevices(); ++index){
        if (mMapUser.contains(index)){
            LARGE_INTEGER address;
            address.QuadPart = 0x20000;
            if (INVALID_SET_FILE_POINTER == SetFilePointerEx(mMapUser[index], address, NULL, FILE_BEGIN)) {
                fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
                continue;
            }

            LARGE_INTEGER start;
            LARGE_INTEGER stop;
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);

            QueryPerformanceCounter(&start);
            DWORD dwNumberOfBytesWritten;
            if (!WriteFile(mMapUser[index], data.constData(), data.size(), &dwNumberOfBytesWritten, NULL)) {
                fprintf(stderr, "WriteFile to device %d failed with Win32 error code: %d\n",
                        index, GetLastError());
                continue;
            }
            QueryPerformanceCounter(&stop);

            double time_sec = (unsigned long long)(stop.QuadPart - start.QuadPart) / (double)freq.QuadPart;
            printf("%ld bytes written in %fs\n", dwNumberOfBytesWritten, time_sec);
        }
    }
}

void PCIeCommSdk::initialize()
{
    mDevices = enumDevices();

    for (int cardIndex = 1; cardIndex <= numberOfDevices(); ++cardIndex){
        QString devicePath = mDevices.at(cardIndex - 1) + XDMA_FILE_C2H_0;
        HANDLE hfInput = CreateFileA(devicePath.toStdString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hfInput == INVALID_HANDLE_VALUE) {
            emit reportCaptureFail(cardIndex, GetLastError());
            continue;
        }
        else{
            mMapDevice[cardIndex] = hfInput;

            devicePath = mDevices.at(cardIndex - 1) + XDMA_FILE_USER;
            hfInput = CreateFileA(devicePath.toStdString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hfInput == INVALID_HANDLE_VALUE) {
                CloseHandle(mMapDevice[cardIndex]);
                mMapDevice.remove(cardIndex);
                emit reportCaptureFail(cardIndex, GetLastError());
                continue;
            }
            else{
                mMapUser[cardIndex] = hfInput;
                emit reportCaptureFail(cardIndex, GetLastError());
            }

            devicePath = mDevices.at(cardIndex - 1) + XDMA_FILE_BYPASS;
            hfInput = CreateFileA(devicePath.toStdString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hfInput == INVALID_HANDLE_VALUE) {
                CloseHandle(mMapDevice[cardIndex]);
                CloseHandle(mMapUser[cardIndex]);
                mMapDevice.remove(cardIndex);
                mMapUser.remove(cardIndex);
                emit reportCaptureFail(cardIndex, GetLastError());
                continue;
            }
            else{
                mMapBypass[cardIndex] = hfInput;
                emit reportCaptureFail(cardIndex, GetLastError());
            }
        }
    }

    for (int cardIndex = 1; cardIndex <= numberOfDevices(); ++cardIndex){
        mMapPower[cardIndex] = true;
        mMapVoltage[cardIndex] = true;
        mMapChannel[cardIndex] = true;
    }
}
/**
 * WriteFileThread 数据写硬盘线程====================================================
*/
/**
* @function name: WriteFileThread
* @brief 构造函数
* @param[in]    cardIndex 采集卡索引
* @param[in]    hFile DDR内存句柄
* @param[in]    hUser Fpga控制指令句柄
* @param[in]    hBypass DDR内存句柄
* @param[in]    saveFilePath 文件保存路径
* @param[in]    captureTimeSeconds 采集时长
* @param[out]
* @return
*/
WriteFileThread::WriteFileThread(const quint32 index, const QString &saveFilePath)
    : mIndex(index)
    , mSaveFilePath(saveFilePath)
{
}

/**
* @function name:replyThreadExit
* @brief 响应线程退出事件
* @param[in]
* @param[out]
* @return
*/
void WriteFileThread::replyThreadExit()
{
    mTerminated = true;
    mElapsedTimer.start();
}

/**
* @function name:replyCaptureData
* @brief 响应实时采集数据事件
* @param[in]    waveformData 波形数据
* @param[in]    spectrumData 能谱数据
* @param[out]
* @return
*/
void WriteFileThread::replyCaptureData(QByteArray& waveformData, QByteArray& spectrumData)
{
    QMutexLocker locker(&mMutexWrite);
    spectrumData.append(waveformData);
    spectrumData.append(spectrumData);
}

void WriteFileThread::run()
{
    qRegisterMetaType<QByteArray>("QByteArray&");

    QThreadPool* pool = QThreadPool::globalInstance();
    pool->setMaxThreadCount(QThread::idealThreadCount());

    quint32 mPackref = 1;
    while (!this->isInterruptionRequested())
    {

        QVector<QByteArray> tempPool;
        {
            QMutexLocker locker(&mMutexWrite);
            if (mCachePool.size() > 0){
                tempPool.swap(mCachePool);
            }
            else{
                if (mTerminated && mElapsedTimer.elapsed() >= 3000)
                    break;
            }
        }

        while (tempPool.size() > 0){
            //QByteArray data = tempPool.at(0);
            // WriteFileTask *task = new WriteFileTask(mIndex, mPackref++, mSaveFilePath, data);
            // connect(task, &WriteFileTask::reportFileWriteElapsedtime, this, &WriteFileThread::reportFileWriteElapsedtime);
            // pool->start(task);
            // tempPool.pop_front();

            //写原始数据
            {
                QByteArray data = tempPool.at(0);
                QString filename = QString("%1/%2data%3.bin").arg(mSaveFilePath).arg(mIndex).arg(mPackref);
                HANDLE hfOutput = CreateFileA(filename.toStdString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (!hfOutput) {
                    qDebug() << "CreateFileA fail, win32 error code:" << GetLastError();
                }
                else{
                    DWORD NumberOfBytesWritten = 0;
                    if (!WriteFile(hfOutput, data.constData(), data.size(), &NumberOfBytesWritten, NULL)){
                        qDebug() << "WriteFile fail, win32 error code:" << GetLastError();
                    }
                    CloseHandle(hfOutput);
                }

                tempPool.pop_front();
            }

            //写能谱数据
            {
                QByteArray data = tempPool.at(0);
                QString filename = QString("%1/%2spec%3.bin").arg(mSaveFilePath).arg(mIndex).arg(mPackref);
                HANDLE hfOutput = CreateFileA(filename.toStdString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (!hfOutput) {
                    qDebug() << "CreateFileA fail, win32 error code:" << GetLastError();
                }
                else{
                    DWORD NumberOfBytesWritten = 0;
                    if (!WriteFile(hfOutput, data.constData(), data.size(), &NumberOfBytesWritten, NULL)){
                        qDebug() << "WriteFile fail, win32 error code:" << GetLastError();
                    }
                    CloseHandle(hfOutput);
                }

                tempPool.pop_front();
            }

            mPackref++;
        }

        QThread::msleep(1);
    }

    pool->waitForDone();
    qDebug() << "destroyWriteFileThread id:" << this->currentThreadId();
}

/**
 * CaptureThread 采集数据线程====================================================
*/

/**
* @function name: CaptureThread
* @brief 构造函数
* @param[in]    cardIndex 采集卡索引
* @param[in]    hFile DDR内存句柄
* @param[in]    hUser Fpga控制指令句柄
* @param[in]    hBypass DDR内存句柄
* @param[in]    saveFilePath 文件保存路径
* @param[in]    captureTimeSeconds 采集时长
* @param[out]
* @return
*/
CaptureThread::CaptureThread(const quint32 cardIndex, HANDLE hFile, HANDLE hUser, HANDLE hBypass, const QString &saveFilePath, quint32 captureTimeSeconds)
    : mCardIndex(cardIndex)
    , mDeviceHandle(hFile)
    , mUserHandle(hUser)
    , mBypassHandle(hBypass)
    , mSaveFilePath(saveFilePath)
    , mCaptureTimeSeconds(captureTimeSeconds)
{
    connect(this, &QThread::finished, this, &QThread::deleteLater);

    mWriteFileThread = new WriteFileThread(mCardIndex, saveFilePath);
    connect(this, QOverload<QByteArray&,QByteArray&>::of(&CaptureThread::reportCaptureData), mWriteFileThread, &WriteFileThread::replyCaptureData);
    connect(this, &CaptureThread::reportThreadExit, mWriteFileThread, &WriteFileThread::replyThreadExit);
    connect(mWriteFileThread, &WriteFileThread::reportFileWriteElapsedtime, this, &CaptureThread::reportFileWriteElapsedtime);
}

void CaptureThread::run()
{
    qRegisterMetaType<QByteArray>("QByteArray&");
    qRegisterMetaType<QVector<QPair<double,double>>>("QVector<QPair<double,double>>&");

    // 打开输入
    if (mDeviceHandle == INVALID_HANDLE_VALUE) {
        emit reportCaptureFail(mCardIndex, GetLastError());
        return;
    }

    LARGE_INTEGER inAddress;
    inAddress.QuadPart = 0x00000000;
    DWORD nNumberOfBytesToRead = 0x0BEBC200;//200000000
    quint32 packingDuration = 50; // 打包时长
    this->mCaptureRef = quint32((double)mCaptureTimeSeconds / 50.0 + 0.4);

    //启动写文件线程
    mWriteFileThread->start();

    quint32 mPackref = 1; //打包次数
    bool firstQuery = true;
    qDebug() << "createCaptureThread id:" << this->currentThreadId();
    while (!this->isInterruptionRequested())
    {
        QElapsedTimer elapsedTimer;
        elapsedTimer.start();
        DWORD nNumberOfBytesRead = 0;

        //检查设备是否准备好
        if (firstQuery){
            if (prepared()){
                firstQuery = false;
            }
            else{
                QThread::usleep(1);
                continue;
            }
        }

        /// 查询DDR/RAM是否写满
        if (!canRead())
            break;

        //读原始数据
        int size = 0x0BEBC200;
        QByteArray waveformData(size, 0);
        if (readRawData(waveformData)){

        }

        //读能谱数据
        size = 0xc800;//50*1024
        QByteArray spectrumData(size, 0);
        if (readSpectrumData(spectrumData)){

        }

        /// 清空状态
        if (!emptyStatus())
            break;

        /// 数据读完了，把标识位恢复回去
        if (!resetReadflag())
            break;

        emit reportCaptureData(waveformData, spectrumData); //发给写文件线程
        //emit reportCaptureWaveformData(mCardIndex, mPackref, waveformData);//发给数据分析
        emit reportCaptureSpectrumData(mCardIndex, mPackref, spectrumData);//发给数据分析

        if (mPackref++ >= this->mCaptureRef){
            emit reportThreadExit(mCardIndex);
            mWriteFileThread->wait();
            mWriteFileThread->deleteLater();
            break;
        }

        qint32 sleepTime = qMax((qint32)0, (qint32)(packingDuration - elapsedTimer.elapsed()));
        QThread::msleep(sleepTime);
    }

    //等待所有录制数据写入硬盘
    emit reportCaptureFinished(mCardIndex);
    qDebug() << "destroyCaptureThread id:" << this->currentThreadId();
}

/**
* @function name:prepared
* @brief 第一次查询准备
* @param[in]
* @param[out]
* @return           bool
*/
bool CaptureThread::prepared()
{
    return emptyStatus();
}

/**
* @function name:canRead
* @brief 判断DDR和RAM数据是否填满可读
* @param[in]
* @param[out]
* @return           bool
*/
//判断是否可读
bool CaptureThread::canRead()
{
    // 2.开始测量
    // xdma_rw.exe user write 0x20000 0x01 0xE0 0x34 0x12
    // xdma_rw.exe user write 0x20000 0x00 0xD0 0x34 0x12
    // xdma_rw.exe user write 0x20000 0x00 0xE0 0x34 0x12
    // xdma_rw.exe user write 0x20000 0x00 0xD0 0x34 0x12
    while (1){
        if (this->isInterruptionRequested())
            break;

        LARGE_INTEGER inAddress;
        inAddress.QuadPart = 0x20000;

        DWORD NumberOfBytesWritten = 0;
        QByteArray buf1 = QByteArray::fromHex("01 E0 34 12");
        QByteArray buf2 = QByteArray::fromHex("00 D0 34 12");
        QByteArray buf3 = QByteArray::fromHex("00 E0 34 12");
        QByteArray buf4 = QByteArray::fromHex("00 D0 34 12");
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf1.constData(), buf1.size(), &NumberOfBytesWritten, NULL);
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf2.constData(), buf2.size(), &NumberOfBytesWritten, NULL);
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf3.constData(), buf3.size(), &NumberOfBytesWritten, NULL);
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf4.constData(), buf4.size(), &NumberOfBytesWritten, NULL);

        //3.继续查询DDR和RAM状态
        //xdma_rw.exe user read 0 -l 1
        //若返回1，表示DDR和RAM写满了，可以进行读操作
        DWORD nNumberOfBytesRead = 0;
        quint8 buffer = 0;
        inAddress.QuadPart = 0;
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);

        if (!ReadFile(mUserHandle, &buffer, 1, &nNumberOfBytesRead, NULL)) {
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
        }
        else{
            if (buffer == 0x01)
                return true;
        }

        QThread::usleep(mTimeout);
        continue;
    }

    return false;
}

/**
* @function name:resetReadflag
* @brief 清空DDR和RAM满状态
* @param[in]
* @param[out]
* @return           bool
*/
bool CaptureThread::emptyStatus()
{
    // 5.PC读取完成，清空DDR和RAM满状态
    // xdma_rw.exe user write 0x20000 0x01 0xF0 0x34 0x12
    // xdma_rw.exe user write 0x20000 0x00 0xD0 0x34 0x12
    while (1){
        if (this->isInterruptionRequested())
            break;

        LARGE_INTEGER inAddress;
        inAddress.QuadPart = 0x20000;

        DWORD NumberOfBytesWritten = 0;
        QByteArray buf1 = QByteArray::fromHex("01 F0 34 12");
        QByteArray buf2 = QByteArray::fromHex("00 D0 34 12");

        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf1.constData(), buf1.size(), &NumberOfBytesWritten, NULL);
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf2.constData(), buf2.size(), &NumberOfBytesWritten, NULL);

        // 6.获取查询DDR和RAM状态
        // xdma_rw.exe user read 0 -l 1
        // 若返回0，则准备下一次测量
        DWORD nNumberOfBytesRead = 0;
        quint8 buffer = 0;
        inAddress.QuadPart = 0;
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);

        if (!ReadFile(mUserHandle, &buffer, 1, &nNumberOfBytesRead, NULL)) {
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
        }
        else{
            if (buffer == 0x00)
                return true;
        }

        QThread::usleep(mTimeout);
        continue;
    }

    return false;
}

/**
* @function name:resetReadflag
* @brief 重置可读写标识
* @param[in]
* @param[out]
* @return           bool
*/
bool CaptureThread::resetReadflag()
{
    // 7.再发一次“开始测量”，用于复位
    // xdma_rw.exe user write 0x20000 0x01 0xE0 0x34 0x12
    // xdma_rw.exe user write 0x20000 0x00 0xD0 0x34 0x12
    // xdma_rw.exe user write 0x20000 0x00 0xE0 0x34 0x12
    // xdma_rw.exe user write 0x20000 0x00 0xD0 0x34 0x12

    while(1){
        LARGE_INTEGER inAddress;
        inAddress.QuadPart = 0x20000;

        DWORD NumberOfBytesWritten = 0;
        QByteArray buf1 = QByteArray::fromHex("01 E0 34 12");
        QByteArray buf2 = QByteArray::fromHex("00 D0 34 12");
        QByteArray buf3 = QByteArray::fromHex("00 E0 34 12");
        QByteArray buf4 = QByteArray::fromHex("00 D0 34 12");

        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf1.constData(), buf1.size(), &NumberOfBytesWritten, NULL);
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf2.constData(), buf2.size(), &NumberOfBytesWritten, NULL);
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf3.constData(), buf3.size(), &NumberOfBytesWritten, NULL);
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);
        WriteFile(mUserHandle, buf4.constData(), buf4.size(), &NumberOfBytesWritten, NULL);

        // 8.继续查询DDR和RAM状态
        // xdma_rw.exe user read 0 -l 1
        // 若返回0，则进行下一次测量
        DWORD nNumberOfBytesRead = 0;
        quint8 buffer = 0;
        inAddress.QuadPart = 0;
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);

        if (!ReadFile(mUserHandle, &buffer, 1, &nNumberOfBytesRead, NULL)) {
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
        }
        else{
            if (buffer == 0x00)
                return true;
        }

        QThread::usleep(mTimeout);
        continue;
    }

    return false;
}

/**
* @function name:readRawData
* @brief 从DDR读波形原始数据
* @param[in]        data
* @param[out]       data
* @return           void
*/
bool CaptureThread::readRawData(QByteArray& data)
{
    DWORD nNumberOfBytesToRead = 0x0BEBC200;//200000000
    DWORD nNumberOfBytesRead = 0;

    LARGE_INTEGER inAddress;
    inAddress.QuadPart = 0;
    SetFilePointerEx(mDeviceHandle, inAddress, NULL, FILE_BEGIN);

    if (!ReadFile(mDeviceHandle, data.data(), nNumberOfBytesToRead, &nNumberOfBytesRead, NULL)) {
        qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
        return false;
    }

    return true;
}

/**
* @function name:readSpectrumData
* @brief 从RAM读能谱数据
* @param[in]        data
* @param[out]       data
* @return           bool
*/
bool CaptureThread::readSpectrumData(QByteArray& data)
{
    DWORD nNumberOfBytesToRead = 0xc800;//1024
    DWORD nNumberOfBytesRead = 0;

    LARGE_INTEGER inAddress;
    inAddress.QuadPart = 0;
    SetFilePointerEx(mBypassHandle, inAddress, NULL, FILE_BEGIN);

    if (!ReadFile(mBypassHandle, data.data(), nNumberOfBytesToRead, &nNumberOfBytesRead, NULL)) {
        qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
        return false;
    }

    return true;
}
