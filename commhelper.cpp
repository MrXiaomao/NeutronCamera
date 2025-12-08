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

    mRequestCmdThread = new QLiteThread(this);
    mRequestCmdThread->setObjectName("mRequestCmdThread");
    mRequestCmdThread->setWorkThreadProc([=](){
        while (!mRequestCmdThread->isInterruptionRequested())
        {
            if (mTcpClient->isOpen()){
                //发送查询指令
                //@01*999*GET*01*#
                for (int i=1; i<=18; ++i){
                    QString askCommand = QString("@%1*999*GET*01*#").arg(i, 2, 10, QLatin1Char('0'));
                    mTcpClient->write(askCommand.toLatin1());
                    mTcpClient->waitForBytesWritten();

                    QThread::msleep(500);
                }
            }

            QThread::msleep(1000);
        }
    });
    mRequestCmdThread->start();
    connect(this, &CommHelper::destroyed, [=]() {
        mRequestCmdThread->exit(0);
        mRequestCmdThread->wait(500);
    });
}

CommHelper::~CommHelper()
{
    mRequestCmdThread->requestInterruption();
    mRequestCmdThread->quit();
    mRequestCmdThread->wait();
    mRequestCmdThread->deleteLater();

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
void CommHelper::readyRead()
{
    //读取新的数据
    QByteArray rawData = mTcpClient->readAll();
    if (rawData.startsWith('@') && rawData.endsWith('#')){
        quint8 moduleNo = rawData.mid(1, 2).toInt();
        /*
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

        QList<QByteArray> lines = rawData.split('\n');
        QVector<float> temperature;
        QVector<QPair<float,float>> voltage_current;
        //IHA238_01 = 0.00V, 0.00mA
        for (int i=0; i<lines.size(); ++i){
            QString input = lines.at(i);
            if (input.contains("IHA238")){
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

        emit reportTemperature(moduleNo, temperature);
        emit reportVoltageCurrent(moduleNo, voltage_current);
    }
}


void CommHelper::connected()
{

}

/*
 打开网络
*/
bool CommHelper::connectServer()
{
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
    this->mTcpClient->close();
}
