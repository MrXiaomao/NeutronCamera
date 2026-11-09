#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

#include "spectypes.h"

#include <QObject>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMutex>
#include <QReadWriteLock>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QColor>
#include <QSettings>
#include <QMap>
#include <QString>

#define SYSTEM_CONFIG_FILE "./Config/system_config.ini" //系统配置文件,保存UI界面等全局配置信息,用户不建议编辑
#define DEVICE_CONFIG_FILE "./Config/device_config.ini" //设备配置文件,保存设备相关的配置信息，如探测器参数等，用户可以编辑
class JsonSettings : public QObject{
    Q_OBJECT
public:
    JsonSettings(const QString &fileName) {
        QFileInfo mConfigurationFile;
        mConfigurationFile.setFile(fileName);
        mFileName = mConfigurationFile.absoluteFilePath();
        mOpened = this->load();

        if (mWatchThisFile){
            // 监视文件内容变化，一旦发现变化重新读取配置文件内容，保持配置信息同步
            mConfigurationFileWatch = new QFileSystemWatcher();
            QFileInfo mConfigurationFile;
            mConfigurationFile.setFile(mFileName);
            if (!mConfigurationFileWatch->files().contains(mConfigurationFile.absoluteFilePath()))
                mConfigurationFileWatch->addPath(mConfigurationFile.absoluteFilePath());

            connect(mConfigurationFileWatch, &QFileSystemWatcher::fileChanged, this, [=](const QString &fileName){
                this->load();
            });

            //mConfigurationFileWatch->addPath(mConfigurationFile.absolutePath());//只需要监视某个文件即可，这里不需要监视整个目录
            // connect(mConfigurationFileWatch, &QFileSystemWatcher::directoryChanged, [=](const QString &path){
            // });
        }
    };
    ~JsonSettings(){
        if (mConfigurationFileWatch)
        {
            delete mConfigurationFileWatch;
            mConfigurationFileWatch = nullptr;
        }

        if (!realtime)
        {
            flush();
        }
    };

    bool isOpen() {
        return this->mOpened;
    }

    QString fileName() const{
        return mFileName;
    }

    bool flush(){
        return save(mFileName);
    };

    /*
        {
            "键key": "值value",
        }
    */
    void setValue(const QString &key, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        mJsonRoot[key] = value.toJsonValue();

        if (realtime)
            flush();
    };
    // 取值
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant())
    {
        QReadLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(key);
        if (iterator != mJsonRoot.end()) {
            return mJsonRoot[key].toVariant();
        }
        else{
            return defaultValue;
        }
    };

    void setValue(QStringList &names, const QString &key, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        setValue(mJsonRoot, names, key, value);
        if (realtime)
            flush();
    };

    QVariant value(QStringList &names, const QString &key, const QVariant &defaultValue = QVariant())
    {
        QReadLocker locker(&mRWLock);
        return value(mJsonRoot, names, key, defaultValue);
    };

    void setValueAt(QStringList &names, const quint8 &index, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        setValueAt(mJsonRoot, names, index, value);

        if (realtime)
            flush();
    };

    QVariant valueAt(QStringList &names, const quint8 &index, const QVariant &defaultValue){
        QReadLocker locker(&mRWLock);
        return valueAt(mJsonRoot, names, index, defaultValue);
    };

    /*
    {
        "arrayName":[
            "键key1": "值value1", //arrayIndex===0
            "键key2": "值value2", //arrayIndex===1
        ]
    }
    */
    void appendArrayValue(const QString &arrayName, const QVariant &value)
    {
        QWriteLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(arrayName);
        if (iterator != mJsonRoot.end())
        {
            QJsonValueRef valueArrayRef = iterator.value();
            if (valueArrayRef.isArray())
            {
                QJsonArray arrayGroup = valueArrayRef.toArray();
                arrayGroup.append(value.toJsonValue());
                valueArrayRef = arrayGroup;

                if (realtime)
                    flush();
            }
        }
        else
        {
            QJsonArray arrayGroup;
            arrayGroup.append(value.toJsonValue());

            mJsonRoot.insert(arrayName, QJsonValue(arrayGroup));

            if (realtime)
                flush();
        }
    };

    /*
    {
        "arrayName":[
            "键key1": "值value1", //arrayIndex===0
            "键key2": "值value2", //arrayIndex===1
        ]
    }
    */
    void setArrayValue(const QString &arrayName, const quint8 &arrayIndex, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(arrayName);
        if (iterator != mJsonRoot.end())
        {
            QJsonValueRef valueArrayRef = iterator.value();
            if (valueArrayRef.isArray())
            {
                QJsonArray arrayGroup = valueArrayRef.toArray();
                if (arrayIndex < arrayGroup.size())
                {
                    arrayGroup.replace(arrayIndex, value.toJsonValue());
                    valueArrayRef = arrayGroup;

                    if (realtime)
                        flush();
                }
            }
        }
    };

    /*
    {
        "groupName":{
            "arrayName":[
                "键key1": "值value1", //arrayIndex===0
                "键key2": "值value2", //arrayIndex===2
            ]
        }
    }
    */

    /*
    {
        "groupName":{
            "arrayName":[
                {
                    "键key1": "值value1", //arrayIndex===0
                    "键key2": "值value2", //arrayIndex===2
                }
            ]
        }
    }
    */
    void setArrayValue(const QString &groupName, const QString &arrayName, const quint8 &arrayIndex, const QString &key, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(groupName);
        if (iterator != mJsonRoot.end())
        {
            QJsonValueRef valueGroupRef = iterator.value();
            if (valueGroupRef.isObject())
            {
                QJsonObject objGroup = valueGroupRef.toObject();
                auto iterator2 = objGroup.find(arrayName);
                if (iterator2 != objGroup.end())
                {
                    QJsonValueRef valueArrayRef = iterator2.value();
                    if (valueArrayRef.isArray())
                    {
                        QJsonArray arrayGroup = valueArrayRef.toArray();
                        if (arrayIndex < arrayGroup.size()){
                            QJsonValueRef valueGroupRef = arrayGroup[arrayIndex];
                            if (valueGroupRef.isObject())
                            {
                                QJsonObject objArray = valueGroupRef.toObject();
                                objArray[key] = value.toJsonValue();

                                valueGroupRef = objArray;
                            }
                            else
                            {
                                // 找到字段，但是类型不对
                                return;
                            }
                        }
                        else
                        {
                            // 数组越界
                            QJsonObject objArray;
                            objArray[key] = value.toJsonValue();

                            arrayGroup.append(objArray);
                        }

                        valueArrayRef = arrayGroup;
                    }
                    else
                    {
                        // 找到字段，但是类型不对
                        return;
                    }
                }
                else
                {
                    QJsonObject objArray;
                    objArray[key] = value.toJsonValue();

                    QJsonArray arrayGroup;
                    arrayGroup.append(objArray);

                    objGroup.insert(arrayName, QJsonValue(arrayGroup));
                }

                valueGroupRef = objGroup;
            }
            else
            {
                // 找到字段，但是类型不对
                return;
            }
        }
        else
        {
            QJsonObject objArray;
            objArray[key] = value.toJsonValue();

            QJsonArray arrayGroup;
            arrayGroup.append(objArray);

            QJsonObject objGroup;
            objGroup.insert(arrayName, QJsonValue(arrayGroup));

            mJsonRoot.insert(groupName, QJsonValue(objGroup));
        }

        if (realtime)
            flush();
    };

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
private:
    bool load(){
        //QReadLocker locker(&mRWLock);
        mJsonRoot = QJsonObject();
        mPrefix.clear();

        QFile file(mFileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray jsonData = file.readAll();
            file.close();

            QJsonParseError error;
            QJsonDocument mJsonDoc = QJsonDocument::fromJson(jsonData, &error);
            if (error.error == QJsonParseError::NoError) {
                if (mJsonDoc.isObject()) {
                    mJsonRoot = mJsonDoc.object();
                    return true;
                } else {
                    qDebug() << "文件[" << mFileName << "]解析失败！";
                    return false;
                }
            } else{
                qDebug() << "文件[" << mFileName << "]解析失败！" << error.errorString().toUtf8().constData();
                return false;
            }
        } else {
            qDebug() << "文件[" << mFileName << "]打开失败！";

            //是否需要创建一个新的文件
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)){
                file.close();
                return true;
            } else {
                return false;
            }
        }
    };

    bool save(const QString &fileName = ""){
        //QWriteLocker locker(&mRWLock);
        QFile file(fileName);
        if (fileName.isEmpty())
            file.setFileName(mFileName);

        if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
            QJsonDocument jsonDoc(mJsonRoot);
            file.write(jsonDoc.toJson());
            file.close();
            return true;
        } else {
            qDebug() << "文件[" << mFileName << "]信息保存失败！";
            return false;
        }
    };

    void setValue(QJsonObject &group, QStringList &names, const QString &key, const QVariant &value){
        // 取首名称
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                setValue(childGroup, names, key, value);
                valueGroupRef = childGroup;
            }
            else {
                QJsonObject childGroup;
                group.insert(name, QJsonValue(childGroup));
                setValue(childGroup, names, key, value);
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject targetGroup = valueGroupRef.toObject();
                targetGroup[key] = value.toJsonValue();
                valueGroupRef = targetGroup;
            }
            else {
                QJsonObject targetGroup;
                targetGroup[key] = value.toJsonValue();
                group.insert(name, QJsonValue(targetGroup));
            }
        }
    };

    QVariant value(QJsonObject &group, QStringList &names, const QString &key, const QVariant &defaultValue = QVariant())
    {
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                return value(childGroup, names, key, defaultValue);
            }
            else {
                return defaultValue;
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject targetGroup = valueGroupRef.toObject();
                return targetGroup[key].toVariant();
            }
            else {
                return defaultValue;
            }
        }
    };

    void setValueAt(QJsonObject &group, QStringList &names, const quint8 &index, const QVariant &value){
        // 取首名称
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                setValueAt(childGroup, names, index, value);
                valueGroupRef = childGroup;
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end())
            {
                QJsonValueRef valueArrayRef = iterator.value();
                if (valueArrayRef.isArray())
                {
                    QJsonArray arrayGroup = valueArrayRef.toArray();
                    if (index < arrayGroup.size())
                    {
                        arrayGroup.replace(index, value.toJsonValue());
                        valueArrayRef = arrayGroup;
                    }
                    else{
                        qDebug() << "Index out of range. " << index;
                    }
                }
                else {
                    qDebug() << "Don't find group " << name;
                }
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        }
    };

    QVariant valueAt(QJsonObject &group, QStringList &names, const quint8 &index, const QVariant &defaultValue){
        // 取首名称
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                return valueAt(childGroup, names, index, defaultValue);
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end())
            {
                QJsonValueRef valueArrayRef = iterator.value();
                if (valueArrayRef.isArray())
                {
                    QJsonArray targetGroup = valueArrayRef.toArray();
                    if (index < targetGroup.size())
                    {
                        return targetGroup[index].toVariant();
                    }
                    else{
                        qDebug() << "Index out of range. " << index;
                    }
                }
                else {
                    qDebug() << "Don't find group " << name;
                }
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        }
    };

protected:
    QString mFileName;
    QString mPrefix;
    QJsonObject mJsonRoot;
    QReadWriteLock mRWLock;
    QMutex mAccessMutex;//访问锁
    bool mOpened = false;//文档打开成功标识
    bool realtime = true;
    bool mWatchThisFile = false;
    QFileSystemWatcher *mConfigurationFileWatch;
};

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
    
    void setIntVector(const QString& key, const QVector<int>& vec);
    void setDoubleVector(const QString& key, const QVector<double>& vec);
    QVector<int> GetIntVector(const QString& key,
                                 const QVector<int>& def = {}) const;
    QVector<double> GetDoubleVector(const QString& key,
                                 const QVector<double>& def = {}) const;

    bool isDarkTheme(){return mIsDarkTheme;};
    bool enableThemeColor(){return mThemeColorEnable;};
    QColor colorTheme(){return mThemeColor;};

private:
    bool realtime = false;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
};

#endif // GLOBALSETTINGS_H
