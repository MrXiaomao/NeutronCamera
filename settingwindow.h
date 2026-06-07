#ifndef SETTINGWINDOW_H
#define SETTINGWINDOW_H

#include <QWidget>

namespace Ui {
class SettingWindow;
}

class SettingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SettingWindow(QWidget *parent = nullptr);
    ~SettingWindow();

    Q_SIGNAL void reportSettingFinished();

private slots:
    void on_pushButton_exit_clicked();

private:
    Ui::SettingWindow *ui;
};

#endif // SETTINGWINDOW_H
