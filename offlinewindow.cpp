#include "offlinewindow.h"
#include "ui_offlinewindow.h"
#include "globalsettings.h"
#include "datacompresswindow.h"
#include "n_gamma.h"
#include <QElapsedTimer>

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
    connect(this, &OfflineWindow::reportWaveform, this, &OfflineWindow::replyWaveform);
    connect(this, SIGNAL(reporWriteLog(const QString&,QtMsgType)), this, SLOT(replyWriteLog(const QString&,QtMsgType)));
    connect(this, SIGNAL(reportCalculateDensityPSD(quint8,QVector<QPair<double,double>>&)), this, SLOT(replyCalculateDensityPSD(quint8,QVector<QPair<double,double>>&)));
    
    // 绘图信号与槽连接
    connect(this, SIGNAL(reportPSDPlot(quint8,const QVector<double>&, const QVector<double>&, const QVector<double>&)), this,
        SLOT(replyPSDPlot(quint8,const QVector<double>&,const QVector<double>&,const QVector<double>&)));
    connect(this, SIGNAL(reportFoMPlot(quint8,const QVector<FOM_CurvePoint>&)), this, 
        SLOT(replyFoMPlot(quint8,const QVector<FOM_CurvePoint>&)));
    connect(this, SIGNAL(reportSpectrum(quint8, quint8, const QVector<QPair<quint16,quint16>>&)), this, 
        SLOT(replySpectrum(quint8, quint8, const QVector<QPair<quint16,quint16>>&)));

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

    {
        QAction *action = ui->lineEdit_savePath->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
        QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
        button->setCursor(QCursor(Qt::PointingHandCursor));
        connect(button, &QToolButton::pressed, this, [=](){
            GlobalSettings settings;
            QString lastPath = settings.value("Global/Offline/LastSaveDir", QDir::homePath()).toString();
            QString fileName = QFileDialog::getSaveFileName(this, tr("选择保存文件"), lastPath, tr("HDF5文件 (*.h5);;所有文件 (*.*)"));
            if (!fileName.isEmpty()){
                // 如果用户没有输入 .h5 后缀，自动添加
                if (!fileName.endsWith(".h5", Qt::CaseInsensitive)) {
                    fileName += ".h5";
                }
                ui->lineEdit_savePath->setText(fileName);
                // 保存最后使用的目录
                QFileInfo fileInfo(fileName);
                settings.setValue("Global/Offline/LastSaveDir", fileInfo.absolutePath());
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
            splitterV1->addWidget(ui->spectroMeter_waveform_LSD);
            splitterV1->addWidget(ui->spectroMeter_spectrum_LSD);
            splitterV1->addWidget(splitterH1);
            splitterV1->addWidget(splitterH2);
            splitterV1->setSizes(QList<int>() << 100000 << 100000 << 400000 << 400000);
            splitterV1->setCollapsible(0,false);
            splitterV1->setCollapsible(1,false);
            splitterV1->setCollapsible(2,false);

            ui->spectroMeterPageInfoWidget_LSD->layout()->addWidget(splitterV1);
        }
        {
            QSplitter *splitterV1 = new QSplitter(Qt::Vertical,ui->spectroMeterPageInfoWidget_PSD);
            splitterV1->setHandleWidth(2);
            splitterV1->addWidget(ui->spectroMeter_waveform_PSD);
            splitterV1->addWidget(ui->spectroMeter_spectrum_PSD);
            splitterV1->setSizes(QList<int>() << 100000 << 100000 << 100000);
            splitterV1->setCollapsible(0,false);
            splitterV1->setCollapsible(1,false);
            splitterV1->setCollapsible(2,false);
            ui->spectroMeterPageInfoWidget_PSD->layout()->addWidget(splitterV1);
        }
        {
            QSplitter *splitterV1 = new QSplitter(Qt::Vertical,ui->spectroMeterPageInfoWidget_LBD);
            splitterV1->setHandleWidth(2);
            splitterV1->addWidget(ui->spectroMeter_waveform_LBD);
            splitterV1->addWidget(ui->spectroMeter_spectrum_LBD);
            splitterV1->setSizes(QList<int>() << 100000 << 100000 << 100000);
            splitterV1->setCollapsible(0,false);
            splitterV1->setCollapsible(1,false);
            splitterV1->setCollapsible(2,false);
            ui->spectroMeterPageInfoWidget_LBD->layout()->addWidget(splitterV1);
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
        initCustomPlot(ui->spectroMeter_waveform_LSD, tr(""), tr("波形"));
        initCustomPlot(ui->spectroMeter_spectrum_LSD, tr(""), tr("能谱"));
        initCustomPlot(ui->spectroMeter_horCamera_PSD, tr("水平 n:γ Energy(keVee) PSD图"), tr(""));
        initCustomPlot(ui->spectroMeter_horCamera_FOM, tr("水平 FOM图"), tr(""));
        initCustomPlot(ui->spectroMeter_verCamera_PSD, tr("垂直 n:γ Energy(keVee) PSD图"), tr(""));
        initCustomPlot(ui->spectroMeter_verCamera_FOM, tr("垂直 FOM图"), tr(""));

        initCustomPlot(ui->spectroMeter_waveform_PSD, tr(""), tr("波形"));
        initCustomPlot(ui->spectroMeter_spectrum_PSD, tr("道址"), tr("中子能谱"));
        initCustomPlot(ui->spectroMeter_waveform_LBD, tr(""), tr("波形"));
        initCustomPlot(ui->spectroMeter_spectrum_LBD, tr("道址"), tr("伽马能谱"));

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

    if (customPlot == ui->spectroMeter_waveform_PSD ||
        customPlot == ui->spectroMeter_spectrum_PSD ||
        customPlot == ui->spectroMeter_waveform_LBD ||
        customPlot == ui->spectroMeter_spectrum_LBD
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
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, colors[i], 6));
            graph->setName(title[i]);
        }

        setCheckBoxHelper(customPlot);
    }
    else if (customPlot == ui->spectroMeter_waveform_LSD){
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
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, colors[i], 6));
            graph->setName(title[i]);
        }

        setCheckBoxHelper(customPlot);
    }
    else if (customPlot == ui->spectroMeter_spectrum_LSD){
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
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, colors[i], 6));
            graph->setName(title[i]);
        }

        setCheckBoxHelper(customPlot);
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
        // QSharedPointer<QCPAxisTicker> ticker(new QCPAxisTicker);
        // customPlot->yAxis->setTicker(ticker);
        customPlot->xAxis->setRange(0, 10000);
        customPlot->yAxis->setRange(0, 1);

        QColor colors[] = {Qt::black, Qt::blue, Qt::red};
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
                graph->setSmooth(true);
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

void OfflineWindow::setCheckBoxHelper(QCustomPlot* customPlot)
{
    customPlot->legend->setVisible(false);
    //添加可选项
    static int index = 0;
    for (int i=0; i<2; ++i){
        QCheckBox* checkBox = new QCheckBox(customPlot);
        checkBox->setText(customPlot->graph(i)->name());
        checkBox->setObjectName(tr(""));
        QIcon actionIcon = roundPixmap(QSize(16,16), customPlot->graph(i)->pen().color());
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
        QFontMetrics fontMetrics(customPlot->font());
        int avg_height = fontMetrics.ascent() + fontMetrics.descent();
        int i = 0;
        for (auto checkBox : checkBoxs){
            checkBox->move(customPlot->axisRect()->left() + 10, customPlot->axisRect()->topRight().y() + i++ * avg_height + 5);
        }
    });
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

void OfflineWindow::on_action_openfile_triggered()
{
    GlobalSettings settings;
    QString lastPath = settings.value("Global/Offline/LastFileDir", QDir::homePath()).toString();
    QString dirPath = QFileDialog::getExistingDirectory(this, tr("选择测量数据存放目录"), lastPath);

    // 目录格式：炮号+日期+[cardIndex]测量数据[packIndex].bin
    if (dirPath.isEmpty())
        return;

    settings.setValue("Global/Offline/LastFileDir", dirPath);
    ui->textBrowser_filepath->setText(dirPath);
    if (!QFileInfo::exists(dirPath+"/Settings.ini")){
        QMessageBox::information(this, tr("提示"), tr("路径无效，缺失\"Settings.ini\"文件！"));
        return;
    }
    else {
        GlobalSettings settings(dirPath+"/Settings.ini");
        mShotNum = settings.value("Global/ShotNum", "00000").toString();
        mCurrentDetectorType = (DetectorType)settings.value("Global/DetectType", 0).toUInt();

        emit reporWriteLog("实验炮号：" + mShotNum);
        if (mCurrentDetectorType == dtLBD){
            emit ui->action_typeLBD->triggered(true);
            emit reporWriteLog("探测器类型：LBD探测器");
        }
        else if (mCurrentDetectorType == dtLSD){
            emit ui->action_typeLSD->triggered(true);
            emit reporWriteLog("探测器类型：LSD探测器");
        }
        else if (mCurrentDetectorType == dtPSD){
            emit ui->action_typePSD->triggered(true);
            emit reporWriteLog("探测器类型：PSD探测器");
        }
        else{
            emit reporWriteLog("探测器类型：未知");
        }
    }

    //加载目录下所有文件，罗列在表格中，统计给出文件大小
    loadRelatedFiles(dirPath);
}


void OfflineWindow::loadRelatedFiles(const QString& dirPath)
{
    ui->tableWidget_file->setRowCount(0);

    // 使用静态函数获取.bin文件列表
    QFileInfoList fileinfoList = DataCompressWindow::getBinFileList(dirPath);
    // 过滤掉能谱文件
    QFileInfoList result;
    for (auto item : fileinfoList){
        if (item.fileName().contains("data"))
            result.append(item);
    }

    QCollator collator;
    collator.setNumericMode(true);
    auto compareFilename = [&](const QFileInfo& A, const QFileInfo& B){
        return collator.compare(A.fileName(), B.fileName()) < 0;
    };
    std::sort(result.begin(), result.end(), compareFilename);

    for (int i = 0; i < result.size(); ++i) {
        const QFileInfo& fi = result.at(i);

        int row = ui->tableWidget_file->rowCount();
        ui->tableWidget_file->insertRow(row);
        ui->tableWidget_file->setItem(row, 0, new QTableWidgetItem(fi.fileName()));
    }
    // 使用静态函数提取文件名列表
    mfileList = DataCompressWindow::extractFileNames(result);

    //统计测量时长，选取光纤口1数据来统计
    int count1data = DataCompressWindow::countFilesByPrefix(mfileList, "1data");

    // 从 ComboBox 获取单个文件包对应的时间长度（单位ms）
    int time_per = 50;
    if (ui->cmb_fileTime->currentText() == "50ms") {
        time_per = 50;
    } else if (ui->cmb_fileTime->currentText() == "66ms") {
        time_per = 66;
    }
    int measureTime = DataCompressWindow::calculateMeasureTime(count1data, time_per);

    ui->line_measure_startT->setText("0");
    ui->line_measure_endT->setText(QString::number(measureTime));

    ui->spinBox_time1->setMaximum(measureTime);
}

void OfflineWindow::on_action_analyze_triggered()
{
    //n-gamma甄别模式
    if(ui->action_ngamma->isChecked()){
        QElapsedTimer totalTimer;
        totalTimer.start();
        
        //提取有效波形参数
        int threshold = ui->spinBox_threshold->value();
        int pre_points = 20;
        int post_points = 512 - pre_points - 1;

        int startT = ui->spinBox_startT->value();
        int endT = ui->spinBox_endT->value();
        if(startT >= endT){
            QMessageBox::information(this, tr("提示"), tr("起始时间不能大于结束时间！"));
            return;
        }
        if(startT < 0 || endT > ui->line_measure_endT->text().toInt()){
            QMessageBox::information(this, tr("提示"), tr("起始时间不能小于0或大于测量时长！"));
            return;
        }

        qApp->setOverrideCursor(QCursor(Qt::WaitCursor));

        int time_per = 50;
        if (ui->cmb_fileTime->currentText() == "50ms") {
            time_per = 50;
        } else if (ui->cmb_fileTime->currentText() == "66ms") {
            time_per = 66;
        }

        //计算起始文件索引
        int fileIndex = floor(startT / time_per) + 1;
        //计算结束文件索引
        int endFileIndex = floor((endT-1) / time_per) + 1;
        //获取水平相机序号
        quint32 cameraIndex = ui->comboBox_horCamera->currentIndex() + 1;
        quint32 deviceIndex = (cameraIndex + 3) / 4;
        //根据相机序号计算出是第几块光纤卡
        int channelIndex = (cameraIndex - 1) % 4 + 1;// 1、2、3、4

        emit reporWriteLog(QString("n-gamma甄别模式，起始时间：%1，结束时间：%2").arg(startT).arg(endT),QtInfoMsg);
        emit reporWriteLog(QString("水平相机序号：%1，设备序号：%2").arg(cameraIndex).arg(deviceIndex),QtInfoMsg);
        emit reporWriteLog(QString("需要处理的文件数量：%1 (从文件%2到%3)").arg(endFileIndex - fileIndex + 1).arg(fileIndex).arg(endFileIndex),QtInfoMsg);
        
        // 提取该通道有效波形数据，并进行合并
        QElapsedTimer fileProcessTimer;
        fileProcessTimer.start();
        qint64 totalFileReadTime = 0;
        qint64 totalBaselineTime = 0;
        qint64 totalWaveExtractTime = 0;
        int processedFileCount = 0;

        QVector<std::array<qint16, 512>> ch_all_valid_wave;
#if 1
        QThreadPool* pool = QThreadPool::globalInstance();
        pool->setMaxThreadCount(QThread::idealThreadCount());
        QMutex mutex;
        for(int i = fileIndex; i <= endFileIndex; i++){
            QString filePath = QString("%1/%2data%3.bin").arg(ui->textBrowser_filepath->toPlainText()).arg(deviceIndex).arg(i);
            quint32 packerStartTime = (fileIndex - 1) * 50;//文件序号，每个文件50ms
            ExtractValidWaveformTask *task = new ExtractValidWaveformTask(deviceIndex,
                    cameraIndex,
                    packerStartTime,
                    threshold,
                    pre_points,
                    post_points,
                    filePath,
                    [&](quint32 packerCurrentTime, quint8 channelIndex, QVector<std::array<qint16, 512>>& wave_ch){
                QMutexLocker locker(&mutex);
                ch_all_valid_wave.append(wave_ch);
            });
            pool->start(task);
        }
        pool->waitForDone();
#else     
        for(int i = fileIndex; i <= endFileIndex; i++){
            QString filePath = QString("%1/%2data%3.bin").arg(ui->textBrowser_filepath->toPlainText()).arg(deviceIndex).arg(i);
            if(!QFileInfo::exists(filePath)){
                QMessageBox::information(this, tr("提示"), QString("文件%1不存在！").arg(filePath));
                emit reporWriteLog(QString("文件%1不存在！").arg(filePath),QtWarningMsg);
                continue;
            }

            QElapsedTimer singleFileTimer;
            singleFileTimer.start();
            
            //1、读取文件，获取该通道所有波形数据
            emit reporWriteLog(QString("读取文件%1 (%2/%3)").arg(filePath).arg(i - fileIndex + 1).arg(endFileIndex - fileIndex + 1),QtInfoMsg);
            QElapsedTimer readTimer;
            readTimer.start();
            QVector<qint16> ch0, ch1, ch2, ch3;
            if(!DataAnalysisWorker::readBin4Ch_fast(filePath, ch0, ch1, ch2, ch3, true)){
                emit reporWriteLog(QString("文件%1读取失败！").arg(filePath),QtWarningMsg);
                continue;
            }
            qint64 readTime = readTimer.elapsed();
            totalFileReadTime += readTime;
            emit reporWriteLog(QString("  文件读取耗时：%1 ms").arg(readTime),QtInfoMsg);

            //2、提取通道号的数据cameraNo
            //根据相机序号计算出是第几块光纤卡
            int board_index = (cameraIndex-1)/4+1;
            int ch_channel = cameraIndex % 4;
            
            QElapsedTimer baselineTimer;
            QElapsedTimer waveExtractTimer;
            
            if (ch_channel == 1) {
                //3、扣基线，调整数据
                baselineTimer.start();
                qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch0);
                DataAnalysisWorker::adjustDataWithBaseline(ch0, baseline_ch, board_index, 1);
                qint64 baselineTime = baselineTimer.elapsed();
                totalBaselineTime += baselineTime;
                emit reporWriteLog(QString("  基线计算和调整耗时：%1 ms").arg(baselineTime),QtInfoMsg);
                
                //4、提取有效波形数据
                waveExtractTimer.start();
                QVector<std::array<qint16, 512>> wave_ch = DataAnalysisWorker::overThreshold(ch0, 1, threshold, pre_points, post_points);
                qint64 waveExtractTime = waveExtractTimer.elapsed();
                totalWaveExtractTime += waveExtractTime;
                emit reporWriteLog(QString("  有效波形提取耗时：%1 ms，提取到 %2 个有效波形").arg(waveExtractTime).arg(wave_ch.size()),QtInfoMsg);
                ch_all_valid_wave.append(wave_ch);
            } else if (channelIndex == 2) {
                //3、扣基线，调整数据
                baselineTimer.start();
                qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch1);
                DataAnalysisWorker::adjustDataWithBaseline(ch1, baseline_ch, board_index, 2);
                qint64 baselineTime = baselineTimer.elapsed();
                totalBaselineTime += baselineTime;
                emit reporWriteLog(QString("  基线计算和调整耗时：%1 ms").arg(baselineTime),QtInfoMsg);
                
                //4、提取有效波形数据
                waveExtractTimer.start();
                QVector<std::array<qint16, 512>> wave_ch = DataAnalysisWorker::overThreshold(ch1, 2, threshold, pre_points, post_points);
                qint64 waveExtractTime = waveExtractTimer.elapsed();
                totalWaveExtractTime += waveExtractTime;
                emit reporWriteLog(QString("  有效波形提取耗时：%1 ms，提取到 %2 个有效波形").arg(waveExtractTime).arg(wave_ch.size()),QtInfoMsg);
                ch_all_valid_wave.append(wave_ch);
            } else if (channelIndex == 3) {
                //3、扣基线，调整数据
                baselineTimer.start();
                qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch2);
                DataAnalysisWorker::adjustDataWithBaseline(ch2, baseline_ch, board_index, 3);
                qint64 baselineTime = baselineTimer.elapsed();
                totalBaselineTime += baselineTime;
                emit reporWriteLog(QString("  基线计算和调整耗时：%1 ms").arg(baselineTime),QtInfoMsg);
                
                //4、提取有效波形数据
                waveExtractTimer.start();
                QVector<std::array<qint16, 512>> wave_ch = DataAnalysisWorker::overThreshold(ch2, 3, threshold, pre_points, post_points);
                qint64 waveExtractTime = waveExtractTimer.elapsed();
                totalWaveExtractTime += waveExtractTime;
                emit reporWriteLog(QString("  有效波形提取耗时：%1 ms，提取到 %2 个有效波形").arg(waveExtractTime).arg(wave_ch.size()),QtInfoMsg);
                ch_all_valid_wave.append(wave_ch);
            } else if (channelIndex == 4) {
                //3、扣基线，调整数据
                baselineTimer.start();
                qint16 baseline_ch = DataAnalysisWorker::calculateBaseline(ch3);
                DataAnalysisWorker::adjustDataWithBaseline(ch3, baseline_ch, board_index, 4);
                qint64 baselineTime = baselineTimer.elapsed();
                totalBaselineTime += baselineTime;
                emit reporWriteLog(QString("  基线计算和调整耗时：%1 ms").arg(baselineTime),QtInfoMsg);

                //4、提取有效波形数据
                waveExtractTimer.start();
                QVector<std::array<qint16, 512>> wave_ch = DataAnalysisWorker::overThreshold(ch3, 4, threshold, pre_points, post_points);
                qint64 waveExtractTime = waveExtractTimer.elapsed();
                totalWaveExtractTime += waveExtractTime;
                emit reporWriteLog(QString("  有效波形提取耗时：%1 ms，提取到 %2 个有效波形").arg(waveExtractTime).arg(wave_ch.size()),QtInfoMsg);
                ch_all_valid_wave.append(wave_ch);
            }
            
            qint64 singleFileTime = singleFileTimer.elapsed();
            processedFileCount++;
            emit reporWriteLog(QString("  文件%1总耗时：%2 ms").arg(i).arg(singleFileTime),QtInfoMsg);
        }
#endif
        qint64 fileProcessTime = fileProcessTimer.elapsed();
        emit reporWriteLog(QString("=== 文件处理阶段统计 ==="),QtInfoMsg);
        emit reporWriteLog(QString("处理文件总数：%1").arg(processedFileCount),QtInfoMsg);
        emit reporWriteLog(QString("文件处理总耗时：%1 ms (%2 秒)")
                    .arg(fileProcessTime)
                    .arg(fileProcessTime / 1000.0, 0, 'f', 2),
                QtInfoMsg
        );

        emit reporWriteLog(QString("  文件读取总耗时：%1 ms (%2 秒，占比 %3%%)")
                .arg(totalFileReadTime)
                .arg(totalFileReadTime / 1000.0, 0, 'f', 2)
                .arg(fileProcessTime > 0 ? (100.0 * totalFileReadTime / fileProcessTime) : 0.0, 0, 'f', 1),
            QtInfoMsg
        );

        emit reporWriteLog(QString("  基线计算总耗时：%1 ms (%2 秒，占比 %3%%)")
                .arg(totalBaselineTime)
                .arg(totalBaselineTime / 1000.0, 0, 'f', 2)
                .arg(fileProcessTime > 0 ? (100.0 * totalBaselineTime / fileProcessTime) : 0.0, 0, 'f', 1),
            QtInfoMsg
        );

        emit reporWriteLog(QString("  波形提取总耗时：%1 ms (%2 秒，占比 %3%%)")
                .arg(totalWaveExtractTime)
                .arg(totalWaveExtractTime / 1000.0, 0, 'f', 2)
                .arg(fileProcessTime > 0 ? (100.0 * totalWaveExtractTime / fileProcessTime) : 0.0, 0, 'f', 1),
            QtInfoMsg
        );
        emit reporWriteLog(QString("合并后有效波形总数：%1").arg(ch_all_valid_wave.size()),QtInfoMsg);
        
        n_gamma neutron;
        //计算PSD
        QElapsedTimer psdTimer;
        psdTimer.start();
        emit reporWriteLog(QString("开始计算PSD，有效波形数量：%1").arg(ch_all_valid_wave.size()),QtInfoMsg);
        QVector<QPair<float, float>> data = neutron.computePSD(ch_all_valid_wave);
        qint64 psdTime = psdTimer.elapsed();
        emit reporWriteLog(QString("PSD计算耗时：%1 ms (%2 秒)，得到 %2 个数据点").arg(psdTime).arg(psdTime / 1000.0, 0, 'f', 2).arg(data.size()),QtInfoMsg);

        // 计算密度
        QElapsedTimer densityTimer;
        densityTimer.start();
        emit reporWriteLog(QString("开始计算密度分布"),QtInfoMsg);
        QVector<float> den = neutron.computeDensity(data, 200);
        qint64 densityTime = densityTimer.elapsed();
        emit reporWriteLog(QString("密度计算耗时：%1 ms (%2 秒)").arg(densityTime).arg(densityTime / 1000.0, 0, 'f', 2),QtInfoMsg);
        
        // 提取 Energy 和 PSD 向量用于绘图，转换为 double（setData 需要 double）
        QElapsedTimer convertTimer;
        convertTimer.start();
        QVector<double> energyVec, psdVec;
        QVector<double> denDouble;  // 将 float 转换为 double
        energyVec.reserve(data.size());
        psdVec.reserve(data.size());
        denDouble.reserve(den.size());
        for (const auto& pair : data) {
            energyVec.append(static_cast<double>(pair.first));   // Energy: float -> double
            psdVec.append(static_cast<double>(pair.second));     // PSD: float -> double
        }
        for (float d : den) {
            denDouble.append(static_cast<double>(d));
        }
        qint64 convertTime = convertTimer.elapsed();
        emit reporWriteLog(QString("数据类型转换耗时：%1 ms").arg(convertTime),QtInfoMsg);
        
        // 绘制PSD图表
        QElapsedTimer plotTimer;
        plotTimer.start();
        emit reportPSDPlot(PCIeCommSdk::CameraOrientation::Horizontal, energyVec, psdVec, denDouble);
        qint64 plotTime = plotTimer.elapsed();
        emit reporWriteLog(QString("PSD图表绘制耗时：%1 ms").arg(plotTime),QtInfoMsg);
        
        // 计算FoM
        QElapsedTimer fomTimer;
        fomTimer.start();
        emit reporWriteLog(QString("开始计算FoM"),QtInfoMsg);
        n_gamma::HistResult histCount = neutron.selectAndHist(data);
        qint64 histTime = fomTimer.elapsed();
        emit reporWriteLog(QString("  直方图计算耗时：%1 ms").arg(histTime),QtInfoMsg);
        
        QElapsedTimer fomCalcTimer;
        fomCalcTimer.start();
        n_gamma::FOM FOM_data = neutron.GetFOM(histCount.psd_x, histCount.count_y);
        qint64 fomCalcTime = fomCalcTimer.elapsed();
        qint64 fomTotalTime = fomTimer.elapsed();
        emit reporWriteLog(QString("  FoM拟合计算耗时：%1 ms").arg(fomCalcTime),QtInfoMsg);
        emit reporWriteLog(QString("FoM计算总耗时：%1 ms (%2 秒)").arg(fomTotalTime).arg(fomTotalTime / 1000.0, 0, 'f', 2),QtInfoMsg);
        
        if(FOM_data.R1 < 0.90 || FOM_data.R2 < 0.90){
            QMessageBox::information(this, tr("提示"), tr("FoM拟合不成功，请调整阈值或延长测量时间！"));
            emit reporWriteLog(QString("FoM拟合不成功，请调整阈值或延长测量时间！"),QtWarningMsg);
        }

        // 存储FoM绘图数据
        QElapsedTimer fomPlotTimer;
        fomPlotTimer.start();
        QVector<FOM_CurvePoint> curveData;
        for (size_t i = 0; i < histCount.psd_x.size(); ++i) {
            curveData.push_back(FOM_CurvePoint(histCount.psd_x[i], FOM_data.Y[i], FOM_data.Y_fit1[i], FOM_data.Y_fit2[i]));
        }

        // 绘制FoM图表
        emit reportFoMPlot(PCIeCommSdk::CameraOrientation::Horizontal, curveData);
        qApp->restoreOverrideCursor();
        qint64 fomPlotTime = fomPlotTimer.elapsed();
        emit reporWriteLog(QString("FoM图表绘制耗时：%1 ms").arg(fomPlotTime),QtInfoMsg);
        
        qint64 totalTime = totalTimer.elapsed();
        emit reporWriteLog(QString("=== n-gamma甄别模式总耗时统计 ==="),QtInfoMsg);
        emit reporWriteLog(QString("总耗时：%1 ms (%2 秒)").arg(totalTime).arg(totalTime / 1000.0, 0, 'f', 2),QtInfoMsg);
        emit reporWriteLog(QString("  文件处理阶段：%1 ms (%2%%)").arg(fileProcessTime).arg(totalTime > 0 ? (100.0 * fileProcessTime / totalTime) : 0.0, 0, 'f', 2),QtInfoMsg);
        emit reporWriteLog(QString("  PSD计算阶段：%1 ms (%2%%)")
                    .arg(psdTime)
                    .arg(totalTime > 0 ? (100.0 * psdTime / totalTime) : 0.0, 0, 'f', 2),
                QtInfoMsg
        );

        emit reporWriteLog(QString("  密度计算阶段：%1 ms (%2%%)")
                .arg(densityTime)
                .arg(totalTime > 0 ? (100.0 * densityTime / totalTime) : 0.0, 0, 'f', 1),
            QtInfoMsg
        );

        emit reporWriteLog(QString("  FoM计算阶段：%1 ms (%2%%)")
                .arg(fomTotalTime)
                .arg(totalTime > 0 ? (100.0 * fomTotalTime / totalTime) : 0.0, 0, 'f', 1),
            QtInfoMsg
        );

        const auto otherTime = totalTime - fileProcessTime - psdTime - densityTime - fomTotalTime;
        emit reporWriteLog(QString("  其他（转换、绘图等）：%1 ms (%2%%)")
                .arg(otherTime)
                .arg(totalTime > 0 ? (100.0 * otherTime / totalTime) : 0.0, 0, 'f', 1),
            QtInfoMsg
        );
        return;
    }

    // 从 RadioButton 提取单个文件包对应的时间长度（单位ms）
    // radioButton 对应 1ms, radioButton_2 对应 10ms
    int timeLength = 1; // 默认值 1ms
    if (ui->radioButton_2->isChecked()) {
        timeLength = 10; // radioButton_2 选中时使用 10ms
    } else if (ui->radioButton->isChecked()) {
        timeLength = 1; // radioButton 选中时使用 1ms
    }
    
    // 从 ComboBox 获取单个文件包对应的时间长度（单位ms）
    int time_per = 50;
    PCIeCommSdk::CaptureTime captureTime = PCIeCommSdk::oldCaptureTime;
    if (ui->cmb_fileTime->currentText() == "50ms") {
        captureTime = PCIeCommSdk::newCaptureTime;
        time_per = 50;
    } else if (ui->cmb_fileTime->currentText() == "66ms") {
        captureTime = PCIeCommSdk::oldCaptureTime;
        time_per = 66;
    }
    
    //由于每个文件的能谱时长是固定的，所有这里需要根据时刻计算出对应的是第几个文件
    qint32 time1 =ui->spinBox_time1->value();
    quint32 fileIndex = (float)(ui->spinBox_time1->value() + time_per-1)/ time_per;

    //t1-水平相机
    {
        //每个文件里面对应的是4个通道，根据通道号判断是第几个文件
        quint32 cameraIndex = ui->comboBox_horCamera->currentIndex() + 1;
        quint32 deviceIndex = (cameraIndex + 3) / 4;
        QString filePath = QString("%1/%2data%3.bin").arg(ui->textBrowser_filepath->toPlainText()).arg(deviceIndex).arg(fileIndex);

        mPCIeCommSdk.setCaptureParamter(captureTime, cameraIndex, timeLength, time1);
        if (QFileInfo::exists(filePath) && !mPCIeCommSdk.openHistoryFile(filePath))
        {
            QMessageBox::information(this, tr("提示"), tr("文件格式错误，加载失败！"));
        }
    }

    //t1-垂直相机
    {
        quint32 cameraIndex = ui->comboBox_verCamera->currentIndex() + 12;
        quint32 deviceIndex = (ui->comboBox_verCamera->currentIndex() + 12) / 4;
        QString filePath = QString("%1/%2data%3.bin").arg(ui->textBrowser_filepath->toPlainText()).arg(deviceIndex).arg(fileIndex);
        mPCIeCommSdk.setCaptureParamter(captureTime, cameraIndex, timeLength, time1);
        if (QFileInfo::exists(filePath) && !mPCIeCommSdk.openHistoryFile(filePath))
        {
            QMessageBox::information(this, tr("提示"), tr("文件格式错误，加载失败！"));
        }
    }
    return;

    // 绘图测试部分
    //波形
    {
        QVector<QPair<double ,double>> waveformPairs[2];// = generateArray(50, 50, 0, 25, 1000, 35);
        QFile file("./waveform_lsd.csv");
        if (ui->action_typeLBD->isChecked())
            file.setFileName("./waveform_lbd.csv");
        else if (ui->action_typePSD->isChecked())
            file.setFileName("./waveform_psd.csv");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
            QTextStream stream(&file);
            stream.readLine();//过滤掉标题头
            while (!stream.atEnd())
            {
                QString line = stream.readLine();
                QStringList row = line.split(',', Qt::SkipEmptyParts);
                if (row.size() >= 3)
                {
                    waveformPairs[0].push_back(qMakePair(row.at(0).toDouble(), row.at(1).toDouble()));
                    waveformPairs[1].push_back(qMakePair(row.at(0).toDouble(), row.at(2).toDouble()));
                }
            }
            file.close();

            emit reportWaveform(1, ui->comboBox_horCamera->currentIndex()+1, waveformPairs[0]);
            emit reportWaveform(1, ui->comboBox_verCamera->currentIndex()+12, waveformPairs[1]);
        }
    }

    //PSD
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

            emit reportCalculateDensityPSD(PCIeCommSdk::CameraOrientation::Horizontal, spectrumPairs);
            emit reportCalculateDensityPSD(PCIeCommSdk::CameraOrientation::Vertical, spectrumPairs);
        }
    }

    //FoM
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
            QVector<FOM_CurvePoint> curveFOM;
            while (!stream.atEnd())
            {
                QString line = stream.readLine();
                QStringList row = line.split(',', Qt::SkipEmptyParts);
                if (row.size() == 4)
                {
                    if (row.at(0).toDouble()>=limit_x1 && row.at(0).toDouble()<=limit_x2){
                        curveFOM.push_back(FOM_CurvePoint(row.at(0).toDouble(), row.at(1).toDouble(), row.at(2).toDouble(), row.at(3).toDouble()));
                    }
                }
            }
            file.close();

            emit reportFoMPlot(PCIeCommSdk::CameraOrientation::Horizontal, curveFOM);
        }
    }

    //能谱
    {
        QVector<QPair<double,double>> spectrumPair[2];
        QFile file("./spectrum_lsd.csv");
        if (ui->action_typeLBD->isChecked())
            file.setFileName("./spectrum_lbd_r.csv");
        else if (ui->action_typePSD->isChecked())
            file.setFileName("./spectrum_psd_n.csv");

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
    QString filePath = ui->lineEdit_savePath->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先选择保存路径！");
        return;
    }
    if (!filePath.endsWith(".h5", Qt::CaseInsensitive)) {
        filePath += ".h5";
    }

    // 获取当前显示的图表
    QCustomPlot* customPlot = nullptr;
    if (ui->action_typeLSD->isChecked())
        customPlot = ui->spectroMeter_waveform_LSD;
    else if (ui->action_typeLBD->isChecked())
        customPlot = ui->spectroMeter_waveform_LBD;
    else
        customPlot = ui->spectroMeter_waveform_PSD;

    if (!customPlot) {
        QMessageBox::warning(this, "警告", "无法获取图表对象！");
        return;
    }

    // 检查图表是否有数据
    if (customPlot->graphCount() < 2) {
        QMessageBox::warning(this, "警告", "图表中没有足够的数据！");
        return;
    }

    // 从 graph(0) 获取 Horizontal 数据，从 graph(1) 获取 Vertical 数据
    QCPGraph* horizontalGraph = customPlot->graph(0);
    QCPGraph* verticalGraph = customPlot->graph(1);

    if (!horizontalGraph || !verticalGraph) {
        QMessageBox::warning(this, "警告", "无法获取图表数据！");
        return;
    }

    // 提取 Horizontal 数据
    QVector<QPair<double, double>> horizontalData;
    if (horizontalGraph && horizontalGraph->data()) {
        QSharedPointer<QCPGraphDataContainer> horizontalDataContainer = horizontalGraph->data();
        if (horizontalDataContainer) {
            for (auto it = horizontalDataContainer->begin(); it != horizontalDataContainer->end(); ++it) {
                horizontalData.append(qMakePair(it->key, it->value));
            }
        }
    }

    // 提取 Vertical 数据
    QVector<QPair<double, double>> verticalData;
    if (verticalGraph && verticalGraph->data()) {
        QSharedPointer<QCPGraphDataContainer> verticalDataContainer = verticalGraph->data();
        if (verticalDataContainer) {
            for (auto it = verticalDataContainer->begin(); it != verticalDataContainer->end(); ++it) {
                verticalData.append(qMakePair(it->key, it->value));
            }
        }
    }

    if (horizontalData.isEmpty() && verticalData.isEmpty()) {
        QMessageBox::warning(this, "警告", "图表中没有数据可保存！");
        return;
    }

    // 将数据写入 HDF5 文件
    try {
        bool fileExists = QFileInfo::exists(filePath);
        H5::H5File file(filePath.toStdString(), fileExists ? H5F_ACC_RDWR : H5F_ACC_TRUNC);

        // 写入 Horizontal 数据集
        if (!horizontalData.isEmpty() && horizontalData.size() > 0) {
            QVector<double> horizontalFlatData;
            horizontalFlatData.reserve(horizontalData.size() * 2);
            for (const auto& pair : horizontalData) {
                horizontalFlatData.append(pair.first);
                horizontalFlatData.append(pair.second);
            }
            
            // 检查数据是否有效，避免创建零大小的数据集
            // horizontalFlatData 的大小应该是 horizontalData.size() * 2
            if (horizontalFlatData.size() > 0 && horizontalFlatData.size() == horizontalData.size() * 2) {
                hsize_t dims[2] = {static_cast<hsize_t>(horizontalData.size()), 2};
                H5::DataSpace dataspace(2, dims);
                
                // 删除已存在的数据集
                if (H5Lexists(file.getId(), "Horizontal", H5P_DEFAULT) > 0) {
                    file.unlink("Horizontal");
                }
                
                H5::DataSet horizontalDataset = file.createDataSet("Horizontal", H5::PredType::NATIVE_DOUBLE, dataspace);
                horizontalDataset.write(horizontalFlatData.data(), H5::PredType::NATIVE_DOUBLE);
                horizontalDataset.close();
            }
        }

        // 写入 Vertical 数据集
        if (!verticalData.isEmpty() && verticalData.size() > 0) {
            QVector<double> verticalFlatData;
            verticalFlatData.reserve(verticalData.size() * 2);
            for (const auto& pair : verticalData) {
                verticalFlatData.append(pair.first);
                verticalFlatData.append(pair.second);
            }
            
            // 检查数据是否有效，避免创建零大小的数据集
            // verticalFlatData 的大小应该是 verticalData.size() * 2
            if (verticalFlatData.size() > 0 && verticalFlatData.size() == verticalData.size() * 2) {
                hsize_t dims[2] = {static_cast<hsize_t>(verticalData.size()), 2};
                H5::DataSpace dataspace(2, dims);
                
                // 删除已存在的数据集
                if (H5Lexists(file.getId(), "Vertical", H5P_DEFAULT) > 0) {
                    file.unlink("Vertical");
                }
                
                H5::DataSet verticalDataset = file.createDataSet("Vertical", H5::PredType::NATIVE_DOUBLE, dataspace);
                verticalDataset.write(verticalFlatData.data(), H5::PredType::NATIVE_DOUBLE);
                verticalDataset.close();
            }
        }

        file.close();
        
        QMessageBox::information(this, "成功", QString("数据已成功保存到：\n%1").arg(filePath));
        
    } catch (const H5::FileIException& error) {
        QMessageBox::critical(this, "错误", QString("保存文件失败：\n%1").arg(error.getDetailMsg().c_str()));
    } catch (const H5::DataSetIException& error) {
        QMessageBox::critical(this, "错误", QString("创建数据集失败：\n%1").arg(error.getDetailMsg().c_str()));
    } catch (const H5::DataSpaceIException& error) {
        QMessageBox::critical(this, "错误", QString("创建数据空间失败：\n%1").arg(error.getDetailMsg().c_str()));
    } catch (...) {
        QMessageBox::critical(this, "错误", "保存数据时发生未知错误！");
    }
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
        // QString styleSheet = mIsDarkTheme ?
        //                          QString("background-color:rgb(%1,%2,%3);color:white;")
        //                              .arg(palette.color(QPalette::Window).red())
        //                              .arg(palette.color(QPalette::Window).green())
        //                              .arg(palette.color(QPalette::Window).blue())
        //                                   : QString("background-color:white;color:black;");
        // ui->logWidget->setStyleSheet(styleSheet);

        //更新样式表
        // QList<QCheckBox*> checkBoxs = customPlot->findChildren<QCheckBox*>();
        // int i = 0;
        // for (auto checkBox : checkBoxs){
        //     checkBox->setStyleSheet(styleSheet);
        // }

        // QGraphicsScene *scene = this->findChild<QGraphicsScene*>("logGraphicsScene");
        // QGraphicsTextItem *textItem = (QGraphicsTextItem*)scene->items()[0];
        // textItem->setHtml(mIsDarkTheme ? QString("<font color='white'>工作日志</font>") : QString("<font color='black'>工作日志</font>"));

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
void OfflineWindow::replyWaveform(quint8 timestampIndex, quint8 cameraIndex, QVector<QPair<double,double>>& waveformBytes)
{
    quint8 cameraOrientation = cameraIndex <= 11 ? PCIeCommSdk::CameraOrientation::Horizontal : PCIeCommSdk::CameraOrientation::Vertical;

    //实测曲线
    QCustomPlot* customPlot = nullptr;
    if (ui->action_typeLSD->isChecked())
        customPlot = ui->spectroMeter_waveform_LSD;
    else if (ui->action_typeLBD->isChecked())
        customPlot = ui->spectroMeter_waveform_LBD;
    else
        customPlot = ui->spectroMeter_waveform_PSD;

    QVector<double> keys, values;
    for (int i=0; i<waveformBytes.size(); ++i){
        keys << waveformBytes[i].first ;
        values << waveformBytes[i].second;
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

    // 设置 X 轴标题
    customPlot->xAxis->setLabel(tr("时间(ns)"));
    
    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);
    customPlot->replot(QCustomPlot::rpQueuedReplot);
}

void OfflineWindow::replySpectrum(quint8 timestampIndex, quint8 cameraOrientation, const QVector<QPair<double,double>>& pairs)
{
    if (ui->action_typeLSD->isChecked()){
        QCustomPlot* customPlot = ui->spectroMeter_spectrum_LSD;
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

        customPlot->xAxis->rescale(true);
        customPlot->yAxis->rescale(true);
        //customPlot->yAxis->setRange(yMin-spaceDisc, yMax+spaceDisc);
        customPlot->replot(QCustomPlot::rpQueuedReplot);
    }
    else{
        QCustomPlot* customPlot = nullptr;
        if (ui->action_typeLBD->isChecked())
            customPlot = ui->spectroMeter_spectrum_LBD;
        else
            customPlot = ui->spectroMeter_spectrum_PSD;

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
}

/**
 * @brief 对PSD数据对，进行统计，给出PSD分布密度图
 * @param cameraOrientation 相机方向，水平或垂直
 * @param pairs PSD数据对,每个元素是一个 QPair<double, double>，first 是 Energy，second 是 PSD
 */
void OfflineWindow::replyCalculateDensityPSD(quint8 cameraOrientation, QVector<QPair<double ,double>>& pairs)
{
    QVector<double> x, y;
    QVector<double> z;
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
    QVector<QVector<quint32>> densityMap(NLevel+1, QVector<quint32>(NLevel+1, 0));//创建(NLevel+1)×(NLevel+1)的零矩阵，用于存储密度
    double step_x = (max_x-min_x)/(NLevel-1);//  % x轴步长
    double step_y = (max_y-min_y)/(NLevel-1);//  % y轴步长

    //‌第一次循环：计算密度分布‌
    //遍历所有数据点，计算每个点所在的网格坐标
    for (auto pair : pairs){
        quint32 densityMap_x = quint32((pair.first-min_x)/step_x)+1;// - 计算x方向网格索引
        quint32 densityMap_y = quint32((pair.second-min_y)/step_y)+1;// - 计算y方向网格索引
        densityMap[densityMap_x][densityMap_y]++;//- 对应网格位置计数加1
    }
    
    //第二次循环：获取每个点的密度值‌
    //再次遍历所有数据点，根据网格索引获取对应的密度值
    for (auto pair : pairs){
        quint32 densityMap_x = quint32((pair.first-min_x)/step_x)+1;// - 计算x方向网格索引
        quint32 densityMap_y = quint32((pair.second-min_y)/step_y)+1;// - 计算y方向网格索引
        z << densityMap[densityMap_x][densityMap_y];
    }

    emit reportPSDPlot(cameraOrientation, x, y, z);
}

/**
 * @brief 绘制PSD分布密度图
 * @param cameraOrientation 相机方向，水平或垂直
 * @param psd_x PSD x轴数据，Energy
 * @param psd_y PSD y轴数据, PSD值
 * @param density PSD分布密度数据，不需要归一化
 */
void OfflineWindow::replyPSDPlot(quint8 cameraOrientation, const QVector<double>& psd_x, const QVector<double>& psd_y, const QVector<double>& density)
{
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

    //归一化PSD分布密度图,范围0-100
    double max_density = *std::max_element(std::begin(density), std::end(density));
    // double min_density = *std::min_element(std::begin(density), std::end(density));
    double min_density = 0.0;
    double range_density = max_density - min_density;
    QVector<double> normalized_density;
    normalized_density.resize(density.size());
    if (range_density > 0){
        for (int i = 0; i < density.size(); i++){
            normalized_density[i] = (density[i] - min_density) / range_density * 100.0;  // 归一化到 0~100
        }
    }
    else{
        normalized_density = density;
    }

    QVector<QColor> z;
    z.reserve(normalized_density.size());
    for (int i = 0; i < normalized_density.size(); i++){
        z << gradient.color(normalized_density[i], QCPRange(0, 100), false);
    }

    // setData 需要 double 类型的数据，第三个参数是 QColor 向量
    double max_x = *std::max_element(std::begin(psd_x), std::end(psd_x));
    double min_x = *std::min_element(std::begin(psd_x), std::end(psd_x));
    double max_y = *std::max_element(std::begin(psd_y), std::end(psd_y));
    double min_y = *std::min_element(std::begin(psd_y), std::end(psd_y));
    colorMap->data()->setRange(QCPRange(min_x, max_x), QCPRange(min_y, max_y));// 设置网格数据范围
    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);

    customPlot->graph(0)->setData(psd_x, psd_y, z);
    customPlot->replot(QCustomPlot::RefreshPriority::rpQueuedReplot);
}

void OfflineWindow::replyFoMPlot(quint8 cameraOrientation, const QVector<FOM_CurvePoint>& pairs)
{
    // 核密度图谱
    QCustomPlot* customPlot = nullptr;
    if (PCIeCommSdk::CameraOrientation::Horizontal == cameraOrientation){
        customPlot = ui->spectroMeter_horCamera_FOM;
    }
    else{
        customPlot = ui->spectroMeter_verCamera_FOM;
    }

    // 绘制FoM散点曲线
    {
        QVector<double> x, y;
        QVector<QColor> z;
        for (auto pair : pairs){
            x << pair.x;
            y << pair.y1;
            z << QColor::fromRgb(0x87,0xBA,0xDD);
        }
        customPlot->graph(0)->setData(x, y, z);
    }
    
    // 绘制拟合曲线1
    {
        QVector<double> x, y;
        QVector<QColor> z;
        for (auto pair : pairs){
            x << pair.x;
            y << pair.y2;
            z << Qt::red;
        }
        customPlot->graph(1)->setData(x, y, z);
    }

    //绘制拟合曲线2
    {
        QVector<double> x, y;
        QVector<QColor> z;
        for (auto pair : pairs){
            x << pair.x;
            y << pair.y3;
            z << Qt::black;
        }
        customPlot->graph(2)->setData(x, y, z);
    }

    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);
    customPlot->replot(QCustomPlot::RefreshPriority::rpQueuedReplot);
}

void OfflineWindow::on_action_typeLSD_triggered(bool checked)
{
    if (checked){
        ui->centralVboxStackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_LSD);
        ui->spectroMeter_spectrum_LSD->hide();

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
        ui->centralVboxStackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_PSD);
        ui->spectroMeter_spectrum_PSD->hide();

        this->setWindowTitle(QApplication::applicationName() + "PSD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "PSD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "PSD");

        ui->action_waveform->setVisible(false);
        ui->action_ngamma->setVisible(false);
    }
}


void OfflineWindow::on_action_typeLBD_triggered(bool checked)
{
    if (checked){
        ui->centralVboxStackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget_LBD);
        ui->spectroMeter_spectrum_LBD->hide();

        this->setWindowTitle(QApplication::applicationName() + "LBD探测器" + " - " + APP_VERSION);
        mainWindow->setWindowTitle(QApplication::applicationName() + "LBD探测器" + " - " + APP_VERSION);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/DetType", "LBD");

        ui->action_waveform->setVisible(false);
        ui->action_ngamma->setVisible(false);
    }
}

//波形模式
void OfflineWindow::on_action_waveform_triggered(bool checked)
{
    if (checked){
        ui->spectroMeter_waveform_LSD->setVisible(true);
        // ui->spectroMeter_spectrum_LSD->setVisible(true);
        ui->spectroMeter_horCamera_PSD->setVisible(false);
        ui->spectroMeter_horCamera_FOM->setVisible(false);
        ui->spectroMeter_verCamera_PSD->setVisible(false);
        ui->spectroMeter_verCamera_FOM->setVisible(false);

        //显示选择时刻、波形长度按钮        
        ui->widget_waveStartT->setVisible(true);
        ui->widget_waveLength->setVisible(true);
        //隐藏n-gamma甄别模式按钮
        ui->widget_ngamma->setVisible(false);
    }
}

//n-gamma甄别模式
void OfflineWindow::on_action_ngamma_triggered(bool checked)
{
    if (checked){
        ui->spectroMeter_waveform_LSD->setVisible(false);
        ui->spectroMeter_spectrum_LSD->setVisible(false);
        ui->spectroMeter_horCamera_PSD->setVisible(true);
        ui->spectroMeter_horCamera_FOM->setVisible(true);
        ui->spectroMeter_verCamera_PSD->setVisible(true);
        ui->spectroMeter_verCamera_FOM->setVisible(true);

        //隐藏选择时刻、波形长度按钮
        ui->widget_waveStartT->setVisible(false);
        ui->widget_waveLength->setVisible(false);
        //显示n-gamma甄别模式按钮
        ui->widget_ngamma->setVisible(true);
    }
}

// 验证时间范围输入
void OfflineWindow::validateTime1Range()
{
    // 获取最大测量时间
    QString maxTimeStr = ui->line_measure_endT->text();
    bool ok;
    int maxTime = maxTimeStr.toInt(&ok);
    
    if (!ok || maxTime <= 0) {
        // 如果最大值无效，不进行验证
        return;
    }
    
    // 更新 spinBox 的最大值（QSpinBox会自动限制输入值不超过最大值）
    ui->spinBox_time1->setMaximum(maxTime);
    
    // 获取当前输入值
    int currentTime = ui->spinBox_time1->value();
    
    // 如果当前值超过最大值，自动调整为最大值
    if (currentTime > maxTime) {
        ui->spinBox_time1->setValue(maxTime);
        QMessageBox::warning(this, "输入警告", 
                            QString("输入的时间值超过了最大测量时间，已自动调整为 %1 ms").arg(maxTime));
    }
}

