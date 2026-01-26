#include "commhelper.h"
#include "globalsettings.h"

#include <QTimer>
#include <QDataStream>
#include <QNetworkSession>
#include <QNetworkConfigurationManager>

CommHelper::CommHelper(QObject *parent)
    : QObject{parent}
{
    /*初始化网络*/
    initSocket();

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

    this->disconnectServer();
}

void CommHelper::initSocket()
{
    // 初始化TCP
    this->mTcpClient = new QTcpSocket();
    //数据到达
    connect(mTcpClient, SIGNAL(readyRead()), this, SLOT(readyRead()));
    //网络故障
    connect(mTcpClient, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(error(QAbstractSocket::SocketError)));
    //客户端连接
    connect(mTcpClient, SIGNAL(connected()), this, SLOT(connected()));

    // 初始化UDP
    this->mUdpStatusClient1 = new QUdpSocket();
    if (this->mUdpStatusClient1->bind(QHostAddress("192.168.1.100"), 1000, QUdpSocket::ShareAddress)){
        connect(mUdpStatusClient1, &QUdpSocket::readyRead, this, &CommHelper::readyRead);
    }

    //炮号接收器
    this->mUdpServer = new QUdpSocket();
    this->mUdpServer->setSocketOption(QAbstractSocket::MulticastLoopbackOption, true);
    connect (mUdpServer, &QUdpSocket::readyRead, this, [=](){
        while (mUdpServer->hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(int(mUdpServer->pendingDatagramSize()));
            QHostAddress sender;
            quint16 senderPort;
            mUdpServer->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
            //+PLS_12345

            qInfo().noquote() << "接收广播消息：" << datagram;
            if (datagram.startsWith("+PLS_")){
                //解析炮号
                QRegularExpression regex("\\d+");
                QRegularExpressionMatchIterator iterator = regex.globalMatch(datagram);
                if (iterator.hasNext()) {
                    QRegularExpressionMatch match = iterator.next();
                    QString shotNum = match.captured(0);
                    emit reportShotnum(shotNum);
                }
            }
            else if (datagram.startsWith("TIME_")){
                QRegularExpression regex("TIME_(\\d{4})-(\\d{2})-(\\d{2}) (\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{3})");
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
                    emit reportSystemtime(tm);
                }
            }
            else if (datagram.startsWith("EMERGENCE_STOP")){
                emit reportEnergenceStop();
            }
        }
    });
    if (this->mUdpServer->bind(QHostAddress::Any, 12100, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)){
        this->mUdpServer->joinMulticastGroup(QHostAddress::LocalHost);
    }
}

void CommHelper::error(QAbstractSocket::SocketError error)
{
    if (error != QAbstractSocket::SocketTimeoutError){

    }
}

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

#include <bitset>
#include <QtEndian>
void CommHelper::readyRead()
{
    QByteArray datagram;
    do {
        datagram.resize(int(mUdpStatusClient1->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        mUdpStatusClient1->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        //+PLS_12345
        // if (datagram.size() > 0)
        //     DoReadyRead(datagram);
        // else
        //     break;
    } while (mUdpStatusClient1->hasPendingDatagrams());

    //读取新的数据
    //QByteArray tempData = mTcpClient->readAll();
    // QByteArray tempData = mTcpClient->readAll();
    // qDebug() << tempData;
    DoReadyRead(datagram);
}

void CommHelper::DoReadyRead(QByteArray& tempData)
{
    if (tempData.at(0) == 0x5A){
        // 选通器状态返回
        //5A 03 FF FF
        bool ok = false;
        quint32 v = tempData.toHex().toUInt(&ok, 16);
        //v = qbswap(v);
        std::bitset<32> bits(v);
        for (int i=0; i<18; ++i){
            if (mMapChannel[i+1] != bits.test(i))
            {
                mMapChannel[i+1] = bits.test(i);
                emit reportBackupChannelStatus(i+1, mMapChannel[i+1]);
            }
        }

        return;
    }

    mRawData.append(tempData);
    while (mRawData.contains('@') && mRawData.contains('#')){
        quint32 start = mRawData.indexOf('@');
        quint32 end = mRawData.indexOf('#', start);
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
            FeAddr = 01
            #
        */
        if (1)
        {
            rawData.replace('\n', ',');
            QMap<QString, QPair<double, double>> result = parseKeyValuePairsWithDefault(rawData);
            emit reportTemperatureAndVoltage(moduleNo, result);

            std::bitset<32> bits(static_cast<uint32_t>(result["IO_BIN"].first));
            for (int i=0; i<18; ++i){
                if (mMapChannel[i+1] != bits.test(i))
                {
                    mMapChannel[i+1] = bits.test(i);
                    emit reportBackupChannelStatus(i+1, mMapChannel[i+1]);
                }
            }
        }
        else
        {
            QList<QByteArray> lines = rawData.split('\n');
            QVector<float> temperature;
            QVector<QPair<float,float>> voltage_current;
            //IHA238_01 = 0.00V, 0.00mA
            for (int i=0; i<lines.size(); ++i){
                QString input = lines.at(i);
                if (input.contains("INA238")){
                    // 电压
                    QRegularExpression regex("(\\d+\\.\\d+)(V|mA)");
                    QRegularExpressionMatchIterator iterator = regex.globalMatch(input);

                    float voltage = 0.0;
                    float current = 0.0;
                    int j = 0;
                    while (iterator.hasNext()) {
                        QRegularExpressionMatch match = iterator.next();
                        QString valueStr = match.captured(1);
                        bool ok;
                        double value = valueStr.toFloat(&ok);
                        if (ok) {
                            if (j++ == 0)
                                voltage = value;
                            else
                                current = value;
                        }
                    }

                    voltage_current.push_back(qMakePair(voltage, current));
                }
                else if (input.contains("DS18B20")){
                    // 电流
                    QRegularExpression regex(R"(=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*[a-zA-Z]*)");
                    QRegularExpressionMatchIterator iterator = regex.globalMatch(input);

                    while (iterator.hasNext()) {
                        QRegularExpressionMatch match = iterator.next();
                        QString valueStr = match.captured(1);
                        bool ok;
                        double value = valueStr.toFloat(&ok);
                        if (ok) {
                            temperature.push_back(value);
                        }
                    }
                }
            }

            if (voltage_current.size() != 12)
            {
                qDebug() << QString(rawData);
                qDebug() << "exception";
            }
            else{

                emit reportTemperature(moduleNo, temperature);
                emit reportVoltageCurrent(moduleNo, voltage_current);
            }
        }
    }
}


void CommHelper::connected()
{
    //mTcpClient->write("start");//stop
}

/*
 打开网络
*/
bool CommHelper::connectServer()
{
    quint32 data = 0x000000A5;
    mUdpStatusClient1->writeDatagram((const char*)&data, 4, QHostAddress("192.168.1.212"), 8000);

    // QByteArray datagram("start");
    // int ret = mUdpStatusClient1->writeDatagram(datagram, QHostAddress("192.168.1.212"), 8000);
    //qDebug() << ret;
    return true;
    GlobalSettings settings(CONFIG_FILENAME);
    QString ip = settings.value("Net/ip", "192.168.1.100").toString();
    quint32 port = settings.value("Net/port", 6000).toUInt();
    mTcpClient->connectToHost(ip, port);
    mTcpClient->waitForConnected();
    if (mTcpClient->state() == QAbstractSocket::ConnectedState){
        //线程可以查询监控数据了
        return true;
    }

    return false;
}

/*
 断开网络
*/
void CommHelper::disconnectServer()
{
    mUdpStatusClient1->writeDatagram("stop", 4, QHostAddress("192.168.1.212"), 8000);
    return ;
    if (mTcpClient->isOpen())
        mTcpClient->write("stop");
    this->mTcpClient->close();
}

bool CommHelper::switchPower(quint32 channel, bool on)
{
    mMapPower[channel] = on;

    emit reportPowerStatus(channel, on);
    return true;
}

bool CommHelper::switchVoltage(quint32 channel, bool on)
{
    mMapVoltage[channel] = on;

    emit reportVoltageStatus(channel, on);
    return true;
}

bool CommHelper::switchBackupPower(quint32 channel, bool on)
{
    mMapBackupPower[channel] = on;

    emit reportBackupPowerStatus(channel, on);
    return true;
}

bool CommHelper::switchBackupVoltage(quint32 channel, bool on)
{
    mMapBackupVoltage[channel] = on;

    emit reportBackupVoltageStatus(channel, on);
    return true;
}

bool CommHelper::switchBackupChannel(quint32 channel, bool on)
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
            bits[i] = on;
        else
            bits[i] = mMapChannel[i+1];
    }

    quint32 v = bits.to_ulong();
    v = qbswap(v);
    QByteArray datagram = QByteArray::fromHex("A5 03 ff ff");
    quint32 data = 0x000000A5;
    mUdpStatusClient1->writeDatagram((const char*)&data, 4, QHostAddress("192.168.1.212"), 8000);
    //mUdpStatusClient1->writeDatagram(datagram, QHostAddress("192.168.1.212"), 1000);
    //mTcpClient->write((const char*)&v, 4);

    //emit reportBackupChannelStatus(channel, on);
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
        bits[i] = on;
    }

    quint32 v = bits.to_ulong();
    v = qbswap(v);
    QByteArray datagram = QByteArray::fromHex("A5 03 ff ff");
    mUdpStatusClient1->writeDatagram(datagram, QHostAddress("192.168.1.212"), 8000);
    //mUdpStatusClient1->writeDatagram((const char*)&v, 4, QHostAddress("192.168.1.212"), 1000);
    //mTcpClient->write((const char*)&v, 4);

    //emit reportBackupChannelStatus(channel, on);
    return true;
}
