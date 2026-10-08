#include "pducontrolwidget.h"
#include "ui_pducontrolwidget.h"
#include "QSnmpClient.h"
#include "AppConfig.h"
#include <QThread>

PDUControlWidget::PDUControlWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PDUControlWidget)
{
    ui->setupUi(this);

    QList<SwitchButton*> listRelay;
    listRelay << ui->relay1;
    listRelay << ui->relay2;
    listRelay << ui->relay3;
    listRelay << ui->relay4;
    listRelay << ui->relay5;
    listRelay << ui->relay6;
    listRelay << ui->relay7;
    listRelay << ui->relay8;
    QStringList listName;
    listName << "PSD全采样机箱";
    listName << "PSD衰减机箱";
    listName << "LSD全采样机箱";
    listName << "LSD衰减机箱";
    listName << "LBD全采样机箱";
    listName << "LBD衰减机箱";
    listName << "电 源 机 箱";
    listName << "备  用";
    int index = 1;
    for (const auto& relay: listRelay){
        relay->setText(listName[index-1], listName[index-1]);
        relay->setObjectName(QString("relay#%1").arg(index));
        relay->setProperty("index", index++);
        relay->setBgColor(Qt::gray, Qt::green);
        relay->setTextColor(Qt::black, Qt::black);
        relay->setAutoChecked(false);
        relay->setChecked(false);

        connect(relay, &SwitchButton::toggled, this, [=](bool checked){
            SwitchButton* relay = qobject_cast<SwitchButton*>(sender());
            qint16 index = relay->property("index").toInt();

            // 继电器状态
            QString host = AppConfig::instance().PDUipAddress();
            QString community = "private";
            QString oid = QString("1.3.6.1.4.1.23280.9.1.2.%1").arg(index);
            mSnmpClient->writeOid(host, "private", oid, checked ? 1 : 2, index);
            //QThread::msleep(125);

            // 控制完了，读一下状态
            // {
            //     QString oid = QString("1.3.6.1.4.1.23280.8.1.2.%1").arg(index);;// 继电器状态
            //     mSnmpClient->readOid(host, "public", oid);
            // }
        });
    }

    mSnmpClient = new QSnmpClient(this);
    QString host = AppConfig::instance().PDUipAddress();

    QObject::connect(mSnmpClient, &QSnmpClient::readFinished, [&](const UniversalSnmpReadResult &res){
        if (res.isValid){
            SwitchButton* relay = this->findChild<SwitchButton*>(QString("relay#%1").arg(res.index));
            if (relay){
                relay->setChecked(res.rawIntValue == 2);// 1-关闭 2-打开
            }
        }
    });

    QObject::connect(mSnmpClient, &QSnmpClient::writeFinished, [&](const UniversalSnmpWriteResult &res){
        if (res.writeSuccess){
            SwitchButton* relay = this->findChild<SwitchButton*>(QString("relay#%1").arg(res.index));
            if (relay){
                relay->setChecked(res.setValue == 1); // 1-闭合 2-断开
            }
        }
    });


    QObject::connect(ui->pushButton_refresh, &QPushButton::clicked, this, [=]{
        // 控制完了，读一下状态
        for (qint16 index=1; index<=8; ++index){
            QString oid = QString("1.3.6.1.4.1.23280.8.1.2.%1").arg(index);;// 继电器状态
            mSnmpClient->readOid(host, "public", oid, index);

            QEventLoop loop;
            QTimer::singleShot(125, &loop, &QEventLoop::quit);
            loop.exec();
        }
    });

    QObject::connect(ui->pushButton_open, &QPushButton::clicked, this, [=]{
        // 闭合
        QString host = AppConfig::instance().PDUipAddress();
        for (qint16 index=1; index<=8; ++index){
            QString oid = QString("1.3.6.1.4.1.23280.9.1.2.%1").arg(index);
            mSnmpClient->writeOid(host, "private", oid, 1, index);

            QEventLoop loop;
            QTimer::singleShot(125, &loop, &QEventLoop::quit);
            loop.exec();
        }
    });

    QObject::connect(ui->pushButton_close, &QPushButton::clicked, this, [=]{
        // 断开
        for (qint16 index=1; index<=8; ++index){
            QString oid = QString("1.3.6.1.4.1.23280.9.1.2.%1").arg(index);
            mSnmpClient->writeOid(host, "private", oid, 2, index);

            QEventLoop loop;
            QTimer::singleShot(125, &loop, &QEventLoop::quit);
            loop.exec();
        }
    });

    emit ui->pushButton_refresh->clicked();
}

PDUControlWidget::~PDUControlWidget()
{
    delete ui;
}
