#include "PcieIocpReader.h"
#include <QDebug>

PcieIocpReader::PcieIocpReader(QObject *parent)
    : QObject(parent)
    , m_hPcieDevice(INVALID_HANDLE_VALUE)
    , m_ioCompletionPort(nullptr)
    , m_workerThread(nullptr)
    , m_isRunning(false)
    , m_baseOffset(0)
    , m_currentSubmitIndex(0)
    , m_readCount(0)
    , m_blockStride(0)
    , m_blocksCount(0)
{
}

PcieIocpReader::~PcieIocpReader()
{
    stop();
    if (m_hPcieDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPcieDevice);
    }
    if (m_ioCompletionPort != nullptr) {
        CloseHandle(m_ioCompletionPort);
    }
    // 释放预分配内存
    for (auto& block : m_blocks) {
        if (block.buffer) delete[] block.buffer;
        delete block.pOverlapped;
    }
}

bool PcieIocpReader::init(const QString& devicePath,
                          bool isSecondDdr,
                          int blocksPerDdr,
                          DWORD blockStride,
                          DWORD readSize)
{
    QMutexLocker locker(&m_mutex);
    m_blocksCount = blocksPerDdr;
    m_blockStride = blockStride;
    resetReadCount();

    // 根据参数设置DDR基址
    m_baseOffset = isSecondDdr ? 0x40000000 : 0x00000000;

    // 1. 打开PCIe设备
    m_hPcieDevice = CreateFile(
        devicePath.toStdWString().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
        );

    if (m_hPcieDevice == INVALID_HANDLE_VALUE) {
        qDebug() << "Open PCIe device failed, error:" << GetLastError();
        return false;
    }

    // 2. 创建IO完成端口
    m_ioCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!m_ioCompletionPort) {
        qDebug() << "Create IoCompletionPort failed, error:" << GetLastError();
        CloseHandle(m_hPcieDevice);
        m_hPcieDevice = INVALID_HANDLE_VALUE;
        return false;
    }
    CreateIoCompletionPort(m_hPcieDevice, m_ioCompletionPort, 0, 0);

    // 3. 预分配当前DDR的所有块
    m_blocks.resize(blocksPerDdr);
    m_currentSubmitIndex = 0;

    for (int i = 0; i < blocksPerDdr; i++) {
        PcieReadBlock& block = m_blocks[i];
        block.buffer = new BYTE[readSize];
        block.bufferSize = readSize;
        block.pOverlapped = new OVERLAPPED;
        ZeroMemory(block.pOverlapped, sizeof(OVERLAPPED));

        // 将块地址存在hEvent，O(1)找到对应块，不需要遍历
        //block.pOverlapped->hEvent = reinterpret_cast<HANDLE>(this);
        block.pOverlapped->Internal = reinterpret_cast<ULONG_PTR>(&block);

        // 计算当前块的偏移 = 当前DDR基址 + 块索引 * 步长
        ULONG_PTR currentOffset = m_baseOffset + (ULONG_PTR)blockStride * (i / 4);
        block.pOverlapped->Offset = (DWORD)currentOffset;
        block.pOverlapped->OffsetHigh = (DWORD)(currentOffset >> 32);

        block.bytesRead = 0;
        block.isCompleted = false;
        block.isInUse = false;
    }

    // 4. 启动IOCP工作线程
    m_isRunning = true;
    unsigned int threadId;
    m_workerThread = (HANDLE)_beginthreadex(nullptr, 0, workerThread, this, 0, &threadId);
    if (!m_workerThread) {
        qDebug() << "Create worker thread failed:" << GetLastError();
        m_isRunning = false;
        // 资源清理
        CloseHandle(m_ioCompletionPort);
        CloseHandle(m_hPcieDevice);
        m_ioCompletionPort = nullptr;
        m_hPcieDevice = INVALID_HANDLE_VALUE;
        return false;
    }

    QString ddrName = isSecondDdr ? "DDR2" : "DDR1";
    qDebug() << QString("%1 init done: %2 blocks, %3 bytes read per block, base offset 0x%4")
                    .arg(ddrName)
                    .arg(blocksPerDdr)
                    .arg(readSize)
                    .arg(QString::number(m_baseOffset, 16).toUpper());
    return true;
}

void PcieIocpReader::stop()
{
    QMutexLocker locker(&m_mutex);
    if (!m_isRunning) return;
    m_isRunning = false;
    PostQueuedCompletionStatus(m_ioCompletionPort, 0, 0, nullptr);
    if (m_workerThread) {
        WaitForSingleObject(m_workerThread, INFINITE);
        CloseHandle(m_workerThread);
        m_workerThread = nullptr;
    }
}

void PcieIocpReader::resetReadCount()
{
    m_readCount.storeRelaxed(0);
}

quint64 PcieIocpReader::getReadCount() const
{
    return m_readCount.loadRelaxed();
}

void PcieIocpReader::submitReadRequestByStep(quint8 step)
{
    if ((PcieStep)step == PcieStep::Stop) {
        // 0x00直接停止，不需要提交
        m_currentSubmitIndex = 0;
        return;
    }

    QMutexLocker locker(&m_mutex);
    if (!m_isRunning || m_hPcieDevice == INVALID_HANDLE_VALUE) return;

    int blockIdx = stepToBlockIndex((PcieStep)step);
    if (blockIdx < 0) {
        qDebug() << QString("Invalid step: 0x%1").arg((quint8)step, 0, 16);
        return;
    }

    PcieReadBlock& currentBlock = m_blocks[m_currentSubmitIndex];
    if (currentBlock.isInUse) {
        qDebug() << QString("Step 0x%1 overlapped, block still pending").arg((int)step, 0, 16);
        return;
    }

    // 标记状态，提交异步读取
    currentBlock.isInUse = true;
    currentBlock.isCompleted = false;

    // 计算当前块的偏移 = 当前DDR基址 + 块索引 * 步长
    ULONG_PTR currentOffset = m_baseOffset + (ULONG_PTR)m_blockStride * (blockIdx - 1);
    currentBlock.pOverlapped->Offset = (DWORD)currentOffset;
    currentBlock.pOverlapped->OffsetHigh = (DWORD)(currentOffset >> 32);

    BOOL ret = ReadFile(
        m_hPcieDevice,
        (LPVOID)currentBlock.buffer,
        currentBlock.bufferSize,
        nullptr,
        currentBlock.pOverlapped
        );

    DWORD err = GetLastError();
    if (!ret && err != ERROR_IO_PENDING) {
        qDebug() << QString("Read failed for step 0x%1, error: %2").arg((quint8)step, 0, 16).arg(err);
        currentBlock.isInUse = false;
        // 读失败不推进索引，重新提交当前块
        return;
    }

    // 提交索引自增，循环使用
    m_currentSubmitIndex = (m_currentSubmitIndex + 1) % m_blocksCount; // 理论上肯定不会超过m_blocksCount这个值，为避免溢出还是取模吧
}

// step → 块索引映射，固定对应，永远不会错
int PcieIocpReader::stepToBlockIndex(PcieStep step)
{
    // 用枚举本身顺序，不需要硬编码switch，新增step自动适配
    int index = static_cast<int>(step);
    if (index >= 0 && index <= static_cast<int>(PcieStep::StepMax)) {
        return index;
    }
    return -1;
}

bool PcieIocpReader::getCompletedBlock(int blockIndex, BYTE*& outBuffer, DWORD& outBytesRead)
{
    QMutexLocker locker(&m_mutex);
    if (blockIndex < 0 || blockIndex >= m_blocksCount) return false;

    PcieReadBlock& block = m_blocks[blockIndex];
    if (!block.isCompleted) return false;

    outBuffer = block.buffer;
    outBytesRead = block.bytesRead;
    block.isCompleted = false;
    block.isInUse = false;
    return true;
}

unsigned int __stdcall PcieIocpReader::workerThread(void* param)
{
    PcieIocpReader* reader = reinterpret_cast<PcieIocpReader*>(param);
    DWORD bytesRead = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = nullptr;

    while (reader->m_isRunning) {
        BOOL ret = GetQueuedCompletionStatus(
            reader->m_ioCompletionPort,
            &bytesRead,
            &completionKey,
            &overlapped,
            INFINITE
            );

        if (!reader->m_isRunning) break;
        if (!ret) {
            qDebug() << "IOCP status error:" << GetLastError();
            continue;
        }

        // ✅ 优化1: 直接从OVERLAPPED拿块指针，不需要遍历，O(1)匹配
        if (!overlapped) continue;
        PcieReadBlock* completedBlock = reinterpret_cast<PcieReadBlock*>(overlapped->Internal);
        if (!completedBlock) continue;

        int blockIndex = completedBlock - reader->m_blocks.data();

        // 更新状态和计数
        completedBlock->bytesRead = bytesRead;
        completedBlock->isCompleted = true;
        {
            QMutexLocker locker(&reader->m_mutex);
            reader->m_readCount++;
        }

        // 回调通知业务层
        reader->onPcieReadComplete(blockIndex, completedBlock->buffer, bytesRead);
    }
    return 0;
}

void PcieIocpReader::onPcieReadComplete(int blockIndex, BYTE *buffer, DWORD bytesRead)
{
    qDebug() << QString("PCIe Read Complete: Block[%1], Bytes: %2").arg(blockIndex).arg(bytesRead);
}
