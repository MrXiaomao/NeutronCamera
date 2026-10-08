#ifndef QSNMPCLIENT_H
#define QSNMPCLIENT_H

#include <QCoreApplication>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QDebug>
#include <QTimer>
#include <QHostAddress>

enum SnmpDataType {
    SnmpTypeInteger = 0x02,
    SnmpTypeOctetString = 0x04,
    SnmpTypeNull = 0x05,
    SnmpTypeObjectIdentifier = 0x06,
    SnmpTypeSequence = 0x30,
    SnmpGetRequest = 0xA0,
    SnmpSetRequest = 0xA3
};

// 只读OID规则配置
struct ReadOidRule {
    QString oidPrefix;
    QString desc;
    QMap<int, QString> enumMap;
    int invalidValue;
};

// 可写OID规则配置（新增）
struct WriteOidRule {
    QString oidPrefix;
    QString desc;
    // 合法值域，空代表不做范围限制
    QList<int> allowedValues;
    // 缩放系数：用于温湿度这类带0.1单位的换算，输入实际温度/湿度自动乘系数转为SNMP整数值
    int scaleFactor;
    int invalidValue;
};

// 通用读返回结果
struct UniversalSnmpReadResult {
    int index;
    int rawIntValue;
    QString strValue;
    QString desc;
    bool isValid;
};

// 通用写返回结果（新增）
struct UniversalSnmpWriteResult {
    int index;
    int setValue; // 实际写入设备的整数值
    bool writeSuccess;
    QString msg;
};

class QSnmpClient : public QObject
{
    Q_OBJECT
public:
    explicit QSnmpClient(QObject *parent = nullptr) : QObject(parent) {
        m_udpSocket = new QUdpSocket(this);
        //disconnect(m_udpSocket, &QUdpSocket::readyRead, this, &QSnmpClient::onUniversalResponse);
        connect(m_udpSocket, &QUdpSocket::readyRead, this, &QSnmpClient::onUniversalResponse);
        initAllOidRules();
    }

    // 通用读取接口（保留之前的能力）
    void readOid(const QString &host, const QString &community, const QString &fullOid, qint16& outRespRequestId) {
        m_community = community;
        m_requestHost = QHostAddress(host);
        m_targetPort = 161;
        m_operationType = 0; // 0=读操作
        m_isFinished = false;

        QByteArray getPacket = buildSnmpGetPacket(fullOid, outRespRequestId);
        m_udpSocket->writeDatagram(getPacket, m_requestHost, m_targetPort);

        QTimer::singleShot(5000, this, [this](){
            if(!m_isFinished) {
                UniversalSnmpReadResult res;
                res.isValid = false;
                res.desc = "读取超时";
                emit readFinished(res);
            }
        });
    }

    /**
     * @brief 通用写入接口，适配所有可写OID
     * @param host 设备IP
     * @param writeCommunity 可写共同体名
     * @param fullOid 要写入的完整OID
     * @param userInputValue 用户输入的实际值（动作编号/实际温度/湿度等）
     */
    void writeOid(const QString &host, const QString &writeCommunity, const QString &fullOid, qint16 userInputValue, qint16& outRespRequestId) {
        m_community = writeCommunity;
        m_requestHost = QHostAddress(host);
        m_targetPort = 161;
        m_operationType = 1; // 1=写操作
        m_isFinished = false;

        // 先在本地做参数合法性校验，不合法直接返回失败
        qint16 targetSetValue = -1;
        bool ruleFound = false;
        for(const auto &rule : m_writeRuleList) {
            if(fullOid.startsWith(rule.oidPrefix)) {
                ruleFound = true;
                // 带缩放系数的单位换算
                qint16 convertedValue = userInputValue * rule.scaleFactor;
                // 校验合法值域
                if(!rule.allowedValues.isEmpty() && !rule.allowedValues.contains(convertedValue)) {
                    UniversalSnmpWriteResult res;
                    res.index = fullOid.split('.').last().toInt();
                    res.setValue = convertedValue;
                    res.writeSuccess = false;
                    res.msg = QString("写入值%1不在合法值域内").arg(userInputValue);
                    emit writeFinished(res);
                    return;
                }
                targetSetValue = convertedValue;
                break;
            }
        }

        if(!ruleFound) {
            UniversalSnmpWriteResult res;
            res.index = fullOid.split('.').last().toInt();
            res.setValue = userInputValue;
            res.writeSuccess = false;
            res.msg = "找不到该OID的写入规则";
            emit writeFinished(res);
            return;
        }

        // 构造SNMP Set请求发送
        QByteArray setPacket = buildSnmpSetPacket(fullOid, targetSetValue, outRespRequestId);
        m_udpSocket->writeDatagram(setPacket, m_requestHost, m_targetPort);

        QTimer::singleShot(5000, this, [this, fullOid, userInputValue, targetSetValue](){
            if(!m_isFinished) {
                UniversalSnmpWriteResult res;
                res.index = fullOid.split('.').last().toInt();
                res.setValue = targetSetValue;
                res.writeSuccess = false;
                res.msg = "写入操作超时";
                emit writeFinished(res);
            }
        });
    }

signals:
    void readFinished(const UniversalSnmpReadResult &result);
    void writeFinished(const UniversalSnmpWriteResult &result);

private slots:
    void onUniversalResponse() {
        while(m_udpSocket->hasPendingDatagrams()) {
            QNetworkDatagram datagram = m_udpSocket->receiveDatagram();
            m_isFinished = true;
            if(m_operationType == 0) {
                // 处理读响应
                UniversalSnmpReadResult res = parseReadPacket(datagram.data());
                emit readFinished(res);
            } else {
                // 处理写响应
                UniversalSnmpWriteResult res = parseWritePacket(datagram.data());
                emit writeFinished(res);
            }
        }
    }

private:
    // 注册所有读写OID规则
    void initAllOidRules() {
        // ========== 只读OID规则 ==========
        // 电压告警状态
        ReadOidRule voltageRule;
        voltageRule.oidPrefix = "1.3.6.1.4.1.23280.6.1.10.";
        voltageRule.desc = "电压告警状态";
        voltageRule.enumMap = {{1,"正常"}, {2,"超越上限"}, {3,"超越下限"}};
        voltageRule.invalidValue = -1;
        m_readRuleList.append(voltageRule);

        // 电流告警状态
        ReadOidRule currentRule;
        currentRule.oidPrefix = "1.3.6.1.4.1.23280.6.1.11.";
        currentRule.desc = "电流告警状态";
        currentRule.enumMap = {{1,"正常"}, {2,"超越上限"}, {3,"超越下限"}};
        currentRule.invalidValue = -1;
        m_readRuleList.append(currentRule);

        // 继电器状态
        ReadOidRule relayStatusRule;
        relayStatusRule.oidPrefix = "1.3.6.1.4.1.23280.8.1.2.";
        relayStatusRule.desc = "继电器状态";
        relayStatusRule.enumMap = {{1,"关闭"}, {2,"打开"}};
        relayStatusRule.invalidValue = -1;
        m_readRuleList.append(relayStatusRule);

        // ========== 可写OID规则 完全对齐你给出的6类 ==========
        // 10 继电器动作
        WriteOidRule relayActionRule;
        relayActionRule.oidPrefix = "1.3.6.1.4.1.23280.9.1.2.";
        relayActionRule.desc = "继电器动作";
        relayActionRule.allowedValues = {1, 2, 3};
        relayActionRule.scaleFactor = 1;
        relayActionRule.invalidValue = -1;
        m_writeRuleList.append(relayActionRule);

        // 11 继电器电能清空
        WriteOidRule clearEnergyRule;
        clearEnergyRule.oidPrefix = "1.3.6.1.4.1.23280.9.1.3.";
        clearEnergyRule.desc = "继电器电能清空";
        clearEnergyRule.allowedValues = {1};
        clearEnergyRule.scaleFactor = 1;
        clearEnergyRule.invalidValue = -1;
        m_writeRuleList.append(clearEnergyRule);

        // 12 温度上限告警设置 0.1℃ 输入25代表2.5℃
        WriteOidRule tempHighRule;
        tempHighRule.oidPrefix = "1.3.6.1.4.1.23280.11.1.2.";
        tempHighRule.desc = "温度上限告警设置";
        tempHighRule.scaleFactor = 10; // 自动乘10，完成0.1℃单位换算
        tempHighRule.allowedValues = {}; // 不限制值域，由设备端校验
        tempHighRule.invalidValue = -1;
        m_writeRuleList.append(tempHighRule);

        // 13 温度下限告警设置
        WriteOidRule tempLowRule;
        tempLowRule.oidPrefix = "1.3.6.1.4.1.23280.11.1.3.";
        tempLowRule.desc = "温度下限告警设置";
        tempLowRule.scaleFactor = 10;
        tempLowRule.allowedValues = {};
        tempLowRule.invalidValue = -1;
        m_writeRuleList.append(tempLowRule);

        // 14 湿度上限告警设置 %0.1RH
        WriteOidRule humHighRule;
        humHighRule.oidPrefix = "1.3.6.1.4.1.23280.11.1.4.";
        humHighRule.desc = "湿度上限告警设置";
        humHighRule.scaleFactor = 10;
        humHighRule.allowedValues = {};
        humHighRule.invalidValue = -1;
        m_writeRuleList.append(humHighRule);

        // 15 湿度下限告警设置
        WriteOidRule humLowRule;
        humLowRule.oidPrefix = "1.3.6.1.4.1.23280.11.1.5.";
        humLowRule.desc = "湿度下限告警设置";
        humLowRule.scaleFactor = 10;
        humLowRule.allowedValues = {};
        humLowRule.invalidValue = -1;
        m_writeRuleList.append(humLowRule);
    }

    QByteArray encodeOid(const QString &oidStr) {
        QStringList parts = oidStr.split('.');
        QList<qint64> oidNumbers;
        for(const auto &part : parts) oidNumbers.append(part.toLongLong());

        QByteArray encoded;
        encoded.append(static_cast<char>(oidNumbers[0] * 40 + oidNumbers[1]));
        for(int i = 2; i < oidNumbers.size(); i++) {
            qint64 num = oidNumbers[i];
            QByteArray bytes;
            if(num < 0x80) {
                bytes.append(static_cast<char>(num));
            } else {
                while(num > 0) {
                    bytes.prepend(static_cast<char>(num & 0x7F));
                    num >>= 7;
                }
                for(int j = 0; j < bytes.size() - 1; j++)
                    bytes[j] = bytes[j] | 0x80;
            }
            encoded.append(bytes);
        }
        return encoded;
    }

    QByteArray buildSnmpGetPacket(const QString &oid, qint16& outRespRequestId) {
        QByteArray oidEncoded = encodeOid(oid);
        QByteArray communityEncoded;
        communityEncoded.append(static_cast<char>(SnmpTypeOctetString));
        communityEncoded.append(static_cast<char>(m_community.size()));
        communityEncoded.append(m_community.toLatin1());

        QByteArray oidPart;
        oidPart.append(static_cast<char>(SnmpTypeObjectIdentifier));
        oidPart.append(static_cast<char>(oidEncoded.size()));
        oidPart.append(oidEncoded);

        QByteArray varBind;
        varBind.append(static_cast<char>(SnmpTypeSequence));
        varBind.append(static_cast<char>(oidPart.size() + 2));
        varBind.append(oidPart);
        varBind.append(static_cast<char>(SnmpTypeNull));
        varBind.append((quint8)0x00);

        QByteArray varBindList;
        varBindList.append(static_cast<char>(SnmpTypeSequence));
        varBindList.append(static_cast<char>(varBind.size()));
        varBindList.append(varBind);

        qint32 requestId = outRespRequestId;//qrand();
        QByteArray requestIdBytes;
        requestIdBytes.append(static_cast<char>(SnmpTypeInteger));
        requestIdBytes.append(0x04);
        requestIdBytes.append((requestId >> 24) & 0xFF);
        requestIdBytes.append((requestId >> 16) & 0xFF);
        requestIdBytes.append((requestId >> 8) & 0xFF);
        requestIdBytes.append(requestId & 0xFF);

        QByteArray errorStatus;
        errorStatus.append(static_cast<char>(SnmpTypeInteger));
        errorStatus.append(0x01);
        errorStatus.append((quint8)0x00);

        QByteArray errorIndex;
        errorIndex.append(static_cast<char>(SnmpTypeInteger));
        errorIndex.append(0x01);
        errorIndex.append((quint8)0x00);

        QByteArray pdu;
        pdu.append(static_cast<char>(SnmpGetRequest));
        pdu.append(static_cast<char>(requestIdBytes.size() + errorStatus.size() + errorIndex.size() + varBindList.size()));
        pdu.append(requestIdBytes);
        pdu.append(errorStatus);
        pdu.append(errorIndex);
        pdu.append(varBindList);

        QByteArray fullPacket;
        fullPacket.append(static_cast<char>(SnmpTypeInteger));
        fullPacket.append(0x01);
        fullPacket.append((quint8)0x00);
        fullPacket.append(communityEncoded);
        fullPacket.append(pdu);

        QByteArray finalPacket;
        finalPacket.append(static_cast<char>(SnmpTypeSequence));
        finalPacket.append(static_cast<char>(fullPacket.size()));
        finalPacket.append(fullPacket);
        return finalPacket;
    }

    // 通用SNMP Set请求构造
    QByteArray buildSnmpSetPacket(const QString &oid, qint16 setValue, qint16 &outRequestId) {
        QByteArray oidEncoded = encodeOid(oid);
        QByteArray communityEncoded;
        communityEncoded.append(static_cast<char>(SnmpTypeOctetString));
        communityEncoded.append(static_cast<char>(m_community.size()));
        communityEncoded.append(m_community.toLatin1());

        QByteArray oidPart;
        oidPart.append(static_cast<char>(SnmpTypeObjectIdentifier));
        oidPart.append(static_cast<char>(oidEncoded.size()));
        oidPart.append(oidEncoded);

        // 把要写入的整数值编码为BER Integer格式
        QByteArray valuePart;
        if(setValue < 0x80) {
            valuePart.append(static_cast<char>(SnmpTypeInteger));
            valuePart.append(0x01);
            valuePart.append(static_cast<char>(setValue));
        } else if(setValue < 0x8000) {
            valuePart.append(static_cast<char>(SnmpTypeInteger));
            valuePart.append(0x02);
            valuePart.append((setValue >> 8) & 0xFF);
            valuePart.append(setValue & 0xFF);
        }

        QByteArray varBind;
        varBind.append(static_cast<char>(SnmpTypeSequence));
        varBind.append(static_cast<char>(oidPart.size() + valuePart.size()));
        varBind.append(oidPart);
        varBind.append(valuePart);

        QByteArray varBindList;
        varBindList.append(static_cast<char>(SnmpTypeSequence));
        varBindList.append(static_cast<char>(varBind.size()));
        varBindList.append(varBind);

        quint32 mergedId = (static_cast<quint32>(setValue) << 16) | outRequestId;
        qint32 requestId = static_cast<qint32>(mergedId);//qrand();
        QByteArray requestIdBytes;
        requestIdBytes.append(static_cast<char>(SnmpTypeInteger));
        requestIdBytes.append(0x04);
        requestIdBytes.append((requestId >> 24) & 0xFF);
        requestIdBytes.append((requestId >> 16) & 0xFF);
        requestIdBytes.append((requestId >> 8) & 0xFF);
        requestIdBytes.append(requestId & 0xFF);

        QByteArray errorStatus;
        errorStatus.append(static_cast<char>(SnmpTypeInteger));
        errorStatus.append(0x01);
        errorStatus.append((quint8)0x00);

        QByteArray errorIndex;
        errorIndex.append(static_cast<char>(SnmpTypeInteger));
        errorIndex.append(0x01);
        errorIndex.append((quint8)0x00);

        QByteArray pdu;
        pdu.append(static_cast<char>(SnmpSetRequest));
        pdu.append(static_cast<char>(requestIdBytes.size() + errorStatus.size() + errorIndex.size() + varBindList.size()));
        pdu.append(requestIdBytes);
        pdu.append(errorStatus);
        pdu.append(errorIndex);
        pdu.append(varBindList);

        QByteArray fullPacket;
        fullPacket.append(static_cast<char>(SnmpTypeInteger));
        fullPacket.append(0x01);
        fullPacket.append((quint8)0x00);
        fullPacket.append(communityEncoded);
        fullPacket.append(pdu);

        QByteArray finalPacket;
        finalPacket.append(static_cast<char>(SnmpTypeSequence));
        finalPacket.append(static_cast<char>(fullPacket.size()));
        finalPacket.append(fullPacket);
        return finalPacket;
    }

    UniversalSnmpReadResult parseReadPacket(const QByteArray &packet) {
        UniversalSnmpReadResult res;
        res.isValid = false;
        res.rawIntValue = -1;

        // 最小合法报文长度校验
        if(packet.size() < 10) {
            res.desc = "响应报文长度过短，无效";
            return res;
        }

        int offset = 0;
        // 外层Sequence头，取后续总长度（兼容大于127字节的长报文）
        uchar seqType = static_cast<uchar>(packet[offset++]);
        uchar seqLen = static_cast<uchar>(packet[offset++]);
        if(seqLen & 0x80) {
            int lenBytes = seqLen & 0x7F;
            offset += lenBytes;
        }

        // 解析版本号
        uchar verType = static_cast<uchar>(packet[offset++]);
        uchar verLen = static_cast<uchar>(packet[offset++]);
        offset += verLen;

        // 动态解析Community字符串，硬编码写死长度的方式在这里全部修正
        uchar commType = static_cast<uchar>(packet[offset++]);
        uchar commLen = static_cast<uchar>(packet[offset++]);
        offset += commLen;

        // 动态计算PDU段起始位置，跳过PDU头
        uchar pduType = static_cast<uchar>(packet[offset++]);
        uchar pduLen = static_cast<uchar>(packet[offset++]);
        if(pduLen & 0x80) {
            int lenBytes = pduLen & 0x7F;
            offset += lenBytes;
        }

        // 动态解析Request ID，不再硬编码为4字节
        uchar reqIdType = static_cast<uchar>(packet[offset++]);
        uchar reqIdLen = static_cast<uchar>(packet[offset++]);
        qint32 outRespRequestId = 0;
        for(int i=0; i<reqIdLen; i++) {
            outRespRequestId = (outRespRequestId << 8) | static_cast<uchar>(packet[offset + i]);
        }
        offset += reqIdLen;

        // 解析错误状态
        uchar errStatusType = static_cast<uchar>(packet[offset++]);
        uchar errStatusLen = static_cast<uchar>(packet[offset++]);
        uchar errorSta = static_cast<uchar>(packet[offset]);
        offset += errStatusLen;

        // 解析错误索引
        uchar errIdxType = static_cast<uchar>(packet[offset++]);
        uchar errIdxLen = static_cast<uchar>(packet[offset++]);
        offset += errIdxLen;

        if(errorSta != 0) {
            res.desc = QString("Error code:%1").arg(errorSta);
            return res;
        }

        // 动态跳过VarBind List头
        uchar varBindListType = static_cast<uchar>(packet[offset++]);
        uchar varBindListLen = static_cast<uchar>(packet[offset++]);

        // 动态跳过单条VarBind Sequence头
        uchar singleVarBindType = static_cast<uchar>(packet[offset++]);
        uchar singleVarBindLen = static_cast<uchar>(packet[offset++]);

        // 动态跳过返回的OID段
        uchar respOidType = static_cast<uchar>(packet[offset++]);
        uchar respOidLen = static_cast<uchar>(packet[offset++]);
        offset += respOidLen;

        // 解析返回值，兼容多字节、带符号位的整数
        uchar valueType = static_cast<uchar>(packet[offset++]);
        uchar valueLen = static_cast<uchar>(packet[offset++]);

        if(valueType == SnmpTypeInteger) {
            // 完全兼容带符号位的变长整数解码
            if(valueLen == 0) return res;
            qint32 intVal = 0;
            // 处理符号位
            if(static_cast<uchar>(packet[offset]) & 0x80) {
                intVal = -1;
            }
            for(int i=0; i<valueLen; i++) {
                intVal = (intVal << 8) | static_cast<uchar>(packet[offset + i]);
            }
            res.rawIntValue = intVal;
        } else if(valueType == SnmpTypeOctetString) {
            res.strValue = QString::fromLatin1(packet.constData() + offset, valueLen);
        } else {
            res.desc = "不支持的返回数据类型";
            return res;
        }

        res.isValid = true;
        res.index = outRespRequestId;
        return res;
    }

    UniversalSnmpWriteResult parseWritePacket(const QByteArray &packet) {
        UniversalSnmpWriteResult res;
        res.writeSuccess = false;
        res.msg = "写入失败";

        // 最小合法SNMP响应报文长度校验
        if(packet.size() < 10) {
            res.msg = "写响应报文长度非法";
            return res;
        }

        int offset = 0;
        // 1. 跳过外层Sequence头，兼容长度大于127的长报文
        uchar seqType = static_cast<uchar>(packet[offset++]);
        uchar seqLen = static_cast<uchar>(packet[offset++]);
        if(seqLen & 0x80) {
            int extraLenBytes = seqLen & 0x7F;
            offset += extraLenBytes;
        }

        // 2. 动态解析版本号，自动跳过变长段
        uchar verType = static_cast<uchar>(packet[offset++]);
        uchar verLen = static_cast<uchar>(packet[offset++]);
        offset += verLen;

        // 3. 动态解析Community共同体字符串，不再硬编码长度
        uchar commType = static_cast<uchar>(packet[offset++]);
        uchar commLen = static_cast<uchar>(packet[offset++]);
        offset += commLen;

        // 4. 跳过SNMP PDU头，兼容变长PDU
        uchar pduType = static_cast<uchar>(packet[offset++]);
        uchar pduLen = static_cast<uchar>(packet[offset++]);
        if(pduLen & 0x80) {
            int extraLenBytes = pduLen & 0x7F;
            offset += extraLenBytes;
        }

        // 5. 动态解析Request ID，兼容设备自定义的2字节/4字节变长ID
        uchar reqIdType = static_cast<uchar>(packet[offset++]);
        uchar reqIdLen = static_cast<uchar>(packet[offset++]);
        qint32 outRespRequestId = 0;
        for(int i=0; i<reqIdLen; i++) {
            outRespRequestId = (outRespRequestId << 8) | static_cast<uchar>(packet[offset + i]);
        }
        offset += reqIdLen;

        // 6. 读取核心的错误状态码，这是SNMP Set操作成功的唯一判断依据
        uchar errStatusType = static_cast<uchar>(packet[offset++]);
        uchar errStatusLen = static_cast<uchar>(packet[offset++]);
        uchar errorSta = static_cast<uchar>(packet[offset]);
        offset += errStatusLen;

        // 7. 跳过错误索引段
        uchar errIdxType = static_cast<uchar>(packet[offset++]);
        uchar errIdxLen = static_cast<uchar>(packet[offset++]);
        offset += errIdxLen;

        // 8. 校验操作结果，错误码为0代表写入完全成功
        if(errorSta == 0) {
            quint16 extractedIndex = outRespRequestId & 0xFFFF;          // 取低16位得到index
            quint16 extractedValue = (outRespRequestId >> 16) & 0xFFFF; // 取高16位得到setValue
            res.index = extractedIndex;
            res.setValue = extractedValue;
            res.writeSuccess = true;
            res.msg = "写入操作成功，设备已确认接收";
        } else {
            // 自动映射标准SNMP错误码，直接返回可读错误信息
            switch(errorSta) {
            case 1: res.msg = "写入失败：设备返回tooBig"; break;
            case 2: res.msg = "写入失败：设备返回noSuchName，OID不存在"; break;
            case 3: res.msg = "写入失败：设备返回badValue，写入值非法"; break;
            case 4: res.msg = "写入失败：设备返回readOnly，该OID不可写"; break;
            case 5: res.msg = "写入失败：设备返回genericError"; break;
            default: res.msg = QString("写入失败，设备返回未知错误码:%1").arg(errorSta); break;
            }
        }
        return res;
    }

    QUdpSocket *m_udpSocket;
    QString m_community;
    QHostAddress m_requestHost;
    quint16 m_targetPort;
    bool m_isFinished = false;
    int m_operationType = 0; // 0=读 1=写
    QList<ReadOidRule> m_readRuleList;
    QList<WriteOidRule> m_writeRuleList;
};

#endif // QSNMPCLIENT_H
