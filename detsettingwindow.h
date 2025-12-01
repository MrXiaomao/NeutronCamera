#ifndef DETSETTINGWINDOW_H
#define DETSETTINGWINDOW_H

#include <QWidget>

namespace Ui {
class DetSettingWindow;
}

class DetSettingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DetSettingWindow(QWidget *parent = nullptr);
    ~DetSettingWindow();

    Q_SIGNAL void reportSettingFinished();

private slots:
    void on_pushButton_save_clicked();

    void on_pushButton_cancel_clicked();

private:
    Ui::DetSettingWindow *ui;
};

#endif // DETSETTINGWINDOW_H
