#ifndef NETSETTINGWINDOW_H
#define NETSETTINGWINDOW_H

#include <QWidget>

namespace Ui {
class NetSettingWindow;
}

class NetSettingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NetSettingWindow(QWidget *parent = nullptr);
    ~NetSettingWindow();

private slots:
    void on_pushButton_save_clicked();

    void on_pushButton_cancel_clicked();

private:
    Ui::NetSettingWindow *ui;
};

#endif // NETSETTINGWINDOW_H
