#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QObject>
#include <QColor>

class QtnPropertySet;
class AppConfig : public QObject
{
    Q_OBJECT
public:
    // 建议用枚举统一管理ID，避免重复
    enum ConfigPropertySetID {
        // 采集卡通道管理
        ID_BOARD_SET = 1000,

        // 探测器测量参数设置
        ID_DETECTOR_SET = 2000,

        // 状态监测模块
        ID_STATEDEV_SET = 3000,

        // 数据中心网络
        ID_CONTROLCENTER_SET = 4000,
    };

    static AppConfig& instance();

    // 板卡通道管理
    void setBoardState(quint8 boardIndex, bool online = true);
    bool enableCapture(quint8 channel);
    bool enableCapture(quint8 boardIndex, bool isDDR1);

    // 探测器测量参数设置
    int deathTime() const;
    void setDeathTime(int deathdTime);

    int triggerThreshold() const;
    void setTriggerThreshold(int opacity);

    int spectrumRefreshTimelength() const;
    void setSpectrumRefreshTimelength(int spectrumRefreshTimelength);

    int triggerMode() const;
    void setTriggerMode(int triggerMode);

    int waveformLength() const;
    void setWaveformLength(int waveformLength);

    // 状态监测模块
    QString ipAddress() const;
    void setIpAddress(const QString& ip);

    int remotePort() const;
    void setRemotePort(int port);

    int localPort() const;
    void setLocalPort(int port);

    // 数据中心网络
    int boardcastPort() const;
    void setBoardcastPort(int port);

    QString dataSrvIpAddress() const;
    void setDataSrvIpAddress(const QString& ip);

    int dataSrvRemotePort() const;
    void setDataSrvRemotePort(int port);

    // 配置操作接口
    bool save(const QString& filePath = "config.json");
    bool load(const QString& filePath = "config.json");

    // 提供属性集给属性编辑界面使用（仅UI层需要）
    QtnPropertySet* propertySet(int id = -1);

signals:
    // 配置变化信号
    void portChanged(int port);
    void opacityChanged(float opacity);
    void autoStartChanged(bool enable);
    void configPathChanged(const QString& path);
    void themeColorChanged(const QColor& color);

    void buttonClicked();

private:
    AppConfig(QObject* parent = nullptr);
    ~AppConfig();
    Q_DISABLE_COPY(AppConfig)

    template<typename PropertyType, typename SignalType>
    void bindPropertyValueChange(PropertyType* property, SignalType signal);

    template<typename PropertyType, typename SignalType>
    void bindPropertyClicked(PropertyType* property, SignalType signal);

    class Private;
    Private* d;
};


#endif // APPCONFIG_H
