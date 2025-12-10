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

private:
    Ui::OfflineWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;
    PCIeCommSdk mPCIeCommSdk;
    QString mShotNum;
    DetectorType mCurrentDetectorType;

    QStringList mfileList;
    void applyColorTheme();
};

#if 1
/**
* @提取有效波形
*/
#include <QMutex>
class ExtractValidWaveformTask : public QObject, public QRunnable{
    Q_OBJECT
public:
    explicit ExtractValidWaveformTask(const quint8& cameraIndex,
                                      int threshold,
                                      int pre_points,
                                      int post_points,
                                      const QString& filePath,
                                      std::function<void(QVector<std::array<qint16, 512>>&)> callback)
        : mFilePath(filePath)
        , mThreshold(threshold)
        , mPre_points(pre_points)
        , mPost_points(post_points)
        , mCameraIndex(cameraIndex)
        , mWaveformCallback(callback)
    {
        this->setAutoDelete(true);
        qRegisterMetaType<std::array<qint16,512>>("std::array<qint16,512>");
        qRegisterMetaType<QVector<std::array<qint16,512>>>("QVector<std::array<qint16,512>>&");
    }

    void run() override{
        if(!QFileInfo::exists(mFilePath)){
            return;
        }

        QVector<qint16> ch0, ch1, ch2, ch3;
        if(!DataAnalysisWorker::readBin4Ch_fast(mFilePath, ch0, ch1, ch2, ch3, true)){
            return;
        }

        QVector<std::array<qint16, 512>> mWaveform;
        int threshold = mThreshold;
        int pre_points = mPre_points;
        int post_points = mPost_points;

        //2、提取通道号的数据cameraNo
        //根据相机序号计算出是第几块光纤卡
        int board_index = (mCameraIndex-1)/4+1;
        int ch_channel = mCameraIndex % 4;
        if (ch_channel == 1) {
            //3、扣基线，调整数据
            qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch0);
            DataAnalysisWorker::adjustDataWithBaseline(ch0, baseline_ch, board_index, 1);

            //4、提取有效波形数据
            QVector<std::array<qint16, 512>> wave_ch = DataAnalysisWorker::overThreshold(ch0, 1, threshold, pre_points, post_points);
            //emit reportWaveform(wave_ch);
            mWaveformCallback(wave_ch);
        } else if (ch_channel == 2) {
            //3、扣基线，调整数据
            qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch1);
            DataAnalysisWorker::adjustDataWithBaseline(ch1, baseline_ch, board_index, 2);

            //4、提取有效波形数据
            QVector<std::array<qint16, 512>> wave_ch = DataAnalysisWorker::overThreshold(ch1, 2, threshold, pre_points, post_points);
            //emit reportWaveform(wave_ch);
            mWaveformCallback(wave_ch);
        } else if (ch_channel == 3) {
            //3、扣基线，调整数据
            qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch2);
            DataAnalysisWorker::adjustDataWithBaseline(ch2, baseline_ch, board_index, 3);

            //4、提取有效波形数据
            QVector<std::array<qint16, 512>> wave_ch = DataAnalysisWorker::overThreshold(ch2, 3, threshold, pre_points, post_points);
            //emit reportWaveform(wave_ch);
            mWaveformCallback(wave_ch);
        } else if (ch_channel == 4) {
            //3、扣基线，调整数据
            qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch3);
            DataAnalysisWorker::adjustDataWithBaseline(ch3, baseline_ch, board_index, 4);

            //4、提取有效波形数据
            QVector<std::array<qint16, 512>> wave_ch = DataAnalysisWorker::overThreshold(ch3, 4, threshold, pre_points, post_points);
            //emit reportWaveform(wave_ch);
            mWaveformCallback(wave_ch);
        }
    }

    Q_SIGNAL void reportWaveform(QVector<std::array<qint16, 512>>&);

private:
    QString mFilePath;
    quint8 mCameraIndex;
    int mThreshold;
    int mPre_points;
    int mPost_points;
    std::function<void(QVector<std::array<qint16, 512>>&)> mWaveformCallback;
};
#endif
#endif // OFFLINEWINDOW_H
