#ifndef PCIECOMMSDK_H
#define PCIECOMMSDK_H

#include <QObject>
#include <QDebug>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <setupapi.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif
#include "xdma_public.h"

#define ENABLE_MAPPING 1
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QDateTime>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QThreadPool>
class WriteFileTask : public QObject, public QRunnable{
    Q_OBJECT
public:
    explicit WriteFileTask(quint32 index, quint32 packref, const QString& saveFilePath, QByteArray& data)
        : mIndex(index)
        , mPackref(packref)
        , mSaveFilePath(saveFilePath)
        , mData(data)
    {
        this->setAutoDelete(true);
    }

    void run() override{
        QElapsedTimer elapsedTimer;
        elapsedTimer.start();
        QDateTime now = QDateTime::currentDateTime();
        QString filename = QString("%1/%2data%3.bin").arg(mSaveFilePath).arg(mIndex).arg(mPackref++);

        HANDLE hfOutput = CreateFileA(filename.toStdString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (!hfOutput) {
            qDebug() << "CreateFileA fail, win32 error code:" << GetLastError();
        }
        else{
            DWORD NumberOfBytesWritten = 0;
            if (!WriteFile(hfOutput, mData.constData(), mData.size(), &NumberOfBytesWritten, NULL)){
                qDebug() << "WriteFile fail, win32 error code:" << GetLastError();
            }
            CloseHandle(hfOutput);

            emit reportFileWriteElapsedtime(mIndex,elapsedTimer.elapsed());
        }
    }

    Q_SIGNAL void reportFileWriteElapsedtime(quint32,quint32);

private:
    quint32 mIndex;
    quint32 mPackref;
    QString mSaveFilePath;
    QByteArray mData;
};

class ReadFileTask : public QObject, public QRunnable{
    Q_OBJECT
public:
    explicit ReadFileTask(quint32 index, quint32 packref, const QString& saveFilePath, QByteArray& data)
        : mIndex(index)
        , mPackref(packref)
        , mSaveFilePath(saveFilePath)
        , mData(data)
    {
        this->setAutoDelete(true);
    }

    void run() override{
        QElapsedTimer elapsedTimer;
        elapsedTimer.start();
        QDateTime now = QDateTime::currentDateTime();
        QString filename = QString("%1/%2data%3.bin").arg(mSaveFilePath).arg(mIndex).arg(mPackref++);

        HANDLE hfOutput = CreateFileA(filename.toStdString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (!hfOutput) {
            qDebug() << "CreateFileA fail, win32 error code:" << GetLastError();
        }
        else{
            DWORD NumberOfBytesWritten = 0;
            if (!WriteFile(hfOutput, mData.constData(), mData.size(), &NumberOfBytesWritten, NULL)){
                qDebug() << "WriteFile fail, win32 error code:" << GetLastError();
            }
            CloseHandle(hfOutput);

            emit reportFileWriteElapsedtime(mIndex,elapsedTimer.elapsed());
        }
    }

    Q_SIGNAL void reportFileWriteElapsedtime(quint32,quint32);

private:
    quint32 mIndex;
    quint32 mPackref;
    QString mSaveFilePath;
    QByteArray mData;
};

class WriteFileThread : public QThread {
    Q_OBJECT
public:
    explicit WriteFileThread(const quint32 index, const QString &saveFilePath)
        : mIndex(index)
        , mSaveFilePath(saveFilePath)
    {
    }

    Q_SLOT void replyThreadExit(){
        mTerminated = true;
        mElapsedTimer.start();
    };

    Q_SLOT void replyCaptureData(QByteArray& waveformData, QByteArray& spectrumData){
        QMutexLocker locker(&mMutexWrite);
        spectrumData.append(waveformData);
        spectrumData.append(spectrumData);
    };

    void run() override {
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

    Q_SIGNAL void reportFileWriteElapsedtime(quint32,quint32);

private:
    quint32 mIndex;//设备名称
    QString mSaveFilePath;//保存路径
    QVector<QByteArray> mCachePool;
    bool mTerminated = false;
    QMutex mMutexWrite;
    QElapsedTimer mElapsedTimer;
};

class CaptureThread : public QThread {
    Q_OBJECT
public:
    explicit CaptureThread(const quint32 cardIndex, HANDLE hFile, HANDLE hUser, HANDLE hBypass, const QString &saveFilePath, quint32 captureTimeSeconds)
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

    BYTE* allocate_buffer(size_t size, size_t alignment) {
        if(size == 0) {
            size = 4;
        }

        if (alignment == 0) {
            SYSTEM_INFO sys_info;
            GetSystemInfo(&sys_info);
            alignment = sys_info.dwPageSize;
        }

        return (BYTE*)_aligned_malloc(size, alignment);
    }

    void run() override {
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
        unsigned char* lpBuffer = allocate_buffer(nNumberOfBytesToRead, 0);
        if (lpBuffer == NULL){
            emit reportCaptureFail(mCardIndex, GetLastError());
            return;
        }

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

        if (lpBuffer)
            _aligned_free(lpBuffer);

        qDebug() << "destroyCaptureThread id:" << this->currentThreadId();
    }

    //第一次查询状态
    bool prepared()
    {
        return emptyStatus();

        // 1.查询DDR/RAM是否写满：
        // xdma_rw.exe user read 0 -l
        // 若返回0，表示DDR和RAM可以进行写操作
        DWORD nNumberOfBytesRead = 0;
        quint8 buffer = 0;

        LARGE_INTEGER inAddress;
        inAddress.QuadPart = 0;
        SetFilePointerEx(mUserHandle, inAddress, NULL, FILE_BEGIN);

        if (!ReadFile(mUserHandle, &buffer, 1, &nNumberOfBytesRead, NULL)) {
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
        }
        else{
            if (buffer == 0x00)
                return true;
        }

        return false;
    }

    //判断是否可读
    bool canRead()
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

    //清空状态
    bool emptyStatus()
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

    bool resetReadflag()
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

    //读原始数据
    bool readRawData(QByteArray& data)
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

    //读能谱数据
    bool readSpectrumData(QByteArray& data)
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

    Q_SIGNAL void reportCaptureFail(quint32, quint32);
    Q_SIGNAL void reportThreadExit(quint32);
    Q_SIGNAL void reportCaptureData(QByteArray& waveformData, QByteArray& spectrumData);
    Q_SIGNAL void reportCaptureWaveformData(quint8,quint32,QByteArray& data);
    Q_SIGNAL void reportCaptureSpectrumData(quint8,quint32,QByteArray& data);
    Q_SIGNAL void reportFileReadElapsedtime(quint32, quint32);
    Q_SIGNAL void reportFileWriteElapsedtime(quint32, quint32);
    Q_SIGNAL void reportCaptureFinished(quint32);

private:
    WriteFileThread* mWriteFileThread = nullptr;
    quint32 mCardIndex;//设备名称
    HANDLE mDeviceHandle;//设备句柄
    HANDLE mUserHandle;//用户句柄
    HANDLE mBypassHandle;//RAM句柄
    QString mSaveFilePath;//保存路径
    quint32 mCaptureTimeSeconds = 50;
    quint32 mCaptureRef = 1;
     quint32 mTimeout = 5000;//超时微秒
};

#define CAMNUMBER_DDR_PER   4   // 每张PCIe对应一个Fpga数采板，每个数采板对应的是8个探测器（但是考虑带宽可能只用到了6路，分2个DDR存储数据，所以每个DDR存储3路）
#define DETNUMBER_PCIE_PER  8   // 每张PCIe对应一个Fpga数采板，每个数采板对应的是8个探测器（但是考虑带宽可能只用到了6路，分2个DDR存储数据，所以每个DDR存储3路）
#define DETNUMBER_MAX       18  // 探测器有效数只用到了18路（11路水平+7路垂直）
class PCIeCommSdk : public QObject
{
    Q_OBJECT
public:
    explicit PCIeCommSdk(QObject *parent = nullptr);
    ~PCIeCommSdk();

    enum CameraOrientation {
        Horizontal = 0x1,
        Vertical = 0x2
    };

    enum CaptureTime {
        oldCaptureTime = 50,//50ms 单个文件对应采集时间，旧版数据采集协议
        newCaptureTime = 66,//66ms 单个文件对应采集时间，新版数据采集协议
    };

    enum SpectrumType {
        PSD = 0x1,
        FOM = 0x2
    };

    Q_SIGNAL void reportNotFoundDevices();
    Q_SIGNAL void reportCaptureFail(quint32, quint32);
    Q_SIGNAL void reportFileReadElapsedtime(quint32, quint32);
    Q_SIGNAL void reportFileWriteElapsedtime(quint32, quint32);
    Q_SIGNAL void reportCaptureFinished();
    Q_SIGNAL void reportWaveform(quint8/*时刻*/, quint8/*相机索引*/, QVector<QPair<double,double>>&);
    Q_SIGNAL void reportNeutronSpectrum(quint8/*时刻*/, quint8/*相机索引*/, QVector<QPair<double,double>>&);
    Q_SIGNAL void reportGammaSpectrum(quint8/*时刻*/, quint8/*相机索引*/, QVector<QPair<double,double>>&);

    Q_SIGNAL void reportPowerStatus(quint32, bool);
    Q_SIGNAL void reportVoltageStatus(quint32, bool);
    Q_SIGNAL void reportBackupPowerStatus(quint32, bool);
    Q_SIGNAL void reportBackupVoltageStatus(quint32, bool);
    Q_SIGNAL void reportBackupChannelStatus(quint32, bool);

    Q_SLOT void replyCaptureWaveformData(quint8, quint32, QByteArray&);
    Q_SLOT void replyCaptureSpectrumData(quint8, quint32, QByteArray&);
    Q_SLOT void replySettingFinished();

    Q_SLOT void startCapture(quint32 index, QString fileSavePath/*文件存储大路径*/, quint32 captureTimeSeconds/*保存时长*/, QString shotNum/*炮号*/);
    Q_SLOT void startAllCapture(QString fileSavePath/*文件存储大路径*/, quint32 captureTimeSeconds/*保存时长*/, QString shotNum/*炮号*/);
    Q_SLOT void stopCapture(quint32 index);
    Q_SLOT void stopAllCapture();

    /*获取设备数量*/
    quint32 numberOfDevices();

    /*获取设备列表*/
    QStringList enumDevices();

    /*初始化*/
    void initialize();

    /*设置采集参数，离线*/
    void setCaptureParamter(CaptureTime captureTime, quint8 cameraIndex, quint32 timeLength, quint32 time1);
    /*设置采集参数，在线线*/
    void setCaptureParamter(quint8 horCameraIndex, quint8 verCameraIndex, quint32 time1, quint32 time2, quint32 time3);

    bool openHistoryFile(QString filename);
    
    void analyzeHistoryWaveformData(quint8 cameraIndex, quint32 timeLength, quint32 remainTime, QString filePath);
    
    void analyzeHistorySpectrumData(quint8 cameraIndex, quint32 remainTime, QString filePath);

    bool switchPower(quint32, bool);
    bool switchVoltage(quint32, bool);
    bool switchBackupPower(quint32, bool);
    bool switchBackupVoltage(quint32, bool);
    bool switchBackupChannel(quint32, bool);

    /*指令集*/
    //死时间
    void writeDeathTime(quint16);
    //触发阈值
    void writeTriggerThold(quint16);
    /*********************************************************
     波形基本配置
    ***********************************************************/
    enum TriggerMode{
        tmTimer = 0x00,//定时触发模式
        tmNormal = 0x01//正常触发模式
    };
    enum WaveformLength{
        wl64 = 0x00,    //波形长度64
        wl128 = 0x01,   //波形长度128
        wl256 = 0x02,   //波形长度256
        wl512 = 0x03    //波形长度512
    };
    void writeWaveformMode(TriggerMode triggerMode = tmTimer, quint16 waveformLength = 64);
    /*********************************************************
     能谱基本配置
    ***********************************************************/
    //能谱刷新时间修改(ms，默认值1000)
    void writeSpectnumRefreshTimelength(quint32 spectrumRefreshTime = 1000);
    /*********************************************************
     工作模式配置
    ***********************************************************/
    enum WorkMode{
        wmWaveform = 0x00,//波形
        wmSpectrum = 0x01,//能谱
    };
    enum DataType{
        dtWaveform = 0x00D1,//波形
        dtSpectrum = 0x00D2,//能谱
    };
    void writeWorkMode(WorkMode workMode = wmSpectrum);
    /*********************************************************
     控制类指令
    ***********************************************************/
    //开始测量
    void writeStartMeasure();
    //停止测量
    void writeStopMeasure();

    void writeCommand(QByteArray& data);

signals:


private:
    QMap<quint32, CaptureThread*> mMapCaptureThread;
    QMap<quint32, HANDLE> mMapDevice;//访问数据设备句柄
    QMap<quint32, HANDLE> mMapUser;//设备用户句柄
    QMap<quint32, HANDLE> mMapBypass;//设备控制句柄
    QMap<quint32, bool> mMapPower;//探测器的1#电源开关
    QMap<quint32, bool> mMapVoltage;//探测器的1#电压开关
    QMap<quint32, bool> mMapBackupPower;//探测器的2#电源开关
    QMap<quint32, bool> mMapBackupVoltage;//探测器的2#电压开关
    QMap<quint32, bool> mMapChannel;//选通开关,true-1#,false-2#

    QStringList mDevices;
    QMap<quint32, bool> mThreadRunning;
    quint8 mCameraIndex = 1;/*相机序号*/
    quint8 mHorCameraIndex = 1;/*在线分析-水平相机序号*/
    quint8 mVerCameraIndex = 12;/*在线分析-垂直相机序号*/
    quint32 mTimestampMs1 = 10;/*分析时刻，单位ms*/
    quint32 mTimestampMs2 = 20;/*分析时刻，单位ms*/
    quint32 mTimestampMs3 = 30;/*分析时刻，单位ms*/
    quint32 mRemainTime = 0;/*剩余时间,单位ms*/
    CaptureTime mCaptureTime = oldCaptureTime;/*采集时间，单位ms*/
    quint32 mTimeLength = 1; /*要提取的波形时间长度，单位ms，默认1ms*/
};

#endif // PCIECOMMSDK_H
