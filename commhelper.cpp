#include "commhelper.h"
#include "AppConfig.h"

#include <bitset>
#include <QtEndian>
#include <QTimer>
#include <QDataStream>
#include <QNetworkSession>
#include <QNetworkConfigurationManager>

#include <QRegularExpression>
// 从带单位字符串中提取纯数值（支持负数、小数）
double extractNumericValue(const QString &valueWithUnit) {
    QRegularExpression regex(R"(-?\d+\.?\d*)");
    QRegularExpressionMatch match = regex.match(valueWithUnit);
    return match.hasMatch() ? match.captured(0).toDouble() : 0.0;
}

// 判断字符串是否为无=的数值项（双值对的第二个值特征）
bool isNumericValueItem(const QString &item) {
    return !item.contains('=') && QRegularExpression(R"(-?\d+\.?\d*)").match(item).hasMatch();
}

// 解析键值对（单值对自动填充value2=0，双值对保留实际值）
QMap<QString, QPair<double, double>> parseKeyValuePairsWithDefault(const QString &rawInput) {
    QMap<QString, QPair<double, double>> parsedData;
    QStringList keyValuePairs = rawInput.split(',', Qt::SkipEmptyParts);

    for (int i = 0; i < keyValuePairs.size(); ++i) {
        QString currentItem = keyValuePairs[i].trimmed();
        if (currentItem.isEmpty() || !currentItem.contains('=')) continue; // 跳过空项或非键值对

        // 提取键和第一个值
        QStringList keyValue = currentItem.split('=', Qt::SkipEmptyParts);
        if (keyValue.size() < 2) continue; // 跳过无效键值对（如"key="）
        QString key = keyValue[0].trimmed();
        double value1 = extractNumericValue(keyValue[1].trimmed());
        double value2 = 0.0; // 单值对默认填充0
        if (key.contains("BIN"))
        {
            bool ok = false;
            value1 = keyValue[1].trimmed().remove(' ').toUInt(&ok, 16);
        }
        else
        {
            // 检查是否为双值对（下一个元素是无=的数值）
            if (i + 1 < keyValuePairs.size()) {
                QString nextItem = keyValuePairs[i+1].trimmed();
                if (isNumericValueItem(nextItem)) {
                    value2 = extractNumericValue(nextItem);
                    i++; // 跳过已处理的第二个值
                }
            }
        }

        parsedData.insert(key, QPair<double, double>(value1, value2));
    }
    return parsedData;
}


UdpDataProcessor::UdpDataProcessor(QObject *parent)
    : QObject(parent)
{
    // for (int cardIndex = 1; cardIndex <= 18; ++cardIndex){
    //     mMapChannel[cardIndex] = false;
    // }

    for (int idx=0; idx<20; ++idx){
        mModuleOccurredCount[idx] = 0;
    }
}

void UdpDataProcessor::enqueueDatagram(const QByteArray &gram)
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingQueue.append(gram);
    // 唤醒正在等待的后台线程
    m_waitCondition.wakeOne();
}

void UdpDataProcessor::stop()
{
    QMutexLocker locker(&m_queueMutex);
    m_isRunning = false;
    m_waitCondition.wakeAll();
}

void UdpDataProcessor::processLoop()
{
    m_isRunning = true;
    QByteArray currentGram;
    while (m_isRunning) {
        // 临界区：安全取出队列里的报文
        {
            QMutexLocker locker(&m_queueMutex);
            if (m_pendingQueue.isEmpty()) {
                // 队列为空时线程进入休眠，等待新报文到来唤醒
                m_waitCondition.wait(&m_queueMutex);
            }
            // 被唤醒后确认队列非空且线程仍在运行，取出队首报文
            if (!m_pendingQueue.isEmpty() && m_isRunning) {
                currentGram.append(m_pendingQueue);
                m_pendingQueue.clear();
            }
        }

        // 完全释放锁之后再执行耗时操作，避免阻塞主线程入队
        if (!currentGram.isEmpty()) {
            // 1. 先把所有收到的新数据追加到全局累积缓冲区
            // 这里你可以把队列取出的新报文append到currentGram里
            QList<QByteArray> allGramList;
            QList<QByteArray> timeoutErrList; // 专门存异常超时报文

            int pos = 0;
            // 遍历缓冲区里所有的起始符@
            while ((pos = currentGram.indexOf('@', pos)) != -1)
            {
                int nextAt = currentGram.indexOf('@', pos + 1);
                int endSharp = currentGram.indexOf('#', pos);

                // 情况1：当前@后面能找到#，说明是完整正常报文
                if (endSharp != -1 && (nextAt == -1 || endSharp < nextAt))
                {
                    QByteArray fullGram = currentGram.mid(pos, endSharp - pos + 1);
                    allGramList.append(fullGram);
                    pos = endSharp + 1; // 跳过当前#，继续往后解析
                    continue;
                }

                // 情况2：前后@见没有#出现，说明这是一条异常超时报文
                if (endSharp > nextAt)
                {
                    QByteArray errGram = currentGram.mid(pos, nextAt - pos);
                    timeoutErrList.append(errGram);
                    pos = nextAt; // 跳到下一个@继续处理
                    continue;
                }

                // 情况3：最后一段数据，只有@没有后续#也没有下一个@，是不完整的半报文，直接退出留到下次处理
                break;
            }

            // 处理完所有已解析的内容，把剩余未完成的半报文保留在缓冲区
            if (pos > 0)
            {
                currentGram = currentGram.mid(pos);
            }
            else{
                currentGram.clear();
            }
            // -------- 分别处理两类报文 --------
            // 1. 处理异常超时报文，提取模块编号
            for(const auto& errItem : timeoutErrList)
            {
                // 格式是@XX*TIMEOUT，直接提取@后面两位编号
                quint8 moduleNo = errItem.mid(1,2).toInt();
                //qDebug() << "检测到超时异常模块：" << moduleNo;
                if (moduleNo>=1 && moduleNo<=20){
                    mModuleOccurredCount[moduleNo-1].fetch_add(1, std::memory_order_relaxed);
                    quint32 currentRef = mModuleOccurredCount[moduleNo-1].load(std::memory_order_acquire);
                    qDebug() << "模块异常：" << moduleNo << "次数：" << currentRef;
                    if (currentRef > 5)
                        emit moduleExceptionOccurred(moduleNo, true);
                }
            }

            // 2. 处理正常闭合的带#报文
            for(auto& normalItem : allGramList)
            {
                //qDebug() << "收到完整正常报文：" << normalItem;
                // 你之前的电压电流解析逻辑直接复用在这里即可
                quint8 moduleNo = normalItem.mid(1, 2).toInt();
                if (moduleNo < 1 || moduleNo > 20) continue;
                quint32 currentRef = mModuleOccurredCount[moduleNo-1].load(std::memory_order_acquire);
                if (currentRef > 5)
                    qDebug() << "模块异常已恢复：" << moduleNo;

                mModuleOccurredCount[moduleNo-1].store(0, std::memory_order_relaxed);
                emit moduleExceptionOccurred(moduleNo, false);

                QMap<QString, QPair<double, double>> result = parseKeyValuePairsWithDefault(normalItem.replace('\n', ','));
                emit temperatureAndVoltageChanged(moduleNo, std::move(result));

                if (moduleNo==19 && result.contains("IO_BIN")){ // 对应的moduleNo==19
                    std::bitset<32> bits(static_cast<uint32_t>(result["IO_BIN"].first));
                    for (int i=0; i<18; ++i){
                        if (!mMapChannel.contains(i+1) || mMapChannel[i+1] != bits.test(i))
                        {
                            mMapChannel[i+1] = bits.test(i);
                            emit backupChannelStatusChanged(i+1, !mMapChannel[i+1]);
                        }
                    }
                }
            }

            // while (currentGram.contains('@') && currentGram.contains('#')){
            //     quint32 start = currentGram.indexOf('@');
            //     quint32 end = currentGram.indexOf('#', start);
            //     quint32 start2 = currentGram.lastIndexOf('@', end);
            //     if (start2 != start){
            //         // 模块故障，判断是否出现超时
            //         // 先处理超时
            //         QByteArray errData = currentGram.mid(start, start2 - start);

            //         // 再处理正常值
            //         start = start2;
            //     }
            //     QByteArray rawData = currentGram.mid(start, end - start + 1);
            //     currentGram.remove(0, end + 1);
            //     quint8 moduleNo = rawData.mid(1, 2).toInt();
            //     /*
            //         @01*999*GETR*01*
            //         PSD1_48V=46.60V,2.41mA
            //         PSD1_29V=29.29V,0.03mA
            //         PSD1-AMP=46.56V,12.04mA
            //         PSD2_48V=46.61V,2.68mA
            //         PSD2_29V=29.39V,0.03mA
            //         PSD2-AMP=46.55V,11.74mA
            //         LSD_48V=46.59V,2.36mA
            //         LSD_29V=29.17V,0.03mA
            //         LSD-AMP=46.56V,11.95mA
            //         LBD_48V=46.60V,3.46mA
            //         LBD_29V=29.41V,0.03mA
            //         LBD-AMP=46.56V,11.75mA
            //         PSD1=16.81
            //         LBD=16.63
            //         PSD2=16.63
            //         LSD=16.63
            //         #

            //         @01*999*GETR*01*
            //         -----Voltage &Current Data-----
            //         IHA238_01 = 0.00V, 0.00mA
            //         IHA238_02 = 0.00V, 0.00mA
            //         IHA238_03 = 0.00V, 0.00mA
            //         IHA238_04 = 0.00V, 0.00mA
            //         IHA238_05 = 0.00V. 0.00mA
            //         IHA238_06 = 0.00V, 0.00mA
            //         IHA238_07 = 0.00V, 0.00mA
            //         IHA238_08 = 0.00V, 0.00mA
            //         IHA238_09 = 0.00V, 0.00mA
            //         IHA238_10 = 0.00V, 0.00mA
            //         IHA238_11 = 0.00V, 0.00mA
            //         IHA238_12 = 0.00V, 0.00mA

            //         -----Temperature ata-----
            //         DS18B20_01 = 0.00°C
            //         DS18B20_02 = 0.00°C
            //         DS18B20_03 = 0.00°C
            //         DS18B20_04 = 0.00°C

            //         -----Device Info-----
            //         UTD = 0x3938353834315100002F0017
            //         FeSoftV=Unknown
            //         FeHardy=Unknown
            //         FeAddr =

            //         @01*TIMEOUT@02*TIMEOUT@03*TIMEOUT@04*TIMEOUT@05*TIMEOUT@06*TIMEOUT@07*TIMEOUT@08*TIMEOUT@09*TIMEOUT@10*TIMEOUT@11*TIMEOUT@12*TIMEOUT@13*TIMEOUT@14*TIMEOUT@15*TIMEOUT@16*TIMEOUT@17*TIMEOUT@18*TIMEOUT@19*999*GETR*01*
            //         INA226_01 (0x40) = 12.71V, 399.76mA
            //         INA226_02 (0x45) = 5.09V, 213.17mA
            //         IO_BIN = 5A 00 00 01
            //         #
            //         @22*999*GETR*01*
            //         01 = 0.11V, 0.00A
            //         02 = 0.05V, 0.00A
            //         #
            //         @22*999*GETR*01*
            //         01 = 48.08V, 0.00A
            //         02 = 48.05V, 1.05A
            //         #
            //     */

            //     rawData.replace('\n', ',');
            //     QMap<QString, QPair<double, double>> result = parseKeyValuePairsWithDefault(rawData);
            //     emit temperatureAndVoltageChanged(moduleNo, std::move(result));

            //     if (result.contains("IO_BIN")){ // 对应的moduleNo==19
            //         std::bitset<32> bits(static_cast<uint32_t>(result["IO_BIN"].first));
            //         for (int i=0; i<18; ++i){
            //             if (mMapChannel[i+1] != bits.test(i))
            //             {
            //                 mMapChannel[i+1] = bits.test(i);
            //                 emit backupChannelStatusChanged(i+1, mMapChannel[i+1]);
            //             }
            //         }
            //     }
            // }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CommHelper::CommHelper(QObject *parent)
    : QObject{parent}
{
    /*初始化网络*/
    initSocket();
    initDataProcessor();

    // mRequestCmdThread = new QLiteThread(this);
    // mRequestCmdThread->setObjectName("mRequestCmdThread");
    // mRequestCmdThread->setWorkThreadProc([=](){
    //     while (!mRequestCmdThread->isInterruptionRequested())
    //     {
    //         if (mTcpClient->isOpen()){
    //             //发送查询指令
    //             //@01*999*GET*01*#
    //             for (int i=1; i<=18; ++i){
    //                 QString askCommand = QString("@%1*999*GET*01*#").arg(i, 2, 10, QLatin1Char('0'));
    //                 mTcpClient->write(askCommand.toLatin1());
    //                 mTcpClient->waitForBytesWritten();

    //                 QThread::msleep(500);
    //             }
    //         }

    //         QThread::msleep(1000);
    //     }
    // });
    // mRequestCmdThread->start();
    // connect(this, &CommHelper::destroyed, [=]() {
    //     mRequestCmdThread->exit(0);
    //     mRequestCmdThread->wait(500);
    // });

    // 2. 初始化后台处理线程
    m_workThread = new QThread(this);
    mUdpPerformanceDataProcessor = new UdpDataProcessor;
    mUdpPerformanceDataProcessor->moveToThread(m_workThread);

    // 绑定线程启动信号到处理循环槽
    connect(m_workThread, &QThread::started, mUdpPerformanceDataProcessor, &UdpDataProcessor::processLoop);
    // 处理完成结果回调主线程更新UI

    connect(mUdpPerformanceDataProcessor, &UdpDataProcessor::moduleExceptionOccurred, this, [=](quint8 v1, bool v2){
        emit moduleExceptionOccurred(v1, v2);
    }, Qt::DirectConnection);// &CommHelper::moduleExceptionOccurred);
    connect(mUdpPerformanceDataProcessor, &UdpDataProcessor::backupChannelStatusChanged, this, [=](quint8 v1, bool v2){
        if (mMapChannel[v1] != v2){
            mMapChannel[v1] = v2;
            emit backupChannelStatusChanged(v1, v2);
        }
    }, Qt::DirectConnection);// &CommHelper::backupChannelStatusChanged);
    connect(mUdpPerformanceDataProcessor, &UdpDataProcessor::temperatureAndVoltageChanged, this, [=](quint8 v1, const QMap<QString, QPair<double, double>>& v2){
        emit temperatureAndVoltageChanged(v1, v2);
    }, Qt::DirectConnection);//&CommHelper::temperatureAndVoltageChanged);
    // 窗口关闭时安全退出线程
    connect(this, &CommHelper::destroyed, mUdpPerformanceDataProcessor, &UdpDataProcessor::stop);
    connect(m_workThread, &QThread::finished, m_workThread, &QThread::deleteLater);
    connect(m_workThread, &QThread::finished, mUdpPerformanceDataProcessor, &UdpDataProcessor::deleteLater);

    m_workThread->start();

    for (int cardIndex = 1; cardIndex <= 18; ++cardIndex){
        mMapPower[cardIndex] = true;
        mMapVoltage[cardIndex] = true;
        mMapChannel[cardIndex] = false;
    }
}

CommHelper::~CommHelper()
{
    // mRequestCmdThread->requestInterruption();
    // mRequestCmdThread->quit();
    // mRequestCmdThread->wait();
    // mRequestCmdThread->deleteLater();

    // 断开性能监测
    //this->disconnectServer();
    // 通知后台处理器停止运行，唤醒等待中的线程
    mUdpPerformanceDataProcessor->stop();
    // 告诉线程终止事件循环，退出线程主函数
    m_workThread->quit();
    // 阻塞等待线程真正执行完毕，最多等3秒避免死锁卡住
    if(!m_workThread->wait(3000)) {
        // 3秒还没退出，强制终止极端阻塞场景
        m_workThread->terminate();
        m_workThread->wait();
    }
}

#include <winsock2.h>   // WSAIoctl函数定义
#include <mstcpip.h>    // 包含 TCP_KEEPALIVE 结构体定义
#include <ws2ipdef.h>
struct TCP_KEEPLIVE {
    u_long onoff;
    u_long keepalivetime;
    u_long keepaliveinterval;
};
// 补充SDK缺失的宏，引用自C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared\ws2ipdef.h
#if !defined(TCP_KEEPIDLE) && defined(_WIN32)
#define TCP_KEEPALIVE       	 3
#define TCP_KEEPCNT              16
#define TCP_KEEPIDLE             TCP_KEEPALIVE
#define TCP_KEEPINTVL            17
#endif
void CommHelper::initSocket()
{
    // 初始化UDP
#ifdef SINGLE_BOARD_MODE
    mTcpServer = new hv::TcpServer();
    mTcpServer->setThreadNum(32);
    mTcpServer->onConnection = [&](const hv::SocketChannelPtr& channel)
    {
        if (channel->isConnected()){// isOpened()){// ->status == hv::SocketChannel::CONNECTED){
            channel->setKeepaliveTimeout(60000);// 60s没有数据交互就断开(这种方式用的是通过判断最后一次网络数据的读/写时间与系统时钟的时间差来判断)

            // 禁用Nagle算法，降低数据包的传输延迟
            tcp_nodelay(channel->fd());
            // 设置接收缓冲区大小
            int rcvbuf = 4 * 1024 * 1024;  // 4MB
            setsockopt(channel->fd(), SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, sizeof(rcvbuf));

            // 1. 开启基础保活开关,缺省值:0(关闭)
            //tcp_keepalive(channel->fd());
            BOOL keepAliveEnable = TRUE;
            setsockopt(channel->fd(), SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepAliveEnable, sizeof(keepAliveEnable));

            // 2. 设置最大探测次数为3次，仅对当前socket生效,缺省值:9(次)
            int keepcnt = 3;
            setsockopt(channel->fd(), IPPROTO_TCP, TCP_KEEPCNT, (char*)&keepcnt, sizeof(keepcnt));

            // 修改空闲等待时间和探测间隔
            // int idle = 5;  // 空闲5秒后开始探测（单位：秒），缺省值:7200(s)
            // int interval = 3; // 探测间隔3秒（单位：秒）,缺省值:75(s)
            // setsockopt(channel->fd(), IPPROTO_TCP, TCP_KEEPIDLE, (char*)&idle, sizeof(idle));
            // setsockopt(channel->fd(), IPPROTO_TCP, TCP_KEEPINTVL, (char*)&interval, sizeof(interval));

            // 3. 配置自定义保活参数（Windows参数单位为毫秒）
            TCP_KEEPLIVE keepCfg = {0};
            keepCfg.onoff = true;//开启keepalive属性
            keepCfg.keepalivetime = 70*1000;  //如果该连接在30秒内没有任何数据往来，则执行TCP层的探测，缺省值:7200(s)
            keepCfg.keepaliveinterval = 10*1000;//探测发包间隔是10秒,缺省值:75(s)

            DWORD retBytes = 0;
            WSAIoctl(
                channel->fd(),
                SIO_KEEPALIVE_VALS,
                &keepCfg,
                sizeof(keepCfg),
                nullptr,
                0,
                &retBytes,
                nullptr,
                nullptr
                );

            QString peerAddress = QString::fromStdString(channel->peerip());
            quint16 peerPort = channel->peerport();
            qDebug().nospace() << "数采板上线，地址：" << peerAddress << ", 端口：" << peerPort;

            //给新上线客户端分配数据处理器
            mDetectorDataProcessor->reallocSocket(channel);
            QMetaObject::invokeMethod(this, "detectorOnline", Qt::QueuedConnection);
        }
        else if (channel->isClosed()){//status == hv::SocketChannel::CLOSED){
            QString peerAddress = QString::fromStdString(channel->peerip());
            quint16 peerPort = channel->peerport();
            qDebug().nospace() << "数采板下线，地址：" << peerAddress << ", 端口：" << peerPort;

            QMetaObject::invokeMethod(this, "detectorOffline", Qt::QueuedConnection);
        }
        else {
            return;
        }
    };
#else
    if (mUdpPerformanceMonitorReceiver == nullptr){
        this->mUdpPerformanceMonitorReceiver = new QUdpSocket();
        connect(mUdpPerformanceMonitorReceiver, &QUdpSocket::readyRead, this, &CommHelper::readyRead);
        mTimerout = new QTimer(this);
        mTimerout->setSingleShot(true);
        connect(mTimerout, &QTimer::timeout, this, [=](){
            if (!mReconnectTimer->isActive())
                qCritical() << "设备连接失败";
        });

        mReconnectTimer = new QTimer(this);
        connect(mReconnectTimer, &QTimer::timeout, this, [=](){
            this->connectServer();
        });
    }
#endif //

    //炮号接收器
    if (mUdpShotReceiver == nullptr){
        this->mUdpShotReceiver = new QUdpSocket();
        this->mUdpShotReceiver->setSocketOption(QAbstractSocket::MulticastLoopbackOption, true);
        connect (mUdpShotReceiver, &QUdpSocket::readyRead, this, [=](){
            while (mUdpShotReceiver->hasPendingDatagrams()) {
                QByteArray datagram;
                datagram.resize(int(mUdpShotReceiver->pendingDatagramSize()));
                QHostAddress sender;
                quint16 senderPort;
                mUdpShotReceiver->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
                //+PLS_12345

                qInfo().noquote() << "接收广播消息：" << datagram;
                if (datagram.startsWith("+PLS_")){
                    //解析炮号
                    QRegularExpression regex("\\d+");
                    QRegularExpressionMatchIterator iterator = regex.globalMatch(datagram);
                    if (iterator.hasNext()) {
                        QRegularExpressionMatch match = iterator.next();
                        QString shotNum = match.captured(0);
                        emit shotnumValueChanged(shotNum);
                    }
                }
                else if (datagram.startsWith("TIME_")){
                    QRegularExpression regex("TIME_(\\d{4})-(\\d{2})-(\\d{2}) (\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{3})");
                    if (!datagram.contains('.'))
                        datagram.append(".000");
                        //regex.setPattern("TIME_(\\d{4})-(\\d{2})-(\\d{2}) (\\d{2}):(\\d{2}):(\\d{2})");
                    QRegularExpressionMatchIterator iterator = regex.globalMatch(datagram);
                    if (iterator.hasNext()) {
                        QRegularExpressionMatch match = iterator.next();
                        // 提取各个时间组件
                        QString year = match.captured(1);    // 年
                        QString month = match.captured(2);   // 月
                        QString day = match.captured(3);     // 日
                        QString hour = match.captured(4);    // 时
                        QString minute = match.captured(5);  // 分
                        QString second = match.captured(6);  // 秒
                        QString millisecond = match.captured(7); // 毫秒
                        QDateTime tm(QDate(year.toUInt(),month.toUInt(),day.toUInt()),
                                     QTime(hour.toUInt(),minute.toUInt(),second.toUInt(),millisecond.toUInt()));
                        emit systemTimeValueChanged(tm);
                    }
                }
                else if (datagram.startsWith("EMERGENCE_STOP")){
                    emit energenceStopSignalTriggered();
                }
            }
        });
        if (this->mUdpShotReceiver->bind(QHostAddress::Any, 12100, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)){
            this->mUdpShotReceiver->joinMulticastGroup(QHostAddress::LocalHost);
        }
    }
}

void CommHelper::initDataProcessor()
{
    // 这里必须是从0开始，0对应的是时间同步模块，1-32对应探测器索引，这样可以通过索引直接访问对应的处理器
    DataProcessor* processor = new DataProcessor(1, nullptr, this);
    mDetectorDataProcessor = processor;

    // 更新温度数据
    connect(processor, &DataProcessor::reportTemperatureData, this, [=](double temperature, const QDateTime& trTime){
        emit reportDetectorTemperature(processor->getIndex(), temperature, trTime);
    }, Qt::DirectConnection);

    // 能谱数据（解析后的完整能谱数据）
    connect(processor, &DataProcessor::reportFullSpectrum, this, [=](quint8 detID, const FullSpectrum& fullSpectrum){
        emit reportFullSpectrum(detID, fullSpectrum); // 通知界面更新能谱数据

        // H5Spectrum h5Spectrum = FullSpectoH5Spec(fullSpectrum);
        // QMutexLocker lock(&mSpectrumH5MutexByDetector[detID]);
        // mPendingH5ByDetector[detID].append(h5Spectrum);
    }, Qt::DirectConnection);

    // 波形数据（解析后的数据）
    connect(processor, &DataProcessor::reportWaveCurveInfo, this, [=](quint8 detID, const QVector<quint16>& wave){
        emit reportWaveCurve(detID, wave); //通知界面更新波形数据
    }, Qt::QueuedConnection);

    // 触发信号时间戳(解析后的数据)
    connect(processor, &DataProcessor::reportTriggerTimeInfo, this, [=](quint8 detID, const QDateTime triggerTime){
        emit reportTimestampe(triggerTime); //通知界面更新触发时间戳
    }, Qt::DirectConnection);

    // 原始OTA升级数据
    // connect(processor, &DataProcessor::reportOTAUpgradeData, this, [=](quint8 detID, const QByteArray& data){
    //     emit reportOTAUpgradeData(detID, data);
    // }, Qt::DirectConnection);

    // 原始网络数据
    connect(processor, &DataProcessor::reportRawNetData, this,
            &CommHelper::onRawNetData_Thread, Qt::DirectConnection);
}

void CommHelper::error(QAbstractSocket::SocketError error)
{
    if (error != QAbstractSocket::SocketTimeoutError){

    }
}

void CommHelper::readyRead()
{
    QByteArray datagram;
    do {
        QByteArray data;
        data.resize(int(mUdpPerformanceMonitorReceiver->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        mUdpPerformanceMonitorReceiver->readDatagram(data.data(), data.size(), &sender, &senderPort);

        QString srcIp = sender.toString();
        if (srcIp == AppConfig::instance().ipAddress() && senderPort == AppConfig::instance().remotePort())
            datagram.append(data);

    } while (mUdpPerformanceMonitorReceiver->hasPendingDatagrams());

    if (!datagram.isEmpty())
        onReadyRead(datagram);
}

void CommHelper::onReadyRead(QByteArray& tempData)
{
    // qDebug() << "================================================================";
    // qDebug() << tempData;
    // qDebug() << "================================================================\n";

    if (tempData == "start"){
        if (mTimerout->isActive())
            mTimerout->stop();

        emit connected();
    }
    else if (tempData == "stop"){
        emit disconnected();

        mUdpPerformanceMonitorReceiver->close();
    }
    else if ((quint8)tempData.at(0) == 0xA5 && tempData.size()==4){// 对返回的数据头部和长度做个限制，避免接收到无效数据
        // 选通器切换指令返回
        //A5 03 FF FF
        // bool ok = false;
        // quint32 v = tempData.toHex().toUInt(&ok, 16);
        // if (ok){
        //     //v = qbswap(v);
        //     std::bitset<32> bits(v);
        //     for (int i=0; i<18; ++i){
        //         if (mMapChannel[i+1] != bits.test(i))
        //         {
        //             mMapChannel[i+1] = bits.test(i);
        //             emit backupChannelStatusChanged(i+1, mMapChannel[i+1]);
        //         }
        //     }
        // }

        return;
    }

    // 记录所有的性能监测原始数据
    const qint64 MAX_LOG_FILE_SIZE = 100 * 1024 * 1024; // 自定义阈值，这里设为100MB，你可以按需调整大小
    const QDateTime now = QDateTime::currentDateTime();
    const QString dateTag = now.toString(QStringLiteral("yyyy-MM-dd"));
    const QString baseFilePath = QStringLiteral("./logs/PerformanceMonitor_%1.log").arg(dateTag);

    QFile currentFile(baseFilePath);
    // 当当前基准文件超过阈值时，执行滚动归档
    if (currentFile.exists() && currentFile.size() >= MAX_LOG_FILE_SIZE) {
        // 读取当前文件里第一行记录的日志启动时间，作为归档文件的起始时间
        QDateTime fileStartTime = now;
        if (currentFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream readStream(&currentFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            readStream.setCodec("UTF-8");
#endif
            QString firstLine = readStream.readLine();
            // 匹配你原有日志开头的时间格式 yyyy-MM-dd HH:mm:ss.zzz
            if (firstLine.contains(">>")) {
                QString timeStr = firstLine.leftRef(23).toString();
                fileStartTime = QDateTime::fromString(timeStr, QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
                if (!fileStartTime.isValid()) {
                    fileStartTime = now;
                }
            }
            currentFile.close();
        }

        // 生成带起止时间的归档文件名
        QString archiveStartTimeStr = fileStartTime.toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QString archiveEndTimeStr = now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QString archiveFilePath = QStringLiteral("./logs/PerformanceMonitor_%1_%2_%3.log")
                                      .arg(dateTag, archiveStartTimeStr, archiveEndTimeStr);

        // 重命名旧文件，生成归档文件
        QFile::remove(archiveFilePath); // 避免重名冲突直接覆盖
        currentFile.rename(archiveFilePath);
    }

    // 打开新的基准日志文件，写入当前日志内容
    if (currentFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&currentFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        if (tempData.startsWith("@01"))
            out << now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz >> \n"));
        out << tempData << '\n';
        currentFile.close();
    }

    // 直接把原始报文送入后台处理队列，主线程无任何阻塞
    mUdpPerformanceDataProcessor->enqueueDatagram(tempData);
    return;

    mRawData.append(tempData);
    while (mRawData.contains('@') && mRawData.contains('#')){
        quint32 start = mRawData.indexOf('@');
        quint32 end = mRawData.indexOf('#', start);
        quint32 start2 = mRawData.indexOf('@', start + 1);
        if (start2 < end)
            start = start2;
        QByteArray rawData = mRawData.mid(start, end - start + 1);
        mRawData.remove(0, end + 1);
        quint8 moduleNo = rawData.mid(1, 2).toInt();
        /*
            @01*999*GETR*01*
            PSD1_48V=46.60V,2.41mA
            PSD1_29V=29.29V,0.03mA
            PSD1-AMP=46.56V,12.04mA
            PSD2_48V=46.61V,2.68mA
            PSD2_29V=29.39V,0.03mA
            PSD2-AMP=46.55V,11.74mA
            LSD_48V=46.59V,2.36mA
            LSD_29V=29.17V,0.03mA
            LSD-AMP=46.56V,11.95mA
            LBD_48V=46.60V,3.46mA
            LBD_29V=29.41V,0.03mA
            LBD-AMP=46.56V,11.75mA
            PSD1=16.81
            LBD=16.63
            PSD2=16.63
            LSD=16.63
            #

            @01*999*GETR*01*
            -----Voltage &Current Data-----
            IHA238_01 = 0.00V, 0.00mA
            IHA238_02 = 0.00V, 0.00mA
            IHA238_03 = 0.00V, 0.00mA
            IHA238_04 = 0.00V, 0.00mA
            IHA238_05 = 0.00V. 0.00mA
            IHA238_06 = 0.00V, 0.00mA
            IHA238_07 = 0.00V, 0.00mA
            IHA238_08 = 0.00V, 0.00mA
            IHA238_09 = 0.00V, 0.00mA
            IHA238_10 = 0.00V, 0.00mA
            IHA238_11 = 0.00V, 0.00mA
            IHA238_12 = 0.00V, 0.00mA

            -----Temperature ata-----
            DS18B20_01 = 0.00°C
            DS18B20_02 = 0.00°C
            DS18B20_03 = 0.00°C
            DS18B20_04 = 0.00°C

            -----Device Info-----
            UTD = 0x3938353834315100002F0017
            FeSoftV=Unknown
            FeHardy=Unknown
            FeAddr =

            @01*TIMEOUT@02*TIMEOUT@03*TIMEOUT@04*TIMEOUT@05*TIMEOUT@06*TIMEOUT@07*TIMEOUT@08*TIMEOUT@09*TIMEOUT@10*TIMEOUT@11*TIMEOUT@12*TIMEOUT@13*TIMEOUT@14*TIMEOUT@15*TIMEOUT@16*TIMEOUT@17*TIMEOUT@18*TIMEOUT@19*999*GETR*01*
            INA226_01 (0x40) = 12.71V, 399.76mA
            INA226_02 (0x45) = 5.09V, 213.17mA
            IO_BIN = 5A 00 00 01
            #
            @22*999*GETR*01*
            01 = 0.11V, 0.00A
            02 = 0.05V, 0.00A
            #
            @22*999*GETR*01*
            01 = 48.08V, 0.00A
            02 = 48.05V, 1.05A
            #
        */
        rawData.replace('\n', ',');
        QMap<QString, QPair<double, double>> result = parseKeyValuePairsWithDefault(rawData);
        emit temperatureAndVoltageChanged(moduleNo, std::move(result));

        if (result.contains("IO_BIN")){ // 对应的moduleNo==19
            std::bitset<32> bits(static_cast<uint32_t>(result["IO_BIN"].first));
            for (int i=0; i<18; ++i){
                if (!mMapChannel.contains(i+1) || mMapChannel[i+1] != bits.test(i))
                {
                    mMapChannel[i+1] = bits.test(i);
                    emit backupChannelStatusChanged(i+1, !mMapChannel[i+1]);
                }
            }
        }
    }
}

/*
 打开网络
*/
bool CommHelper::connectServer()
{
    QString ip = AppConfig::instance().ipAddress();
    quint32 port = AppConfig::instance().remotePort();
    quint32 portLocal = AppConfig::instance().localPort();

#ifdef SINGLE_BOARD_MODE
    initSocket();
    int ret = this->mTcpServer->createsocket(portLocal/*8151*/, ip.toStdString().c_str());
    if (ret >= 0 ){
        this->mTcpServer->start();
        return true;
    }
    else{
        disconnectServer();
        return false;
    }
#else
    this->mUdpPerformanceMonitorReceiver->close();
    if (this->mUdpPerformanceMonitorReceiver->bind(QHostAddress::AnyIPv4, portLocal, QUdpSocket::ShareAddress)){
        mTimerout->start(1000);
        if (this->openAllPower()){
            QByteArray datagram("start");
            int ret = mUdpPerformanceMonitorReceiver->writeDatagram(datagram, QHostAddress(ip), port);
            return ret > 0;
        }
    }

    return false;
#endif // SINGLE_BOARD_MODE
}

/*
 断开网络
*/
void CommHelper::disconnectServer()
{
#ifdef SINGLE_BOARD_MODE
    if (this->mTcpServer)
    {
        this->mTcpServer->stop();
        delete mTcpServer;
        mTcpServer = nullptr;
    }
#else
    QString ip = AppConfig::instance().ipAddress();
    quint32 port = AppConfig::instance().remotePort();

    QByteArray datagram("stop");
    if (mUdpPerformanceMonitorReceiver->writeDatagram(datagram, QHostAddress(ip), port) > 0){
        emit disconnected();

        mUdpPerformanceMonitorReceiver->close();
    }
#endif // SINGLE_BOARD_MODE
}

bool CommHelper::switchPower(quint8 channel, bool on)
{
    mMapPower[channel] = on;

    emit powerStatusChanged(channel, on);
    return true;
}

bool CommHelper::switchVoltage(quint8 channel, bool on)
{
    mMapVoltage[channel] = on;

    emit voltageStatusChanged(channel, on);
    return true;
}

bool CommHelper::openAllPower()
{
    //qInfo().noquote() << "打开电源";

    QString ip = AppConfig::instance().ipAddress();
    quint32 port = AppConfig::instance().remotePort();
    QByteArray datagram("openA");
    return mUdpPerformanceMonitorReceiver->writeDatagram(datagram, QHostAddress(ip), port) > 0;
}

bool CommHelper::closeAllPower()
{
    //qInfo().noquote() << "关闭电源";

    QString ip = AppConfig::instance().ipAddress();
    quint32 port = AppConfig::instance().remotePort();
    QByteArray datagram("closeA");
    return mUdpPerformanceMonitorReceiver->writeDatagram(datagram, QHostAddress(ip), port) > 0;
}

bool CommHelper::switchBackupPower(quint8 channel, bool on)
{
    mMapBackupPower[channel] = on;

    emit backupPowerStatusChanged(channel, on);
    return true;
}

bool CommHelper::switchBackupVoltage(quint8 channel, bool on)
{
    mMapBackupVoltage[channel] = on;

    emit backupVoltageStatusChanged(channel, on);
    return true;
}

bool CommHelper::switchBackupChannel(quint8 channel, bool on)
{
    std::bitset<32> bits;
    //0000 0000 0000 0000 0000 0000 0000 0000
    //1010 0101 0000 0000 0000 0000 0000 0000
    bits.set(24);
    bits.set(26);
    bits.set(29);
    bits.set(31);
    for (int i=0; i<18; ++i){
        if (i+1 == channel)
            bits[i] = !on;
        else
            bits[i] = !mMapChannel[i+1];
    }

    quint32 v = bits.to_ulong();
    v = qbswap(v);//转为网络大端字节顺序
    // QByteArray datagram = QByteArray::fromHex("A5 03 ff ff");
    // quint32 data = 0xffff03A5;

    QString ip = AppConfig::instance().ipAddress();
    quint32 port = AppConfig::instance().remotePort();
    mUdpPerformanceMonitorReceiver->writeDatagram((const char*)&v, sizeof(quint32), QHostAddress(ip), port);
    //mUdpPerformanceMonitorReceiver->writeDatagram(datagram, QHostAddress("192.168.1.212"), 1000);
    //mTcpClient->write((const char*)&v, sizeof(quint32));

    return true;
}

bool CommHelper::switchAllBackupChannel(bool on)
{
    std::bitset<32> bits;
    //0000 0000 0000 0000 0000 0000 0000 0000
    //1010 0101 0000 0000 0000 0000 0000 0000
    bits.set(24);
    bits.set(26);
    bits.set(29);
    bits.set(31);

    for (int i=0; i<18; ++i){
        bits[i] = !on;
    }

    quint32 v = bits.to_ulong();
    v = qbswap(v);//转为网络大端字节顺序

    QString ip = AppConfig::instance().ipAddress();
    quint32 port = AppConfig::instance().remotePort();
    mUdpPerformanceMonitorReceiver->writeDatagram((const char*)&v, sizeof(quint32), QHostAddress(ip), port);
    //mTcpClient->write((const char*)&v, sizeof(quint32));

    return true;
}

void CommHelper::startReconnect()
{
    mReconnectTimer->start(3000);
}

void CommHelper::stopReconnect()
{
    mReconnectTimer->stop();
}

//网口原始数据（解析线程）
void CommHelper::onRawNetData_Thread(quint8 /*index*/, const QByteArray& data)
{
    QFile* file = mDetectFileProcessor;
    if (!file || !file->isOpen())
        return;

    file->write(data.constData(), data.size());
}

/*
 开始测量
*/
#include <QFileInfo>
void CommHelper::startMeasure(QString fileSavePath, CommandAdapter::MeasureMode measureMode,
                  CommandAdapter::WorkMode workMode)
{
    constexpr int kReserveH5PerDetector = 60; // 每路探测器预留60个能谱数据
    constexpr int kReserveRawBytesPerDetector = 1024 * 1024; // 每路探测器预留1MB网口原始数据，50ms能谱刷新频率下0.625MB/s

    // mMeasureStartTime = tmStart.toString("yyyy-MM-dd_HHmmss");// QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    // mCurrentWorkMode = workMode;
    // const bool wasMeasuring = mMeasuring;
    // mMeasuring = true;


   if (mDetectFileProcessor->isOpen()){
        mDetectFileProcessor->close();
        mDetectFileProcessor->deleteLater();
        mDetectFileProcessor = nullptr;
    }

    // 清理flush信息
    // {
    //     QMutexLocker flushLocker(&mMutexFileFlush);
    //     mFileFlushInfo.remove(index);
    // }

    // // 测量中单通道重测（掉线重上线）：先冲刷共用文件缓冲，再为该通道新建独立 H5
    // if (workMode == CommandAdapter::WorkMode::wmSpectrum) {
    //     QVector<H5Spectrum> pending;
    //     {
    //         QMutexLocker lock(&mSpectrumH5MutexByDetector[index]);
    //         mPendingH5ByDetector[index].swap(pending);
    //         mPendingH5ByDetector[index].reserve(kReserveH5PerDetector);
    //     }
    //     if (!pending.isEmpty())
    //         HDF5Settings::instance()->writeH5SpectrumBatch(1, pending);

    //     const QString channelStartTime = mMeasureStartTime;
    //     QString h5Path = QString("%1/能谱.H5").arg(fileSavePath);
    //     const QString createdPath = HDF5Settings::instance()->createChannelH5Spectrum(
    //         1, h5Path, channelStartTime, mShotTime.isEmpty() ? shotTime : mShotTime);
    //     if (!createdPath.isEmpty() && !mDataFileList.contains(createdPath))
    //         mDataFileList.push_back(createdPath);
    // }

    if (!mDetectorDataProcessor->isFreeSocket()){
        //创建有效网络数据文件存储

        QString filePath = QString("%1/能谱.dat").arg(fileSavePath);
        if (CommandAdapter::WorkMode::wmWaveform == workMode)
            filePath = QString("%1/波形.dat").arg(fileSavePath);

        mDetectFileProcessor = new QFile(filePath);
        if (mDetectFileProcessor->open(QIODevice::WriteOnly)){
            // 初始化flush信息
            // {
            //     QMutexLocker flushLocker(&mMutexFileFlush);
            //     mFileFlushInfo[index] = FileFlushInfo();
            // }

            QFileInfo info(filePath);
            QString absolutePath = info.absoluteFilePath();
            qDebug().nospace() << "小数采创建存储文件：" << absolutePath;
        }

        mDetectorDataProcessor->startMeasure(workMode, measureMode);
    }
}

/*
 停止测量
*/
void CommHelper::stopMeasure()
{
    // // 停止前先将 H5 缓存落盘，避免累计在内存中
    // flushSpectrumH5BuffersOnWorkerThread(true);
    // // 停止前先将网口原始数据缓存落盘，避免尾包丢失
    // flushNetRawBuffersOnWorkerThread(true);

    //默认情况所有通道直接一次性停止测量
    // {
    //     QMutexLocker lock(&mSpectrumRawMutexByDetector[index]);
    //     if (index < mPendingNetRawByDetector.size())
    //         mPendingNetRawByDetector[index].clear();
    // }
    if (!mDetectorDataProcessor->isFreeSocket()){
        mDetectorDataProcessor->stopMeasure();
    }

    //延迟关闭文件，确保尾包数据能够正常存储
    if (mDetectFileProcessor){
        QFile* file = mDetectFileProcessor;
        if (file && file->isOpen()){
            //先刷新缓冲区，确保已写入的数据被保存
            file->flush();
        }

        //关闭文件
        {
            QFile* fileToClose = mDetectFileProcessor;
            if (fileToClose && fileToClose->isOpen()){
                fileToClose->flush(); //再次刷新，确保尾包数据写入
                fileToClose->close();
            }
            fileToClose->deleteLater();
            mDetectFileProcessor = nullptr;
        }

        // // 清理flush信息
        // {
        //     QMutexLocker flushLocker(&mMutexFileFlush);
        //     mFileFlushInfo.remove(detIndex);
        // }

        //关闭能谱文件
        // HDF5Settings::instance()->closeChannelH5Spectrum(index);
        // HDF5Settings::instance()->closeH5Spectrum();
    }
}
