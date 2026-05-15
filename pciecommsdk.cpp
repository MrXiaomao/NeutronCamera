#include "pciecommsdk.h"
#include <math.h>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include "datacompresswindow.h"

#ifdef _WIN32
#define	XDMA_FILE_USER		"\\user"
#define	XDMA_FILE_CONTROL	"\\control"
#define XDMA_FILE_BYPASS	"\\bypass"
#define	XDMA_FILE_H2C_0		"\\h2c_0"
#define	XDMA_FILE_H2C_1		"\\h2c_1"
#define	XDMA_FILE_H2C_2		"\\h2c_2"
#define	XDMA_FILE_H2C_3		"\\h2c_3"
#define	XDMA_FILE_C2H		"\\c2h"
#define	XDMA_FILE_C2H_0		"\\c2h_0"
#define	XDMA_FILE_C2H_1		"\\c2h_1"
#define	XDMA_FILE_C2H_2		"\\c2h_2"
#define	XDMA_FILE_C2H_3		"\\c2h_3"
#define	XDMA_FILE_EVENT_0	"\\event_0"
#define	XDMA_FILE_EVENT_1	"\\event_1"
#define	XDMA_FILE_EVENT_2	"\\event_2"
#define	XDMA_FILE_EVENT_3	"\\event_3"
#else
#define	XDMA_FILE_USER		"_user"
#define	XDMA_FILE_CONTROL	"_control"
#define XDMA_FILE_BYPASS	"_bypass"
#define	XDMA_FILE_H2C_0		"_h2c_0"
#define	XDMA_FILE_H2C_1		"_h2c_1"
#define	XDMA_FILE_H2C_2		"_h2c_2"
#define	XDMA_FILE_H2C_3		"_h2c_3"
#define	XDMA_FILE_C2H_0		"_c2h_0"
#define	XDMA_FILE_C2H_1		"_c2h_1"
#define	XDMA_FILE_C2H_2		"_c2h_2"
#define	XDMA_FILE_C2H_3		"_c2h_3"
#define	XDMA_FILE_EVENT_0	"_event_0"
#define	XDMA_FILE_EVENT_1	"_event_1"
#define	XDMA_FILE_EVENT_2	"_event_2"
#define	XDMA_FILE_EVENT_3	"_event_3"
#endif

PCIeCommSdk::PCIeCommSdk(QObject *parent)
    : QObject{parent}
{
    QStringList args = QCoreApplication::arguments();
    if (args.size() == 1){
        mDevices = enumDevices();
        initCaptureThreads();
    }
}


PCIeCommSdk::~PCIeCommSdk()
{
    for (int cardIndex = 1; cardIndex <= numberOfDevices(); ++cardIndex){
        auto iter = mMapCaptureThread.find(cardIndex);
        if (iter != mMapCaptureThread.end()){
            CaptureThread *captureThread = iter.value();
            captureThread->stop();
            captureThread->quit();
            captureThread = nullptr;
            mMapCaptureThread.remove(cardIndex);
        }
    }
}

QStringList PCIeCommSdk::enumDevices()
{
#ifdef _WIN32
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

    if (lstDevices.size() == 0)
        lstDevices << "/dev/xdma0" << "/dev/xdma1" << "/dev/xdma2";

    return lstDevices;
#else
    QStringList lstDevices;
    for (int i=0; i<=3; ++i)
    {
        if (QFileInfo::exists(QString("/dev/xdma%1_user").arg(i)))
            lstDevices << QString("/dev/xdma%1").arg(i);
    }

    return QStringList() << lstDevices;//"/dev/xdma0" << "/dev/xdma1";// << "/dev/xdma2";
#endif
}


quint32 PCIeCommSdk::numberOfDevices()
{
    return mDevices.count();
}

#ifdef _WIN32
#include <processtopologyapi.h> //SetThreadGroupAffinity
#endif
void PCIeCommSdk::startCapture(quint32 index, QString fileSavePath, quint32 captureTimeSeconds, QString shotNum/*炮号*/)
{
    auto iter = mMapCaptureThread.find(index);
    if (iter != mMapCaptureThread.end()){
        //QString devicePath = mDevices.at(index - 1) + XDMA_FILE_C2H + QStringLiteral().arg();
        // if (i==0)
        //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&B189E7&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";
        // else if (i==1)
        //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&10D89B97&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";
        // else if (i==2)
        //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&18ec5e6b&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";

        mMapCaptureThread[index]->setParamter(fileSavePath, captureTimeSeconds);

        //启动写文件线程
        mThreadRunning[index] = true;
        if (mMapCaptureThread[index]->isRunning())
            mMapCaptureThread[index]->resume();
        else
            mMapCaptureThread[index]->start();
    }
}

void PCIeCommSdk::stopCapture(quint32 index)
{
    auto iter = mMapCaptureThread.find(index);
    if (iter != mMapCaptureThread.end()){
        CaptureThread *captureThread = iter.value();
        captureThread->pause();
    }
}

void PCIeCommSdk::startAllCapture(QString fileSavePath, quint32 captureTimeSeconds,  QString shotNum/*炮号*/)
{
    //initCaptureThreads();

#if ENABLE_DDR2
    for (int index = 1; index <= numberOfDevices() * 2; ++index)
#else
    for (int index = 1; index <= numberOfDevices(); ++index)
#endif
    {
        startCapture(index, fileSavePath, captureTimeSeconds, shotNum);
    }
}

void PCIeCommSdk::stopAllCapture()
{
#if ENABLE_DDR2
    for (int index = 1; index <= numberOfDevices() * 2; ++index)
#else
    for (int index = 1; index <= numberOfDevices(); ++index)
#endif
    {
        stopCapture(index);
    }
}

void PCIeCommSdk::init()
{
    for (int cardIndex = 1; cardIndex <= mDevices.size(); ++cardIndex){
        if (!mMapCaptureThread.contains(cardIndex))
            continue;

        PCIeCommSdk::writeData(cardIndex, mMapCaptureThread[cardIndex]->userHandle(), 0x20000, QByteArray::fromHex("30 FA 34 12"));
        ::QThread::msleep(100);
        PCIeCommSdk::writeData(cardIndex, mMapCaptureThread[cardIndex]->userHandle(), 0x20000, QByteArray::fromHex("00 00 00 00"));
        ::QThread::msleep(100);
    }
}

void PCIeCommSdk::reset()
{
    for (int cardIndex = 1; cardIndex <= mDevices.size(); ++cardIndex){
        if (!mMapCaptureThread.contains(cardIndex))
            continue;

        writeData(cardIndex, mMapCaptureThread[cardIndex]->userHandle(), 0x20000, QByteArray::fromHex("02 D0 34 12"));//数采
        QThread::msleep(100);
        writeData(cardIndex, mMapCaptureThread[cardIndex]->userHandle(), 0x20000, QByteArray::fromHex("00 00 00 00"));
        QThread::msleep(100);
        writeData(cardIndex, mMapCaptureThread[cardIndex]->userHandle(), 0x20000, QByteArray::fromHex("01 D0 34 12"));//DDR
        QThread::msleep(100);
        writeData(cardIndex, mMapCaptureThread[cardIndex]->userHandle(), 0x20000, QByteArray::fromHex("00 00 00 00"));

        // 初始化，设置测量时间
        PCIeCommSdk::writeData(cardIndex, mMapCaptureThread[cardIndex]->userHandle(), 0x20000, QByteArray::fromHex("30 FA 34 12"));//16-160ms 18-800ms 20-4000ms
        ::QThread::msleep(100);
        PCIeCommSdk::writeData(cardIndex, mMapCaptureThread[cardIndex]->userHandle(), 0x20000, QByteArray::fromHex("00 00 00 00"));
        ::QThread::msleep(100);
    }

    qInfo().nospace() << "复位完成";
}

bool PCIeCommSdk::test()
{
    // QByteArray buffer;
    // replyCaptureWaveformData(1, 1, buffer);
    bool allOk = true;
    for (int index = 1; index <= mDevices.size()*2; ++index){
        if (mMapCaptureThread[index]->dataExistError())
            allOk = false;
    }

    return allOk;
}

void PCIeCommSdk::printDataError()
{
    for (int index = 1; index <= mDevices.size()*2; ++index){
        mMapCaptureThread[index]->printDebugInfo();
    }
}

#include <QtEndian>
/* 在线数据分析
 *
*/
void PCIeCommSdk::replyCaptureWaveformData(quint8 cardIndex/*PCIe卡序号*/, bool isDDR1, quint32 currentPackIndex/*包时间ms*/, const QByteArray& waveformData)
{
    // 根据相机序号，计算起始相机通道号
    quint8 cameraFrom = (cardIndex==1 ? 1 : (cardIndex == 2 ? 7 : 13));
    cameraFrom += (isDDR1 ? 0 : CAMNUMBER_DDR_PER);
    quint8 cameraTo = cameraFrom + CAMNUMBER_DDR_PER - 1;
    quint8 cameraNo = 0;//当前通道序号(0开始)
    quint8 cameraIndex = 0;
    if (mHorCameraIndex >= cameraFrom && mHorCameraIndex <= cameraTo){
        cameraNo = (mHorCameraIndex - 1) % CAMNUMBER_DDR_PER;
    }
    else if (mVerCameraIndex >= cameraFrom && mVerCameraIndex <= cameraTo){
        cameraNo = (mVerCameraIndex - 1) % CAMNUMBER_DDR_PER;
    }
    else
        return;

    cameraIndex = cameraFrom + cameraNo;

    /* 原始数据格式(总大小120000000B)
     * 包头              FFAB
     * 数据类型           00D3
     * 包序号             0000
     * 测量时间           0000
     * 包头时间戳         0000 0000 0000 0000
     * 原始数据(B  )      120,000,000-32
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
        for (quint8 timeIndex=1; timeIndex<=mTimestampMs.size(); ++timeIndex){
            quint32 packIndex = mTimestampMs[timeIndex-1] / PACKET_TIMELENGTH + 1;
            //quint8 pac kPos = mTimestampMs[timeIndex-1] % 50;
            if (currentPackIndex == packIndex){
                bool ok;
                QByteArray head = waveformHead.left(4);
                quint16 serialNumber = qbswap(waveformHead.mid(4, 2).toHex().toUShort(&ok, 16));//包序号
                quint16 time = qbswap(waveformHead.mid(6, 2).toHex().toUShort(&ok, 16));//测量时间
                quint16 headTimeStamp = qbswap(waveformHead.mid(8, 8).toHex().toULongLong(&ok, 16));//包头时间戳
                quint16 tailTimeStamp = qbswap(waveformTai.left(8).toHex().toULongLong(&ok, 16));//包头时间戳
                QByteArray reserve = waveformTai.mid(8, 6);
                QByteArray tail = waveformTai.right(2);

                QByteArray chunk = waveformData.mid(16, 120000000-32);
                QVector<qint16> ch[3];
                if (!DataAnalysisWorker::readBin3Ch_fast(chunk, ch[0], ch[1], ch[2], true)) {
                    return;
                }

                // 这里借用解析历史数据的代码
                quint32 timeLength = PACKET_TIMELENGTH;
                quint32 captureTime = PACKET_TIMELENGTH;
                quint32 remainTime = mTimestampMs[timeIndex-1] % captureTime;

                //计算出是当前文件波形的第几个数据点
                int index = remainTime * 1000 * 1000/ 2;
                int point_num = timeLength * 1000 * 1000 / 2;
                //根据相机序号计算出是第几块光纤卡
                int board_index = (cameraIndex-1)/4+1;

                //扣基线，调整数据
                qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[cameraNo]);
                DataAnalysisWorker::adjustDataWithBaseline(ch[cameraNo], baseline_ch, board_index, cameraNo + 1);
                //提取通道号的数据cameraNo
                QVector<qint16> waveform = ch[cameraNo].mid(index, point_num);

                //数据流类型是int16
                QVector<QPair<double, double>> waveformPair;
                for (int k=0; k<ch[cameraNo].size(); ++k){
                    waveformPair.append(qMakePair(k*2, ch[cameraNo][k]));
                }

                emit doWaveform(timeIndex, cameraIndex, waveformPair);
            }
        }
    }
}

void PCIeCommSdk::replyCaptureSpectrumData(quint8 cardIndex/*PCIe卡序号*/, bool isDDR1, quint32 currentPackIndex/*包时间ms*/, const QByteArray& spectrumData)
{
    // 根据相机序号，计算起始相机通道号
    quint8 cameraFrom = (cardIndex==1 ? 1 : (cardIndex == 2 ? 7 : 13));
    cameraFrom += (isDDR1 ? 0 : CAMNUMBER_DDR_PER);
    quint8 cameraTo = cameraFrom + CAMNUMBER_DDR_PER - 1;
    quint8 cameraNo = 0;//当前通道序号(0开始)
    quint8 cameraIndex = 0;
    if (mHorCameraIndex >= cameraFrom && mHorCameraIndex <= cameraTo){
        cameraNo = (mHorCameraIndex - 1) % CAMNUMBER_DDR_PER;
    }
    else if (mVerCameraIndex >= cameraFrom && mVerCameraIndex <= cameraTo){
        cameraNo = (mVerCameraIndex - 1) % CAMNUMBER_DDR_PER;
    }
    else
        return;

    cameraIndex = cameraFrom + cameraNo;
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
    QByteArray reverseData = PCIeCommSdk::reverseArray(spectrumData, 16);
    if (reverseData.startsWith(QByteArray::fromHex("FFAB00D2")))
    {
        //数据包的其实时间戳/ns
        for (quint8 timeIndex=1; timeIndex<=mTimestampMs.size(); ++timeIndex){
            quint32 packIndex = mTimestampMs[timeIndex-1] / PACKET_TIMELENGTH + 1;
            quint32 packPos = mTimestampMs[timeIndex-1] % PACKET_TIMELENGTH - 1;
            if (currentPackIndex == packIndex){
                QByteArray chunk = reverseData.mid(packPos*1024, 1024);

                QByteArray chunkHead = chunk.left(16);
                //std::reverse(chunkHead.begin(), chunkHead.end());
                QByteArray chunkTail = chunk.right(16);
                //std::reverse(chunkTail.begin(), chunkTail.end());

                bool ok;
                //能谱序号 高位16bi表示这是 第几个50ms的数据，低位8bit表示，在每一个50ms里，这是第几个能谱数据（范围是1-50）
                QByteArray head = chunk.left(4);
                quint32 serialNumber = chunk.mid(4, 2).toHex().toUInt(&ok, 16);
                quint32 serialNumberRef = chunk.mid(6, 1).toInt();
                quint16 time = chunk.mid(7, 2).toHex().toUShort(&ok, 16);//测量时间
                QByteArray reserve1 = chunk.mid(9, 7);

                QByteArray chunkgamma = chunk.mid(16, 496);  //62*4*2
                QByteArray chunkneutron = chunk.mid(512, 496);

                quint64 absoluteTime = chunk.mid(1008, 8).toHex().toUInt(&ok, 16);//绝对时间
                QByteArray reserve2 = chunk.mid(1016, 6);
                QByteArray tail = chunk.right(2);

                //数据包的起始时间戳/ns
                //quint64 timestamp = (serialNumber - 1) * 50 + serialNumberRef;
                //if (timestamp == mTimestampMs1)
                {
                    QByteArray gamma = chunkgamma.mid(cameraNo*124, 124);
                    QByteArray neutron = chunkgamma.mid(cameraNo*124, 124);
                    {
                        QVector<QPair<double, double>> data;
                        for (int i=0; i<gamma.size(); i+=2){
                            bool ok;
                            quint16 amplitude = gamma.mid(i, 2).toHex().toUShort(&ok, 16);
                            data.append(qMakePair(i/2, amplitude));
                        }

                        qDebug() << "reportGammaSpectrum" << timeIndex << cameraIndex;

                        emit reportGammaSpectrum(timeIndex, cameraIndex, data);
                    }

                    {
                        QVector<QPair<double, double>> data;
                        for (int i=0; i<neutron.size(); i+=2){
                            bool ok;
                            quint16 amplitude = neutron.mid(i, 2).toHex().toUShort(&ok, 16);
                            data.append(qMakePair(i/2, amplitude));
                        }

                        emit reportNeutronSpectrum(timeIndex, cameraIndex, data);
                    }
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
bool PCIeCommSdk::analyzeHistorySpectrumData(quint8 cameraIndex, quint8 timeIndex, quint32 remainTime, QString filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray spectrumData = f.readAll();
    f.close();

    //根据通道号计算对应采集卡的第几通道
    quint8 cameraNo = (cameraIndex - 1) % CAMNUMBER_DDR_PER;

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
    QByteArray reverseData = PCIeCommSdk::reverseArray(spectrumData, 16);
    if (reverseData.startsWith(QByteArray::fromHex("FFAB00D2")))
    {
        //数据包的其实时间戳/ns
        for (quint8 timeIndex=1; timeIndex<=mTimestampMs.size(); ++timeIndex){
            quint32 packIndex = mTimestampMs[timeIndex-1] / PACKET_TIMELENGTH + 1;
            quint32 packPos = mTimestampMs[timeIndex-1] % PACKET_TIMELENGTH;
            {
                QByteArray chunk = reverseData.mid(packPos*1024, 1024);

                QByteArray chunkHead = chunk.left(16);
                //std::reverse(chunkHead.begin(), chunkHead.end());
                QByteArray chunkTail = chunk.right(16);
                //std::reverse(chunkTail.begin(), chunkTail.end());

                bool ok;
                //能谱序号 高位16bi表示这是 第几个50ms的数据，低位8bit表示，在每一个50ms里，这是第几个能谱数据（范围是1-50）
                QByteArray head = chunk.left(4);
                quint32 serialNumber = chunk.mid(4, 2).toHex().toUInt(&ok, 16);
                quint32 serialNumberRef = chunk.mid(6, 1).toInt();
                quint16 time = chunk.mid(7, 2).toHex().toUShort(&ok, 16);//测量时间
                QByteArray reserve1 = chunk.mid(9, 7);

                QByteArray chunkgamma = chunk.mid(16, 496);  //62*4*2
                QByteArray chunkneutron = chunk.mid(512, 496);

                quint64 absoluteTime = chunk.mid(1008, 8).toHex().toUInt(&ok, 16);//绝对时间
                QByteArray reserve2 = chunk.mid(1016, 6);
                QByteArray tail = chunk.right(2);

                //数据包的起始时间戳/ns
                //quint64 timestamp = (serialNumber - 1) * 50 + serialNumberRef;
                //if (timestamp == mTimestampMs1)
                {
                    QByteArray gamma = chunkgamma.mid(cameraNo*124, 124);
                    QByteArray neutron = chunkgamma.mid(cameraNo*124, 124);
                    {
                        QVector<QPair<double, double>> data;
                        for (int i=0; i<gamma.size(); i+=2){
                            bool ok;
                            quint16 amplitude = gamma.mid(i, 2).toHex().toUShort(&ok, 16);
                            data.append(qMakePair(i/2, amplitude));
                        }

                        qDebug() << "reportGammaSpectrum" << timeIndex << cameraIndex;

                        emit reportGammaSpectrum(timeIndex, cameraIndex, data);
                    }

                    {
                        QVector<QPair<double, double>> data;
                        for (int i=0; i<neutron.size(); i+=2){
                            bool ok;
                            quint16 amplitude = neutron.mid(i, 2).toHex().toUShort(&ok, 16);
                            data.append(qMakePair(i/2, amplitude));
                        }

                        emit reportNeutronSpectrum(timeIndex, cameraIndex, data);
                    }
                }
            }
        }
    }
    // QByteArray reverseData = DataCachPoolThread::reverseArray(spectrumData, 16);
    // if (reverseData.startsWith(QByteArray::fromHex("FFAB00D2"))){
    //     if (reverseData.size() >= 1024*PACKET_TIMELENGTH){
    //         quint32 packIndex = mTimestampMs[timeIndex-1] / PACKET_TIMELENGTH + 1;
    //         quint32 packPos = mTimestampMs[timeIndex-1] % PACKET_TIMELENGTH - 1;
    //         QByteArray chunk = reverseData.mid(packPos*1024, 1024);

    //         QByteArray chunkHead = chunk.left(16);
    //         //std::reverse(chunkHead.begin(), chunkHead.end());
    //         QByteArray chunkTail = chunk.right(16);
    //         //std::reverse(chunkTail.begin(), chunkTail.end());

    //         bool ok;
    //         //能谱序号 高位16bi表示这是 第几个50ms的数据，低位8bit表示，在每一个50ms里，这是第几个能谱数据（范围是1-50）
    //         QByteArray head = chunk.left(4);
    //         quint32 serialNumber = chunk.mid(4, 2).toHex().toUInt(&ok, 16);
    //         quint32 serialNumberRef = chunk.mid(6, 1).toInt();
    //         quint16 time = chunk.mid(7, 2).toHex().toUShort(&ok, 16);//测量时间
    //         QByteArray reserve1 = chunk.mid(9, 7);

    //         QByteArray chunkgamma = chunk.mid(16, 496);  //62*4*2
    //         QByteArray chunkneutron = chunk.mid(512, 496);

    //         quint64 absoluteTime = chunk.mid(1008, 8).toHex().toUInt(&ok, 16);//绝对时间
    //         QByteArray reserve2 = chunk.mid(1016, 6);
    //         QByteArray tail = chunk.right(2);

    //         //数据包的起始时间戳/ns
    //         //quint64 timestamp = (serialNumber - 1) * 50 + serialNumberRef;
    //         //if (timestamp == mTimestampMs1)
    //         {
    //             QByteArray gamma = chunkgamma.mid(cameraNo*124, 124);
    //             QByteArray neutron = chunkgamma.mid(cameraNo*124, 124);
    //             {
    //                 QVector<QPair<double, double>> data;
    //                 for (int i=0; i<gamma.size(); i+=2){
    //                     bool ok;
    //                     quint16 amplitude = gamma.mid(i, 2).toHex().toUShort(&ok, 16);
    //                     data.append(qMakePair(i/2, amplitude));
    //                 }

    //                 emit reportGammaSpectrum(timeIndex, cameraIndex, data);
    //             }

    //             {
    //                 QVector<QPair<double, double>> data;
    //                 for (int i=0; i<neutron.size(); i+=2){
    //                     bool ok;
    //                     quint16 amplitude = neutron.mid(i, 2).toHex().toUShort(&ok, 16);
    //                     data.append(qMakePair(i/2, amplitude));
    //                 }

    //                 emit reportNeutronSpectrum(timeIndex, cameraIndex, data);
    //             }
    //         }
    //     }
    // }
}

/**
 * @brief PCIeCommSdk::analyzeHistoryWaveformData 离线分析波形历史数据，兼容旧版和新版FPGA数据协议
 * 波形数据排列方式：ch3, ch3, ch2, ch2, ch0, ch0, ch1, ch1...（逐点交织）
 * @param cameraIndex 相机序号
 * @param timeLength 要提取的波形时间长度，单位ms
 * @param remainTime 剩余时间，用于计算采集时刻 
 * @param filePath 历史数据文件路径
 */
bool PCIeCommSdk::analyzeHistoryWaveformData(quint8 cameraIndex, quint32 timeLength, quint32 remainTime, QString filePath)
{
    //根据通道号计算对应采集卡的第几通道
    quint8 cameraNo = (cameraIndex - 1) % CAMNUMBER_DDR_PER;
    QVector<qint16> ch[3];
    if (!DataAnalysisWorker::readBin3Ch_fast(filePath, ch[0], ch[1], ch[2], true)) {
        // emit logMessage(QString("文件%1: 读取失败或文件不存在").arg(filePath), QtWarningMsg);
        qDebug() << "文件" << filePath << "读取失败或文件不存在";
        return false;
    }

    //计算出是当前文件波形的第几个数据点
    int index = remainTime * 1000 * 1000/ 2;
    int point_num = timeLength * 1000 * 1000 / 2;
    //根据相机序号计算出是第几块光纤卡
    int board_index = (cameraIndex-1)/CAMNUMBER_DDR_PER+1;

    //扣基线，调整数据
    qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[cameraNo]);
    DataAnalysisWorker::adjustDataWithBaseline(ch[cameraNo], baseline_ch, board_index, cameraNo + 1);
    //提取通道号的数据cameraNo
    QVector<qint16> waveform = ch[cameraNo].mid(index, point_num);

    QVector<QPair<double,double>> waveformPair;
    for (int i=0;i<waveform.size();++i)
        waveformPair.push_back(qMakePair(i*2, waveform[i]));
    emit doWaveform(1, cameraIndex, waveformPair);

    return true;
}

#include "globalsettings.h"
bool PCIeCommSdk::analyzeHistoryWaveformData(const quint8& cameraIndex,
                                        const quint32& timeStart/*开始时刻ms*/,
                                        const quint32& timeStop/*结束时刻ms*/,
                                        const QString& fileDir/*文件存储路径*/,
                                        std::function<void(const QMap<quint64/*时刻（ns）*/,qint16/*数值*/>&)> callback
                                        )
{
    QMap<quint64/*时刻*/,qint16/*数值*/> waveformPair;

    //根据开始时间和结束时间，过滤掉不在时间范围内的文件
    int startFileId = timeStart / PACKET_TIMELENGTH + 1;
    int endFileId = startFileId + (timeStop - timeStart - 1) / PACKET_TIMELENGTH;
    //根据通道号判断是板卡的A面还是B面
    QString sideFile = "B";
    if ((cameraIndex % 6) >= 1 && (cameraIndex % 6) <= 3)
        sideFile = "A";
    //根据通道号计算板卡的索引
    int board_index = (cameraIndex+5) / 6;
    //根据通道号计算对应采集卡的第几通道
    quint8 cameraNo = (cameraIndex - 1) % CAMNUMBER_DDR_PER;
    for (int id = startFileId; id <= endFileId; ++id){
        QString filePath = QString("%1/%2%3data%4.bin").arg(fileDir).arg(board_index).arg(sideFile).arg(id);

        QVector<qint16> ch[3];
        if (DataAnalysisWorker::readBin3Ch_fast(filePath, ch[0], ch[1], ch[2], true)) {

            //计算出是当前文件波形的第几个数据点
            quint32 timeFrom;
            quint32 timeTo;

            if (id == startFileId)
                timeFrom = (timeStart % 40) * 1000 * 1000 / 2;
            else
                timeFrom = 0;

            if (id == endFileId){
                if ((timeStop % 40) == 0)
                    timeTo  = ch[cameraNo].size();
                else
                    timeTo = (timeStop % 40) * 1000 * 1000 / 2;
            }
            else
                timeTo  = ch[cameraNo].size();

            //扣基线，调整数据
            qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[cameraNo]);
            DataAnalysisWorker::adjustDataWithBaseline(ch[cameraNo], baseline_ch, board_index, cameraNo + 1);

            //提取通道号的数据cameraNo
            QVector<qint16> waveform = ch[cameraNo].mid(timeFrom, timeTo-timeFrom);

            for (int i=0;i<waveform.size();++i)
                waveformPair.insert(timeStart * 1000 * 1000 / 2 + i*2, waveform[i]);
        }
    }

    callback(waveformPair);
    return true;
}

bool PCIeCommSdk::analyzeHistoryCpsData(
                                        const quint32 channels/*多道道数*/,
                                        const quint32 timeWidth/*时间宽度ms*/,
                                        const quint32 timeStart/*开始时刻ms*/,
                                        const quint32 timeStop/*结束时刻ms*/,
                                        const QString& filePath/*H5文件路径*/,
                                        std::function<void(QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>>, QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>>)> callback,
                                        const quint32 minPeak/*最小峰值0*/,
                                        const quint32 maxPeak/*最大峰值16384*/
                                        )
{
    //根据通道号计算对应采集卡的第几通道
    QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
    QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
    try {
        H5::H5File file(filePathBytes.toStdString(), H5F_ACC_RDONLY);

        for (int boardNum=1; boardNum<=6; ++boardNum){
            // 创建或打开板卡组
            QString boardGroupName = QString("Board%1").arg(boardNum);

            H5::Group boardGroup;
            htri_t existsGroup = H5Lexists(file.getId(), boardGroupName.toStdString().c_str(), H5P_DEFAULT);

            if (existsGroup > 0) {
                boardGroup = file.openGroup(boardGroupName.toStdString());

                // 辅助函数：写入单个通道的数据集
                auto readChannel = [&](const QString& datasetName, QVector<qint16>& timeTrigger, QVector<qint16>& timePeak) {
                    htri_t existsDataset = H5Lexists(boardGroup.getId(), datasetName.toStdString().c_str(), H5P_DEFAULT);
                    if (existsDataset){
                        std::string ds = datasetName.toUtf8().constData();
                        H5::DataSet dataset = boardGroup.openDataSet(ds);
                        H5::DataSpace fileSpace = dataset.getSpace();

                        // 校验维度
                        int rank = fileSpace.getSimpleExtentNdims();
                        if (rank != 2) return false;
                        hsize_t dims[2];
                        fileSpace.getSimpleExtentDims(dims, nullptr);
                        const hsize_t totalRows = dims[0];

                        std::vector<std::vector<int16_t>> outData;
                        outData.resize(2);//第1列时间毫秒 第2列峰值
                        // 逐列选切片读取：每次选所有行 + 当前1列
                        for (int colIdx = 0; colIdx < 2; colIdx++) {
                            hsize_t offset[2] = {0, (hsize_t)colIdx};
                            hsize_t count[2]  = {totalRows, 1}; // 一次读1列
                            fileSpace.selectHyperslab(H5S_SELECT_SET, count, offset);

                            H5::DataSpace memSpace(1, &totalRows); // 内存空间是一维，长度为总行数
                            outData[colIdx].resize(totalRows);

                            dataset.read(outData[colIdx].data(), H5::PredType::NATIVE_INT16, memSpace, fileSpace);
                        }

                        for (int rowIdx = 0; rowIdx < totalRows; ++rowIdx){
                            qint16 timeMs = static_cast<qint16>(outData[0][rowIdx]);
                            timeTrigger.push_back(timeMs);

                            timePeak.push_back(static_cast<qint16>(outData[1][rowIdx]));
                        }

                        dataset.close();
                    }
                };

                // 写入3个通道的数据
                QVector<qint16> timeTrigger_ch[3], timePeak_ch[3];
                readChannel("wave_ch0",  timeTrigger_ch[0], timePeak_ch[0]);
                readChannel("wave_ch1", timeTrigger_ch[1], timePeak_ch[1]);
                readChannel("wave_ch2", timeTrigger_ch[2], timePeak_ch[2]);

                //根据时间段统计计数率
                QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>> cpsMapPair;
                for (quint8 cameraNo=0; cameraNo<3; ++cameraNo){
                    quint8 cameraIndex = (boardNum-1)*3 + cameraNo + 1;

                    // 1.按照时间段和点位时间间隔分配计数点数组长度
                    quint32 cpsTotal = (timeStop-timeStart)/timeWidth;
                    //cpsMapPair[cameraIndex]. reserve(cpsTotal);
                    //初始化每个时间段内计数率为0
                    for (quint16 intervalNo = 0; intervalNo <= cpsTotal; ++intervalNo) {
                        cpsMapPair[cameraIndex][timeStart + intervalNo * timeWidth] = quint32(0);
                    }

                    // 2. 遍历所有输入数据，分桶统计
                    int i = 0;
                    for (qint32 time : timeTrigger_ch[cameraNo]) {
                        if (time < timeStart)
                            continue;

                        if (time > timeStop)
                            continue;

                        quint64 peak = timePeak_ch[cameraNo][i++];// 能量峰值
                        if (peak >= minPeak && peak <= maxPeak)
                            cpsMapPair[cameraIndex][timeStart + (time - timeStart) / timeWidth * timeWidth] += 1;
                    }
                }

                //根据时间段统计能谱
                QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>> spectrumMapPair;
                for (quint8 cameraNo=0; cameraNo<3; ++cameraNo){
                    quint8 cameraIndex = (boardNum-1)*3 + cameraNo + 1;

                    //初始化每个道址默认能量值为0
                    for (quint16 channel = 0; channel < channels; ++channel) {
                        spectrumMapPair[cameraIndex][channel] = quint32(0);
                    }

                    // 2. 遍历所有时间段内的能量值，分桶统计
                    int i = 0;
                    double channelWidth = (double)(16384/channels);
                    for (qint32 time : timeTrigger_ch[cameraNo]) {
                        if (time < timeStart)
                            continue;

                        if (time > timeStop)
                            continue;

                        quint64 peak = timePeak_ch[cameraNo][i++];// 能量峰值
                        quint16 channel = peak / channelWidth;// 道址
                        spectrumMapPair[cameraIndex][channel] += 1;
                    }
                }

                boardGroup.close();
                callback(cpsMapPair, spectrumMapPair);
            }
        }

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

#include "globalsettings.h"
void PCIeCommSdk::replySettingFinished()
{
    GlobalSettings settings(DEVICE_CONFIG_FILE);
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
void PCIeCommSdk::setCaptureParamter(CaptureTime captureTime, quint8 cameraIndex, quint32 timeLength, quint32 tmMeasure)
{
    mTimestampMs.clear();
    mCaptureTime = captureTime;
    mCameraIndex = cameraIndex;
    mTimestampMs.push_back(tmMeasure);
    mTimeLength = timeLength;
    mRemainTime = tmMeasure % captureTime;
}

/**
 * @brief PCIeCommSdk::setCaptureParamter 设置在线分析时刻参数
 * @param horCameraIndex 水平相机序号
 * @param verCameraIndex 垂直相机序号
 * @param time1 提取能谱起始时刻，单位ms
 * @param time2 提取能谱起始时刻，单位ms
 * @param time3 提取能谱起始时刻，单位ms
 */
void PCIeCommSdk::setCaptureParamter(quint8 horCameraIndex, quint8 verCameraIndex, QVector<quint32> tmMeasure)
{
    mHorCameraIndex = horCameraIndex;
    mVerCameraIndex = verCameraIndex;
    mTimestampMs.swap(tmMeasure);
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
    for (int cardIndex = 1; cardIndex <= mDevices.size(); ++cardIndex){
        if (!mMapCaptureThread.contains(cardIndex))
            continue;

        writeData(cardIndex, mMapCaptureThread[cardIndex]->userHandle(), 0x20000, data);
    }
}

bool PCIeCommSdk::writeData(quint8 cardIndex, HANDLE fd, quint64 offset, const QByteArray& data)
{
#ifdef _WIN32
    LARGE_INTEGER address;
    address.QuadPart = offset;
    if (INVALID_SET_FILE_POINTER == SetFilePointerEx(fd, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return false;
    }

    DWORD dwNumberOfBytesWritten;
    if (!WriteFile(fd, data.constData(), data.size(), &dwNumberOfBytesWritten, NULL)) {
        fprintf(stderr, "WriteFile failed with Win32 error code: %d\n", GetLastError());
        return false;
    }
 #else
    lseek(fd, offset, SEEK_SET);
    write(fd, data.data(), data.size());
#endif

    qDebug() << "[" << cardIndex << "] writeData" << QString("0x%1").arg((quint64)offset, 8, 16, QLatin1Char('0')) << data.toHex(' ');
    return true;
}

bool PCIeCommSdk::readData(HANDLE fd, quint64 offset, const QByteArray& data)
{
    //QMutexLocker locker(&mReadLocker);
#ifdef _WIN32
    LARGE_INTEGER address;
    address.QuadPart = offset;
    if (INVALID_SET_FILE_POINTER == SetFilePointerEx(fd, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return false;
    }

    // static char* buffer = nullptr;
    // if (nullptr == buffer)
    //     buffer = new char[120000000];
    // LARGE_INTEGER start;
    // LARGE_INTEGER stop;
    // LARGE_INTEGER freq;
    // QueryPerformanceFrequency(&freq);
    // QueryPerformanceCounter(&start);

    if (0/*data.size() == 0x07271400*/)
    {
        DWORD nNumberOfBytesToRead = data.size() > 2 ? data.size() / 2 : data.size();
        DWORD nNumberOfBytesRead = 0;
        memset((void*)data.data(), 0, data.size());
        if (!ReadFile(fd, (void*)data.data(), nNumberOfBytesToRead, &nNumberOfBytesRead, NULL)) {
            //if (!ReadFile(fd, (void*)buffer, 120000000, &nNumberOfBytesRead, NULL)) {
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
            return false;
        }

        address.QuadPart = nNumberOfBytesToRead;
        SetFilePointerEx(fd, address, NULL, FILE_CURRENT);//FILE_CURRENT
        if (!ReadFile(fd, (void*)(data.data()+nNumberOfBytesToRead), nNumberOfBytesToRead, &nNumberOfBytesRead, NULL)) {
            //if (!ReadFile(fd, (void*)buffer, 120000000, &nNumberOfBytesRead, NULL)) {
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
            return false;
        }
    }
    else {
        DWORD nNumberOfBytesToRead = data.size();
        DWORD nNumberOfBytesRead = 0;
        memset((void*)data.data(), 0, data.size());
        if (!ReadFile(fd, (void*)data.data(), nNumberOfBytesToRead, &nNumberOfBytesRead, NULL)) {
            //if (!ReadFile(fd, (void*)buffer, 120000000, &nNumberOfBytesRead, NULL)) {
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
            return false;
        }
    }

    // if (nNumberOfBytesToRead != nNumberOfBytesRead){
    //     qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
    //     return false;
    // }

    //delete[] buffer;
    // QueryPerformanceCounter(&stop);
    // double time_usec = (double)(stop.QuadPart - start.QuadPart) / (double)freq.QuadPart;
    // qDebug().nospace() << data.size() << " bytes received in " << time_usec << "s";
#else
    lseek(fd, offset, SEEK_SET);
    read(fd, (char*)data.data(), data.size());
#endif

    return true;
}

HANDLE PCIeCommSdk::getHandle(quint8 cardIndex, quint32 flags/* = GENERIC_READ | GENERIC_WRITE*/, quint32 dwFlagsAndAttributes)
{
    return PCIeCommSdk::getHandle(mDevices.at(cardIndex), flags, dwFlagsAndAttributes);
}

HANDLE PCIeCommSdk::getHandle(QString path, quint32 dwDesiredAccess, quint32 dwFlagsAndAttributes)
{
#ifdef _WIN32
    //FILE_FLAG_SEQUENTIAL_SCAN
    //FILE_FLAG_NO_BUFFERING
    HANDLE fd = CreateFileA(path.toStdString().c_str(),
                            dwDesiredAccess/*GENERIC_READ | GENERIC_WRITE*/,
                            0,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE/* | FILE_FLAG_NO_BUFFERING*/,
                            NULL);
#else
    int fd = open(devicePath.toStdString().c_str(), flags/*O_RDWR*/);
    //int fd_usr = open("/dev/xdma0_user",O_RDWR);
#endif

    return fd;
}

void PCIeCommSdk::initCaptureThreads()
{
#if ENABLE_DDR2
    for (int index = 1; index <= numberOfDevices() * 2; ++index)
#else
    for (int index = 1; index <= numberOfDevices(); ++index)
#endif
    {
        bool isDDR1 = index <= numberOfDevices();
        quint8 cardIndex = (index - 1) % numberOfDevices() + 1;

        CaptureThread *captureThread = new CaptureThread(cardIndex, mDevices.at(cardIndex-1), isDDR1);
        captureThread->setPriority(QThread::Priority::HighPriority);
        connect(captureThread, &CaptureThread::reportThreadExit, this, [=](quint32 index){
            mMapCaptureThread.remove(index);
            mThreadRunning[index] = false;
        });
        connect(captureThread, &CaptureThread::reportCaptureFinished, this, [=](quint32 cardIndex, bool isDDR1){
            mThreadRunning[isDDR1 ? cardIndex : (numberOfDevices() + cardIndex)] = false;

            bool allCaptureFinished = true;
#if ENABLE_DDR2
            for (int index = 1; index <= numberOfDevices() * 2; ++index)
#else
            for (int index = 1; index <= numberOfDevices(); ++index)
#endif
            {
                if (mThreadRunning[index])
                {
                    allCaptureFinished = false;
                    break;
                }
            }

            if (allCaptureFinished)
            {
                emit reportCaptureFinished();
                qInfo() << "数据采集完毕！";
            }
        }, Qt::DirectConnection);

        //connect(captureThread, QOverload<quint8,bool,quint32,const QByteArray&>::of(&CaptureThread::reportCaptureWaveformData), this, &PCIeCommSdk::replyCaptureWaveformData);
        connect(captureThread, QOverload<quint8,bool,quint32,const QByteArray&>::of(&CaptureThread::reportCaptureSpectrumData), this, &PCIeCommSdk::replyCaptureSpectrumData);

    // 设置线程亲和性(小于64核)
#ifdef _WIN32
        SetThreadAffinityMask(captureThread->currentThreadId(), index << 1ULL);
#else
        cpu_set_t mask;// cpu核的集合
        CPU_ZERO(&mask);// 将集合置为空集
        CPU_SET(cardIndex, &mask);// 设置亲和力值
        sched_setaffinity(0,sizeof(cpu_set_t),&mask);// 设置线程cpu亲和力

#endif

        mMapCaptureThread[index] = captureThread;
        mMapCaptureThread[index]->start();
    }
}

QByteArray PCIeCommSdk::reverseArray(const QByteArray& data, quint8 offset)
{
    QByteArray result;
    for (int i=0; i<data.size()/offset; ++i){
        QByteArray block = data.mid(i*offset, offset);
        std::reverse(block.begin(), block.end());
        result.append(block);
    }

    return result;
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
CaptureThread::CaptureThread(const quint32 cardIndex, const QString& devicePath, bool isDDR1/* = true*/)
    : mCardIndex(cardIndex)
    , mIsDDR1(isDDR1)
{
    mUserHandle = PCIeCommSdk::getHandle(devicePath + XDMA_FILE_USER);
    qDebug() << cardIndex
             << (isDDR1 ? "DDR1" : "DDR2")
             << devicePath
             << mUserHandle;
#if ENABLE_IOCP
    mPcieReader = new PcieIocpReader(this);
    // 初始化：4个分区，总大小800MB，每个分区默认200MB
    if (!mPcieReader->init(devicePath + XDMA_FILE_C2H_0, !isDDR1)) {
        delete mPcieReader;
        mPcieReader = nullptr;
    }
    for (int i=0; i<=3; ++i){
//        if (!mPcieReader->init(devicePath + XDMA_FILE_C2H + QStringLiteral("_%1").arg(i), 1, 0x07270E00)) {
//            delete mPcieReader;
//            mPcieReader = nullptr;
//            break;
//        }

        mRAMHandle[i] = PCIeCommSdk::getHandle(devicePath + XDMA_FILE_BYPASS);
    }
#else
    for (int i=0; i<=3; ++i){
        mDDRHandle[i] = PCIeCommSdk::getHandle(devicePath + XDMA_FILE_C2H + QStringLiteral("_%1").arg(i));
        mRAMHandle[i] = PCIeCommSdk::getHandle(devicePath + XDMA_FILE_BYPASS);
#if NORAM
        mRAMHandle[i] = (void*)1000;//PCIeCommSdk::getHandle(devicePath + XDMA_FILE_BYPASS);
#endif

#ifdef _WIN32
        if (mDDRHandle[i] == INVALID_HANDLE_VALUE
            || mRAMHandle[i] == INVALID_HANDLE_VALUE)
        {
            qCritical() << "打开设备失败 index:" << cardIndex;

            if (mDDRHandle[i] != INVALID_HANDLE_VALUE)
                CloseHandle(mDDRHandle[i]);
            if (mRAMHandle[i] != INVALID_HANDLE_VALUE)
                CloseHandle(mDDRHandle[i]);
#else
            if (mDDRHandle[i] >= 0)
                CloseHandle(mDDRHandle[i]);
            if (mRAMHandle[i] >= 0)
                CloseHandle(mRAMHandle[i]);
#endif
            break;
        }
    }
#endif //ENABLE_IOCP

    int capacity = 4;// 40ms, 4s共100帧，10s共250帧
    mDDRWaveformDatas.reserve(capacity);
    mRAMSpectrumDatas.reserve(capacity);
    try{
        for (int i=0; i < capacity; ++i){
            mDDRWaveformDatas.push_back(QByteArray(0x07270E00, 0));//200MB=0x0BEBC200 150MB=0x09600000 12*10e70=0x07270E00
            mRAMSpectrumDatas.push_back(QByteArray(0xA000, 0));
        }
    }
    catch (const std::bad_alloc& e){
        qDebug() << "Memory allocation failed:" << e.what();
    }

    connect(this, &QThread::finished, this, &QThread::deleteLater);
}

CaptureThread::~CaptureThread()
{
    requestInterruption();
    quit();
    wait();

    //关闭句柄
#if ENABLE_IOCP
    if (mPcieReader) {
        mPcieReader->stop();
        delete mPcieReader;
        mPcieReader = nullptr;
    }

    for (int i = 0; i<4; ++i){
        CloseHandle(mRAMHandle[i]);
    }
#else
    for (int i = 0; i<4; ++i){        
        CloseHandle(mDDRHandle[i]);
        CloseHandle(mRAMHandle[i]);
    }
#endif //ENABLE_IOCP

    CloseHandle(mUserHandle);
}

void CaptureThread::setParamter(const QString &saveFilePath, quint32 captureTimeSeconds)
{
    this->mSaveFilePath = saveFilePath;
    this->mCaptureCount = quint32((double)captureTimeSeconds / 40.0 + 0.4);
}

bool CaptureThread::startMeasure()
{
    qInfo().nospace() << "[" << mCardIndex << "] " << "发送开始测量指令";
    PCIeCommSdk::writeData(mCardIndex, mUserHandle, 0x20000, QByteArray::fromHex("00 00 00 00"));
    //::QThread::msleep(100);
    this->delay(100);
    PCIeCommSdk::writeData(mCardIndex, mUserHandle, 0x20000, QByteArray::fromHex("01 E0 34 12"));
    //::QThread::msleep(100);
    //this->delay(100);
    //PCIeCommSdk::writeData(mUserHandle, 0x20000, QByteArray::fromHex("00 00 00 00"));
}

void CaptureThread::clear()
{
    PCIeCommSdk::writeData(mCardIndex, mUserHandle, 0x20000, QByteArray::fromHex("00 00 00 00"));
}

void CaptureThread::empty()
{
    PCIeCommSdk::writeData(mCardIndex, mUserHandle, 0x20000, QByteArray::fromHex("01 F0 34 12"));
}

#include <QEventLoop>
void CaptureThread::delay(quint32 us)
{
    LARGE_INTEGER start;
    LARGE_INTEGER stop;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    while (true) {
        QueryPerformanceCounter(&stop);
        quint32 time_usec = (unsigned long long)(stop.QuadPart - start.QuadPart) / (double)freq.QuadPart * 1e6;
        if (time_usec >= us)
            break;

        qApp->processEvents();
    }
}

bool CaptureThread::checkDataError()
{
    QString ddrName = (mIsDDR1 ? "DDR1" : "DDR2");
    bool okPkg = true;
    int lastSeq = 0;

    // 检测波形数据是否完整，并记录出错时序号
    {
        // 校验包头序号和包尾序号是否一致
        for (int i=0; i<this->mCapturedRef; ++i){
            QByteArray pkgHead = PCIeCommSdk::reverseArray(mDDRWaveformDatas.at(i).left(12));
            QByteArray pkgTail = PCIeCommSdk::reverseArray(mDDRWaveformDatas.at(i).right(12));
            int headSeq = (quint8)pkgHead[7];
            int tailSeq = (quint8)pkgTail[7];
            if (headSeq != tailSeq){
                // 包头和包尾序号不一致
                mErrorStart = i;
                okPkg = false;
                break;
            }

            int currentSeq = headSeq;
            if (i>=255)
                currentSeq += 256;

            if ((currentSeq - lastSeq) != 1){
                mErrorStart = i;
                okPkg = false;
                break;
            }

            lastSeq = currentSeq;
        }

        if (okPkg && !mIsRegisterInvalid){
            qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 波形正常";
            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << " 波形正常";
        }
        else{
            if (!okPkg){
                qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 波形帧序号异常, 索引 = " << mErrorStart+1;
                qCritical().nospace() << "[" << mCardIndex << "] " << ddrName << " 波形帧序号异常, 索引 = " << mErrorStart+1;
            }

            if (mIsRegisterInvalid){
                qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 波形寄存器值异常, 索引 = " << mRegisterInvalidPosition;
                qCritical().nospace() << "[" << mCardIndex << "] " << ddrName << " 波形寄存器值异常, 索引 = " << mRegisterInvalidPosition;
            }

            //存储异常数据
            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "波形数据正在存储到硬盘中，请等待...";
            if (!okPkg){
                for (int i = mErrorStart-5; i < mErrorStart+5; ++i)
                {
                    if (i>=0 && i<mCapturedRef)
                    {
                        QString filename = QString("%1/%2%3data%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mIsDDR1 ? 'A' : 'B').arg(i+1);
                        QFile file(filename);
                        if (!file.open(QIODevice::WriteOnly)) {
                            qDebug() << "Cannot open file for writing";
                        }
                        else{
                            file.write(mDDRWaveformDatas.at(i));
                            file.close();
                        }
                    }
                }
            }
            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "波形数据已经全部存储到硬盘中！";
        }
    }

    mDataError = !okPkg;

    // 检测能谱数据是否完整，并记录出错时序号
    {
        okPkg = true;
        lastSeq = 0;
        for (int i=0; i<this->mCapturedRef; ++i){
            QByteArray pkgHead = PCIeCommSdk::reverseArray(mRAMSpectrumDatas.at(i).left(16), 16);
            bool ok = false;
            int currentSeq = pkgHead.mid(4, 2).toHex().toUInt(&ok, 16);
            if ((currentSeq - lastSeq) != 1){
                mErrorStart = i;
                okPkg = false;
                break;
            }

            lastSeq = currentSeq;
        }

        if (!okPkg){
            qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 能谱帧序号异常, 索引 = " << mErrorStart+1;
            qCritical().nospace() << "[" << mCardIndex << "] " << ddrName << " 能谱帧序号异常, 索引 = " << mErrorStart+1;
        }
        else{
            qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 能谱正常";
            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << " 能谱正常";
        }

        //存储异常数据
        if (!okPkg){
            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "能谱数据正在存储到硬盘中，请等待...";
            for (int i = mErrorStart-5; i <= mErrorStart+5; ++i)
            {
                if (i>=0 && i<mCapturedRef)
                {
                    QString filename = QString("%1/%2%3spec%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mIsDDR1 ? 'A' : 'B').arg(i+1);
                    QFile file(filename);
                    if (!file.open(QIODevice::WriteOnly)) {
                        qDebug() << "Cannot open file for writing";
                    }
                    else{
                        file.write(mRAMSpectrumDatas.at(i));
                        file.close();
                    }
                }
            }
            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "能谱数据已经全部存储到硬盘中！";
        }
    }

    mDataError = mDataError ? mDataError : !okPkg;
    return mDataError;
}

void CaptureThread::printDebugInfo()
{
    QString ddrName = (mIsDDR1 ? "DDR1" : "DDR2");
    bool ok = true;
    int lastSeq = 0;
    int index = 0;

    QFile logFile;
    if (mDataError || mIsRegisterInvalid)
        logFile.setFileName(QStringLiteral("%1/%2_%3_%4.err").arg(mSaveFilePath).arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss")).arg(mCardIndex).arg(ddrName));
    else
        logFile.setFileName(QStringLiteral("%1/%2_%3_%4.inf").arg(mSaveFilePath).arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss")).arg(mCardIndex).arg(ddrName));
    logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    QTextStream out(&logFile);
    // out << QString::fromUtf8("\xEF\xBB\xBF");
    // out.setCodec(QTextCodec::codecForName("utf8"));// "UTF-8");

    // 先输出包头信息
    for (int i=0; i<this->mCapturedRef; ++i){
        QByteArray pkgHead = PCIeCommSdk::reverseArray(mDDRWaveformDatas.at(i).left(12));
        QByteArray pkgTail = PCIeCommSdk::reverseArray(mDDRWaveformDatas.at(i).right(12));
        int headSeq = (quint8)pkgHead[7];
        int tailSeq = (quint8)pkgTail[7];

        int currentSeq = (quint8)headSeq;
        if (i>=255)
            currentSeq += 256;

        if (headSeq != tailSeq){
            // 包头和包尾序号不一致
            index = i+1;
            ok = false;
            lastSeq = currentSeq;
            out << "[" << mCardIndex << "] "
                << ddrName << " [" << i+1 << "] "
                << pkgHead.toHex(' ')
                << " ... "
                << pkgTail.toHex(' ')
                << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  << "\n";
        }

        else if ((currentSeq - lastSeq) != 1){
            index = i+1;
            ok = false;
            out << "[" << mCardIndex << "] "
                << ddrName << " [" << i+1 << "] "
                << pkgHead.toHex(' ')
                << " ... "
                << pkgTail.toHex(' ')
                << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  << "\n";
        } else{
            out << "[" << mCardIndex << "] "
                << ddrName << " [" << i+1 << "] "
                << pkgHead.toHex(' ')
                << " ... "
                << pkgTail.toHex(' ')
                << "\n";
        }

        lastSeq = currentSeq;
    }

    out << "================================================================================================\n\n";
    // 读寄存器时间
    for (const auto &outerPair : mRAMReadTime.toStdMap()){
        quint16 outerKey = outerPair.first;
        const auto& pairVec = outerPair.second;
        for (int idx=0; idx<pairVec.size(); ++idx){
            const auto& pair = pairVec[idx];
            out << "[" << mCardIndex << "] " << ddrName << " " << outerKey << " " <<
                QStringLiteral(" 读寄存器时间：") << pair.first << "-" << pair.second.first <<
                QStringLiteral(" 返回值：0x") << QString::number(pair.second.second, 16) << "\n";
        }
    }

    out << "================================================================================================\n\n";
    // 读数据时间
    for (int i=1; i<=this->mCapturedRef; ++i){
        out << "[" << mCardIndex << "] " << ddrName << " " << i <<
            QStringLiteral(" 寄存器值变化时刻：") << mRamChangedTime[i] <<
            QStringLiteral(" 创建读数据线程时刻") << mCreateThreadTime[i] <<
            QStringLiteral(" 开始读数据时刻：") << mBeforeReadTime[i] <<
            QStringLiteral(" 读完数据时刻：") << mAfterReadTime[i] <<
            QStringLiteral(" 开始读延迟：") << (mRamChangedTime[i]-i*40) <<
            QStringLiteral(" 读花费时间：") << (mAfterReadTime[i]-mBeforeReadTime[i]) <<
            QStringLiteral(" 累积延迟：") << (mAfterReadTime[i]-i*40) << "\n";            
    }

    out << "================================================================================================\n\n";
    if (!ok){
        out << "[" << mCardIndex << "] " << ddrName << " frames sequence exception, index = " << index;
    }

    if (mIsRegisterInvalid){
        out << "[" << mCardIndex << "] " << ddrName << " frames ramvalue exception, index = " << mRegisterInvalidPosition;
    }

    logFile.close();

    for (int i = 0; i < mCapturedRef; ++i)
    {
        memset((char*)mDDRWaveformDatas.at(i).data(), 0, mDDRWaveformDatas.at(i).size());// 数据清0
        memset((char*)mRAMSpectrumDatas.at(i).data(), 0, mRAMSpectrumDatas.at(i).size());// 数据清0
    }
}

bool CaptureThread::dataExistError()
{
    return mDataError || mIsRegisterInvalid;
}

void CaptureThread::run()
{
    qRegisterMetaType<QByteArray>("QByteArray");
    qRegisterMetaType<QVector<QPair<double,double>>>("QVector<QPair<double,double>>");
    //提升延时时间精度
    timeBeginPeriod(1);// 可提高精度到2ms -lwinmm

    quint64 memOffet = mIsDDR1 ? 0 : 0x40000000;
    quint64 memRamOffet = mIsDDR1 ? 0 : 0x40000;

    QElapsedTimer elapsedTimer;
    QByteArray readBuf(1, 0);
    qint64 t0 = 0;
    elapsedTimer.start();

    // 原子变量保护停止标志（避免数据竞争）
    QMutex irqMutex;
    QWaitCondition irqCond;
    std::atomic<bool> irqSignal = false;

    QMutex timeMutex;
    QWaitCondition timeCond;
    quint8 lastRegisterValue = 0x00; // 保存最后寄存器值 0~3

    qDebug().nospace() << "[" << mCardIndex << (mIsDDR1 ? "] DDR1" : "] DDR2") << " 数据采集线程 id:" << this->currentThreadId();

    // 创建线程池
    QThreadPool* threadPool = new QThreadPool(this);
    // 获取默认最大线程数（等于 CPU 核心数）
    int defaultThreads = QThreadPool::globalInstance()->maxThreadCount();
    // 为 I/O 密集型任务调整最大线程数
    threadPool->setMaxThreadCount(24);
    threadPool->setExpiryTimeout(-1);//过期超时不收回
	QString ddrName = (mIsDDR1 ? "DDR1" : "DDR2");

    mRamChangedTime.resize(500);// RAM值改变的时间
    mBeforeReadTime.resize(500);// DDR读之前的时间
    mAfterReadTime.resize(500);// DDR读之后的时间
    mCreateThreadTime.resize(500);// DDR读之后的时间

    while (!mIsStopped)
    {
        if (mIsPaused.load()){
            QThread::usleep(100);
            continue;
        }
        qDebug().noquote() << "[" << QString("0x%1").arg((quint64)QThread::currentThreadId(), 8, 16, QLatin1Char('0')) << "]"
                           << "[" << mCardIndex << "] "
                           << ddrName
                           << " 苏醒";

        // 开始测量(Release版本会触发异常，导致程序退出)
        elapsedTimer.restart();

        if (mIsDDR1){ // 1张卡只需要发一次开始测量指令
            startMeasure();
        }
        else {
            // 只测试DDR1
            delay(500);
        }

        elapsedTimer.restart();
        qDebug().nospace() << "[" << mCardIndex << "] "
                           << ddrName
                           << " 开始测量>>>>>>>>>>>>>>>>>>>>>>>>"
                           << elapsedTimer.elapsed();

        readBuf[0] = 0u;
        bool isFirstPacket = true;// 第一个数据包标识
        bool isOver = false;// 结束指令标识
        bool isTimeout = false;// 超时标识
        bool isPrintedTimeoutInfo = false; // 超时消息是否已经打印过一次

        mCapturedRef = 1;
        mIsRegisterInvalid = false;
        mRegisterInvalidPosition = 0;
        lastRegisterValue = 0x00;
        quint64 offsetRegister = mIsDDR1 ? 0x0 : 0x10000;

        mRamChangedTime.fill(0);
        mBeforeReadTime.fill(0);
        mAfterReadTime.fill(0);
        mCreateThreadTime.fill(0);
        mRAMReadTime.clear();

        for (;mCapturedRef <= this->mCaptureCount/* && !isTimeout*//*超时是否继续*/; ++mCapturedRef)
        {
            quint32 failCount = 0;
            isTimeout = false;

            while (true)
            {
                readBuf[0] = 0u;
                qint64 innerKey = elapsedTimer.elapsed();
                if (PCIeCommSdk::readData(mUserHandle, offsetRegister, readBuf))
                {
                    // 记录读数前后时刻
                    mRAMReadTime[mCapturedRef].push_back(qMakePair(innerKey, qMakePair(elapsedTimer.elapsed(), (quint8)readBuf[0])));
                    if ((quint8)readBuf[0] == 0x00){
                        // 数据还没准备好
                        if (!isFirstPacket){
                            mRamChangedTime[mCapturedRef] = elapsedTimer.elapsed();
                            lastRegisterValue = (quint8)readBuf[0];
                            isOver = true;
                            qDebug().noquote().nospace() << "[" << mCardIndex << "] "
                                                  << ddrName
                                                  << " 收到停止测量指令"
                                                  << " 当前寄存器值:0x" << readBuf.toHex()
                                                  << " 帧序号:" << mCapturedRef;// 测试停止，最后一个数据包序号无效，所以减1
                            break;
                        }
                    }
                    else if ((quint8)readBuf[0] == 0x10){
                        if (!isFirstPacket){
                            // 第1个0x10表示数据准备中，从第2个开始表示第4块区域数据已经准备就绪
                            if (lastRegisterValue != (quint8)readBuf[0]){
                                // 寄存器值发生改变
                                isFirstPacket = false;
                                lastRegisterValue = (quint8)readBuf[0];
                                mRamChangedTime[mCapturedRef] = elapsedTimer.elapsed();
                                break;
                            }
                        }
                    }
                    else if ((quint8)readBuf[0] == 0x18)
                    {
                        // 收到停止测量指令
                        if (!isFirstPacket && mCapturedRef == 260/*极限值*/){
                            mRamChangedTime[mCapturedRef] = elapsedTimer.elapsed();
                            lastRegisterValue = (quint8)readBuf[0];
                            qDebug().noquote().nospace() << "[" << mCardIndex << "] "
                                                  << ddrName
                                                  << " 收到停止测量指令"
                                                  << " 当前寄存器值:0x" << readBuf.toHex()
                                                  << " 帧序号:" << mCapturedRef;// 测试停止，最后一个数据包序号无效，所以减1
                            break;
                        }
                    }
                    else if (lastRegisterValue != (quint8)readBuf[0]){
                        // 寄存器值发生改变
                        isFirstPacket = false;
                        lastRegisterValue = (quint8)readBuf[0];
                        mRamChangedTime[mCapturedRef] = elapsedTimer.elapsed();
                        break;
                    }
                }
                else
                {
                    // 读数失败了(从未进入到这里，如果进入到这里，肯定是硬件出问题了)
                    qCritical().nospace() << "[" << mCardIndex << "] "
                                          << ddrName << "读寄存器失败，测量时刻：" << elapsedTimer.elapsed();
                    break;
                }

                if (++failCount>=50)//50ms
                {
                    isTimeout = true;
                    mIsRegisterInvalid = true;
                    if (!isPrintedTimeoutInfo){ //一旦超时，后面数据可能都超时，所以这里只打印一次消息即可
                        isPrintedTimeoutInfo = true;
                        mRegisterInvalidPosition = mCapturedRef;
                        qCritical().noquote().nospace() << "[" << mCardIndex << "] "
                                          << ddrName << "寄存器值异常"
                                          << " 当前寄存器值:0x" << readBuf.toHex()
                                          << " 帧序号:" << mCapturedRef;
                    }
                    break;
                }

                //QThread::msleep(1);//精度2ms
                delay(1000);//精度1us
            }

            if (isOver)
                break;

            if (isTimeout){
                continue;
            }

#if ENABLE_IOCP
            mPcieReader->submitReadRequestByStep(readBuf[0]);
#else
            //读原始数据
            quint32 capturedRef = mCapturedRef;
            quint8 step = 0;
            if (lastRegisterValue == 0x11)
                step = 0;
            else if (lastRegisterValue == 0x12)
                step = 1;
            else if (lastRegisterValue == 0x14)
                step = 2;
            else if (lastRegisterValue == 0x18 || lastRegisterValue == 0x10)
                step = 3;
            else
                continue;

			mCreateThreadTime[capturedRef] = elapsedTimer.elapsed();
            threadPool->start([=](){
                SetThreadAffinityMask(QThread::currentThreadId(), step << 1ULL);
                mBeforeReadTime[capturedRef] = elapsedTimer.elapsed();
                //读波形数据
                readWaveformData(step, mDDRWaveformDatas.at(capturedRef-1), memOffet + step * 0x07271400);
                //读能谱数据
                readSpectrumData(step, mRAMSpectrumDatas.at(capturedRef-1), memRamOffet + step * 0xA000);
				mAfterReadTime[capturedRef] = elapsedTimer.elapsed();

            }, QThread::HighestPriority);
#endif //ENABLE_IOCP
        }

        if (!isOver)// 循环次数到了，最后一个异常采集结束
            mCapturedRef--;

        {
            if (mIsDDR1){
                this->clear();
            }

            //emit reportCaptureFinished(mCardIndex, mIsDDR1);

            threadPool->waitForDone();
            // qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "数据正在存储到硬盘中，请等待...";
            // for (int i = 0; i < mCapturedRef; ++i)
            // {
            //     //emit reportCaptureData(mIsDDR1, i+1, mDDRWaveformDatas.at(i), mRAMSpectrumDatas.at(i)); //发给数据缓存处理线程
            //     quint16 packref = i + 1;
            //     threadPool->start([=](){
            //         SetThreadAffinityMask(QThread::currentThreadId(), (packref % 16) << 1ULL);

            //         //写波形数据
            //         {
            //             QString filename = QString("%1/%2%3data%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mIsDDR1 ? 'A' : 'B').arg(packref/*++mPackref*/);
            //             QFile file(filename);
            //             if (!file.open(QIODevice::WriteOnly)) {
            //                 qDebug() << "Cannot open file for writing";
            //             }
            //             else{
            //                 file.write(mDDRWaveformDatas.at(i));
            //                 file.close();
            //             }
            //         }

            //         //写能谱数据
            //         {
            //             QString filename = QString("%1/%2%3spec%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mIsDDR1 ? 'A' : 'B').arg (packref/*mPackref*/);
            //             QFile file(filename);
            //             if (!file.open(QIODevice::WriteOnly)) {
            //                 qDebug() << "Cannot open file for writing";
            //             }
            //             else{
            //                 file.write(mRAMSpectrumDatas.at(i));
            //                 file.close();
            //             }

            //             emit reportCaptureSpectrumData(mCardIndex, mIsDDR1, packref, mRAMSpectrumDatas.at(i));
            //         }
            //     });
            // }
            // qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "数据已经全部存储到硬盘中！";
            //  threadPool->waitForDone();

            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "采集结束，共采集：" << mCapturedRef;

            checkDataError();
            emit reportCaptureFinished(mCardIndex, mIsDDR1);

            pause();
            // 暂停逻辑
            {
                qDebug().noquote() << "[" << QString("0x%1").arg((quint64)QThread::currentThreadId(), 8, 16, QLatin1Char('0')) << "]"
                                   << "[" << mCardIndex << "] "
                                   << ddrName
                                   << " 休眠";
            }

            continue;
        }
    }

    // 等待子线程安全退出（避免资源泄漏）
    irqCond.wakeOne();

    //timeEndPeriod(1);

    // 报告线程退出
    emit reportThreadExit(mCardIndex);
    qDebug() << "destroyCaptureThread id:" << this->currentThreadId();
}

bool CaptureThread::readDataAsync(HANDLE fd, quint64 offset, const QByteArray& data)
{
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(nullptr, true, false, nullptr);
    overlapped.Offset = offset;

    DWORD nNumberOfBytesToRead = data.size();
    DWORD nNumberOfBytesRead = 0;
    if (!ReadFile(fd, (void*)data.data(), nNumberOfBytesToRead, &nNumberOfBytesRead, &overlapped)) {
        if (GetLastError() == ERROR_IO_PENDING){
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
            return false;
        }

        WaitForSingleObject(overlapped.hEvent, 120/*INFINITE*/);
        // if (WaitForSingleObject(overlapped.hEvent, 120000/*INFINITE*/) != WAIT_OBJECT_0){
        //     qDebug() << "WaitForSingleObject fail, win32 error code:" << GetLastError();
        //     CloseHandle(overlapped.hEvent);
        //     return false;
        // }

        if (!GetOverlappedResult(fd, &overlapped, &nNumberOfBytesRead, false)){
            qDebug() << "GetOverlappedResult fail, win32 error code:" << GetLastError();
            CloseHandle(overlapped.hEvent);
            return false;
        }
    }

    ResetEvent(overlapped.hEvent);
    CloseHandle(overlapped.hEvent);

    return false;
}

/**
* @function name:readWaveformData
* @brief 从DDR读波形原始数据
* @param[in]        data
* @param[out]       data
* @return           void
*/
bool CaptureThread::readWaveformData(quint8 index, const QByteArray& data, const quint64 offset)
{
#if ENABLE_IOCP
    return false;
#else
    return PCIeCommSdk::readData(mDDRHandle[index], offset, data);
#endif //ENABLE_IOCP
}

/**
* @function name:readSpectrumData
* @brief 从RAM读能谱数据
* @param[in]        data
* @param[out]       data
* @return           bool
*/
bool CaptureThread::readSpectrumData(quint8 index, const QByteArray& data, const quint64 offset)
{
    return PCIeCommSdk::readData(mRAMHandle[index], offset, data);
}
