#ifndef PCIEIOCPREADER_H
#define PCIEIOCPREADER_H

#include <QObject>
#include <QMutex>
#include <vector>
#include <windows.h>

#pragma comment(lib, "setupapi.lib")

class PcieIocpReader : public QObject
{
    Q_OBJECT
public:
    explicit PcieIocpReader(QObject *parent = nullptr);
    ~PcieIocpReader();

    // 保持枚举不变
    enum class PcieStep : int {
        Stop   = 0x00,
        Step11 = 0x11,
        Step12 = 0x12,
        Step14 = 0x14,
        Step18 = 0x18,
        StepMax  = Step18 // 一共只有5种，直接对应数组大小
    };

    bool init(const QString& devicePath,
              bool isSecondDdr = false,       // 是否初始化第二个DDR（true为DDR2，基址0x40000000；false为DDR1，基址0x00000000）
              int blocksPerDdr = 280,         // 预分配内存块数量
              DWORD blockStride = 0x07271400, // 块之间的步长
              DWORD readSize = 0x07270E00);   // 单块实际读取大小
    void stop();
    void submitReadRequestByStep(quint8 step);

    void resetReadCount();
    quint64 getReadCount() const;

    bool getCompletedBlock(int blockIndex, BYTE*& outBuffer, DWORD& outBytesRead);
    virtual void onPcieReadComplete(int blockIndex, BYTE* buffer, DWORD bytesRead);

private:
    int stepToBlockIndex(PcieStep step); // step → 块索引映射

    static unsigned int __stdcall workerThread(void* param);

private:
    // 单个内存块结构
    struct PcieReadBlock {
        BYTE* buffer;               // 预分配缓冲区
        DWORD bufferSize;           // 缓冲区大小（实际读取长度）
        OVERLAPPED* pOverlapped;    // 异步IO结构体
        DWORD bytesRead;            // 本次读取实际字节数
        bool isCompleted;           // 是否已完成读取
        bool isInUse;               // 是否正在被处理
    };

    HANDLE          m_hPcieDevice;     // PCIe设备句柄
    HANDLE          m_ioCompletionPort;// IO完成端口句柄
    HANDLE          m_workerThread;    // 工作线程句柄
    bool            m_isRunning;       // 线程运行标志
    mutable QMutex  m_mutex;           // 线程安全锁
    ULONG_PTR       m_baseOffset;      // 当前DDR基址（由init参数决定）
    std::vector<PcieReadBlock> m_blocks; // 当前DDR的预分配块数组
    int             m_currentSubmitIndex; // 下一个要提交读取的索引
    QAtomicInteger<quint64> m_readCount;       // 当前DDR累计读取次数
    DWORD           m_blockStride;     // 块步长
    int             m_blocksCount;      // 当前DDR的块数量
};

#endif // PCIEIOCPREADER_H
