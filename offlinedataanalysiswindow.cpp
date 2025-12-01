#include "offlinedataanalysiswindow.h"
#include "ui_offlinedataanalysiswindow.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QRandomGenerator>

OfflineDataAnalysisWindow::OfflineDataAnalysisWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::OfflineDataAnalysisWindow)
{
    ui->setupUi(this);

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

    {
        initCustomPlot(1, ui->spectroMeter_top, tr("波形 时间/ns"), tr("计数"), 1);
        initCustomPlot(2, ui->spectroMeter_central, tr("道址"), tr("计数"), 1);
        initCustomPlot(3, ui->spectroMeter_bottom, tr("道址"), tr("计数"), 1);
    }
}

OfflineDataAnalysisWindow::~OfflineDataAnalysisWindow()
{
    delete ui;
}

void OfflineDataAnalysisWindow::initCustomPlot(int index, QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, int graphCount/* = 1*/)
{
    customPlot->installEventFilter(this);
    customPlot->setProperty("index", index);

    // 设置背景网格线是否显示
    //customPlot->xAxis->grid()->setVisible(true);
    //customPlot->yAxis->grid()->setVisible(true);
    // 设置背景网格线条颜色
    //customPlot->xAxis->grid()->setPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::SolidLine));  // 垂直网格线条属性
    //customPlot->yAxis->grid()->setPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::SolidLine)); // 水平网格线条属性
    //customPlot->xAxis->grid()->setSubGridPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::DotLine));
    //customPlot->yAxis->grid()->setSubGridPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::SolidLine));

    // 设置全局抗锯齿
    customPlot->setAntialiasedElements(QCP::aeAll);
    // 图例名称隐藏
    customPlot->legend->setVisible(false);
    // customPlot->legend->setFillOrder(QCPLayoutGrid::foRowsFirst);//设置图例在一列中显示
    // customPlot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop|Qt::AlignRight);// 图例名称显示位置
    // customPlot->legend->setBrush(Qt::NoBrush);//设置背景颜色
    // customPlot->legend->setBorderPen(Qt::NoPen);//设置边框颜色

    // 设置边界
    //customPlot->setContentsMargins(0, 0, 0, 0);
    // 设置标签倾斜角度，避免显示不下
    customPlot->xAxis->setTickLabelRotation(-45);
    // 允许拖拽，缩放
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    // 允许轴自适应大小
    customPlot->xAxis->rescale(false);
    customPlot->yAxis->rescale(false);
    // 设置刻度范围
    customPlot->xAxis->setRange(0, 200);
    customPlot->yAxis->setRange(graphCount == 1 ? 10e2 : 0, graphCount == 1 ? 10e8 : 4100);
    customPlot->yAxis->ticker()->setTickCount(5);
    customPlot->xAxis->ticker()->setTickCount(graphCount == 1 ? 10 : 5);

    customPlot->yAxis2->ticker()->setTickCount(5);
    customPlot->xAxis2->ticker()->setTickCount(graphCount == 1 ? 10 : 5);

    //设置轴标签名称
    customPlot->xAxis->setLabel(axisXLabel);
    customPlot->yAxis->setLabel(axisYLabel);

    // 添加散点图
    QColor colors[] = {Qt::green, Qt::blue, Qt::red, Qt::cyan};
    for (int i=0; i<graphCount; ++i){
        QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
        graph->setAntialiased(false);
        graph->setPen(QPen(colors[i]));
        graph->selectionDecorator()->setPen(QPen(colors[i]));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setSelectable(QCP::SelectionType::stNone);
        //graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, colors[i], 2));//显示散点图
        //graph->setSmooth(true);
    }

    // 添加可选项
    if (graphCount > 1){
        static int index = 0;
        for (int i=0; i<graphCount; ++i){
            QCheckBox* checkBox = new QCheckBox(customPlot);
            checkBox->setText(QLatin1String("")+QString::number(++index));
            checkBox->setObjectName(QLatin1String("CH ")+QString::number(index));
            auto createColorIcon = [&](const QColor &color, int size = 16) {
                // 创建透明背景的QPixmap
                QPixmap pixmap(size, size);
                pixmap.fill(Qt::transparent);

                // 使用QPainter绘制圆形图标
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                //painter.fillRect(pixmap.rect(), color);
                painter.setBrush(color);
                //painter.setPen(Qt::NoPen);
                painter.drawEllipse(1, 1, size-2, size-2);
                painter.end();

                return QIcon(pixmap);
            };

            QColor colors[] = {Qt::green, Qt::blue, Qt::red, Qt::cyan};
            QIcon actionIcon = createColorIcon(colors[i]);
            checkBox->setIcon(actionIcon);
            checkBox->setProperty("index", i+1);
            checkBox->setChecked(true);
            connect(checkBox, &QCheckBox::stateChanged, this, [=](int state){
                int index = checkBox->property("index").toInt();
                QCPGraph *graph = customPlot->graph(QLatin1String("Graph ")+QString::number(index));
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
                checkBox->move(customPlot->axisRect()->topRight().x() - 150, customPlot->axisRect()->topRight().y() + i++ * 20 + 10);
            }
        });
    }

    // if (graphCount == 1){
    //     QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
    //     customPlot->yAxis->setTicker(logTicker);
    //     //customPlot->yAxis2->setTicker(logTicker);
    //     customPlot->yAxis->setScaleType(QCPAxis::ScaleType::stLogarithmic);
    //     customPlot->yAxis->setNumberFormat("eb");//使用科学计数法表示刻度
    //     customPlot->yAxis->setNumberPrecision(0);//小数点后面小数位数

    //     customPlot->yAxis2->setTicker(logTicker);
    //     customPlot->yAxis2->setScaleType(QCPAxis::ScaleType::stLogarithmic);
    //     customPlot->yAxis2->setNumberFormat("eb");//使用科学计数法表示刻度
    //     customPlot->yAxis2->setNumberPrecision(0);//小数点后面小数位数
    // }
    // else {
    //     QSharedPointer<QCPAxisTicker> ticker(new QCPAxisTicker);
    //     customPlot->yAxis->setTicker(ticker);
    // }

    customPlot->replot();
    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));


    // 是否允许X轴自适应缩放
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));
}

bool OfflineDataAnalysisWindow::eventFilter(QObject *watched, QEvent *event){
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

void OfflineDataAnalysisWindow::replyWriteLog(const QString &msg, QtMsgType msgType/* = QtDebugMsg*/)
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

bool OfflineDataAnalysisWindow::loadOfflineFilename(const QString& filename)
{
    ui->textBrowser_filepath->setText(filename);
    return true;
}

void OfflineDataAnalysisWindow::startAnalyze()
{

}

void OfflineDataAnalysisWindow::on_pushButton_test_clicked()
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

void OfflineDataAnalysisWindow::on_pushButton_startUpload_clicked()
{
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

void OfflineDataAnalysisWindow::on_pushButton_start_clicked()
{
    // 开始分析
}

void OfflineDataAnalysisWindow::replyWaveform(quint8, QVector<quint16>&)
{

}
