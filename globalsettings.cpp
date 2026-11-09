#include "globalsettings.h"
#include <QFileInfo>
#include <QApplication>
#include <QTextCodec>
#include <QMessageBox>
#include <QDateTime>
#include <QThread>
#include <QDebug>
#include <QDir>

namespace {

/** 内存中只保留通道 1..DET_NUM，去掉 id=0 等非法键，并强制每路 id 与键一致。 */
inline void normalizeDetParameterMap(QMap<quint8, DetParameter*>& map)
{
    map.remove(0);
    for (quint8 k : map.keys()) {
        if (k > DET_NUM)
            map.remove(k);
    }
    for (int i = 1; i <= DET_NUM; ++i) {
        const quint8 ki = static_cast<quint8>(i);
        if (!map.contains(ki)) {
            DetParameter *d = new DetParameter;
            d->id = ki;
            map[ki] = d;
        } else {
            map[ki]->id = ki;
        }
    }
}

/** 写入 HDF5 Configuration：固定 DET_NUM 行，行序与 id 一一对应为 1..DET_NUM。 */
QVector<DetParameter*> configurationRowsForWrite(const QMap<quint8, DetParameter*>& map)
{
    QVector<DetParameter*> data;
    data.reserve(DET_NUM);
    for (int i = 1; i <= DET_NUM; ++i) {
        const quint8 ki = static_cast<quint8>(i);
        if (!map.contains(ki)) {
            DetParameter *d = new DetParameter;
            d->id = ki;
            data.push_back(d);
        } else {
            map[ki]->id = ki;
            data.push_back(map[ki]);
        }
    }
    return data;
}

} // namespace

H5Spectrum FullSpectoH5Spec(const FullSpectrum& src)
{
    H5Spectrum h5Spec{};
    h5Spec.sequence    = src.sequence;
    h5Spec.timeUTCs   = src.timeUTCs;
    h5Spec.timeUTCms   = src.timeUTCms;
    h5Spec.measureTime = src.measureTime;
    h5Spec.deathTime   = src.deathTime;

    std::memcpy(h5Spec.spectrum,
                src.spectrum,
                sizeof(h5Spec.spectrum));

    return h5Spec;
}

/*#########################################################*/
GlobalSettings::GlobalSettings(QObject *parent)
    : QSettings(SYSTEM_CONFIG_FILE, QSettings::IniFormat, parent)
{
    this->setIniCodec(QTextCodec::codecForName("utf-8"));
}

GlobalSettings::GlobalSettings(QString filePath, QObject *parent)
    : QSettings(filePath, QSettings::IniFormat, parent)
{
    this->setIniCodec(QTextCodec::codecForName("utf-8"));
}

GlobalSettings::~GlobalSettings() {

}

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
void GlobalSettings::setValue(QAnyStringView key, const QVariant &value)
#else
void GlobalSettings::setValue(const QString &key, const QVariant &value)
#endif
{
    QSettings::setValue(key, value);
    if(realtime)
        sync();
}

void GlobalSettings::setIntVector(const QString& key, const QVector<int>& vec)
{
    QStringList sl;
    sl.reserve(vec.size());
    for (int v : vec)
        sl << QString::number(v);
    this->setValue(key, sl);
}

QVector<int> GlobalSettings::GetIntVector(const QString& key,
                                            const QVector<int>& def) const
{
    const QVariant v = value(key);
    if (!v.isValid())
        return def;
}

void GlobalSettings::setDoubleVector(const QString& key, const QVector<double>& vec)
{
    QStringList sl;
    sl.reserve(vec.size());

    // 'g', 17：尽量保证 double 往返不丢精度
    for (double v : vec)
        sl << QString::number(v, 'g', 17);

    this->setValue(key, sl);
    // 如果你们希望立刻落盘，可选sync()：
    if(realtime)
        sync();
}

// 从配置读取 double 向量
// eg：QVector<double> loaded = settings.doubleVector("Global/Keys");
QVector<double> GlobalSettings::GetDoubleVector(const QString& key,
                                            const QVector<double>& def) const
{
    const QVariant v = value(key);
    if (!v.isValid())
        return def;

    // Qt/ini 里通常会以 QStringList 形式读回
    const QStringList sl = v.toStringList();
    if (sl.isEmpty())
        return QVector<double>{}; // 或 def，看你语义

    QVector<double> vec;
    vec.reserve(sl.size());
    for (const QString& s : sl)
        vec << s.toDouble();

    return vec;
}

void GlobalSettings::setRealtimeSave(bool realtime)
{
    this->realtime = realtime;
}
