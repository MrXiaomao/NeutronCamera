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
        if (mMapCaptureThread[index]->dataExistError()){
            mMapCaptureThread[index]->printDataError();
        }
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

                QByteArray chunk = waveformData.mid(16, 200000000-32);
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

                //提取通道号的数据cameraNo
                QVector<qint16> waveform;
                if (cameraNo == 0) {
                    //扣基线，调整数据
                    qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[0]);
                    //根据相机序号计算出是第几块光纤卡
                    int board_index = (cameraIndex-1)/4+1;
                    DataAnalysisWorker::adjustDataWithBaseline(ch[0], baseline_ch, board_index, 1);
                    //提取数据
                    waveform = ch[0].mid(index, point_num);
                } else if (cameraNo == 1) {
                    //扣基线，调整数据
                    qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[1]);
                    //根据相机序号计算出是第几块光纤卡
                    int board_index = (cameraIndex-1)/4+1;
                    DataAnalysisWorker::adjustDataWithBaseline(ch[1], baseline_ch, board_index, 2);
                    //提取数据
                    waveform = ch[1].mid(index, point_num);
                } else if (cameraNo == 2) {
                    //扣基线，调整数据
                    qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[2]);
                    //根据相机序号计算出是第几块光纤卡
                    int board_index = (cameraIndex-1)/4+1;
                    DataAnalysisWorker::adjustDataWithBaseline(ch[2], baseline_ch, board_index, 3);
                    //提取数据
                    waveform = ch[2].mid(index, point_num);
                }

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
    QByteArray reverseData = DataCachPoolThread::reverseArray(spectrumData, 16);
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
    QByteArray reverseData = DataCachPoolThread::reverseArray(spectrumData, 16);
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
    return false;

    //计算出是当前文件波形的第几个数据点
    int index = remainTime * 1000 * 1000/ 2;
    int point_num = timeLength * 1000 * 1000 / 2;

    //扣基线，调整数据
    qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[cameraNo]);
    //根据相机序号计算出是第几块光纤卡
    int board_index = (cameraIndex-1)/CAMNUMBER_DDR_PER+1;
    DataAnalysisWorker::adjustDataWithBaseline(ch[cameraNo], baseline_ch, board_index, cameraNo + 1);
    //根据时间段提取数据
    //提取通道号的数据cameraNo
    QVector<qint16> waveform = ch[cameraNo].mid(index, point_num);

//    QVector<QPair<double,double>> waveformPair;
//    for (int i=0;i<waveform.size();++i)
//        waveformPair.push_back(qMakePair(i*2, waveform[i]));
//    emit doWaveform(1, cameraIndex, waveformPair);

//    return true;
}

#include "globalsettings.h"
bool PCIeCommSdk::analyzeHistoryCpsData(quint32 channels/*多道道数*/,
                                        quint32 timeWidth/*时间宽度ms*/,
                                        quint32 timeStart/*开始时刻ms*/,
                                        quint32 timeStop/*结束时刻ms*/,
                                        QString filePath/*H5文件路径*/,
                                        std::function<void(QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>>, QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>>)> callback
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
                auto readChannel = [&](const QString& datasetName, QVector<quint64>& timeTrigger, QVector<quint64>& timePeak) {
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
                        outData.resize(4);
                        // 逐列选切片读取：每次选所有行 + 当前1列
                        for (int colIdx = 0; colIdx < 4; colIdx++) {
                            hsize_t offset[2] = {0, (hsize_t)colIdx};
                            hsize_t count[2]  = {totalRows, 1}; // 一次读1列
                            fileSpace.selectHyperslab(H5S_SELECT_SET, count, offset);

                            H5::DataSpace memSpace(1, &totalRows); // 内存空间是一维，长度为总行数
                            outData[colIdx].resize(totalRows);

                            dataset.read(outData[colIdx].data(), H5::PredType::NATIVE_INT16, memSpace, fileSpace);
                        }

                        for (int rowIdx = 0; rowIdx < totalRows; ++rowIdx){
                            quint64 timeMs = static_cast<quint16>(outData[0][rowIdx]);
                            quint64 timeNs = (static_cast<quint16>(outData[1][rowIdx])<<16 | static_cast<quint16>(outData[2][rowIdx]));
                            timeTrigger.push_back((timeMs*1e6+timeNs)/1e6);

                            timePeak.push_back(static_cast<quint16>(outData[3][rowIdx]));
                        }

                        dataset.close();
                    }
                };

                // 写入3个通道的数据
                QVector<quint64> timeTrigger_ch[3], timePeak_ch[3];
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
                    for (quint16 intervalNo = 0; intervalNo < cpsTotal; ++intervalNo) {
                        cpsMapPair[cameraIndex][timeStart + intervalNo * timeWidth] = quint32(0);
                    }

                    // 2. 遍历所有输入数据，分桶统计
                    for (qint32 time : timeTrigger_ch[cameraNo]) {
                        cpsMapPair[cameraIndex][timeStart + (time-timeStart) / timeWidth] += 1;
                        // int intervalIndex = (time - timeStart) / timeWidth;
                        // // 对应区间计数+1
                        // if (intervalIndex<cpsTotal)
                        //     cpsMapPair[cameraIndex][timeStart + intervalIndex * timeWidth].second += 1;
                        // else
                        //     qDebug();
                    }

                }

                //根据时间段统计能谱
                QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>> spectrumMapPair;
                for (quint8 cameraNo=0; cameraNo<3; ++cameraNo){
                    quint8 cameraIndex = (boardNum-1)*3 + cameraNo + 1;

                    // 1.默认道址为8192
                    timeWidth = (timeStop - timeStart) * 1e3 / channels;

                    //初始化每个道址默认能量值为0
                    for (quint16 channel = 0; channel < channels; ++channel) {
                        spectrumMapPair[cameraIndex][channel] = quint32(0);
                    }

                    // 2. 遍历所有时间段内的能量值，分桶统计
                    int i = 0;
                    for (qint32 time : timeTrigger_ch[cameraNo]) {
                        quint16 channel = (time-timeStart) / timeWidth;// 道址
                        quint64 peak = timePeak_ch[cameraNo][i++];// 能量峰值
                        spectrumMapPair[cameraIndex][channel] += peak;
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
        connect(captureThread, &CaptureThread::reportFileReadElapsedtime, this, &PCIeCommSdk::reportFileReadElapsedtime);
        connect(captureThread, &CaptureThread::reportFileWriteElapsedtime, this, &PCIeCommSdk::reportFileWriteElapsedtime);
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

/**
 * WriteFileThread 数据缓存/写硬盘线程====================================================
*/
/**
* @function name: DataCachPoolThread
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
DataCachPoolThread::DataCachPoolThread()
{

}

void DataCachPoolThread::setParamter(const quint32 cardIndex, const QString &saveFilePath)
{
    mPackref = 0;
    mCardIndex = cardIndex;
    mSaveFilePath = saveFilePath;
}

/**
* @function name:replyThreadExit
* @brief 响应线程退出事件
* @param[in]
* @param[out]
* @return
*/
void DataCachPoolThread::replyThreadExit()
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
void DataCachPoolThread::replyCaptureData(bool isDDR1, quint8 packref, const QByteArray& waveformData, const QByteArray& spectrumData)
{
    QMutexLocker locker(&mMutexWrite);

    {
        QString filename = QString("%1/%2%3data%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(isDDR1 ? 'A' : 'B').arg(packref/*++mPackref*/);
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly)) {
            qDebug() << "Cannot open file for writing";
        }
        else{
            file.write(waveformData);
            file.close();
        }
    }

    {
        QString filename = QString("%1/%2%3spec%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(isDDR1 ? 'A' : 'B').arg(packref/*mPackref*/);
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly)) {
            qDebug() << "Cannot open file for writing";
        }
        else{
            file.write(spectrumData);
            file.close();
        }

        //QByteArray reverseData = reverseArray(spectrumData, 16);
        //emit reportCaptureSpectrumData(mCardIndex, packref, reverseData);
        emit reportCaptureSpectrumData(mCardIndex,isDDR1,packref, spectrumData);
    }
}

void DataCachPoolThread::run()
{
    qRegisterMetaType<QByteArray>("QByteArray");

    QThreadPool* pool = QThreadPool::globalInstance();
    pool->setMaxThreadCount(QThread::idealThreadCount());

    while (!this->isInterruptionRequested())
    {
        QVector<QByteArray> tempPool;
        {
            QMutexLocker locker(&mMutexWrite);
            if (mCachePool.size() > 0){
                tempPool.swap(mCachePool);
            }
            else{
                //数据处理完了，超过3秒没数据过来表示已经录制完了
                if (mTerminated && mElapsedTimer.elapsed() >= 3000)
                    break;

                while (!mReady){
                    mCondition.wait(&mMutexWrite);
                }

                tempPool.swap(mCachePool);
                mReady = false;
            }
        }

        while (tempPool.size() > 0){
            //QByteArray data = tempPool.at(0);
            // WriteFileTask *task = new WriteFileTask(mIndex, ++mPackref, mSaveFilePath, data);
            // connect(task, &WriteFileTask::reportFileWriteElapsedtime, this, &WriteFileThread::reportFileWriteElapsedtime);
            // pool->start(task);
            // tempPool.pop_front();

            //写原始数据
            {
                QByteArray data = tempPool.at(0);
                QString filename = QString("%1/%2data%3.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mPackref);
                QFile file(filename);
                if (!file.open(QIODevice::WriteOnly)) {
                    qDebug() << "Cannot open file for writing";
                }
                else{
                    file.write(data);
                    file.close();
                }

                tempPool.pop_front();
            }

            //写能谱数据
            {
                QByteArray data = reverseArray(tempPool.at(0));
                emit reportCaptureSpectrumData(mCardIndex,true,mPackref, data);

                QString filename = QString("%1/%2spec%3.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mPackref);
                QFile file(filename);
                if (!file.open(QIODevice::WriteOnly)) {
                    qDebug() << "Cannot open file for writing";
                }
                else{
                    file.write(data);
                    file.close();
                }

                tempPool.pop_front();
            }

            mPackref++;
        }

        QThread::msleep(1);
    }

    pool->waitForDone();
    qDebug() << "destroyDataCachPoolThread id:" << this->currentThreadId();
}

QByteArray DataCachPoolThread::reverseArray(const QByteArray& data, quint8 offset)
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

    mDataCachPoolThread = new DataCachPoolThread();
    connect(this, QOverload<bool,quint8,const QByteArray&,const QByteArray&>::of(&CaptureThread::reportCaptureData), mDataCachPoolThread, &DataCachPoolThread::replyCaptureData, Qt::DirectConnection);
    connect(this, &CaptureThread::reportThreadExit, mDataCachPoolThread, &DataCachPoolThread::replyThreadExit);
    connect(mDataCachPoolThread, &DataCachPoolThread::reportFileWriteElapsedtime, this, &CaptureThread::reportFileWriteElapsedtime);
    connect(mDataCachPoolThread, &DataCachPoolThread::reportCaptureWaveformData, this, &CaptureThread::reportCaptureWaveformData);
    connect(mDataCachPoolThread, &DataCachPoolThread::reportCaptureSpectrumData, this, &CaptureThread::reportCaptureSpectrumData);
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
    mDataCachPoolThread->setParamter(mCardIndex, mSaveFilePath);
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
    // qint32 capturedRef = 1; //包序号
    // quint8 step = 3;// 0~3 数据块
    // quint64 offset = step * 0x07270E00;
    // if (readWaveformData(step, mDDRWaveformDatas.at(capturedRef-1), offset)){
    //     QByteArray data = DataCachPoolThread::reverseArray(mDDRWaveformDatas.at(capturedRef-1));
    //     qDebug().noquote() << data.left(12).toHex(' ');
    //     qDebug().noquote() << data.right(12).toHex(' ');
    //     qDebug() << "";
    // }

    QString ddrName = (mIsDDR1 ? "DDR1" : "DDR2");
    bool ok = true;
    int lastSeq = 0;
    int index = 0;

    // 校验包头序号和包尾序号是否一致
    for (int i=0; i<this->mCapturedRef; ++i){
        QByteArray data = DataCachPoolThread::reverseArray(mDDRWaveformDatas.at(i).left(96));
        int currentSeq = (quint8)data[7];
        if (ok && ((currentSeq - lastSeq) != 1)){
            if (i>=255){
                int currentSeq2 = 256 + currentSeq;
                if ((currentSeq2 - lastSeq) == 1){
                    lastSeq = currentSeq2;
                    continue;
                }
            }

            mErrorStart = i;
            index = i+1;
            ok = false;
            // qDebug().nospace() << "[" << mCardIndex << "] "
            //                    << ddrName << " [" << i+1 << "] "
            //                    << data.left(12).toHex(' ')
            //                    << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx" ;
        } else{
            // qDebug().nospace() << "[" << mCardIndex << "] "
            //                << ddrName << " [" << i+1 << "] "
            //                << data.left(12).toHex(' ');
        }

        lastSeq = currentSeq;
        //     qDebug().noquote() << data.right(12).toHex(' ');
        //     qDebug() << "";
    }

    if (ok){
        qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " frames ok";
        qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << " frames ok";
    }
    else{
        qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " frames exception, index = " << index;
        qCritical().nospace() << "[" << mCardIndex << "] " << ddrName << " frames exception, index = " << index;

        qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "数据正在存储到硬盘中，请等待...";
        for (int i = mErrorStart-5; i < mErrorStart+5; ++i)
        {
            if (i>=0 && i<mCapturedRef)
                emit reportCaptureData(mIsDDR1, i+1, mDDRWaveformDatas.at(i), mRAMSpectrumDatas.at(i)); //发给数据缓存处理线程
        }
        qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "数据已经全部存储到硬盘中！";
    }

    mDataError = !ok;
    return mDataError;
}

void CaptureThread::printDataError()
{
    QString ddrName = (mIsDDR1 ? "DDR1" : "DDR2");
    bool ok = true;
    int lastSeq = 0;
    int index = 0;
    for (int i=0; i<this->mCapturedRef; ++i){
        QByteArray data = DataCachPoolThread::reverseArray(mDDRWaveformDatas.at(i).left(96));
        int currentSeq = (quint8)data[7];
        if (ok && ((currentSeq - lastSeq) != 1)){
            if (i>=255){
                int currentSeq2 = 256 + currentSeq;
                if ((currentSeq2 - lastSeq) == 1){
                    lastSeq = currentSeq2;
                    qDebug().nospace() << "[" << mCardIndex << "] "
                                       << ddrName << " [" << i+1 << "] "
                                       << data.left(12).toHex(' ');
                    continue;
                }
            }

            index = i+1;
            ok = false;
            qDebug().nospace() << "[" << mCardIndex << "] "
                               << ddrName << " [" << i+1 << "] "
                               << data.left(12).toHex(' ')
                               << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx" ;
        } else{
            qDebug().nospace() << "[" << mCardIndex << "] "
                               << ddrName << " [" << i+1 << "] "
                               << data.left(12).toHex(' ');
        }

        lastSeq = currentSeq;
        //     qDebug().noquote() << data.right(12).toHex(' ');
        //     qDebug() << "";
    }
}

bool CaptureThread::dataExistError()
{
    return mDataError;
}

void CaptureThread::run()
{
    qRegisterMetaType<QByteArray>("QByteArray");
    qRegisterMetaType<QVector<QPair<double,double>>>("QVector<QPair<double,double>>");
    //提升线程优先级
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
    quint8 lastRegisterValue = 0x00; // 0~3

    // 启动数据缓存线程（确保事件循环启动）
    mDataCachPoolThread->start();
    qDebug().nospace() << "[" << mCardIndex << (mIsDDR1 ? "] DDR1" : "] DDR2") << " 数据采集线程 id:" << this->currentThreadId();

    // 创建线程池
    QThreadPool* threadPool = new QThreadPool();
    // 获取默认最大线程数（等于 CPU 核心数）
    int defaultThreads = QThreadPool::globalInstance()->maxThreadCount();
    // 为 I/O 密集型任务调整最大线程数
    threadPool->setMaxThreadCount(defaultThreads*2);
	QString ddrName = (mIsDDR1 ? "DDR1" : "DDR2");

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

        elapsedTimer.restart();
        qDebug().nospace() << "[" << mCardIndex << "] "
                           << ddrName
                           << " 开始测量>>>>>>>>>>>>>>>>>>>>>>>>";

        readBuf[0] = 0u;
        bool isFirstPacket = true;
        bool isOver = false;
        bool isTimeout = false;

        mCapturedRef = 1;
        lastRegisterValue = 0x00;
        quint64 offsetRegister = mIsDDR1 ? 0x0 : 0x10000;

        for (;mCapturedRef <= this->mCaptureCount/* && !isTimeout*//*超时是否继续*/; ++mCapturedRef)
        {
            quint32 failCount = 0;
            bool isTimeout = false;
            while (true)
            {
                readBuf[0] = 0u;
                if (PCIeCommSdk::readData(mUserHandle, offsetRegister, readBuf))
                {
                    if ((quint8)readBuf[0] == 0x10)
                    {
                    }
                    else if ((quint8)readBuf[0] == 0x00)
                    {
                        if (!isFirstPacket){
                            lastRegisterValue = 0x00;
                            isOver = true;
                            mCapturedRef--;
                            break;
                        }
                    }
                    else if (lastRegisterValue != (quint8)readBuf[0])
                    {
                        isFirstPacket = false;
                        lastRegisterValue = (quint8)readBuf[0];
                        break;
                    }

                }

                if (++failCount>=50)//50ms
                {
                    isTimeout = true;
                    break;
                }

                //QThread::msleep(1);//精度2ms
                delay(1000);//精度1us
            }

            if (isOver)
                break;

            if (isTimeout)
                continue;

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
            else if (lastRegisterValue == 0x18)
                step = 3;
            else
                continue;

            threadPool->start([=](){
                //读能谱数据
                //qDebug() << "[" << mCardIndex << "] " << ddrName << capturedRef << " enter elapsedTimerStart=" <<tt1;
                //读原始数据
                //delay(60000);
                SetThreadAffinityMask(QThread::currentThreadId(), step << 1ULL);
                if (readWaveformData(step, mDDRWaveformDatas.at(capturedRef-1), memOffet + step * 0x07271400)){//0x07270E00 0x07271400
                    //qDebug().noquote() << "[" << QString("0x%1").arg((quint64)QThread::currentThreadId(), 8, 16, QLatin1Char('0')) << "] [" << mCardIndex << "] " << mCapturedRef << chunkStep << readBuf.toHex();
                }

                if (readSpectrumData(step, mRAMSpectrumDatas.at(capturedRef-1), memRamOffet + step * 0xA000)){

                }

            }, QThread::HighestPriority);
#endif //ENABLE_IOCP
        }

        if (!isOver)//非正常采集结束
            mCapturedRef--;

        {
            if (mIsDDR1){
                this->clear();
            }

            //emit reportCaptureFinished(mCardIndex, mIsDDR1);

            threadPool->waitForDone();
            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "数据正在存储到硬盘中，请等待...";
            for (int i = 0; i < mCapturedRef; ++i)
            {
                //emit reportCaptureData(mIsDDR1, i+1, mDDRWaveformDatas.at(i), mRAMSpectrumDatas.at(i)); //发给数据缓存处理线程

                quint16 packref = i + 1;
                {
                    QString filename = QString("%1/%2%3data%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mIsDDR1 ? 'A' : 'B').arg(packref/*++mPackref*/);
                    QFile file(filename);
                    if (!file.open(QIODevice::WriteOnly)) {
                        qDebug() << "Cannot open file for writing";
                    }
                    else{
                        file.write(mDDRWaveformDatas.at(i));
                        file.close();
                    }
                }

                {
                    QString filename = QString("%1/%2%3spec%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mIsDDR1 ? 'A' : 'B').arg(packref/*mPackref*/);
                    QFile file(filename);
                    if (!file.open(QIODevice::WriteOnly)) {
                        qDebug() << "Cannot open file for writing";
                    }
                    else{
                        file.write(mRAMSpectrumDatas.at(i));
                        file.close();
                    }

                    qDebug() << "reportCaptureSpectrumData" << mCardIndex << mIsDDR1 << packref;
                    emit reportCaptureSpectrumData(mCardIndex, mIsDDR1, packref, mRAMSpectrumDatas.at(i));
                }
            }
            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "数据已经全部存储到硬盘中！";

            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "采集结束，共采集：" << mCapturedRef;

            //checkDataError();
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

    // 数据缓存线程管理（确保事件循环退出）
    if (mDataCachPoolThread->isRunning()) {
        mDataCachPoolThread->quit();
        mDataCachPoolThread->wait();
    }
    mDataCachPoolThread->deleteLater();
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
