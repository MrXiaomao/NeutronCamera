#ifndef DATAANALYSISWORKER_H
#define DATAANALYSISWORKER_H

#include <QObject>
#include <QMutex>
#include <QFile>
#include <QtEndian>
#include <QDebug>
#include <QDir>
#include <QThread>
#include <QWaitCondition>
#include <QThreadPool>
#include <QQueue>

#ifndef H5_DATA_COLS
#define H5_DATA_EXTEND      2       //触发时刻1（毫秒）+峰值1
#define RISING_WIDTH        20      //波形上升沿宽度
#define WAVEFORM_LENGTH     512     //波形上升沿参考点
#define H5_DATA_WAVEFORM    WAVEFORM_LENGTH     //扩展数据长度
#define H5_DATA_COLS        (H5_DATA_WAVEFORM + H5_DATA_EXTEND)
#endif //H5_DATA_COLS

// ====== 有界队列：最多缓存 N 个文件（N * 256MB 内存）======
struct FileJob {
    QString filePath;
    quint8 deviceIndex = 0;      // 1~6
    quint32 packerStartTime = 0; // ms
    //QByteArray data;             // 约 256MB
};

// 数据分析工作线程类
class DataAnalysisWorker : public QObject
{
    Q_OBJECT

public:
    explicit DataAnalysisWorker(QObject *parent = nullptr);
    ~DataAnalysisWorker();

    void setParameters(const QString& dataDir,
                       const QStringList& fileList,
                       const QString& outfileName,
                       int threshold,
                       int timePerFile,
                       int startTime,
                       int endTime);

    // 单个文件总大小：120 MB（整）
    // 文件头：128 bit = 16 字节（公共部分，跳过）
    // 文件尾：128 bit = 16 字节（跳过）
    // 中间全部是有效数据
    // 每个数据点 16 bit（2 字节，无符号）
    // 数据排列方式：ch2, ch2, ch0, ch0, ch1, ch1...（逐点交织）
    static bool readBin3Ch_fast(const QString& filePath,
                                QVector<qint16>& ch0,
                                QVector<qint16>& ch1,
                                QVector<qint16>& ch2,
                                bool littleEndian = true);

    static bool readBin3Ch_fast(const QByteArray& fileData,
                                QVector<qint16>& ch0,
                                QVector<qint16>& ch1,
                                QVector<qint16>& ch2,
                                bool littleEndian = true);

    // 计算基线值：使用直方图方法，找到出现频率最高的值作为基线
    // 当波形信号过多时，该方法明显会出现问题（暂时采用该方法）
    static qint16 calculateBaseline(const QVector<qint16>& data_ch);

    // 根据基线调整数据：根据板卡编号和通道号对数据进行不同的调整
    // boardNum: 板卡编号 1-6，根据编号的奇偶性判断（1,3,5为奇数板卡，2,4,6为偶数板卡）
    // ch: 1-4 (通道号)
    //     ch1和ch2：根据板卡编号的奇偶性决定调整方式
    //     ch3：data - baseline（与板卡编号无关）
    //     ch4：baseline - data（与板卡编号无关）
    static void adjustDataWithBaseline(QVector<qint16>& data_ch, qint16 baseline_ch, int boardNum, int ch);

    // 提取超过阈值的有效波形数据
    // data: 输入数据（已扣基线）
    // ch: 通道号（用于日志输出）
    // threshold: 触发阈值
    // pre_points: 触发点之前的点数
    // post_points: 触发点之后的点数
    // 返回: 所有提取的波形，每个波形是固定长度512的数组（优化内存使用）
    static QVector<std::array<qint16, H5_DATA_COLS>>/*波形数据*/ overThreshold(quint16 packerStartTime/*毫秒*/,
                                                                                const QVector<qint16>& data,
                                                                                int ch, int threshold,
                                                                                int pre_points/*触发阈值往前多少个点*/,
                                                                                int post_points/*触发阈值往后多少个点*/);

    void getValidWave();

    // 将波形数据按板卡分组写入HDF5文件
    // filePath: HDF5文件路径
    // boardNum: 板卡编号 (1-6)
    // wave_ch0, wave_ch1, wave_ch2, wave_ch3: 4个通道的波形数据
    // 返回: 是否成功写入
    static bool writeWaveformToHDF5(const QString& filePath, int boardNum,
                                    const QVector<std::array<qint16, H5_DATA_COLS>>& wave_ch0,
                                    const QVector<std::array<qint16, H5_DATA_COLS>>& wave_ch1,
                                    const QVector<std::array<qint16, H5_DATA_COLS>>& wave_ch2);

    // 波形文件头部信息(开始时刻、结束时刻、阈值)
    static bool writeWaveformHeadToHDF5(const QString& filePath, quint32 packerStartTime, quint32 packerEndTime, quint32 threshold);
    static bool readWaveformHeadFromHDF5(const QString& filePath, quint32& packerStartTime, quint32& packerEndTime, quint32& threshold);

public slots:
    void startAnalysis();
    void cancelAnalysis();

signals:
    void logMessage(const QString& msg, QtMsgType msgType);
    void progressUpdated(int current, int total);
    void analysisFinished(bool success, const QString& message);
    void analysisError(const QString& error);

private:

    QString mDataDir;
    QStringList mFileList;
    QString mOutfileName;
    int mThreshold;
    int mTimePerFile;
    int mStartTime;
    int mEndTime;

    QMutex mMutex;
    bool mCancelled;
};

class BoundedFileQueue {
public:
    explicit BoundedFileQueue(int capacity) : mCapacity(qMax(1, capacity)) {}

    void push(FileJob&& job) {
        QMutexLocker lk(&mMutex);
        while (!mStopped && mQueue.size() >= mCapacity) {
            mNotFull.wait(&mMutex);
        }
        if (mStopped) return;
        mQueue.enqueue(std::move(job));
        mNotEmpty.wakeOne();
    }

    // 返回 false：队列已结束（stop() 已调用且队列空）
    bool pop(FileJob& out) {
        QMutexLocker lk(&mMutex);
        while (!mStopped && mQueue.isEmpty()) {
            mNotEmpty.wait(&mMutex);
        }
        if (mQueue.isEmpty()) return false;

        out = std::move(mQueue.front());
        mQueue.dequeue();
        mNotFull.wakeOne();
        return true;
    }

    void stop() {
        QMutexLocker lk(&mMutex);
        mStopped = true;
        mNotEmpty.wakeAll();
        mNotFull.wakeAll();
    }

private:
    int mCapacity = 2;
    QQueue<FileJob> mQueue;
    QMutex mMutex;
    QWaitCondition mNotEmpty;
    QWaitCondition mNotFull;
    bool mStopped = false;
};

// ====== 解析/提取任务：从内存 data 解析 3 通道并提取波形 ======
class ExtractValidWaveformFromBufferTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    ExtractValidWaveformFromBufferTask(FileJob&& job,
                                       quint8 cameraIndex, // 0=所有通道
                                       int threshold,
                                       int pre_points,
                                       int post_points,
                                       std::function<void(quint32 packerCurrentTime,
                                                          quint8 channelIndex,
                                                          QVector<std::array<qint16, H5_DATA_COLS>>&)> cb,
                                       std::function<void()> onFinished = {})
        : mJob(std::move(job))
        , mCameraIndex(cameraIndex)
        , mThreshold(threshold)
        , mPre(pre_points)
        , mPost(post_points)
        , mCallback(std::move(cb))
        , mOnFinished(std::move(onFinished))
    {
        setAutoDelete(true);
    }

    void run() override {
        // 1) 从 buffer 解交织出 4 通道
        QVector<qint16> ch[3];
        if (!DataAnalysisWorker::readBin3Ch_fast(mJob.filePath, ch[0], ch[1], ch[2], true)) {
            if (mOnFinished) mOnFinished();
            return;
        }

        // 2) 通道选择逻辑（0=全通道；否则按相机号映射板卡+通道）
        quint8 deviceIndex = mJob.deviceIndex;
        int cameraNo = 0;
        if (mCameraIndex != 0) {
            //只有nγ甄别才会进入到此处
            deviceIndex = (mCameraIndex - 1) / 3 + 1;
            cameraNo = (mCameraIndex - 1) % 3; // 0..2
        }

         quint32 packerCurrentTime = mJob.packerStartTime;

        // 3) 基线 + 调整 + 过阈提取（每个通道独立）
        if (cameraNo == 0 || mCameraIndex == 0) {
            qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[0]);
            DataAnalysisWorker::adjustDataWithBaseline(ch[0], baseline_ch, deviceIndex, 1);
            auto wave = DataAnalysisWorker::overThreshold(mJob.packerStartTime, ch[0], 1, mThreshold, mPre, mPost);
            mCallback(packerCurrentTime, 1, wave);
        }
        if (cameraNo == 1 || mCameraIndex == 0) {
            qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[1]);
            DataAnalysisWorker::adjustDataWithBaseline(ch[1], baseline_ch, deviceIndex, 2);
            auto wave = DataAnalysisWorker::overThreshold(mJob.packerStartTime, ch[1], 2, mThreshold, mPre, mPost);
            mCallback(packerCurrentTime, 2, wave);
        }
        if (cameraNo == 2 || mCameraIndex == 0) {
            qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[2]);
            DataAnalysisWorker::adjustDataWithBaseline(ch[2], baseline_ch, deviceIndex, 3);
            auto wave = DataAnalysisWorker::overThreshold(mJob.packerStartTime, ch[2], 3, mThreshold, mPre, mPost);
            mCallback(packerCurrentTime, 3, wave);
        }

        if (mOnFinished) mOnFinished();
    }

private:
    FileJob mJob;
    quint8 mCameraIndex = 0;
    int mThreshold = 200;
    int mPre = 20;
    int mPost = 200;
    std::function<void(quint32, quint8, QVector<std::array<qint16, H5_DATA_COLS>>&)> mCallback;
    std::function<void()> mOnFinished;
};

#endif // DATAANALYSISWORKER_H
