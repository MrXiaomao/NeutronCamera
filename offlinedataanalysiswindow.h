#ifndef OFFLINEDATAANALYSISWINDOW_H
#define OFFLINEDATAANALYSISWINDOW_H

#include <QWidget>

namespace Ui {
class OfflineDataAnalysisWindow;
}

class QCustomPlot;
class OfflineDataAnalysisWindow : public QWidget
{
    Q_OBJECT

public:
    explicit OfflineDataAnalysisWindow(QWidget *parent = nullptr);
    ~OfflineDataAnalysisWindow();

    void initCustomPlot(int index, QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, int graphCount = 1);

    bool loadOfflineFilename(const QString&);
    void startAnalyze();

    Q_SIGNAL void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SLOT void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志
    Q_SLOT void replyWaveform(quint8, QVector<quint16>&);

    virtual bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void on_pushButton_test_clicked();

    void on_pushButton_startUpload_clicked();

    void on_pushButton_start_clicked();

private:
    Ui::OfflineDataAnalysisWindow *ui;
};

#endif // OFFLINEDATAANALYSISWINDOW_H
