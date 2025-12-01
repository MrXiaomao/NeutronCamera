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
    connect(this, SIGNAL(reportKernelDensitySpectrumPSD(quint8,QVector<QPair<double,double>>&)), this, SLOT(replyKernelDensitySpectrumPSD(quint8,QVector<QPair<double,double>>&)));
    connect(this, SIGNAL(reportKernelDensitySpectrumFoM(quint8,QVector<QVector<QPair<double,double>>>&)), this, SLOT(replyKernelDensitySpectrumFoM(quint8,QVector<QVector<QPair<double,double>>>&)));
    connect(this, SIGNAL(reportSpectrum(quint8,quint8,QVector<QPair<quint16,quint16>>&)), this, SLOT(replySpectrum(quint8,quint8,QVector<QPair<quint16,quint16>>&)));

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
    //connect(&mPCIeCommSdk, &PCIeCommSdk::reportWaveform, this, &MainWindow::replyWaveform);

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
    ui->widgetTemperature->setUnit("℃");
    ui->widgetTemperature->setText(tr("温度(℃)"));
    ui->widgetTemperature->setPrecision(2);
    ui->widgetTemperature->setRange(0, 10);
    // ui->widgetTemperature->setWarnThold(80);
    // ui->widgetTemperature->setAlarmThold(120);
    ui->widgetTemperature->setValue(0);

    ui->widgetVoltage->setUnit("V");
    ui->widgetVoltage->setText(tr("电压(V)"));
    ui->widgetVoltage->setPrecision(2);
    ui->widgetVoltage->setRange(0, 10);
    // ui->widgetVoltage->setWarnThold(120);
    // ui->widgetVoltage->setAlarmThold(180);
    ui->widgetVoltage->setValue(0);

    mDetSettingWindow = new DetSettingWindow();
    mDetSettingWindow->setWindowFlags(Qt::Widget | Qt::WindowStaysOnTopHint);
    mDetSettingWindow->hide();
    connect(mDetSettingWindow, &DetSettingWindow::reportSettingFinished, &mPCIeCommSdk, &PCIeCommSdk::replySettingFinished);

    mNetSettingWindow = new NetSettingWindow();
    mNetSettingWindow->setWindowFlags(Qt::Widget | Qt::WindowStaysOnTopHint);
    mNetSettingWindow->hide();

    {
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);//编号
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);//电源
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);//偏压
        ui->tableWidget_camera->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);//选通
        ui->tableWidget_camera->horizontalHeader()->setFixedHeight(30);
        ui->tableWidget_camera->setColumnWidth(0, 35);
        ui->tableWidget_camera->setColumnWidth(1, 50);
        ui->tableWidget_camera->setColumnWidth(2, 77);
        ui->tableWidget_camera->setColumnWidth(3, 77);
        ui->tableWidget_camera->setColumnWidth(4, 77);
        ui->leftStackedWidget->setFixedWidth(320);
        for (int i=0; i<DETNUMBER_MAX; ++i)
            ui->tableWidget_camera->setRowHeight(i, 30);
        ui->tableWidget_camera->setFixedHeight(572);

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
            for (int column=2; column<=4; ++column){
                SwitchButton* cell = new SwitchButton(this);
                if (column == 2){
                    cell->setProperty("isPower", true);
                    cell->setProperty("index", row + 1);
                }
                else if (column == 3){
                    cell->setProperty("isVoltage", true);
                    cell->setProperty("index", row + 1);
                }
                else {
                    cell->setText("#2", "#1");
                    cell->setProperty("isSelChannel", true);
                    cell->setProperty("index", row + 1);

                    QColor bgColorOff = cell->getBgColorOff();
                    QColor bgColorOn = cell->getBgColorOn();
                    bgColorOff = QColor(82, 0, 195);
                    cell->setBgColor(bgColorOff, bgColorOn);
                }
                ui->tableWidget_camera->setCellWidget(row, column, cell);
                cell->setChecked(true);

                connect(cell, &SwitchButton::toggled, this, [=](bool checked){
                    SwitchButton* button = qobject_cast<SwitchButton*>(sender());
                    if (checked){
                        if (column == 2){
                            mPCIeCommSdk.switchPower(row + 1, checked);
                        }
                        else if (column == 3) {
                            mPCIeCommSdk.switchVoltage(row + 1, checked);
                        }
                        else if (column == 4) {
                            mPCIeCommSdk.switchChannel(row + 1, checked);
                        }
                    }
                });
            }
        }
    }

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

        //判断磁盘空间
        {
            qint8 channelCount = 3;
            qint32 timeBase = 50;//打包时长基数
            qint64 onePacketSize = 268435456;//打包大小基数
            QString cacheDir = ui->lineEdit_savePath->text();
            QDir dir(cacheDir);
            qint64 diskFreeSpace = getDiskFreeSpace(dir.absoluteFilePath(cacheDir));
            qint32 canRecordSeconds = diskFreeSpace / (onePacketSize*1000*channelCount/timeBase);
            this->findChild<QLabel*>("label_alarm")->setToolTip(QString("可录时长：%1秒").arg(canRecordSeconds));
            // 这里只分3个阶段吧（30秒 5分钟 60分钟）
            if (diskFreeSpace <= (qint64(onePacketSize*30000*channelCount) / timeBase)){
                //不足30秒 红色
                QPixmap pixmap = maskPixmap(QPixmap(":/resource/image/tip.png"), QSize(24, 24), Qt::red);
                this->findChild<QLabel*>("label_alarm")->setPixmap(pixmap);
            }
            else if (diskFreeSpace <= (qint64(onePacketSize*5*60000*channelCount) / timeBase)){
                //不足5分钟 橙色
                QPixmap pixmap = maskPixmap(QPixmap(":/resource/image/tip.png"), QSize(24, 24), QColor::fromRgb(0xff,0xa5,0x00,0xff));
                this->findChild<QLabel*>("label_alarm")->setPixmap(pixmap);
            }
            else if (diskFreeSpace <= (qint64(onePacketSize*60*60000*channelCount) / timeBase)){
                //不足1小时 绿色
                QPixmap pixmap = maskPixmap(QPixmap(":/resource/image/tip.png"), QSize(24, 24), QColor::fromRgb(0x00,0xff,0x00,0xff));
                this->findChild<QLabel*>("label_alarm")->setPixmap(pixmap);
            }
            else{
                //草绿色
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
            QSplitter *splitterH1 = new QSplitter(Qt::Horizontal,ui->spectroMeterPageInfoWidget_LSD);
            splitterH1->setHandleWidth(2);
            splitterH1->addWidget(ui->spectroMeter_horCamera_PSD);
            splitterH1->addWidget(ui->spectroMeter_horCamera_FOM);
            splitterH1->setSizes(QList<int>() << 100000 << 100000);
            splitterH1->setCollapsible(0,false);
            splitterH1->setCollapsible(1,false);

            QSplitter *splitterH2 = new QSplitter(Qt::Horizontal,ui->spectroMeterPageInfoWidget_LSD);
            splitterH2->setHandleWidth(2);
            splitterH2->addWidget(ui->spectroMeter_verCamera_PSD);
            splitterH2->addWidget(ui->spectroMeter_verCamera_FOM);
            splitterH2->setSizes(QList<int>() << 100000 << 100000);
            splitterH2->setCollapsible(0,false);
            splitterH2->setCollapsible(1,false);

            QSplitter *splitterV1 = new QSplitter(Qt::Vertical,ui->spectroMeterPageInfoWidget_LSD);
            splitterV1->setHandleWidth(2);
            splitterV1->addWidget(ui->spectroMeter_top);
            splitterV1->addWidget(splitterH1);
            splitterV1->addWidget(splitterH2);
            splitterV1->setSizes(QList<int>() << 100000 << 400000 << 400000);
            splitterV1->setCollapsible(0,false);
            splitterV1->setCollapsible(1,false);
            splitterV1->setCollapsible(2,false);

            ui->spectroMeterPageInfoWidget_LSD->layout()->addWidget(splitterV1);
        }
        {
            QSplitter *splitterV1 = new QSplitter(Qt::Vertical,ui->spectroMeterPageInfoWidget_PSD_LBD);
            splitterV1->setHandleWidth(2);
            splitterV1->addWidget(ui->spectroMeter_time1);
            splitterV1->addWidget(ui->spectroMeter_time2);
            splitterV1->addWidget(ui->spectroMeter_time3);
            splitterV1->setSizes(QList<int>() << 100000 << 100000 << 100000);
            splitterV1->setCollapsible(0,false);
            splitterV1->setCollapsible(1,false);
            splitterV1->setCollapsible(2,false);
            ui->spectroMeterPageInfoWidget_PSD_LBD->layout()->addWidget(splitterV1);
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
    QPushButton* statusButton = nullptr;
    QPushButton* controlButton = nullptr;
    {        
        {
            statusButton = new QPushButton();
            statusButton->setText(tr("状态监测"));
            statusButton->setFixedSize(250,29);
            statusButton->setCheckable(true);

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
        sideHboxLayout->addWidget(statusButton);
        sideHboxLayout->addWidget(controlButton);

        QGraphicsScene *scene = new QGraphicsScene(this);
        QGraphicsProxyWidget *w = scene->addWidget(sideProxyWidget);
        w->setPos(0,0);
        w->setRotation(-90);
        ui->graphicsView_2->setScene(scene);
        ui->graphicsView_2->setFrameStyle(0);
        ui->graphicsView_2->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView_2->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView_2->setFixedSize(30, 500);
        ui->leftSidewidget->setFixedWidth(30);

        connect(statusButton,&QPushButton::clicked,this,[=](/*bool checked*/){
            controlButton->setChecked(false);

            if(ui->leftStackedWidget->isHidden()) {
                ui->leftStackedWidget->setCurrentWidget(ui->detectorStatusWidget);
                ui->leftStackedWidget->show();            

                GlobalSettings settings;
                settings.setValue("Global/DefaultPage", "detectorStatus");
            } else {
                if(ui->leftStackedWidget->currentWidget() == ui->detectorStatusWidget) {
                    ui->leftStackedWidget->hide();

                    statusButton->setChecked(false);
                    GlobalSettings settings;
                    settings.setValue("Global/DefaultPage", "");
                } else {
                    ui->leftStackedWidget->setCurrentWidget(ui->detectorStatusWidget);

                    GlobalSettings settings;
                    settings.setValue("Global/DefaultPage", "detectorStatus");
                }
            }
        });

        connect(controlButton,&QPushButton::clicked,this,[=](/*bool checked*/){
            statusButton->setChecked(false);

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

        connect(ui->toolButton_closeDetectorStatusWidget,&QPushButton::clicked,this,[=](){
            ui->leftStackedWidget->hide();
            statusButton->setChecked(false);

            GlobalSettings settings;
            settings.setValue("Global/DefaultPage", "");
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
        else if (settings.value("Global/DefaultPage").toString() == "detectorStatus"){
            ui->leftStackedWidget->setCurrentWidget(ui->detectorStatusWidget);
            statusButton->setChecked(true);
        }
        else{
            ui->leftStackedWidget->hide();
            controlButton->setChecked(false);
            statusButton->setChecked(false);
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
        ui->spinBox_shotNum->setValue(settings.value("Global/ShotNum", 100).toUInt());
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

    initCustomPlot(ui->spectroMeter_top, tr(""), tr("Energy Counts"));
    initCustomPlot(ui->spectroMeter_horCamera_PSD, tr("水平 n:γ Energy(keVee) PSD图"), tr(""));
    initCustomPlot(ui->spectroMeter_horCamera_FOM, tr("水平 FOM图"), tr(""));
    initCustomPlot(ui->spectroMeter_verCamera_PSD, tr("垂直 n:γ Energy(keVee) PSD图"), tr(""));
    initCustomPlot(ui->spectroMeter_verCamera_FOM, tr("垂直 FOM图"), tr(""));

    initCustomPlot(ui->spectroMeter_time1, tr("时刻1 Energy"), tr("Counts"));
    initCustomPlot(ui->spectroMeter_time2, tr("时刻2 Energy"), tr("Counts"));
    initCustomPlot(ui->spectroMeter_time3, tr("时刻3 Energy"), tr("Counts"));

    connect(mCommHelper, &CommHelper::reportTemperature, this, [=](float v){
        ui->widgetTemperature->setValue(v);
    });
    connect(mCommHelper, &CommHelper::reportVoltage, this, [=](float v){
        ui->widgetVoltage->setValue(v);
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

    if (customPlot == ui->spectroMeter_time1 || customPlot == ui->spectroMeter_time2 || customPlot == ui->spectroMeter_time3){
        //默认对数
        QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
        customPlot->yAxis->setTicker(logTicker);
        //customPlot->yAxis2->setTicker(logTicker);
        customPlot->yAxis->setScaleType(QCPAxis::ScaleType::stLogarithmic);
        customPlot->yAxis->setNumberFormat("eb");//使用科学计数法表示刻度
        customPlot->yAxis->setNumberPrecision(0);//小数点后面小数位数
        customPlot->yAxis2->setTicker(logTicker);
        customPlot->yAxis2->setScaleType(QCPAxis::ScaleType::stLogarithmic);
        customPlot->yAxis2->setNumberFormat("eb");//使用科学计数法表示刻度
        customPlot->yAxis2->setNumberPrecision(0);//小数点后面小数位数
        customPlot->xAxis->setRange(0, 2048);
        customPlot->yAxis->setRange(0, 10000);

        QColor colors[] = {Qt::red, Qt::darkRed};
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
    else if (customPlot == ui->spectroMeter_top){
        //默认对数
        QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
        customPlot->yAxis->setTicker(logTicker);
        //customPlot->yAxis2->setTicker(logTicker);
        customPlot->yAxis->setScaleType(QCPAxis::ScaleType::stLogarithmic);
        customPlot->yAxis->setNumberFormat("eb");//使用科学计数法表示刻度
        customPlot->yAxis->setNumberPrecision(0);//小数点后面小数位数
        customPlot->yAxis2->setTicker(logTicker);
        customPlot->yAxis2->setScaleType(QCPAxis::ScaleType::stLogarithmic);
        customPlot->yAxis2->setNumberFormat("eb");//使用科学计数法表示刻度
        customPlot->yAxis2->setNumberPrecision(0);//小数点后面小数位数
        customPlot->xAxis->setRange(0, 2048);
        customPlot->yAxis->setRange(0, 10000);

        QColor colors[] = {Qt::red, Qt::darkRed, Qt::green, Qt::darkGreen, Qt::blue, Qt::darkBlue};
        QString title[] = {"t1 水平", "t1 垂直", "t2 水平", "t2 垂直", "t3 水平", "t3 垂直"};
        customPlot->legend->setVisible(true);
        for (int i=0; i<6; ++i){
            QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
            graph->setAntialiased(false);
            graph->setPen(QPen(colors[i]));
            graph->selectionDecorator()->setPen(QPen(colors[i]));
            graph->setLineStyle(QCPGraph::lsLine);
            graph->setSelectable(QCP::SelectionType::stNone);
            graph->setName(title[i]);
        }
#if 1
        customPlot->legend->setVisible(false);
        //添加可选项
        static int index = 0;
        for (int i=0; i<6; ++i){
            QCheckBox* checkBox = new QCheckBox(customPlot);
            checkBox->setText(title[i]);
            checkBox->setObjectName(tr(""));
            QIcon actionIcon = roundPixmap(QSize(16,16), colors[i]);
            checkBox->setIcon(actionIcon);
            checkBox->setProperty("index", i+1);
            checkBox->setChecked(true);
            connect(checkBox, &QCheckBox::stateChanged, this, [=](int state){
                int index = checkBox->property("index").toInt();
                QCPGraph *graph = customPlot->graph(i);
                if (graph){
                    graph->setVisible(Qt::CheckState::Checked == state ? true : false);
                    customPlot->replot();
                }
            });
        }
        connect(customPlot, &QCustomPlot::afterLayout, this, [=](){
            QCustomPlot* customPlot = qobject_cast<QCustomPlot*>(sender());
            QList<QCheckBox*> checkBoxs = customPlot->findChildren<QCheckBox*>();
            int i = 0;
            for (auto checkBox : checkBoxs){
                checkBox->move(customPlot->axisRect()->topRight().x() - 120, customPlot->axisRect()->topRight().y() + i++ * 20 + 10);
            }
        });
#endif
    }
    else if (customPlot == ui->spectroMeter_horCamera_PSD || customPlot == ui->spectroMeter_verCamera_PSD){
        QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
        graph->setAntialiased(false);
        graph->setLineStyle(QCPGraph::lsNone);
        graph->setSelectable(QCP::SelectionType::stNone);
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, Qt::red, 3));//显示散点图

        QCPColorMap *colorMap = new QCPColorMap(customPlot->xAxis, customPlot->yAxis);
        colorMap->setName("colorMap");

        colorMap->data()->setSize(100, 100);// 设置网格维度
        colorMap->data()->setRange(QCPRange(0, 10000), QCPRange(0, 1.0));// 设置网格数据范围
        colorMap->data()->fillAlpha(0);

        QCPColorScale *colorScale = new QCPColorScale(customPlot);
        customPlot->plotLayout()->addElement(0, 1, colorScale);
        colorScale->setType(QCPAxis::atRight);
        colorScale->setDataRange(QCPRange(0, 100));//颜色值取值范围

        //重新定义色带 （蓝绿黄红）
        QCPColorGradient gradient = QCPColorGradient::gpJet;
        // gradient.setColorStopAt(0, QColor(0, 0, 200));
        // gradient.setColorStopAt(1, QColor(255, 0, 0));
        colorScale->setGradient(gradient);

        colorMap->setColorScale(colorScale);// 色图与颜色条关联
        colorMap->setGradient(gradient/*QCPColorGradient::gpSpectrum*/);
        colorMap->rescaleDataRange();

        QCPMarginGroup *marginGroup = new QCPMarginGroup(customPlot);
        customPlot->axisRect()->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
        colorScale->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
        colorScale->setRangeDrag(false);
        colorScale->setRangeZoom(false);
        customPlot->rescaleAxes();
    }
    else if (customPlot == ui->spectroMeter_horCamera_FOM || customPlot == ui->spectroMeter_verCamera_FOM){
        QSharedPointer<QCPAxisTicker> ticker(new QCPAxisTicker);
        customPlot->yAxis->setTicker(ticker);
        customPlot->xAxis->setRange(0, 1);
        customPlot->yAxis->setRange(0, 250);

        QColor colors[] = {RGB(0x64,0x91,0xAF), Qt::red, Qt::black};
        QString title[] = {"Original data", "Gamma", "Neutron"};
        customPlot->legend->setVisible(true);
        for (int i=0; i<3; ++i){
            QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
            graph->setAntialiased(false);
            graph->setPen(QPen(colors[i]));
            graph->selectionDecorator()->setPen(QPen(colors[i]));
            if (i == 0){
                graph->setLineStyle(QCPGraph::lsNone);
                graph->setSelectable(QCP::SelectionType::stNone);
                graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, colors[i], 6));
            }
            else {
                graph->setLineStyle(QCPGraph::lsLine);
                graph->setSelectable(QCP::SelectionType::stNone);
            }
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


void MainWindow::on_action_startMeasure_triggered()
{
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
    QString fileSaveDir = QString("%1/%2/%3").arg(ui->spinBox_timeLength->value()).arg(ui->spinBox_shotNum->value()).arg(now.toString("yyyy-MM-dd_HH-mm-ss"));
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
    settings.setValue("Global/ShotNum", ui->spinBox_shotNum->value());
    settings.setValue("Global/ShotNumIsAutoIncrease", ui->checkBox_autoIncrease->isChecked());
    settings.setValue("Global/CacheDir", ui->lineEdit_savePath->text());
    settings.setValue("Global/CacheDir", ui->lineEdit_savePath->text());
    QFile::copy(CONFIG_FILENAME, fileSaveDir + "/Settings.ini");

    // 发送指令集
    mPCIeCommSdk.writeStartMeasure();
    //mPCIeCommSdk.setCaptureParamter(ui->comboBox_channel->currentIndex()+1, ui->spinBox_timeNode1->value(), ui->spinBox_timeNode2->value(), ui->spinBox_timeNode3->value());

    mPCIeCommSdk.startAllCapture(fileSaveDir,
                             ui->spinBox_timeLength->value(),
                             ui->spinBox_shotNum->value());

}


void MainWindow::on_action_stopMeasure_triggered()
{
    // 发送指令集
    mPCIeCommSdk.writeStopMeasure();

    mPCIeCommSdk.stopAllCapture();

    mCommHelper->disconnectServer();
}

#include <QtMath>
void MainWindow::replyWaveform(quint8 timestampIndex, quint8 cameraOrientation, QVector<quint16>& waveformBytes)
{
    //实测曲线
    QCustomPlot* customPlot = ui->spectroMeter_top;
    QVector<double> keys, values;
    quint32 yMin = 1e10;
    quint32 yMax = 0;
    for (int i=0; i<waveformBytes.size(); ++i){
        keys << i ;
        values << waveformBytes[i];
        yMin = qMin(yMin, (quint32)waveformBytes[i]);
        yMax = qMax(yMax, (quint32)waveformBytes[i]);
    }
    double spaceDisc = 0;
    if (yMax != yMin){
        spaceDisc = (yMax - yMin) * 0.15;
    }

    if (timestampIndex == 1){
        if (cameraOrientation == PCIeCommSdk::CameraOrientation::Horizontal)
            customPlot->graph(0)->setData(keys, values);
        else
            customPlot->graph(1)->setData(keys, values);
    }
    else if (timestampIndex == 2){
        if (cameraOrientation == PCIeCommSdk::CameraOrientation::Horizontal)
            customPlot->graph(2)->setData(keys, values);
        else
            customPlot->graph(3)->setData(keys, values);
    }
    else if (timestampIndex == 3){
        if (cameraOrientation == PCIeCommSdk::CameraOrientation::Horizontal)
            customPlot->graph(4)->setData(keys, values);
        else
            customPlot->graph(5)->setData(keys, values);
    }

    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(false);
    customPlot->yAxis->setRange(yMin-spaceDisc, yMax+spaceDisc);
    customPlot->replot(QCustomPlot::rpQueuedReplot);
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


void MainWindow::on_action_clock_triggered()
{
    qInfo().noquote() << tr("发送时钟同步信息");
}


void MainWindow::on_action_shotNum_triggered()
{
    qInfo().noquote() << tr("更新炮号：") << ui->spinBox_shotNum->value();
}


void MainWindow::on_action_stop_triggered()
{
    qInfo().noquote() << tr("紧急停机！！！");
}


void MainWindow::on_action_linear_triggered(bool checked)
{
    QList<QCustomPlot*> customPlots = this->findChildren<QCustomPlot*>();
    for (auto customPlot : customPlots){
        if (customPlot == ui->spectroMeter_horCamera_PSD ||
            customPlot == ui->spectroMeter_horCamera_FOM ||
            customPlot == ui->spectroMeter_verCamera_PSD ||
            customPlot == ui->spectroMeter_verCamera_FOM)
            continue;

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
        if (customPlot == ui->spectroMeter_horCamera_PSD ||
            customPlot == ui->spectroMeter_horCamera_FOM ||
            customPlot == ui->spectroMeter_verCamera_PSD ||
            customPlot == ui->spectroMeter_verCamera_FOM)
            continue;

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
    QList<SwitchButton*> buttons = this->findChildren<SwitchButton*>();
    for (auto btn : buttons){
        if (btn->property("isPower").toBool()){
            btn->setChecked(true);
        }
    }
}


void MainWindow::on_pushButton_closePower_clicked()
{
    QList<SwitchButton*> buttons = this->findChildren<SwitchButton*>();
    for (auto btn : buttons){
        if (btn->property("isPower").toBool()){
            btn->setChecked(false);
        }
    }
}


void MainWindow::on_pushButton_openVoltage_clicked()
{
    QList<SwitchButton*> buttons = this->findChildren<SwitchButton*>();
    for (auto btn : buttons){
        if (btn->property("isVoltage").toBool()){
            btn->setChecked(true);
        }
    }
}


void MainWindow::on_pushButton_closeVoltage_clicked()
{
    QList<SwitchButton*> buttons = this->findChildren<SwitchButton*>();
    for (auto btn : buttons){
        if (btn->property("isVoltage").toBool()){
            btn->setChecked(false);
        }
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
        ui->tableWidget_camera->setColumnHidden(4, true);
        ui->leftStackedWidget->setFixedWidth(240);
        ui->pushButton_selChannel1->hide();
        ui->pushButton_selChannel2->hide();

        ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_LSD);
        this->setWindowTitle(QApplication::applicationName() + "LSD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "LSD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "LSD");
    }
}


void MainWindow::on_action_typePSD_triggered(bool checked)
{
    if (checked){
        ui->tableWidget_camera->setColumnHidden(4, false);
        ui->leftStackedWidget->setFixedWidth(320);
        ui->pushButton_selChannel1->show();
        ui->pushButton_selChannel2->show();

        ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_PSD_LBD);
        this->setWindowTitle(QApplication::applicationName() + "PSD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "PSD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "PSD");
    }
}


void MainWindow::on_action_typeLBD_triggered(bool checked)
{
    if (checked){
        ui->tableWidget_camera->setColumnHidden(4, true);
        ui->leftStackedWidget->setFixedWidth(240);
        ui->pushButton_selChannel1->hide();
        ui->pushButton_selChannel2->hide();

        ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_PSD_LBD);
        this->setWindowTitle(QApplication::applicationName() + "LBD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "LBD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "LBD");
    }
}

#include <vector>
#include <random>
#include <cmath>
// 计算点到中心(0,50)的欧几里得距离
double calculateDistance(int x, int y, int centerX, int centerY) {
    return sqrt(pow(x - centerX, 2) + pow(y - centerY, 2));
}

// 生成二维数组
QVector<QVector<quint16>> generateArray(int rows, int cols, int centerX, int centerY, int maxValue, double zeroDistance) {
    QVector<QVector<quint16>> array(rows, QVector<quint16>(cols, 0));
    std::random_device rd;
    std::mt19937 gen(rd());

    // 计算最大距离（从最远角到中心点的距离）
    double maxValidDistance = 0;
    for (int x = 0; x < rows; x++) {
        for (int y = 0; y < cols; y++) {
            double dist = calculateDistance(x, y, centerX, centerY);
            if (dist <= zeroDistance && dist > maxValidDistance) {
                maxValidDistance = dist;
            }
        }
    }

    // 生成数组值，越靠近中心值越大
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            double distance = calculateDistance(i, j, centerX, centerY);
            // 如果距离超过指定值，设置为0
            if (distance > zeroDistance) {
                array[i][j] = 0;
                continue;
            }

            // if ((float)i/qAbs(j-centerY)>1.8 || (float)qAbs(j-centerY)/i>1.8){
            //     array[i][j] = 0;
            //     continue;
            // }
            // 距离因子：距离越小，因子越大（范围0.3-1.0）
            double distanceFactor = 1.0 - (distance / maxValidDistance) * 0.9;

            // 基础值加上随机扰动
            std::uniform_int_distribution<> dis(0, 50); // 随机扰动范围
            int baseValue = static_cast<int>(maxValue * distanceFactor);
            int randomOffset = dis(gen);

            int value = baseValue + randomOffset;
            // 确保值在0-1000范围内
            array[i][j] = std::max(0, std::min(maxValue, value));
        }
    }

    return array;
}

void MainWindow::replyKernelDensitySpectrumPSD(quint8 cameraOrientation, QVector<QPair<double ,double>>& pairs)
{
    // 核密度图谱
    QCPColorMap* colorMap = nullptr;
    QCustomPlot* customPlot = nullptr;
    if (PCIeCommSdk::CameraOrientation::Horizontal == cameraOrientation){
        customPlot = ui->spectroMeter_horCamera_PSD;
    }
    else{
        customPlot = ui->spectroMeter_verCamera_PSD;
    }

    colorMap = qobject_cast<QCPColorMap*>(customPlot->plottable("colorMap"));
    QCPColorScale *colorScale = colorMap->colorScale();
    QCPColorGradient gradient = colorScale->gradient();

    QVector<double> x, y;
    QVector<QColor> z;
    for (auto pair : pairs){
        x << pair.first;
        y << pair.second;
    }

    //‌数据预处理部分
    double max_x, min_x;
    double max_y, min_y;
    max_x = *std::max_element(std::begin(x), std::end(x));
    min_x = *std::min_element(std::begin(x), std::end(x));
    max_y = *std::max_element(std::begin(y), std::end(y));
    min_y = *std::min_element(std::begin(y), std::end(y));

    //‌网格划分和密度计算
    quint32 NLevel=200;//   %划分等级100份
    QVector<QVector<quint32>> color_Map(NLevel+1, QVector<quint32>(NLevel+1, 0));//创建(NLevel+1)×(NLevel+1)的零矩阵，用于存储密度
    double step_x = (max_x-min_x)/(NLevel-1);//  % x轴步长
    double step_y = (max_y-min_y)/(NLevel-1);//  % y轴步长

    //‌第一次循环：计算密度分布‌
    //遍历所有数据点，计算每个点所在的网格坐标
    for (auto pair : pairs){
        quint32 color_Map_x = quint32((pair.first-min_x)/step_x)+1;// - 计算x方向网格索引
        quint32 color_Map_y = quint32((pair.second-min_y)/step_y)+1;// - 计算y方向网格索引
        color_Map[color_Map_x][color_Map_y]++;//- 对应网格位置计数加1
    }

    //第二次循环：获取每个点的密度值‌
    //再次遍历所有数据点，根据网格索引获取对应的密度值
    for (auto pair : pairs){
        quint32 color_Map_x = quint32((pair.first-min_x)/step_x)+1;// - 计算x方向网格索引
        quint32 color_Map_y = quint32((pair.second-min_y)/step_y)+1;// - 计算y方向网格索引
        z << gradient.color(color_Map[color_Map_x][color_Map_y], QCPRange(0, 100), false);
    }

    customPlot->graph(0)->setData(x, y, z);    

    // 网络定义为50*50吧
    // int gridRow = pairs.size();
    // int gridColumn = gridRow;
    // colorMap->data()->setSize(gridColumn, gridRow);// 设置网格维度
    // colorMap->data()->setRange(QCPRange(0, 10000), QCPRange(0., 0.6));// 设置网格数据范围

    //将随机数转换到网格坐标中来
    // GaussianRandomGenerator generator;
    // const int PAIR_COUNT = 100000;
    // auto gaussianPairs = generator.generateGaussianPairs(PAIR_COUNT);
    // // 将随机数转换到网格坐标中来
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

    // for (int xIndex=0; xIndex<gridColumn; ++xIndex)
    // {
    //     for (int yIndex=0; yIndex<gridRow; ++yIndex)
    //     {
    //         if (pairs[xIndex][yIndex] == 0)
    //             colorMap->data()->setAlpha(xIndex, yIndex, 0);
    //         else
    //             colorMap->data()->setCell(xIndex, yIndex, pairs[xIndex][yIndex]);

    //         //qDebug() << xIndex << yIndex << pairs[xIndex][yIndex];
    //     }
    // }

    // colorMap->rescaleDataRange(true);
    customPlot->replot(QCustomPlot::RefreshPriority::rpQueuedReplot);
}

void MainWindow::replyKernelDensitySpectrumFoM(quint8 cameraOrientation, QVector<QVector<QPair<double ,double>>>& pairs)
{
    // 核密度图谱
    QCustomPlot* customPlot = nullptr;
    if (PCIeCommSdk::CameraOrientation::Horizontal == cameraOrientation){
        customPlot = ui->spectroMeter_horCamera_FOM;
    }
    else{
        customPlot = ui->spectroMeter_verCamera_FOM;
    }

    QColor clrs[] = {QColor::fromRgb(0x87,0xBA,0xDD), Qt::red, Qt::black};
    for (int i=0; i<=2; ++i){
        QVector<double> x, y;
        QVector<QColor> z;
        for (auto pair : pairs[i]){
            x << pair.first;
            y << pair.second;
            z << clrs[i];
        }

        customPlot->graph(i)->setData(x, y, z);
    }

    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);
    customPlot->replot(QCustomPlot::RefreshPriority::rpQueuedReplot);
}


void MainWindow::replySpectrum(quint8 timestampIndex, quint8 cameraOrientation, QVector<QPair<quint16,quint16>>& pairs)
{
    //实测曲线
    QCustomPlot* customPlot = ui->spectroMeter_top;
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

    if (timestampIndex == 1){
        if (cameraOrientation == PCIeCommSdk::CameraOrientation::Horizontal)
            customPlot->graph(0)->setData(keys, values);
        else
            customPlot->graph(1)->setData(keys, values);
    }
    else if (timestampIndex == 2){
        if (cameraOrientation == PCIeCommSdk::CameraOrientation::Horizontal)
            customPlot->graph(2)->setData(keys, values);
        else
            customPlot->graph(3)->setData(keys, values);
    }
    else if (timestampIndex == 3){
        if (cameraOrientation == PCIeCommSdk::CameraOrientation::Horizontal)
            customPlot->graph(4)->setData(keys, values);
        else
            customPlot->graph(5)->setData(keys, values);
    }

    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(false);
    customPlot->yAxis->setRange(yMin-spaceDisc, yMax+spaceDisc);
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
    {
        QVector<QPair<double ,double>> spectrumPairs;// = generateArray(50, 50, 0, 25, 1000, 35);
        QFile file("./PSD.csv");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
            QTextStream stream(&file);
            while (!stream.atEnd())
            {
                QString line = stream.readLine();
                QStringList row = line.split(',', Qt::SkipEmptyParts);
                if (row.size() == 2)
                {
                    spectrumPairs.push_back(qMakePair(row.at(0).toDouble(), row.at(1).toDouble()));
                }
            }
            file.close();

            emit reportKernelDensitySpectrumPSD(PCIeCommSdk::CameraOrientation::Horizontal, spectrumPairs);
            emit reportKernelDensitySpectrumPSD(PCIeCommSdk::CameraOrientation::Vertical, spectrumPairs);
        }
    }

    if (1){
        /*限制数据显示范围*/
        double f1_b1 = 0.5085;
        double f1_c1 = 0.0033;
        double f2_b1 = 0.5270;
        double f2_c1 = 0.0050;
        double limit_x1 = f1_b1-8*f1_c1/1.414;
        double limit_x2 = f2_b1+8*f2_c1/1.414;

        QVector<QPair<double ,double>> spectrumPair[3];
        QFile file("./FoM.csv");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
            QTextStream stream(&file);
            while (!stream.atEnd())
            {
                QString line = stream.readLine();
                QStringList row = line.split(',', Qt::SkipEmptyParts);
                if (row.size() == 4)
                {
                    if (row.at(0).toDouble()>=limit_x1 && row.at(0).toDouble()<=limit_x2){
                        spectrumPair[0].push_back(qMakePair(row.at(0).toDouble(), row.at(1).toDouble()));
                        spectrumPair[1].push_back(qMakePair(row.at(0).toDouble(), row.at(2).toDouble()));
                        spectrumPair[2].push_back(qMakePair(row.at(0).toDouble(), row.at(3).toDouble()));
                    }
                }
            }
            file.close();

            QVector<QVector<QPair<double ,double>>> spectrumPairs;
            spectrumPairs.push_back(spectrumPair[0]);
            spectrumPairs.push_back(spectrumPair[1]);
            spectrumPairs.push_back(spectrumPair[2]);
            emit reportKernelDensitySpectrumFoM(PCIeCommSdk::CameraOrientation::Horizontal, spectrumPairs);
            emit reportKernelDensitySpectrumFoM(PCIeCommSdk::CameraOrientation::Vertical, spectrumPairs);
        }
    }

    {

        QVector<QPair<quint16 ,quint16>> spectrumPair[3];
        QFile file("./Spectrum.csv");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
            QTextStream stream(&file);
            while (!stream.atEnd())
            {
                QString line = stream.readLine();
                QStringList row = line.split(',', Qt::SkipEmptyParts);
                if (row.size() == 4)
                {
                    spectrumPair[0].push_back(qMakePair(row.at(0).toDouble(), row.at(1).toUInt()));
                    spectrumPair[1].push_back(qMakePair(row.at(0).toDouble(), row.at(2).toUInt()));
                    spectrumPair[2].push_back(qMakePair(row.at(0).toDouble(), row.at(3).toUInt()));
                }
            }
            file.close();

            emit reportSpectrum(1, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[0]);
            emit reportSpectrum(1, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[1]);
            emit reportSpectrum(2, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[0]);
            emit reportSpectrum(2, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[2]);
            emit reportSpectrum(3, PCIeCommSdk::CameraOrientation::Horizontal, spectrumPair[1]);
            emit reportSpectrum(4, PCIeCommSdk::CameraOrientation::Vertical, spectrumPair[2]);
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
