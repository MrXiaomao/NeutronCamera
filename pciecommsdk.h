#ifndef PCIECOMMSDK_H
#define PCIECOMMSDK_H

#include <QObject>
#include <QDebug>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <setupapi.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

typedef int HANDLE;
#define CloseHandle close
#endif

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
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly)) {
            qDebug() << "Cannot open file for writing";
        }
        else{
            file.write(mData);
            file.close();
        }

        emit reportFileWriteElapsedtime(mIndex,elapsedTimer.elapsed());
    }

    Q_SIGNAL void reportFileWriteElapsedtime(quint32,quint32);

private:
    quint32 mIndex;
    quint32 mPackref;
    QString mSaveFilePath;
    QByteArray mData;
};

class DataCachPoolThread : public QThread {
    Q_OBJECT
public:
    explicit DataCachPoolThread();

    void run() override;

    void setParamter(const quint32 cardIndex, const QString &saveFilePath);

    Q_SIGNAL void reportFileWriteElapsedtime(quint32,quint32);
    Q_SIGNAL void reportCaptureWaveformData(quint8,quint32,QByteArray& data);
    Q_SIGNAL void reportCaptureSpectrumData(quint8,quint32,QByteArray& data);

    Q_SLOT void replyThreadExit();
    Q_SLOT void replyCaptureData(const QByteArray& waveformData, const QByteArray& spectrumData);

    /**
    * @function name: reverseArray
    * @brief 对数据每16字节做个反转
    * @param[in]        data
    * @param[out]       data
    * @return           void
    */
    QByteArray reverseArray(const QByteArray& data);

private:
    quint32 mCardIndex;//设备名称
    QString mSaveFilePath;//保存路径
    QVector<QByteArray> mCachePool;
    unsigned char *mTotalBytes[10];
    bool mTerminated = false;
    QMutex mMutexWrite;
    QElapsedTimer mElapsedTimer;

    quint32 mPackref = 0;
    bool mReady = false;
    QWaitCondition mCondition;
};

class CaptureThread : public QThread {
    Q_OBJECT
public:
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
    explicit CaptureThread(const quint32 cardIndex,
                           HANDLE hFile,
                           HANDLE hUser,
                           HANDLE hBypass);

    void run() override;

    void setParamter(const QString &saveFilePath, quint32 captureTimeSeconds);

    void pause(){
        QMutexLocker locker(&mMutex);
        mIsPaused = true;
    }

    void resume(){
        QMutexLocker locker(&mMutex);
        mIsPaused = false;
        mCondition.wakeOne();
    }

    void stop(){
        QMutexLocker locker(&mMutex);
        mIsStopped = true;
        mCondition.wakeOne();
    }

    /**
    * @function name:prepared
    * @brief 第一次查询准备
    * @param[in]
    * @param[out]
    * @return           bool
    */
    bool prepared();

    /**
    * @function name:canRead
    * @brief 判断DDR和RAM数据是否填满可读
    * @param[in]
    * @param[out]
    * @return           bool
    */
    bool canRead();

    /**
    * @function name:resetReadflag
    * @brief 清空DDR和RAM满状态
    * @param[in]
    * @param[out]
    * @return           bool
    */
    bool emptyStatus();

    /**
    * @function name:resetReadflag
    * @brief 重置可读写标识
    * @param[in]
    * @param[out]
    * @return           bool
    */
    bool resetReadflag();

    /**
    * @function name:readWaveformData
    * @brief 从DDR读波形原始数据
    * @param[in]        data
    * @param[out]       data
    * @return           void
    */
    bool readWaveformData(const QByteArray& data, const int offset = 0);

    /**
    * @function name:readSpectrumData
    * @brief 从RAM读能谱数据
    * @param[in]        data
    * @param[out]       data
    * @return           bool
    */
    bool readSpectrumData(const QByteArray& data, const int offset = 0);

    Q_SIGNAL void reportCaptureFail(quint32, quint32);
    Q_SIGNAL void reportThreadExit(quint32);
    Q_SIGNAL void reportCaptureData(const QByteArray& waveformData, const QByteArray& spectrumData);
    Q_SIGNAL void reportCaptureWaveformData(quint8,quint32,QByteArray& data);
    Q_SIGNAL void reportCaptureSpectrumData(quint8,quint32,QByteArray& data);
    Q_SIGNAL void reportFileReadElapsedtime(quint32, quint32);
    Q_SIGNAL void reportFileWriteElapsedtime(quint32, quint32);
    Q_SIGNAL void reportCaptureFinished(quint32);

private:
    DataCachPoolThread* mDataCachPoolThread = nullptr;
    quint32 mCardIndex;//设备名称
    HANDLE mDeviceHandle;//设备句柄
    HANDLE mUserHandle;//用户句柄
    HANDLE mBypassHandle;//RAM句柄

    QString mSaveFilePath;//保存路径
    quint32 mCaptureTimeSeconds = 50;
    quint32 mCaptureRef = 1;
    quint32 mTimeout = 5000;//超时微秒

    quint32 mCapturedRef = 0;
    QVector<QByteArray> mWaveformDatas;
    QVector<QByteArray> spectrumDatas;

    QMutex mMutex;
    QWaitCondition mCondition;
    bool mIsPaused = false;
    bool mIsStopped = false;
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
    void initializeDevices();
    void initCaptureThreads();

    /*设置采集参数，离线*/
    void setCaptureParamter(CaptureTime captureTime, quint8 cameraIndex, quint32 timeLength, quint32 time1);
    /*设置采集参数，在线线*/
    void setCaptureParamter(quint8 horCameraIndex, quint8 verCameraIndex, quint32 time1, quint32 time2, quint32 time3);

    bool openHistoryFile(QString filename);
    
    void analyzeHistoryWaveformData(quint8 cameraIndex, quint32 timeLength, quint32 remainTime, QString filePath);
    
    void analyzeHistorySpectrumData(quint8 cameraIndex, quint8 timeIndex, quint32 remainTime, QString filePath);

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

    static bool writeData(HANDLE hFile, quint64 offset, const QByteArray& data);
    static bool readData(HANDLE hFile, quint64 offset, const QByteArray& data);

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
