#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qcustomplot.h"
#include "globalsettings.h"
#include "switchbutton.h"

MainWindow::MainWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper *>(parent))
{
    ui->setupUi(this);
    setWindowTitle(QApplication::applicationName() + " - " + APP_VERSION);

    mCommHelper = CommHelper::instance();

    initUi();
    restoreSettings();
    applyColorTheme();

    connect(this, SIGNAL(reporWriteLog(const QString&,QtMsgType)), this, SLOT(replyWriteLog(const QString&,QtMsgType)));
    connect(this, SIGNAL(reportNeutronSpectrum(quint8,quint8,QVector<QPair<double,double>>&)), this, SLOT(replyNeutronSpectrum(quint8,quint8,QVector<QPair<double,double>>&)));
    connect(this, SIGNAL(reportGammaSpectrum(quint8,quint8,QVector<QPair<double,double>>&)), this, SLOT(replyGammaSpectrum(quint8,quint8,QVector<QPair<double,double>>&)));

    ui->toolButton_startMeasure->setDefaultAction(ui->action_startMeasure);
    ui->toolButton_stopMeasure->setDefaultAction(ui->action_stopMeasure);
    ui->tableWidget_camera->setEnabled(false);
    ui->pushButton_openPower->setEnabled(false);
    ui->pushButton_closePower->setEnabled(false);
    ui->pushButton_openVoltage->setEnabled(false);
    ui->pushButton_closeVoltage->setEnabled(false);
    ui->pushButton_selChannel1->setEnabled(false);
    ui->pushButton_selChannel2->setEnabled(false);

    mPCIeCommSdk.initialize();
    if (mPCIeCommSdk.numberOfDevices() <= 0){
        ui->action_init->setEnabled(false);
        ui->action_startMeasure->setEnabled(false);
        ui->action_stopMeasure->setEnabled(false);
    }
    else{
        ui->action_init->setEnabled(true);
        ui->action_startMeasure->setEnabled(false);
        ui->action_stopMeasure->setEnabled(false);
    }
    qInfo().noquote() << "发现数据采集卡张数：" << mPCIeCommSdk.numberOfDevices();
    connect(ui->statusbar,&QStatusBar::messageChanged,this,[&](const QString &message){
        if(message.isEmpty()) {
            ui->statusbar->showMessage(tr("准备就绪"));
        } else {
            ui->statusbar->showMessage(message);
        }
    });

    connect(&mPCIeCommSdk, &PCIeCommSdk::reportFileReadElapsedtime, this, [=](quint32 index, quint32 elapsedtime){
        // QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        // QString msg = QString("[%1] Device #%2 268435456 bytes received in %3ms").arg(time).arg(index).arg(elapsedtime);
        // ui->textEdit_log->append(msg);
    });
    connect(&mPCIeCommSdk, &PCIeCommSdk::reportFileWriteElapsedtime, this, [=](quint32 index, quint32 elapsedtime){
        // QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        // QString msg = QString("[%1] Device #%2 268435456 bytes written in %3ms").arg(time).arg(index).arg(elapsedtime);
        // ui->textEdit_log->append(msg);
    });
    connect(&mPCIeCommSdk, &PCIeCommSdk::reportCaptureFinished, this, [=](){
        ui->action_startMeasure->setEnabled(true);
        ui->action_stopMeasure->setEnabled(false);
    });

    QTimer::singleShot(0, this, [&](){
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
    });

    QTimer::singleShot(0, this, [&](){
        if(mainWindow) {
            mainWindow->fixMenuBarWidth();
        }
    });
}

MainWindow::~MainWindow()
{
    GlobalSettings settings;
    QSplitter *splitterH1 = this->findChild<QSplitter*>("splitterH1");// QSplitter(Qt::Horizontal,this);
    if (splitterH1){
        settings.setValue("Global/splitterH1/State", splitterH1->saveState());
        settings.setValue("Global/splitterH1/Geometry", splitterH1->saveGeometry());
    }

    QSplitter *splitterV2 = this->findChild<QSplitter*>("splitterV2");
    if (splitterV2){
        settings.setValue("Global/splitterV2/State", splitterV2->saveState());
        settings.setValue("Global/splitterV2/Geometry", splitterV2->saveGeometry());
    }

    settings.setValue("Global/MainWindows/State", this->saveState());

    delete ui;
}

void MainWindow::initUi()
{
    mDetSettingWindow = new DetSettingWindow();
    mDetSettingWindow->setWindowFlags(Qt::Widget | Qt::WindowStaysOnTopHint);
    mDetSettingWindow->hide();
    connect(mDetSettingWindow, &DetSettingWindow::reportSettingFinished, &mPCIeCommSdk, &PCIeCommSdk::replySettingFinished);

    mNetSettingWindow = new NetSettingWindow();
    mNetSettingWindow->setWindowFlags(Qt::Widget | Qt::WindowStaysOnTopHint);
    mNetSettingWindow->hide();

    {
        ui->tableWidget_status->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        ui->tableWidget_status->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        ui->tableWidget_status->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

        ui->tableWidget_status->setSpan(0, 0, 4, 1);
        ui->tableWidget_status->setItem(0, 0, new QTableWidgetItem("温度\n℃"));
        ui->tableWidget_status->item(0, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget_status->item(0, 0)->setBackground(QColor::fromRgb(0x00,0x00,0xff,0x70));
        for (int i=0; i<=3; ++i){
            ui->tableWidget_status->setItem(i, 1, new QTableWidgetItem(QString("#%1").arg(i+1)));
            ui->tableWidget_status->item(i, 1)->setTextAlignment(Qt::AlignCenter);
        }

        ui->tableWidget_status->setSpan(4, 0, 4, 1);
        ui->tableWidget_status->setItem(4, 0, new QTableWidgetItem("29V\n电压\n(V)"));
        ui->tableWidget_status->item(4, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget_status->item(4, 0)->setBackground(QColor::fromRgb(0x00,0x00,0xff,0x70));
        for (int i=0; i<=3; ++i){
            ui->tableWidget_status->setItem(4+i, 1, new QTableWidgetItem(QString("#%1").arg(i+1)));
            ui->tableWidget_status->item(4+i, 1)->setTextAlignment(Qt::AlignCenter);
        }
        ui->tableWidget_status->setSpan(8, 0, 4, 1);
        ui->tableWidget_status->setItem(8, 0, new QTableWidgetItem("29V\n电流\n(mA)"));
        ui->tableWidget_status->item(8, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget_status->item(8, 0)->setBackground(QColor::fromRgb(0x00,0x00,0xff,0x70));
        for (int i=0; i<=3; ++i){
            ui->tableWidget_status->setItem(8+i, 1, new QTableWidgetItem(QString("#%1").arg(i+1)));
            ui->tableWidget_status->item(8+i, 1)->setTextAlignment(Qt::AlignCenter);
        }

        ui->tableWidget_status->setSpan(12, 0, 4, 1);
        ui->tableWidget_status->setItem(12, 0, new QTableWidgetItem("48V\n电压(V)"));
        ui->tableWidget_status->item(12, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget_status->item(12, 0)->setBackground(QColor::fromRgb(0x00,0x00,0xff,0x70));
        for (int i=0; i<=3; ++i){
            ui->tableWidget_status->setItem(12+i, 1, new QTableWidgetItem(QString("#%1").arg(i+1)));
            ui->tableWidget_status->item(12+i, 1)->setTextAlignment(Qt::AlignCenter);
        }
        ui->tableWidget_status->setSpan(16, 0, 4, 1);
        ui->tableWidget_status->setItem(16, 0, new QTableWidgetItem("48V\n电流\n(mA)"));
        ui->tableWidget_status->item(16, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget_status->item(16, 0)->setBackground(QColor::fromRgb(0x00,0x00,0xff,0x70));
        for (int i=0; i<=3; ++i){
            ui->tableWidget_status->setItem(16+i, 1, new QTableWidgetItem(QString("#%1").arg(i+1)));
            ui->tableWidget_status->item(16+i, 1)->setTextAlignment(Qt::AlignCenter);
        }

        ui->tableWidget_status->setSpan(20, 0, 4, 1);
        ui->tableWidget_status->setItem(20, 0, new QTableWidgetItem("运放板\n电压\n(V)"));
        ui->tableWidget_status->item(20, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget_status->item(20, 0)->setBackground(QColor::fromRgb(0x00,0x00,0xff,0x70));
        for (int i=0; i<=3; ++i){
            ui->tableWidget_status->setItem(20+i, 1, new QTableWidgetItem(QString("#%1").arg(i+1)));
            ui->tableWidget_status->item(20+i, 1)->setTextAlignment(Qt::AlignCenter);
        }
        ui->tableWidget_status->setSpan(24, 0, 4, 1);
        ui->tableWidget_status->setItem(24, 0, new QTableWidgetItem("运放板\n电流\n(mA)"));
        ui->tableWidget_status->item(24, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget_status->item(24, 0)->setBackground(QColor::fromRgb(0x00,0x00,0xff,0x70));
        for (int i=0; i<=3; ++i){
            ui->tableWidget_status->setItem(24+i, 1, new QTableWidgetItem(QString("#%1").arg(i+1)));
            ui->tableWidget_status->item(24+i, 1)->setTextAlignment(Qt::AlignCenter);
        }

        for (int i=2; i<20; ++i){
            for (int j=0; j<28; ++j){
                ui->tableWidget_status->setItem(j, i, new QTableWidgetItem("0.0"));
                ui->tableWidget_status->item(j, i)->setTextAlignment(Qt::AlignCenter);
            }
        }
    }

    {
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);//编号
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);//1#电源
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);//1#偏压
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);//1#电源
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);//1#偏压
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);//选通
        ui->tableWidget_camera->horizontalHeader()->setFixedHeight(30);
        ui->tableWidget_camera->setColumnWidth(0, 35);
        ui->tableWidget_camera->setColumnWidth(1, 50);
        ui->tableWidget_camera->setColumnWidth(2, 77);
        ui->tableWidget_camera->setColumnWidth(3, 77);
        ui->tableWidget_camera->setColumnWidth(4, 77);
        ui->tableWidget_camera->setColumnWidth(5, 77);
        ui->tableWidget_camera->setColumnWidth(6, 77);
        for (int i=0; i<DETNUMBER_MAX; ++i)
            ui->tableWidget_camera->setRowHeight(i, 30);
        quint16 maxWidth = 0;
        for (int i=0; i<ui->tableWidget_camera->columnCount(); ++i)
            maxWidth += ui->tableWidget_camera->columnWidth(i);
        ui->tableWidget_camera->setFixedWidth(maxWidth);
        ui->tableWidget_camera->setFixedHeight(19*30+2);
        ui->leftStackedWidget->setFixedWidth(maxWidth+2);

        ui->tableWidget_camera->setSpan(0,0,11,1);
        ui->tableWidget_camera->setSpan(11,0,7,1);

        ui->tableWidget_camera->setItem(0, 0, new QTableWidgetItem("水\n平\n相\n机"));
        ui->tableWidget_camera->setItem(11, 0, new QTableWidgetItem("垂\n直\n相\n机"));
        ui->tableWidget_camera->item(0, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget_camera->item(11, 0)->setTextAlignment(Qt::AlignCenter);
        for (int i=1; i<=DETNUMBER_MAX; ++i){
            ui->tableWidget_camera->setItem(i-1, 1, new QTableWidgetItem(QString("#%1").arg(i)));
            ui->tableWidget_camera->item(i-1, 1)->setTextAlignment(Qt::AlignCenter);
        }

        for (int row=0; row<ui->tableWidget_camera->rowCount(); ++row){
            for (int column=2; column<=6; ++column){
                SwitchButton* cell = new SwitchButton(this);
                if (column == 2){
                    cell->setObjectName(QString("1#Power#%1").arg(row+1));
                    cell->setProperty("isPower", true);
                    cell->setProperty("index", row + 1);
                    cell->setChecked(true);
                }
                else if (column == 3){
                    cell->setObjectName(QString("1#Voltage#%1").arg(row+1));
                    cell->setProperty("isVoltage", true);
                    cell->setProperty("index", row + 1);
                    cell->setChecked(true);
                }
                else if (column == 4){
                    cell->setObjectName(QString("2#Power#%1").arg(row+1));
                    cell->setProperty("isPower", true);
                    cell->setProperty("index", row + 1);
                }
                else if (column == 5){
                    cell->setObjectName(QString("2#Voltage#%1").arg(row+1));
                    cell->setProperty("isVoltage", true);
                    cell->setProperty("index", row + 1);
                }
                else {
                    cell->setObjectName(QString("BackupChannel#%1").arg(row+1));
                    cell->setText("#2", "#1");
                    cell->setProperty("isSelChannel", true);
                    cell->setProperty("index", row + 1);

                    QColor bgColorOff = cell->getBgColorOff();
                    QColor bgColorOn = cell->getBgColorOn();
                    bgColorOff = QColor(82, 0, 195);
                    cell->setBgColor(bgColorOff, bgColorOn);
                    cell->setChecked(true);
                }
                ui->tableWidget_camera->setCellWidget(row, column, cell);                

                connect(cell, &SwitchButton::toggled, this, [=](bool checked){
                    SwitchButton* button = qobject_cast<SwitchButton*>(sender());
                    if (column == 2){
                        mPCIeCommSdk.switchPower(row + 1, checked);
                        mPCIeCommSdk.switchBackupPower(row + 1, !checked);
                    }
                    else if (column == 3) {
                        mPCIeCommSdk.switchVoltage(row + 1, checked);
                        mPCIeCommSdk.switchBackupVoltage(row + 1, !checked);
                    }
                    else if (column == 4){
                        mPCIeCommSdk.switchBackupPower(row + 1, checked);
                        mPCIeCommSdk.switchPower(row + 1, !checked);
                    }
                    else if (column == 5) {
                        mPCIeCommSdk.switchBackupVoltage(row + 1, checked);
                        mPCIeCommSdk.switchVoltage(row + 1, !checked);
                    }
                    else if (column == 6) {
                        mPCIeCommSdk.switchBackupChannel(row + 1, checked);
                    }
                });
            }
        }

        connect(&mPCIeCommSdk, &PCIeCommSdk::reportPowerStatus, this, [=](quint32 moduleNo, bool on){
            SwitchButton* button = this->findChild<SwitchButton*>(QString("1#Power#%1").arg(moduleNo));
            if (button){
                button->setChecked(on);
            }
        });
        connect(&mPCIeCommSdk, &PCIeCommSdk::reportVoltageStatus, this, [=](quint32 moduleNo, bool on){
            SwitchButton* button = this->findChild<SwitchButton*>(QString("1#Voltage#%1").arg(moduleNo));
            if (button){
                button->setChecked(on);
            }
        });
        connect(&mPCIeCommSdk, &PCIeCommSdk::reportBackupPowerStatus, this, [=](quint32 moduleNo, bool on){
            SwitchButton* button = this->findChild<SwitchButton*>(QString("2#Power#%1").arg(moduleNo));
            if (button){
                button->setChecked(on);
            }
        });
        connect(&mPCIeCommSdk, &PCIeCommSdk::reportBackupVoltageStatus, this, [=](quint32 moduleNo, bool on){
            SwitchButton* button = this->findChild<SwitchButton*>(QString("2#Voltage#%1").arg(moduleNo));
            if (button){
                button->setChecked(on);
            }
        });
        connect(&mPCIeCommSdk, &PCIeCommSdk::reportBackupChannelStatus, this, [=](quint32 moduleNo, bool on){
            SwitchButton* button = this->findChild<SwitchButton*>(QString("BackupChannel#%1").arg(moduleNo));
            if (button){
                button->setChecked(on);
            }
        });
    }

    QActionGroup *actionGrp = new QActionGroup(this);
    actionGrp->addAction(ui->action_manualTrigger);
    actionGrp->addAction(ui->action_externalTrigger);

    QActionGroup *themeActionGroup = new QActionGroup(this);
    ui->action_lightTheme->setActionGroup(themeActionGroup);
    ui->action_darkTheme->setActionGroup(themeActionGroup);
    ui->action_lightTheme->setChecked(!mIsDarkTheme);
    ui->action_darkTheme->setChecked(mIsDarkTheme);

    QActionGroup *actionChartGroup = new QActionGroup(this);
    ui->action_linear->setActionGroup(actionChartGroup);
    ui->action_logarithm->setActionGroup(actionChartGroup);

    // 任务栏信息
    QLabel *label_Idle = new QLabel(ui->statusbar);
    label_Idle->setObjectName("label_Idle");
    label_Idle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    label_Idle->setFixedWidth(300);
    label_Idle->setText(tr("准备就绪"));
    connect(ui->statusbar,&QStatusBar::messageChanged,this,[&](const QString &message){
        label_Idle->setText(message);
    });

    ui->statusbar->setContentsMargins(5, 0, 5, 0);
    ui->statusbar->addWidget(label_Idle);

    // 工作日志
    {
        QGraphicsScene *scene = new QGraphicsScene(this);
        scene->setObjectName("logGraphicsScene");
        QGraphicsTextItem *textItem = scene->addText(tr("工作日志"));
        textItem->setObjectName("logGraphicsTextItem");
        textItem->setPos(0,0);
        //textItem->setRotation(-90);
        ui->graphicsView->setFrameStyle(0);
        ui->graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView->setFixedHeight(30);
        ui->graphicsView->setScene(scene);
    }

    /*设置任务栏信息*/
    QLabel *label_systemtime = new QLabel(ui->statusbar);
    label_systemtime->setObjectName("label_systemtime");
    label_systemtime->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QLabel *label_alarm = new QLabel(ui->statusbar);
    label_alarm->setObjectName("label_alarm");
    label_alarm->setFixedSize(QSize(24, 24));
    label_alarm->setPixmap(QPixmap(":/resource/image/tip.png").scaled(24, 24));

    ui->statusbar->addWidget(new QLabel(ui->statusbar), 1);
    ui->statusbar->addWidget(nullptr, 1);
    ui->statusbar->addPermanentWidget(label_systemtime);
    ui->statusbar->addPermanentWidget(label_alarm);

    QTimer* systemClockTimer = new QTimer(this);
    systemClockTimer->setObjectName("systemClockTimer");
    connect(systemClockTimer, &QTimer::timeout, this, [=](){
        // 获取当前时间
        QDateTime currentDateTime = QDateTime::currentDateTime();

        // 获取星期几的数字（1代表星期日，7代表星期日）
        int dayOfWeekNumber = currentDateTime.date().dayOfWeek();

        // 星期几的中文名称列表
        QStringList dayNames = {
            tr("星期日"), QObject::tr("星期一"), QObject::tr("星期二"), QObject::tr("星期三"), QObject::tr("星期四"), QObject::tr("星期五"), QObject::tr("星期六"), QObject::tr("星期日")
        };

        // 根据数字获取中文名称
        QString dayOfWeekString = dayNames.at(dayOfWeekNumber);
        this->findChild<QLabel*>("label_systemtime")->setText(QString(QObject::tr("系统时间：")) + currentDateTime.toString("yyyy/MM/dd hh:mm:ss ") + dayOfWeekString);

        {
            //监测温度和电压报警状态
            static quint32 ref = 0;
            ref = (ref==0) ? 1 : 0;
            if (mIsAlarm[0] || mIsAlarm[1]){
                if (ref == 0){
                    QPixmap pixmap = maskPixmap(QPixmap(":/resource/image/tip.png"), QSize(24, 24), QColor::fromRgb(0xff,0x00,0x00,0xff));
                    this->findChild<QLabel*>("label_alarm")->setPixmap(pixmap);
                }
                else {
                    QPixmap pixmap = maskPixmap(QPixmap(":/resource/image/tip.png"), QSize(24, 24), QColor::fromRgb(0xff,0x00,0x00,0x00));
                    this->findChild<QLabel*>("label_alarm")->setPixmap(pixmap);
                }
            }
            else{
                QPixmap pixmap = maskPixmap(QPixmap(":/resource/image/tip.png"), QSize(24, 24), QColor::fromRgb(0x7c,0xfc,0x00,0xff));
                this->findChild<QLabel*>("label_alarm")->setPixmap(pixmap);
            }
        }
    });
    systemClockTimer->start(900);

    //布局
    {
        //中间布局
        {
            QSplitter *splitterV1 = new QSplitter(Qt::Vertical,ui->spectroMeterPageInfoWidget_LSD);
            splitterV1->setHandleWidth(2);
            splitterV1->addWidget(ui->spectroMeter_neutronSpectrum);
            splitterV1->addWidget(ui->spectroMeter_gammaSpectrum);
            splitterV1->setSizes(QList<int>() << 100000 << 100000);
            splitterV1->setCollapsible(0,false);
            splitterV1->setCollapsible(1,false);

            ui->spectroMeterPageInfoWidget_LSD->layout()->addWidget(splitterV1);
        }
        {
            QSplitter *splitterV1 = new QSplitter(Qt::Vertical,ui->spectroMeterPageInfoWidget_PSD);
            splitterV1->setHandleWidth(2);
            splitterV1->addWidget(ui->spectroMeter_time1_PSD);
            splitterV1->addWidget(ui->spectroMeter_time2_PSD);
            splitterV1->addWidget(ui->spectroMeter_time3_PSD);
            splitterV1->setSizes(QList<int>() << 100000 << 100000 << 100000);
            splitterV1->setCollapsible(0,false);
            splitterV1->setCollapsible(1,false);
            splitterV1->setCollapsible(2,false);
            ui->spectroMeterPageInfoWidget_PSD->layout()->addWidget(splitterV1);
        }
        {
            QSplitter *splitterV1 = new QSplitter(Qt::Vertical,ui->spectroMeterPageInfoWidget_LBD);
            splitterV1->setHandleWidth(2);
            splitterV1->addWidget(ui->spectroMeter_time1_LBD);
            splitterV1->addWidget(ui->spectroMeter_time2_LBD);
            splitterV1->addWidget(ui->spectroMeter_time3_LBD);
            splitterV1->setSizes(QList<int>() << 100000 << 100000 << 100000);
            splitterV1->setCollapsible(0,false);
            splitterV1->setCollapsible(1,false);
            splitterV1->setCollapsible(2,false);
            ui->spectroMeterPageInfoWidget_LBD->layout()->addWidget(splitterV1);
        }

        //大布局
        QSplitter *splitterH1 = new QSplitter(Qt::Horizontal,this);
        splitterH1->setObjectName("splitterH1");
        splitterH1->setHandleWidth(2);
        splitterH1->addWidget(ui->leftStackedWidget);
        splitterH1->addWidget(ui->centralHboxTabWidget);
        splitterH1->addWidget(ui->rightVboxWidget);
        splitterH1->setSizes(QList<int>() << 100000 << 600000 << 100000);
        splitterH1->setCollapsible(0,false);
        splitterH1->setCollapsible(1,false);
        splitterH1->setCollapsible(2,false);
        ui->centralwidget->layout()->addWidget(splitterH1);
        ui->centralwidget->layout()->addWidget(ui->rightSidewidget);
    }

    // 左侧栏
    QPushButton* controlButton = nullptr;
    {        
        {
            controlButton = new QPushButton();
            controlButton->setText(tr("设备控制"));
            controlButton->setFixedSize(250,29);
            controlButton->setCheckable(true);
        }

        QHBoxLayout* sideHboxLayout = new QHBoxLayout();
        sideHboxLayout->setObjectName("sideHboxLayout");
        sideHboxLayout->setContentsMargins(0,0,0,0);
        sideHboxLayout->setSpacing(2);

        QWidget* sideProxyWidget = new QWidget();
        sideProxyWidget->setObjectName("sideProxyWidget");
        sideProxyWidget->setLayout(sideHboxLayout);
        sideHboxLayout->addWidget(controlButton);

        QGraphicsScene *scene = new QGraphicsScene(this);
        QGraphicsProxyWidget *w = scene->addWidget(sideProxyWidget);
        w->setPos(0,0);
        w->setRotation(-90);
        ui->graphicsView_2->setScene(scene);
        ui->graphicsView_2->setFrameStyle(0);
        ui->graphicsView_2->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView_2->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView_2->setFixedSize(30, 250);
        ui->leftSidewidget->setFixedWidth(30);

        connect(controlButton,&QPushButton::clicked,this,[=](/*bool checked*/){
            if(ui->leftStackedWidget->isHidden()) {
                ui->leftStackedWidget->setCurrentWidget(ui->detectorControlWidget);
                ui->leftStackedWidget->show();

                GlobalSettings settings;
                settings.setValue("Global/DefaultPage", "detectorControl");
            } else {
                if(ui->leftStackedWidget->currentWidget() == ui->detectorControlWidget) {
                    ui->leftStackedWidget->hide();

                    controlButton->setChecked(false);
                    GlobalSettings settings;
                    settings.setValue("Global/DefaultPage", "");
                } else {
                    ui->leftStackedWidget->setCurrentWidget(ui->detectorControlWidget);

                    GlobalSettings settings;
                    settings.setValue("Global/DefaultPage", "detectorControl");
                }
            }
        });

        connect(ui->toolButton_closeDetectorControlWidget,&QPushButton::clicked,this,[=](){
            ui->leftStackedWidget->hide();
            controlButton->setChecked(false);

            GlobalSettings settings;
            settings.setValue("Global/DefaultPage", "");
        });

        GlobalSettings settings;
        if (settings.value("Global/DefaultPage").toString() == "detectorControl"){
            ui->leftStackedWidget->setCurrentWidget(ui->detectorControlWidget);
            controlButton->setChecked(true);
        }
        else{
            ui->leftStackedWidget->hide();
            controlButton->setChecked(false);
        }
    }

    // 右侧栏
    QPushButton* labPrametersButton = nullptr;
    {
        labPrametersButton = new QPushButton();
        labPrametersButton->setText(tr("实验参数"));
        labPrametersButton->setFixedSize(250,29);
        labPrametersButton->setCheckable(true);

        connect(labPrametersButton,&QPushButton::clicked,this,[=](){
            if(ui->rightVboxWidget->isHidden()) {
                ui->rightVboxWidget->show();

                GlobalSettings settings;
                settings.setValue("Global/ShowRightSide", "true");
            } else {
                ui->rightVboxWidget->hide();

                GlobalSettings settings;
                settings.setValue("Global/ShowRightSide", "false");
            }
        });

        QGraphicsScene *scene = new QGraphicsScene(this);
        QGraphicsProxyWidget *w = scene->addWidget(labPrametersButton);
        w->setPos(0,0);
        w->setRotation(-90);
        ui->rightGraphicsView->setScene(scene);
        ui->rightGraphicsView->setFrameStyle(0);
        ui->rightGraphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->rightGraphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->rightGraphicsView->setFixedSize(30, 250);
        ui->rightSidewidget->setFixedWidth(30);
    }

    QAction *action = ui->lineEdit_savePath->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
    QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
    button->setCursor(QCursor(Qt::PointingHandCursor));
    connect(button, &QToolButton::pressed, this, [=](){
        QString cacheDir = QFileDialog::getExistingDirectory(this);
        if (!cacheDir.isEmpty()){
            GlobalSettings settings(CONFIG_FILENAME);
            settings.setValue("Global/CacheDir", cacheDir);
            ui->lineEdit_savePath->setText(cacheDir);
        }
    });

    // 数据保存路径
    {
        GlobalSettings settings(CONFIG_FILENAME);
        QString cacheDir = settings.value("Global/CacheDir").toString();
        if (cacheDir.isEmpty())
            cacheDir = QApplication::applicationDirPath() + "/cache/";
        ui->lineEdit_savePath->setText(cacheDir);

        // 发次
        ui->lineEdit_shotNum->setText(settings.value("Global/ShotNum", "00001").toString());
        ui->checkBox_autoIncrease->setChecked(settings.value("Global/ShotNumIsAutoIncrease", false).toBool());
    }

    //恢复页面布局
    {
        GlobalSettings settings;
        if (settings.contains("Global/MainWindows-State")){
            this->restoreState(settings.value("Global/MainWindows-State").toByteArray());
        }

        if (settings.value("Global/ShowRightSide").toString() == "false")
            ui->rightVboxWidget->hide();

        if (settings.contains("Global/splitterH1/State")){
            QSplitter *splitterH1 = this->findChild<QSplitter*>("splitterH1");
            if (splitterH1)
            {
                splitterH1->restoreState(settings.value("Global/splitterH1/State").toByteArray());
                //splitterH1->restoreGeometry(settings.value("Global/splitterH1/Geometry").toByteArray());
            }
        }

        if (settings.contains("Global/splitter/State")){
            QSplitter *splitterV2 = this->findChild<QSplitter*>("splitterV2");
            if (splitterV2)
            {
                splitterV2->restoreState(settings.value("Global/splitterV2/State").toByteArray());
                //splitterV2->restoreGeometry(settings.value("Global/splitterV2/Geometry").toByteArray());
            }
        }
    }

    {
        initCustomPlot(ui->spectroMeter_neutronSpectrum, tr(""), tr("中子能谱"));
        initCustomPlot(ui->spectroMeter_gammaSpectrum, tr("道址"), tr("伽马能谱"));

        initCustomPlot(ui->spectroMeter_time1_PSD, tr(""), tr("时刻1 中子能谱"));
        initCustomPlot(ui->spectroMeter_time2_PSD, tr(""), tr("时刻2 中子能谱"));
        initCustomPlot(ui->spectroMeter_time3_PSD, tr("道址"), tr("时刻3 中子能谱"));

        initCustomPlot(ui->spectroMeter_time1_LBD, tr(""), tr("时刻1 伽马能谱"));
        initCustomPlot(ui->spectroMeter_time2_LBD, tr(""), tr("时刻2 伽马能谱"));
        initCustomPlot(ui->spectroMeter_time3_LBD, tr("道址"), tr("时刻3 伽马能谱"));

        QActionGroup *actionType = new QActionGroup(this);
        actionType->addAction(ui->action_typeLSD);
        actionType->addAction(ui->action_typePSD);
        actionType->addAction(ui->action_typeLBD);
        {
            GlobalSettings settings(CONFIG_FILENAME);
            if (settings.value("Global/DetType", "PSD").toString() == "LSD"){
                ui->action_typeLSD->setChecked(true);
                emit ui->action_typeLSD->triggered(true);
            }
            else if (settings.value("Global/DetType", "PSD").toString() == "PSD"){
                ui->action_typePSD->setChecked(true);
                emit ui->action_typePSD->triggered(true);
            }
            else if (settings.value("Global/DetType", "PSD").toString() == "LBD"){
                ui->action_typeLBD->setChecked(true);
                emit ui->action_typeLBD->triggered(true);
            }
            else{
                ui->action_typePSD->setChecked(true);
                emit ui->action_typePSD->triggered(true);
            }
        }
    }

    connect(mCommHelper, &CommHelper::reportShotnum, this, [=](QString shotnum){
        qInfo().noquote() << "接收指令，更新炮号：" << shotnum;
        if (mEnableAutoUpdateShotnum){
            ui->lineEdit_shotNum->setText(shotnum);
        }
    });
    connect(mCommHelper, &CommHelper::reportSystemtime, this, [=](QDateTime tm){
        qInfo().noquote() << "接收指令，同步时钟：" << tm.toString("yyyy-MM-dd hh:mm:ss.zzz");
        if (mEnableClockSynchronization){
        }
    });
    connect(mCommHelper, &CommHelper::reportEnergenceStop, this, [=](){
        qInfo().noquote() << "接收指令，紧急停机！";
        ui->action_stop->setChecked(true);
        mEnableEmergencyStop = true;

        //软件立马控制软件停止测量，并将已测的数据继续保存，在界面打印“紧急停机”日志。然后发送电源断开指令、偏压关闭指令到设备来控制测量系统进行断电。
        qInfo().noquote() << "自动停止测量、切断电压和关闭偏压";

        for (int row = 0; row < 18; ++row){
            mPCIeCommSdk.switchPower(row + 1, false);
            mPCIeCommSdk.switchVoltage(row + 1, false);
        }
    });
    connect(mCommHelper, &CommHelper::reportTemperature, this, [=](quint8 moduleNo, QVector<float>& pairs){
        quint32 column = moduleNo + 1;
        for (int row=0; row<pairs.size(); ++row){
            ui->tableWidget_status->item(row, column)->setText(QString::number(pairs[row], 'f', 2));

            //温度设成10~50℃
            if (pairs[row] > 50 || pairs[row] < 10){
                ui->tableWidget_status->item(row, column)->setTextColor(Qt::red);

                qCritical().noquote() << "模组#" << moduleNo << "温度异常，温度值：" << QString::number(pairs[row], 'f', 2);

                mPCIeCommSdk.switchPower(moduleNo, false);
                mPCIeCommSdk.switchVoltage(moduleNo, false);
            }
            else{
                ui->tableWidget_status->item(row, column)->setTextColor(mIsDarkTheme ? Qt::white : Qt::black);
            }
        }
    });
    connect(mCommHelper, &CommHelper::reportVoltageCurrent, this, [=](quint8 moduleNo, QVector<QPair<float,float>>& pairs){
        quint32 column = moduleNo + 1;

        //29V电压
        quint8 rowOffset = 4;
        for (int row=0; row<3; ++row){
            ui->tableWidget_status->item(row + rowOffset, column)->setText(QString::number(pairs[row].first, 'f', 2));

            //电压设成28~30V
            if (pairs[row].first > 30 || pairs[row].first < 28){
                ui->tableWidget_status->item(row + rowOffset, column)->setTextColor(Qt::red);

                qCritical().noquote() << "模组#" << moduleNo << " 29V电压异常，电压值：" << QString::number(pairs[row].first, 'f', 2) << "V";

                mPCIeCommSdk.switchPower(moduleNo, false);
                mPCIeCommSdk.switchVoltage(moduleNo, false);
                mPCIeCommSdk.switchBackupPower(moduleNo, true);
                mPCIeCommSdk.switchBackupVoltage(moduleNo, true);
                mPCIeCommSdk.switchBackupChannel(moduleNo, true);
            }
            else{
                ui->tableWidget_status->item(row + rowOffset, column)->setTextColor(mIsDarkTheme ? Qt::white : Qt::black);
            }

            //电流设置成0~20mA
            ui->tableWidget_status->item(row + rowOffset + 4, column)->setText(QString::number(pairs[row].second, 'f', 2));
            if (pairs[row].second > 20 || pairs[row].second == 0){
                ui->tableWidget_status->item(row + rowOffset + 4, column)->setTextColor(Qt::red);

                qCritical().noquote() << "模组#" << moduleNo << " 29V电流异常，电流值：" << QString::number(pairs[row].first, 'f', 2) << "mA";

                mPCIeCommSdk.switchPower(moduleNo, false);
                mPCIeCommSdk.switchVoltage(moduleNo, false);
                mPCIeCommSdk.switchBackupPower(moduleNo, true);
                mPCIeCommSdk.switchBackupVoltage(moduleNo, true);
                mPCIeCommSdk.switchBackupChannel(moduleNo, true);
            }
            else{
                ui->tableWidget_status->item(row + rowOffset + 4, column)->setTextColor(mIsDarkTheme ? Qt::white : Qt::black);
            }
        }

        //48V电压
        rowOffset += 4;
        for (int row=4; row<8; ++row){
            ui->tableWidget_status->item(row + rowOffset, column)->setText(QString::number(pairs[row].first, 'f', 2));

            //电压设成45~50V
            if (pairs[row].first > 50 || pairs[row].first < 45){
                ui->tableWidget_status->item(row + rowOffset, column)->setTextColor(Qt::red);

                qCritical().noquote() << "模组#" << moduleNo << " 48V电压异常，电压值：" << QString::number(pairs[row].first, 'f', 2) << "V";

                mPCIeCommSdk.switchPower(moduleNo, false);
                mPCIeCommSdk.switchVoltage(moduleNo, false);
                mPCIeCommSdk.switchBackupPower(moduleNo, true);
                mPCIeCommSdk.switchBackupVoltage(moduleNo, true);
                mPCIeCommSdk.switchBackupChannel(moduleNo, true);
            }
            else{
                ui->tableWidget_status->item(row + rowOffset, column)->setTextColor(mIsDarkTheme ? Qt::white : Qt::black);
            }

            //电流设置成0~20mA
            ui->tableWidget_status->item(row + rowOffset + 4, column)->setText(QString::number(pairs[row].second, 'f', 2));
            if (pairs[row].second > 20 || pairs[row].second == 0){
                ui->tableWidget_status->item(row + rowOffset + 4, column)->setTextColor(Qt::red);

                qCritical().noquote() << "模组#" << moduleNo << " 48V电流异常，电流值：" << QString::number(pairs[row].first, 'f', 2) << "mA";

                mPCIeCommSdk.switchPower(moduleNo, false);
                mPCIeCommSdk.switchVoltage(moduleNo, false);
                mPCIeCommSdk.switchBackupPower(moduleNo, true);
                mPCIeCommSdk.switchBackupVoltage(moduleNo, true);
                mPCIeCommSdk.switchBackupChannel(moduleNo, true);
            }
            else{
                ui->tableWidget_status->item(row + rowOffset + 4, column)->setTextColor(mIsDarkTheme ? Qt::white : Qt::black);
            }
        }

        //运放板电压
        rowOffset += 4;
        for (int row=8; row<12; ++row){
            ui->tableWidget_status->item(row + rowOffset, column)->setText(QString::number(pairs[row].first, 'f', 2));

            //电压设成45~50V
            if (pairs[row].first > 50 || pairs[row].first < 45){
                ui->tableWidget_status->item(row + rowOffset, column)->setTextColor(Qt::red);

                qCritical().noquote() << "模组#" << moduleNo << " 运放板电压异常，电压值：" << QString::number(pairs[row].first, 2, 'f') << "V";

                mPCIeCommSdk.switchPower(moduleNo, false);
                mPCIeCommSdk.switchVoltage(moduleNo, false);
                mPCIeCommSdk.switchBackupPower(moduleNo, true);
                mPCIeCommSdk.switchBackupVoltage(moduleNo, true);
                mPCIeCommSdk.switchBackupChannel(moduleNo, true);
            }
            else{
                ui->tableWidget_status->item(row + rowOffset, column)->setTextColor(mIsDarkTheme ? Qt::white : Qt::black);
            }

            //电流设置成0~20mA
            ui->tableWidget_status->item(row + rowOffset + 4, column)->setText(QString::number(pairs[row].second, 'f', 2));
            if (pairs[row].second > 20 || pairs[row].second == 0){
                ui->tableWidget_status->item(row + rowOffset + 4, column)->setTextColor(Qt::red);

                qCritical().noquote() << "模组#" << moduleNo << " 运放板电流异常，电流值：" << QString::number(pairs[row].first, 2, 'f') << "mA";

                mPCIeCommSdk.switchPower(moduleNo, false);
                mPCIeCommSdk.switchVoltage(moduleNo, false);
                mPCIeCommSdk.switchBackupPower(moduleNo, true);
                mPCIeCommSdk.switchBackupVoltage(moduleNo, true);
                mPCIeCommSdk.switchBackupChannel(moduleNo, true);
            }
            else{
                ui->tableWidget_status->item(row + rowOffset + 4, column)->setTextColor(mIsDarkTheme ? Qt::white : Qt::black);
            }
        }
    });
}


#include <random>
#include <vector>
#include <cmath>
class GaussianRandomGenerator {
private:
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_real_distribution<double> dis;

public:
    GaussianRandomGenerator() : gen(rd()), dis(0.0, 1.0) {}

    // 生成单个高斯分布随机数
    double generateGaussian(double mean = 0.0, double stddev = 1.0) {
        double u1 = dis(gen);
        double u2 = dis(gen);

        // Box-Muller变换
        double z0 = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);

        return z0 * stddev + mean;
    }

    // 生成一对高斯分布随机数
    QPair<double, double> generateGaussianPair(double mean = 0.0, double stddev = 1.0) {
        double u1 = dis(gen);
        double u2 = dis(gen);

        // Box-Muller变换，同时生成两个独立的高斯随机数
        // double z0 = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
        // double z1 = std::sqrt(-2.0 * std::log(u1)) * std::sin(2.0 * M_PI * u2);
        // return qMakePair(z0 * stddev + mean, z1 * stddev + mean);
        return qMakePair(u1, u2);
    }

    // 生成指定数量的高斯随机数对
    QVector<QPair<double, double>> generateGaussianPairs(int count, double mean = 0.0, double stddev = 1.0) {
        QVector<QPair<double, double>> pairs;
        pairs.reserve(count);

        for (int i = 0; i < count; ++i) {
            pairs.push_back(generateGaussianPair(mean, stddev));
        }

        return pairs;
    }
};

void MainWindow::initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel)
{
    customPlot->installEventFilter(this);
    customPlot->setAntialiasedElements(QCP::aeAll);
    customPlot->legend->setVisible(false);
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom/* | QCP::iSelectPlottables*/);
    customPlot->xAxis->setTickLabelRotation(-45);
    // customPlot->xAxis->rescale(false);
    // customPlot->yAxis->rescale(false);
    // customPlot->yAxis->ticker()->setTickCount(5);
    // customPlot->xAxis->ticker()->setTickCount(10);
    // customPlot->yAxis2->ticker()->setTickCount(5);
    // customPlot->xAxis2->ticker()->setTickCount(10);
    customPlot->xAxis->setLabel(axisXLabel);
    customPlot->yAxis->setLabel(axisYLabel);
    customPlot->axisRect()->setupFullAxesBox(true);

    if (customPlot == ui->spectroMeter_time1_PSD ||
        customPlot == ui->spectroMeter_time2_PSD ||
        customPlot == ui->spectroMeter_time3_PSD ||
        customPlot == ui->spectroMeter_time1_LBD ||
        customPlot == ui->spectroMeter_time2_LBD ||
        customPlot == ui->spectroMeter_time3_LBD
        ){
        customPlot->xAxis->setRange(0, 2048);
        customPlot->yAxis->setRange(0, 10000);

        QColor colors[] = {Qt::red, Qt::blue};
        QString title[] = {"水平", "垂直"};

        customPlot->legend->setVisible(true);
        for (int i=0; i<2; ++i){
            QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
            graph->setAntialiased(false);
            graph->setPen(QPen(colors[i]));
            graph->selectionDecorator()->setPen(QPen(colors[i]));
            graph->setLineStyle(QCPGraph::lsLine);
            graph->setSelectable(QCP::SelectionType::stNone);
            graph->setName(title[i]);
        }
    }
    else if (customPlot == ui->spectroMeter_neutronSpectrum){
        customPlot->xAxis->setRange(0, 2048);
        customPlot->yAxis->setRange(0, 10000);

        QColor colors[] = {Qt::red, Qt::blue};
        QString title[] = {"水平", "垂直"};
        customPlot->legend->setVisible(true);
        for (int i=0; i<2; ++i){
            QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
            graph->setAntialiased(false);
            graph->setPen(QPen(colors[i]));
            graph->selectionDecorator()->setPen(QPen(colors[i]));
            graph->setLineStyle(QCPGraph::lsLine);
            graph->setSelectable(QCP::SelectionType::stNone);
            graph->setName(title[i]);
        }
    }
    else if (customPlot == ui->spectroMeter_gammaSpectrum){
        customPlot->xAxis->setRange(0, 2048);
        customPlot->yAxis->setRange(0, 10000);

        QColor colors[] = {Qt::red, Qt::blue};
        QString title[] = {"水平", "垂直"};
        customPlot->legend->setVisible(true);
        for (int i=0; i<2; ++i){
            QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
            graph->setAntialiased(false);
            graph->setPen(QPen(colors[i]));
            graph->selectionDecorator()->setPen(QPen(colors[i]));
            graph->setLineStyle(QCPGraph::lsLine);
            graph->setSelectable(QCP::SelectionType::stNone);
            graph->setName(title[i]);
        }
    }

    customPlot->replot();
    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));

    // 是否允许X轴自适应缩放
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (mIsMeasuring){
        QMessageBox::information(this, tr("系统退出提示"), tr("测量中禁止退出软件系统！"),
                                             QMessageBox::Ok, QMessageBox::Ok);
        event->ignore();
    }
    else
    {
        event->accept();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event){
    if (watched != this){
        if (event->type() == QEvent::MouseButtonPress){
            QMouseEvent *e = reinterpret_cast<QMouseEvent*>(event);
            if (watched->inherits("QCustomPlot")){
                QCustomPlot* customPlot = qobject_cast<QCustomPlot*>(watched);

                if (e->button() == Qt::RightButton) {// 右键恢复
                    QMenu contextMenu(customPlot);
                    contextMenu.addAction(tr("恢复视图"), this, [=]{
                        customPlot->xAxis->rescale(true);
                        customPlot->yAxis->rescale(true);
                        customPlot->replot(QCustomPlot::rpQueuedReplot);
                    });
                    contextMenu.addAction(tr("导出图像..."), this, [=]{
                        QString filePath = QFileDialog::getSaveFileName(this);
                        if (!filePath.isEmpty()){
                            if (!filePath.endsWith(".png"))
                                filePath += ".png";
                            if (!customPlot->savePng(filePath, 1920, 1080))
                                QMessageBox::information(this, tr("提示"), tr("导出失败！"));
                        }
                    });
                    contextMenu.exec(QCursor::pos());

                    //释放内存
                    QList<QAction*> list = contextMenu.actions();
                    foreach (QAction* action, list) delete action;
                }                                
            }
        }

        else if (event->type() == QEvent::MouseButtonDblClick){
            // QMouseEvent *e = reinterpret_cast<QMouseEvent*>(event);
            // if (watched->inherits("QCustomPlot")){
            //     QCustomPlot* customPlot = qobject_cast<QCustomPlot*>(watched);
            //     if (e->button() == Qt::LeftButton) {
            //         mIsOneLayout = !mIsOneLayout;
            //         if (mIsOneLayout)
            //             ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageDetailWidget);
            //         else
            //             ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget);
            //     }
            // }
        }
    }
    if(event->type() == QEvent::StatusTip) {
        QStatusTipEvent* statusTipEvent = static_cast<QStatusTipEvent *>(event);
        if (!statusTipEvent->tip().isEmpty()) {
            ui->statusbar->showMessage(statusTipEvent->tip(), 2000);
        }

        return true;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::replyWriteLog(const QString &msg, QtMsgType msgType)
{
#if 0
    // 创建一个 QTextCursor
    QTextCursor cursor = ui->textEdit_log->textCursor();
    // 将光标移动到文本末尾
    cursor.movePosition(QTextCursor::End);

    // 先插入时间
    QString color = "black";
    if (mIsDarkTheme)
        color = "white";
    cursor.insertHtml(QString("<span style='color:%1;'>%2</span>").arg(color, QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz >> ")));
    // 再插入文本
    if (msgType == QtDebugMsg || msgType == QtInfoMsg)
        cursor.insertHtml(QString("<span style='color:%1;'>%2</span>").arg(color, msg));
    else if (msgType == QtCriticalMsg || msgType == QtFatalMsg)
        cursor.insertHtml(QString("<span style='color:red;'>%1</span>").arg(msg));
    else
        cursor.insertHtml(QString("<span style='color:green;'>%1</span>").arg(msg));

    // 最后插入换行符
    cursor.insertHtml("<br>");

    // 确保 QTextEdit 显示了光标的新位置
    ui->textEdit_log->setTextCursor(cursor);
#else
    ui->textEdit_log->append(QString("%1 %2").arg(QDateTime::currentDateTime().toString("[yyyy-MM-dd hh:mm:ss.zzz]"), msg));
#endif

    //限制行数
    QTextDocument *document = ui->textEdit_log->document(); // 获取文档对象，想象成打开了一个TXT文件
    int rowCount = document->blockCount(); // 获取输出区的行数
    int maxRowNumber = 2000;//设定最大行
    if(rowCount > maxRowNumber){//超过最大行则开始删除
        QTextCursor cursor = QTextCursor(document); // 创建光标对象
        cursor.movePosition(QTextCursor::Start); //移动到开头，就是TXT文件开头

        for (int var = 0; var < rowCount - maxRowNumber; ++var) {
            cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor); // 向下移动并选中当前行
        }
        cursor.removeSelectedText();//删除选择的文本
    }
}


void MainWindow::on_action_exit_triggered()
{
    mainWindow->close();
}

void MainWindow::on_action_open_triggered()
{
    QString program = QCoreApplication::applicationFilePath();
    QStringList arguments;
    arguments.append("-m");
    arguments.append("offline");

    static int num = 0;
    arguments.append("-num");
    arguments.append(QString::number(num));
    num++;
    QProcess::startDetached(program, arguments);

    qInfo().noquote() << tr("打开离线数据分析程序");
}

void MainWindow::on_action_data_compress_triggered()
{
    QString program = QCoreApplication::applicationFilePath();
    QStringList arguments;
    arguments.append("-c");
    arguments.append("compress");

    static int num = 0;
    arguments.append("-num");
    arguments.append(QString::number(num));
    num++;
    QProcess::startDetached(program, arguments);

    qInfo().noquote() << tr("打开数据压缩与上传程序");
}

void MainWindow::on_action_startMeasure_triggered()
{
    if (mEnableEmergencyStop){
        QMessageBox::critical(this, tr("紧急停机"), tr("禁止在紧急停机状态下实验！"));
        return;
    }

    //根据实验时长判断磁盘空间是否足够
    {
        qint32 timeBase = 50;//打包时长基数
        qint32 onePacketSize = 256*1024*1024;//打包大小基数
        QString cacheDir = ui->lineEdit_savePath->text();
        QDir dir(cacheDir);
        qint64 diskFreeSpace = getDiskFreeSpace(dir.dirName());
        qint64 validDiskSpace = ui->spinBox_timeLength->value() / timeBase * onePacketSize;
        if (diskFreeSpace <= validDiskSpace){
            QMessageBox::information(this, tr("提示"), tr("磁盘空间不足！"));
            return;
        }
    }

    QCustomPlot *spectroMeter_top = this->findChild<QCustomPlot*>("spectroMeter_top");
    for (int j=0; j<spectroMeter_top->graphCount(); ++j)
        spectroMeter_top->graph(j)->data()->clear();

    QCustomPlot *spectroMeter_horCamera = this->findChild<QCustomPlot*>("spectroMeter_horCamera");
    for (int j=0; j<spectroMeter_horCamera->graphCount(); ++j)
        spectroMeter_horCamera->graph(j)->data()->clear();

    QCustomPlot *spectroMeter_verCamera = this->findChild<QCustomPlot*>("spectroMeter_verCamera");
    for (int j=0; j<spectroMeter_verCamera->graphCount(); ++j)
        spectroMeter_verCamera->graph(j)->data()->clear();

    spectroMeter_top->replot();
    spectroMeter_horCamera->replot();
    spectroMeter_verCamera->replot();

    QDateTime now = QDateTime::currentDateTime();
    QString fileSaveDir = QString("%1/%2/%3").arg(ui->spinBox_timeLength->value()).arg(ui->lineEdit_shotNum->text()).arg(now.toString("yyyy-MM-dd_HH-mm-ss"));
    QDir dir(fileSaveDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")){
            qInfo().noquote() << tr("本次实验数据存储路径无效！！！") << fileSaveDir;
            return;
        }
    }

    qInfo().noquote() << tr("本次实验数据存储路径：") << fileSaveDir;

    // 保存本地测量参数信息
    GlobalSettings settings(CONFIG_FILENAME);
    settings.setValue("Global/ShotNum", ui->lineEdit_shotNum->text());
    settings.setValue("Global/ShotNumIsAutoIncrease", ui->checkBox_autoIncrease->isChecked());
    settings.setValue("Global/CacheDir", ui->lineEdit_savePath->text());
    settings.setValue("Global/CacheDir", ui->lineEdit_savePath->text());
    QFile::copy(CONFIG_FILENAME, fileSaveDir + "/Settings.ini");

    // 发送指令集
    mPCIeCommSdk.writeStartMeasure();
    //mPCIeCommSdk.setCaptureParamter(ui->comboBox_channel->currentIndex()+1, ui->spinBox_timeNode1->value(), ui->spinBox_timeNode2->value(), ui->spinBox_timeNode3->value());

    mPCIeCommSdk.startAllCapture(fileSaveDir,
                             ui->spinBox_timeLength->value(),
                             ui->lineEdit_shotNum->text());

}


void MainWindow::on_action_stopMeasure_triggered()
{
    // 发送指令集
    mPCIeCommSdk.writeStopMeasure();

    mPCIeCommSdk.stopAllCapture();

    mCommHelper->disconnectServer();
}


void MainWindow::on_action_about_triggered()
{
    QString filename = QFileInfo(QCoreApplication::applicationFilePath()).baseName();
    QMessageBox::about(this, tr("关于"),
                       QString("<p>") +
                           tr("版本") +
                           QString("</p><span style='color:blue;'>%1</span><p>").arg(filename).arg(APP_VERSION) +
                           tr("提交") +
                           QString("</p><span style='color:blue;'>%1: %2</span><p>").arg(GIT_BRANCH).arg(GIT_HASH) +
                           tr("日期") +
                           QString("</p><span style='color:blue;'>%1</span><p>").arg(GIT_DATE) +
                           tr("开发者") +
                           QString("</p><span style='color:blue;'>MaoXiaoqing</span><p>") +
                           "</p><p>四川大学物理学院 版权所有 (C) 2025</p>"
                       );
}

void MainWindow::on_action_aboutQt_triggered()
{
    QMessageBox::aboutQt(this);
}


void MainWindow::on_action_lightTheme_triggered()
{
    if(!mIsDarkTheme) return;
    mIsDarkTheme = false;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","false");
    applyColorTheme();
}


void MainWindow::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","true");
    applyColorTheme();
}


void MainWindow::on_action_colorTheme_triggered()
{
    GlobalSettings settings;
    QColor color = QColorDialog::getColor(mThemeColor, this, tr("选择颜色"));
    if (color.isValid()) {
        mThemeColor = color;
        mThemeColorEnable = true;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
        settings.setValue("Global/Startup/themeColor",mThemeColor);
    } else {
        mThemeColorEnable = false;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    }
    settings.setValue("Global/Startup/themeColorEnable",mThemeColorEnable);
    applyColorTheme();
}

void MainWindow::applyColorTheme()
{
    QList<QCustomPlot*> customPlots = this->findChildren<QCustomPlot*>();
    for (auto customPlot : customPlots){
        QPalette palette = customPlot->palette();
        if (mIsDarkTheme)
        {
            if (this->mThemeColorEnable)
            {
                CustomColorDarkStyle darkStyle(mThemeColor);
                darkStyle.polish(palette);
            }
            else
            {
                DarkStyle darkStyle;
                darkStyle.polish(palette);
            }
        }
        else
        {
            if (this->mThemeColorEnable)
            {
                CustomColorLightStyle lightStyle(mThemeColor);
                lightStyle.polish(palette);
            }
            else
            {
                LightStyle lightStyle;
                lightStyle.polish(palette);
            }
        }
        //日志窗体
        QString styleSheet = mIsDarkTheme ?
                                 QString("background-color:rgb(%1,%2,%3);color:white;")
                                    .arg(palette.color(QPalette::Dark).red())
                                    .arg(palette.color(QPalette::Dark).green())
                                    .arg(palette.color(QPalette::Dark).blue())
                                          : QString("background-color:white;color:black;");
        ui->logWidget->setStyleSheet(styleSheet);

        //更新样式表
        QList<QCheckBox*> checkBoxs = customPlot->findChildren<QCheckBox*>();
        int i = 0;
        for (auto checkBox : checkBoxs){
            checkBox->setStyleSheet(styleSheet);
        }

        QGraphicsScene *scene = this->findChild<QGraphicsScene*>("logGraphicsScene");
        QGraphicsTextItem *textItem = (QGraphicsTextItem*)scene->items()[0];
        textItem->setHtml(mIsDarkTheme ? QString("<font color='white'>工作日志</font>") : QString("<font color='black'>工作日志</font>"));

        QCPColorMap *colorMap = qobject_cast<QCPColorMap*>(customPlot->plottable("colorMap"));
        if (colorMap){
            colorMap->colorScale()->axis()->axisRect()->axis(QCPAxis::atBottom)->setTickLabelColor(mIsDarkTheme ? Qt::white : Qt::black); // 设置底部轴的刻度标签颜色
            colorMap->colorScale()->axis()->axisRect()->axis(QCPAxis::atRight)->setTickLabelColor(mIsDarkTheme ? Qt::white : Qt::black); // 设置右侧轴的刻度标签颜色
        }

        // 窗体背景色
        customPlot->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Dark) : Qt::white));
        // 四边安装轴并显示
        customPlot->axisRect()->setupFullAxesBox();
        customPlot->axisRect()->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Dark) : Qt::white));
        // 坐标轴线颜色
        customPlot->xAxis->setBasePen(QPen(palette.color(QPalette::WindowText)));
        customPlot->xAxis2->setBasePen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis->setBasePen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis2->setBasePen(QPen(palette.color(QPalette::WindowText)));
        // 刻度线颜色
        customPlot->xAxis->setTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->xAxis2->setTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis->setTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis2->setTickPen(QPen(palette.color(QPalette::WindowText)));
        // 子刻度线颜色
        customPlot->xAxis->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->xAxis2->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis2->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        // 坐标轴文本标签颜色
        customPlot->xAxis->setLabelColor(palette.color(QPalette::WindowText));
        customPlot->xAxis2->setLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis->setLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis2->setLabelColor(palette.color(QPalette::WindowText));
        // 坐标轴刻度文本标签颜色
        customPlot->xAxis->setTickLabelColor(palette.color(QPalette::WindowText));
        customPlot->xAxis2->setTickLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis->setTickLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis2->setTickLabelColor(palette.color(QPalette::WindowText));

        customPlot->replot();
    }
}

void MainWindow::restoreSettings()
{
    GlobalSettings settings;
    if(mainWindow) {
        mainWindow->restoreGeometry(settings.value("MainWindow/Geometry").toByteArray());
        mainWindow->restoreState(settings.value("MainWindow/State").toByteArray());
    } else {
        restoreGeometry(settings.value("MainWindow/Geometry").toByteArray());
        restoreState(settings.value("MainWindow/State").toByteArray());
    }
    mThemeColor = settings.value("Global/Startup/themeColor",QColor(30,30,30)).value<QColor>();
    mThemeColorEnable = settings.value("Global/Startup/themeColorEnable",true).toBool();
    if(mThemeColorEnable) {
        QTimer::singleShot(0, this, [&](){
            qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
            QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
        });
    }
}

qint64 MainWindow::getDiskFreeSpace(const QString& disk)
{
    qint64 ret = 0;
    QList<QStorageInfo> storageInfoList = QStorageInfo::mountedVolumes();
    for (auto storageInfo : storageInfoList){
        if (disk.startsWith(storageInfo.rootPath())){
            ret = storageInfo.bytesAvailable();// / (1024*1024);//返回MB
            break;
        }
    }

    return ret;
}

QPixmap MainWindow::maskPixmap(QPixmap pixmap, QSize sz, QColor clrMask)
{
    // 更新图标颜色
    QPixmap result = pixmap.scaled(sz);
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), clrMask);
    return result;
}

QPixmap MainWindow::roundPixmap(QSize sz, QColor clrOut)
{
    QPixmap result(sz);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing,true);

    QPainterPath bigCirclePath;
    bigCirclePath.addEllipse(0, 0, sz.width(), sz.height());
    painter.fillPath(bigCirclePath, QBrush(clrOut));

    return result;
}

QPixmap MainWindow::dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut)
{
    QPixmap result(sz);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing,true);

    QPainterPath bigCirclePath;
    bigCirclePath.addEllipse(1, 1, sz.width()-2, sz.height()-2);
    painter.setPen(QPen(QBrush(clrOut), 2, Qt::SolidLine));
    painter.drawPath(bigCirclePath);

    QPainterPath smallCirclePath;
    smallCirclePath.addEllipse(4, 4, sz.width() - 8, sz.height() - 8);
    painter.fillPath(smallCirclePath, QBrush(clrIn));

    return result;
}


void MainWindow::on_action_linear_triggered(bool checked)
{
    QList<QCustomPlot*> customPlots = this->findChildren<QCustomPlot*>();
    for (auto customPlot : customPlots){
        customPlot->yAxis->setSubTicks(false);
        customPlot->yAxis2->setSubTicks(false);

        QSharedPointer<QCPAxisTicker> ticker(new QCPAxisTicker);
        customPlot->yAxis->setTicker(ticker);

        customPlot->yAxis->setScaleType(QCPAxis::ScaleType::stLinear);
        customPlot->yAxis->setNumberFormat("f");
        customPlot->yAxis->setNumberPrecision(0);

        customPlot->replot();
    }
}


void MainWindow::on_action_logarithm_triggered(bool checked)
{
    QList<QCustomPlot*> customPlots = this->findChildren<QCustomPlot*>();
    for (auto customPlot : customPlots){
        customPlot->yAxis->setSubTicks(true);
        customPlot->yAxis2->setSubTicks(true);

        customPlot->yAxis->setScaleType(QCPAxis::ScaleType::stLogarithmic);
        customPlot->yAxis->setNumberFormat("eb");//使用科学计数法表示刻度
        customPlot->yAxis->setNumberPrecision(0);//小数点后面小数位数

        QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
        customPlot->yAxis->setTicker(logTicker);
        customPlot->replot();
    }
}

void MainWindow::on_pushButton_openPower_clicked()
{
    for (int moduleNo=1; moduleNo<=18; ++moduleNo)
        mPCIeCommSdk.switchPower(moduleNo, true);
}


void MainWindow::on_pushButton_closePower_clicked()
{
    for (int moduleNo=1; moduleNo<=18; ++moduleNo){
        mPCIeCommSdk.switchPower(moduleNo, false);
        mPCIeCommSdk.switchBackupPower(moduleNo, true);
    }
}


void MainWindow::on_pushButton_openPower_2_clicked()
{
    for (int moduleNo=1; moduleNo<=18; ++moduleNo){
        mPCIeCommSdk.switchBackupPower(moduleNo, true);
        mPCIeCommSdk.switchPower(moduleNo, false);
    }
}


void MainWindow::on_pushButton_closePower_2_clicked()
{
    for (int moduleNo=1; moduleNo<=18; ++moduleNo){
        mPCIeCommSdk.switchBackupPower(moduleNo, false);
        mPCIeCommSdk.switchPower(moduleNo, true);
    }
}


void MainWindow::on_pushButton_openVoltage_clicked()
{
    for (int moduleNo=1; moduleNo<=18; ++moduleNo){
        mPCIeCommSdk.switchVoltage(moduleNo, true);
        mPCIeCommSdk.switchBackupVoltage(moduleNo, false);
    }
}


void MainWindow::on_pushButton_closeVoltage_clicked()
{
    for (int moduleNo=1; moduleNo<=18; ++moduleNo){
        mPCIeCommSdk.switchVoltage(moduleNo, false);
        mPCIeCommSdk.switchBackupVoltage(moduleNo, true);
    }
}


void MainWindow::on_pushButton_openVoltage_2_clicked()
{
    for (int moduleNo=1; moduleNo<=18; ++moduleNo){
        mPCIeCommSdk.switchBackupVoltage(moduleNo, true);
        mPCIeCommSdk.switchVoltage(moduleNo, false);
    }
}


void MainWindow::on_pushButton_closeVoltage_2_clicked()
{
    for (int moduleNo=1; moduleNo<=18; ++moduleNo){
        mPCIeCommSdk.switchBackupVoltage(moduleNo, false);
        mPCIeCommSdk.switchVoltage(moduleNo, true);
    }
}


void MainWindow::on_action_cfgParam_triggered()
{
    mDetSettingWindow->show();
}


void MainWindow::on_action_cfgNet_triggered()
{
    //@01*999*GET*01*#
    //@01*999*GETR*01*
    /*
    -----Voltage &Current Data-----
    IHA238_01 = 0.00V, 0.00mA
    IHA238_02 = 0.00V, 0.00mA
    IHA238_03 = 0.00V, 0.00mA
    IHA238_04 = 0.00V, 0.00mA
    IHA238_05 = 0.00V. 0.00mA
    IHA238_06 = 0.00V, 0.00mA
    IHA238_07 = 0.00V, 0.00mA
    IHA238_08 = 0.00V, 0.00mA
    THA238_09 = 0.00V, 0.00mA
    THA238_10 = 0.00V, 0.00mA
    IHA238_11 = 0.00V, 0.00mA
    IHA238_12 = 0.00V, 0.00mA

    -----Temperature Data-----
    DS18B20_01 = 0.00°C
    DS18B20_02 = 0.00°C
    DS18B20_03 = 0.00°C
    DS18B20_04 = 0.00°C

    -----Device Info-----
    UTD = 0x3938353834315100002F0017
    FeSoftV=Unknown
    FeHardy=Unknown
    FeAddr = 01
    #
    */
    mNetSettingWindow->show();
}


void MainWindow::on_pushButton_selChannel1_clicked()
{
    QList<SwitchButton*> buttons = this->findChildren<SwitchButton*>();
    for (auto btn : buttons){
        if (btn->property("isSelChannel").toBool()){
            btn->setChecked(true);
        }
    }
}


void MainWindow::on_pushButton_selChannel2_clicked()
{
    QList<SwitchButton*> buttons = this->findChildren<SwitchButton*>();
    for (auto btn : buttons){
        if (btn->property("isSelChannel").toBool()){
            btn->setChecked(false);
        }
    }
}


void MainWindow::on_action_typeLSD_triggered(bool checked)
{
    if (checked){
        ui->tableWidget_camera->setColumnHidden(6, true);

        quint16 maxWidth = 0;
        for (int i=0; i<ui->tableWidget_camera->columnCount(); ++i){
            if (!ui->tableWidget_camera->isColumnHidden(i))
                maxWidth += ui->tableWidget_camera->columnWidth(i);
        }
        ui->tableWidget_camera->setFixedWidth(maxWidth);
        ui->leftStackedWidget->setFixedWidth(maxWidth+2);

        ui->label_12->hide();
        ui->label_18->hide();
        ui->spinBox_timeNode2->hide();
        ui->spinBox_timeNode3->hide();
        ui->pushButton_selChannel1->hide();
        ui->pushButton_selChannel2->hide();

        if (!ui->action_status->isChecked())
            ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_LSD);//spectroMeterPageInfoWidget_LSD
        this->setWindowTitle(QApplication::applicationName() + "LSD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "LSD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "LSD");
    }
}


void MainWindow::on_action_typePSD_triggered(bool checked)
{
    if (checked){
        ui->tableWidget_camera->setColumnHidden(6, false);

        quint16 maxWidth = 0;
        for (int i=0; i<ui->tableWidget_camera->columnCount(); ++i){
            if (!ui->tableWidget_camera->isColumnHidden(i))
                maxWidth += ui->tableWidget_camera->columnWidth(i);
        }
        ui->tableWidget_camera->setFixedWidth(maxWidth);
        ui->leftStackedWidget->setFixedWidth(maxWidth+2);

        ui->label_12->show();
        ui->label_18->show();
        ui->spinBox_timeNode2->show();
        ui->spinBox_timeNode3->show();
        ui->pushButton_selChannel1->show();
        ui->pushButton_selChannel2->show();

        if (!ui->action_status->isChecked())
            ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_PSD);
        this->setWindowTitle(QApplication::applicationName() + "PSD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "PSD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "PSD");
    }
}


void MainWindow::on_action_typeLBD_triggered(bool checked)
{
    if (checked){
        ui->tableWidget_camera->setColumnHidden(6, true);

        quint16 maxWidth = 0;
        for (int i=0; i<ui->tableWidget_camera->columnCount(); ++i){
            if (!ui->tableWidget_camera->isColumnHidden(i))
                maxWidth += ui->tableWidget_camera->columnWidth(i);
        }
        ui->tableWidget_camera->setFixedWidth(maxWidth);
        ui->leftStackedWidget->setFixedWidth(maxWidth+2);

        ui->label_12->show();
        ui->label_18->show();
        ui->spinBox_timeNode2->show();
        ui->spinBox_timeNode3->show();
        ui->pushButton_selChannel1->hide();
        ui->pushButton_selChannel2->hide();

        if (!ui->action_status->isChecked())
            ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_LBD);
        this->setWindowTitle(QApplication::applicationName() + "LBD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "LBD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "LBD");
    }
}


void MainWindow::replyNeutronSpectrum(quint8 timestampIndex, quint8 cameraOrientation, QVector<QPair<double,double>>& pairs)
{
    //实测曲线
    QCustomPlot* customPlot = nullptr;
    if (ui->action_typeLSD->isChecked()){
        customPlot = ui->spectroMeter_neutronSpectrum;
    }
    else{
        if (timestampIndex == 1){
            customPlot = ui->spectroMeter_time1_PSD;
        }
        else if (timestampIndex == 2){
            customPlot = ui->spectroMeter_time2_PSD;
        }
        else if (timestampIndex == 3){
            customPlot = ui->spectroMeter_time3_PSD;
        }
    }

    QVector<double> keys, values;
    quint16 yMin = 1e10;
    quint16 yMax = 0;
    for (auto pair : pairs){
        keys << pair.first ;
        values << pair.second;
        yMin = qMin(yMin, (quint16)pair.second);
        yMax = qMax(yMax, (quint16)pair.second);
    }

    double spaceDisc = 0;
    if (yMax != yMin){
        spaceDisc = (yMax - yMin) * 0.15;
    }

    if (cameraOrientation == PCIeCommSdk::CameraOrientation::Horizontal)
        customPlot->graph(0)->setData(keys, values);
    else
        customPlot->graph(1)->setData(keys, values);

    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);
    //customPlot->yAxis->setRange(yMin-spaceDisc, yMax+spaceDisc);
    customPlot->replot(QCustomPlot::rpQueuedReplot);
}


void MainWindow::replyGammaSpectrum(quint8 timestampIndex, quint8 cameraOrientation, QVector<QPair<double,double>>& pairs)
{
    //实测曲线
    QCustomPlot* customPlot = nullptr;
    if (ui->action_typeLSD->isChecked()){
        customPlot = ui->spectroMeter_gammaSpectrum;
    }
    else{
        if (timestampIndex == 1){
            customPlot = ui->spectroMeter_time1_LBD;
        }
        else if (timestampIndex == 2){
            customPlot = ui->spectroMeter_time2_LBD;
        }
        else if (timestampIndex == 3){
            customPlot = ui->spectroMeter_time3_LBD;
        }
    }

    QVector<double> keys, values;
    quint16 yMin = 1e10;
    quint16 yMax = 0;
    for (auto pair : pairs){
        keys << pair.first ;
        values << pair.second;
        yMin = qMin(yMin, (quint16)pair.second);
        yMax = qMax(yMax, (quint16)pair.second);
    }

    double spaceDisc = 0;
    if (yMax != yMin){
        spaceDisc = (yMax - yMin) * 0.15;
    }

    if (cameraOrientation == PCIeCommSdk::CameraOrientation::Horizontal)
        customPlot->graph(0)->setData(keys, values);
    else
        customPlot->graph(1)->setData(keys, values);

    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);
    //customPlot->yAxis->setRange(yMin-spaceDisc, yMax+spaceDisc);
    customPlot->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::on_pushButton_preview_clicked()
{
    // 将随机数转换到网格坐标中来
    // QVector<QVector<quint16>> ref(gridColumn, QVector<quint16>(gridRow, 0));
    // for (const auto& pair : pairs) {
    //     int keyIndex, valueIndex;
    //     colorMap->data()->coordToCell(pair.first, pair.second, &keyIndex, &valueIndex);
    //     if (keyIndex>=0 && keyIndex<gridColumn && valueIndex>=0 && valueIndex<gridRow)
    //         ref[keyIndex][valueIndex]++;
    //     else{
    //         qDebug() << "invalid data:" << pair.first << pair.second << keyIndex << valueIndex;
    //     }
    // }

    //LBD r能谱
    if (ui->action_typeLBD->isChecked()){
        QVector<QPair<double ,double>> spectrumPair[6];
        QFile file("./spectrum_lbd_r.csv");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
            QTextStream stream(&file);
            stream.readLine();//过滤掉标题头
            while (!stream.atEnd())
            {
                QString line = stream.readLine();
                QStringList row = line.split(',', Qt::SkipEmptyParts);
                if (row.size() >= 9)
                {
                    spectrumPair[0].push_back(qMakePair(row.at(0).toDouble(), row.at(1).toDouble()));
                    spectrumPair[1].push_back(qMakePair(row.at(0).toDouble(), row.at(2).toDouble()));
                    spectrumPair[2].push_back(qMakePair(row.at(3).toDouble(), row.at(4).toDouble()));
                    spectrumPair[3].push_back(qMakePair(row.at(3).toDouble(), row.at(5).toDouble()));
                    spectrumPair[4].push_back(qMakePair(row.at(6).toDouble(), row.at(7).toDouble()));
                    spectrumPair[5].push_back(qMakePair(row.at(6).toDouble(), row.at(8).toDouble()));
                }
            }
            file.close();

            emit reportGammaSpectrum(1, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[0]);
            emit reportGammaSpectrum(1, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[1]);

            emit reportGammaSpectrum(2, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[2]);
            emit reportGammaSpectrum(2, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[3]);

            emit reportGammaSpectrum(3, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[4]);
            emit reportGammaSpectrum(3, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[5]);
        }
    }

    //PSD n能谱
    if (ui->action_typePSD->isChecked()){
        QVector<QPair<double ,double>> spectrumPair[6];
        QFile file("./spectrum_psd_n.csv");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
            QTextStream stream(&file);
            stream.readLine();//过滤掉标题头
            while (!stream.atEnd())
            {
                QString line = stream.readLine();
                QStringList row = line.split(',', Qt::SkipEmptyParts);
                if (row.size() >= 9)
                {
                    spectrumPair[0].push_back(qMakePair(row.at(0).toDouble(), row.at(1).toDouble()));
                    spectrumPair[1].push_back(qMakePair(row.at(0).toDouble(), row.at(2).toDouble()));
                    spectrumPair[2].push_back(qMakePair(row.at(3).toDouble(), row.at(4).toDouble()));
                    spectrumPair[3].push_back(qMakePair(row.at(3).toDouble(), row.at(5).toDouble()));
                    spectrumPair[4].push_back(qMakePair(row.at(6).toDouble(), row.at(7).toDouble()));
                    spectrumPair[5].push_back(qMakePair(row.at(6).toDouble(), row.at(8).toDouble()));
                }
            }
            file.close();

            emit reportNeutronSpectrum(1, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[0]);
            emit reportNeutronSpectrum(1, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[1]);

            emit reportNeutronSpectrum(2, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[2]);
            emit reportNeutronSpectrum(2, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[3]);

            emit reportNeutronSpectrum(3, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[4]);
            emit reportNeutronSpectrum(3, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[5]);
        }
    }

    //LSD
    if (ui->action_typeLSD->isChecked()){
        // n能谱
        {
            QVector<QPair<double ,double>> spectrumPair[2];
            QFile file("./spectrum_lsd_n.csv");
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
                QTextStream stream(&file);
                stream.readLine();//过滤掉标题头
                while (!stream.atEnd())
                {
                    QString line = stream.readLine();
                    QStringList row = line.split(',', Qt::SkipEmptyParts);
                    if (row.size() >= 3)
                    {
                        spectrumPair[0].push_back(qMakePair(row.at(0).toDouble(), row.at(1).toDouble()));
                        spectrumPair[1].push_back(qMakePair(row.at(0).toDouble(), row.at(2).toDouble()));
                    }
                }
                file.close();

                emit reportNeutronSpectrum(1, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[0]);
                emit reportNeutronSpectrum(1, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[1]);
            }
        }

        //r能谱
        {
            QVector<QPair<double ,double>> spectrumPair[2];
            QFile file("./spectrum_lsd_r.csv");
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
                QTextStream stream(&file);
                stream.readLine();//过滤掉标题头
                while (!stream.atEnd())
                {
                    QString line = stream.readLine();
                    QStringList row = line.split(',', Qt::SkipEmptyParts);
                    if (row.size() >= 3)
                    {
                        spectrumPair[0].push_back(qMakePair(row.at(0).toDouble(), row.at(1).toUInt()));
                        spectrumPair[1].push_back(qMakePair(row.at(0).toDouble(), row.at(2).toUInt()));
                    }
                }
                file.close();

                emit reportGammaSpectrum(1, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[0]);
                emit reportGammaSpectrum(1, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[1]);
            }
        }
    }
}


void MainWindow::on_action_init_triggered()
{
    if (mCommHelper->connectServer()){
        qInfo().noquote() << tr("初始化成功");
        ui->action_startMeasure->setEnabled(true);
        ui->action_stopMeasure->setEnabled(false);

        ui->tableWidget_camera->setEnabled(true);
        ui->pushButton_openPower->setEnabled(true);
        ui->pushButton_closePower->setEnabled(true);
        ui->pushButton_openVoltage->setEnabled(true);
        ui->pushButton_closeVoltage->setEnabled(true);
        ui->pushButton_selChannel1->setEnabled(true);
        ui->pushButton_selChannel2->setEnabled(true);
    }
    else{
        qInfo().noquote() << tr("初始化失败");
    }

    emit mPCIeCommSdk.replySettingFinished();
}

void MainWindow::on_action_clock_triggered(bool checked)
{
    if (checked)
        qInfo().noquote() << tr("启用时钟同步");
    else
        qInfo().noquote() << tr("禁用时钟同步");

    mEnableAutoUpdateShotnum = checked;
}


void MainWindow::on_action_shotNum_triggered(bool checked)
{
    if (checked)
        qInfo().noquote() << tr("启用自动更新炮号");
    else
        qInfo().noquote() << tr("禁用自动更新炮号");

    mEnableClockSynchronization = checked;
}


void MainWindow::on_action_stop_triggered(bool checked)
{
    if (checked)
        qInfo().noquote() << tr("启用紧急停机");
    else
        qInfo().noquote() << tr("禁用紧急停机");

    mEnableEmergencyStop = checked;
}


void MainWindow::on_action_status_triggered(bool checked)
{
    if (checked){      
        ui->stackedWidget->setCurrentWidget(ui->statusMonitorPageInfoWidget);
    }
    else{
        if (ui->action_typePSD->isChecked())
            ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_PSD);
        else
            ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_LBD);
    }
}

