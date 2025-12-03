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
}

CommHelper::~CommHelper()
{
    this->disconnectServer();
}

void CommHelper::initSocket()
{
    // 初始化串口

    this->mSerialPort = new QSerialPort();
    //数据到达
    connect(mSerialPort, SIGNAL(readyRead()), this, SLOT(serialPortReadyRead()));

    GlobalSettings settings(CONFIG_FILENAME);
    QString portName = settings.value("SerialPort/name", "COM1").toString();
    quint32 baudRate = settings.value("SerialPort/baudRate", 115200).toUInt();
    quint32 dataBits = settings.value("SerialPort/dataBits", 8).toUInt();
    quint32 parity = settings.value("SerialPort/parity", 0).toUInt();
    quint32 stopBits = settings.value("SerialPort/stopBits", 1).toUInt();
    mSerialPort->setPortName(portName);
    if(mSerialPort->open(QIODevice::ReadWrite))
    {
        mSerialPort->setBaudRate(QSerialPort::Baud115200,QSerialPort::AllDirections);//设置波特率和读写方向
        mSerialPort->setDataBits(QSerialPort::DataBits(dataBits));
        mSerialPort->setFlowControl(QSerialPort::NoFlowControl);//无流控制
        mSerialPort->setParity(QSerialPort::Parity(parity));	//无校验位
        mSerialPort->setStopBits(QSerialPort::StopBits(stopBits)); //一位停止位
    }
    else{
        qInfo().noquote() << "串口打开失败！！！";
    }

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
            THA238_09 = 0.00V, 0.00mA
            THA238_10 = 0.00V, 0.00mA
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
        float temperature = 0.0;
        float voltage = 0.0;
        float current = 0.0;
        //IHA238_01 = 0.00V, 0.00mA
        for (int i=0; i<lines.size(); ++i){
            QString input = lines.at(i);
            if (input.contains("IHA238")){
                QRegularExpression regex(R"(=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*[a-zA-Z]*)");
                QRegularExpressionMatchIterator iterator = regex.globalMatch(input);

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
            }
            else if (input.contains("DS18B20")){
                QRegularExpression regex(R"(=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*[a-zA-Z]*)");
                QRegularExpressionMatchIterator iterator = regex.globalMatch(input);

                int j = 0;
                while (iterator.hasNext()) {
                    QRegularExpressionMatch match = iterator.next();
                    QString valueStr = match.captured(1);
                    bool ok;
                    double value = valueStr.toFloat(&ok);
                    if (ok) {
                        temperature = value;
                    }
                }
            }
        }

        emit reportTemperature(1, 1, temperature);
        emit reportVoltage(1, 1, voltage);
        emit reportCurrent(1, 1, current);
    }
}

void CommHelper::serialPortReadyRead()
{
    //读取新的数据
    QByteArray rawData = mSerialPort->readAll();
    if (rawData.startsWith('@') && rawData.endsWith('#')){
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
            THA238_09 = 0.00V, 0.00mA
            THA238_10 = 0.00V, 0.00mA
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
        float temperature = 0.0;
        float voltage = 0.0;
        float current = 0.0;
        //IHA238_01 = 0.00V, 0.00mA
        for (int i=0; i<lines.size(); ++i){
            QString input = lines.at(i);
            if (input.contains("IHA238")){
                QRegularExpression regex(R"(=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*[a-zA-Z]*)");
                QRegularExpressionMatchIterator iterator = regex.globalMatch(input);

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
            }
            else if (input.contains("DS18B20")){
                QRegularExpression regex(R"(=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*[a-zA-Z]*)");
                QRegularExpressionMatchIterator iterator = regex.globalMatch(input);

                int j = 0;
                while (iterator.hasNext()) {
                    QRegularExpressionMatch match = iterator.next();
                    QString valueStr = match.captured(1);
                    bool ok;
                    double value = valueStr.toFloat(&ok);
                    if (ok) {
                        temperature = value;
                    }
                }
            }
        }

        emit reportTemperature(1, 1, temperature);
        emit reportVoltage(1, 1, voltage);
        emit reportCurrent(1, 1, current);
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
        //发送查询指令
        //@01*999*GET*01*#
        QByteArray askCommand = "@01*999*GET*01*#";
        mTcpClient->write(askCommand, askCommand.size());

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
