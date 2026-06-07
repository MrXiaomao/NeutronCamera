#include "AppConfig.h"
#include <QtnProperty/PropertySet.h>
#include <QtnProperty/Core/PropertyInt.h>
#include <QtnProperty/Core/PropertyUInt.h>
#include <QtnProperty/Core/PropertyFloat.h>
#include <QtnProperty/Core/PropertyBool.h>
#include <QtnProperty/Core/PropertyQString.h>
#include <QtnProperty/Core/PropertyEnum.h>
#include <QtnProperty/Core/PropertyQPoint.h>
#include <QtnProperty/Core/PropertyQSize.h>
#include <QtnProperty/Core/PropertyQRect.h>
#include <QtnProperty/GUI/PropertyButton.h>
#include <QtnProperty/GUI/PropertyQBrush.h>
#include <QtnProperty/GUI/PropertyQFont.h>
#include <QtnProperty/GUI/PropertyQPen.h>
#include <QtnProperty/GUI/PropertyQColor.h>
#include <QFile>
#include <QFileDialog>
#include <QDataStream>

class AppConfig::Private
{
public:
    QtnPropertySet* propSetRoot;

    //板卡通道管理
    QtnPropertySet* propSetBoardChannels;
    QtnPropertySet* propSetBoard1;
    QtnPropertySet* propSetBoard2;
    QtnPropertySet* propSetBoard3;

    QtnPropertyBool* propBoard1EnableDDR1;
    QtnPropertyBool* propBoard1EnableDDR2;
    QtnPropertyBool* propBoard2EnableDDR1;
    QtnPropertyBool* propBoard2EnableDDR2;
    QtnPropertyBool* propBoard3EnableDDR1;
    QtnPropertyBool* propBoard3EnableDDR2;

    //探测器参数设置
    QtnPropertySet* propSetDetector;
    QtnPropertyInt* psdThreshold; // PSD甄别阈值
    QtnPropertyInt* deathTime; // 死时间
    QtnPropertyInt* triggerThreshold;// 触发阈值
    QtnPropertyInt* spectrumRefreshTimelength;// 能谱刷新时间
    QtnPropertyQString* triggerMode;// 触发模式
    QtnPropertyQString* waveformLength;// 波形长度

    //状态监测模块
    QtnPropertySet* propSetState;
    QtnPropertyQString* devIpAddress;// 设备地址
    QtnPropertyUInt* devRemotePort;  // 远程端口
    QtnPropertyUInt* devLocalPort;  // 本地端口

    //集控中心网络
    QtnPropertySet* propSetControlCenter;
    QtnPropertyUInt* cmdUdpBroadcastPort;// 广播端口
    QtnPropertyQString* dataSrvIpAddress;  // 数据服务器地址
    QtnPropertyUInt* dataSrvRemotePort;  // 通讯端口
};

// 在AppConfig类中添加辅助函数
template<typename PropertyType, typename SignalType>
void AppConfig::bindPropertyValueChange(PropertyType* property, SignalType signal)
{
    connect(property, &PropertyType::propertyWillChange,
            this, [=](QtnPropertyChangeReason reason, QtnPropertyValuePtr newValue, int typeId) {
                Q_UNUSED(typeId);
                if (reason & QtnPropertyChangeReasonValue) {
                    using ValueType = decltype(property->value());
                    ValueType value = *qtnCastPropertyValue<ValueType>(newValue);
                    emit (this->*signal)(value);
                }
            });
}

template<typename PropertyType, typename SignalType>
void AppConfig::bindPropertyClicked(PropertyType* property, SignalType signal)
{
    connect(property, &PropertyType::setClickHandler,
            this, [=](const QtnPropertyButton* btn) {
                Q_UNUSED(btn);
                emit (this->*signal)();
            });
}

AppConfig::AppConfig(QObject* parent)
    : QObject(parent)
    , d(new Private)
{
    d->propSetRoot = new QtnPropertySet(this);

    quint32 baseId = ID_BOARD_SET;
    //板卡通道管理
    {
        QtnPropertySet* propSet = qtnCreateProperty<QtnPropertySet>(d->propSetRoot, QStringLiteral("采集卡通道管理"));
        d->propSetBoardChannels = propSet;
        d->propSetBoard1 = qtnCreateProperty<QtnPropertySet>(propSet, QStringLiteral("板卡#1"));
        d->propSetBoard2 = qtnCreateProperty<QtnPropertySet>(propSet, QStringLiteral("板卡#2"));
        d->propSetBoard3 = qtnCreateProperty<QtnPropertySet>(propSet, QStringLiteral("板卡#3"));

        d->propBoard1EnableDDR1 = qtnCreateProperty<QtnPropertyBool>(d->propSetBoard1, QStringLiteral("启用DDR#1"));
        d->propBoard1EnableDDR2 = qtnCreateProperty<QtnPropertyBool>(d->propSetBoard1, QStringLiteral("启用DDR#2"));
        d->propBoard2EnableDDR1 = qtnCreateProperty<QtnPropertyBool>(d->propSetBoard2, QStringLiteral("启用DDR#1"));
        d->propBoard2EnableDDR2 = qtnCreateProperty<QtnPropertyBool>(d->propSetBoard2, QStringLiteral("启用DDR#2"));
        d->propBoard3EnableDDR1 = qtnCreateProperty<QtnPropertyBool>(d->propSetBoard3, QStringLiteral("启用DDR#1"));
        d->propBoard3EnableDDR2 = qtnCreateProperty<QtnPropertyBool>(d->propSetBoard3, QStringLiteral("启用DDR#2"));

        d->propSetBoard1->setState(QtnPropertyStateImmutable, false);
        d->propSetBoard2->setState(QtnPropertyStateImmutable, false);
        d->propSetBoard3->setState(QtnPropertyStateImmutable, false);
        d->propBoard1EnableDDR1->setState(QtnPropertyStateImmutable, false);
        d->propBoard1EnableDDR2->setState(QtnPropertyStateImmutable, false);
        d->propBoard2EnableDDR1->setState(QtnPropertyStateImmutable, false);
        d->propBoard2EnableDDR2->setState(QtnPropertyStateImmutable, false);
        d->propBoard3EnableDDR1->setState(QtnPropertyStateImmutable, false);
        d->propBoard3EnableDDR1->setState(QtnPropertyStateImmutable, false);

        propSet->collapse();
    }

    // 探测器参数设置
    baseId = ID_DETECTOR_SET;
    {
        QtnPropertySet* propSet = new QtnPropertySet(d->propSetRoot);
        d->propSetDetector = propSet;
        d->propSetRoot->addChildProperty(propSet);
        propSet->setName("探测器参数设置");
        propSet->setId(ID_DETECTOR_SET);

        // PSD甄别阈值
        d->psdThreshold = qtnCreateProperty<QtnPropertyInt>(propSet);
        d->psdThreshold->setId(++baseId);
        d->psdThreshold->setName("PSD甄别阈值");
        d->psdThreshold->setDescription("范围：0 ~ 255");
        d->psdThreshold->setMaxValue(255);
        d->psdThreshold->setMinValue(0);
        d->psdThreshold->setValue(133);

        //死时间*10ns
        d->deathTime = qtnCreateProperty<QtnPropertyInt>(propSet);
        d->deathTime->setId(++baseId);
        d->deathTime->setName("死时间（*10ns）");
        d->deathTime->setDescription("死时间范围：0 ~ 1000");
        d->deathTime->setMaxValue(1000);
        d->deathTime->setMinValue(0);
        d->deathTime->setValue(30);

        //触发阈值
        d->triggerThreshold = qtnCreateProperty<QtnPropertyInt>(propSet);
        d->triggerThreshold->setId(++baseId);
        d->triggerThreshold->setName("触发阈值");
        d->triggerThreshold->setDescription("阈值范围：0 ~ 1000");
        d->triggerThreshold->setMaxValue(1000);
        d->triggerThreshold->setMinValue(0);
        d->triggerThreshold->setValue(200);

        //能谱刷新时间
        d->spectrumRefreshTimelength = qtnCreateProperty<QtnPropertyInt>(propSet);
        d->spectrumRefreshTimelength->setId(++baseId);
        d->spectrumRefreshTimelength->setName("能谱刷新时间");
        d->spectrumRefreshTimelength->setDescription("时间范围：0 ~ 10000");
        d->spectrumRefreshTimelength->setMaxValue(10000);
        d->spectrumRefreshTimelength->setMinValue(0);
        d->spectrumRefreshTimelength->setValue(1000);

        //触发模式
        d->triggerMode = qtnCreateProperty<QtnPropertyQString>(propSet);
        d->triggerMode->setId(++baseId);
        d->triggerMode->setName("触发模式");
        d->triggerMode->setDescription("模式：定时触发 正常触发");
        {
            QtnPropertyDelegateInfo listDele;
            listDele.name = "ComboBox";
            listDele.attributes["items"] = QStringList()<<"定时触发"<<"正常触发";
            d->triggerMode->setDelegateInfo(listDele);
        }
        d->triggerMode->setValue("正常触发");

        //波形长度
        d->waveformLength = qtnCreateProperty<QtnPropertyQString>(propSet);
        d->waveformLength->setId(++baseId);
        d->waveformLength->setName("波形长度");
        d->waveformLength->setDescription("取值范围：64 128 256 512");
        {
            QtnPropertyDelegateInfo listDele;
            listDele.name = "ComboBox";
            listDele.attributes.insert("items", QVariant::fromValue(QStringList() << "64" << "128" << "256" << "512"));
            d->waveformLength->setDelegateInfo(listDele);
        }
        d->waveformLength->setValue("64");
    }

    // 状态监测模块
    baseId = ID_STATEDEV_SET;
    {
        QtnPropertySet* propSet = new QtnPropertySet(d->propSetRoot);
        d->propSetState = propSet;
        d->propSetRoot->addChildProperty(propSet);
        propSet->setName("状态监测模块");
        propSet->setId(ID_STATEDEV_SET);

        // 设备地址
        d->devIpAddress = new QtnPropertyQString(propSet);
        d->devIpAddress->setId(++baseId);
        d->devIpAddress->setName("设备地址");
        d->devIpAddress->setDescription("如：192.168.1.212");
        d->devIpAddress->setValue("192.168.1.212");

        // 远程端口
        d->devRemotePort = new QtnPropertyUInt(propSet);
        d->devRemotePort->setId(++baseId);
        d->devRemotePort->setName("远程端口");
        d->devRemotePort->setDescription("端口范围：0 ~ 65536");
        d->devRemotePort->setMaxValue(65536);
        d->devRemotePort->setMinValue(0);
        d->devRemotePort->setValue(8000);

        // 本地端口
        d->devLocalPort = new QtnPropertyUInt(propSet);
        d->devLocalPort->setId(++baseId);
        d->devLocalPort->setName("本地端口");
        d->devLocalPort->setDescription("UDP端口范围：0 ~ 65536");
        d->devLocalPort->setMaxValue(65536);
        d->devLocalPort->setMinValue(0);
        d->devLocalPort->setValue(1000);

        propSet->addChildProperty(d->devIpAddress);
        propSet->addChildProperty(d->devRemotePort);
        propSet->addChildProperty(d->devLocalPort);
    }

    // 数据中心网络
    baseId = ID_CONTROLCENTER_SET;
    {
        QtnPropertySet* propSet = new QtnPropertySet(d->propSetRoot);
        d->propSetControlCenter = propSet;
        d->propSetRoot->addChildProperty(propSet);
        propSet->setName("数据中心网络");
        propSet->setId(ID_CONTROLCENTER_SET);

        // 广播端口
        d->cmdUdpBroadcastPort = new QtnPropertyUInt(propSet);
        d->cmdUdpBroadcastPort->setId(++baseId);
        d->cmdUdpBroadcastPort->setName("信令广播端口");
        d->cmdUdpBroadcastPort->setDescription("UDP端口范围：0 ~ 65536");
        d->cmdUdpBroadcastPort->setMaxValue(65536);
        d->cmdUdpBroadcastPort->setMinValue(0);
        d->cmdUdpBroadcastPort->setValue(12100);

        // 设备地址
        d->dataSrvIpAddress = new QtnPropertyQString(propSet);
        d->dataSrvIpAddress->setId(++baseId);
        d->dataSrvIpAddress->setName("服务器地址");
        d->dataSrvIpAddress->setDescription("如：192.168.1.212");
        d->dataSrvIpAddress->setValue("192.168.1.212");

        // 远程端口
        d->dataSrvRemotePort = new QtnPropertyUInt(propSet);
        d->dataSrvRemotePort->setId(++baseId);
        d->dataSrvRemotePort->setName("远程端口");
        d->dataSrvRemotePort->setDescription("范围：0 ~ 65536");
        d->dataSrvRemotePort->setMaxValue(65536);
        d->dataSrvRemotePort->setMinValue(0);
        d->dataSrvRemotePort->setValue(8000);

        propSet->addChildProperty(d->cmdUdpBroadcastPort);
        propSet->addChildProperty(d->dataSrvIpAddress);
        propSet->addChildProperty(d->dataSrvRemotePort);
    }

    //this->load();

    // 类型安全的绑定，自动识别属性类型
    //    bindPropertyValueChange(d->port, &AppConfig::portChanged);
    //    bindPropertyValueChange(d->opacity, &AppConfig::opacityChanged);
    //    bindPropertyValueChange(d->autoStart, &AppConfig::autoStartChanged);
    //    bindPropertyValueChange(d->configPath, &AppConfig::configPathChanged);
    //    bindPropertyValueChange(d->themeColor, &AppConfig::themeColorChanged);
    //    bindPropertyClicked(d->button, &AppConfig::buttonClicked);
}

AppConfig::~AppConfig()
{
    this->save();
    delete d;
}

AppConfig& AppConfig::instance()
{
    static AppConfig config;
    return config;
}

#include <QJsonObject>
#include <QJsonDocument>
bool AppConfig::save(const QString& filePath)
{
    if (1){
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

        QJsonObject rootObj;
        // 递归遍历属性集生成JSON
        std::function<void(QtnPropertySet*, QJsonObject&)> serializeSet = [&](QtnPropertySet* set, QJsonObject& obj) {
            for (int i = 0; i < set->childProperties().count(); ++i) {
                QtnPropertyBase* prop = set->childProperties().at(i);
                QString propName = prop->name();

                if (prop->inherits("QtnPropertySet")) {
                    // 嵌套属性集，递归处理
                    QJsonObject subObj;
                    serializeSet(qobject_cast<QtnPropertySet*>(prop), subObj);
                    obj[prop->name()] = subObj;
                } else {
                    QVariant value;
                    // 按类型取值
                    if (prop->inherits("QtnPropertyInt")) {
                        value = static_cast<QtnPropertyInt*>(prop)->value();
                    } else if (prop->inherits("QtnPropertyQString")) {
                        value = static_cast<QtnPropertyQString*>(prop)->value();
                    } else if (prop->inherits("QtnPropertyBool")) {
                        value = static_cast<QtnPropertyBool*>(prop)->value();
                    } else if (prop->inherits("QtnPropertyFloat")) {
                        value = static_cast<QtnPropertyFloat*>(prop)->value();
                    } else if (prop->inherits("QtnPropertyUInt")) {
                        value = static_cast<QtnPropertyUInt*>(prop)->value();
                    }
                    obj.insert(propName, QJsonValue::fromVariant(value));
                }
            }
        };

        serializeSet(d->propSetRoot, rootObj);
        QJsonDocument doc(rootObj);
        file.write(doc.toJson(QJsonDocument::Indented)); // 格式化输出，可读性好
        file.close();
        return true;
    } else
    {
        // 二进制流方式保存，不方便查看
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) return false;
        QDataStream out(&file);
        //out << *d->propSetRoot;
        d->propSetRoot->save(out);
        return true;
    }
}

bool AppConfig::load(const QString& filePath)
{
    if (!QFile::exists(filePath))
        return false;

    if (1){
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

        QByteArray data = file.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) return false;

        QJsonObject rootObj = doc.object();
        // 递归加载属性
        std::function<void(QtnPropertySet*, const QJsonObject&)> deserializeSet = [&](QtnPropertySet* set, const QJsonObject& obj) {
            for (int i = 0; i < set->childProperties().count(); ++i) {
                QtnPropertyBase* prop = set->childProperties().at(i);
                QString propName = prop->name(); // 中文属性名
                if (!obj.contains(propName)) continue;
                QJsonValue jsonValue = obj.value(propName);
                if (jsonValue.isUndefined()) continue;

                if (prop->inherits("QtnPropertySet")) {
                    // 嵌套属性集，递归加载
                    deserializeSet(qobject_cast<QtnPropertySet*>(prop), obj[prop->name()].toObject());
                } else {
                    // 加载时类型安全赋值示例
                    if (prop->inherits("QtnPropertyInt")) {
                        QtnPropertyInt* intProp = static_cast<QtnPropertyInt*>(prop);
                        intProp->setValue(jsonValue.toInt());
                    } else if (prop->inherits("QtnPropertyQString")) {
                        QtnPropertyQString* strProp = static_cast<QtnPropertyQString*>(prop);
                        strProp->setValue(jsonValue.toString());
                    } else if (prop->inherits("QtnPropertyBool")) {
                        QtnPropertyBool* boolProp = static_cast<QtnPropertyBool*>(prop);
                        boolProp->setValue(jsonValue.toBool());
                    } else if (prop->inherits("QtnPropertyFloat")) {
                        QtnPropertyFloat* floatProp = static_cast<QtnPropertyFloat*>(prop);
                        floatProp->setValue(jsonValue.toDouble());
                    } else if (prop->inherits("QtnPropertyUInt")) {
                        QtnPropertyUInt* uintProp = static_cast<QtnPropertyUInt*>(prop);
                        uintProp->setValue(jsonValue.toInt());
                    }
                }
            }
        };

        deserializeSet(d->propSetRoot, rootObj);
        file.close();
        return true;
    } else
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        QDataStream in(&file);
        //in >> *d->propSetRoot;
        d->propSetRoot->load(in);
        return true;
    }
}

QtnPropertySet* AppConfig::propertySet(int id)
{
    if (ID_DETECTOR_SET == id)
        return d->propSetDetector;
    else if (ID_STATEDEV_SET == id)
        return d->propSetState;
    else if (ID_CONTROLCENTER_SET == id)
        return d->propSetControlCenter;
    else
        return d->propSetRoot;
}


// 板卡通道管理
void AppConfig::setBoardState(quint8 boardIndex, bool online)
{
    QtnPropertySet* propSetBoard = nullptr;
    if (1 == boardIndex)
        propSetBoard = d->propSetBoard1;
    if (2 == boardIndex)
        propSetBoard = d->propSetBoard2;
    if (3 == boardIndex)
        propSetBoard = d->propSetBoard3;

    if (propSetBoard){
        if (online)
        {
            propSetBoard->removeState(QtnPropertyStateImmutable, true);
            if (1 == boardIndex)
            {
                d->propBoard1EnableDDR1->removeState(QtnPropertyStateImmutable, true);
                d->propBoard1EnableDDR2->removeState(QtnPropertyStateImmutable, true);
                d->propBoard1EnableDDR1->setValue(true);
                d->propBoard1EnableDDR2->setValue(true);
            } else if (2 == boardIndex)
            {
                d->propBoard2EnableDDR1->removeState(QtnPropertyStateImmutable, true);
                d->propBoard2EnableDDR2->removeState(QtnPropertyStateImmutable, true);
                d->propBoard2EnableDDR1->setValue(true);
                d->propBoard2EnableDDR2->setValue(true);
            } else if (3 == boardIndex)
            {
                d->propBoard3EnableDDR1->removeState(QtnPropertyStateImmutable, true);
                d->propBoard3EnableDDR2->removeState(QtnPropertyStateImmutable, true);
                d->propBoard3EnableDDR1->setValue(true);
                d->propBoard3EnableDDR2->setValue(true);
            }
        }
        else
        {
            propSetBoard->addState(QtnPropertyStateImmutable, true);
            if (1 == boardIndex)
            {
                d->propBoard1EnableDDR1->addState(QtnPropertyStateImmutable, true);
                d->propBoard1EnableDDR2->addState(QtnPropertyStateImmutable, true);
                d->propBoard1EnableDDR1->setValue(false);
                d->propBoard1EnableDDR2->setValue(false);
            } else if (2 == boardIndex)
            {
                d->propBoard2EnableDDR1->addState(QtnPropertyStateImmutable, true);
                d->propBoard2EnableDDR2->addState(QtnPropertyStateImmutable, true);
                d->propBoard2EnableDDR1->setValue(false);
                d->propBoard2EnableDDR2->setValue(false);
            } else if (3 == boardIndex)
            {
                d->propBoard3EnableDDR1->addState(QtnPropertyStateImmutable, true);
                d->propBoard3EnableDDR2->addState(QtnPropertyStateImmutable, true);
                d->propBoard3EnableDDR1->setValue(false);
                d->propBoard3EnableDDR2->setValue(false);
            }
        }
    }
}

bool AppConfig::enableCapture(quint8 boardIndex, bool isDDR1)
{
    QtnPropertyBool* propBoardEnableDDR = nullptr;
    if (1 == boardIndex)
    {
        propBoardEnableDDR = isDDR1 ? d->propBoard1EnableDDR1 : d->propBoard1EnableDDR2;
    } else if (2 == boardIndex)
    {
        propBoardEnableDDR = isDDR1 ? d->propBoard2EnableDDR1 : d->propBoard2EnableDDR2;
    } else if (3 == boardIndex)
    {
        propBoardEnableDDR = isDDR1 ? d->propBoard3EnableDDR1 : d->propBoard3EnableDDR2;
    }

    if (propBoardEnableDDR)
        return propBoardEnableDDR->value();
    else
        return false;
}

// 探测器测量参数设置
int AppConfig::psdThreshold() const { return d->psdThreshold->value(); }
int AppConfig::deathTime() const { return d->deathTime->value(); }
void AppConfig::setDeathTime(int deathTime) { d->deathTime->setValue(deathTime); }

int AppConfig::triggerThreshold() const { return d->triggerThreshold->value(); }
void AppConfig::setTriggerThreshold(int triggerThreshold) { d->triggerThreshold->setValue(triggerThreshold); }

int AppConfig::spectrumRefreshTimelength() const { return d->spectrumRefreshTimelength->value(); }
void AppConfig::setSpectrumRefreshTimelength(int spectrumRefreshTimelength) { d->spectrumRefreshTimelength->setValue(spectrumRefreshTimelength); }

int AppConfig::triggerMode() const {
    QStringList lst = QStringList()<<"定时触发"<<"正常触发";
    return lst.indexOf(d->triggerMode->value());
}
void AppConfig::setTriggerMode(int triggerMode) {
    QStringList lst = QStringList()<<"定时触发"<<"正常触发";
    d->triggerMode->setValue(lst.at(triggerMode));
}

int AppConfig::waveformLength() const {
    QStringList lst = QStringList() << "64" << "128" << "256" << "512";
    return lst.indexOf(d->waveformLength->value());
}
void AppConfig::setWaveformLength(int waveformLength) {
    QStringList lst = QStringList() << "64" << "128" << "256" << "512";
    d->triggerMode->setValue(lst.at(waveformLength));
}

// 状态监测模块
QString AppConfig::ipAddress() const { return d->devIpAddress->value(); }
void AppConfig::setIpAddress(const QString& ip) { d->devIpAddress->setValue(ip); };

int AppConfig::remotePort() const { return d->devRemotePort->value(); }
void AppConfig::setRemotePort(int port) { d->devRemotePort->setValue(port); }

int AppConfig::localPort() const { return d->devLocalPort->value(); }
void AppConfig::setLocalPort(int port){ d->devLocalPort->setValue(port); }

// 数据中心网络
int AppConfig::boardcastPort() const { return d->cmdUdpBroadcastPort->value(); }
void AppConfig::setBoardcastPort(int port) { d->cmdUdpBroadcastPort->setValue(port); }

QString AppConfig::dataSrvIpAddress() const { return d->dataSrvIpAddress->value(); }
void AppConfig::setDataSrvIpAddress(const QString& ip) {d->dataSrvIpAddress->setValue(ip); }

int AppConfig::dataSrvRemotePort() const { return d->dataSrvRemotePort->value(); }
void AppConfig::setDataSrvRemotePort(int port) { d->dataSrvRemotePort->setValue(port); }
