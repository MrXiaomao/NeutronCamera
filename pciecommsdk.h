#ifndef PCIECOMMSDK_H
#define PCIECOMMSDK_H

#include <QObject>
#include <QDebug>
#include "pcieiocpreader.h"

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
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

// pciecommsdk.h 头文件
class NoBufferingFile : public QFile
{
public:
    // 去掉override，使用正确的参数类型
    bool open(const QString &fileName, QIODevice::OpenMode mode);
};


#include <cstring>
class CaptureThread : public QThread {
    Q_OBJECT
public:
    // 共享数据（原子变量+条件变量）
    struct SharedData {
        std::atomic<bool> trigger{false};          // 原子变量：线程A修改，线程B读取
        QWaitCondition cond;            // 条件变量：线程B等待通知
        QMutex mutex;                   // 条件变量必须配合互斥锁（Qt要求）
    };

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
    explicit CaptureThread(const quint32 cardIndex, const QString& devPath, bool isDDR1 = true);
    ~CaptureThread();

    void run() override;
    void delay(quint32 us);
    bool checkDataError();
    void printDebugInfo();
    bool dataExistError();
    bool writeFileWithNoBuffering(const QString &filePath, const char *data, qint64 size);

    void setParamter(const QString &saveFilePath, quint32 captureTimeSeconds, bool testMode);

    void pause(){
        QMutexLocker locker(&mMutex);
        mIsPaused.store(true);
    }

    void resume(){
        QMutexLocker locker(&mMutex);
        mIsPaused.store(false);
        mCondition.wakeOne();
    }

    void stop(){
        QMutexLocker locker(&mMutex);
        mIsStopped.store(true);
        mCondition.wakeOne();
    }

    bool startMeasure();
    void stopMeasure();
    void clear();
    void empty();

    /**
    * @function name:readWaveformData
    * @brief 从DDR读波形原始数据
    * @param[in]        data
    * @param[out]       data
    * @return           void
    */
    bool readWaveformData(quint8 index, const QByteArray& data, const quint64 offset);

    /**
    * @function name:readSpectrumData
    * @brief 从RAM读能谱数据
    * @param[in]        data
    * @param[out]       data
    * @return           bool
    */
    bool readSpectrumData(quint8 index, const QByteArray& data, const quint64 offset);

    bool readDataAsync(HANDLE fd, quint64 offset, const QByteArray& data);

    HANDLE userHandle() const{
        return mUserHandle;
    };

    Q_SIGNAL void reportCaptureFail(quint32, quint32);
    Q_SIGNAL void reportThreadExit(quint32);
    Q_SIGNAL void reportCaptureWaveformData(quint8,quint32,const QByteArray& data);
    Q_SIGNAL void reportCaptureSpectrumData(quint8,bool,quint32,const QByteArray& data);
    Q_SIGNAL void reportCaptureFinished(quint32, bool);

private:
    quint32 mIsDDR1 = true;//
    quint32 mCardIndex;//板卡索引
    QString mDevPath;//板卡设备路径
#if ENABLE_IOCP
    PcieIocpReader* mPcieReader = nullptr;
#else
    HANDLE mDDRHandle[4];//设备句柄
#endif // ENABLE_IOCP
    HANDLE mRAMHandle[4];//RAM句柄
    HANDLE mUserHandle;//用户句柄
    bool mIsRegisterInvalid = false;
    int mRegisterInvalidPosition = 0;

    QString mSaveFilePath;//保存路径
    quint16 mCaptureCount = 1;//需要采集的总包数据
    quint16 mCapturedRef = 1;//已经采集的包数
    quint32 mTimeout = 5000;//超时微秒
    bool mEnableTestMode = false;//启用测试模式
    bool mInterruptSave = false;//是否终止
    bool mIsException = false;//数据是否出现异常
    QVector<QByteArray> mDDRWaveformDatas;
    QVector<QByteArray> mRAMSpectrumDatas;

    QMutex mMutex;
    QWaitCondition mCondition;
    std::atomic<bool> mIsPaused = true;
    std::atomic<bool> mIsStopped = false;
    QMutex mEventMutex[2];
    QWaitCondition mInterruptEvent[2];

    SharedData mIrq1Trigger, mIrq2Trigger;
    bool mDataError = false;
    QMutex mReadLocker;

    QVector<qint64> zeroTime;//记录0时刻
    QVector<qint64> mRamChangedTime;// RAM值改变的时间
    QVector<qint64> mBeforeReadTime;// DDR读之前的时间
    QVector<qint64> mAfterReadTime;// DDR读之后的时间
    QVector<qint64> mCreateThreadTime;// 创建线程时间
    QVector<qint64> mAfterCreateThreadTime;// 创建线程时间
    QMap<quint16, QVector<QPair<qint64, quint8>>> mDDRReadTime; // 记录DDR读取时间
    QMap<quint16/*包序号*/, QVector<QPair<qint64/*读寄存器前时刻*/, QPair<qint64/*读寄存器前时刻*/, quint8/*寄存器值*/>>>> mRAMReadTime; // 记录RAM读取时间
};

#define CAMNUMBER_DDR_PER   3   // 每张PCIe对应一个Fpga数采板，每个数采板对应的是8个探测器（但是考虑带宽可能只用到了6路，分2个DDR存储数据，所以每个DDR存储3路）
#define DETNUMBER_PCIE_PER  6   // 每张PCIe对应一个Fpga数采板，每个数采板对应的是8个探测器（但是考虑带宽可能只用到了6路，分2个DDR存储数据，所以每个DDR存储3路）
#define DETNUMBER_MAX       18  // 探测器有效数只用到了18路（11路水平+7路垂直）
#define PACKET_TIMELENGTH   40 // 每个文件对应采集时间40ms
class PCIeCommSdk : public QObject
{
    Q_OBJECT
public:
    explicit PCIeCommSdk(QObject *parent = nullptr);
    ~PCIeCommSdk();

    // 封装设备信息，带启用状态标记
    struct DeviceInfo {
        QString devicePath;  // 设备路径
        bool isEnabled;      // 是否启用
    };

    enum CameraOrientation {
        Horizontal = 0x1,
        Vertical = 0x2
    };

    enum CaptureTime {
        oldCaptureTime = 66,//66ms 单个文件对应采集时间，旧版数据采集协议
        newCaptureTime = 40,//40ms 单个文件对应采集时间，新版数据采集协议
    };

    enum SpectrumType {
        PSD = 0x1,
        FOM = 0x2
    };

    Q_SIGNAL void reportNotFoundDevices();
    Q_SIGNAL void reportOpenDeviceFail(quint8);
    Q_SIGNAL void reportCaptureFail(quint32, quint32);
    Q_SIGNAL void reportCaptureFinished();
    Q_SIGNAL void doWaveform(quint8/*时刻*/, quint8/*相机索引*/, QVector<QPair<double,double>>&);
    Q_SIGNAL void reportNeutronSpectrum(quint8/*时刻*/, quint8/*相机索引*/, QVector<QPair<double,double>>&);
    Q_SIGNAL void reportGammaSpectrum(quint8/*时刻*/, quint8/*相机索引*/, QVector<QPair<double,double>>&);

    Q_SLOT void replyCaptureWaveformData(quint8, bool, quint32, const QByteArray&);
    Q_SLOT void replyCaptureSpectrumData(quint8, bool, quint32, const  QByteArray&);
    Q_SLOT void replySettingFinished();

    Q_SLOT void startCapture(quint32 index, QString fileSavePath/*文件存储大路径*/, quint32 captureTimeSeconds/*保存时长*/, QString shotNum/*炮号*/, bool testMode = false/*测试模式*/);
    Q_SLOT void startAllCapture(QString fileSavePath/*文件存储大路径*/, quint32 captureTimeSeconds/*保存时长*/, QString shotNum/*炮号*/);
    Q_SLOT void stopCapture(quint32 index);
    Q_SLOT void stopAllCapture();

    Q_SLOT void init(); /* 初始化 */
    Q_SLOT void reset();/* 重置 */
    Q_SLOT void reboot();/* 重启 */
    Q_SLOT void setPSDThreshold();/* 设置PSD甄别阈值 */
    Q_SLOT bool test();

    /*获取设备数量*/
    quint32 numberOfDevices();

    /*输出设备信息*/
    void printDevicesInfomation();

    /*判断板卡是否存在*/
    bool boardExists(const quint8& index/*板卡序号1-3*/);

    /*获取设备列表*/
    QStringList enumDevices();
    QList<DeviceInfo> enumerateSpecifiedDevices(const GUID& deviceClassGuid = {0x74c7e4a9, 0x6d5d, 0x4a70, {0xbc, 0x0d, 0x20, 0x69, 0x1d, 0xff, 0x9e, 0x9d}});
    // 根据硬件ID找到目标设备并修改状态
    BOOL changeDeviceStateByHardwareId(LPCTSTR lpszHardwareId, BOOL bEnable);


    /*根据卡名称判断卡序号*/
    quint8 boardNameToBoardIndex(const QString& name);

    /*初始化*/
    void initCaptureThreads();

    /*设置采集参数，离线*/
    void setCaptureParamter(CaptureTime captureTime, quint8 cameraIndex, quint32 timeLength, quint32 tmMeasure);
    /*设置采集参数，在线*/
    void setCaptureParamter(quint8 horCameraIndex, quint8 verCameraIndex, QVector<quint32> tmMeasure);
    
    bool analyzeHistorySpectrumData(quint8 cameraIndex,
                                    quint8 timeIndex/*时刻索引1~3*/,
                                    quint32 remainTime/**/ ,
                                    QString filePath/*文件名*/);

    // 根据通道号、开始时间、结束时间以及文件存储目录解析波形数据
    bool analyzeHistoryWaveformData(
                                const quint8& cameraIndex,
                                const quint32& timeStart/*开始时刻ms*/,
                                const quint32& timeStop/*结束时刻ms*/,
                                const QString& fileDir/*文件存储路径*/,
                                std::function<void(
                                    const QMap<quint64/*时刻（ns）*/,qint16/*数值*/>&
                                    )> callback);

    // 从H5文件解析计数率信息和能谱信息
    bool analyzeHistoryCpsData(const quint32 channels/*多道道数（统计能谱用）*/,
                               const quint32 timeWidth/*时间宽度ms（统计计数率用）*/,
                               const quint32 timeStart/*开始时刻ms*/,
                               const quint32 timeStop/*结束时刻ms*/,
                               const QString& filePath/*H5文件路径*/,
                               std::function<void(
                                   QMap<quint8/*通道号*/, QMap<quint16/*时刻（ms）*/,quint32/*计数率*/>>,
                                   QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>>
                                   )> callback,
                                const quint32 minPeak = 0/*最小峰值0*/,
                                const quint32 maxPeak = 16384/*最大峰值16384*/);

    /*指令集*/
    //死时间
    void writeDeathTime(quint16);
    //触发阈值
    void writeTriggerThold(quint16);

    static QByteArray reverseArray(const QByteArray& data, quint8 offset = 12);

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
    //发送指令
    void writeCommand(QByteArray& data);

    inline static bool writeData(quint8 cardIndex, HANDLE hFile, quint64 offset, const QByteArray& data);
    inline static bool readData(HANDLE hFile, quint64 offset, const QByteArray& data);

    HANDLE getHandle(quint8 cardIndex, quint32 flags = GENERIC_READ | GENERIC_WRITE, quint32 dwFlagsAndAttributes = 0);//O_RDWR
    static HANDLE getHandle(QString path, quint32 flags = GENERIC_READ | GENERIC_WRITE, quint32 dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_FLAG_NO_BUFFERING);//O_RDWR

    enum MeasureMode{
        mmTest          = 0x01,// 测试模式
        mmSingle        = 0x02,// 单次测量
        mmContinue      = 0x04,// 连续测量
        mmSingleTest    = mmTest | mmSingle,// 单次测量
        mmContinueTest  = mmTest | mmContinue,// 连续测量
    };
    void setMeasureMode(MeasureMode mm = mmSingle){mMeasureMode = mm;};

signals:

private:
    QMap<quint32, CaptureThread*> mMapCaptureThread;
    QStringList mDevices;
    QMap<quint32, bool> mThreadRunning;
    quint8 mCameraIndex = 1;/*相机序号*/
    quint8 mHorCameraIndex = 1;/*在线分析-水平相机序号*/
    quint8 mVerCameraIndex = 12;/*在线分析-垂直相机序号*/
    QVector<quint32> mTimestampMs;/*分析时刻，单位ms*/
    quint32 mRemainTime = 0;/*剩余时间,单位ms*/
    CaptureTime mCaptureTime = oldCaptureTime;/*采集时间，单位ms*/
    quint32 mTimeLength = 1; /*要提取的波形时间长度，单位ms，默认1ms*/
    MeasureMode mMeasureMode = mmSingle;
};

#endif // PCIECOMMSDK_H
