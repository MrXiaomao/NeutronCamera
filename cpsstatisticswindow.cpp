#include "cpsstatisticswindow.h"
#include "ui_cpsstatisticswindow.h"
#include "globalsettings.h"
#include "datacompresswindow.h"
#include "qprogressindicator.h"
#include "qcustomplothelper.h"
#include <QElapsedTimer>

CpsStatisticsWindow::CpsStatisticsWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::CpsStatisticsWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper*>(parent))
    , mAnalysisThread(nullptr)
    , mAnalysisWorker(nullptr)
{
    ui->setupUi(this);

    initUi();
    initWaveformPage();
    initCpsPage();

    QActionGroup* actGroup = new QActionGroup(this);
    actGroup->addAction(ui->action_waveform);
    actGroup->addAction(ui->action_process);
    actGroup->addAction(ui->action_cps);
    ui->action_waveform->setChecked(true);
    emit ui->action_waveform->triggered(true);

    connect(ui->toolButton_cps, &QToolButton::clicked, this, [=]{
        onCpsStatistics();
    });
    connect(ui->toolButton_process, &QToolButton::clicked, this, &CpsStatisticsWindow::onDataProcess);
    //connect(ui->toolButton_waveform, &QToolButton::clicked, this, &CpsStatisticsWindow::doWaveformPlot);

    //QStringList args = QCoreApplication::arguments();
    //this->setWindowTitle(QApplication::applicationName()+" - "+APP_VERSION + " [" + args[4] + "]");
    this->applyColorTheme();

    connect(this, SIGNAL(doWriteLog(const QString&,QtMsgType)), this, SLOT(onWriteLog(const QString&,QtMsgType)));
    connect(this, SIGNAL(doCpsPlot(QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>>)), this, SLOT(onCpsPlot(QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>>)));
    connect(this, SIGNAL(doSpectrumPlot(QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>>)), this, SLOT(onSpectrumPlot(QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>>)));
    
    QTimer::singleShot(0, this, [&](){
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96

        if(mainWindow) {
            mainWindow->fixMenuBarWidth();
        }
    });
}

CpsStatisticsWindow::~CpsStatisticsWindow()
{
    delete ui;
}


void CpsStatisticsWindow::initUi()
{
    mProgressIndicator = new QProgressIndicator(this);

    ui->tableWidget_file->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    ui->tableWidget_filelist->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableWidget_filelist->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    //////////////////////////////////////////////////////////////////////
    //布局
    {
        //大布局
        QSplitter *splitterH1 = new QSplitter(Qt::Horizontal,this);
        splitterH1->setObjectName("splitterH1");
        splitterH1->setHandleWidth(5);
        splitterH1->addWidget(ui->leftStackedWidget);
        splitterH1->addWidget(ui->centralStackedWidget);
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
        QActionGroup *themeActionGroup = new QActionGroup(this);
        ui->action_lightTheme->setActionGroup(themeActionGroup);
        ui->action_darkTheme->setActionGroup(themeActionGroup);
        ui->action_lightTheme->setChecked(!mIsDarkTheme);
        ui->action_darkTheme->setChecked(mIsDarkTheme);
    }
}

void CpsStatisticsWindow::closeEvent(QCloseEvent *event) {
    if ( QMessageBox::Yes == QMessageBox::question(this, tr("系统退出提示"), tr("确定要退出软件系统吗？"),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes))
        event->accept();
    else
        event->ignore();
}

bool CpsStatisticsWindow::eventFilter(QObject *watched, QEvent *event){
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

void CpsStatisticsWindow::onWriteLog(const QString &msg, QtMsgType msgType/* = QtDebugMsg*/)
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

void CpsStatisticsWindow::on_action_openfile_triggered()
{
    GlobalSettings settings;
    QString lastPath = settings.value("Global/Offline/LastFileDir", QDir::homePath()).toString();
    QString dirPath = QFileDialog::getExistingDirectory(this, tr("选择测量数据存放目录"), lastPath);

    // 目录格式：炮号+日期+[cardIndex]测量数据[packIndex].bin
    if (dirPath.isEmpty())
        return;

    settings.setValue("Global/Offline/LastFileDir", dirPath);
    ui->textBrowser_filepath->setText(dirPath);
    if (!QFileInfo::exists(dirPath+"/device_config.ini")){
        QMessageBox::information(this, tr("提示"), tr("路径无效，缺失\"device_config.ini\"文件！"));
        return;
    }
    else {
        GlobalSettings settings(dirPath+"/device_config.ini");
        mShotNum = settings.value("Global/ShotNum", "00000").toString();
        mCurrentDetectorType = (DetectorType)settings.value("Global/DetectType", 0).toUInt();

        emit doWriteLog("实验炮号：" + mShotNum);
        if (mCurrentDetectorType == dtLBD){
            emit doWriteLog("探测器类型：LBD探测器");
        }
        else if (mCurrentDetectorType == dtLSD){
            emit doWriteLog("探测器类型：LSD探测器");
        }
        else if (mCurrentDetectorType == dtPSD){
            emit doWriteLog("探测器类型：PSD探测器");
        }
        else{
            emit doWriteLog("探测器类型：未知");
        }
    }

    //加载目录下所有文件，罗列在表格中，统计给出文件大小
    loadRelatedFiles(dirPath);
}

// 计算文件信息列表的总大小
qint64 CpsStatisticsWindow::calculateTotalSize(const QFileInfoList& fileinfoList)
{
    qint64 totalSize = 0;
    for (const QFileInfo& fi : fileinfoList) {
        totalSize += fi.size();
    }
    return totalSize;
}

void CpsStatisticsWindow::loadRelatedFiles(const QString& dirPath)
{
    mFileDir = dirPath;
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
    {
        qint64 totalSize = calculateTotalSize(fileinfoList);
        int fileCount = fileinfoList.count();

        //统计文件详细信息
        // ==== 填表 ====
        ui->tableWidget_filelist->setSortingEnabled(false);  // 填表时关闭排序避免抖动
        ui->tableWidget_filelist->clearContents();
        ui->tableWidget_filelist->setRowCount(fileCount);

        // 列设置：只需要设一次
        // 例：列0 文件名，列1 大小(bytes)，列2 可读大小，列3 最后修改时间
        if (ui->tableWidget_filelist->columnCount() != 4) {
            ui->tableWidget_filelist->setColumnCount(4);
            ui->tableWidget_filelist->setHorizontalHeaderLabels(
                {"文件名", "大小(bytes)", "大小(MB)", "修改时间"}
                );
        }

        QLocale locale(QLocale::English);
        for (int i = 0; i < fileCount; ++i) {
            const QFileInfo& fi = fileinfoList.at(i);

            auto *itemName = new QTableWidgetItem(fi.fileName());
            itemName->setFlags(itemName->flags() ^ Qt::ItemIsEditable);

            auto *itemBytes = new QTableWidgetItem(locale.toString(fi.size()));
            itemBytes->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            itemBytes->setFlags(itemBytes->flags() ^ Qt::ItemIsEditable);

            auto *itemHuman = new QTableWidgetItem(humanReadableSize(fi.size()));
            itemHuman->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);            
            itemHuman->setFlags(itemHuman->flags() ^ Qt::ItemIsEditable);

            auto *itemTime = new QTableWidgetItem(fi.lastModified().toString("yyyy-MM-dd HH:mm:ss"));
            itemHuman->setTextAlignment(Qt::AlignCenter);
            itemTime->setFlags(itemTime->flags() ^ Qt::ItemIsEditable);

            ui->tableWidget_filelist->setItem(i, 0, itemName);
            ui->tableWidget_filelist->setItem(i, 1, itemBytes);
            ui->tableWidget_filelist->setItem(i, 2, itemHuman);
            ui->tableWidget_filelist->setItem(i, 3, itemTime);
        }

        // 表头美化（可选）
        ui->tableWidget_filelist->horizontalHeader()->setMinimumWidth(200);
        ui->tableWidget_filelist->horizontalHeader()->setStretchLastSection(true);
        ui->tableWidget_filelist->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        ui->tableWidget_filelist->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->tableWidget_filelist->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->tableWidget_filelist->setAlternatingRowColors(true);

        emit doWriteLog(QString("bin文件数量: %1, 总大小: %2").arg(fileCount).arg(humanReadableSize(totalSize)), QtDebugMsg);
        ui->lineEdit_binCount->setText(QString::number(fileCount));
        ui->lineEdit_binTotal->setText(humanReadableSize(totalSize));
    }

    // 根据文件名统计整个目录下文件的测量时长（仅已第1张卡的DDR1作为参考）
    {
        //统计测量时长，选取光纤口1数据来统计
        int count1data = DataCompressWindow::countFilesByPrefix(mfileList, "Adata") / 3;//正常情况下是3张卡（每张卡分A、B两面，所以这里除以3）

        // 从 ComboBox 获取单个文件包对应的时间长度（单位ms）
        const int time_per = 40;
        int measureTime = DataCompressWindow::calculateMeasureTime(count1data, time_per);

        // 获取第一个文件名打包序号作为开始时间，如：1Adata27.bin
        QString file_name = mfileList.first();
        // 查找data起始位置
        int data_start = file_name.indexOf("data");
        if (data_start != -1) {
            // 跳过data，从后续字符中提取开头的连续数字
            QString sub_str = file_name.mid(data_start + 4); // data长度为4，所以偏移4
            int digit_end = 0;
            while (digit_end < sub_str.length() && sub_str[digit_end].isDigit()) {
                digit_end++;
            }
            QString result_str = sub_str.left(digit_end); // 得到"27"
            int start_tm = result_str.toInt();

            // 波形显示页面
            ui->line_waveform_startT_1->setText(QString::number((start_tm-1)*time_per));
            ui->line_waveform_endT_1->setText(QString::number((start_tm-1)*time_per+measureTime));
            ui->spinBox_startT_1->setValue((start_tm-1)*time_per);
            ui->spinBox_endT_1->setValue(start_tm*time_per);

            // 数据压缩处理页面
            ui->line_waveform_startT_2->setText(QString::number((start_tm-1)*time_per));
            ui->line_waveform_endT_2->setText(QString::number((start_tm-1)*time_per+measureTime));
            ui->spinBox_startT_2->setValue((start_tm-1)*time_per);
            ui->spinBox_endT_2->setValue((start_tm-1)*time_per+measureTime);

            // 计数率统计页面
            ui->line_waveform_startT_3->setText(QString::number((start_tm-1)*time_per));
            ui->line_waveform_endT_3->setText(QString::number((start_tm-1)*time_per+measureTime));
            ui->spinBox_startT_3->setValue((start_tm-1)*time_per);
            ui->spinBox_endT_3->setValue(start_tm*time_per);
        }
        else {
            ui->line_measure_startT_3->setText("0");
            ui->line_measure_endT_3->setText(QString::number(measureTime));
            ui->spinBox_time1->setMinimum(0);
            ui->spinBox_time1->setMaximum(measureTime);
        }
    }

    // 仅过滤 .h5 文件
    QStringList filters;
    filters << "*.h5";
    fileinfoList.clear();
    fileinfoList = QDir(mFileDir).entryInfoList(
        filters,
        QDir::Files | QDir::NoSymLinks,
        QDir::Unsorted
        );

    ui->comboBox_h5Files->clear();
    for (auto item : fileinfoList){
        ui->comboBox_h5Files->addItem(item.baseName());
    }

    if (fileinfoList.size() == 0)
        emit doWriteLog(QStringLiteral("未找到压缩后的H5文件，请先对数据做压缩处理"));
    else
        emit doWriteLog(QStringLiteral("目录下共找到%1个经过压缩处理的H5格式波形文件").arg(fileinfoList.size()));
}


void CpsStatisticsWindow::on_action_exit_triggered()
{
    mainWindow->close();
}


void CpsStatisticsWindow::on_action_lightTheme_triggered()
{
    if(!mIsDarkTheme) return;
    mIsDarkTheme = false;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Offline/Startup/darkTheme","false");
    applyColorTheme();
}


void CpsStatisticsWindow::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Offline/Startup/darkTheme","true");
    applyColorTheme();
}

void CpsStatisticsWindow::applyColorTheme()
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

        // 窗体背景色
        customPlot->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Dark) : Qt::white));
        // 四边安装轴并显示
        customPlot->axisRect()->setupFullAxesBox();
        customPlot->axisRect()->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Dark) : Qt::white));

        customPlot->replot();
    }
}

void CpsStatisticsWindow::on_action_colorTheme_triggered()
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

QPixmap CpsStatisticsWindow::maskPixmap(QPixmap pixmap, QSize sz, QColor clrMask)
{
    // 更新图标颜色
    QPixmap result = pixmap.scaled(sz);
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), clrMask);
    return result;
}

QPixmap CpsStatisticsWindow::roundPixmap(QSize sz, QColor clrOut)
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

QPixmap CpsStatisticsWindow::dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut)
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


#include <QRandomGenerator>
//void CpsStatisticsWindow::init3DCurve()
//{
//    const int channelCount = 18; // 通道总数

//    // 1. 创建3D曲面视图
//    surface = new CustomSurface();//QtDataVisualization::Q3DSurface();
//    surface->setShadowQuality(QtDataVisualization::QAbstract3DGraph::ShadowQualityNone); //禁用阴影以提升性能
//    surface->setSelectionMode(QtDataVisualization::QAbstract3DGraph::SelectionNone); //禁用选中

//    QWidget *container = QWidget::createWindowContainer(surface);
//    container->setMinimumSize(800, 600);  // 强制设置容器大小（Qt 5.15.2 必须）
//    QVBoxLayout* vLayout = new QVBoxLayout(ui->spectroMeterPageInfoWidget_3);
//    vLayout->addWidget(container);
//    ui->spectroMeterPageInfoWidget_3->setLayout(vLayout);

//    surface->activeTheme()->setGridEnabled(true);  // 显示网格
//    surface->activeTheme()->setLabelBorderEnabled(false);  // 关闭标签边框

//    // QList<QLinearGradient> stops;
//    // stops << QLinearGradient(0.0, Qt::blue);   // 最小值对应蓝色
//    // stops << QLinearGradient(0.5, Qt::green);  // 中间值对应绿色
//    // stops << QLinearGradient(1.0, Qt::red);    // 最大值对应红色
//    // surface->activeTheme()->setBaseGradients(stops);

//    // 2. 设置基础颜色与渐变
//    // surface->activeTheme()->setGridLineColor(QColor(80, 80, 80)); // 深灰色网格线
//    // surface->activeTheme()->setLightColor(QColor(255, 255, 245)); // 暖白色光源

//    // 3. 设置高亮效果
//    // surface->activeTheme()->setSingleHighlightColor(QColor(255, 90, 90)); // 单元素高亮红
//    // surface->activeTheme()->setMultiHighlightColor(QColor(90, 255, 90));  // 多元素高亮绿

//    // 4. 配置光照强度
//    surface->activeTheme()->setLightStrength(1.0);          // 主光强度
//    surface->activeTheme()->setAmbientLightStrength(1.0);   // 环境光强度
//    surface->activeTheme()->setHighlightLightStrength(1.0); // 高亮光强度

//    // 2. 配置坐标轴（X:通道, Y:计数率, Z:时间）
//    QtDataVisualization::QValue3DAxis *xAxis = new QtDataVisualization::QValue3DAxis();
//    xAxis->setTitle("通道号");
//    xAxis->setTitleVisible(true);
//    xAxis->setRange(0, channelCount*10 + 10); // CH1-CH18
//    xAxis->setLabelFormat("CH%.0f"); // 显示CH1-CH18
//    xAxis->setSegmentCount(channelCount + 1); // 设置刻度数量（确保每个通道都有刻度）
//    xAxis->setFormatter(new QValue3DAxisFormatterX());

//    QtDataVisualization::QValue3DAxis *yAxis = new QtDataVisualization::QValue3DAxis();
//    yAxis->setTitle("计数率");
//    yAxis->setTitleVisible(true);
//    yAxis->setRange(0, 200); // 示例计数率范围0-100

//    QtDataVisualization::QValue3DAxis *zAxis = new QtDataVisualization::QValue3DAxis();
//    zAxis->setTitle("时间 (μs)");
//    zAxis->setTitleVisible(true);
//    zAxis->setRange(200, 300); // 示例时间范围200-300μs

//    surface->setAxisX(xAxis);
//    surface->setAxisY(yAxis);
//    surface->setAxisZ(zAxis);

//    // 3. 生成18个通道的平面数据

//    // 预定义18种颜色（区分通道）
//    QList<QColor> channelColors = {
//        QColor(230, 243, 233, 100), QColor(229, 239, 248, 100), QColor(251, 238, 221, 100), QColor(248, 224, 222, 100), Qt::cyan, Qt::magenta,
//        Qt::darkRed, Qt::darkGreen, Qt::darkBlue, Qt::darkYellow, Qt::darkCyan, Qt::darkMagenta,
//        Qt::lightGray, Qt::gray, Qt::darkGray, Qt::black, Qt::white, Qt::red
//    };

//    // 随机生成浅色系颜色
//    auto generateLightColor1=[]()->QColor {
//        // HSL 模式：色相(0-359)随机，饱和度(50-80)适中，亮度(80-95)高（浅色核心）
//        int hue = QRandomGenerator64::global()->bounded(0, 359);          // 随机色相（全色系覆盖）
//        int saturation = 50 + QRandomGenerator64::global()->bounded(0, 30); // 饱和度 50-80（避免过灰或过艳）
//        int lightness = 80 + QRandomGenerator64::global()->bounded(0, 15);  // 亮度 80-95（确保浅色）
//        return QColor::fromHsl(hue, saturation, lightness);
//    };
//    auto generateLightColor2=[]()->QColor {
//        // RGB 每个通道取 180-255（高亮度区间），随机组合
//        int r = 180 + QRandomGenerator64::global()->bounded(0, 75); // 180-255
//        int g = 180 + QRandomGenerator64::global()->bounded(0, 75);
//        int b = 180 + QRandomGenerator64::global()->bounded(0, 75);
//        return QColor(r, g, b);
//    };
//    auto generateLightColor3=[]()->QColor {
//        // HSV 模式：色相随机，饱和度(30-60)偏低，明度(90-98)极高
//        int hue = QRandomGenerator64::global()->bounded(0, 359);
//        int saturation = 30 + QRandomGenerator64::global()->bounded(0, 30); // 30-60（柔和不刺眼）
//        int value = 90 + QRandomGenerator64::global()->bounded(0, 8);       // 90-98（接近白色但保留色彩）
//        return QColor::fromHsv(hue, saturation, value);
//    };

//    // 随机生成深色系颜色
//    auto generateDarkColor1=[]()->QColor {
//        // HSV 模式：色相随机，饱和度(80-100)极高，明度(20-40)低
//        int hue = QRandomGenerator64::global()->bounded(0, 359);
//        int saturation = 80 + QRandomGenerator64::global()->bounded(0, 20); // 80-100（色彩浓郁）
//        int value = 20 + QRandomGenerator64::global()->bounded(0, 20);      // 20-40（深色但不发黑）
//        return QColor::fromHsv(hue, saturation, value);
//    };
//    auto generateDarkColor2=[]()->QColor {
//        // RGB 每个通道取 0-100（低亮度区间），随机组合
//        int r = QRandomGenerator64::global()->bounded(0, 100); // 0-100
//        int g = QRandomGenerator64::global()->bounded(0, 100);
//        int b = QRandomGenerator64::global()->bounded(0, 100);
//        return QColor(r, g, b);
//    };
//    auto generateDarkColor3=[]()->QColor {
//        // HSV 模式：色相随机，饱和度(80-100)极高，明度(20-40)低
//        int hue = QRandomGenerator64::global()->bounded(0, 359);
//        int saturation = 80 + QRandomGenerator64::global()->bounded(0, 20); // 80-100（色彩浓郁）
//        int value = 20 + QRandomGenerator64::global()->bounded(0, 20);      // 20-40（深色但不发黑）
//        return QColor::fromHsv(hue, saturation, value);
//    };

//    for (int ch = 1; ch <= channelCount; ++ch) {
//        // 3.1. 固定X值（平面垂直于X轴，如X=10）
//        float fixedX = ch*10.0f; // 每个通道对应一个不同的X值，间隔100单位

//        // 3.2. 定义Z范围（底边和上边的Z坐标一致）
//        int zMin = 201, zMax = 300;  // Z∈[200,300]（101个点）
//        int zCount = zMax - zMin + 1;  // Z轴点数：101

//        // 3.3. 定义上边的Y值（折线函数，示例：Y随Z分段变化）
//        QVector<float> upperY(zCount);
//        for (int zIdx = 0; zIdx < zCount; ++zIdx) {
//            int z = zMin + zIdx;
//            // 折线逻辑：Z≤250时Y=50，250<Z≤280时Y=80，Z>280时Y=30
//            if (z <= 250) upperY[zIdx] = 50.0f;
//            else if (z <= 280) upperY[zIdx] = 80.0f;
//            else upperY[zIdx] = 30.0f;

//            upperY[zIdx] = 100.0f + QRandomGenerator::global()->bounded(-5, 5); // 添加随机波动
//        }

//        // 3.7. 配置曲面系列
//        if (1){
//            QtDataVisualization::QSurfaceDataRow *upperRow = new QtDataVisualization::QSurfaceDataRow(zCount);
//            for (int zIdx = 0; zIdx < zCount; ++zIdx) {
//                int z = zMin + zIdx;
//                (*upperRow)[zIdx] = QVector3D(fixedX, upperY[zIdx], z);  // Y=折线值
//            }
//            QtDataVisualization::QSurfaceDataRow *bottomRow = new QtDataVisualization::QSurfaceDataRow(zCount);
//            for (int zIdx = 0; zIdx < zCount; ++zIdx) {
//                int z = zMin + zIdx;
//                (*bottomRow)[zIdx] = QVector3D(fixedX, -10.0f, z);  // Y=0，贴合XZ平面
//            }

//            QtDataVisualization::QSurfaceDataProxy *proxy = new QtDataVisualization::QSurfaceDataProxy();
//            QtDataVisualization::QSurface3DSeries *series = new QtDataVisualization::QSurface3DSeries(proxy);
//            series->setBaseColor(channelColors[ch-1]);  // 半透蓝色
//            series->setDrawMode(QtDataVisualization::QSurface3DSeries::DrawSurface);
//            series->setMeshSmooth(true); // 启用网格平滑

//            QLinearGradient gradient;//generateDarkColor1
//            gradient.setColorAt(0.0, generateLightColor1());    // 最低高度
//            gradient.setColorAt(0.5, generateLightColor2());   // 中间高度
//            gradient.setColorAt(1.0, channelColors[ch-1]);     // 最高高度
//            gradient.setStart(QPointF(0, 0));     // 渐变起始坐标（相对系列）
//            gradient.setFinalStop(QPointF(1, 1)); // 渐变结束坐标
//            series->setBaseGradient(gradient);
//            series->setColorStyle(QtDataVisualization::Q3DTheme::ColorStyleRangeGradient); // 启用渐变着色
//            series->setProperty("fixedColorStyle", false);

//            QtDataVisualization::QSurfaceDataArray *dataArray = new QtDataVisualization::QSurfaceDataArray();
//            dataArray->reserve(2);
//            dataArray->append(bottomRow);
//            dataArray->append(upperRow);

//            series->dataProxy()->resetArray(dataArray);
//            series->setFlatShadingEnabled(false);// 启用平面着色

//            // 添加到视图并调整视角
//            surface->addSeries(series);
//        }

//        // 3.8. 配置窄曲面表示曲线系列
//        if (1){
//            QtDataVisualization::QSurfaceDataRow *upperRow = new QtDataVisualization::QSurfaceDataRow(zCount);
//            QtDataVisualization::QSurfaceDataRow *bottomRow = new QtDataVisualization::QSurfaceDataRow(zCount);
//            for (int zIdx = 0; zIdx < zCount; ++zIdx) {
//                int z = zMin + zIdx;
//                (*upperRow)[zIdx] = QVector3D(fixedX, upperY[zIdx], z);  // Y=折线值
//                (*bottomRow)[zIdx] = QVector3D(fixedX, upperY[zIdx]+0.5, z);  // Y=0
//            }

//            QtDataVisualization::QSurfaceDataProxy *proxy = new QtDataVisualization::QSurfaceDataProxy();
//            QtDataVisualization::QSurface3DSeries *series = new QtDataVisualization::QSurface3DSeries(proxy);
//            series->setBaseColor(channelColors[ch-1]);
//            series->setDrawMode(QtDataVisualization::QSurface3DSeries::DrawSurface); // 线框模式，仅显示曲线
//            series->setMeshSmooth(true); // 启用网格平滑
//            series->setColorStyle(QtDataVisualization::Q3DTheme::ColorStyleUniform); // 启用渐变着色
//            series->setProperty("fixedColorStyle", true);

//            QtDataVisualization::QSurfaceDataArray *dataArray = new QtDataVisualization::QSurfaceDataArray();
//            dataArray->reserve(2);
//            dataArray->append(upperRow);
//            dataArray->append(bottomRow); // 仅添加上边数据，底边数据不添加（Y=0，贴合XZ平面，不显示）
//            series->dataProxy()->resetArray(dataArray);
//            series->setFlatShadingEnabled(false);// 启用平面着色

//            surface->addSeries(series);
//        }
//    }

//    // 4. 调整视角
//    // 沿X轴正方向观察（平面垂直于X轴，需从侧面看）
//    surface->scene()->activeCamera()->setCameraPosition(
//        -45.0f,   // 方位角（Azimuth）：沿Y轴旋转90°，正对X轴
//        45.0f,   // 仰角（Elevation）：45°俯视
//        100.0f   // 距离（Distance）：拉远视角，确保平面在视场内
//        );

//    surface->show();
//}


void CpsStatisticsWindow::initWaveformPage()
{
    mGraphisColor.push_back(QColor::fromRgb(0,47,167));
    mGraphisColor.push_back(QColor::fromRgb(255,0,77));
    mGraphisColor.push_back(QColor::fromRgb(255,204,51));
    mGraphisColor.push_back(QColor::fromRgb(10,186,181));
    mGraphisColor.push_back(QColor::fromRgb(5,75,58));
    mGraphisColor.push_back(QColor::fromRgb(129,216,208));
    mGraphisColor.push_back(QColor::fromRgb(0,140,141));
    mGraphisColor.push_back(QColor::fromRgb(128,0,32));
    mGraphisColor.push_back(QColor::fromRgb(142,76,41));
    mGraphisColor.push_back(QColor::fromRgb(205,0,0));
    mGraphisColor.push_back(QColor::fromRgb(0,102,128));
    mGraphisColor.push_back(QColor::fromRgb(32,88,39));
    mGraphisColor.push_back(QColor::fromRgb(132,88,39));
    mGraphisColor.push_back(QColor::fromRgb(32,188,39));
    mGraphisColor.push_back(QColor::fromRgb(32,88,139));
    mGraphisColor.push_back(QColor::fromRgb(92,188,139));
    mGraphisColor.push_back(QColor::fromRgb(62,108,179));
    mGraphisColor.push_back(QColor::fromRgb(232,88,39));

    QCustomPlot *customPlotHor = new QCustomPlot(this);
    QCustomPlotHelper* customPlotHelperHor = new QCustomPlotHelper(customPlotHor, this);
    customPlotHor->legend->setVisible(true);
    //customPlotHor->legend->setWrap(9);
    customPlotHor->xAxis->setRange(QCPRange(0, 1000000));
    customPlotHor->yAxis->setRange(QCPRange(0, 16000));
    customPlotHor->setNoAntialiasingOnDrag(false);
    for (int i=1; i<=11; ++i){
        QCPGraph *graph = customPlotHor->addGraph();
        graph->setLineStyle(QCPGraph::lsLine);
        if (i<12){
            graph->setName(QStringLiteral("HC %1").arg(i));
            graph->setPen(QPen(mGraphisColor[i-1], 2, Qt::PenStyle::SolidLine));
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, mGraphisColor[i-1], 10));//显示散点图
        }
        else{
            graph->setName(QStringLiteral("VC %1").arg(i));
            graph->setPen(QPen(mGraphisColor[i-1], 2, Qt::PenStyle::DashLine));
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCross, mGraphisColor[i-1], 10));//显示散点图
        }
    }
    customPlotHor->replot(QCustomPlot::rpQueuedReplot);

    QCustomPlot *customPlotVer = new QCustomPlot(this);
    QCustomPlotHelper* customPlotHelperVer = new QCustomPlotHelper(customPlotVer, this);
    customPlotVer->legend->setVisible(true);
    //customPlotVer->legend->setWrap(9);
    customPlotVer->xAxis->setRange(QCPRange(0, 1000000));
    customPlotVer->yAxis->setRange(QCPRange(0, 16000));
    customPlotVer->setNoAntialiasingOnDrag(false);
    for (int i=12; i<=18; ++i){
        QCPGraph *graph = customPlotVer->addGraph();
        graph->setLineStyle(QCPGraph::lsLine);
        if (i<12){
            graph->setName(QStringLiteral("HC %1").arg(i));
            graph->setPen(QPen(mGraphisColor[i-1], 2, Qt::PenStyle::SolidLine));
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, mGraphisColor[i-1], 10));//显示散点图
        }
        else{
            graph->setName(QStringLiteral("VC %1").arg(i));
            graph->setPen(QPen(mGraphisColor[i-1], 2, Qt::PenStyle::DashLine));
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCross, mGraphisColor[i-1], 10));//显示散点图
        }
    }
    customPlotVer->replot(QCustomPlot::rpQueuedReplot);

    QVBoxLayout* vLayout = new QVBoxLayout(ui->pageInfoWidget_waveform);
    vLayout->setMargin(0);
    vLayout->setSpacing(1);
    vLayout->addWidget(customPlotHor);
    vLayout->addWidget(customPlotVer);
    ui->pageInfoWidget_waveform->setLayout(vLayout);

}

void CpsStatisticsWindow::initCpsPage()
{
    const int channelCount = 18; // 通道总数
    QCustomPlot *customPlot = new QCustomPlot(this);
    QCustomPlotHelper* customPlotHelper = new QCustomPlotHelper(customPlot, this);
    // customPlot->setInteraction(QCP::iRangeDrag, true);
    // customPlot->setInteraction(QCP::iRangeZoom, true);
    // customPlot->setInteraction(QCP::iSelectPlottables, true);
    customPlot->legend->setVisible(true);
    customPlot->legend->setWrap(9);
    customPlot->setNoAntialiasingOnDrag(false);
    customPlot->plotLayout()->clear();

    connect(customPlot, &QCustomPlot::plottableClick, this, [&](QCPAbstractPlottable* plottable, int dataIndex, QMouseEvent* event) {

    });

    // 能谱曲线
    QCPAxisRect *spectrumAxisRect = new QCPAxisRect(customPlot);
    spectrumAxisRect->setObjectName("spectrumAxisRect");
    {
        spectrumAxisRect->setupFullAxesBox();
        spectrumAxisRect->setMinimumMargins(QMargins(0,0,0,0));
        spectrumAxisRect->setMargins(QMargins(0,0,0,0));
        spectrumAxisRect->axis(QCPAxis::AxisType::atBottom)->setPadding(0);
        spectrumAxisRect->axis(QCPAxis::AxisType::atLeft)->setLabel(tr("V"));
        spectrumAxisRect->axis(QCPAxis::AxisType::atBottom)->setLabel(tr("Channel"));
        spectrumAxisRect->axis(QCPAxis::AxisType::atBottom)->setRange(0, 2000);
        spectrumAxisRect->axis(QCPAxis::AxisType::atLeft)->setRange(0, 16384);

        // 左上角添加图例
        QCPLegend *legend = new QCPLegend();
        legend->setWrap(9);
        spectrumAxisRect->insetLayout()->addElement(legend, Qt::AlignLeft | Qt::AlignTop); // 清空默认布局

        QCPAxis *keyAxis = spectrumAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = spectrumAxisRect->axis(QCPAxis::AxisType::atLeft);

        for (int i=1; i<=18; ++i){            
            QCPGraph *graph = customPlot->addGraph(keyAxis, valueAxis);
            graph->setLineStyle(QCPGraph::lsLine);
            if (i<12){
                graph->setName(QStringLiteral("HC %1").arg(i));
                graph->setPen(QPen(mGraphisColor[i-1], 1, Qt::PenStyle::SolidLine));
                graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, mGraphisColor[i-1], 5));//显示散点图
            }
            else{
                graph->setName(QStringLiteral("VC %1").arg(i));
                graph->setPen(QPen(mGraphisColor[i-1], 1, Qt::PenStyle::DashLine));
                graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCross, mGraphisColor[i-1], 5));//显示散点图
            }
        }
    }

    // 时间计数曲线
    QCPAxisRect *timeCountsAxisRect = new QCPAxisRect(customPlot);
    timeCountsAxisRect->setObjectName("timeCountsAxisRect");
    {
        timeCountsAxisRect->setupFullAxesBox();
        timeCountsAxisRect->setMinimumMargins(QMargins(0,0,0,0));
        timeCountsAxisRect->setMargins(QMargins(0,0,0,0));
        timeCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->setPadding(0);
        timeCountsAxisRect->axis(QCPAxis::AxisType::atLeft)->setLabel(tr("Count"));
        timeCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->setLabel(tr("Time/ms"));
        timeCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->setRange(0, 2000);
        timeCountsAxisRect->axis(QCPAxis::AxisType::atLeft)->setRange(0, 1200);

        // 左上角添加图例
        QCPLegend *legend = new QCPLegend();
        legend->setWrap(9);
        timeCountsAxisRect->insetLayout()->addElement(legend, Qt::AlignLeft | Qt::AlignTop); // 清空默认布局

        QCPAxis *keyAxis = timeCountsAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = timeCountsAxisRect->axis(QCPAxis::AxisType::atLeft);

        for (int i=1; i<=18; ++i){
            QCPGraph *graph = customPlot->addGraph(keyAxis, valueAxis);
            graph->setLineStyle(QCPGraph::lsLine);
            if (i<12){
                graph->setName(QStringLiteral("HC %1").arg(i));
                graph->setPen(QPen(mGraphisColor[i-1], 1, Qt::PenStyle::SolidLine));
                graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, mGraphisColor[i-1], 5));//显示散点图
            }
            else{
                graph->setName(QStringLiteral("VC %1").arg(i));
                graph->setPen(QPen(mGraphisColor[i-1], 1, Qt::PenStyle::DashLine));
                graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCross, mGraphisColor[i-1], 5));//显示散点图
            }

            customPlot->legend->removeItem(customPlot->legend->itemWithPlottable(graph)); // 移除与graph关联的图例项，从而隐藏图例
        }
    }

    // 时间探测器计数曲线
    QCPAxisRect *timeChannelCountsAxisRect = new QCPAxisRect(customPlot);
    timeChannelCountsAxisRect->setObjectName("timeChannelCountsAxisRect");
   	timeChannelCountsAxisRect->setRangeZoomFactor(1, 1);//禁止轴缩放
   	timeChannelCountsAxisRect->setRangeDragAxes(nullptr, nullptr);// 禁止轴拖拽
    QCPColorScale *timeChannelCountsColorScale = nullptr;
    {
        timeChannelCountsAxisRect->setupFullAxesBox();
        timeChannelCountsAxisRect->setMinimumMargins(QMargins(0,0,0,0));
        timeChannelCountsAxisRect->setMargins(QMargins(0,0,0,0));
        timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atLeft)->setLabel(tr("Channel#"));
        timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->setLabel(tr("Time/ms"));

        QCPAxis *keyAxis = timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atLeft);
        timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atLeft)->setTickLengthIn(0);
        timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atLeft)->setSubTickLengthIn(0);
        timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atTop)->setTickPen(Qt::NoPen);
        timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atTop)->setSubTickPen(Qt::NoPen);
        timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atRight)->setTickPen(Qt::NoPen);
        timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atRight)->setSubTickPen(Qt::NoPen);
        timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->setSubTickLengthIn(0);

        // 禁止坐标轴缩放
        //keyAxis->setRange(-50.0, 2050.0);// 数据量是20个，每个间隔是100ms，所以总时间是2000，+/-50是为了让标签名称正好显示在中间位置
        //valueAxis->setRange(0.5, 18.5);// +0.5是为了让名称正好显示在中间位置
        //keyAxis->setRange(0.0, 10000.0);// 数据量是20个，每个间隔是100ms，所以总时间是2000，+/-50是为了让标签名称正好显示在中间位置
        //valueAxis->setRange(0. , 18.0);// +0.5是为了让名称正好显示在中间位置
        {
            QSharedPointer<QCPAxisTickerText> textTicker(new QCPAxisTickerText);
            QVector<QString> labels;
            QVector<double> positions;
            for (int i = 0; i <= 17; ++i) {
                positions << (double)(i+0.5);
                labels << QString::number(i+1);
            }
            textTicker->setTicks(positions, labels);
            valueAxis->setTicker(textTicker);
        }

        QCPColorMap *colorMap = new QCPColorMap(keyAxis, valueAxis);
        colorMap->setName("colorMap");
        colorMap->setInterpolate(false);
        colorMap->data()->setSize(1000, 18);
        colorMap->data()->setRange(QCPRange(0, 1000), QCPRange(0, 18));
        colorMap->setInterpolate(true);// 颜色平滑过度
        colorMap->setTightBoundary(true);//设置最外层的数据行和列是否被裁剪到指定的键值范围
        colorMap->rescaleDataRange(true);
        colorMap->rescaleValueAxis(true, true);
        colorMap->rescaleKeyAxis(true);

        timeChannelCountsColorScale = new QCPColorScale(customPlot);
        timeChannelCountsColorScale->setType(QCPAxis::atRight);
        timeChannelCountsColorScale->setRangeDrag(false);
        timeChannelCountsColorScale->setRangeZoom(false);
        colorMap->setColorScale(timeChannelCountsColorScale);
        QCPColorGradient gradient(QCPColorGradient::gpRainbow);
        gradient.setColorStopAt(0, QColor(255, 255, 255));
        colorMap->setGradient(gradient/*QCPColorGradient::gpRainbow*/);

        QCPMarginGroup *marginGroup = new QCPMarginGroup(customPlot);
        timeChannelCountsAxisRect->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
        timeChannelCountsColorScale->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);

        customPlot->legend->removeItem(customPlot->legend->itemWithPlottable(colorMap)); // 移除与graph关联的图例项，从而隐藏图例
    }

    // 计数叠加柱状图
    QCPAxisRect *channelCountsAxisRect = new QCPAxisRect(customPlot);
    channelCountsAxisRect->setRangeZoomFactor(1, 1.2);//禁止X轴缩放
    channelCountsAxisRect->setRangeDragAxes(nullptr, nullptr);// 禁止轴拖拽
    channelCountsAxisRect->setObjectName("channelCountsAxisRect");
    {
        channelCountsAxisRect->setupFullAxesBox();
        channelCountsAxisRect->setMinimumMargins(QMargins(0,0,0,0));
        channelCountsAxisRect->setMargins(QMargins(0,0,0,0));
        channelCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->setPadding(0);
        channelCountsAxisRect->axis(QCPAxis::AxisType::atLeft)->setLabel(tr("Counts(summed)"));
        channelCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->setLabel(tr("Channel#"));
        channelCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->setRange(0, 19);
        channelCountsAxisRect->axis(QCPAxis::AxisType::atLeft)->setRange(0, 12000);
        //countsAxisRect->axis(QCPAxis::AxisType::atBottom)->grid()->setZeroLinePen(Qt::NoPen);

        QCPAxis *keyAxis = channelCountsAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = channelCountsAxisRect->axis(QCPAxis::AxisType::atLeft);
        valueAxis->setRangeLower(0);// 设置Y轴最小值
        QCPBars *cpBars = new QCPBars(keyAxis, valueAxis);
        cpBars->setName("cpBars");
        cpBars->setAntialiased(false);
        cpBars->setWidth(1);
        cpBars->setWidthType(QCPBars::WidthType::wtPlotCoords);
        cpBars->setSelectable(QCP::stNone);

        QVector<QString> labels;
        QVector<double> positions;
        for (int i = 1; i < 19; ++i) {
             positions << (double)(i);
             labels << QString::number(i);
        }
        QSharedPointer<QCPAxisTickerText> textTicker(new QCPAxisTickerText);
        textTicker->setTicks(positions, labels);
        keyAxis->setTicker(textTicker);
        keyAxis->setRange(0.5, 18.5);
        valueAxis->setRange(0.0, 12000.0);

        customPlot->legend->removeItem(customPlot->legend->itemWithPlottable(cpBars)); // 移除与graph关联的图例项，从而隐藏图例
    }

    /*
    spectrumAxisRect            |   timeCountsAxisRect
    timeChannelCountsAxisRect   |   channelCountsAxisRect
    */

    // 设置上下对齐
    {
        QCPMarginGroup* marginGroup = new QCPMarginGroup(customPlot);
        spectrumAxisRect->setMarginGroup(QCP::msTop | QCP::msBottom, marginGroup);
        timeCountsAxisRect->setMarginGroup(QCP::msTop | QCP::msBottom, marginGroup);
    }

    // 设置上下对齐
    {
        QCPMarginGroup* marginGroup = new QCPMarginGroup(customPlot);
        timeChannelCountsAxisRect->setMarginGroup(QCP::msTop | QCP::msBottom, marginGroup);
        channelCountsAxisRect->setMarginGroup(QCP::msTop | QCP::msBottom, marginGroup);
        timeChannelCountsColorScale->setMarginGroup(QCP::msTop | QCP::msBottom, marginGroup);
    }

    // 设置左边边界联动对齐
    {
        QCPMarginGroup* marginGroup = new QCPMarginGroup(customPlot);
        spectrumAxisRect->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);
        timeChannelCountsAxisRect->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);
    }

    {
        QCPMarginGroup* marginGroup = new QCPMarginGroup(customPlot);
        timeCountsAxisRect->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);
        channelCountsAxisRect->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);
    }

    // 关联信号槽函数
    {
        connect(timeCountsAxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), timeCountsAxisRect->axis(QCPAxis::AxisType::atTop), SLOT(setRange(QCPRange)));
        connect(timeCountsAxisRect->axis(QCPAxis::AxisType::atLeft), SIGNAL(rangeChanged(QCPRange)), timeCountsAxisRect->axis(QCPAxis::AxisType::atRight), SLOT(setRange(QCPRange)));
        connect(channelCountsAxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), channelCountsAxisRect->axis(QCPAxis::AxisType::atTop), SLOT(setRange(QCPRange)));
        connect(channelCountsAxisRect->axis(QCPAxis::AxisType::atLeft), SIGNAL(rangeChanged(QCPRange)), channelCountsAxisRect->axis(QCPAxis::AxisType::atRight), SLOT(setRange(QCPRange)));
    }

    QList<QCPAxis*> allAxes;
    allAxes << spectrumAxisRect->axes() << timeCountsAxisRect->axes() << timeChannelCountsAxisRect->axes() << channelCountsAxisRect->axes();
    foreach (QCPAxis *axis, allAxes)
    {
        axis->setLayer("axes");
        axis->grid()->setLayer("grid");
    }

    // 布局
    QCPLayoutGrid *leftLayout = new QCPLayoutGrid;
    {
        leftLayout->addElement(0, 0, spectrumAxisRect);//上面
        leftLayout->addElement(1, 0, timeChannelCountsAxisRect);//下面
        leftLayout->addElement(1, 1, timeChannelCountsColorScale);//下面
    }

    QCPLayoutGrid *rightLayout = new QCPLayoutGrid;
    {
        rightLayout->addElement(0, 0, timeCountsAxisRect);//上面
        rightLayout->addElement(1, 0, channelCountsAxisRect);//下面
    }

    customPlotHelper->setGraphCheckBox(customPlot, spectrumAxisRect);
    customPlotHelper->setGraphCheckBox(customPlot, timeCountsAxisRect);

    customPlot->plotLayout()->clear();
    customPlot->plotLayout()->addElement(0, 0, leftLayout);//左边
    customPlot->plotLayout()->addElement(0, 1, rightLayout);//右边
    //customPlot->plotLayout()->setColumnStretchFactor(0, 1);
    //customPlot->plotLayout()->setColumnStretchFactor(1, 1);

    // 右键菜单项
    customPlotHelper->onContextMenu = [=](const QCPAxisRect* axisRect, bool& allow){
        if (axisRect == timeCountsAxisRect || axisRect == spectrumAxisRect)
            allow = true;
        else
            allow = false;
    };

    connect(customPlotHelper, &QCustomPlotHelper::selectRangeChanged, this, [=](const QCPAxisRect *axisRect, const QCPRange& range){
        if (axisRect == spectrumAxisRect && range.size() >= 1){
            int minPeak, maxPeak;
            int channels = ui->comboBox_channels->currentText().toInt();
            minPeak = range.lower * 16384 / channels;
            maxPeak = range.upper * 16384 / channels;
            onCpsStatistics(minPeak, maxPeak);
        }

        else if (axisRect == timeCountsAxisRect && range.size() >= 1){
            Q_UNUSED(range);

            QCPColorMap *colorMap = qobject_cast<QCPColorMap*>(customPlot->plottable("colorMap"));
            colorMap->data()->clear();
            colorMap->data()->setSize(range.size(), 18);
            colorMap->data()->setRange(range, QCPRange(0, 18));//限制坐标轴显示范围
            colorMap->data()->setKeyRange(range);//限制坐标轴显示范围

            QCPBars *cpBars = qobject_cast<QCPBars*>(customPlot->plottable("cpBars"));

            QVector<double> keys, values;
            QVector<QPen> barPen;
            QVector<QBrush> barBrush;
            double yMax = 0;
            values.resize(18);
            for (int channel=0; channel<18; ++channel){
                QCPGraph *graph = nullptr;
                if (channel < 11)
                    graph = customPlot->graph(timeCountsAxisRect, QStringLiteral("HC %1").arg(channel+1));
                else
                    graph = customPlot->graph(timeCountsAxisRect, QStringLiteral("VC %1").arg(channel+1));

                keys << (double)(channel + 1);
                barPen << QPen(Qt::black );
                barBrush << QBrush(mGraphisColor[channel]);

                for (int i=0; i<graph->data()->size(); ++i){
                    if (graph->data()->at(i)->key>=range.lower && graph->data()->at(i)->key<=range.upper){
                        values[channel] += (double)graph->data()->at(i)->value;
                        yMax = qMax((double)yMax, (double)values[channel]);

                        int keyIndex = graph->data()->at(i)->key-range.lower;
                        int valueIndex = channel;
                        double z = graph->data()->at(i)->value;
                        colorMap->data()->setCell(keyIndex, valueIndex, z);
                    }
                }
            }

            // 时间+通道+计数率热度图
            colorMap->rescaleDataRange(true);
            colorMap->rescaleValueAxis(true, true);
            colorMap->rescaleKeyAxis(true);

            QCPAxisRect *timeChannelCountsAxisRect = customPlot->findChild<QCPAxisRect*>("timeChannelCountsAxisRect");
            timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->setRange(range);
            timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atBottom)->rescale(false);

            cpBars->setData(keys, values, barPen, barBrush);
            cpBars->valueAxis()->setRange(QCPRange(0,  yMax * 1.1));
            customPlot->replot(QCustomPlot::rpQueuedReplot);
        }
    });

    // 绘制数据
    if (0){
        // 生成18组高斯峰数据
        const int numGroups = 18;
        const int pointsPerGroup = 20;
        const double xMin = 0, xMax = 20;
        const double step = (xMax - xMin) / (pointsPerGroup - 1);  // 20个点的步长

        QCPColorMap *colorMap = qobject_cast<QCPColorMap*>(customPlot->plottable("colorMap"));
        //colorMap->data()->setSize(pointsPerGroup, numGroups);
        //colorMap->data()->setRange(QCPRange(0, pointsPerGroup*100), QCPRange(0.5, numGroups + 0.5));

        QCPBars *cpBars = qobject_cast<QCPBars*>(customPlot->plottable("cpBars"));

        QVector<double> keys;
        QVector<double> values;
        QVector<QPen> barPen;
        QVector<QBrush> barBrush;

        // 高斯函数：y = A * exp(-(x-μ)²/(2σ²))
         auto gaussian1 = [=](double x, double amplitude, double mean, double sigma)->double {
            return amplitude * exp(-pow(x - mean, 2) / (2 * pow(sigma, 2)));
        };

        // 高斯函数：y = A * exp(-(x-μ)²/(2σ²)) + baseline（基线偏移）
        auto gaussian = [=](double x, double amplitude, double mean, double sigma, double baseline)->double {
            return amplitude * exp(-pow(x - mean, 2) / (2 * pow(sigma, 2))) + baseline;
        };

        for (int group = 0; group < numGroups; ++group) {
            // 随机生成当前组的高斯参数
            double amplitude = QRandomGenerator::global()->bounded(600, 1200);  // 峰值100~1200
            double mean = QRandomGenerator::global()->bounded(6, 12);          // 中心位置2~18
            double sigma = (double)QRandomGenerator::global()->bounded(5, 20)/10;          // 宽度0.5~2.0
            double baseline = group * 20.0;  // 垂直偏移：每组间隔100（可调整）

            keys << (double)(group + 1);
            values << 0;
            barPen << QPen(Qt::black );
            barBrush << QBrush(mGraphisColor[group]);

            // 计算当前组的x和y数据
            QVector<double> xData, yData;
            for (int i = 0; i < pointsPerGroup; ++i) {
                double key = xMin + i * step;
                double value = gaussian(key, amplitude, mean, sigma, baseline);
                xData.append(key*100);
                yData.append(value);

//                double x, y;
//                colorMap->data()->cellToCoord(i, group, &x, &y);
//                //colorMap->data()->setCell(i, group, value);
//                colorMap->data()->setCell(0, 0, 60);
//                colorMap->data()->setCell(1, 1, 80);
                values[group] += value;
            }           

            // 时间+计数率曲线
            QCPGraph *graph = nullptr;
            if (group < 11)
                graph = customPlot->graph(timeCountsAxisRect, QStringLiteral("HC %1").arg(group+1));
            else
                graph = customPlot->graph(timeCountsAxisRect, QStringLiteral("VC %1").arg(group+1));
            graph->setData(xData, yData);
        }

        //colorMap->data()->setSize(1000, 18);
        colorMap->data()->setSize(50, 18);
        colorMap->data()->setRange(QCPRange(0, 50), QCPRange(0, 18));//限制坐标轴显示范围
        for (int x=0; x<50; ++x)
            for (int y=0; y<18; ++y)
                colorMap->data()->setCell(x, y, qCos(x)+qSin(y));

        colorMap->setInterpolate(true);// 颜色平滑过度
        colorMap->setTightBoundary(true);//设置最外层的数据行和列是否被裁剪到指定的键值范围

        // 时间+通道+计数率热度图
        colorMap->rescaleDataRange(true);
        colorMap->rescaleValueAxis(true, true);
        colorMap->rescaleKeyAxis(true);

        // 通道+计数率总和柱状图
        cpBars->setData(keys, values, barPen, barBrush);
    }

    customPlot->replot(QCustomPlot::rpQueuedReplot);

    QVBoxLayout* vLayout = new QVBoxLayout(ui->pageInfoWidget_cps);
    vLayout->setMargin(0);
    vLayout->addWidget(customPlot);
    ui->pageInfoWidget_cps->setLayout(vLayout);

    mCpsPlot = customPlot;
    emit mCpsPlot->afterLayout();
}

void CpsStatisticsWindow::onCpsPlot(QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>> mapPairs)
{
    QCPAxisRect *timeCountsAxisRect = mCpsPlot->findChild<QCPAxisRect*>("timeCountsAxisRect");
    {
        // 设置坐标轴范围
        QCPAxis *keyAxis = timeCountsAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = timeCountsAxisRect->axis(QCPAxis::AxisType::atLeft);
        keyAxis->setRange(QCPRange(ui->spinBox_startT_3->value(), ui->spinBox_endT_3->value()));
    }
    QCPAxisRect *timeChannelCountsAxisRect = mCpsPlot->findChild<QCPAxisRect*>("timeChannelCountsAxisRect");
    {
        // 设置坐标轴范围
        QCPAxis *keyAxis = timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = timeChannelCountsAxisRect->axis(QCPAxis::AxisType::atLeft);
        keyAxis->setRange(QCPRange(ui->spinBox_startT_3->value(), ui->spinBox_endT_3->value()));
    }

    QCPColorMap *colorMap = qobject_cast<QCPColorMap*>(mCpsPlot->plottable("colorMap"));
    QCPBars *cpBars = qobject_cast<QCPBars*>(mCpsPlot->plottable("cpBars"));

    const int channels = 18;
    const int pointsPerGroup = ui->spinBox_endT_3->value();
    int keySize = ui->spinBox_endT_3->value() - ui->spinBox_startT_3->value();
    colorMap->data()->setSize(keySize, channels);// 范围不要超出坐标轴范围，否则坐标轴会被覆盖
    colorMap->data()->setRange(QCPRange(ui->spinBox_startT_3->value()/* + 1*/, ui->spinBox_endT_3->value()), QCPRange(0, channels));

    QVector<double> keys;
    QVector<double> values;
    QVector<QPen> barPen;
    QVector<QBrush> barBrush;
    double yMax = 0;
    double yMax2 = 0;

    for (auto iter = mapPairs.begin(); iter != mapPairs.end(); ++iter){
        int channel = iter.key();
        QMap<quint16/*时刻*/,quint32/*计数率*/> mapPair = iter.value();

        keys << iter.key();
        values << 0;
        barPen << QPen(Qt::black);
        barBrush << QBrush(mGraphisColor[channel-1]);

        // 计算当前组的x和y数据
        QVector<double> xData, yData;
        for (auto iterSub = mapPair.begin(); iterSub != mapPair.end(); ++iterSub){
            xData.append(iterSub.key());
            yData.append(iterSub.value());
            yMax = qMax((double)yMax, (double)iterSub.value());

            int keyIndex = iterSub.key()-ui->spinBox_startT_3->value();
            int valueIndex = channel - 0.5;
            double x, y, z;
            colorMap->data()->cellToCoord(keyIndex, valueIndex, &x, &y);
            z = iterSub.value();
            colorMap->data()->setCell(keyIndex, valueIndex, z);

            values[channel-1] += iterSub.value();
            yMax2 = qMax((double)yMax2, (double)values[channel-1]);
        }

        // 时间+计数率曲线
        QCPGraph *graph = nullptr;
        if (channel <= 11)
            graph = mCpsPlot->graph(timeCountsAxisRect, QStringLiteral("HC %1").arg(channel));
        else
            graph = mCpsPlot->graph(timeCountsAxisRect, QStringLiteral("VC %1").arg(channel));
        graph->setData(xData, yData);
    }

    colorMap->setInterpolate(true);// 颜色平滑过度
    colorMap->setTightBoundary(true);//设置最外层的数据行和列是否被裁剪到指定的键值范围

    // 时间+通道+计数率热度图
    colorMap->rescaleDataRange(true);
    colorMap->rescaleValueAxis(true, true);
    colorMap->rescaleKeyAxis(true);

    // 时间+通道+计数率热度图
    colorMap->rescaleDataRange();

    // 通道+计数率总和柱状图
    cpBars->setData(keys, values, barPen, barBrush);

    // 队列刷新
    timeCountsAxisRect->axis(QCPAxis::AxisType::atLeft)->setRange(QCPRange(0,  yMax * 1.1));
    cpBars->valueAxis()->setRange(QCPRange(0,  yMax2 * 1.1));
    mCpsPlot->replot(QCustomPlot::rpQueuedReplot);
}

#include <QtMath>
void CpsStatisticsWindow::onSpectrumPlot(QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>> mapPairs)
{
    QCPAxisRect *spectrumAxisRect = mCpsPlot->findChild<QCPAxisRect*>("spectrumAxisRect");
    {
        // 设置坐标轴范围
        QCPAxis *keyAxis = spectrumAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = spectrumAxisRect->axis(QCPAxis::AxisType::atLeft);
        keyAxis->setRange(QCPRange(0, mapPairs[1].count()));
    }

    QVector<double> keys;
    QVector<double> values;
    QVector<QPen> barPen;
    QVector<QBrush> barBrush;
    double yMax = 0;

    for (auto iter = mapPairs.begin(); iter != mapPairs.end(); ++iter){
        int channel = iter.key();
        QMap<quint16/*时刻*/,quint32/*计数率*/> mapPair = iter.value();

        keys << iter.key();
        values << 0;
        barPen << QPen(Qt::black);
        barBrush << QBrush(mGraphisColor[channel-1]);

        // 计算当前组的x和y数据
        QVector<double> xData, yData;
        for (auto iterSub = mapPair.begin(); iterSub != mapPair.end(); ++iterSub){
            xData.append(iterSub.key());
            yData.append(iterSub.value());
            yMax = qMax((double)yMax, (double)iterSub.value());
        }

        // 时间+计数率曲线
        QCPGraph *graph = nullptr;
        if (channel <= 11)
            graph = mCpsPlot->graph(spectrumAxisRect, QStringLiteral("HC %1").arg(channel));
        else
            graph = mCpsPlot->graph(spectrumAxisRect, QStringLiteral("VC %1").arg(channel));
        graph->setData(xData, yData);
    }

    // 队列刷新
    spectrumAxisRect->axis(QCPAxis::AxisType::atLeft)->setRange(QCPRange(0,  yMax * 1.1));
    mCpsPlot->replot(QCustomPlot::rpQueuedReplot);
}

QString CpsStatisticsWindow::joinFilename(const int& cameraIndex)
{
    QString filename;

    switch (cameraIndex){
    case 1: filename = "1A"; break;
    case 2: filename = "1A"; break;
    case 3: filename = "1A"; break;
    case 4: filename = "1B"; break;
    case 5: filename = "1B"; break;
    case 6: filename = "1B"; break;

    case 7: filename = "2A"; break;
    case 8: filename = "2A"; break;
    case 9: filename = "2A"; break;
    case 10: filename = "2B"; break;
    case 11: filename = "2B"; break;
    case 12: filename = "2B"; break;

    case 13: filename = "3A"; break;
    case 14: filename = "3A"; break;
    case 15: filename = "3A"; break;
    case 16: filename = "3B"; break;
    case 17: filename = "3B"; break;
    case 18: filename = "3B"; break;
    }

    return filename;
}

void CpsStatisticsWindow::on_comboBox_h5Files_currentTextChanged(const QString &arg1)
{
    QString filePath = mFileDir + "/" + arg1 + ".h5";
    quint32 packerStartTime, packerEndTime, threshold;
    if (QFileInfo(filePath).exists() && DataAnalysisWorker::readWaveformHeadFromHDF5(filePath, packerStartTime, packerEndTime, threshold))
    {
        ui->line_measure_startT_3->setText(QString::number(packerStartTime));
        ui->line_measure_endT_3->setText(QString::number(packerEndTime));
        ui->spinBox_threshold_3->setValue(threshold);

        ui->spinBox_startT_3->setValue(packerStartTime);
        ui->spinBox_endT_3->setValue(packerEndTime);
    }
}


void CpsStatisticsWindow::on_action_waveform_triggered()
{
    ui->centralStackedWidget->setCurrentWidget(ui->pageInfoWidget_waveform);
    ui->optionStackedWidget->setCurrentWidget(ui->page_waveform);
}


void CpsStatisticsWindow::on_action_process_triggered()
{
    ui->centralStackedWidget->setCurrentWidget(ui->pageInfoWidget_process);
    ui->optionStackedWidget->setCurrentWidget(ui->page_process);
}


void CpsStatisticsWindow::on_action_cps_triggered()
{
    ui->centralStackedWidget->setCurrentWidget(ui->pageInfoWidget_cps);
    ui->optionStackedWidget->setCurrentWidget(ui->page_cps);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
void CpsStatisticsWindow::doWaveformPlot()
{
    // // 计数率统计
    // //提取有效波形参数
    // int timeLength = ui->spinBox_time1->value(); // 默认值 1ms
    // int timeStart = ui->spinBox_startT->value(); // 开始时刻
    // int timeStop = ui->spinBox_endT->value(); // 截止时刻

    // // 查找目录下的h5文件
    // QString h5FilePath = mFileDir + "/waveform_data.h5";
    // QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>> mapPairs;
    // if (QFileInfo::exists(h5FilePath) && !mPCIeCommSdk.analyzeHistoryCpsData(timeLength, timeStart, timeStop, h5FilePath, [&](QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>> mapPair){
    //         for (auto iter = mapPair.begin(); iter!=mapPair.end(); ++iter){
    //             mapPairs[iter.key()] = iter.value();
    //         }
    //     }))
    // {
    //     QMessageBox::information(this, tr("提示" ), tr("文件格式错误，加载失败！"));
    // }

    //emit reportWavePlot(mapPairs);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CpsStatisticsWindow::onDataProcess
QString CpsStatisticsWindow::humanReadableSize(qint64 bytes)
{
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    const double GB = MB * 1024.0;

    if (bytes >= GB) return QString::asprintf("%.2f GB", bytes / GB);
    if (bytes >= MB) return QString::asprintf("%.2f MB", bytes / MB);
    if (bytes >= KB) return QString::asprintf("%.2f KB", bytes / KB);
    return QString("%1 B").arg(bytes);
}

void CpsStatisticsWindow::onDataProcess()
{
    // 获取数据目录路径
    QString dataDir = ui->textBrowser_filepath->toPlainText();
    if (dataDir.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("数据目录路径为空"), QtWarningMsg);
        return;
    }

    QString outfileName = ui->lineEdit_outputFile->text().trimmed();
    //根据用户输入，对文件名后缀进行追加.h5，如果存在.h5则不追加后缀，否则追加后缀
    if (outfileName.isEmpty()) {
        emit doWriteLog("输出文件名不能为空", QtWarningMsg);
        return;
    }

    // 检查是否已有.h5后缀（区分大小写）
    if (!outfileName.endsWith(".h5", Qt::CaseSensitive)) {
        outfileName += ".h5";
    }

    emit doWriteLog("========================================", QtInfoMsg);
    emit doWriteLog("开始数据压缩分析", QtInfoMsg);
    emit doWriteLog(QString("数据目录: %1").arg(dataDir), QtInfoMsg);

    // 创建HDF5文件路径（在数据目录下）
    QString hdf5FilePath = QDir(dataDir).filePath(outfileName);
    emit doWriteLog(QString("输出文件: %1").arg(outfileName), QtInfoMsg);

    //检查文件是否存在，如果存在则询问用户是否删除
    if (QFileInfo::exists(hdf5FilePath)) {
        //弹窗提醒用户hdf5FilePath已存在，给出选项是否删除
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "警告",
            QString("文件 \"%1\" 已存在，是否删除并重新生成？").arg(hdf5FilePath),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
            );
        if (reply == QMessageBox::No) {
            emit doWriteLog("用户取消操作", QtInfoMsg);
            return;
        }
        QFile::remove(hdf5FilePath);
        emit doWriteLog(QString("已删除已存在的文件: %1").arg(hdf5FilePath), QtInfoMsg);
    }

    mProgressIndicator->startAnimation();

    // 创建并启动工作线程
    // 如果已有线程在运行，先停止并清理
    if (mAnalysisThread) {
        if (mAnalysisThread->isRunning()) {
            // 如果有worker，先取消分析
            if (mAnalysisWorker) {
                mAnalysisWorker->cancelAnalysis();
                disconnect(mAnalysisWorker, nullptr, this, nullptr);
            }
            mAnalysisThread->quit();
            mAnalysisThread->wait(2000); // 等待最多2秒
            if (mAnalysisThread->isRunning()) {
                mAnalysisThread->terminate();
                mAnalysisThread->wait();
            }
        }
        // 清理旧对象
        disconnect(mAnalysisThread, nullptr, nullptr, nullptr);
        if (mAnalysisWorker) {
            mAnalysisWorker->deleteLater();
            mAnalysisWorker = nullptr;
        }
        mAnalysisThread->deleteLater();
        mAnalysisThread = nullptr;
    }

    mAnalysisThread = new QThread(this);
    mAnalysisWorker = new DataAnalysisWorker();

    // 设置参数
    int timePerFile = 40;// 每个文件40ms
    int startTime = ui->spinBox_startT_2->value();
    int endTime = ui->spinBox_endT_2->value();
    int threshold = ui->spinBox_threshold_2->value();

    mAnalysisWorker->setParameters(dataDir, mfileList, outfileName, threshold,
                                   timePerFile, startTime, endTime);

    // 将worker移动到工作线程
    mAnalysisWorker->moveToThread(mAnalysisThread);

    // 连接信号和槽（使用QueuedConnection确保线程安全）
    connect(mAnalysisThread, &QThread::started, mAnalysisWorker, &DataAnalysisWorker::startAnalysis);
    connect(mAnalysisWorker, &DataAnalysisWorker::logMessage,
            this, &CpsStatisticsWindow::onAnalysisLogMessage, Qt::QueuedConnection);
    connect(mAnalysisWorker, &DataAnalysisWorker::progressUpdated,
            this, &CpsStatisticsWindow::onAnalysisProgress, Qt::QueuedConnection);
    connect(mAnalysisWorker, &DataAnalysisWorker::analysisFinished,
            this, &CpsStatisticsWindow::onAnalysisFinished, Qt::QueuedConnection);
    connect(mAnalysisWorker, &DataAnalysisWorker::analysisError,
            this, &CpsStatisticsWindow::onAnalysisError, Qt::QueuedConnection);

    // 不在这里设置自动清理，由onAnalysisFinished统一处理

    // 启动工作线程
    mAnalysisThread->start();
}

void CpsStatisticsWindow::onAnalysisLogMessage(const QString& msg, QtMsgType msgType)
{
    // 这个槽函数在工作线程中通过信号调用，会自动切换到UI线程执行
    emit doWriteLog(msg, msgType);
}

void CpsStatisticsWindow::onAnalysisProgress(int current, int total)
{
    // 更新进度条（在UI线程中执行）
    if (total > 0) {
        ui->progressBar->setMaximum(total);
        ui->progressBar->setValue(current);
    }
}

void CpsStatisticsWindow::onAnalysisFinished(bool success, const QString& message)
{
    // 恢复UI状态
    ui->toolButton_process->setEnabled(true);

    if (success) {
        emit doWriteLog("数据压缩分析完成", QtInfoMsg);

        // 更新文件大小显示
        QString dataDir = ui->textBrowser_filepath->toPlainText();
        QString outfileName = ui->lineEdit_outputFile->text().trimmed();
        if (!outfileName.endsWith(".h5", Qt::CaseSensitive)) {
            outfileName += ".h5";
        }
        QString hdf5FilePath = QDir(dataDir).filePath(outfileName);

        QFileInfo fi(hdf5FilePath);
        if (fi.exists()) {
            qint64 sizeBytes = fi.size();
            ui->lineEdit_fileSize->setText(humanReadableSize(sizeBytes));
            emit doWriteLog(QString("压缩后文件大小: %1").arg(humanReadableSize(sizeBytes)), QtInfoMsg);
        }

        emit doWriteLog("========================================", QtInfoMsg);
    } else {
        emit doWriteLog(QString("数据分析失败: %1").arg(message), QtCriticalMsg);
        QMessageBox::critical(this, "错误", QString("数据分析失败: %1").arg(message));
    }

    // Worker已经完成工作，现在停止线程并清理
    // 注意：这里必须在UI线程中执行，确保线程安全
    if (mAnalysisThread) {
        // 先断开worker的信号连接，避免后续信号触发
        if (mAnalysisWorker) {
            disconnect(mAnalysisWorker, nullptr, this, nullptr);
        }

        // 停止线程
        if (mAnalysisThread->isRunning()) {
            mAnalysisThread->quit();
            // 等待线程结束（最多等待3秒）
            if (!mAnalysisThread->wait(3000)) {
                // 如果等待超时，强制终止
                emit doWriteLog("警告：线程未能正常结束，强制终止", QtWarningMsg);
                mAnalysisThread->terminate();
                mAnalysisThread->wait();
            }
        }

        // 断开线程的所有连接
        disconnect(mAnalysisThread, nullptr, nullptr, nullptr);

        // 清理worker对象（线程已停止，可以安全删除）
        if (mAnalysisWorker) {
            mAnalysisWorker->deleteLater();
            mAnalysisWorker = nullptr;
        }

        // 清理线程对象
        mAnalysisThread->deleteLater();
        mAnalysisThread = nullptr;
    }

    mProgressIndicator->stopAnimation();

    // 重新加载h5文件列表
    // 仅过滤 .h5 文件
    QStringList filters;
    filters << "*.h5";
    QFileInfoList fileinfoList = QDir(mFileDir).entryInfoList(
        filters,
        QDir::Files | QDir::NoSymLinks,
        QDir::Unsorted
        );

    ui->comboBox_h5Files->clear();
    for (auto item : fileinfoList){
        ui->comboBox_h5Files->addItem(item.baseName());
    }
    emit ui->comboBox_h5Files->currentIndexChanged(0);
}

void CpsStatisticsWindow::onAnalysisError(const QString& error)
{
    emit doWriteLog(QString("错误: %1").arg(error), QtCriticalMsg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
void CpsStatisticsWindow::onCpsStatistics(int minPeak, int maxPeak)
{
    mProgressIndicator->startAnimation();
    // 计数率统计
    //提取有效波形参数
    int timeWidth = ui->spinBox_time1->value(); // 默认值 1ms
    int timeStart = ui->spinBox_startT_3->value(); // 开始时刻
    int timeStop = ui->spinBox_endT_3->value(); // 截止时刻
    int channels = ui->comboBox_channels->currentText().toInt();

    std::thread producer([=]{
        // 查找目录下的h5文件
	    QString h5FilePath = mFileDir + "/waveform_data.h5";
	    QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>> cpsMapPairs;
	    QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>> spectrumMapPairs;
	    if (QFileInfo::exists(h5FilePath) &&
	        !mPCIeCommSdk.analyzeHistoryCpsData(channels,
	            timeWidth,
	            timeStart,
	            timeStop,
	            h5FilePath,
	            [&](QMap<quint8/*通道号*/, QMap<quint16/*时刻*/,quint32/*计数率*/>> cpsMapPair, QMap<quint8/*通道号*/, QMap<quint16/*道址*/,quint32/*计数率*/>> spectrumMapPair){
	            for (auto iter = cpsMapPair.begin(); iter!=cpsMapPair.end(); ++iter){
	                cpsMapPairs[iter.key()] = iter.value();
	            }
	
	            for (auto iter = spectrumMapPair.begin(); iter!=spectrumMapPair.end(); ++iter){
	                spectrumMapPairs[iter.key()] = iter.value();
	            }
	        }, minPeak, maxPeak))
	    {
	        mProgressIndicator->stopAnimation();
	        QMessageBox::information(this, tr("提示" ), tr("文件格式错误，加载失败！"));
	        return;
	    }

        // 在std::thread中，拿到接收对象的指针后，通过QMetaObject invokeMethod投递，否则槽函数无法响应
        QMetaObject::invokeMethod(this, [=](){
		    emit doCpsPlot(cpsMapPairs);
		
		    if (0==minPeak && 16384==maxPeak)//如果是选择能谱范围就不要重新刷新能谱图了
		        emit doSpectrumPlot(spectrumMapPairs);
		
		    mProgressIndicator->stopAnimation();
        }, Qt::QueuedConnection);
    });
    producer.detach();
}
