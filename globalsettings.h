#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

#include <QObject>
#include <string.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMutex>
#include <QReadWriteLock>
#include <QFileInfo>
#include <QFileSystemWatcher>

#define SYSTEM_CONFIG_FILE "./Config/system_config.ini" //系统配置文件,保存UI界面等全局配置信息,用户不建议编辑
#define DEVICE_CONFIG_FILE "./Config/device_config.ini" //设备配置文件,保存设备相关的配置信息，如探测器参数等，用户可以编辑

#ifndef H5_DATA_COLS
#define H5_DATA_EXTEND      2       //触发时刻1（毫秒）+峰值1
#define RISING_WIDTH        20      //波形上升沿宽度
#define WAVEFORM_LENGTH     512     //波形上升沿参考点
#define H5_DATA_WAVEFORM    WAVEFORM_LENGTH     //扩展数据长度
#define H5_DATA_COLS        (H5_DATA_WAVEFORM + H5_DATA_EXTEND)
#endif //H5_DATA_COLS

#include <QSettings>
#include <QApplication>
class GlobalSettings: public QSettings
{
    Q_OBJECT
public:
    explicit GlobalSettings(QObject *parent = nullptr);
    explicit GlobalSettings(QString fileName, QObject *parent = nullptr);
    ~GlobalSettings();

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    void setValue(QAnyStringView key, const QVariant &value);
#else
    void setValue(const QString &key, const QVariant &value);
#endif
    void setRealtimeSave(bool realtime);
    bool isRealtimeSave() const { return realtime;}

private:
    bool realtime = false;
};

#define DET_NUM 24
#define IP_LENGTH 16
#define MAC_LENGTH 18
#define DEFAULT_HDF5_FILENAME "./Config/Settings.H5"
//参数结构定义
typedef struct _DetParameter{
    //探测器ID
    quint8 id;

    // 全局
    //心跳时间(秒)
    quint32 pluseCheckTime;
    //交换机地址
    //char switcherIp[IP_LENGTH];

    //数据接收服务器
    //IP地址
    char srvIp[IP_LENGTH];
    //子网掩码
    char srvSubnetMask[IP_LENGTH];
    //网关
    char srvGateway[IP_LENGTH];

    //时间服务器
    //IP地址
    char timerSrvIp[IP_LENGTH];

    //基本设置
    //增益
    /*
     0001：对应增益为0.08；
     0002：对应增益为0.16；
     0003：对应增益为0.32；
     0004：对应增益为0.63；
     0005：对应增益为1.26；
     0006：对应增益为2.52；
     0007：对应增益为5.01；
     0008：对应增益为10.0
    */
    quint32 gain;
    //死时间
    quint32 deathTime;
    //触发阈值
    quint32 triggerThold;

    // //网络设置
    //IP地址
    char detIp[IP_LENGTH];
    //MAC地址
    char detMacAddress[MAC_LENGTH];

    //能谱设置
    //能谱刷新时间（毫秒）
    quint32 spectrumRefreshTime;
    //能谱长度
    quint32 spectrumLength;

    //波形设置
    //触发模式
    quint8 waveformTriggerMode;
    //波形长度
    quint32 waveformLength;

    //梯形成型
    //是否启用
    bool trapShapeEnable;
    //时间常数D1
    quint32 trapShapeTimeConstD1;
    //时间常数D2
    quint32 trapShapeTimeConstD2;
    //上升沿
    quint32 trapShapeRisePoint;
    //平顶
    quint32 trapShapePeakPoint;
    //下降沿
    quint32 trapShapeFallPoint;

    //高压电源
    //是否启用
    bool highVoltageEnable;
    //DAC高压输出电平
    quint16 highVoltageOutLevel;

    _DetParameter(){
        // 全局
        //心跳时间(秒)
        pluseCheckTime = 10;
        //交换机地址
        // memset(switcherIp, 0, sizeof(switcherIp));
        // qstrcpy(switcherIp, "192.168.1.253");
        //数据接收服务器
        //IP地址
        memset(srvIp, 0, sizeof(srvIp));
        qstrcpy(srvIp, "192.168.0.200");
        //子网掩码
        memset(srvSubnetMask, 0, sizeof(srvSubnetMask));
        qstrcpy(srvSubnetMask, "255.255.255.0");
        //网关
        memset(srvGateway, 0, sizeof(srvGateway));
        qstrcpy(srvGateway, "192.168.0.1");

        //时间服务器
        //IP地址
        memset(timerSrvIp, 0, sizeof(timerSrvIp));
        qstrcpy(timerSrvIp, "192.168.0.30");

        //基本设置
        //增益
        gain = 1;
        //死时间
        deathTime = 300;
        //触发阈值
        triggerThold = 100;

        //网络设置
        //IP地址
        memset(detIp, 0, sizeof(detIp));
        qstrcpy(detIp, "0.0.0.0");
        //MAC地址
        memset(detMacAddress, 0, sizeof(detMacAddress));
        qstrcpy(detMacAddress, "00-08-00-00-00-00");

        //能谱设置
        //能谱刷新时间
        spectrumRefreshTime = 1000;
        //能谱长度
        spectrumLength = 2048;

        //波形设置
        //触发模式 0-定时触发 1-普通触发
        waveformTriggerMode = 0;
        //波形长度
        waveformLength = 64;

        //梯形成型
        //是否启用
        trapShapeEnable = false;
        //时间常数D1
        trapShapeTimeConstD1 = 0;
        //时间常数D2
        trapShapeTimeConstD2 = 0;
        //上升沿
        trapShapeRisePoint = 15;
        //平顶
        trapShapePeakPoint = 15;
        //下降沿
        trapShapeFallPoint = 15;

        //高压电源
        //是否启用
        highVoltageEnable = false;
        //DAC高压输出电平
        highVoltageOutLevel = 0;
    }
}DetParameter;

#include "H5Cpp.h"
class HDF5Settings: public QObject
{
    Q_OBJECT
public:
    explicit HDF5Settings(QObject *parent = nullptr);
    ~HDF5Settings();

    static HDF5Settings *instance() {
        static HDF5Settings hdf5Settings;
        return &hdf5Settings;
    }

    QMap<quint8, DetParameter>& detParameters();
    void setDetParameter(QMap<quint8, DetParameter>&);

    void sync();

private:
    H5::CompType mCompDataType;//复合数据类型
    QMap<quint8, DetParameter> mMapDetParameter;
};

#endif // GLOBALSETTINGS_H
