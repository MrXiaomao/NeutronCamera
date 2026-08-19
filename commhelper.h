#ifndef COMMHELPER_H
#define COMMHELPER_H

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QMutex>
#include <QFile>
#include <QElapsedTimer>
#include <QWaitCondition>
#include <QTimer>
#include <QDateTime>
#include <QEventLoop>
#include "qlitethread.h"

class CommHelper : public QObject
{
    Q_OBJECT
public:
    explicit CommHelper(QObject *parent = nullptr);
    ~CommHelper();

    static CommHelper *instance() {
        static CommHelper commHelper;
        return &commHelper;
    }

    /*
     打开服务
    */
    bool connectServer();
    /*
     关闭服务
    */
    void disconnectServer();

    bool switchPower(quint32, bool);
    bool switchVoltage(quint32, bool);
    bool openAllPower();
    bool closeAllPower();
    bool switchBackupPower(quint32, bool);
    bool switchBackupVoltage(quint32, bool);
    bool switchBackupChannel(quint32, bool);
    bool switchAllBackupChannel(bool);

    Q_SIGNAL void powerStatusChanged(quint32, bool);
    Q_SIGNAL void voltageStatusChanged(quint32, bool);
    Q_SIGNAL void backupPowerStatusChanged(quint32, bool);
    Q_SIGNAL void backupVoltageStatusChanged(quint32, bool);
    Q_SIGNAL void backupChannelStatusChanged(quint32, bool);

    Q_SIGNAL void temperatureChanged(quint8, QVector<float>&);
    Q_SIGNAL void voltageAndCurrentChanged(quint8, QVector<QPair<float,float>>&);
    Q_SIGNAL void temperatureAndVoltageChanged(quint8, QMap<QString, QPair<double, double>>&);
    Q_SIGNAL void shotnumValueChanged(const QString&);
    Q_SIGNAL void systemTimeValueChanged(const QDateTime&);
    Q_SIGNAL void energenceStopSignalTriggered();
    Q_SIGNAL void connected();
    Q_SIGNAL void disconnected();

    Q_SLOT void error(QAbstractSocket::SocketError);
    Q_SLOT void readyRead();

    Q_SLOT void onReadyRead(QByteArray&);

private:
    QUdpSocket *mUdpShotReceiver = nullptr;// 炮号接收器
    QUdpSocket *mUdpPerformanceMonitorReceiver = nullptr;// 设备电压/电流/温度等性能监测，所有发送端口都是8000，接收端口ip100:1000,ip101:8080,ip102:8081
    QTimer* mTimerout;// 网络连接超时
    QLiteThread* mRequestCmdThread = nullptr;
    QByteArray mRawData;

    QMap<quint32, bool> mMapPower;//探测器的1#电源开关
    QMap<quint32, bool> mMapVoltage;//探测器的1#电压开关
    QMap<quint32, bool> mMapBackupPower;//探测器的2#电源开关
    QMap<quint32, bool> mMapBackupVoltage;//探测器的2#电压开关
    QMap<quint32, bool> mMapChannel;//选通开关,false-1#,true-2#

    /*
     初始化网络
    */
    void initSocket();
};

#endif // COMMHELPER_H
