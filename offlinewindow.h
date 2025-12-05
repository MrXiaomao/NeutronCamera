#ifndef OFFLINEWINDOW_H
#define OFFLINEWINDOW_H

#include <QMainWindow>

#include "QGoodWindowHelper"
#include "pciecommsdk.h"

namespace Ui {
class OfflineWindow;
}

class QCustomPlot;
class OfflineWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit OfflineWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~OfflineWindow();

    void initUi();
    void initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel);

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

public slots:
    void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志
    void replyWaveform(quint8, quint8, QVector<QPair<double,double>>&);
    void replySpectrum(quint8, quint8, QVector<QPair<double,double>>&);
    void replyKernelDensitySpectrumPSD(quint8, QVector<QPair<double,double>>&);// 核密度图谱
    void replyKernelDensitySpectrumFoM(quint8, QVector<QVector<QPair<double,double>>>&);// FoM拟合

signals:
    void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    void reportWaveform(quint8, quint8, QVector<QPair<double,double>>&);
    void reportSpectrum(quint8, quint8, QVector<QPair<double,double>>&);
    void reportKernelDensitySpectrumPSD(quint8, QVector<QPair<double,double>>&);// 核密度图谱
    void reportKernelDensitySpectrumFoM(quint8, QVector<QVector<QPair<double,double>>>&);// FoM拟合

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

    QStringList mfileList;
    void applyColorTheme();
};

#endif // OFFLINEWINDOW_H
