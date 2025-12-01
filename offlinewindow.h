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

    void initCustomPlot(int index, QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, int graphCount = 1);

    bool loadOfflineFilename(const QString&);
    void startAnalyze();
    void loadRelatedFiles(const QString& src);

    Q_SIGNAL void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SLOT void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志
    Q_SLOT void replyWaveform(quint8, quint8, QVector<quint16>&);
    Q_SLOT void replyNeutronSpectrum(quint8, QVector<quint16>&);
    Q_SLOT void replyGammaSpectrum(quint8, QVector<quint16>&);

    virtual void closeEvent(QCloseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void on_pushButton_test_clicked();

    void on_pushButton_startUpload_clicked();

    void on_action_openfile_triggered();

    void on_action_analyze_triggered();

    void on_action_linear_triggered();

    void on_action_logarithm_triggered();

    void on_action_exit_triggered();

    void on_pushButton_export_clicked();

    void on_action_lightTheme_triggered();

    void on_action_darkTheme_triggered();

    void on_action_colorTheme_triggered();

private:
    Ui::OfflineWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;
    PCIeCommSdk mPCIeCommSdk;

    void applyColorTheme();
};

#endif // OFFLINEWINDOW_H
