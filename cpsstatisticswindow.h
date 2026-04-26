#ifndef CPSSTATISTICSWINDOW_H
#define CPSSTATISTICSWINDOW_H

#include <QMainWindow>

#include "QGoodWindowHelper"
#include "pciecommsdk.h"
#include "datacompresswindow.h"

// 3D平面图头文件添加
//#include <QtDataVisualization>
//#include <QSurface>
//using namespace QtDataVisualization;

//#include <QtDataVisualization/Q3DInputHandler>
//#include <QtDataVisualization/Q3DSurface>
//#include <QMouseEvent>

//class CustomInputHandler : public QtDataVisualization::Q3DInputHandler {
//    Q_OBJECT
//public:
//    explicit CustomInputHandler(QObject *parent = nullptr)
//        : Q3DInputHandler(parent), m_isDragging(false) {}

//protected:
//    // 鼠标按下事件：记录初始状态
//    void mousePressEvent(QMouseEvent *event, const QPoint &mousePos) override {
//        if (event->button() == Qt::LeftButton) {
//            m_isDragging = true;
//            m_lastMousePos = mousePos;
//            event->accept(); // 拦截事件，避免默认处理
//        } else {
//            Q3DInputHandler::mousePressEvent(event, mousePos); // 其他按键沿用默认逻辑
//        }
//    }

//    // 鼠标移动事件：判断操作类型（旋转/平移）
//    // 鼠标移动事件：修正旋转/平移逻辑
//    void mouseMoveEvent(QMouseEvent *event, const QPoint &mousePos) override {
//        if (!m_isDragging) return;

//        QPoint delta = mousePos - m_lastMousePos;
//        Q3DCamera *camera = scene()->activeCamera(); // 获取当前相机

//        if (event->modifiers() & Qt::ControlModifier) {
//            // 修正：左键+Ctrl → 平移（通过调整水平/垂直角度实现伪平移）
//            // 原理：平移等价于相机围绕场景中心旋转（模拟平移效果）
//            float currentHorizontal = camera->yRotation(); // 当前水平角度（Y轴）
//            float currentVertical = camera->xRotation();   // 当前垂直角度（X轴）
//            float currentZoom = camera->zoomLevel();       // 当前缩放级别

//            // 计算平移后的水平/垂直角度（灵敏度可调整）
//            float newHorizontal = currentHorizontal + delta.x() * 0.1f;
//            float newVertical = currentVertical - delta.y() * 0.1f;

//            // 应用新位置（保持缩放不变）
//            camera->setCameraPosition(newHorizontal, newVertical, currentZoom);
//        } else {
//            // 修正：左键 → 旋转（直接调整X/Y轴角度）
//            float currentXRot = camera->xRotation();
//            float currentYRot = camera->yRotation();

//            // 鼠标Y轴移动 → 相机X轴旋转（上下视角），鼠标X轴移动 → 相机Y轴旋转（左右视角）
//            float newXRot = currentXRot - delta.y() * 0.5f; // 反转Y轴方向（符合直觉）
//            float newYRot = currentYRot + delta.x() * 0.5f;

//            // 限制X轴旋转范围（避免过度翻转，可根据需求调整）
//            newXRot = qBound(-90.0f, newXRot, 90.0f); // 垂直角度限制在±90°

//            camera->setXRotation(newXRot);
//            camera->setYRotation(newYRot);
//        }

//        m_lastMousePos = mousePos;
//        event->accept();
//    }

//    // 鼠标释放事件：结束拖动
//    void mouseReleaseEvent(QMouseEvent *event, const QPoint &mousePos) override {
//        if (event->button() == Qt::LeftButton) {
//            m_isDragging = false;
//            event->accept();
//        } else {
//            Q3DInputHandler::mouseReleaseEvent(event, mousePos);
//        }
//    }

//private:
//    bool m_isDragging;       // 是否处于拖动状态
//    QPoint m_lastMousePos;   // 上一帧鼠标位置
//};

namespace Ui {
class CpsStatisticsWindow;
}

///// 自定义X轴格式化器：将数值转换为CH1-CH18的标签
//class QValue3DAxisFormatterX: public QtDataVisualization::QValue3DAxisFormatter
//{
//    Q_OBJECT
//public:
//    explicit QValue3DAxisFormatterX(QObject *parent = nullptr) : QValue3DAxisFormatter(parent) {}

//    virtual QString stringForValue(qreal value, const QString &/*format*/) const override
//    {
//        if (value >= 10 && value <= 180)
//            return QString("CH %1").arg(QString::number(value / 10, 'f', 0));
//        else
//            return QString();
//    }
//};

//#include <QtDataVisualization/Q3DSurface>
//#include <QMouseEvent>

//class CustomSurface : public QtDataVisualization::Q3DSurface {
//    Q_OBJECT
//public:
//    explicit CustomSurface(const QSurfaceFormat *format = nullptr, QWindow *parent = nullptr) : QtDataVisualization::Q3DSurface(format, parent) {}

//protected:
//    void mousePressEvent(QMouseEvent *event) override {
//        // 交换左右键的按下状态
//        if (event->button() == Qt::LeftButton) {
//            // 模拟右键按下（触发旋转）
//            QMouseEvent fakeRightPress(event->type(), event->pos(),
//                                       Qt::RightButton, Qt::RightButton, event->modifiers());
//            Q3DSurface::mousePressEvent(&fakeRightPress);
//        } else if (event->button() == Qt::RightButton) {
//            // 模拟左键按下（默认是选择/平移，此处改为旋转）
//            QMouseEvent fakeLeftPress(event->type(), event->pos(),
//                                      Qt::LeftButton, Qt::LeftButton, event->modifiers());
//            Q3DSurface::mousePressEvent(&fakeLeftPress);
//        } else {
//            Q3DSurface::mousePressEvent(event);
//        }
//    }

//    void mouseMoveEvent(QMouseEvent *event) override {
//        // 移动时保持交换后的按键逻辑
//        if (event->buttons() & Qt::LeftButton) {
//            QMouseEvent fakeRightMove(event->type(), event->pos(),
//                                      Qt::RightButton, Qt::RightButton, event->modifiers());
//            Q3DSurface::mouseMoveEvent(&fakeRightMove);
//        } else if (event->buttons() & Qt::RightButton) {
//            QMouseEvent fakeLeftMove(event->type(), event->pos(),
//                                     Qt::LeftButton, Qt::LeftButton, event->modifiers());
//            Q3DSurface::mouseMoveEvent(&fakeLeftMove);
//        } else {
//            Q3DSurface::mouseMoveEvent(event);
//        }
//    }
//};

class QCustomPlot;
class QProgressIndicator;
class CpsStatisticsWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CpsStatisticsWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~CpsStatisticsWindow();

    enum DetectorType{
        dtLSD,
        dtPSD,
        dtLBD
    };

    void initUi();
    void initHeatmap(); // 热度图
    void loadRelatedFiles(const QString& src);

    QPixmap maskPixmap(QPixmap, QSize sz, QColor clrMask);
    QPixmap roundPixmap(QSize sz, QColor clrOut = Qt::gray);//单圆
    QPixmap dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut = Qt::gray);//双圆

    virtual void closeEvent(QCloseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志
    void replyCpsPlot(QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>>);

signals:
    void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    void reportCpsPlot(QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>>);

private slots:
    void on_action_openfile_triggered();

    void on_action_exit_triggered();

    void on_action_lightTheme_triggered();

    void on_action_darkTheme_triggered();

    void on_action_colorTheme_triggered();

    void on_action_cps_triggered(bool checked);

    // 计数率统计
    void cpsStatistics();

    void on_comboBox_h5Files_currentTextChanged(const QString &arg1);

private:
    Ui::CpsStatisticsWindow *ui;
    //CustomSurface *surface;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;
    PCIeCommSdk mPCIeCommSdk;
    QString mShotNum;
    DetectorType mCurrentDetectorType;
    QVector<QColor> mGraphisColor;

    QProgressIndicator *mProgressIndicator = nullptr;
    QStringList mfileList;
    void applyColorTheme();

    //void init3DCurve(); // 3维曲线图
    QCustomPlot* mCpsPlot; //计数率
    QVector<QColor> mBarColor;
    QString mFileDir;

    QString joinFilename(const int& cameraIndex); // 拼接文件名
};

#endif // CPSSTATISTICSWINDOW_H
