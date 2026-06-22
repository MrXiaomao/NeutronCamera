#include "pciecommsdk.h"
#include <math.h>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include "datacompresswindow.h"
#include "AppConfig.h"

#ifdef _WIN32
#define	XDMA_FILE_USER		"\\user"
#define	XDMA_FILE_CONTROL	"\\control"
#define XDMA_FILE_BYPASS	"\\bypass"
#define	XDMA_FILE_H2C_0		"\\h2c_0"
#define	XDMA_FILE_H2C_1		"\\h2c_1"
#define	XDMA_FILE_H2C_2		"\\h2c_2"
#define	XDMA_FILE_H2C_3		"\\h2c_3"
#define	XDMA_FILE_C2H		"\\c2h"
#define	XDMA_FILE_C2H_0		"\\c2h_0"
#define	XDMA_FILE_C2H_1		"\\c2h_1"
#define	XDMA_FILE_C2H_2		"\\c2h_2"
#define	XDMA_FILE_C2H_3		"\\c2h_3"
#define	XDMA_FILE_EVENT_0	"\\event_0"
#define	XDMA_FILE_EVENT_1	"\\event_1"
#define	XDMA_FILE_EVENT_2	"\\event_2"
#define	XDMA_FILE_EVENT_3	"\\event_3"
#else
#define	XDMA_FILE_USER		"_user"
#define	XDMA_FILE_CONTROL	"_control"
#define XDMA_FILE_BYPASS	"_bypass"
#define	XDMA_FILE_H2C_0		"_h2c_0"
#define	XDMA_FILE_H2C_1		"_h2c_1"
#define	XDMA_FILE_H2C_2		"_h2c_2"
#define	XDMA_FILE_H2C_3		"_h2c_3"
#define	XDMA_FILE_C2H_0		"_c2h_0"
#define	XDMA_FILE_C2H_1		"_c2h_1"
#define	XDMA_FILE_C2H_2		"_c2h_2"
#define	XDMA_FILE_C2H_3		"_c2h_3"
#define	XDMA_FILE_EVENT_0	"_event_0"
#define	XDMA_FILE_EVENT_1	"_event_1"
#define	XDMA_FILE_EVENT_2	"_event_2"
#define	XDMA_FILE_EVENT_3	"_event_3"
#endif

// pciecommsdk.cpp 实现
bool NoBufferingFile::open(const QString &fileName, QIODevice::OpenMode mode)
{
    DWORD access = 0;
    if (mode & QIODevice::WriteOnly) access |= GENERIC_WRITE;
    if (mode & QIODevice::ReadOnly) access |= GENERIC_READ;

    DWORD creationDisposition = (mode & QIODevice::Truncate) ? CREATE_ALWAYS : OPEN_EXISTING;
    if (mode & QIODevice::Append) creationDisposition = OPEN_ALWAYS;

    HANDLE hFile = CreateFileW(
        reinterpret_cast<LPCWSTR>(fileName.utf16()),
        access,
        0,
        nullptr,
        creationDisposition,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
        nullptr
        );

    if (hFile == INVALID_HANDLE_VALUE) {
        setErrorString(QString("CreateFile failed: %1").arg(GetLastError()));
        return false;
    }

    return QFile::open(reinterpret_cast<intptr_t>(hFile), mode, QFile::AutoCloseHandle);
}


PCIeCommSdk::PCIeCommSdk(QObject *parent)
    : QObject{parent}
{
    QStringList args = QCoreApplication::arguments();
    if (args.size() == 1){
        mDevices = enumDevices();
        initCaptureThreads();
    }
}


PCIeCommSdk::~PCIeCommSdk()
{
    for (int cardIndex = 1; cardIndex <= numberOfDevices(); ++cardIndex){
        auto iter = mMapCaptureThread.find(cardIndex);
        if (iter != mMapCaptureThread.end()){
            CaptureThread *captureThread = iter.value();
            captureThread->stop();
            captureThread->quit();
            captureThread = nullptr;
            mMapCaptureThread.remove(cardIndex);
        }
    }
}

// 枚举指定设备接口GUID下所有已连接设备，返回设备路径列表
/*
    SetupDiEnumDeviceInterfaces 枚举指定设备接口GUID下所有已连接设备（已经禁用设备无法枚举到）
    SetupDiEnumDeviceInfo 枚举指定设备接口GUID下所有设备（包括已禁用设备)
*/
// QStringList PCIeCommSdk::enumDevices()
// {
// #ifdef _WIN32
//     const GUID guid = {0x74c7e4a9, 0x6d5d, 0x4a70, {0xbc, 0x0d, 0x20, 0x69, 0x1d, 0xff, 0x9e, 0x9d}};
//     QStringList lstDevices;
//     HDEVINFO hDevInfo = SetupDiGetClassDevsA((LPGUID)&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
//     if (hDevInfo == INVALID_HANDLE_VALUE) {
//         qCritical() << "GetDevices INVALID_HANDLE_VALUE";
//         return lstDevices;
//     }

//     SP_DEVICE_INTERFACE_DATA device_interface;
//     device_interface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

//     DWORD index;
//     for (index = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &guid, index, &device_interface); ++index) {
//         ULONG detailLength = 0;
//         if (!SetupDiGetDeviceInterfaceDetailA(hDevInfo, &device_interface, NULL, 0, &detailLength, NULL) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
//             qDebug() << "SetupDiGetDeviceInterfaceDetail - get length failed";
//             break;
//         }

//         PSP_DEVICE_INTERFACE_DETAIL_DATA_A dev_detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, detailLength);
//         if (!dev_detail) {
//             qDebug() << "HeapAlloc failed";
//             break;
//         }
//         dev_detail->cbSize = sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA_A);

//         if (!SetupDiGetDeviceInterfaceDetailA(hDevInfo, &device_interface, dev_detail, detailLength, NULL, NULL)) {
//             qDebug() << "SetupDiGetDeviceInterfaceDetail - get detail failed";
//             HeapFree(GetProcessHeap(), 0, dev_detail);
//             break;
//         }

//         lstDevices.append(QString::fromLatin1(dev_detail->DevicePath, strlen(dev_detail->DevicePath)));
//         HeapFree(GetProcessHeap(), 0, dev_detail);
//     }

//     SetupDiDestroyDeviceInfoList(hDevInfo);

//     if (lstDevices.size() == 0)
//         lstDevices << "/dev/xdma0" << "/dev/xdma1" << "/dev/xdma2";

//     return lstDevices;
// #else
//     QStringList lstDevices;
//     for (int i=0; i<=3; ++i)
//     {
//         if (QFileInfo::exists(QString("/dev/xdma%1_user").arg(i)))
//             lstDevices << QString("/dev/xdma%1").arg(i);
//     }

//     return QStringList() << lstDevices;//"/dev/xdma0" << "/dev/xdma1";// << "/dev/xdma2";
// #endif
// }

QStringList PCIeCommSdk::enumDevices()
{
#ifdef _WIN32
    const GUID guid = {0x74c7e4a9, 0x6d5d, 0x4a70, {0xbc, 0x0d, 0x20, 0x69, 0x1d, 0xff, 0x9e, 0x9d}};
    QStringList lstDevices;
    HDEVINFO hDevInfo = SetupDiGetClassDevsA((LPGUID)&guid, NULL, NULL, DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCritical() << "GetDevices INVALID_HANDLE_VALUE";
        return lstDevices;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD devIndex = 0; SetupDiEnumDeviceInfo(hDevInfo, devIndex, &devInfoData); ++devIndex) {
        ULONG devStatus = 0;
        ULONG problemCode = 0;
        CONFIGRET cr = CM_Get_DevNode_Status(&devStatus, &problemCode, devInfoData.DevInst, 0);
        bool isDisabled = false;
        if (cr == CR_SUCCESS) {
            // 核心逻辑：存在问题 + 问题码匹配硬件禁用，才标记为禁用
            if (((devStatus & DN_HAS_PROBLEM) != 0 || (devStatus & DN_DISABLEABLE) != 0) &&
                (problemCode == CM_PROB_DISABLED || problemCode == CM_PROB_HARDWARE_DISABLED)) {
                isDisabled = true;
            }
        }
        else {
            continue;
        }

        SP_DEVICE_INTERFACE_DATA device_interface;
        device_interface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        for (DWORD ifIndex = 0; SetupDiEnumDeviceInterfaces(hDevInfo, &devInfoData, &guid, ifIndex, &device_interface); ++ifIndex) {
            ULONG detailLength = 0;
            if (!SetupDiGetDeviceInterfaceDetailA(hDevInfo, &device_interface, NULL, 0, &detailLength, NULL)
                && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                qDebug() << "SetupDiGetDeviceInterfaceDetail - get length failed";
                break;
            }

            PSP_DEVICE_INTERFACE_DETAIL_DATA_A dev_detail =
                (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, detailLength);
            if (!dev_detail) {
                qDebug() << "HeapAlloc failed";
                break;
            }
            // 修复64位cbSize
            if (sizeof(ULONG_PTR) == 8) {
                dev_detail->cbSize = 8;
            } else {
                dev_detail->cbSize = offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_A, DevicePath);
            }

            if (SetupDiGetDeviceInterfaceDetailA(hDevInfo, &device_interface, dev_detail, detailLength, NULL, &devInfoData)) {
                QString path = QString::fromLatin1(dev_detail->DevicePath);
                if (isDisabled) {
                    path += "[已禁用]";
                }
                else {
                    lstDevices.append(path);
                }
            } else {
                qDebug() << "SetupDiGetDeviceInterfaceDetail - get detail failed, error:" << GetLastError();
            }

            HeapFree(GetProcessHeap(), 0, dev_detail);
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return lstDevices;
#else
    QStringList lstDevices;
    for (int i=0; i<=3; ++i)
    {
        if (QFileInfo::exists(QString("/dev/xdma%1_user").arg(i)))
            lstDevices.append(QString("/dev/xdma%1").arg(i));
    }
    return lstDevices;
#endif
}


QList<PCIeCommSdk::DeviceInfo> PCIeCommSdk::enumerateSpecifiedDevices(const GUID& deviceClassGuid)
{
    QList<DeviceInfo> result;
    // 获取指定设备类的设备信息集合
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(
        (LPGUID)&deviceClassGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        );

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCritical() << "Failed to get device info handle, error code:" << GetLastError();
        return result;
    }

    SP_DEVICE_INTERFACE_DATA deviceInterfaceData = {};
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // 遍历枚举所有匹配设备
    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(
             hDevInfo,
             nullptr,
             &deviceClassGuid,
             index,
             &deviceInterfaceData
             );
         ++index) {

        // 1. 先获取详情所需缓冲区大小
        ULONG requiredSize = 0;
        if (!SetupDiGetDeviceInterfaceDetailA(
                hDevInfo,
                &deviceInterfaceData,
                nullptr,
                0,
                &requiredSize,
                nullptr) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            qDebug() << "Get required buffer size failed, error:" << GetLastError();
            break;
        }

        // 分配缓冲区
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A pDeviceDetail =
            (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)HeapAlloc(
                GetProcessHeap(),
                HEAP_ZERO_MEMORY,
                requiredSize
                );

        if (!pDeviceDetail) {
            qDebug() << "Heap allocation failed";
            break;
        }
        pDeviceDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        // 准备存储设备信息的结构体
        SP_DEVINFO_DATA devInfoData = {};
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        // 2. 读取设备详情
        if (!SetupDiGetDeviceInterfaceDetailA(
                hDevInfo,
                &deviceInterfaceData,
                pDeviceDetail,
                requiredSize,
                nullptr,
                &devInfoData)) {
            qDebug() << "Get device detail failed, error:" << GetLastError();
            HeapFree(GetProcessHeap(), 0, pDeviceDetail);
            break;
        }

        // 3. 查询设备启用状态
        DeviceInfo currentDev;
        currentDev.devicePath = QString::fromLocal8Bit(pDeviceDetail->DevicePath);

        DWORD devStatus = 0;
        if (SetupDiGetDeviceRegistryPropertyA(
                hDevInfo,
                &devInfoData,
                SPDRP_CONFIGFLAGS,
                nullptr,
                (PBYTE)&devStatus,
                sizeof(devStatus),
                nullptr)) {
            // 如果位标志中不存在DN_STARTED，则表示设备未启用
            currentDev.isEnabled = (devStatus & DN_STARTED) != 0;
        } else {
            // 如果读取状态失败，默认标记为未启用
            qDebug() << "Get device status failed for:" << currentDev.devicePath;
            currentDev.isEnabled = false;
        }

        result.append(currentDev);
        HeapFree(GetProcessHeap(), 0, pDeviceDetail);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return result;
}

// 查询设备当前是否为启用状态
#ifndef CONFIGFLAG_DISABLED
#define CONFIGFLAG_DISABLED 0x00000001
#endif
BOOL IsDeviceEnabled(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, BOOL* pIsEnabled)
{
    if (hDevInfo == NULL || devInfoData == NULL || pIsEnabled == NULL)
        return FALSE;

    DWORD configFlags = 0;
    if (!SetupDiGetDeviceRegistryProperty(
            hDevInfo,
            devInfoData,
            SPDRP_CONFIGFLAGS,
            NULL,
            (PBYTE)&configFlags,
            sizeof(configFlags),
            NULL))
    {
        return FALSE;
    }

    // 若包含DISABLED标志则为禁用，否则为启用
    *pIsEnabled = !(configFlags & CONFIGFLAG_DISABLED);
    return TRUE;
}

// 控制设备启用/禁用：TRUE=启用，FALSE=禁用
BOOL SetDeviceEnabled(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, BOOL enable)
{
    if (hDevInfo == NULL || devInfoData == NULL)
        return FALSE;

    // 1. 获取当前设备状态，避免重复操作
    BOOL currentState = FALSE;
    if (!IsDeviceEnabled(hDevInfo, devInfoData, &currentState))
        return FALSE;
    if (currentState == enable)
        return TRUE; // 状态已经符合要求，直接返回成功

    // 2. 构造启用/禁用属性参数
    SP_PROPCHANGE_PARAMS propChangeParams = {0};
    propChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    propChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    propChangeParams.Scope = DICS_FLAG_GLOBAL;
    propChangeParams.StateChange = enable ? DICS_ENABLE : DICS_DISABLE;

    // 3. 调用API执行状态变更
    if (!SetupDiSetClassInstallParams(
            hDevInfo,
            devInfoData,
            &propChangeParams.ClassInstallHeader,
            sizeof(propChangeParams)))
    {
        return FALSE;
    }

    // 4. 触发设备属性变更生效
    return SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, devInfoData);
}

quint32 PCIeCommSdk::numberOfDevices()
{
    return mDevices.count();
}

/*输出设备信息*/
void PCIeCommSdk::printDevicesInfomation()
{
    QStringList args = QCoreApplication::arguments();
    if (args.size() > 1)
        return; // 离线处理不需要检测卡是否存在

    if (0 == numberOfDevices())
        qCritical().noquote() << QStringLiteral("未发现数据采集卡，请检查卡是否松动或是已被禁用！");
    else{
        QStringList lst;
        for (int i=1; i<=3; ++i)
        {
            if (boardExists(i)){
                lst << QStringLiteral("卡%1%2").arg(i).arg((boardIsEnable(i) ? "" : "[未启用]"));
            }
        }

        qInfo().noquote() << QStringLiteral("发现数据采集卡：") << lst.join(',');
    }
}

/*判断板卡是否存在*/
bool PCIeCommSdk::boardExists(const quint8& index/*板卡序号1-3*/)
{
    QStringList lst = QStringList() << QStringLiteral("B189E7&0&0020") << QStringLiteral("10D89B97&0&0020") << QStringLiteral("18EC5E6B&0&0020");
    foreach(QString name, mDevices){
        if (name.toUpper().contains(lst[index-1]))
            return true;
    }

    return false;
}

#ifdef _WIN32
#include <processtopologyapi.h> //SetThreadGroupAffinity
#endif
void PCIeCommSdk::startCapture(quint32 index, QString fileSavePath, quint32 captureTimeSeconds, QString shotNum/*炮号*/, bool testMode)
{
    auto iter = mMapCaptureThread.find(index);
    if (iter != mMapCaptureThread.end()){
        //QString devicePath = mDevices.at(index - 1) + XDMA_FILE_C2H + QStringLiteral().arg();
        // if (i==0)
        //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&B189E7&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";
        // else if (i==1)
        //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&10D89B97&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";
        // else if (i==2)
        //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&18ec5e6b&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";

        mMapCaptureThread[index]->setParamter(fileSavePath, captureTimeSeconds, testMode);

        //启动写文件线程
        mThreadRunning[index] = true;
        if (mMapCaptureThread[index]->isRunning())
            mMapCaptureThread[index]->resume();
        else
            mMapCaptureThread[index]->start();
    }
}

void PCIeCommSdk::stopCapture(quint32 index)
{
    auto iter = mMapCaptureThread.find(index);
    if (iter != mMapCaptureThread.end()){
        CaptureThread *captureThread = iter.value();
        //captureThread->pause();
        captureThread->stopMeasure();
    }
}

void PCIeCommSdk::startAllCapture(QString fileSavePath, quint32 captureTimeSeconds,  QString shotNum/*炮号*/)
{
    for (int deviceIndex = 1; deviceIndex <= numberOfDevices() * 2; ++deviceIndex)
    {
        bool isDDR1 = deviceIndex <= numberOfDevices();
        quint8 cardIndex = (deviceIndex - 1) % numberOfDevices() + 1;
        quint8 boardIndex = boardNameToBoardIndex(mDevices.at(cardIndex - 1));
        if (boardIsEnable(boardIndex) && AppConfig::instance().enableCapture(boardIndex, isDDR1))
            startCapture(deviceIndex, fileSavePath, captureTimeSeconds, shotNum, mMeasureMode & mmTest);
        else
            mThreadRunning[deviceIndex] = false;
    }
}

void PCIeCommSdk::stopAllCapture()
{
    for (int deviceIndex = 1; deviceIndex <= numberOfDevices() * 2; ++deviceIndex)
    {
        stopCapture(deviceIndex);
    }
}

void PCIeCommSdk::init()
{
    for (int cardIndex = 1; cardIndex <= mDevices.size(); ++cardIndex){
        quint8 boardIndex = boardNameToBoardIndex(mDevices.at(cardIndex - 1));
        if (!boardIsEnable(boardIndex))
            continue;

        if (!mMapCaptureThread.contains(cardIndex))
            continue;

        HANDLE fUserHandle = PCIeCommSdk::getHandle(mDevices[cardIndex-1] + XDMA_FILE_USER,
                                                    GENERIC_READ | GENERIC_WRITE,
                                                    FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE);
        if (fUserHandle == INVALID_HANDLE_VALUE)
            continue;

        PCIeCommSdk::writeData(cardIndex, fUserHandle, 0x20000, QByteArray::fromHex("30 FA 34 12"));
        ::QThread::msleep(100);
        PCIeCommSdk::writeData(cardIndex, fUserHandle, 0x20000, QByteArray::fromHex("00 00 00 00"));
        ::QThread::msleep(100);

        CloseHandle(fUserHandle);
    }
}

void PCIeCommSdk::reset()
{
    for (int cardIndex = 1; cardIndex <= mDevices.size(); ++cardIndex){
        quint8 boardIndex = boardNameToBoardIndex(mDevices.at(cardIndex - 1));
        if (!boardIsEnable(boardIndex))
            continue;

        if (!mMapCaptureThread.contains(cardIndex))
            continue;

        HANDLE fUserHandle = PCIeCommSdk::getHandle(mDevices[cardIndex-1] + XDMA_FILE_USER,
                                             GENERIC_READ | GENERIC_WRITE,
                                             FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE);
        if (fUserHandle == INVALID_HANDLE_VALUE)
            continue;

        writeData(cardIndex, fUserHandle, 0x20000, QByteArray::fromHex("02 D0 34 12"));//数采
        QThread::msleep(100);
        writeData(cardIndex, fUserHandle, 0x20000, QByteArray::fromHex("00 00 00 00"));
        QThread::msleep(100);
        writeData(cardIndex, fUserHandle, 0x20000, QByteArray::fromHex("01 D0 34 12"));//DDR
        QThread::msleep(100);
        writeData(cardIndex, fUserHandle, 0x20000, QByteArray::fromHex("00 00 00 00"));

        // 初始化，设置测量时间
        writeData(cardIndex, fUserHandle, 0x20000, QByteArray::fromHex("30 FA 34 12"));//16-160ms 18-800ms 20-4000ms
        ::QThread::msleep(100);
        writeData(cardIndex, fUserHandle, 0x20000, QByteArray::fromHex("00 00 00 00"));
        ::QThread::msleep(100);

        CloseHandle(fUserHandle);
    }

    qInfo().nospace() << "复位完成";
}

bool PCIeCommSdk::boardIsEnable(quint8 boardIndex)
{
    const GUID& deviceClassGuid = {0x74c7e4a9, 0x6d5d, 0x4a70, {0xbc, 0x0d, 0x20, 0x69, 0x1d, 0xff, 0x9e, 0x9d}};

    // 获取指定设备类的设备信息集合
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(
        (LPGUID)&deviceClassGuid,
        nullptr,
        nullptr,
        DIGCF_DEVICEINTERFACE//DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        );

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCritical() << "Failed to get device info handle, error code:" << GetLastError();
        return false;
    }

    // 遍历枚举所有匹配设备
    SP_DEVINFO_DATA devInfoData = { 0 };
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    if (SetupDiEnumDeviceInfo(hDevInfo, (boardIndex-1), &devInfoData)){
        ULONG devStatus = 0;
        ULONG problemCode = 0;
        CONFIGRET cr = CM_Get_DevNode_Status(&devStatus, &problemCode, devInfoData.DevInst, 0);
        bool isEnabled = true;
        if (cr == CR_SUCCESS) {
            // 核心逻辑：存在问题 + 问题码匹配硬件禁用，才标记为禁用
            if (((devStatus & DN_HAS_PROBLEM) != 0 || (devStatus & DN_DISABLEABLE) != 0) &&
                (problemCode == CM_PROB_DISABLED || problemCode == CM_PROB_HARDWARE_DISABLED)) {
                isEnabled = false;
            }
        }
        else {
            isEnabled = false;
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);
        return isEnabled;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return false;
}

bool PCIeCommSdk::setBoardState(quint8 boardIndex, bool enable)
{
    const GUID& deviceClassGuid = {0x74c7e4a9, 0x6d5d, 0x4a70, {0xbc, 0x0d, 0x20, 0x69, 0x1d, 0xff, 0x9e, 0x9d}};

    // 获取指定设备类的设备信息集合
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(
        (LPGUID)&deviceClassGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_ALLCLASSES
        );

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCritical() << "Failed to get device info handle, error code:" << GetLastError();
        return false;
    }

    SP_PROPCHANGE_PARAMS PropChangeParams;
    SP_DEVINFO_DATA devInfoData = {sizeof(SP_DEVINFO_DATA)};
    SP_DEVINSTALL_PARAMS devParams;
    //查询设备信息
    if (!SetupDiEnumDeviceInfo( hDevInfo, (boardIndex-1), &devInfoData))
    {
        qDebug() << ("SetupDiEnumDeviceInfo FAILED");
        return FALSE;
    }
    //设置设备属性变化参数
    PropChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    PropChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    PropChangeParams.Scope = DICS_FLAG_GLOBAL; //使修改的属性保存在所有的硬件属性文件
    PropChangeParams.StateChange = enable ? DICS_ENABLE : DICS_DISABLE;//enable ? DICS_START: DICS_STOP;//enable ? DICS_ENABLE : DICS_DISABLE;
    PropChangeParams.HwProfile = 0;
    //改变设备属性
    if (!SetupDiSetClassInstallParams( hDevInfo,
                                      &devInfoData,
                                      (SP_CLASSINSTALL_HEADER *)&PropChangeParams,
                                      sizeof(PropChangeParams)))
    {
        qDebug() << ("SetupDiSetClassInstallParams FAILED");
        return FALSE;
    }
    PropChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    PropChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    PropChangeParams.Scope = DICS_FLAG_CONFIGSPECIFIC;//使修改的属性保存在指定的属性文件
    PropChangeParams.StateChange = enable ? DICS_ENABLE : DICS_DISABLE;
    PropChangeParams.HwProfile = 0;
    //改变设备属性并调用安装服务
    if (!SetupDiSetClassInstallParams( hDevInfo,
                                      &devInfoData,
                                      (SP_CLASSINSTALL_HEADER *)&PropChangeParams,
                                      sizeof(PropChangeParams))
        ||!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, &devInfoData))
    {
        qDebug() << ("SetupDiSetClassInstallParams or SetupDiCallClassInstaller FAILED");
        return false;
    }

    //判断是否需要重新启动
    devParams.cbSize = sizeof(devParams);
    if (!SetupDiGetDeviceInstallParams( hDevInfo, &devInfoData, &devParams))
    {
        qDebug() << ("SetupDiGetDeviceInstallParams FAILED");
        return FALSE;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    if (devParams.Flags & (DI_NEEDRESTART|DI_NEEDREBOOT))
    {
        qDebug() << ("Need Restart Computer");
    }

    return true;
}


void PCIeCommSdk::reboot()
{
    const GUID& deviceClassGuid = {0x74c7e4a9, 0x6d5d, 0x4a70, {0xbc, 0x0d, 0x20, 0x69, 0x1d, 0xff, 0x9e, 0x9d}};

    // 获取指定设备类的设备信息集合
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(
        (LPGUID)&deviceClassGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        );

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCritical() << "Failed to get device info handle, error code:" << GetLastError();
        return ;
    }

    // 遍历枚举所有匹配设备
    SP_DEVINFO_DATA devInfoData = { 0 };
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD devIndex = 0; SetupDiEnumDeviceInfo(hDevInfo, devIndex, &devInfoData); ++devIndex) {
        // 调用状态变更启用设备
        // // 构造启用/禁用属性参数
        // SP_PROPCHANGE_PARAMS propChangeParams = {0};
        // propChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        // propChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        // propChangeParams.Scope = DICS_FLAG_GLOBAL;
        // propChangeParams.StateChange = true ? DICS_ENABLE : DICS_DISABLE;

        {
            // 先禁用
            SP_REMOVEDEVICE_PARAMS rmp = {0};
            rmp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            rmp.ClassInstallHeader.InstallFunction = DIF_REMOVE;
            // 禁用时不删除设备，只禁用；启用不需要这个参数
            rmp.Scope = DI_REMOVEDEVICE_GLOBAL;

            // 3. 调用API执行状态变更
            if (!SetupDiSetClassInstallParams(
                    hDevInfo,
                    &devInfoData,
                    &rmp.ClassInstallHeader,
                    sizeof(rmp)))
            {
                return ;
            }
        }

        {
            // 再启用
            SP_REMOVEDEVICE_PARAMS rmp = {0};
            rmp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            rmp.ClassInstallHeader.InstallFunction = DIF_REMOVE;

            // 3. 调用API执行状态变更
            if (!SetupDiSetClassInstallParams(
                    hDevInfo,
                    &devInfoData,
                    &rmp.ClassInstallHeader,
                    sizeof(rmp)))
            {
                return ;
            }
        }

        // 3. 调用API执行状态变更
        // if (!SetupDiSetClassInstallParams(
        //         hDevInfo,
        //         &devInfoData,
        //         &propChangeParams.ClassInstallHeader,
        //         sizeof(propChangeParams)))
        // {
        //     return ;
        // }

        // 触发设备属性变更生效
        SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, &devInfoData);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
}

void PCIeCommSdk::setPSDThreshold()
{
    /* 设置PSD甄别阈值 */
    quint8 threshold = AppConfig::instance().psdThreshold();
    for (int cardIndex = 1; cardIndex <= mDevices.size(); ++cardIndex){
        if (!mMapCaptureThread.contains(cardIndex))
            continue;

        HANDLE fUserHandle = PCIeCommSdk::getHandle(mDevices[cardIndex-1] + XDMA_FILE_USER,
                                                    GENERIC_READ | GENERIC_WRITE,
                                                    FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE);
        if (fUserHandle == INVALID_HANDLE_VALUE)
            continue;

        QByteArray command = QByteArray::fromHex("00 FD 34 12");
        command[0] = threshold;
        writeData(cardIndex, fUserHandle, 0x20000, command);//PSD阈值
        ::QThread::msleep(100);
        writeData(cardIndex, fUserHandle, 0x20000, QByteArray::fromHex("00 00 00 00"));
        ::QThread::msleep(100);

        CloseHandle(fUserHandle);
    }

    qInfo().nospace() << "设置PSD阈值：" << threshold;
}

bool PCIeCommSdk::test()
{
    bool allOk = true;
    for (int deviceIndex = 1; deviceIndex <= mDevices.size()*2; ++deviceIndex){
        bool isDDR1 = deviceIndex <= numberOfDevices();
        quint8 cardIndex = (deviceIndex - 1) % numberOfDevices() + 1;
        quint8 boardIndex = boardNameToBoardIndex(mDevices.at(cardIndex - 1));

        if (AppConfig::instance().enableCapture(boardIndex, isDDR1))
        {
            if (mMapCaptureThread[deviceIndex]->dataExistError())
                allOk = false;
        }
    }

    return allOk;
}

#include <QtEndian>
/**
 * @brief PCIeCommSdk::analyzeHistorySpectrumData 分析能谱历史数据
 * @param cameraIndex 相机序号
 */
bool PCIeCommSdk::analyzeHistorySpectrumData(const quint8& cameraIndex,
                                const quint32& timeStart/*开始时刻*/,
                                const quint32& timeStop/*停止时刻*/ ,
                                const QString& fileDir/*文件名*/,
                                std::function<void(
                                     QPair<QVector<double>/*道址*/,QVector<double>/*伽马累积能谱*/>&,
                                     QPair<QVector<double>/*道址*/,QVector<double>/*中子累积能谱*/>&
                                    )> callback)
{
    QVector<double> spectrumGamma;
    QVector<double> spectrumNeutron;

    //根据开始时间和结束时间，过滤掉不在时间范围内的文件
    int startFileId = timeStart / PACKET_TIMELENGTH + 1;
    int endFileId = startFileId + (timeStop - timeStart) / PACKET_TIMELENGTH;
    //根据通道号判断是板卡的A面还是B面
    QString sideFile = "B";
    if ((cameraIndex % 6) >= 1 && (cameraIndex % 6) <= CAMNUMBER_DDR_PER)
        sideFile = "A";
    //根据通道号计算板卡的索引
    int board_index = (cameraIndex + 5) / 6;
    int deviceIndex = (cameraIndex - 1) / CAMNUMBER_DDR_PER + 1;
    //根据通道号计算对应采集卡的第几通道
    quint8 cameraNo = (cameraIndex - 1) % CAMNUMBER_DDR_PER;
    for (int id = startFileId; id <= endFileId; ++id){
        QString filePath = QString("%1/%2%3spec%4.bin").arg(fileDir).arg(board_index).arg(sideFile).arg(id);

        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QByteArray spectrumData = f.readAll();
        f.close();

        //计算出是当前文件波形的第几个数据点
        quint32 timeFrom;
        quint32 timeTo;

        if (id == startFileId)
            timeFrom = (timeStart % PACKET_TIMELENGTH);
        else
            timeFrom = 0;

        if (id == endFileId){
            if ((timeStop % PACKET_TIMELENGTH) == 0)
                timeTo  = PACKET_TIMELENGTH;
            else
                timeTo = (timeStop % PACKET_TIMELENGTH);
        }
        else
            timeTo  = PACKET_TIMELENGTH;

        QByteArray reverseData = PCIeCommSdk::reverseArray(spectrumData, 16);
        if (reverseData.startsWith(QByteArray::fromHex("FFAB00D2")))
        {
            for (quint8 packPos = timeFrom; packPos < timeTo; ++packPos){
                QByteArray chunk = reverseData.mid(packPos*1024, 1024);
                QByteArray chunkHead = chunk.left(16);
                QByteArray chunkTail = chunk.right(16);

                bool ok;
                //能谱序号 高位16bi表示这是 第几个50ms的数据，低位8bit表示，在每一个50ms里，这是第几个能谱数据（范围是1-50）
                QByteArray head = chunk.left(4);
                quint32 serialNumber = chunk.mid(4, 2).toHex().toUInt(&ok, 16);
                quint32 serialNumberRef = chunk.mid(6, 1).toInt();
                quint16 time = chunk.mid(7, 2).toHex().toUShort(&ok, 16);//测量时间
                QByteArray reserve1 = chunk.mid(9, 7);

                QByteArray chunkgamma = chunk.mid(16, 496);  //62*4*2
                QByteArray chunkneutron = chunk.mid(512, 496);

                quint64 absoluteTime = chunk.mid(1008, 8).toHex().toULongLong(&ok, 16);//绝对时间
                QByteArray reserve2 = chunk.mid(1016, 6);
                QByteArray tail = chunk.right(2);

                //数据包的起始时间戳/ns
                //quint64 timestamp = (serialNumber - 1) * 50 + serialNumberRef;
                //if (timestamp == mTimestampMs1)
                {
                    QByteArray gamma = chunkgamma.mid(cameraNo*124, 124);
                    QByteArray neutron = chunkneutron.mid(cameraNo*124, 124);
                    {
                        QVector<double> data;
                        for (int i=0; i<gamma.size(); i+=2){
                            bool ok;
                            quint16 amplitude = gamma.mid(i, 2).toHex().toUShort(&ok, 16);
                            data.append(amplitude);
                        }

                        if (spectrumGamma.isEmpty())
                        {
                            spectrumGamma.swap(data);
                        }
                        else
                        {
                            for (int i=0; i<data.size(); ++i){
                                spectrumGamma[i] += data[i];
                            }
                        }
                    }

                    {
                        QVector<double> data;
                        for (int i=0; i<neutron.size(); i+=2){
                            bool ok;
                            quint16 amplitude = neutron.mid(i, 2).toHex().toUShort(&ok, 16);
                            data.append(amplitude);
                        }

                        if (spectrumNeutron.isEmpty())
                        {
                            spectrumNeutron.swap(data);
                        }
                        else
                        {
                            for (int i=0; i<data.size(); ++i){
                                spectrumNeutron[i] += data[i];
                            }
                        }
                    }
                }
            }
        }
    }

    QVector<double>/*道址*/  channel;
    for (int i=0; i<spectrumGamma.size(); ++i){
        channel << i;
    }

    QPair<QVector<double>/*道址*/,QVector<double>/*伽马累积能谱*/> pairSpectrumGamma = qMakePair(channel, spectrumGamma);
    QPair<QVector<double>/*道址*/,QVector<double>/*中子累积能谱*/> pairSpectrumNeutron= qMakePair(channel, spectrumGamma);
    callback(pairSpectrumGamma, pairSpectrumNeutron);
    return true;
}

/**
 * @brief PCIeCommSdk::analyzeHistoryWaveformData 离线分析波形历史数据，兼容旧版和新版FPGA数据协议
 * 波形数据排列方式：ch3, ch3, ch2, ch2, ch0, ch0, ch1, ch1...（逐点交织）
 * @param cameraIndex 相机序号
 * @param timeLength 要提取的波形时间长度，单位ms
 * @param remainTime 剩余时间，用于计算采集时刻
 * @param filePath 历史数据文件路径
 */
bool PCIeCommSdk::analyzeHistoryWaveformData(const quint8& cameraIndex,
                                        const quint32& timeStart/*开始时刻ms*/,
                                        const quint32& timeStop/*结束时刻ms*/,
                                        const QString& fileDir/*文件存储路径*/,
                                        std::function<void(const QMap<quint64/*时刻（ns）*/,qint16/*数值*/>&)> callback
                                        )
{
    QMap<quint64/*时刻*/,qint16/*数值*/> waveformPair;

    //根据开始时间和结束时间，过滤掉不在时间范围内的文件
    int startFileId = timeStart / PACKET_TIMELENGTH + 1;
    int endFileId = startFileId + (timeStop - timeStart) / PACKET_TIMELENGTH;
    //根据通道号判断是板卡的A面还是B面
    QString sideFile = "B";
    if ((cameraIndex % 6) >= 1 && (cameraIndex % 6) <= CAMNUMBER_DDR_PER)
        sideFile = "A";
    //根据通道号计算板卡的索引
    int board_index = (cameraIndex + 5) / 6;
    int deviceIndex = (cameraIndex - 1) / CAMNUMBER_DDR_PER + 1;
    //根据通道号计算对应采集卡的第几通道
    quint8 cameraNo = (cameraIndex - 1) % CAMNUMBER_DDR_PER;
    for (int id = startFileId; id <= endFileId; ++id){
        QString filePath = QString("%1/%2%3data%4.bin").arg(fileDir).arg(board_index).arg(sideFile).arg(id);
        QVector<QVector<qint16>> ch(3);
        if (DataAnalysisWorker::readBin3Ch_fast(filePath, ch[0], ch[1], ch[2], true)) {

            //计算出是当前文件波形的第几个数据点
            quint32 timeFrom;
            quint32 timeTo;

            if (id == startFileId)
                timeFrom = (timeStart % PACKET_TIMELENGTH) * 1000 * 1000 / 2;
            else
                timeFrom = 1;

            if (id == endFileId){
                if ((timeStop % PACKET_TIMELENGTH) == 0)
                    timeTo  = ch[cameraNo].size();
                else
                    timeTo = (timeStop % PACKET_TIMELENGTH) * 1000 * 1000 / 2;
            }
            else
                timeTo  = ch[cameraNo].size();

            //计算出是当前文件波形的第几个数据点
            int packPos = timeFrom - 1;
            int point_num = timeTo - timeFrom + 1;

            //扣基线，调整数据
            qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch[cameraNo]);
            DataAnalysisWorker::adjustDataWithBaseline(ch[cameraNo], baseline_ch, deviceIndex, cameraNo + 1);

            //提取通道号的数据cameraNo
            QVector<qint16> waveform = ch[cameraNo].mid(packPos, point_num);

            for (int i=0;i<waveform.size();++i)
                waveformPair.insert(timeStart * 1000 * 1000 + i*2, waveform[i]);
        }
    }

    callback(waveformPair);
    return true;
}

bool PCIeCommSdk::analyzeHistoryCpsData(
                                        const quint32 channels/*多道道数*/,
                                        const quint32 timeWidth/*时间宽度ms*/,
                                        const quint32 timeStart/*开始时刻ms*/,
                                        const quint32 timeStop/*结束时刻ms*/,
                                        const QString& filePath/*H5文件路径*/,
                                        std::function<void(QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>>, QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>>)> callback,
                                        const quint32 minPeak/*最小峰值0*/,
                                        const quint32 maxPeak/*最大峰值16384*/
                                        )
{
    //根据通道号计算对应采集卡的第几通道
    QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
    QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
    try {
        H5::H5File file(filePathBytes.toStdString(), H5F_ACC_RDONLY);

        for (int boardNum=1; boardNum<=6; ++boardNum){
            // 创建或打开板卡组
            QString boardGroupName = QString("Board%1").arg(boardNum);

            H5::Group boardGroup;
            htri_t existsGroup = H5Lexists(file.getId(), boardGroupName.toStdString().c_str(), H5P_DEFAULT);

            if (existsGroup > 0) {
                boardGroup = file.openGroup(boardGroupName.toStdString());

                // 辅助函数：写入单个通道的数据集
                auto readChannel = [&](const QString& datasetName, QVector<qint16>& timeTrigger, QVector<qint16>& timePeak) {
                    htri_t existsDataset = H5Lexists(boardGroup.getId(), datasetName.toStdString().c_str(), H5P_DEFAULT);
                    if (existsDataset){
                        std::string ds = datasetName.toUtf8().constData();
                        H5::DataSet dataset = boardGroup.openDataSet(ds);
                        H5::DataSpace fileSpace = dataset.getSpace();

                        // 校验维度
                        int rank = fileSpace.getSimpleExtentNdims();
                        if (rank != 2) return false;
                        hsize_t dims[2];
                        fileSpace.getSimpleExtentDims(dims, nullptr);
                        const hsize_t totalRows = dims[0];

                        std::vector<std::vector<int16_t>> outData;
                        outData.resize(2);//第1列时间毫秒 第2列峰值
                        // 逐列选切片读取：每次选所有行 + 当前1列
                        for (int colIdx = 0; colIdx < 2; colIdx++) {
                            hsize_t offset[2] = {0, (hsize_t)colIdx};
                            hsize_t count[2]  = {totalRows, 1}; // 一次读1列
                            fileSpace.selectHyperslab(H5S_SELECT_SET, count, offset);

                            H5::DataSpace memSpace(1, &totalRows); // 内存空间是一维，长度为总行数
                            outData[colIdx].resize(totalRows);

                            dataset.read(outData[colIdx].data(), H5::PredType::NATIVE_INT16, memSpace, fileSpace);
                        }

                        for (int rowIdx = 0; rowIdx < totalRows; ++rowIdx){
                            qint16 timeMs = static_cast<qint16>(outData[0][rowIdx]);
                            timeTrigger.push_back(timeMs);

                            timePeak.push_back(static_cast<qint16>(outData[1][rowIdx]));
                        }

                        dataset.close();
                    }
                };

                // 写入3个通道的数据
                QVector<qint16> timeTrigger_ch[3], timePeak_ch[3];
                readChannel("wave_ch0",  timeTrigger_ch[0], timePeak_ch[0]);
                readChannel("wave_ch1", timeTrigger_ch[1], timePeak_ch[1]);
                readChannel("wave_ch2", timeTrigger_ch[2], timePeak_ch[2]);

                //根据时间段统计计数率
                QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>> cpsMapPair;
                for (quint8 cameraNo=0; cameraNo<3; ++cameraNo){
                    quint8 cameraIndex = (boardNum-1)*3 + cameraNo + 1;

                    // 1.按照时间段和点位时间间隔分配计数点数组长度
                    quint32 cpsTotal = (timeStop-timeStart)/timeWidth;
                    //cpsMapPair[cameraIndex]. reserve(cpsTotal);
                    //初始化每个时间段内计数率为0
                    for (quint16 intervalNo = 0; intervalNo <= cpsTotal; ++intervalNo) {
                        cpsMapPair[cameraIndex][timeStart + intervalNo * timeWidth] = quint32(0);
                    }

                    // 2. 遍历所有输入数据，分桶统计
                    int i = 0;
                    for (qint32 time : timeTrigger_ch[cameraNo]) {
                        if (time < timeStart)
                            continue;

                        if (time > timeStop)
                            continue;

                        if (i>timePeak_ch[cameraNo].size())
                            qDebug();

                        quint64 peak = timePeak_ch[cameraNo][i++];// 能量峰值
                        if (peak >= minPeak && peak <= maxPeak)
                            cpsMapPair[cameraIndex][timeStart + (time - timeStart) / timeWidth * timeWidth] += 1;
                    }
                }

                //根据时间段统计能谱
                QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>> spectrumMapPair;
                for (quint8 cameraNo=0; cameraNo<3; ++cameraNo){
                    quint8 cameraIndex = (boardNum-1)*3 + cameraNo + 1;

                    //初始化每个道址默认能量值为0
                    for (quint16 channel = 0; channel < channels; ++channel) {
                        spectrumMapPair[cameraIndex][channel] = quint32(0);
                    }

                    // 2. 遍历所有时间段内的能量值，分桶统计
                    int i = 0;
                    double channelWidth = (double)16384/channels;
                    for (qint32 time : timeTrigger_ch[cameraNo]) {
                        if (time < timeStart)
                            continue;

                        if (time > timeStop)
                            continue;

                        if (i>timePeak_ch[cameraNo].size())
                            qDebug();

                        quint64 peak = timePeak_ch[cameraNo][i++];// 能量峰值
                        quint16 channel = peak / channelWidth;// 道址
                        spectrumMapPair[cameraIndex][channel] += 1;
                    }
                }

                boardGroup.close();
                callback(cpsMapPair, spectrumMapPair);
            }
        }

        file.close();

        return true;
    } catch (H5::FileIException& error) {
        // 注意：这是静态函数，不能直接访问 ui，异常信息通过返回值或参数传递
        qDebug() << "HDF5 File Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::DataSetIException& error) {
        qDebug() << "HDF5 DataSet Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::DataSpaceIException& error) {
        qDebug() << "HDF5 DataSpace Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::GroupIException& error) {
        qDebug() << "HDF5 Group Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (...) {
        qDebug() << "Unknown HDF5 Exception";
        return false;
    }
}

// 从H5文件提起波形数据
bool PCIeCommSdk::takeWaveformData(const quint8& cameraIndex,
                                   const QString& filePath/*H5文件路径*/,
                                   QVector<std::array<qint16, H5_DATA_COLS>>& data)
{
    //根据通道号计算对应采集卡的第几通道
    int deviceIndex = (cameraIndex - 1) / CAMNUMBER_DDR_PER + 1;
    //根据相机序号计算出是第几块光纤卡
    quint8 cameraNo = (cameraIndex - 1) % CAMNUMBER_DDR_PER;

    QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
    QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
    try {
        H5::H5File file(filePathBytes.toStdString(), H5F_ACC_RDONLY);

        // 创建或打开板卡组
        QString boardGroupName = QString("Board%1").arg(deviceIndex);

        H5::Group boardGroup;
        htri_t existsGroup = H5Lexists(file.getId(), boardGroupName.toStdString().c_str(), H5P_DEFAULT);

        if (existsGroup > 0) {
            boardGroup = file.openGroup(boardGroupName.toStdString());

            // 辅助函数：写入单个通道的数据集
            auto readChannel = [&](const QString& datasetName) {
                htri_t existsDataset = H5Lexists(boardGroup.getId(), datasetName.toStdString().c_str(), H5P_DEFAULT);
                if (existsDataset){
                    std::string ds = datasetName.toUtf8().constData();
                    H5::DataSet dataset = boardGroup.openDataSet(ds);
                    H5::DataSpace fileSpace = dataset.getSpace();

                    // 校验维度
                    hsize_t dims[2];
                    fileSpace.getSimpleExtentDims(dims, nullptr);
                    if (dims[1] != H5_DATA_COLS) {
                        qWarning() << QString("Dataset columns is %1, required 514").arg(dims[1]);
                        return;
                    }
                    const hsize_t totalRows = dims[0];

                    // 调整容器容量，直接读取到连续内存
                    data.resize(totalRows);
                    qint16* buffer = data.front().data();
                    dataset.read(buffer, H5::PredType::NATIVE_INT16);

                    dataset.close();
                }
            };

            // 读通道的数据
            readChannel(QStringLiteral("wave_ch%1").arg(cameraNo));
            boardGroup.close();
        }

        file.close();

        return true;
    } catch (H5::FileIException& error) {
        // 注意：这是静态函数，不能直接访问 ui，异常信息通过返回值或参数传递
        qDebug() << "HDF5 File Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::DataSetIException& error) {
        qDebug() << "HDF5 DataSet Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::DataSpaceIException& error) {
        qDebug() << "HDF5 DataSpace Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::GroupIException& error) {
        qDebug() << "HDF5 Group Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (...) {
        qDebug() << "Unknown HDF5 Exception";
        return false;
    }
}

void PCIeCommSdk::replySettingFinished()
{
    quint16 deathTime = AppConfig::instance().deathTime();
    writeDeathTime(deathTime);

    quint16 triggerThreshold = AppConfig::instance().triggerThreshold();
    writeTriggerThold(triggerThreshold);

    quint32 refreshTime = AppConfig::instance().spectrumRefreshTimelength();
    writeSpectnumRefreshTimelength(refreshTime);

    quint16 triggerMode = AppConfig::instance().triggerMode();
    quint16 waveformLength = AppConfig::instance().waveformLength();
    writeWaveformMode((TriggerMode)triggerMode, (WaveformLength)waveformLength);
}

void PCIeCommSdk::writeDeathTime(quint16 deathTime)
{
    QByteArray askCurrentCmd = QByteArray::fromHex("12 34 00 0F FA 11 00 00 00 00 AB CD");
    askCurrentCmd[8] = ((deathTime >> 8) & 0xFF);
    askCurrentCmd[9] = (deathTime & 0xFF);
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeTriggerThold(quint16 triggerThold)
{
    QByteArray askCurrentCmd = QByteArray::fromHex("12 34 00 0F FA 12 00 00 00 00 AB CD");
    askCurrentCmd[8] = ((triggerThold >> 8) & 0xFF);
    askCurrentCmd[9] = (triggerThold & 0xFF);
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeWaveformMode(TriggerMode triggerMode, quint16 waveformLength)
{
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FC 10 00 00 00 00 AB CD").toUtf8());
    askCurrentCmd[7] = triggerMode;
    switch (waveformLength) {
    case 64:
        askCurrentCmd[9] = wl64;
        break;
    case 128:
        askCurrentCmd[9] = wl128;
        break;
    case 256:
        askCurrentCmd[9] = wl256;
        break;
    case 512:
        askCurrentCmd[9] = wl512;
        break;
    default:
        break;
    }
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeSpectnumRefreshTimelength(quint32 spectrumRefreshTime){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FD 10 00 00 00 00 AB CD").toUtf8());
    askCurrentCmd[6] = (spectrumRefreshTime >> 24) & 0xFF;
    askCurrentCmd[7] = (spectrumRefreshTime >> 16) & 0xFF;
    askCurrentCmd[8] = (spectrumRefreshTime >> 8)  & 0xFF;
    askCurrentCmd[9] = (spectrumRefreshTime)       & 0xFF;
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeWorkMode(WorkMode workMode){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FF 10 00 00 00 00 AB CD").toUtf8());
    askCurrentCmd[9] = workMode;
    writeCommand(askCurrentCmd);
}

/*********************************************************
     控制类指令
    ***********************************************************/
//开始测量
void PCIeCommSdk::writeStartMeasure(){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 01 AB CD").toUtf8());
    writeCommand(askCurrentCmd);
};

//停止测量
void PCIeCommSdk::writeStopMeasure(){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 00 AB CD").toUtf8());
    writeCommand(askCurrentCmd);
}

void PCIeCommSdk::writeCommand(QByteArray& data)
{
    for (int cardIndex = 1; cardIndex <= mDevices.size(); ++cardIndex){
        if (!mMapCaptureThread.contains(cardIndex))
            continue;

        HANDLE fUserHandle = PCIeCommSdk::getHandle(mDevices[cardIndex-1] + XDMA_FILE_USER,
                                                    GENERIC_READ | GENERIC_WRITE,
                                                    FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE);
        if (fUserHandle == INVALID_HANDLE_VALUE)
            continue;

        writeData(cardIndex, fUserHandle, 0x20000, data);

        CloseHandle(fUserHandle);
    }
}

bool PCIeCommSdk::writeData(quint8 cardIndex, HANDLE fd, quint64 offset, const QByteArray& data)
{
#ifdef _WIN32
    LARGE_INTEGER address;
    address.QuadPart = offset;
    if (INVALID_SET_FILE_POINTER == SetFilePointerEx(fd, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return false;
    }

    DWORD dwNumberOfBytesWritten;
    if (!WriteFile(fd, data.constData(), data.size(), &dwNumberOfBytesWritten, NULL)) {
        fprintf(stderr, "WriteFile failed with Win32 error code: %d\n", GetLastError());
        return false;
    }
 #else
    lseek(fd, offset, SEEK_SET);
    write(fd, data.data(), data.size());
#endif

    qDebug() << "[" << cardIndex << "] writeData" << QString("0x%1").arg((quint64)offset, 8, 16, QLatin1Char('0')) << data.toHex(' ');
    return true;
}

bool PCIeCommSdk::readData(HANDLE fd, quint64 offset, const QByteArray& data)
{
    //QMutexLocker locker(&mReadLocker);
#ifdef _WIN32
    LARGE_INTEGER address;
    address.QuadPart = offset;
    if (INVALID_SET_FILE_POINTER == SetFilePointerEx(fd, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return false;
    }

    // static char* buffer = nullptr;
    // if (nullptr == buffer)
    //     buffer = new char[120000000];
    // LARGE_INTEGER start;
    // LARGE_INTEGER stop;
    // LARGE_INTEGER freq;
    // QueryPerformanceFrequency(&freq);
    // QueryPerformanceCounter(&start);

    {
        DWORD nNumberOfBytesToRead = data.size();
        DWORD nNumberOfBytesRead = 0;
        //memset((void*)data.data(), 0, data.size());
        if (!ReadFile(fd, (void*)data.data(), nNumberOfBytesToRead, &nNumberOfBytesRead, NULL)) {
            //if (!ReadFile(fd, (void*)buffer, 120000000, &nNumberOfBytesRead, NULL)) {
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
            return false;
        }
    }

    // if (nNumberOfBytesToRead != nNumberOfBytesRead){
    //     qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
    //     return false;
    // }

    //delete[] buffer;
    // QueryPerformanceCounter(&stop);
    // double time_usec = (double)(stop.QuadPart - start.QuadPart) / (double)freq.QuadPart;
    // qDebug().nospace() << data.size() << " bytes received in " << time_usec << "s";
#else
    lseek(fd, offset, SEEK_SET);
    read(fd, (char*)data.data(), data.size());
#endif

    return true;
}

HANDLE PCIeCommSdk::getHandle(quint8 cardIndex, quint32 flags/* = GENERIC_READ | GENERIC_WRITE*/, quint32 dwFlagsAndAttributes)
{
    return PCIeCommSdk::getHandle(mDevices.at(cardIndex), flags, dwFlagsAndAttributes);
}

HANDLE PCIeCommSdk::getHandle(QString path, quint32 dwDesiredAccess, quint32 dwFlagsAndAttributes)
{
#ifdef _WIN32
    //FILE_FLAG_SEQUENTIAL_SCAN
    //FILE_FLAG_NO_BUFFERING    // 增加FILE_FLAG_NO_BUFFERING标志，跳过内核缓存，直接读取
    HANDLE fd = CreateFileA(path.toStdString().c_str(),
                            dwDesiredAccess/*GENERIC_READ | GENERIC_WRITE*/,
                            0,
                            NULL,
                            OPEN_EXISTING,
                            dwFlagsAndAttributes,
                            NULL);
#else
    int fd = open(devicePath.toStdString().c_str(), flags/*O_RDWR*/);
    //int fd_usr = open("/dev/xdma0_user",O_RDWR);
#endif

    return fd;
}

/*根据卡名称判断卡序号*/
quint8 PCIeCommSdk::boardNameToBoardIndex(const QString& name)
{
    // if (i==0)
    //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&B189E7&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";
    // else if (i==1)
    //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&10D89B97&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";
    // else if (i==2)
    //     devicePath = "\\\\?\\PCI#VEN_10EE&DEV_9038&SUBSYS_000710EE&REV_00#4&18ec5e6b&0&0020#{74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d}\\c2h_3";

    if (name.toUpper().contains(QStringLiteral("B189E7")))
        return 1;
    else if (name.toUpper().contains(QStringLiteral("10D89B97")))
        return 2;
    else if (name.toUpper().contains(QStringLiteral("18EC5E6B")))
        return 3;
    else
        return 0;
}


void PCIeCommSdk::initCaptureThreads()
{
    for (int deviceIndex = 1; deviceIndex <= numberOfDevices() * 2; ++deviceIndex)
    {
        bool isDDR1 = deviceIndex <= numberOfDevices();
        quint8 cardIndex = (deviceIndex - 1) % numberOfDevices() + 1;
        quint8 boardIndex = boardNameToBoardIndex(mDevices.at(cardIndex - 1));
        if (boardIndex == 0)
            continue;

        AppConfig::instance().setBoardState(boardIndex, true);
        CaptureThread *captureThread = new CaptureThread(boardIndex, mDevices.at(cardIndex-1), isDDR1);
        captureThread->setPriority(QThread::Priority::HighPriority);
        connect(captureThread, &CaptureThread::reportThreadExit, this, [=](quint32 index){
            mMapCaptureThread.remove(index);
            mThreadRunning[index] = false;
        });
        connect(captureThread, &CaptureThread::reportCaptureFinished, this, [=](quint32 cardIndex, bool isDDR1){
            mThreadRunning[deviceIndex] = false;

            bool allCaptureFinished = true;
            for (int deviceIndex = 1; deviceIndex <= numberOfDevices() * 2; ++deviceIndex)
            {
                if (mThreadRunning[deviceIndex])
                {
                    allCaptureFinished = false;
                    break;
                }
            }

            if (allCaptureFinished)
            {
                emit reportCaptureFinished();
                qInfo() << "数据采集完毕！";
            }
        }, Qt::DirectConnection);

    // 设置线程亲和性(小于64核)
#ifdef _WIN32
        SetThreadAffinityMask(captureThread->currentThreadId(), deviceIndex << 1ULL);
#else
        cpu_set_t mask;// cpu核的集合
        CPU_ZERO(&mask);// 将集合置为空集
        CPU_SET(cardIndex, &mask);// 设置亲和力值
        sched_setaffinity(0,sizeof(cpu_set_t),&mask);// 设置线程cpu亲和力

#endif

        mMapCaptureThread[deviceIndex] = captureThread;
        mMapCaptureThread[deviceIndex]->start();
    }
}

QByteArray PCIeCommSdk::reverseArray(const QByteArray& data, quint8 offset)
{
    QByteArray result;
    for (int i=0; i<data.size()/offset; ++i){
        QByteArray block = data.mid(i*offset, offset);
        std::reverse(block.begin(), block.end());
        result.append(block);
    }

    return result;
}

/**
 * CaptureThread 采集数据线程====================================================
*/

/**
* @function name: CaptureThread
* @brief 构造函数
* @param[in]    cardIndex 采集卡索引
* @param[in]    hFile DDR内存句柄
* @param[in]    hUser Fpga控制指令句柄
* @param[in]    hBypass DDR内存句柄
* @param[in]    saveFilePath 文件保存路径
* @param[in]    captureTimeSeconds 采集时长
* @param[out]
* @return
*/
// 分配大页内存（需要管理员权限运行程序）
static char* allocLargePhysicsPage(int size) {
    // 获取大页最小粒度
    ULONG largePageSize = GetLargePageMinimum();
    // 对齐大小到大页粒度
    int alignedSize = (size + largePageSize - 1) & ~(largePageSize - 1);
    // 分配大页内存
    char* buffer = reinterpret_cast<char*>(VirtualAlloc(NULL,
                                                      alignedSize,
                                                      MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES,
                                                      PAGE_READWRITE));
    return buffer;
}

static char* allocVirtualAlignedBuffer(size_t size,size_t alignment = 0){
    if (alignment == 0){
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        alignment = sys_info.dwPageSize;
    }

    char* buffer = (char*)_aligned_malloc(size, alignment);
    if (buffer){
        for (int i=0; i<size; i+=alignment){
            buffer[i] = 0;
        }
    }

    return buffer;
}

static char* allocPhysicsAlignedBuffer(size_t size){
    char* buffer = reinterpret_cast<char*>(VirtualAlloc(NULL,
                                                         size,
                                                         MEM_COMMIT | MEM_RESERVE,
                                                         PAGE_READWRITE));
    if (buffer){
        bool lockOk= VirtualLock(buffer, size);
        if (!lockOk){
            // 内存锁定失败，通过逐页访问确保所有页都被提交到物理内存
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            const DWORD PAGE_SIZE = sysInfo.dwPageSize; // 使用系统实际页面大小更可靠
            for (int i=0; i<size; i+=PAGE_SIZE){
                buffer[i] = 0;
            }
        }
    }

    return buffer;
}

#include <processthreadsapi.h>
static bool enableLargePagePrivilege(){
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)){
        return false;
    }

    if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid)){
        CloseHandle(hToken);
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL)){
        CloseHandle(hToken);
        return false;
    }

    CloseHandle(hToken);
    return TRUE;
}
static char* allocLargePhysicalAlignedBuffer(size_t size){
    ULONG largePageSize = GetLargePageMinimum();
    int alignedSize = (size + largePageSize - 1) & ~(largePageSize - 1);
    char *buf = reinterpret_cast<char*>(VirtualAlloc(NULL,
                                                      alignedSize,
                                                      MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES,
                                                      PAGE_READWRITE));
    return buf;
}

CaptureThread::CaptureThread(const quint32 cardIndex, const QString& devicePath, bool isDDR1/* = true*/)
    : mDevPath(devicePath)
    , mCardIndex(cardIndex)
    , mIsDDR1(isDDR1)
{
#if ENABLE_IOCP
    mPcieReader = new PcieIocpReader(this);
    // 初始化：4个分区，总大小800MB，每个分区默认200MB
    if (!mPcieReader->init(devicePath + XDMA_FILE_C2H_0, !isDDR1)) {
        delete mPcieReader;
        mPcieReader = nullptr;
    }
    for (int i=0; i<=3; ++i){
//        if (!mPcieReader->init(devicePath + XDMA_FILE_C2H + QStringLiteral("_%1").arg(i), 1, 0x07270E00)) {
//            delete mPcieReader;
//            mPcieReader = nullptr;
//            break;
//        }
    }
#else
#endif //ENABLE_IOCP

    int capacity = 260;// 40ms, 4s共100帧，10s共250帧
    mDDRWaveformDatas.reserve(capacity);
    mRAMSpectrumDatas.reserve(capacity);
    try{
        for (int i=0; i < capacity; ++i){
            // mDDRWaveformDatas.push_back(QByteArray(0x07270E00, 0));//200MB=0x0BEBC200 150MB=0x09600000 12*10e70=0x07270E00
            // mRAMSpectrumDatas.push_back(QByteArray(0xA000, 0));
            {
                char* alignedBuffer = allocPhysicsAlignedBuffer(0x07270E00);//allocate_buffer(0x07270E00, 4096);
                if (!alignedBuffer)
                    qCritical() << i+1 << "allocate_buffer fail. err:" << GetLastError();
                else{
                    if ((ULONG64)alignedBuffer % 4096 != 0)
                        qCritical() << i+1 << "allocate_buffer align fail.";
                }

                mDDRWaveformDatas.push_back(QByteArray::fromRawData(alignedBuffer, 0x07270E00));
            }

            {
                char* alignedBuffer = allocPhysicsAlignedBuffer(0xA000);//allocate_buffer(0xA000, 4096);
                if (!alignedBuffer)
                    qCritical() << i+1 << "allocate_buffer fail. err:" << GetLastError();

                mRAMSpectrumDatas.push_back(QByteArray::fromRawData(alignedBuffer, 0xA000));
            }
        }
    }
    catch (const std::bad_alloc& e){
        qDebug() << "Memory allocation failed:" << e.what();
    }

    connect(this, &QThread::finished, this, &QThread::deleteLater);
}

CaptureThread::~CaptureThread()
{
    requestInterruption();
    quit();
    wait();

    for (auto& buf : mDDRWaveformDatas) {
        VirtualUnlock(buf.data(), buf.size());
        VirtualFree(buf.data(), 0, MEM_RELEASE);
        //_aligned_free(buf.data());
    }
    for (auto& buf : mRAMSpectrumDatas) {
        VirtualUnlock(buf.data(), buf.size());
        VirtualFree(buf.data(), 0, MEM_RELEASE);
        //_aligned_free(buf.data());
    }

    //关闭句柄
#if ENABLE_IOCP
    if (mPcieReader) {
        mPcieReader->stop();
        delete mPcieReader;
        mPcieReader = nullptr;
    }
#else
#endif //ENABLE_IOCP
}

void CaptureThread::setParamter(const QString &saveFilePath, quint32 captureTimeSeconds, bool testMode)
{
    this->mSaveFilePath = saveFilePath;
    this->mCaptureCount = quint32((double)captureTimeSeconds / 40.0 + 0.4);
    this->mEnableTestMode = testMode;
}

bool CaptureThread::startMeasure()
{
    this->mInterruptSave = false;
    qInfo().nospace() << "[" << mCardIndex << "] " << "发送开始测量指令";
    auto writeData = [=](quint8 cardIndex, HANDLE fd, quint64 offset, const char* data, int size){
#ifdef _WIN32
        LARGE_INTEGER address;
        address.QuadPart = offset;
        if (INVALID_SET_FILE_POINTER == SetFilePointerEx(fd, address, NULL, FILE_BEGIN)) {
            fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
            return false;
        }

        DWORD dwNumberOfBytesWritten;
        if (!WriteFile(fd, data, size, &dwNumberOfBytesWritten, NULL)) {
            fprintf(stderr, "WriteFile failed with Win32 error code: %d\n", GetLastError());
            return false;
        }
#else
        lseek(fd, offset, SEEK_SET);
        write(fd, data.data(), data.size());
#endif

        return true;
    };

    HANDLE fHandle = PCIeCommSdk::getHandle(mDevPath + XDMA_FILE_USER,
                                         GENERIC_READ | GENERIC_WRITE,
                                         FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (fHandle == INVALID_HANDLE_VALUE)
        return false;

    //QByteArray cmdClear = QByteArray::fromHex("00 00 00 00");
    char cmdClear[4] = {0x00, 0x00, 0x00, 0x00};
    writeData(mCardIndex, fHandle, 0x20000, cmdClear, 4);

    //::QThread::msleep(1);
    this->delay(1000);

    // QByteArray cmdStart = QByteArray::fromHex("01 E0 34 12");
    unsigned char cmdStart[4] = {0x01, 0xE0, 0x34, 0x12};
    writeData(mCardIndex, fHandle, 0x20000, (const char*)cmdStart, 4);

    CloseHandle(fHandle);
    return true;
}

void CaptureThread::stopMeasure()
{
    mInterruptSave = true;
}

void CaptureThread::clear()
{
    HANDLE fHandle = PCIeCommSdk::getHandle(mDevPath + XDMA_FILE_USER,
                                            GENERIC_READ | GENERIC_WRITE,
                                            FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (fHandle == INVALID_HANDLE_VALUE)
        return ;

    PCIeCommSdk::writeData(mCardIndex, fHandle, 0x20000, QByteArray::fromHex("00 00 00 00"));
    CloseHandle(fHandle);
}

void CaptureThread::empty()
{
    HANDLE fHandle = PCIeCommSdk::getHandle(mDevPath + XDMA_FILE_USER,
                                            GENERIC_READ | GENERIC_WRITE,
                                            FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (fHandle == INVALID_HANDLE_VALUE)
        return ;

    PCIeCommSdk::writeData(mCardIndex, fHandle, 0x20000, QByteArray::fromHex("01 F0 34 12"));
    CloseHandle(fHandle);
}

#include <QEventLoop>
void CaptureThread::delay(quint32 us)
{
    LARGE_INTEGER start;
    LARGE_INTEGER stop;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    while (true) {
        QueryPerformanceCounter(&stop);
        quint32 time_usec = (unsigned long long)(stop.QuadPart - start.QuadPart) / (double)freq.QuadPart * 1e6;
        if (time_usec >= us)
            break;

        qApp->processEvents();
    }
}

bool CaptureThread::checkDataError()
{
    if (mInterruptSave)
        return false;

    QString ddrName = (mIsDDR1 ? "DDR1" : "DDR2");
    bool waveformError = false;
    bool spectrumError = false;
    int waveformErrorIndex = 0;
    int spectrumErrorIndex = 0;

    // 检测波形序号
    int lastSeq = 0;
    for (int i=0; i<this->mCapturedRef; ++i){
        QByteArray pkgHead = PCIeCommSdk::reverseArray(mDDRWaveformDatas.at(i).left(12));
        QByteArray pkgTail = PCIeCommSdk::reverseArray(mDDRWaveformDatas.at(i).right(12));
        int headSeq = (quint8)pkgHead[7];
        int tailSeq = (quint8)pkgTail[7];
        int currentSeq = headSeq;
        if (i>=255)
            currentSeq += 256;

        if ((headSeq != tailSeq) || ((currentSeq - lastSeq) != 1)){
            if (headSeq!=0){
                waveformErrorIndex = i+1;
                waveformError = true;
                break;
            }
        }

        lastSeq = currentSeq;
    }

    // 检测能谱序号
    lastSeq = 0;
    for (int i=0; i<this->mCapturedRef; ++i){
        QByteArray pkgHead = PCIeCommSdk::reverseArray(mRAMSpectrumDatas.at(i).left(16), 16);
        bool ok = false;
        int currentSeq = pkgHead.mid(4, 2).toHex().toUInt(&ok, 16);
        if ((currentSeq - lastSeq) != 1){
            spectrumErrorIndex = i+1;
            spectrumError = true;
            break;
        }

        lastSeq = currentSeq;
    }

    mDataError = waveformError | spectrumError | mIsRegisterInvalid;
    printDebugInfo();

    if (!waveformError && !mIsRegisterInvalid){
        qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 波形正常";
        qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << " 波形正常";
    }
    else{
        if (waveformError){
            qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 波形帧序号异常, 索引 = " << waveformError;
            qCritical().nospace() << "[" << mCardIndex << "] " << ddrName << " 波形帧序号异常, 索引 = " << waveformErrorIndex;
        }
        else if (mIsRegisterInvalid){
            qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 波形寄存器值异常, 索引 = " << mRegisterInvalidPosition;
            qCritical().nospace() << "[" << mCardIndex << "] " << ddrName << " 波形寄存器值异常, 索引 = " << mRegisterInvalidPosition;
        }
    }

    if (!spectrumError){
        qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 能谱正常";
        qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << " 能谱正常";
    }
    else{
        qDebug().nospace() << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> [" << mCardIndex << "] " << ddrName << " 能谱帧序号异常, 索引 = " << spectrumErrorIndex;
        qCritical().nospace() << "[" << mCardIndex << "] " << ddrName << " 能谱帧序号异常, 索引 = " << spectrumErrorIndex;
    }

    if (!mEnableTestMode/*测试模式*/ || mDataError/*数据出现错误*/){

        qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "数据正在存储到硬盘中，请等待...";

        for (int i = 0; i < mCapturedRef; ++i)
        {
            if (mInterruptSave)
                break;

            {
                QString filename = QString("%1/%2%3data%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mIsDDR1 ? 'A' : 'B').arg(i+1);
                QFile file(filename);
                if (!file.open(QIODevice::WriteOnly)) {
                    qDebug() << "Cannot open file for writing";
                }
                else{
                    file.write(mDDRWaveformDatas.at(i));
                    file.close();
                }
            }

            {
                QString filename = QString("%1/%2%3spec%4.bin").arg(mSaveFilePath).arg(mCardIndex).arg(mIsDDR1 ? 'A' : 'B').arg(i+1);
                QFile file(filename);
                if (!file.open(QIODevice::WriteOnly)) {
                    qDebug() << "Cannot open file for writing";
                }
                else{
                    file.write(mRAMSpectrumDatas.at(i));
                    file.close();
                }
            }

        }

        qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "数据已经全部存储到硬盘中！";
    }

    // 清空内存耗时，暂时屏蔽
    // for (int i=0; i<this->mCapturedRef; ++i){
    //     memset(mDDRWaveformDatas[i].data(), 0, mDDRWaveformDatas[i].size());
    //     memset(mRAMSpectrumDatas[i].data(), 0, mRAMSpectrumDatas[i].size());
    // }

    return mDataError;
}

void CaptureThread::printDebugInfo()
{
    QString ddrName = (mIsDDR1 ? "DDR1" : "DDR2");
    bool ok = true;
    int lastSeq = 0;
    int index = 0;

    QFile logFile;
    if (mDataError || mIsRegisterInvalid)
        logFile.setFileName(QStringLiteral("%1/%2_%3_%4.err").arg(mSaveFilePath).arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss")).arg(mCardIndex).arg(ddrName));
    else
        logFile.setFileName(QStringLiteral("%1/%2_%3_%4.inf").arg(mSaveFilePath).arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss")).arg(mCardIndex).arg(ddrName));
    logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    QTextStream out(&logFile);
    // out << QString::fromUtf8("\xEF\xBB\xBF");
    // out.setCodec(QTextCodec::codecForName("utf8"));// "UTF-8");

    // 先输出包头信息
    for (int i=0; i<this->mCapturedRef; ++i){
        QByteArray pkgHead = PCIeCommSdk::reverseArray(mDDRWaveformDatas.at(i).left(12));
        QByteArray pkgTail = PCIeCommSdk::reverseArray(mDDRWaveformDatas.at(i).right(12));
        int headSeq = (quint8)pkgHead[7];
        int tailSeq = (quint8)pkgTail[7];

        int currentSeq = (quint8)headSeq;
        if (i>=255)
            currentSeq += 256;

        if (((headSeq != tailSeq) || ((currentSeq - lastSeq) != 1)) && headSeq!=0){
            index = i+1;
            ok = false;
            out << "[" << mCardIndex << "] "
                << ddrName << " [" << i+1 << "] "
                << pkgHead.toHex(' ')
                << " ... "
                << pkgTail.toHex(' ')
                << "    xxxxxx"  << "\n";
        } else{
            out << "[" << mCardIndex << "] "
                << ddrName << " [" << i+1 << "] "
                << pkgHead.toHex(' ')
                << " ... "
                << pkgTail.toHex(' ')
                << "\n";
        }

        lastSeq = currentSeq;
    }

    out << "================================================================================================\n\n";

    // 寄存器0值出现的时间段
    out << QStringLiteral("数据来到之前寄存器0值出现的时间段\n");
    for (int i = 0; i < zeroTime.size(); ++i){
        out << "[" << mCardIndex << "] " << ddrName << " " << zeroTime[i] << "\n";
    }

    // 读寄存器时间
    for (const auto &outerPair : mRAMReadTime.toStdMap()){
        quint16 outerKey = outerPair.first;
        const auto& pairVec = outerPair.second;
        for (int idx=0; idx<pairVec.size(); ++idx){
            const auto& pair = pairVec[idx];
            out << "[" << mCardIndex << "] " << ddrName << " " << outerKey << " " <<
                QStringLiteral(" 读寄存器时刻：") << pair.first << "-" << pair.second.first <<
                QStringLiteral(" 返回值：0x") << QString::number(pair.second.second, 16) << "\n";
        }
    }

    out << "================================================================================================\n\n";
    // 读数据时间
    for (int i=1; i<=this->mCapturedRef; ++i){
        out << "[" << mCardIndex << "] " << ddrName << " " << i <<
            QStringLiteral(" 寄存器值变化时刻：") << mRamChangedTime[i] <<
            QStringLiteral(" 创建读线程时刻：") << mCreateThreadTime[i] << "-" << mAfterCreateThreadTime[i] <<
            QStringLiteral(" 开始读数据时刻：") << mBeforeReadTime[i] <<
            QStringLiteral(" 读完数据时刻：") << mAfterReadTime[i] <<
            QStringLiteral(" 开始读延迟：") << (mRamChangedTime[i]-i*40) <<
            QStringLiteral(" 读花费时间：") << (mAfterReadTime[i]-mBeforeReadTime[i]) <<
            QStringLiteral(" 累积延迟：") << (mAfterReadTime[i]-i*40) << (((mAfterReadTime[i]-i*40)>120) ? " ******\n" : "\n");
    }

    out << "================================================================================================\n\n";
    if (!ok){
        out << "[" << mCardIndex << "] " << ddrName << " frames sequence exception, index = " << index;
    }

    if (mIsRegisterInvalid){
        out << "[" << mCardIndex << "] " << ddrName << " frames ramvalue exception, index = " << mRegisterInvalidPosition;
    }

    logFile.close();
}

bool CaptureThread::dataExistError()
{
    return mDataError || mIsRegisterInvalid;
}

// 无缓存写入文件函数
// 参数：filePath - 文件路径，data - 要写入的数据，size - 数据大小（必须是扇区大小的整数倍，通常是512或4096字节）
bool CaptureThread::writeFileWithNoBuffering(const QString &filePath, const char *data, qint64 size)
{
    // 1. 获取磁盘扇区大小（必须按扇区对齐写入，否则无缓存模式会失败）
    DWORD bytesPerSector = 0;
    QString drive = filePath.left(2) + "\\"; // 提取盘符如 "C:\\"
    if (!GetDiskFreeSpaceA(drive.toLocal8Bit().constData(), nullptr, &bytesPerSector, nullptr, nullptr)) {
        qWarning() << "获取扇区大小失败，错误码：" << GetLastError();
        return false;
    }

    // 校验数据大小必须是扇区大小的整数倍
    if (size % bytesPerSector != 0) {
        qWarning() << "数据大小必须是扇区大小的整数倍，当前扇区大小：" << bytesPerSector;
        return false;
    }

    // 2. 用Windows API创建带无缓存标记的文件句柄
    HANDLE hFile = CreateFileW(
        (LPCWSTR)filePath.utf16(),
        GENERIC_WRITE,
        0, // 独占访问
        nullptr,
        CREATE_ALWAYS, // 覆盖现有文件，需要追加用OPEN_ALWAYS
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, // 无缓存+直写，完全绕过系统缓存
        nullptr
        );

    if (hFile == INVALID_HANDLE_VALUE) {
        qWarning() << "创建文件失败，错误码：" << GetLastError();
        return false;
    }

    // 3. 将原生句柄交给QFile管理
    QFile file;
    if (!file.open(reinterpret_cast<intptr_t>(hFile), QIODevice::WriteOnly, QFile::AutoCloseHandle)) {
        qWarning() << "QFile打开原生句柄失败：" << file.errorString();
        CloseHandle(hFile);
        return false;
    }

    // 4. 写入数据（QFile的写入接口和普通使用完全一致）
    qint64 written = file.write(data, size);
    file.flush();
    file.close();

    return written == size;
}

void CaptureThread::run()
{
    qRegisterMetaType<QByteArray>("QByteArray");
    qRegisterMetaType<QVector<QPair<double,double>>>("QVector<QPair<double,double>>");
    //提升线程优先级
    timeBeginPeriod(1);// 可提高精度到2ms 需引用-lwinmm

    const quint32 PACKING_DURATION = 50; // 打包时长
    const int IRQ_WAIT_TIMEOUT = 1;      // 中断等待超时（ms）
    const std::chrono::microseconds IRQ_POLL_INTERVAL = std::chrono::microseconds(1);

    quint64 memOffet = mIsDDR1 ? 0 : 0x40000000;
    quint64 memRamOffet = mIsDDR1 ? 0 : 0x40000;

    QElapsedTimer elapsedTimer;
    QByteArray readBuf(1, 0);
    qint64 t0 = 0;
    elapsedTimer.start();

    QMutex timeMutex;
    quint8 lastRegisterValue = 0x00;// 最后寄存器值

    qDebug().nospace() << "[" << mCardIndex << (mIsDDR1 ? "] DDR1" : "] DDR2") << " 数据采集线程 id:" << this->currentThreadId();

    // 创建线程池
    QThreadPool* threadPool = new QThreadPool(this);
    // 获取默认最大线程数（等于 CPU 核心数）
    //int defaultThreads = QThreadPool::globalInstance()->maxThreadCount();
    int defaultThreads = QThread::idealThreadCount() * 2;//()->maxThreadCount();
    // 为 I/O 密集型任务调整最大线程数
    threadPool->setMaxThreadCount(defaultThreads);
    //threadPool->setExpiryTimeout(-1);//过期超时不收回
	QString ddrName = (mIsDDR1 ? "DDR1" : "DDR2");

    mRamChangedTime.resize(500);// RAM值改变的时间
    mBeforeReadTime.resize(500);// DDR读之前的时间
    mAfterReadTime.resize(500);// DDR读之后的时间
    mCreateThreadTime.resize(500);// DDR读之后的时间
    mAfterCreateThreadTime.resize(500);// DDR读之后的时间

    // 设置线程为实时优先级，确保采集不被打断
    //SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // 卡0绑定核心2，卡1绑定核心3，...，卡5绑定核心7
    //DWORD_PTR affinityMask = 1LL << (mCardIndex + 2);
    //SetThreadAffinityMask(GetCurrentThread(), affinityMask);
    //SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    while (true)
    {
        if (mIsStopped.load())
            break;

        if (mIsPaused.load()){
            QThread::usleep(100);
            continue;
        }
        qDebug().noquote() << "[" << QString("0x%1").arg((quint64)QThread::currentThreadId(), 8, 16, QLatin1Char('0')) << "]"
                           << "[" << mCardIndex << "] "
                           << ddrName
                           << " 苏醒";

        HANDLE fUserHandle = PCIeCommSdk::getHandle(mDevPath + XDMA_FILE_USER,
                                                GENERIC_READ | GENERIC_WRITE,
                                                FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE);
        if (fUserHandle == INVALID_HANDLE_VALUE)
        {
            qCritical().noquote() << "[" << QString("0x%1").arg((quint64)QThread::currentThreadId(), 8, 16, QLatin1Char('0')) << "]"
                               << "[" << mCardIndex << "] "
                               << ddrName
                               << " 设备打开失败";
            pause();
            continue ;
        }

        readBuf[0] = 0u;
        bool isFirstPacket = true;// 第一个数据包标识
        bool isOver = false;// 结束指令标识
        bool isTimeout = false;// 超时标识
        bool isPrintedTimeoutInfo = false; // 超时消息是否已经打印过一次

        mCapturedRef = 1;
        mIsRegisterInvalid = false;
        mRegisterInvalidPosition = 0;
        lastRegisterValue = 0x00;
        quint64 offsetRegister = mIsDDR1 ? 0x0 : 0x10000;

        mRamChangedTime.fill(0);
        mBeforeReadTime.fill(0);
        mAfterReadTime.fill(0);
        mCreateThreadTime.fill(0);
        mAfterCreateThreadTime.fill(0);
        mDDRReadTime.clear();
        mRAMReadTime.clear();
		zeroTime.clear();

        if (mIsDDR1){ // 1张卡只需要发一次开始测量指令
            startMeasure();
        }
        else {
            // 只测试DDR1
            delay(500);
        }

        // pause();
        // continue;

        elapsedTimer.start();
        qDebug().nospace() << "[" << mCardIndex << "] "
                           << ddrName
                           << " 开始测量>>>>>>>>>>>>>>>>>>>>>>>>"
                           << elapsedTimer.elapsed();

        for (;mCapturedRef <= this->mCaptureCount/* && !isTimeout*//*超时是否继续*/; ++mCapturedRef)
        {
            quint32 failCount = 0;
            isTimeout = false;

            while (true)
            {
                readBuf[0] = 0u;
                qint64 innerKey = elapsedTimer.elapsed();
                if (PCIeCommSdk::readData(fUserHandle, offsetRegister, readBuf))
                {
                    // 记录读数前后时刻
                    qint64 outnerKey = elapsedTimer.elapsed();
                    mRAMReadTime[mCapturedRef].push_back(qMakePair(innerKey, qMakePair(outnerKey, (quint8)readBuf[0])));
                    if ((quint8)readBuf[0] == 0x00){
                        // 数据还没准备好
                        if (!isFirstPacket){
                            mRamChangedTime[mCapturedRef] = outnerKey;
                            lastRegisterValue = (quint8)readBuf[0];
                            isOver = true;
                            qDebug().noquote().nospace() << "[" << mCardIndex << "] "
                                                  << ddrName
                                                  << " 收到停止测量指令"
                                                  << " 当前寄存器值:0x" << readBuf.toHex()
                                                  << " 帧序号:" << mCapturedRef;// 测试停止，最后一个数据包序号无效，所以减1
                            break;
                        }
                        else{
                            // 记录正式数据包之前，寄存器值0x00出现的时间
                            zeroTime.push_back(outnerKey);
                        }
                    }
                    else if ((quint8)readBuf[0] == 0x10){
                        if (!isFirstPacket){
                            // 第1个0x10表示数据准备中，从第2个开始表示第4块区域数据已经准备就绪
                            if (lastRegisterValue != (quint8)readBuf[0]){
                                // 寄存器值发生改变
                                isFirstPacket = false;
                                lastRegisterValue = (quint8)readBuf[0];
                                mRamChangedTime[mCapturedRef] = outnerKey;
                                break;
                            }
                        }
                    }
                    else if ((quint8)readBuf[0] == 0x18)
                    {
                        // 收到停止测量指令
                        if (!isFirstPacket && mCapturedRef == 260/*极限值*/){
                            mRamChangedTime[mCapturedRef] = outnerKey;
                            lastRegisterValue = (quint8)readBuf[0];
                            qDebug().noquote().nospace() << "[" << mCardIndex << "] "
                                                  << ddrName
                                                  << " 收到停止测量指令"
                                                  << " 当前寄存器值:0x" << readBuf.toHex()
                                                  << " 帧序号:" << mCapturedRef;// 测试停止，最后一个数据包序号无效，所以减1
                            break;
                        }
                    }
                    else if (lastRegisterValue != (quint8)readBuf[0]){
                        // 寄存器值发生改变
                        isFirstPacket = false;
                        lastRegisterValue = (quint8)readBuf[0];
                        mRamChangedTime[mCapturedRef] = outnerKey;
                        break;
                    }
                }
                else
                {
                    isOver = true;

                    // 读数失败了(从未进入到这里，如果进入到这里，肯定是硬件出问题了)
                    qCritical().nospace() << "[" << mCardIndex << "] "
                                          << ddrName << "读寄存器失败，测量时刻：" << elapsedTimer.elapsed();
                    break;
                }

                if (++failCount>=50)//50ms
                {
                    isTimeout = true;
                    mIsRegisterInvalid = true;
                    if (!isPrintedTimeoutInfo){ //一旦超时，后面数据可能都超时，所以这里只打印一次消息即可
                        isPrintedTimeoutInfo = true;
                        mRegisterInvalidPosition = mCapturedRef;
                        qCritical().noquote().nospace() << "[" << mCardIndex << "] "
                                          << ddrName << "寄存器值异常"
                                          << " 当前寄存器值:0x" << readBuf.toHex()
                                          << " 帧序号:" << mCapturedRef;
                    }
                    break;
                }

                //QThread::msleep(1);//精度2ms
                delay(1000);//精度1us
            }

            if (isOver)
                break;

            if (isTimeout){
                continue;
            }

#if ENABLE_IOCP
            mPcieReader->submitReadRequestByStep(readBuf[0]);
#else
            //读原始数据
            quint16 capturedRef = mCapturedRef;
            quint8 step = 0;
            if (lastRegisterValue == 0x11)
                step = 0;
            else if (lastRegisterValue == 0x12)
                step = 1;
            else if (lastRegisterValue == 0x14)
                step = 2;
            else if (lastRegisterValue == 0x18 || lastRegisterValue == 0x10)
                step = 3;
            else
                continue;

			mCreateThreadTime[capturedRef] = elapsedTimer.elapsed();
            threadPool->start([=](){
                // 设置线程为实时优先级，确保采集不被打断
                //SetThreadPriority(QThread::currentThreadId(), THREAD_PRIORITY_TIME_CRITICAL);

                // 绑定到固定CPU核心，避免核心切换开销
                //SetThreadAffinityMask(QThread::currentThreadId(), step << 1ULL);

                mBeforeReadTime[capturedRef] = elapsedTimer.elapsed();
                //读波形数据
                readWaveformData(step, mDDRWaveformDatas.at(capturedRef-1), memOffet + step * 0x07271400);
                //读能谱数据
                readSpectrumData(step, mRAMSpectrumDatas.at(capturedRef-1), memRamOffet + step * 0xA000);
				mAfterReadTime[capturedRef] = elapsedTimer.elapsed();

            }, QThread::HighestPriority);

            mAfterCreateThreadTime[capturedRef] = elapsedTimer.elapsed();
        }
#endif //ENABLE_IOCP

        CloseHandle(fUserHandle);

        if (!isOver)// 循环次数到了，最后一个异常数据包计数减1
            mCapturedRef--;

        {
            // if (mIsDDR1){
            //     this->clear();
            // }

            //emit reportCaptureFinished(mCardIndex, mIsDDR1);

            threadPool->waitForDone();
            qInfo().nospace() << "[" << mCardIndex << "] " << ddrName << "采集结束，共采集：" << mCapturedRef;

            checkDataError();
            emit reportCaptureFinished(mCardIndex, mIsDDR1);

            pause();
            // 暂停逻辑
            {
                qDebug().noquote() << "[" << QString("0x%1").arg((quint64)QThread::currentThreadId(), 8, 16, QLatin1Char('0')) << "]"
                                   << "[" << mCardIndex << "] "
                                   << ddrName
                                   << " 休眠";
            }

            continue;
        }
    }

    timeEndPeriod(1);

    // 报告线程退出
    emit reportThreadExit(mCardIndex);
    qDebug() << "destroyCaptureThread id:" << this->currentThreadId();
}

bool CaptureThread::readDataAsync(HANDLE fd, quint64 offset, const QByteArray& data)
{
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(nullptr, true, false, nullptr);
    overlapped.Offset = offset;

    DWORD nNumberOfBytesToRead = data.size();
    DWORD nNumberOfBytesRead = 0;
    if (!ReadFile(fd, (void*)data.data(), nNumberOfBytesToRead, &nNumberOfBytesRead, &overlapped)) {
        if (GetLastError() == ERROR_IO_PENDING){
            qDebug() << "ReadFile fail, win32 error code:" << GetLastError();
            return false;
        }

        WaitForSingleObject(overlapped.hEvent, 120/*INFINITE*/);
        // if (WaitForSingleObject(overlapped.hEvent, 120000/*INFINITE*/) != WAIT_OBJECT_0){
        //     qDebug() << "WaitForSingleObject fail, win32 error code:" << GetLastError();
        //     CloseHandle(overlapped.hEvent);
        //     return false;
        // }

        if (!GetOverlappedResult(fd, &overlapped, &nNumberOfBytesRead, false)){
            qDebug() << "GetOverlappedResult fail, win32 error code:" << GetLastError();
            CloseHandle(overlapped.hEvent);
            return false;
        }
    }

    ResetEvent(overlapped.hEvent);
    CloseHandle(overlapped.hEvent);

    return true;
}

/**
* @function name:readWaveformData
* @brief 从DDR读波形原始数据
* @param[in]        data
* @param[out]       data
* @return           void
*/
bool CaptureThread::readWaveformData(quint8 index, const QByteArray& data, const quint64 offset)
{
    HANDLE fHandle = PCIeCommSdk::getHandle(mDevPath + XDMA_FILE_C2H + "_" + QString::number(index));//, FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_FLAG_OVERLAPPED);
    bool ret = PCIeCommSdk::readData(fHandle, offset, data);
    CloseHandle(fHandle);
    return ret;
}

/**
* @function name:readSpectrumData
* @brief 从RAM读能谱数据
* @param[in]        data
* @param[out]       data
* @return           bool
*/
bool CaptureThread::readSpectrumData(quint8 index, const QByteArray& data, const quint64 offset)
{
    HANDLE fHandle = PCIeCommSdk::getHandle(mDevPath + XDMA_FILE_BYPASS,
                                           GENERIC_READ | GENERIC_WRITE,
                                           FILE_ATTRIBUTE_NORMAL | FILE_SHARE_READ | FILE_SHARE_WRITE);
    bool ret = PCIeCommSdk::readData(fHandle, offset, data);
    CloseHandle(fHandle);
    return ret;
}
