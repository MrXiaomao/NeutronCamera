#ifndef DEVICEMANAGERWINDOW_H
#define DEVICEMANAGERWINDOW_H

#include <QWidget>

namespace Ui {
class DeviceManagerWindow;
}

class DeviceManagerWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceManagerWindow(QWidget *parent = nullptr);
    ~DeviceManagerWindow();

    void updataUi();
    virtual void showEvent(QShowEvent *event);

private slots:
    void on_pushButton_disable_clicked();

    void on_pushButton_enable_clicked();

    void on_checkBox_board_clicked(bool checked);

    void on_pushButton_disable_2_clicked();

    void on_pushButton_enable_2_clicked();

    void on_checkBox_board_2_clicked(bool checked);

    void on_pushButton_disable_3_clicked();

    void on_pushButton_enable_3_clicked();

    void on_checkBox_board_3_clicked(bool checked);

    void on_pushButton_clicked();

private:
    Ui::DeviceManagerWindow *ui;
};

#endif // DEVICEMANAGERWINDOW_H
