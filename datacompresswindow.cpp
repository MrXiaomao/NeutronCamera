#include "datacompresswindow.h"
#include "ui_datacompresswindow.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QRandomGenerator>
#include <QMap>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include "globalsettings.h"
#include "qprogressindicator.h"

#include <array>
#include <QVector>
#include <cstring>
#include <QThread>
#include <QMutexLocker>

DataCompressWindow::DataCompressWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::DataCompressWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper*>(parent))
    , mAnalysisThread(nullptr)
    , mAnalysisWorker(nullptr)
{
    ui->setupUi(this);
    applyColorTheme();

    mProgressIndicator = new QProgressIndicator(this);
    ui->tableWidget_file->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    ui->tableWidget_file->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    ui->toolButton_start->setDefaultAction(ui->action_analyze);
    ui->toolButton_start->setText(tr("开始压缩"));
    ui->toolButton_start->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    {
        QActionGroup *themeActionGroup = new QActionGroup(this);
        ui->action_lightTheme->setActionGroup(themeActionGroup);
        ui->action_darkTheme->setActionGroup(themeActionGroup);
        ui->action_lightTheme->setChecked(!mIsDarkTheme);
        ui->action_darkTheme->setChecked(mIsDarkTheme);
    }

//    QStringList args = QCoreApplication::arguments();
//    this->setWindowTitle(QApplication::applicationName()+" - "+APP_VERSION + " [" + args[4] + "]");

    connect(this, SIGNAL(doWriteLog(const QString&,QtMsgType)), this, SLOT(onWriteLog(const QString&,QtMsgType)));

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

DataCompressWindow::~DataCompressWindow()
{
    // 清理工作线程
    if (mAnalysisWorker && mAnalysisThread) {
        mAnalysisWorker->cancelAnalysis();
        mAnalysisThread->quit();
        mAnalysisThread->wait(5000); // 等待最多5秒
        if (mAnalysisThread->isRunning()) {
            mAnalysisThread->terminate();
            mAnalysisThread->wait();
        }
        mAnalysisWorker->deleteLater();
        mAnalysisThread->deleteLater();
    }
    delete ui;
}

void DataCompressWindow::closeEvent(QCloseEvent *event) {
    if ( QMessageBox::Yes == QMessageBox::question(this, tr("系统退出提示"), tr("确定要退出软件系统吗？"),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes))
        event->accept();
    else
        event->ignore();
}

void DataCompressWindow::onWriteLog(const QString &msg, QtMsgType msgType/* = QtDebugMsg*/)
{
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

// 测试连接数据库
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
// 返回 QVector<QVector<float>>，尺寸为 [numPulses x numSamples] = [N x 512]
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
        onWriteLog(errorMsg, QtCriticalMsg);
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
    onWriteLog(QString("创建数据表SQL: %1").arg(sql), QtDebugMsg);
    if (!query.exec(sql)){
        db.close();
        QMessageBox::critical(nullptr, QStringLiteral("提示"), QStringLiteral("数据表格创建失败！") + query.lastError().text());
        return;
    }

    QVector<QVector<qint16>> wave_CH1 = readWave(outfileName.toStdString(), "data");
    ui->pushButton_startUpload->setEnabled(false);
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->setMaximum(wave_CH1.size());

    mProgressIndicator->startAnimation();
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

                ui->progressBar_2->setValue(i);
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

        ui->progressBar_2->setValue(100);
        db.close();
        QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("数据上传完成！"));
        ui->pushButton_startUpload->setEnabled(true);
    }

    mProgressIndicator->stopAnimation();
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
    if (!QFileInfo::exists(filePath+"/device_config.ini")){
        QMessageBox::information(this, tr("提示"), tr("路径无效，缺失\"device_config.ini\"文件！"));
        return;
    }
    else {
        GlobalSettings settings(filePath+"/device_config.ini");
        mShotNum = settings.value("Global/ShotNum", "00000").toString();
        emit doWriteLog("炮号：" + mShotNum);
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
        onWriteLog(QString("目录不存在: %1").arg(dirPath), QtWarningMsg);
        ui->tableWidget_file->clearContents();
        ui->tableWidget_file->setRowCount(0);
        ui->lineEdit_binCount->setText("0");
        ui->lineEdit_binTotal->setText("0 B");
        return fileList;
    }

    // 使用静态函数获取.bin文件列表
    QFileInfoList fileinfoList = getBinFileList(dirPath);
    mfileinfoList = fileinfoList;

    // 使用静态函数统计总大小
    qint64 totalSize = calculateTotalSize(fileinfoList);
    int fileCount = fileinfoList.size();
    
    // 使用静态函数提取文件名列表
    fileList = extractFileNames(fileinfoList);

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

    onWriteLog(QString("bin文件数量: %1, 总大小: %2").arg(fileCount).arg(humanReadableSize(totalSize)), QtDebugMsg);
    ui->lineEdit_binCount->setText(QString::number(fileCount));
    ui->lineEdit_binTotal->setText(humanReadableSize(totalSize));

    //统计测量时长，选取光纤口1数据来统计
    int count1data = countFilesByPrefix(fileList, "1Adata");
    
    int time_per = 40; //单个文件包对应的时间长度，单位ms
    int measureTime = calculateMeasureTime(count1data, time_per);

    // 获取第一个文件名打包序号作为开始时间，如：1Adata27.bin
    QString file_name = fileList.first();
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
        ui->line_measure_startT->setText(QString::number((start_tm-1)*time_per));
        ui->line_measure_endT->setText(QString::number((start_tm-1)*time_per+measureTime));

        ui->spinBox_startT->setMinimum((start_tm-1)*time_per);
        ui->spinBox_endT->setMaximum(start_tm*time_per+measureTime);

        ui->spinBox_startT->setValue((start_tm-1)*time_per);
        ui->spinBox_endT->setValue(start_tm*time_per+measureTime);
    }
    else {
        ui->line_measure_startT->setText("0");
        ui->line_measure_endT->setText(QString::number(measureTime));
        ui->spinBox_startT->setMinimum(0);
        ui->spinBox_endT->setMaximum(measureTime);
    }
//    ui->line_measure_startT->setText("0");
//    ui->line_measure_endT->setText(QString::number(measureTime));
//    ui->spinBox_endT->setValue(time_per);
    
    // 更新 spinBox 的最大值
//    ui->spinBox_startT->setMaximum(measureTime);
//    ui->spinBox_endT->setMaximum(measureTime);
    
    // 验证当前值
    validateTimeRange();

    return fileList;
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

// 从目录获取所有.bin文件列表（按名称排序）
#include <QFileInfoList>
#include <QRegularExpression>
#include <algorithm>
#include <atomic>
#include <thread>
QFileInfoList DataCompressWindow::getBinFileList(const QString& dirPath)
{
    QFileInfoList fileinfoList;
    
    QDir dir(dirPath);
    if (!dir.exists()) {
        return fileinfoList;  // 返回空列表
    }

    // 仅过滤 .bin 文件，按名称排序
    QStringList filters;
    filters << "*.bin";
    fileinfoList = dir.entryInfoList(
        filters,
        QDir::Files | QDir::NoSymLinks,   // 只要文件
        QDir::Unsorted    // 不排序了，后面手动排序
    );

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
    return result;
}

// 计算文件信息列表的总大小
qint64 DataCompressWindow::calculateTotalSize(const QFileInfoList& fileinfoList)
{
    qint64 totalSize = 0;
    for (const QFileInfo& fi : fileinfoList) {
        totalSize += fi.size();
    }
    return totalSize;
}

// 从文件信息列表提取文件名列表
QStringList DataCompressWindow::extractFileNames(const QFileInfoList& fileinfoList)
{
    QStringList fileList;
    fileList.reserve(fileinfoList.size());
    for (const QFileInfo& fi : fileinfoList) {
        fileList << fi.fileName();
    }
    return fileList;
}

// 统计以指定前缀开头的文件数量
int DataCompressWindow::countFilesByPrefix(const QStringList& fileList, const QString& prefix)
{
    int count = 0;
    for (const QString& fileName : fileList) {
        if (fileName.contains(prefix, Qt::CaseInsensitive)) {
            ++count;
        }
    }
    return count;
}

// 计算测量时长
int DataCompressWindow::calculateMeasureTime(int fileCount, int timePerFile)
{
    return fileCount * timePerFile;
}

void DataCompressWindow::on_action_analyze_triggered()
{
    // 如果已有分析在进行中，不重复启动
    if (mAnalysisThread && mAnalysisThread->isRunning()) {
        QMessageBox::information(this, "提示", "数据分析正在进行中，请等待完成。");
        return;
    }
    
    // 获取数据目录路径
    QString dataDir = ui->textBrowser_filepath->toPlainText();
    if (dataDir.isEmpty()) {
        onWriteLog("数据目录路径为空", QtWarningMsg);
        QMessageBox::warning(this, "警告", "数据目录路径为空！");
        return;
    }

    //解析文件，提取有效波形数据
    int threshold = ui->spinBox_threshold->value();
    QString outfileName = ui->lineEdit_outputFile->text().trimmed();
    
    //根据用户输入，对文件名后缀进行追加.h5，如果存在.h5则不追加后缀，否则追加后缀
    if (outfileName.isEmpty()) {
        onWriteLog("输出文件名不能为空", QtWarningMsg);
        QMessageBox::warning(this, "警告", "输出文件名不能为空！");
        return;
    }
    
    // 检查是否已有.h5后缀（区分大小写）
    if (!outfileName.endsWith(".h5", Qt::CaseSensitive)) {
        outfileName += ".h5";
    }

    onWriteLog("========================================", QtInfoMsg);
    onWriteLog("开始数据压缩分析", QtInfoMsg);
    onWriteLog(QString("数据目录: %1").arg(dataDir), QtInfoMsg);

    mProgressIndicator->startAnimation();

    // 创建HDF5文件路径（在数据目录下） 
    QString hdf5FilePath = QDir(dataDir).filePath(outfileName);
    onWriteLog(QString("输出文件: %1").arg(outfileName), QtInfoMsg);

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
            onWriteLog("用户取消操作", QtInfoMsg);
            return;
        }
        QFile::remove(hdf5FilePath);
        onWriteLog(QString("已删除已存在的文件: %1").arg(hdf5FilePath), QtInfoMsg);
    }

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
    int startTime = ui->spinBox_startT->value();
    int endTime = ui->spinBox_endT->value();
    
    mAnalysisWorker->setParameters(dataDir, mfileList, outfileName, threshold,
                                   timePerFile, startTime, endTime);

    // 将worker移动到工作线程
    mAnalysisWorker->moveToThread(mAnalysisThread);

    // 连接信号和槽（使用QueuedConnection确保线程安全）
    connect(mAnalysisThread, &QThread::started, mAnalysisWorker, &DataAnalysisWorker::startAnalysis);
    connect(mAnalysisWorker, &DataAnalysisWorker::logMessage, 
            this, &DataCompressWindow::onAnalysisLogMessage, Qt::QueuedConnection);
    connect(mAnalysisWorker, &DataAnalysisWorker::progressUpdated, 
            this, &DataCompressWindow::onAnalysisProgress, Qt::QueuedConnection);
    connect(mAnalysisWorker, &DataAnalysisWorker::analysisFinished, 
            this, &DataCompressWindow::onAnalysisFinished, Qt::QueuedConnection);
    connect(mAnalysisWorker, &DataAnalysisWorker::analysisError, 
            this, &DataCompressWindow::onAnalysisError, Qt::QueuedConnection);
    
    // 不在这里设置自动清理，由onAnalysisFinished统一处理

    // 禁用开始按钮，防止重复启动
    ui->action_analyze->setEnabled(false);
    ui->toolButton_start->setEnabled(false);
    
    // 初始化进度条
    ui->progressBar->setValue(0);
    ui->progressBar->setMaximum(0); // 初始值设为0，表示不确定

    // 启动工作线程
    mAnalysisThread->start();
}


void DataCompressWindow::on_action_exit_triggered()
{
    mainWindow->close();
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
    applyColorTheme();
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
    applyColorTheme();
}

void DataCompressWindow::applyColorTheme()
{
    if (mIsDarkTheme)
    {
        // 创建一个 QTextCursor
        QTextCursor cursor = ui->textEdit_log->textCursor();
        QTextDocument *document = cursor.document();
        QString html = document->toHtml();
        qDebug() << html;
        html = html.replace("color:#000000", "color:#ffffff");
        document->setHtml(html);
    }
    else
    {
        QTextCursor cursor = ui->textEdit_log->textCursor();
        QTextDocument *document = cursor.document();
        QString html = document->toHtml();
        html = html.replace("color:#ffffff", "color:#000000");
        document->setHtml(html);
    }
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

// 工作线程相关的槽函数实现
void DataCompressWindow::onAnalysisLogMessage(const QString& msg, QtMsgType msgType)
{
    // 这个槽函数在工作线程中通过信号调用，会自动切换到UI线程执行
    onWriteLog(msg, msgType);
}

void DataCompressWindow::onAnalysisProgress(int current, int total)
{
    // 更新进度条（在UI线程中执行）
    if (total > 0) {
        ui->progressBar->setMaximum(total);
        ui->progressBar->setValue(current);
    }
}

void DataCompressWindow::onAnalysisFinished(bool success, const QString& message)
{
    // 恢复UI状态
    ui->action_analyze->setEnabled(true);
    ui->toolButton_start->setEnabled(true);

    if (success) {
        onWriteLog("数据压缩分析完成", QtInfoMsg);
        
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
            onWriteLog(QString("压缩后文件大小: %1").arg(humanReadableSize(sizeBytes)), QtInfoMsg);
        }
        
        onWriteLog("========================================", QtInfoMsg);
    } else {
        onWriteLog(QString("数据分析失败: %1").arg(message), QtCriticalMsg);
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
                onWriteLog("警告：线程未能正常结束，强制终止", QtWarningMsg);
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
}

void DataCompressWindow::onAnalysisError(const QString& error)
{
    onWriteLog(QString("错误: %1").arg(error), QtCriticalMsg);
}

