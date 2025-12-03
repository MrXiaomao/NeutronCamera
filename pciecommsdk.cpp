#include "pciecommsdk.h"
#include <math.h>
#include <strsafe.h>
#include <QDateTime>
#include <QDir>
#include <QDebug>

PCIeCommSdk::PCIeCommSdk(QObject *parent)
    : QObject{parent}
{
    mDevices = enumDevices();
    qDebug() << QString("Devices found: %1").arg(mDevices.count());
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

    CaptureThread *captureThread = new CaptureThread(cardIndex, mMapDevice[cardIndex], fileSavePath, captureTimeSeconds);
    captureThread->setPriority(QThread::Priority::HighestPriority);
    connect(captureThread, &CaptureThread::reportCaptureFinished, this, [=](quint32 index){
        mMapCaptureThread.remove(index);
        mThreadRunning[index] = false;
        if (!mThreadRunning[1] && !mThreadRunning[2] && !mThreadRunning[3])
            emit reportCaptureFinished();
    });
    connect(captureThread, &CaptureThread::reportFileReadElapsedtime, this, &PCIeCommSdk::reportFileReadElapsedtime);
    connect(captureThread, &CaptureThread::reportFileWriteElapsedtime, this, &PCIeCommSdk::reportFileWriteElapsedtime);
    connect(captureThread, QOverload<quint32,quint32,QByteArray&>::of(&CaptureThread::reportCaptureData), this, &PCIeCommSdk::replyCaptureData);

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

bool inTimestampRange(quint64 timestamp, quint64 timestamp_start, quint64 timestamp_stop)
{
    return timestamp*1000000>=timestamp_start && timestamp*1000000<timestamp_stop;
}

#include <QtEndian>
void PCIeCommSdk::replyCaptureData(quint8 cardIndex/*PCIe卡序号*/, quint32 captureRef/*包序号*/, QByteArray& data)
{
    // 根据相机序号，计算起始相机通道号
    quint8 cameraFrom = (cardIndex-1) * CAMNUMBER_DDR_PER + 1;
    quint8 cameraTo = cardIndex * CAMNUMBER_DDR_PER;
    quint8 cameraOrientation = 0;
    quint8 currentNo = 0;//当前通道序号(0开始)
    // if (mHorCameraIndex >= cameraFrom && mHorCameraIndex <= cameraTo){
    //     currentNo = mHorCameraIndex - (cardIndex-1)*4;
    //     cameraOrientation = CameraOrientation::Horizontal;
    // }
    // else if (mVerCameraIndex >= cameraFrom && mVerCameraIndex <= cameraTo){
    //     currentNo = mVerCameraIndex - (cardIndex-1)*4;
    //     cameraOrientation = CameraOrientation::Vertical;
    // }
    // else
    //     return;

    //根据通道号计算对应采集卡的第几通道
    {
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
        if (data.startsWith(QByteArray::fromHex("FFAB00D3"))){
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
        if (data.startsWith(QByteArray::fromHex("FFAB00D2"))){
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

                //数据包的其实时间戳/ns
                if (serialNumberRef == 1){// 只显示第1个能谱数据
                    quint64 timestamp_start = (serialNumber - 1) * 50;
                    quint64 timestamp_stop = captureRef * 50;
                    quint64 mTimestampMs[] = {mTimestampMs1};
                    for (int i=0; i<1; ++i){
                        if (inTimestampRange(mTimestampMs[i], timestamp_start, timestamp_stop)){
                            QByteArray gamma = chunk.mid(currentNo*124, 124);  //62*4*2
                            QByteArray neutron = chunk.mid(currentNo*124, 124);
                            {
                                QVector<quint16> data;
                                for (int i=0; i<gamma.size(); i+=2){
                                    bool ok;
                                    //每个通道的每个数据点还要除以4，才得到最后的全采样的数据值
                                    quint16 amplitude = gamma.mid(i, 2).toHex().toUShort(&ok, 16);
                                    data.append(qbswap(amplitude));
                                }

                                emit reportGammaSpectrum(i+1, cameraOrientation, data);
                            }

                            {
                                QVector<quint16> data;
                                for (int i=0; i<neutron.size(); i+=2){
                                    bool ok;
                                    //每个通道的每个数据点还要除以4，才得到最后的全采样的数据值
                                    quint16 amplitude = neutron.mid(i, 2).toHex().toUShort(&ok, 16);
                                    data.append(qbswap(amplitude));
                                }

                                emit reportNeutronSpectrum(i+1, cameraOrientation, data);
                            }
                        }
                    }
                }
            }
        }
    }

    //旧数据包解析方法，待废弃使用......
    {
        //采集ns总时间是：1024*1024*32*2ns,正好等于单个通道的数据总长度64MB,等于是1ns对应一个字节的数据
        quint32 capture_timelength = 1024 * 1024 * 32 * 2;

        //数据包的其实时间戳/ns
        captureRef = (mTimestampMs1 + 49) / 50;
        quint64 timestamp_start = (captureRef - 1) * capture_timelength;
        quint64 timestamp_stop = captureRef * capture_timelength;

        //判断时间戳范围
        quint64 mTimestampMs[] = {mTimestampMs1};
        for (int i=0; i<1; ++i){
            //if (inTimestampRange(mTimestampMs[i], timestamp_start, timestamp_stop))
            {
                //将时间戳ms换算成ns
                quint64 cut_timestamp_start = mTimestampMs[i] * 1000 * 1000 - timestamp_start;

                //256M分成4个通道，每个通道64M，
                quint32 chunk_size = capture_timelength;
                QByteArray chunk_channel = data.mid(currentNo*chunk_size, chunk_size);

                //根据时间戳范围截取需要的数据段，1ms对应数据长度就是1*1000*1000ns，这里只显示1us数据吧
                QByteArray chunk_time = data.mid(cut_timestamp_start, 1000);;
                chunk_time.reserve(1000);

                //数据流类型是int16
                QVector<quint16> waveform;
                for (int i=0; i<chunk_time.size(); i+=2){
                    bool ok;
                    //每个通道的每个数据点还要除以4，才得到最后的全采样的数据值
                    quint16 amplitude = chunk_time.mid(i, 2).toHex().toUShort(&ok, 16);
                    waveform.append(qbswap(amplitude) / 4);
                }

                emit reportWaveform(i+1, cameraOrientation, waveform);
            }
        }
    }
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

/*设置采集参数*/
void PCIeCommSdk::setCaptureParamter(quint8 cameraIndex, quint32 time1)
{
    mCameraIndex = cameraIndex;
    mTimestampMs1 = time1;
}

#include <QRegularExpression>
bool PCIeCommSdk::openHistoryData(QString filename)
{
    QFile file(filename);
    if (file.open(QIODevice::ReadWrite)){
        QByteArray data = file.readAll();            
        replyCaptureData(mCameraIndex, mTimestampMs1, data);
        file.close();
        return true;
    }
    else{
        return false;
    }
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
        }
    }

    for (int cardIndex = 1; cardIndex <= numberOfDevices(); ++cardIndex){
        mMapPower[cardIndex] = true;
        mMapVoltage[cardIndex] = true;
        mMapChannel[cardIndex] = true;
    }
}
