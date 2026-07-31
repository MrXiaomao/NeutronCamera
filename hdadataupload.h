#ifndef HDADATAUPLOAD_H
#define HDADATAUPLOAD_H

#include <QObject>
// 包含头文件
#include "hda_client.h"

class HDADataUpload : public QObject
{
    Q_OBJECT
public:
    explicit HDADataUpload(QObject *parent = nullptr);

    bool connect();
    bool startUpload();
    bool startUploadSpectrumCpsData(quint32 shot/*炮号*/,
                                 const std::string& shotTime/*打靶时刻*/,
                                 quint8 channel/*通道号*/,
                                 const std::vector<double>& time/*时间ms*/,
                                 const std::vector<double>& values/*能谱计数率*/);
    void disconnect();

signals:

public slots:

private:
    // 初始化客户端配置
    hda_client_t client;

};

#endif // HDADATAUPLOAD_H
