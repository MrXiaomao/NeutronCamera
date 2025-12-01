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

    Q_SLOT void replyCaptureData(QByteArray& data){
        QMutexLocker locker(&mMutexWrite);
        mCachePool.push_back(data);
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
                QByteArray data = tempPool.at(0);
                WriteFileTask *task = new WriteFileTask(mIndex, mPackref++, mSaveFilePath, data);
                connect(task, &WriteFileTask::reportFileWriteElapsedtime, this, &WriteFileThread::reportFileWriteElapsedtime);
                pool->start(task);

                // QElapsedTimer elapsedTimer;
                // elapsedTimer.start();
                // QDateTime now = QDateTime::currentDateTime();
                // QString filename = QString("%1/%2data%3.bin").arg(mSaveFilePath).arg(mIndex).arg(mPackref++);

                // HANDLE hfOutput = CreateFileA(filename.toStdString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                // if (!hfOutput) {
                //     qDebug() << "CreateFileA fail, win32 error code:" << GetLastError();
                // }
                // else{
                //     DWORD NumberOfBytesWritten = 0;
                //     if (!WriteFile(hfOutput, data.constData(), data.size(), &NumberOfBytesWritten, NULL)){
                //         qDebug() << "WriteFile fail, win32 error code:" << GetLastError();
                //     }
                //     CloseHandle(hfOutput);

                //     emit reportFileWriteElapsedtime(mIndex,elapsedTimer.elapsed());
                // }

                tempPool.pop_front();
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
    explicit CaptureThread(const quint32 index, HANDLE hFile, const QString &saveFilePath, quint32 captureTimeSeconds)
        : mIndex(index)
        , mDeviceHandle(hFile)
        , mSaveFilePath(saveFilePath)
        , mCaptureTimeSeconds(captureTimeSeconds)
    {
        connect(this, &QThread::finished, this, &QThread::deleteLater);

        mWriteFileThread = new WriteFileThread(index, saveFilePath);
        connect(this, QOverload<QByteArray&>::of(&CaptureThread::reportCaptureData), mWriteFileThread, &WriteFileThread::replyCaptureData);
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

        // 打开输入
        if (mDeviceHandle == INVALID_HANDLE_VALUE) {
            emit reportCaptureFail(mIndex, GetLastError());
            return;
        }

        LARGE_INTEGER inAddress;
        inAddress.QuadPart = 0x00000000;
        DWORD nNumberOfBytesToRead = 268435456;
        unsigned char* lpBuffer = allocate_buffer(nNumberOfBytesToRead, 0);
        if (lpBuffer == NULL){
            emit reportCaptureFail(mIndex, GetLastError());
            return;
        }

        quint32 packingDuration = 50; // 打包时长
        quint32 captureRef = quint32((double)mCaptureTimeSeconds / 50.0 + 0.4);

        //启动写文件线程
        mWriteFileThread->start();

        quint8 eventflag = 0;
        quint32 mPackref = 1; //打包次数
        qDebug() << "createCaptureThread id:" << this->currentThreadId();
        while (!this->isInterruptionRequested())
        {
            QElapsedTimer elapsedTimer;
            elapsedTimer.start();
            DWORD nNumberOfBytesRead = 0;

            if (eventflag == 0x00)
                inAddress.QuadPart = 0x00000000;
            else
                inAddress.QuadPart = 0x10000000;

            ///////////////////////////////
            /// 先读前面1个字节的标识位，如果标识为为1，表示可读
            while (!this->isInterruptionRequested())
            {
                DWORD nNumberOfBytesRead = 0;
                if (!ReadFile(mDeviceHandle, lpBuffer, 1, &nNumberOfBytesRead, NULL)) {
                    qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
                    emit reportCaptureFail(mIndex, GetLastError());
                    break;
                }
                else{
                    if (lpBuffer[0] == 0x00){
                        QThread::usleep(1);
                        continue;
                    }
                }
            }
            ///////////////////////////////

            if (this->isInterruptionRequested())
                break;

            if (!SetFilePointerEx(mDeviceHandle, inAddress, NULL, FILE_BEGIN)) {
                qDebug() << "Error setting file pointer, win32 error code:" << GetLastError();
                emit reportCaptureFail(mIndex, GetLastError());
                break;
            }

            qDebug() << mIndex << (mPackref-1) << "Elapsed time:" << elapsedTimer.elapsed();
            if (!ReadFile(mDeviceHandle, lpBuffer, nNumberOfBytesToRead, &nNumberOfBytesRead, NULL)) {
                qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
                emit reportCaptureFail(mIndex, GetLastError());
                break;
            }
            else{
                ///////////////////////////////
                /// 数据读完了，把标识位恢复回去
                lpBuffer[0] = 0x00;
                DWORD nNumberOfBytesWrite = 0;
                if (!WriteFile(mDeviceHandle, lpBuffer, 1, &nNumberOfBytesWrite, NULL)) {
                    qDebug() << "WriteFile fail, win32 error code:" << GetLastError();
                    emit reportCaptureFail(mIndex, GetLastError());
                    break;
                }
                ///////////////////////////////

                qDebug() << mIndex << (mPackref-1) << "Elapsed time1:" << elapsedTimer.elapsed();
                emit reportFileReadElapsedtime(mIndex, elapsedTimer.elapsed());
                if (nNumberOfBytesToRead != nNumberOfBytesRead){
                    qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
                }

                QByteArray data = QByteArray::fromRawData((const char *)lpBuffer, nNumberOfBytesRead);
                emit reportCaptureData(data); //发给写文件线程
                emit reportCaptureData(mIndex, mPackref, data);//发给数据分析

                if (mPackref++ >= this->mCaptureRef){
                    emit reportThreadExit(mIndex);
                    mWriteFileThread->wait();
                    mWriteFileThread->deleteLater();
                    break;
                }
            }

            eventflag = (eventflag==0) ? 1 : 0;
            qDebug() << mIndex << (mPackref-1) << "Elapsed time2:" << elapsedTimer.elapsed();
            qint32 sleepTime = qMax((qint32)0, (qint32)(packingDuration - elapsedTimer.elapsed()));
            QThread::msleep(sleepTime);
        }

        //等待所有录制数据写入硬盘
        emit reportCaptureFinished(mIndex);

        if (lpBuffer)
            _aligned_free(lpBuffer);

        qDebug() << "destroyCaptureThread id:" << this->currentThreadId();
    }

    Q_SIGNAL void reportCaptureFail(quint32, quint32);
    Q_SIGNAL void reportThreadExit(quint32);
    Q_SIGNAL void reportCaptureData(QByteArray& data);
    Q_SIGNAL void reportCaptureData(quint32,quint32,QByteArray& data);
    Q_SIGNAL void reportFileReadElapsedtime(quint32, quint32);
    Q_SIGNAL void reportFileWriteElapsedtime(quint32, quint32);
    Q_SIGNAL void reportCaptureFinished(quint32);

private:
    WriteFileThread* mWriteFileThread = nullptr;
    quint32 mIndex;//设备名称
    HANDLE mDeviceHandle;//设备句柄
    QString mSaveFilePath;//保存路径
    quint32 mCaptureTimeSeconds = 50;
    quint32 mCaptureRef = 1;
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

    enum SpectrumType {
        PSD = 0x1,
        FOM = 0x2
    };

    Q_SIGNAL void reportNotFoundDevices();
    Q_SIGNAL void reportCaptureFail(quint32, quint32);
    Q_SIGNAL void reportFileReadElapsedtime(quint32, quint32);
    Q_SIGNAL void reportFileWriteElapsedtime(quint32, quint32);
    Q_SIGNAL void reportCaptureFinished();
    Q_SIGNAL void reportWaveform(quint8, quint8, QVector<quint16>&);
    Q_SIGNAL void reportNeutronSpectrum(quint8, quint8, QVector<quint16>&);
    Q_SIGNAL void reportGammaSpectrum(quint8, quint8, QVector<quint16>&);

    Q_SLOT void replyCaptureData(quint32, quint32, QByteArray&);
    Q_SLOT void replySettingFinished();

    Q_SLOT void startCapture(quint32 index, QString fileSavePath/*文件存储大路径*/, quint32 captureTimeSeconds/*保存时长*/, quint32 shotNum/*炮号*/);
    Q_SLOT void startAllCapture(QString fileSavePath/*文件存储大路径*/, quint32 captureTimeSeconds/*保存时长*/, quint32 shotNum/*炮号*/);
    Q_SLOT void stopCapture(quint32 index);
    Q_SLOT void stopAllCapture();

    /*获取设备数量*/
    quint32 numberOfDevices();

    /*获取设备列表*/
    QStringList enumDevices();

    /*初始化*/
    void initialize();

    /*设置采集参数*/
    void setCaptureParamter(quint32, quint32, quint32, quint32, quint32);

    bool openHistoryData(QString filename);

    bool switchPower(quint32, bool);
    bool switchVoltage(quint32, bool);
    bool switchChannel(quint32, bool);

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
    QMap<quint32, HANDLE> mMapUser;//设备控制句柄
    QMap<quint32, bool> mMapPower;//探测器的电源开关
    QMap<quint32, bool> mMapVoltage;//探测器的电压开关
    QMap<quint32, bool> mMapChannel;//选通开关,true-1#,false

    QStringList mDevices;
    QMap<quint32, bool> mThreadRunning;
    quint32 mHorCameraIndex = 1;
    quint32 mVerCameraIndex = 12;
    quint64 mTimestampMs1 = 1000, mTimestampMs2 = 2000, mTimestampMs3 = 3000;
};

#endif // PCIECOMMSDK_H
