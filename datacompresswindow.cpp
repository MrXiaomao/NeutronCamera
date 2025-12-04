#include "datacompresswindow.h"
#include "ui_datacompresswindow.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QRandomGenerator>
#include <QMap>
#include <QFileInfo>
#include <QDir>
#include "globalsettings.h"

#include <array>
#include <QVector>
#include <cstring>

DataCompressWindow::DataCompressWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::DataCompressWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper*>(parent))
{
    ui->setupUi(this);

    ui->tableWidget_file->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    ui->tableWidget_file->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    ui->toolButton_start->setDefaultAction(ui->action_analyze);
    ui->toolButton_start->setText(tr("开始压缩"));
    ui->toolButton_start->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

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
        QActionGroup *themeActionGroup = new QActionGroup(this);
        ui->action_lightTheme->setActionGroup(themeActionGroup);
        ui->action_darkTheme->setActionGroup(themeActionGroup);
        ui->action_lightTheme->setChecked(!mIsDarkTheme);
        ui->action_darkTheme->setChecked(mIsDarkTheme);
    }

    QStringList args = QCoreApplication::arguments();
    this->setWindowTitle(QApplication::applicationName()+" - "+APP_VERSION + " [" + args[4] + "]");

    connect(this, SIGNAL(reporWriteLog(const QString&,QtMsgType)), this, SLOT(replyWriteLog(const QString&,QtMsgType)));

    QTimer::singleShot(0, this, [&](){
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
    });

    QTimer::singleShot(0, this, [&](){
        if(mainWindow) {
            mainWindow->fixMenuBarWidth();
        }
    });

    // 连接时间范围验证信号
    connect(ui->spinBox_startT, &QSpinBox::editingFinished, this, &DataCompressWindow::validateTimeRange);
    connect(ui->spinBox_endT, &QSpinBox::editingFinished, this, &DataCompressWindow::validateTimeRange);
    
    // 当最大测量时间改变时，更新 spinBox 的最大值
    connect(ui->line_measure_endT, &QLineEdit::textChanged, this, [this](const QString& text) {
        bool ok;
        int maxTime = text.toInt(&ok);
        if (ok && maxTime > 0) {
            ui->spinBox_startT->setMaximum(maxTime);
            ui->spinBox_endT->setMaximum(maxTime);
            // 验证当前值
            validateTimeRange();
        }
    });
}

DataCompressWindow::~DataCompressWindow()
{
    delete ui;
}

void DataCompressWindow::closeEvent(QCloseEvent *event) {
    if ( QMessageBox::Yes == QMessageBox::question(this, tr("系统退出提示"), tr("确定要退出软件系统吗？"),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes))
        event->accept();
    else
        event->ignore();
}

void DataCompressWindow::replyWriteLog(const QString &msg, QtMsgType msgType/* = QtDebugMsg*/)
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

bool DataCompressWindow::loadOfflineFilename(const QString& filename)
{
    ui->textBrowser_filepath->setText(filename);
    return true;
}

void DataCompressWindow::startAnalyze()
{

}

void DataCompressWindow::on_pushButton_test_clicked()
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

// 读取波形数据，这里是有效波形
// 读取 wave_CH1.h5 中的数据集 data（行 = 脉冲数, 列 = 采样点数）
// 返回 QVector<QVector<float>>，尺寸为 [numPulses x numSamples] = [35679 x 512]
QVector<QVector<qint16>> DataCompressWindow::readWave(const std::string &fileName,
                                                 const std::string &dsetName)
{
    QVector<QVector<qint16>> wave_CH1;

    try {
        H5::H5File file(fileName, H5F_ACC_RDONLY);
        H5::DataSet dataset = file.openDataSet(dsetName);
        H5::DataSpace dataspace = dataset.getSpace();

        const int RANK = dataspace.getSimpleExtentNdims();
        if (RANK != 2) {
            return {};
        }

        hsize_t dims[2];
        dataspace.getSimpleExtentDims(dims, nullptr);
        // 约定：dims[0] = 脉冲数（35679），dims[1] = 采样点数（512）
        hsize_t numPulses  = dims[0];
        hsize_t numSamples = dims[1];

        if (numSamples != 512) {
            return {};
        }

        // 先读到一维 buffer（int16）
        std::vector<short> buffer(numPulses * numSamples);
        dataset.read(buffer.data(), H5::PredType::NATIVE_SHORT);

        // 填到 QVector<QVector<float>>：wave_CH1[pulse][sample]
        wave_CH1.resize(static_cast<int>(numPulses));
        for (int p = 0; p < static_cast<int>(numPulses); ++p) {
            wave_CH1[p].resize(static_cast<int>(numSamples));
            for (int s = 0; s < static_cast<int>(numSamples); ++s) {
                short v = buffer[p * numSamples + s];      // 行主序：第 p 行第 s 列
                wave_CH1[p][s] = v;
            }
        }
    }
    catch (const H5::FileIException &e) {
        e.printErrorStack();
        return {};
    }
    catch (const H5::DataSetIException &e) {
        e.printErrorStack();
        return {};
    }
    catch (const H5::DataSpaceIException &e) {
        e.printErrorStack();
        return {};
    }

    return wave_CH1;
}

void DataCompressWindow::on_pushButton_startUpload_clicked()
{
    if (ui->lineEdit_host->text().isEmpty()){
        QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("请填写远程数据库信息！"));
        return;
    }

    // 读取 HDF5 中的 N x 512 波形数据
    QString outfileName = ui->lineEdit_outputFile->text().trimmed();
    // 检查是否已有.h5后缀（区分大小写）
    if (!outfileName.endsWith(".h5", Qt::CaseSensitive)) {
        outfileName += ".h5";
    }
    if (!QFileInfo::exists(outfileName)){
        QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("压缩文件不存在！"));
        return;
    }

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
        QString errorMsg = QString("Failed to create database: %1").arg(query.lastError().text());
        replyWriteLog(errorMsg, QtCriticalMsg);
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
                  "shotNum char(5),"
                  "timestamp datetime,";
    for (int i=1; i<=512; ++i){
        if (i==512)
            sql += QString("data%1 smallint)").arg(i);
        else
            sql += QString("data%1 smallint,").arg(i);
    }
    replyWriteLog(QString("创建数据表SQL: %1").arg(sql), QtDebugMsg);
    if (!query.exec(sql)){
        db.close();
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据表格创建失败！") + query.lastError().text());
        return;
    }

    QVector<QVector<qint16>> wave_CH1 = readWave(outfileName.toStdString(), "data");
    ui->pushButton_startUpload->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->progressBar->setMaximum(wave_CH1.size());

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

        //批量插入，效率高
        QVariantList shotNumList;
        QVariantList timestampList;
        QVariantList dataList[512];

        // 单次最大插入512条记录，这里每次插入100条记录吧
        int count = 0;
        for (int i=0; i<wave_CH1.size(); ++i){
            shotNumList << mShotNum;
            timestampList << timestamp;

            for (int j=0; j<512; ++j){
                dataList[j] << (qint16)wave_CH1[i][j];
                QApplication::processEvents();
            }

            if (++count == 100){
                query.prepare(sql);
                query.bindValue(":shotNum", shotNumList);
                query.bindValue(":timestamp", timestampList);
                for (int i=0; i<512; ++i){
                    query.bindValue(QString(":data%1").arg(i+1), dataList[i]);
                }

                if (!query.execBatch()){
                    QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据上传失败！") + query.lastError().text());
                    db.close();
                    ui->pushButton_startUpload->setEnabled(true);
                    return;
                }

                ui->progressBar->setValue(i);
                count = 0;
                shotNumList.clear();
                timestampList.clear();
                for (int j=0; j<512; ++j)
                    dataList[j].clear();
            }
        }

        if (count != 0){
            query.prepare(sql);
            query.bindValue(":shotNum", shotNumList);
            query.bindValue(":timestamp", timestampList);
            for (int i=0; i<512; ++i){
                query.bindValue(QString(":data%1").arg(i+1), dataList[i]);
            }

            //执行预处理命令
            if (!query.execBatch()){
                QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据上传失败！") + query.lastError().text());
                db.close();
                ui->pushButton_startUpload->setEnabled(true);
                return;
            }
        }

        ui->progressBar->setValue(100);
        db.close();
        QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("数据上传完成！"));
        ui->pushButton_startUpload->setEnabled(true);
        return;
    }
}

void DataCompressWindow::on_action_choseDir_triggered()
{
    GlobalSettings settings;
    QString lastPath = settings.value("Global/Offline/LastFileDir", QDir::homePath()).toString();
    QString filePath = QFileDialog::getExistingDirectory(this, tr("选择测量数据存放目录"), lastPath);

    // 目录格式：炮号+日期+[cardIndex]测量数据[packIndex].bin
    if (filePath.isEmpty())
        return;

    settings.setValue("Global/Offline/LastFileDir", filePath);
    ui->textBrowser_filepath->setText(filePath);
    if (!QFileInfo::exists(filePath+"/Settings.ini")){
        QMessageBox::information(this, tr("提示"), tr("路径无效，缺失\"Settings.ini\"文件！"));
        return;
    }
    else {
        GlobalSettings settings(filePath+"/Settings.ini");
        mShotNum = settings.value("Global/ShotNum", "00000").toString();
        emit reporWriteLog("炮号：" + mShotNum);
    }


    //加载目录下所有文件，罗列在表格中，统计给出文件大小
    mfileList = loadRelatedFiles(filePath);
}


QStringList DataCompressWindow::loadRelatedFiles(const QString& dirPath)
{
    // 结果：按名称排序后的 .bin 文件名列表
    QStringList fileList;

    //对目录下的配置文件信息进行解析，里面包含了炮号、测试开始时间、测量时长、以及探测器类型
    QDir dir(dirPath);
    if (!dir.exists()) {
        replyWriteLog(QString("目录不存在: %1").arg(dirPath), QtWarningMsg);
        ui->tableWidget_file->clearContents();
        ui->tableWidget_file->setRowCount(0);
        ui->lineEdit_binCount->setText("0");
        ui->lineEdit_binTotal->setText("0 B");
        return fileList;
    }

    // 仅过滤 .bin 文件，按名称排序
    QStringList filters;
    filters << "*.bin";
    QFileInfoList fileinfoList = dir.entryInfoList(
        filters,
        QDir::Files | QDir::NoSymLinks,   // 只要文件
        QDir::Name | QDir::IgnoreCase     // 名称排序（忽略大小写）
    );

    mfileinfoList = fileinfoList;

    // 统计
    qint64 totalSize = 0;
    int fileCount = fileinfoList.size();
    fileList.reserve(fileCount);
    for (const QFileInfo& fi : fileinfoList) {
        totalSize += fi.size();
        fileList << fi.fileName();  // 已经是排序顺序
    }

    // ==== 填表 ====
    ui->tableWidget_file->setSortingEnabled(false);  // 填表时关闭排序避免抖动
    ui->tableWidget_file->clearContents();
    ui->tableWidget_file->setRowCount(fileCount);

    // 列设置：只需要设一次
    // 例：列0 文件名，列1 大小(bytes)，列2 可读大小，列3 最后修改时间
    if (ui->tableWidget_file->columnCount() != 4) {
        ui->tableWidget_file->setColumnCount(4);
        ui->tableWidget_file->setHorizontalHeaderLabels(
            {"File Name", "Size(bytes)", "Size", "Last Modified"}
            );
    }

    for (int i = 0; i < fileCount; ++i) {
        const QFileInfo& fi = fileinfoList.at(i);

        auto *itemName = new QTableWidgetItem(fi.fileName());
        itemName->setFlags(itemName->flags() ^ Qt::ItemIsEditable);

        auto *itemBytes = new QTableWidgetItem(QString::number(fi.size()));
        itemBytes->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemBytes->setFlags(itemBytes->flags() ^ Qt::ItemIsEditable);

        auto *itemHuman = new QTableWidgetItem(humanReadableSize(fi.size()));
        itemHuman->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemHuman->setFlags(itemHuman->flags() ^ Qt::ItemIsEditable);

        auto *itemTime = new QTableWidgetItem(fi.lastModified().toString("yyyy-MM-dd HH:mm:ss"));
        itemTime->setFlags(itemTime->flags() ^ Qt::ItemIsEditable);

        ui->tableWidget_file->setItem(i, 0, itemName);
        ui->tableWidget_file->setItem(i, 1, itemBytes);
        ui->tableWidget_file->setItem(i, 2, itemHuman);
        ui->tableWidget_file->setItem(i, 3, itemTime);
    }

    // 表头美化（可选）
    ui->tableWidget_file->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidget_file->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableWidget_file->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget_file->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget_file->setAlternatingRowColors(true);

    replyWriteLog(QString("bin文件数量: %1, 总大小: %2").arg(fileCount).arg(humanReadableSize(totalSize)), QtDebugMsg);
    ui->lineEdit_binCount->setText(QString::number(fileCount));
    ui->lineEdit_binTotal->setText(humanReadableSize(totalSize));

    //统计测量时长，选取光纤口1数据来统计
    int count1data = 0;
    for (const QFileInfo& fi : fileList) {
        const QString name = fi.fileName();
        if (name.startsWith("1data", Qt::CaseInsensitive))
            ++count1data;
    }

    int time_per = ui->spinBox_oneFileTime->value(); //单个文件包对应的时间长度，单位ms
    int measureTime = count1data * time_per;

    ui->line_measure_startT->setText("0");
    ui->line_measure_endT->setText(QString::number(measureTime));
    ui->spinBox_endT->setValue(time_per);
    
    // 更新 spinBox 的最大值
    ui->spinBox_startT->setMaximum(measureTime);
    ui->spinBox_endT->setMaximum(measureTime);
    
    // 验证当前值
    validateTimeRange();

    return fileList;
}

// 6根光纤，每根光纤4个通道，每根光纤的数据分别存储为一个文件，
// 也就是每单位时间(50ms)产生6份文件。
// threshold触发阈值，这个对应扣基线后的波形来进行触发阈值判断
/**
 * @brief DataCompressWindow::getValidWave 6根光纤，每根光纤4个通道，每根光纤的数据分别存储为一个文件
 * 也就是每单位时间(50ms)产生6份文件。
 * 先进行基线扣除后再进行阈值判断
 * @param fileList 文件列表
 * @param outfileName 输出文件名
 * @param threshold 触发阈值
 */
void DataCompressWindow::getValidWave(QStringList& fileList, QString outfileName, int threshold/* = 200*/)
{
    replyWriteLog(QString("开始处理波形数据，阈值: %1").arg(threshold), QtInfoMsg);
    
    int time_per = ui->spinBox_oneFileTime->value(); //单个文件包对应的时间长度，单位ms

    // 设置触发阈值前后波形点数
    int pre_points = 20;
    int post_points = 512 - pre_points - 1;

    // 获取数据目录路径
    QString dataDir = ui->textBrowser_filepath->toPlainText();
    if (dataDir.isEmpty()) {
        replyWriteLog("数据目录路径为空", QtWarningMsg);
        return;
    }

    //读取界面的起止时间 
    int startFile = ui->spinBox_startT->value() / time_per + 1;
    int endFile = ui->spinBox_endT->value() / time_per + 1;
    
    replyWriteLog(QString("处理时间范围: %1 ms - %2 ms (文件范围: %3 - %4)").arg(
        ui->spinBox_startT->value()).arg(ui->spinBox_endT->value()).arg(startFile).arg(endFile), QtInfoMsg);

    // 创建HDF5文件路径（在数据目录下）
    QString hdf5FilePath = QDir(dataDir).filePath(outfileName);
    replyWriteLog(QString("输出文件路径: %1").arg(hdf5FilePath), QtInfoMsg);

    //读取文件，提取有效波形
    //6个光纤口，每个光纤口4个通道
    for(int j=1; j<=6; ++j) {
        replyWriteLog(QString("开始处理板卡%1的数据...").arg(j), QtInfoMsg);

        //对每个通道的有效波形数据进行合并
        QVector<std::array<qint16, 512>> wave_ch0_all;
        QVector<std::array<qint16, 512>> wave_ch1_all;
        QVector<std::array<qint16, 512>> wave_ch2_all;
        QVector<std::array<qint16, 512>> wave_ch3_all;

        int totalFiles = endFile - startFile;
        int processedFiles = 0;
        for (int fileID = startFile; fileID < endFile; ++fileID) {
            QString fileName = QString("%1data%2.bin").arg(j).arg(fileID);
            QString filePath = QDir(dataDir).filePath(fileName);
            QVector<qint16> ch0, ch1, ch2, ch3;
            if (!readBin4Ch_fast(filePath, ch0, ch1, ch2, ch3, true)) {
                replyWriteLog(QString("板卡%1 文件%2: 读取失败或文件不存在").arg(j).arg(fileName), QtWarningMsg);
                continue;
            }
            processedFiles++;

            //扣基线，调整数据
            qint16 baseline_ch = calculateBaseline(ch0);
            adjustDataWithBaseline(ch0, baseline_ch, j, 1);
            qint16 baseline_ch2 = calculateBaseline(ch1);
            adjustDataWithBaseline(ch1, baseline_ch2, j, 2);
            qint16 baseline_ch3 = calculateBaseline(ch2);
            adjustDataWithBaseline(ch2, baseline_ch3, j, 3);
            qint16 baseline_ch4 = calculateBaseline(ch3);
            adjustDataWithBaseline(ch3, baseline_ch4, j, 4);

            //提取有效波形
            QVector<std::array<qint16, 512>> wave_ch0 = overThreshold(ch0, 1, threshold, pre_points, post_points);
            QVector<std::array<qint16, 512>> wave_ch1 = overThreshold(ch1, 2, threshold, pre_points, post_points);
            QVector<std::array<qint16, 512>> wave_ch2 = overThreshold(ch2, 3, threshold, pre_points, post_points);
            QVector<std::array<qint16, 512>> wave_ch3 = overThreshold(ch3, 4, threshold, pre_points, post_points);

            // 统计提取到的波形数量（静默处理，只在最后汇总）

            //合并有效波形
            wave_ch0_all.append(wave_ch0);
            wave_ch1_all.append(wave_ch1);
            wave_ch2_all.append(wave_ch2);
            wave_ch3_all.append(wave_ch3);
        }
        
        replyWriteLog(QString("板卡%1: 已处理 %2/%3 个文件").arg(j).arg(processedFiles).arg(totalFiles), QtInfoMsg);

        // 存储有效波形数据到HDF5文件，按板卡分组
        // 每个板卡数据共用一个数据组，即Board1, Board2, ..., Board6
        // 每个板卡组下有4个数据集：wave_ch0, wave_ch1, wave_ch2, wave_ch3
        // 每个数据集维度为 (N, 512)，N是波形数量，数据类型为int16
        replyWriteLog(QString("正在写入板卡%1的波形数据...").arg(j), QtInfoMsg);
        if (!writeWaveformToHDF5(hdf5FilePath, j, wave_ch0_all, wave_ch1_all, wave_ch2_all, wave_ch3_all)) {
            replyWriteLog(QString("写入板卡%1的波形数据失败，请检查文件路径和权限").arg(j), QtCriticalMsg);
        } else {
            replyWriteLog(QString("板卡%1写入成功: 通道0=%2个波形, 通道1=%3个波形, 通道2=%4个波形, 通道3=%5个波形")
                        .arg(j)
                        .arg(wave_ch0_all.size())
                        .arg(wave_ch1_all.size())
                        .arg(wave_ch2_all.size())
                        .arg(wave_ch3_all.size()), QtInfoMsg);
        }
    }

    replyWriteLog(QString("所有波形数据处理完成，已保存到: %1").arg(hdf5FilePath), QtInfoMsg);
}

bool DataCompressWindow::readBin4Ch_fast(const QString& path,
                     QVector<qint16>& ch0,
                     QVector<qint16>& ch1,
                     QVector<qint16>& ch2,
                     QVector<qint16>& ch3,
                     bool littleEndian /*= true*/)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    // const qint64 headBytes = 16;
    // const qint64 tailBytes = 16;

    const qint64 headBytes = 0;
    const qint64 tailBytes = 0;

    const qint64 fileSize = f.size();
    if (fileSize < headBytes + tailBytes) return false;

    const qint64 payloadBytes = fileSize - headBytes - tailBytes;
    if (payloadBytes % 2 != 0) return false;

    const qint64 totalSamples = payloadBytes / 2;
    if (totalSamples % 4 != 0) return false;
    const qint64 samplesPerChannel = totalSamples / 4;

    // 预分配，避免反复扩容
    ch0.resize(samplesPerChannel);
    ch1.resize(samplesPerChannel);
    ch2.resize(samplesPerChannel);
    ch3.resize(samplesPerChannel);

    // 映射有效段：从 headBytes 开始，长度 payloadBytes
    uchar* p = f.map(headBytes, payloadBytes);
    if (!p) return false;

    const quint16* src = reinterpret_cast<const quint16*>(p);

    if (littleEndian) {
        // 逐点解交织：src = [ch3,ch3,ch2,ch2,ch0,ch0,ch1,ch1,...]
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 8) {
            quint16 v0 = src[i + 0];
            quint16 v1 = src[i + 1];
            quint16 v2 = src[i + 2];
            quint16 v3 = src[i + 3];
            quint16 v4 = src[i + 4];
            quint16 v5 = src[i + 5];
            quint16 v6 = src[i + 6];
            quint16 v7 = src[i + 7];

            v0 = qFromLittleEndian(v0); v1 = qFromLittleEndian(v1);
            v2 = qFromLittleEndian(v2); v3 = qFromLittleEndian(v3);
            v4 = qFromLittleEndian(v4); v5 = qFromLittleEndian(v5);
            v6 = qFromLittleEndian(v6); v7 = qFromLittleEndian(v7);

            ch3[j] = static_cast<qint16>(v0)/4;ch3[j+1] = static_cast<qint16>(v1)/4;
            ch2[j] = static_cast<qint16>(v2)/4;ch2[j+1] = static_cast<qint16>(v3)/4;
            ch0[j] = static_cast<qint16>(v4)/4;ch0[j+1] = static_cast<qint16>(v5)/4;
            ch1[j] = static_cast<qint16>(v6)/4;ch1[j+1] = static_cast<qint16>(v7)/4;
        }
    } else {
        // 如果文件是大端序，需要 byteswap
        for (qint64 i = 0, j = 0; j < samplesPerChannel; j +=2, i += 8) {
            quint16 v0 = src[i + 0];
            quint16 v1 = src[i + 1];
            quint16 v2 = src[i + 2];
            quint16 v3 = src[i + 3];
            quint16 v4 = src[i + 4];
            quint16 v5 = src[i + 5];
            quint16 v6 = src[i + 6];
            quint16 v7 = src[i + 7];

            v0 = qFromBigEndian(v0); v1 = qFromBigEndian(v1); 
            v2 = qFromBigEndian(v2); v3 = qFromBigEndian(v3); 
            v4 = qFromBigEndian(v4); v5 = qFromBigEndian(v5); 
            v6 = qFromBigEndian(v6); v7 = qFromBigEndian(v7);
            ch3[j] = static_cast<qint16>(v0)/4; ch3[j+1] = static_cast<qint16>(v1)/4;
            ch2[j] = static_cast<qint16>(v2)/4; ch2[j+1] = static_cast<qint16>(v3)/4;
            ch0[j] = static_cast<qint16>(v4)/4; ch0[j+1] = static_cast<qint16>(v5)/4;
            ch1[j] = static_cast<qint16>(v6)/4; ch1[j+1] = static_cast<qint16>(v7)/4;
        }
    }

    f.unmap(p);
    return true;
}

// 计算基线值：使用直方图方法，找到出现频率最高的值作为基线
qint16 DataCompressWindow::calculateBaseline(const QVector<qint16>& data_ch)
{
    if (data_ch.isEmpty()) {
        return 0;
    }

    // 创建直方图，binwidth=1
    // 使用QMap存储每个值的计数
    QMap<qint16, int> histogram;
    for (qint16 value : data_ch) {
        histogram[value]++;
    }

    // 找到出现频率最高的值
    int maxCount = 0;
    qint16 baseline_ch = 0;
    for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            baseline_ch = it.key();
        }
    }

    return baseline_ch;
}

// 根据基线调整数据：根据板卡编号和通道号对数据进行不同的调整
// boardNum: 板卡编号 1-6，根据编号的奇偶性判断（1,3,5为奇数板卡，2,4,6为偶数板卡）
// ch: 1-4 (通道号)
void DataCompressWindow::adjustDataWithBaseline(QVector<qint16>& data_ch, qint16 baseline_ch, int boardNum, int ch)
{
    if (data_ch.isEmpty()) {
        return;
    }

    // 当前通道中波形信号有两类，部分为负脉冲信号，部分为正脉冲信号
    // ch1和ch2：板卡编号、通道号共同决定调整方式
    // ch3和ch4：通道号决定调整方式，与板卡编号无关
    
    bool isPositive = false; //是否为正脉冲信号
    
    if (ch == 3) {
        // 第3通道：data - baseline
        isPositive = true;
    }
    else if (ch == 4) {
        // 第4通道：baseline - data
        isPositive = false;
    }
    else {
        // ch1和ch2：根据板卡编号的奇偶性和通道号的组合来决定
        // 奇数板卡(1,3,5)ch1 或 偶数板卡(2,4,6)ch2: data - baseline
        // 其他情况: baseline - data
        bool isOddBoard = (boardNum % 2 == 1);  // 判断是否为奇数板卡
        isPositive = (isOddBoard && ch == 1) || (!isOddBoard && ch == 2);
    }
    
    for (int i = 0; i < data_ch.size(); ++i) {
        if (isPositive) {
            data_ch[i] = data_ch[i] - baseline_ch;
        } else {
            data_ch[i] = baseline_ch - data_ch[i];
        }
    }
}

// 提取超过阈值的有效波形数据
QVector<std::array<qint16, 512>> DataCompressWindow::overThreshold(const QVector<qint16>& data, int ch, int threshold, int pre_points, int post_points)
{
    QVector<std::array<qint16, 512>> wave_ch;
    QVector<int> cross_indices;
    constexpr int WAVEFORM_LENGTH = 512;

    // 丢弃可能不完整的包（比如半个波形）
    // 从索引21开始（因为需要检查i-20到i的均值）
    for (int i = 20; i < data.size(); ++i) {
        // 条件：从低于阈值跨越到高于阈值，且前21个点（i-20到i）的均值小于阈值
        if (data[i - 1] <= threshold && data[i] > threshold) {
            // 计算从i-20到i（包括i）的21个点的均值
            qint64 sum = 0;
            for (int j = i - 20; j <= i; ++j) {
                sum += data[j];
            }
            double mean_value = static_cast<double>(sum) / 21.0;
            
            if (mean_value < threshold) {
                cross_indices.append(i);
            }
        }
    }

    if (cross_indices.isEmpty()) {
        // 注意：这是静态函数，不能直接访问 ui，所以这里保留 qDebug
        // 如果需要日志，应该通过参数传递日志回调函数
        qDebug() << QString("CH%1 don't have valid wave").arg(ch);
        return wave_ch;
    }

    // 提取每个交叉点前后的波形数据，每个波形固定长度为512
    wave_ch.reserve(cross_indices.size());  // 预分配空间
    for (int a = 0; a < cross_indices.size(); ++a) {
        try {
            int cross_idx = cross_indices[a];
            int start_idx = cross_idx - pre_points;
            
            // 检查边界，确保能提取完整的512点
            if (start_idx < 0 || start_idx + WAVEFORM_LENGTH > data.size()) {
                // 注意：这是静态函数，不能直接访问 ui
                qDebug() << QString("skip第%1个数据：边界不足").arg(a + 1);
                continue;
            }
            
            // 创建固定大小的波形数组
            std::array<qint16, WAVEFORM_LENGTH> segment_data;
            
            // 提取波形段（正好512点）
            for (int i = 0; i < WAVEFORM_LENGTH; ++i) {
                segment_data[i] = data[start_idx + i];
            }
            
            wave_ch.append(segment_data);
        }
        catch (...) {
            // 注意：这是静态函数，不能直接访问 ui
            qDebug() << QString("skip第%1个数据").arg(a + 1);
        }
    }

    return wave_ch;
}

// 将波形数据按板卡分组写入HDF5文件
bool DataCompressWindow::writeWaveformToHDF5(const QString& filePath, int boardNum,
                                              const QVector<std::array<qint16, 512>>& wave_ch0,
                                              const QVector<std::array<qint16, 512>>& wave_ch1,
                                              const QVector<std::array<qint16, 512>>& wave_ch2,
                                              const QVector<std::array<qint16, 512>>& wave_ch3)
{
    try {
        // 检查文件是否存在，决定打开方式
        bool fileExists = QFileInfo::exists(filePath);
        H5::H5File file(filePath.toStdString(),
                       fileExists ? H5F_ACC_RDWR : H5F_ACC_TRUNC);

        // 创建或打开板卡组
        QString boardGroupName = QString("Board%1").arg(boardNum);

        H5::Group boardGroup;
        htri_t exists = H5Lexists(file.getId(), boardGroupName.toStdString().c_str(), H5P_DEFAULT);

        if (exists > 0) {
            boardGroup = file.openGroup(boardGroupName.toStdString());
        } else {
            boardGroup = file.createGroup(boardGroupName.toStdString());
        }

        // 辅助函数：写入单个通道的数据集
        auto writeChannel = [&](const QString& datasetName, const QVector<std::array<qint16, 512>>& data) {
             std::string ds = datasetName.toUtf8().constData();

            if (data.isEmpty()) {
                // 如果没有数据，创建一个空数据集
                hsize_t dims[2] = {0, 512};
                H5::DataSpace dataspace(2, dims);
                H5::DataSet dataset = boardGroup.createDataSet(datasetName.toStdString(),
                                                               H5::PredType::NATIVE_INT16,
                                                               dataspace);
                dataset.close();
                return;
            }

            // 准备数据：将 QVector<std::array<qint16, 512>> 转换为连续内存
            hsize_t dims[2] = {static_cast<hsize_t>(data.size()), 512};
            H5::DataSpace dataspace(2, dims);
            QVector<qint16> flatData;
            flatData.resize(static_cast<int>(data.size() * 512));
            int offset = 0;
            for (const auto& wave : data) {
                std::memcpy(flatData.data() + offset,
                            wave.data(),
                            sizeof(qint16) * wave.size());
                offset += static_cast<int>(wave.size());
            }

            // 存在才删（不 open，不抛异常）
            if (H5Lexists(boardGroup.getId(), ds.c_str(), H5P_DEFAULT) > 0) {
                boardGroup.unlink(ds);
            }

            H5::DataSet dataset = boardGroup.createDataSet(
                ds, H5::PredType::NATIVE_INT16, dataspace);

            // 保险起见：显式 memspace/fileSpace rank 一致
            H5::DataSpace fileSpace = dataset.getSpace();
            H5::DataSpace memSpace(2, dims);

            dataset.write(flatData.data(),
                          H5::PredType::NATIVE_INT16,
                          memSpace,
                          fileSpace);

            dataset.close();
        };

        // 写入4个通道的数据
        writeChannel("wave_ch0", wave_ch0);
        writeChannel("wave_ch1", wave_ch1);
        writeChannel("wave_ch2", wave_ch2);
        writeChannel("wave_ch3", wave_ch3);

        boardGroup.close();
        file.close();

        return true;
    } catch (H5::FileIException& error) {
        // 注意：这是静态函数，不能直接访问 ui，异常信息通过返回值或参数传递
        qDebug() << "HDF5 File Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::DataSetIException& error) {
        qDebug() << "HDF5 DataSet Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::DataSpaceIException& error) {
        qDebug() << "HDF5 DataSpace Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (H5::GroupIException& error) {
        qDebug() << "HDF5 Group Exception:" << error.getDetailMsg().c_str();
        return false;
    } catch (...) {
        qDebug() << "Unknown HDF5 Exception";
        return false;
    }
}

QString DataCompressWindow::humanReadableSize(qint64 bytes)
{
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    const double GB = MB * 1024.0;

    if (bytes >= GB) return QString::asprintf("%.2f GB", bytes / GB);
    if (bytes >= MB) return QString::asprintf("%.2f MB", bytes / MB);
    if (bytes >= KB) return QString::asprintf("%.2f KB", bytes / KB);
    return QString("%1 B").arg(bytes);
}

void DataCompressWindow::on_action_analyze_triggered()
{
    replyWriteLog("========================================", QtInfoMsg);
    replyWriteLog("开始数据压缩分析", QtInfoMsg);
    
    // 获取数据目录路径
    QString dataDir = ui->textBrowser_filepath->toPlainText();
    if (dataDir.isEmpty()) {
        replyWriteLog("数据目录路径为空", QtWarningMsg);
        return;
    }
    
    replyWriteLog(QString("数据目录: %1").arg(dataDir), QtInfoMsg);

    //解析文件，提取有效波形数据
    int th = ui->spinBox_threshold->value();
    QString outfileName = ui->lineEdit_outputFile->text().trimmed();
    
    //根据用户输入，对文件名后缀进行追加.h5，如果存在.h5则不追加后缀，否则追加后缀
    if (outfileName.isEmpty()) {
        replyWriteLog("输出文件名不能为空", QtWarningMsg);
        QMessageBox::warning(this, "警告", "输出文件名不能为空！");
        return;
    }
    
    // 检查是否已有.h5后缀（区分大小写）
    if (!outfileName.endsWith(".h5", Qt::CaseSensitive)) {
        outfileName += ".h5";
    }

    // 创建HDF5文件路径（在数据目录下） 
    QString hdf5FilePath = QDir(dataDir).filePath(outfileName);
    replyWriteLog(QString("输出文件: %1").arg(outfileName), QtInfoMsg);

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
            replyWriteLog("用户取消操作", QtInfoMsg);
            return;
        }
        QFile::remove(hdf5FilePath);
        replyWriteLog(QString("已删除已存在的文件: %1").arg(hdf5FilePath), QtInfoMsg);
    }

    //提取有效数据
    getValidWave(mfileList, outfileName, th);
    
    replyWriteLog("数据压缩分析完成", QtInfoMsg);
    //压缩后的文件大小
    QFileInfo fi(hdf5FilePath);
    qint64 sizeBytes = fi.size();
    ui->lineEdit_fileSize->setText(humanReadableSize(sizeBytes));
    replyWriteLog(QString("压缩后文件大小: %1").arg(humanReadableSize(sizeBytes)), QtInfoMsg);
    replyWriteLog("========================================", QtInfoMsg);
}


void DataCompressWindow::on_action_exit_triggered()
{
    mainWindow->close();
}


void DataCompressWindow::on_pushButton_export_clicked()
{

}


void DataCompressWindow::on_action_lightTheme_triggered()
{
    if(!mIsDarkTheme) return;
    mIsDarkTheme = false;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","false");
}


void DataCompressWindow::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","true");
}


void DataCompressWindow::on_action_colorTheme_triggered()
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
}

// 验证时间范围输入
void DataCompressWindow::validateTimeRange()
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
    ui->spinBox_startT->setMaximum(maxTime);
    ui->spinBox_endT->setMaximum(maxTime);
    
    // 获取当前输入值（此时值已经被 spinBox 自动限制在最大值内）
    int startTime = ui->spinBox_startT->value();
    int endTime = ui->spinBox_endT->value();
    
    // 验证 startTime 不能大于 endTime
    if (startTime > endTime) {
        // 如果起始时间大于结束时间，自动调整结束时间
        ui->spinBox_endT->setValue(startTime);
        QMessageBox::warning(this, "输入警告", 
                            QString("起始时间不能大于结束时间，结束时间已自动调整为 %1 ms").arg(startTime));
    }
}

