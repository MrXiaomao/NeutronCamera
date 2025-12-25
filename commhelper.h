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

    Q_SIGNAL void reportTemperature(quint8, QVector<float>&);
    Q_SIGNAL void reportVoltageCurrent(quint8, QVector<QPair<float,float>>&);
    Q_SIGNAL void reportShotnum(QString);
    Q_SIGNAL void reportSystemtime(QDateTime);
    Q_SIGNAL void reportEnergenceStop();

    Q_SLOT void error(QAbstractSocket::SocketError);
    Q_SLOT void readyRead();
    Q_SLOT void connected();

private:
    QTcpSocket *mTcpClient = nullptr;
    QUdpSocket *mUdpServer = nullptr;
    QLiteThread* mRequestCmdThread = nullptr;
    QByteArray mRawData;
    /*
     初始化网络
    */
    void initSocket();
};

#endif // COMMHELPER_H
