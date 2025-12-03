#include "offlinewindow.h"
#include "ui_offlinewindow.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QRandomGenerator>
#include "globalsettings.h"

OfflineWindow::OfflineWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::OfflineWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper*>(parent))
{
    ui->setupUi(this);

    initUi();

    //QStringList args = QCoreApplication::arguments();
    //this->setWindowTitle(QApplication::applicationName()+" - "+APP_VERSION + " [" + args[4] + "]");
    this->applyColorTheme();

    connect(&mPCIeCommSdk, &PCIeCommSdk::reportWaveform, this, &OfflineWindow::replyWaveform);
    connect(this, SIGNAL(reporWriteLog(const QString&,QtMsgType)), this, SLOT(replyWriteLog(const QString&,QtMsgType)));
    connect(this, SIGNAL(reportKernelDensitySpectrumPSD(quint8,QVector<QPair<double,double>>&)), this, SLOT(replyKernelDensitySpectrumPSD(quint8,QVector<QPair<double,double>>&)));
    connect(this, SIGNAL(reportKernelDensitySpectrumFoM(quint8,QVector<QVector<QPair<double,double>>>&)), this, SLOT(replyKernelDensitySpectrumFoM(quint8,QVector<QVector<QPair<double,double>>>&)));
    connect(this, SIGNAL(reportSpectrum(quint8,quint8,QVector<QPair<quint16,quint16>>&)), this, SLOT(replySpectrum(quint8,quint8,QVector<QPair<quint16,quint16>>&)));

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

OfflineWindow::~OfflineWindow()
{
    delete ui;
}


void OfflineWindow::initUi()
{
    ui->tableWidget_file->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    ui->toolButton_start->setDefaultAction(ui->action_analyze);

    {
        QActionGroup *actionGrp = new QActionGroup(this);
        actionGrp->addAction(ui->action_waveform);
        actionGrp->addAction(ui->action_ngamma);
        emit ui->action_waveform->triggered(true);
    }

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

    {
        QAction *action = ui->lineEdit_savePath->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
        QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
        button->setCursor(QCursor(Qt::PointingHandCursor));
        connect(button, &QToolButton::pressed, this, [=](){
            QString cacheDir = QFileDialog::getExistingDirectory(this);
            if (!cacheDir.isEmpty()){
                ui->lineEdit_savePath->setText(cacheDir);
            }
        });
    }

    //////////////////////////////////////////////////////////////////////
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
            splitterV1->addWidget(ui->spectroMeter_LSD_waveform);
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
            splitterV1->addWidget(ui->spectroMeter_waveform);
            splitterV1->addWidget(ui->spectroMeter_spectrum);
            splitterV1->setSizes(QList<int>() << 100000 << 100000 << 100000);
            splitterV1->setCollapsible(0,false);
            splitterV1->setCollapsible(1,false);
            splitterV1->setCollapsible(2,false);
            ui->spectroMeterPageInfoWidget_PSD_LBD->layout()->addWidget(splitterV1);
        }


        //大布局
        QSplitter *splitterH1 = new QSplitter(Qt::Horizontal,this);
        splitterH1->setObjectName("splitterH1");
        splitterH1->setHandleWidth(5);
        splitterH1->addWidget(ui->leftStackedWidget);
        splitterH1->addWidget(ui->centralVboxStackedWidget);
        splitterH1->addWidget(ui->rightVboxWidget);
        splitterH1->setSizes(QList<int>() << 100000 << 600000 << 100000);
        splitterH1->setCollapsible(0,false);
        splitterH1->setCollapsible(1,false);
        splitterH1->setCollapsible(2,false);
        ui->centralwidget->layout()->addWidget(splitterH1);
        ui->centralwidget->layout()->addWidget(ui->rightSidewidget);
    }

    // 左侧栏
    QPushButton* filelistButton = nullptr;
    {
        filelistButton = new QPushButton();
        filelistButton->setText(tr("文件列表"));
        filelistButton->setFixedSize(250,29);
        filelistButton->setCheckable(true);

        QGraphicsScene *scene = new QGraphicsScene(this);
        QGraphicsProxyWidget *w = scene->addWidget(filelistButton);
        w->setPos(0,0);
        w->setRotation(-90);
        ui->graphicsView_2->setScene(scene);
        ui->graphicsView_2->setFrameStyle(0);
        ui->graphicsView_2->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView_2->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView_2->setFixedSize(30, 250);
        ui->leftSidewidget->setFixedWidth(30);

        connect(filelistButton,&QPushButton::clicked,this,[=](){
            if(ui->leftStackedWidget->isHidden()) {
                ui->leftStackedWidget->setCurrentWidget(ui->listlistWidget);
                ui->leftStackedWidget->show();

                filelistButton->setChecked(true);
            } else {
                ui->leftStackedWidget->hide();
                filelistButton->setChecked(false);
            }
        });

        connect(ui->toolButton_closeFilelistWidget,&QAbstractButton::clicked,this,[=](){
            ui->leftStackedWidget->hide();
            filelistButton->setChecked(false);
        });
        ui->toolButton_closeFilelistWidget->click();
    }

    // 右侧栏
    QPushButton* labPrametersButton = nullptr;
    {
        labPrametersButton = new QPushButton();
        labPrametersButton->setText(tr("操作选项"));
        labPrametersButton->setFixedSize(250,29);
        labPrametersButton->setCheckable(true);

        connect(labPrametersButton,&QPushButton::clicked,this,[=](){
            if(ui->rightVboxWidget->isHidden()) {
                ui->rightVboxWidget->show();

                GlobalSettings settings;
                settings.setValue("Global/Offline/ShowRightSide", "true");
            } else {
                ui->rightVboxWidget->hide();

                GlobalSettings settings;
                settings.setValue("Global/Offline/ShowRightSide", "false");
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

    {
        initCustomPlot(ui->spectroMeter_LSD_waveform, tr(""), tr("Energy Counts"));
        initCustomPlot(ui->spectroMeter_horCamera_PSD, tr("水平 n:γ Energy(keVee) PSD图"), tr(""));
        initCustomPlot(ui->spectroMeter_horCamera_FOM, tr("水平 FOM图"), tr(""));
        initCustomPlot(ui->spectroMeter_verCamera_PSD, tr("垂直 n:γ Energy(keVee) PSD图"), tr(""));
        initCustomPlot(ui->spectroMeter_verCamera_FOM, tr("垂直 FOM图"), tr(""));

        initCustomPlot(ui->spectroMeter_waveform, tr(""), tr("波形"));
        initCustomPlot(ui->spectroMeter_spectrum, tr(""), tr("能谱"));

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

    {
        QActionGroup *themeActionGroup = new QActionGroup(this);
        ui->action_lightTheme->setActionGroup(themeActionGroup);
        ui->action_darkTheme->setActionGroup(themeActionGroup);
        ui->action_lightTheme->setChecked(!mIsDarkTheme);
        ui->action_darkTheme->setChecked(mIsDarkTheme);

        QActionGroup *actionChartGroup = new QActionGroup(this);
        ui->action_linear->setActionGroup(actionChartGroup);
        ui->action_logarithm->setActionGroup(actionChartGroup);
    }
}

void OfflineWindow::initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel)
{
    customPlot->installEventFilter(this);
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

    if (customPlot == ui->spectroMeter_waveform || customPlot == ui->spectroMeter_spectrum){
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
    else if (customPlot == ui->spectroMeter_LSD_waveform){
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
#if 1
        customPlot->legend->setVisible(false);
        //添加可选项
        static int index = 0;
        for (int i=0; i<2; ++i){
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

void OfflineWindow::closeEvent(QCloseEvent *event) {
    if ( QMessageBox::Yes == QMessageBox::question(this, tr("系统退出提示"), tr("确定要退出软件系统吗？"),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes))
        event->accept();
    else
        event->ignore();
}

bool OfflineWindow::eventFilter(QObject *watched, QEvent *event){
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

    }

    return QWidget::eventFilter(watched, event);
}

void OfflineWindow::replyWriteLog(const QString &msg, QtMsgType msgType/* = QtDebugMsg*/)
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

bool OfflineWindow::loadOfflineFilename(const QString& filename)
{
    ui->textBrowser_filepath->setText(filename);
    return true;
}

void OfflineWindow::startAnalyze()
{

}

void OfflineWindow::on_pushButton_test_clicked()
{
    //连接数据库
    if (ui->lineEdit_host->text().isEmpty())
        return;

    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");
    db.setHostName(ui->lineEdit_host->text());
    db.setPort(ui->spinBox_port->value());
    db.setDatabaseName("mysql");
    db.setUserName(ui->lineEdit_username->text());
    db.setPassword(ui->lineEdit_password->text());
    db.open();
    if (!db.isOpen()) {
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("连接MySQL数据库失败！"));
        return;
    }

    db.close();
    QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("数据库连接成功！"));
}

void OfflineWindow::on_pushButton_startUpload_clicked()
{
    if (ui->lineEdit_host->text().isEmpty())
        return;

    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");
    db.setHostName(ui->lineEdit_host->text());
    db.setPort(ui->spinBox_port->value());
    db.setDatabaseName("mysql");
    db.setUserName(ui->lineEdit_username->text());
    db.setPassword(ui->lineEdit_password->text());
    db.open();
    if (!db.isOpen()) {
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("连接MySQL数据库失败！"));
        return;
    }

    // 先判断数据库是否存在
    QSqlQuery query;
    if (!query.exec("CREATE DATABASE IF NOT EXISTS NeutronCamera")) {
        qDebug() << "Failed to create database:" << query.lastError().text();
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据库创建失败！") + query.lastError().text());
        return;
    }

    // 关闭当前连接，重新连接到数据库 NeutronCamera
    db.close();
    db.setDatabaseName("NeutronCamera");
    if (!db.open()) {
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("连接数据库失败！"));
        return;
    }

    //创建用户表
    QString sql = "CREATE TABLE IF NOT EXISTS Spectrum ("
                  "id INT PRIMARY KEY AUTO_INCREMENT,"
                  "shotNum int UNSIGNED,"
                  "timestamp datetime,";
    for (int i=1; i<=512; ++i){
        if (i==512)
            sql += QString("data%1 smallint UNSIGNED)").arg(i);
        else
            sql += QString("data%1 smallint UNSIGNED,").arg(i);
    }
    qDebug() << sql;
    if (!query.exec(sql)){
        db.close();
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据表格创建失败！") + query.lastError().text());
        return;
    }

    // 保存到数据库
    {
        QString sql = "INSERT INTO Spectrum (shotNum, timestamp,";
        for (int i=1; i<=512; ++i){
            if (i==512)
                sql += QString("data%1)").arg(i);
            else
                sql += QString("data%1,").arg(i);
        }
        sql += " VALUES (:shotNum, :timestamp,";
        for (int i=1; i<=512; ++i){
            if (i==512)
                sql += QString(":data%1)").arg(i);
            else
                sql += QString(":data%1,").arg(i);
        }

        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        query.prepare(sql);
        for (int k=1; k<=16; k++){
            query.bindValue(":shotNum", 100);
            query.bindValue(":timestamp", timestamp);
            for (int i=1; i<=512; ++i){
                quint16 data = QRandomGenerator::global()->bounded(1000, 15000);
                query.bindValue(QString(":data%1").arg(i), data);
            }
            if (!query.exec() || query.numRowsAffected() == 0){
                QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据表写入失败！") + query.lastError().text());
                db.close();
                return;
            }
        }
    }

    db.close();
    QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("数据上传完成！"));
}


void OfflineWindow::on_action_openfile_triggered()
{
    GlobalSettings settings;
    QString lastPath = settings.value("Global/Offline/LastFileDir", QDir::homePath()).toString();
    //QString filter = "二进制文件 (*.bin);;所有文件 (*.*)";
    //QString filePath = QFileDialog::getOpenFileName(this, tr("打开测量数据文件"), lastPath, filter);
    QString filePath = QFileDialog::getExistingDirectory(this, tr("选择测量数据存放目录"), lastPath);

    // 目录格式：炮号+日期+[cardIndex]测量数据[packIndex].bin
    if (filePath.isEmpty())
        return;

    if (!QFileInfo::exists(filePath+"/Settings.ini")){
        QMessageBox::information(this, tr("提示"), tr("路径无效，缺失\"Settings.ini\"文件！"));
        return;
    }

    settings.setValue("Global/LastFileDir", filePath);
    ui->textBrowser_filepath->setText(filePath);
    this->setWindowTitle(filePath + " - 中子相机数据处理离线版");

    //加载目录下所有文件
    loadRelatedFiles(filePath);
}


void OfflineWindow::loadRelatedFiles(const QString& filePath)
{
    ui->tableWidget_file->setRowCount(0);

    //对目录下的配置文件信息进行解析，里面包含了炮号、测试开始时间、测量时长、以及探测器类型

    QDir dir(filePath);
    if (dir.exists()) {
        dir.setFilter(QDir::Files);
        dir.setNameFilters(QStringList() << "*.bin");
        QStringList binFiles = dir.entryList();
        foreach (const QString &filePath, binFiles) {
            // 这里应该对文件进一步解析，得到时间范围

            int row = ui->tableWidget_file->rowCount();
            ui->tableWidget_file->insertRow(row);
            ui->tableWidget_file->setItem(row, 0, new QTableWidgetItem(QFileInfo(filePath).fileName()));
        }
    }
}

void OfflineWindow::on_action_analyze_triggered()
{
    // //由于每个文件的能谱时长是固定的，所有这里需要根据时刻计算出对应的是第几个文件
    // //这类假设每个文件能谱时长为50ms
    // quint32 fileIndex = (float)(ui->spinBox_time1->value() + 49)/ 50;

    // //每个文件里面对于的是4个通道，根据通道号判断是第几个文件
    // quint32 deviceIndex = (ui->comboBox_horCamera->currentIndex()+4) / 4;

    // //t1-水平相机
    // {
    //     QString filePath = QString("%1/%2data%3.bin").arg(ui->textBrowser_filepath->toPlainText()).arg(deviceIndex).arg(fileIndex);
    //     mPCIeCommSdk.setCaptureParamter(PCIeCommSdk::Horizontal, ui->spinBox_time1->value());
    //     if (!mPCIeCommSdk.openHistoryData(filePath))
    //     {
    //         QMessageBox::information(this, tr("提示"), tr("文件格式错误，加载失败！"));
    //     }
    // }

    // //t1-垂直相机
    // {
    //     deviceIndex = (ui->comboBox_horCamera->currentIndex()+12) / 4;
    //     QString filePath = QString("%1/%2data%3.bin").arg(ui->textBrowser_filepath->toPlainText()).arg(deviceIndex).arg(fileIndex);
    //     mPCIeCommSdk.setCaptureParamter(PCIeCommSdk::Vertical, ui->spinBox_time1->value());
    //     if (!mPCIeCommSdk.openHistoryData(filePath))
    //     {
    //         QMessageBox::information(this, tr("提示"), tr("文件格式错误，加载失败！"));
    //     }
    // }
    // return;

    mPCIeCommSdk.setCaptureParamter(1, ui->spinBox_time1->value());
    mPCIeCommSdk.openHistoryData("D:\\work\\Qt\\MicroDetector\\build_NeutronCamera\\x64\\qt5.15.2\\cache\\100\\2025-12-01_09-30-21\\1data1.bin");

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
        }
    }
}


void OfflineWindow::on_action_linear_triggered()
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


void OfflineWindow::on_action_logarithm_triggered()
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


void OfflineWindow::on_action_exit_triggered()
{
    mainWindow->close();
}


void OfflineWindow::on_pushButton_export_clicked()
{

}


void OfflineWindow::on_action_lightTheme_triggered()
{
    if(!mIsDarkTheme) return;
    mIsDarkTheme = false;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Offline/Startup/darkTheme","false");
    applyColorTheme();
}


void OfflineWindow::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Offline/Startup/darkTheme","true");
    applyColorTheme();
}

void OfflineWindow::applyColorTheme()
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
                                     .arg(palette.color(QPalette::Window).red())
                                     .arg(palette.color(QPalette::Window).green())
                                     .arg(palette.color(QPalette::Window).blue())
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

        // 窗体背景色
        customPlot->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Window) : Qt::white));
        // 四边安装轴并显示
        customPlot->axisRect()->setupFullAxesBox();
        customPlot->axisRect()->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Window) : Qt::white));
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

void OfflineWindow::on_action_colorTheme_triggered()
{
    GlobalSettings settings;
    QColor color = QColorDialog::getColor(mThemeColor, this, tr("选择颜色"));
    if (color.isValid()) {
        mThemeColor = color;
        mThemeColorEnable = true;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
        settings.setValue("Global/Offline/Startup/themeColor",mThemeColor);
    } else {
        mThemeColorEnable = false;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    }
    settings.setValue("Global/Offline/Startup/themeColorEnable",mThemeColorEnable);
    applyColorTheme();
}

QPixmap OfflineWindow::maskPixmap(QPixmap pixmap, QSize sz, QColor clrMask)
{
    // 更新图标颜色
    QPixmap result = pixmap.scaled(sz);
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), clrMask);
    return result;
}

QPixmap OfflineWindow::roundPixmap(QSize sz, QColor clrOut)
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

QPixmap OfflineWindow::dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut)
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

#include <QtMath>
void OfflineWindow::replyWaveform(quint8 timestampIndex, quint8 cameraOrientation, QVector<quint16>& waveformBytes)
{
    //实测曲线
    QCustomPlot* customPlot = nullptr;
    if (ui->action_typeLSD->isChecked())
        customPlot = ui->spectroMeter_LSD_waveform;
    else
        customPlot = ui->spectroMeter_waveform;
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

void OfflineWindow::replySpectrum(quint8 timestampIndex, quint8 cameraOrientation, QVector<QPair<quint16,quint16>>& pairs)
{
    if (ui->action_typeLSD->isChecked()){
        QCustomPlot* customPlot = ui->spectroMeter_LSD_waveform;
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
    else{
        QCustomPlot* customPlot = nullptr;
        customPlot = ui->spectroMeter_spectrum;

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
        customPlot->yAxis->rescale(false);
        customPlot->yAxis->setRange(yMin-spaceDisc, yMax+spaceDisc);
        customPlot->replot(QCustomPlot::rpQueuedReplot);
    }
}


void OfflineWindow::replyKernelDensitySpectrumPSD(quint8 cameraOrientation, QVector<QPair<double ,double>>& pairs)
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

void OfflineWindow::replyKernelDensitySpectrumFoM(quint8 cameraOrientation, QVector<QVector<QPair<double ,double>>>& pairs)
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

void OfflineWindow::on_action_typeLSD_triggered(bool checked)
{
    if (checked){
        ui->centralVboxStackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_LSD);
        this->setWindowTitle(QApplication::applicationName() + "LSD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "LSD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "LSD");

        ui->action_waveform->setVisible(true);
        ui->action_ngamma->setVisible(true);
    }
}


void OfflineWindow::on_action_typePSD_triggered(bool checked)
{
    if (checked){
        ui->centralVboxStackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_PSD_LBD);
        this->setWindowTitle(QApplication::applicationName() + "PSD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "PSD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "PSD");

        ui->spectroMeter_spectrum->yAxis->setLabel("中子能谱");
        ui->spectroMeter_spectrum->replot();
        ui->action_waveform->setVisible(false);
        ui->action_ngamma->setVisible(false);
    }
}


void OfflineWindow::on_action_typeLBD_triggered(bool checked)
{
    if (checked){
        ui->centralVboxStackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_PSD_LBD);
        this->setWindowTitle(QApplication::applicationName() + "LBD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "LBD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "LBD");

        ui->spectroMeter_spectrum->yAxis->setLabel("伽马能谱");//γ Energy(keVee) Counts
        ui->spectroMeter_spectrum->replot();
        ui->action_waveform->setVisible(false);
        ui->action_ngamma->setVisible(false);
    }
}


void OfflineWindow::on_action_waveform_triggered(bool checked)
{
    if (checked){
        ui->spectroMeter_LSD_waveform->setVisible(true);
        ui->spectroMeter_horCamera_PSD->setVisible(false);
        ui->spectroMeter_horCamera_FOM->setVisible(false);
        ui->spectroMeter_verCamera_PSD->setVisible(false);
        ui->spectroMeter_verCamera_FOM->setVisible(false);
    }
}


void OfflineWindow::on_action_ngamma_triggered(bool checked)
{
    if (checked){
        ui->spectroMeter_LSD_waveform->setVisible(false);
        ui->spectroMeter_horCamera_PSD->setVisible(true);
        ui->spectroMeter_horCamera_FOM->setVisible(true);
        ui->spectroMeter_verCamera_PSD->setVisible(true);
        ui->spectroMeter_verCamera_FOM->setVisible(true);
    }
}

