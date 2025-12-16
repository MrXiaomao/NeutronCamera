#ifndef OFFLINEWINDOW_H
#define OFFLINEWINDOW_H

#include <QMainWindow>

#include "QGoodWindowHelper"
#include "pciecommsdk.h"
#include "datacompresswindow.h"

namespace Ui {
class OfflineWindow;
}

// 定义结构体：每个x对应多条曲线的y值
struct FOM_CurvePoint {
    double x;          // 公共的x坐标
    double y1;         // 曲线1的y值
    double y2;         // 曲线2的y值
    double y3;         // 曲线3的y值
    
    // 构造函数（可选，方便使用）
    FOM_CurvePoint(double x_ = 0, double y1_ = 0, double y2_ = 0, double y3_ = 0) 
        : x(x_), y1(y1_), y2(y2_), y3(y3_) {}
};

class QCustomPlot;
class QProgressIndicator;
class OfflineWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit OfflineWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~OfflineWindow();

    enum DetectorType{
        dtLSD,
        dtPSD,
        dtLBD
    };

    void initUi();
    void initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel);
    void setCheckBoxHelper(QCustomPlot* customPlot);

    bool loadOfflineFilename(const QString&);
    void loadRelatedFiles(const QString& src);

    QPixmap maskPixmap(QPixmap, QSize sz, QColor clrMask);
    QPixmap roundPixmap(QSize sz, QColor clrOut = Qt::gray);//单圆
    QPixmap dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut = Qt::gray);//双圆

    // Q_SIGNAL void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    // Q_SLOT void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志
    // Q_SLOT void replyWaveform(quint8, quint8, QVector<quint16>&);
    // Q_SLOT void replyNeutronSpectrum(quint8, QVector<quint16>&);
    // Q_SLOT void replyGammaSpectrum(quint8, QVector<quint16>&);

    virtual void closeEvent(QCloseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;
    void PSDPlot(quint8, QVector<double> psd_x, QVector<double> psd_y, QVector<double> density);// PSD分布密度图绘制

public slots:
    void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志
    void replyWaveform(quint8, quint8, QVector<QPair<double,double>>&);
    void replySpectrum(quint8, quint8, QVector<QPair<double,double>>&);
    void replyCalculateDensityPSD(quint8, QVector<QPair<double,double>>&);// PSD密度分布计算
    void replyPlotFoM(quint8, QVector<FOM_CurvePoint>&);// FoM拟合

signals:
    void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    void reportWaveform(quint8, quint8, QVector<QPair<double,double>>&);
    void reportSpectrum(quint8, quint8, QVector<QPair<double,double>>&);
    void reportCalculateDensityPSD(quint8, QVector<QPair<double,double>>&);// 对PSD数据对，进行统计，给出PSD分布密度图
    // void reportPSDPlot(quint8, QVector<double> psd_x, QVector<double> psd_y, QVector<double> density);// PSD分布密度图
    void reportPlotFoM(quint8, QVector<FOM_CurvePoint>&);// FoM拟合

private slots:
    void on_action_openfile_triggered();

    void on_action_analyze_triggered();

    void on_action_linear_triggered();

    void on_action_logarithm_triggered();

    void on_action_exit_triggered();

    void on_pushButton_export_clicked();

    void on_action_lightTheme_triggered();

    void on_action_darkTheme_triggered();

    void on_action_colorTheme_triggered();

    void on_action_typeLSD_triggered(bool checked);

    void on_action_typePSD_triggered(bool checked);

    void on_action_typeLBD_triggered(bool checked);

    void on_action_waveform_triggered(bool checked);

    void on_action_ngamma_triggered(bool checked);

    // 验证时间范围输入
    void validateTime1Range();

    void startAnalysis(int,int, int, int, int);
private:
    Ui::OfflineWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;
    PCIeCommSdk mPCIeCommSdk;
    QString mShotNum;
    DetectorType mCurrentDetectorType;

    QProgressIndicator *mProgressIndicator = nullptr;
    QStringList mfileList;
    void applyColorTheme();
};

#endif // OFFLINEWINDOW_H
