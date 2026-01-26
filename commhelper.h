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
    bool switchBackupPower(quint32, bool);
    bool switchBackupVoltage(quint32, bool);
    bool switchBackupChannel(quint32, bool);
    bool switchAllBackupChannel(bool);

    Q_SIGNAL void reportPowerStatus(quint32, bool);
    Q_SIGNAL void reportVoltageStatus(quint32, bool);
    Q_SIGNAL void reportBackupPowerStatus(quint32, bool);
    Q_SIGNAL void reportBackupVoltageStatus(quint32, bool);
    Q_SIGNAL void reportBackupChannelStatus(quint32, bool);

    Q_SIGNAL void reportTemperature(quint8, QVector<float>&);
    Q_SIGNAL void reportVoltageCurrent(quint8, QVector<QPair<float,float>>&);
    Q_SIGNAL void reportTemperatureAndVoltage(quint8, QMap<QString, QPair<double, double>>&);
    Q_SIGNAL void reportShotnum(QString);
    Q_SIGNAL void reportSystemtime(QDateTime);
    Q_SIGNAL void reportEnergenceStop();

    Q_SLOT void error(QAbstractSocket::SocketError);
    Q_SLOT void readyRead();
    Q_SLOT void connected();

    Q_SLOT void DoReadyRead(QByteArray&);

private:
    QTcpSocket *mTcpClient = nullptr;
    QUdpSocket *mUdpServer = nullptr;
    QUdpSocket *mUdpStatusClient1 = nullptr;// 设备电压/电流状态检测
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
