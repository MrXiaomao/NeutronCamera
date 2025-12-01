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
    this->mTcpClient = new QTcpSocket();
    //数据到达
    connect(mTcpClient, SIGNAL(readyRead()), this, SLOT(readyRead()));
    //网络故障
    connect(mTcpClient, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(error(QAbstractSocket::SocketError)));
    //客户端连接
    connect(mTcpClient, SIGNAL(connected()), this, SLOT(connected()));
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

        emit reportTemperature(temperature);
        emit reportVoltage(voltage);
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
