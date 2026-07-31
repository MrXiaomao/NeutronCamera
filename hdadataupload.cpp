#include "hdadataupload.h"
#include "hda_client.h"
#include "AppConfig.h"
#include <QDebug>

HDADataUpload::HDADataUpload(QObject *parent)
    : QObject{parent}
{}

bool HDADataUpload::connect()
{
    QString ip_HDASrv = AppConfig::instance().dataSrvIpAddress();
    qint32 port_HDZSrv = AppConfig::instance().dataSrvRemotePort();
    QString srvAddrStr = QString("%1:%2").arg(ip_HDASrv).arg(port_HDZSrv);

    hda_client_config_t cfg;
    cfg.server_address = "172.30.20.11:9090";//srvAddrStr.toStdString().c_str();

    // 连接到服务器
    hda_status_t status = hda_client_connect(&cfg, &client);
    if (status != HDA_OK) {
        printf("Failed to connect: %d\n", status);
        return false;
    }

    return true;
}

void HDADataUpload::disconnect()
{
    // 断开连接，释放所有资源
    hda_client_disconnect(client);
}

bool HDADataUpload::startUpload()
{
    // 写入数据
    {
        // 设置写入键
        hda_write_key_t key;
        key.device = "HL-3";
        key.shot = 12345;
        key.subsystem = "test_subsystem";

        // 打开写入器
        hda_writer_t writer;
        hda_status_t status = hda_open_writer(client, &key, &writer);
        if (status != HDA_OK) {
            qDebug() << "Failed to open writer: " << status;
            hda_client_disconnect(client);
            return false;
        }

        // 准备1D数据
        std::vector<double> time = {0.0, 1.0, 2.0, 3.0, 4.0};
        std::vector<float> values = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};

        // 设置通道属性
        hda_channel_attr_t attrs;
        attrs.DEVICE = "HL-3";
        attrs.SUBSYSTEM = "test_subsystem";
        attrs.CONTACT_INFO = "";
        attrs.DAQ_DTYPE = "PROCD";
        attrs.DAQ_RAW_BITS = 16;
        attrs.DAQ_VALIDITY = 1;
        attrs.DATA_TYPE = HDA_DT_FLOAT32;
        attrs.DESCRIPTION = "Voltage measurement channel";
        attrs.EBAR_EXIST = 0;
        attrs.ENTRY_TIME = "2026-01-31 10:00:00";
        attrs.PHI_DIM = -1;
        attrs.R_DIM = -1;
        attrs.T_COORD_FREQ = 1000.0f;
        attrs.T_COORD_START = 0.0f;
        attrs.T_DIM = 5;
        attrs.T_TYPE = "US";
        attrs.T_UNIT = "s";
        attrs.VERSION = 1;
        attrs.V_UNIT = "V";
        attrs.Z_DIM = -1;

        // 准备写入请求
        hda_write_1d_req_t req;
        req.channel = "test_channel_1d";
        req.dtype = HDA_DT_FLOAT32;
        req.count = values.size();
        req.pTime = time.data();
        req.pValue = values.data();
        req.attrs = &attrs;

        // 写入数据
        status = hda_write_1d(writer, &req);
        if (status != HDA_OK) {
            qDebug() << "Failed to write 1D data: " << status;
            hda_close_writer(writer, 5000);
            hda_client_disconnect(client);
            return false;
        }

        // 关闭写入器
        status = hda_close_writer(writer, 5000);
        if (status != HDA_OK) {
            qDebug() << "Failed to close writer: " << status;
            hda_client_disconnect(client);
            return false;
        }

        return true;
    }
}

bool HDADataUpload::startUploadSpectrumCpsData(quint32 shot/*炮号*/,
                                            const std::string& shotTime/*打靶时刻*/,
                                            quint8 channel/*通道号*/,
                                            const std::vector<double>& time/*时间*/,
                                            const std::vector<double>& values/*能谱计数率*/)
{
    // 写入数据
    {
        // 设置写入键
        hda_write_key_t key;
        key.device = "HL-3";
        key.shot = shot;
        key.subsystem = "test_subsystem";

        // 打开写入器
        hda_writer_t writer;
        hda_status_t status = hda_open_writer(client, &key, &writer);
        if (status != HDA_OK) {
            qDebug() << "Failed to open writer: " << status;
            hda_client_disconnect(client);
            return false;
        }

        // 设置通道属性
        hda_channel_attr_t attrs;
        attrs.DEVICE = "HL-3";
        attrs.SUBSYSTEM = "test_subsystem";
        attrs.CONTACT_INFO = "";
        attrs.DAQ_DTYPE = "PROCD";
        attrs.DAQ_RAW_BITS = 16;
        attrs.DAQ_VALIDITY = 1;
        attrs.DATA_TYPE = HDA_DT_RAW_U32;//HDA_DT_FLOAT32;
        attrs.DESCRIPTION = "Voltage measurement channel";
        attrs.EBAR_EXIST = 0;
        attrs.ENTRY_TIME = shotTime.c_str();// "2026-01-31 10:00:00";
        attrs.PHI_DIM = -1;
        attrs.R_DIM = -1;
        attrs.T_COORD_FREQ = 1000.0f;
        attrs.T_COORD_START = 0.0f;
        attrs.T_DIM = 5;
        attrs.T_TYPE = "US";
        attrs.T_UNIT = "ms";
        attrs.VERSION = 1;
        attrs.V_UNIT = "keV";
        attrs.Z_DIM = -1;

        // 准备写入请求
        hda_write_1d_req_t req;
        req.channel = std::to_string(channel).c_str();//"test_channel_1d";
        req.dtype = HDA_DT_RAW_U32;//HDA_DT_FLOAT32;
        req.count = values.size();
        req.pTime = time.data();
        req.pValue = values.data();
        req.attrs = &attrs;

        // 写入数据
        status = hda_write_1d(writer, &req);
        if (status != HDA_OK) {
            qDebug() << "Failed to write 1D data: " << status;
            hda_close_writer(writer, 5000);
            hda_client_disconnect(client);
            return false;
        }

        // 关闭写入器
        status = hda_close_writer(writer, 5000);
        if (status != HDA_OK) {
            qDebug() << "Failed to close writer: " << status;
            hda_client_disconnect(client);
            return false;
        }

        return true;
    }
}
