#ifndef COMMHELPER_H
#define COMMHELPER_H

#include <QObject>
#include <QTcpSocket>
#include <QMutex>
#include <QFile>
#include <QElapsedTimer>
#include <QWaitCondition>
#include <QTimer>
#include <QEventLoop>

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

    Q_SIGNAL void reportTemperature(float);
    Q_SIGNAL void reportVoltage(float);

    Q_SLOT void error(QAbstractSocket::SocketError);
    Q_SLOT void readyRead();
    Q_SLOT void connected();

private:
    QTcpSocket *mTcpClient = nullptr;

    /*
     初始化网络
    */
    void initSocket();
};

#endif // COMMHELPER_H
