#include "mainwindow.h"
#include "offlinewindow.h"
#include "datacompresswindow.h"
#include "globalsettings.h"
#include "darkstyle.h"

#include <QApplication>
#include <QStyleFactory>
#include <QFileInfo>
#include <QDir>
#include <QSplashScreen>
#include <QScreen>
#include <QMessageBox>
#include <QTimer>

#include <log4qt/log4qt.h>
#include <log4qt/logger.h>
#include <log4qt/layout.h>
#include <log4qt/patternlayout.h>
#include <log4qt/consoleappender.h>
#include <log4qt/dailyfileappender.h>
#include <log4qt/logmanager.h>
#include <log4qt/propertyconfigurator.h>
#include <log4qt/loggerrepository.h>
#include <log4qt/fileappender.h>

QMainWindow *mMainWindow = nullptr;
QMutex mutexMsg;
QtMessageHandler system_default_message_handler = NULL;// 用来保存系统默认的输出接口
void AppMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString &msg)
{
    QMutexLocker locker(&mutexMsg);
    if (type == QtWarningMsg)
        return;

    if (mMainWindow && type != QtDebugMsg){
        QMetaObject::invokeMethod(mMainWindow, "reporWriteLog", Qt::QueuedConnection, Q_ARG(QString, msg), Q_ARG(QtMsgType, type));
    }

    //这里必须调用，否则消息被拦截，log4qt无法捕获系统日志
    if (system_default_message_handler){
        system_default_message_handler(type, context, msg);
    }
}

#include <locale.h>
#include <QTextCodec>
#include <QTranslator>
#include <QLibraryInfo>
static QTranslator qtTranslator;
static QTranslator qtbaseTranslator;
static QTranslator appTranslator;
int main(int argc, char *argv[])
{
    // QApplication::setAttribute(Qt::AA_DisableHighDpiScaling); // 禁用高DPI缩放支持
    // QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps); // 使用高DPI位图
    // QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGoodWindow::setup();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication a(argc, argv);
    QApplication::setApplicationName("中子相机");
    QApplication::setOrganizationName("Copyright (c) 2025");
    QApplication::setOrganizationDomain("");
    QApplication::setApplicationVersion(APP_VERSION);
    QApplication::setStyle(QStyleFactory::create("fusion"));//WindowsVista fusion windows

    GlobalSettings settings;
    if(settings.value("Global/Options/enableNativeUI",false).toBool()) {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);
        QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar,false);
        QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings,false);
    }

    QFont font = QApplication::font();
#if defined(Q_OS_WIN) && defined(Q_CC_MSVC)
    int fontId = QFontDatabase::addApplicationFont(QApplication::applicationDirPath() + "/inziu-iosevkaCC-SC-regular.ttf");
#else
    int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/font/font/inziu-iosevkaCC-SC-regular.ttf"));
#endif
    QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
    if (fontFamilies.size() > 0) {
        font.setFamily(fontFamilies[0]);//启用内置字体
    }

    int pointSize = font.pointSize();
    qreal dpi = QGuiApplication::primaryScreen()->devicePixelRatio();
    if (dpi >= 2.0)
        pointSize += 3;
    else if (dpi > 1.0)
        pointSize += 2;
    font.setPointSize(pointSize);
    font.setFixedPitch(true);
    qApp->setFont(font);
    qApp->setStyle(new DarkStyle());
    qApp->style()->setObjectName("fusion");

    settings.beginGroup("Version");
    settings.setValue("Version",GIT_VERSION);
    settings.endGroup();

    QSplashScreen splash;
    splash.setPixmap(QPixmap(":/splash.png"));
    splash.show();

    splash.showMessage(QObject::tr("启动中，请稍等..."), Qt::AlignLeft | Qt::AlignBottom, Qt::white);

    // 启用新的日子记录类
    QString filename = QFileInfo(QCoreApplication::applicationFilePath()).baseName();
    QStringList args = QCoreApplication::arguments();
    if (args.contains("-m") && args.contains("offline")){
        filename += ".offline";
    }
    else if (args.contains("-c") && args.contains("compress")){
        filename += ".compress";
    }

    QString sConfFilename = QString("./config/%1.log4qt.conf").arg(filename);
    if (QFileInfo::exists(sConfFilename)){
        Log4Qt::PropertyConfigurator::configure(sConfFilename);
    } else {
        Log4Qt::LogManager::setHandleQtMessages(true);
        Log4Qt::Logger *logger = Log4Qt::Logger::rootLogger();
        logger->setLevel(Log4Qt::Level::DEBUG_INT); //设置日志输出级别

        /****************PatternLayout配置日志的输出格式****************************/
        Log4Qt::PatternLayout *layout = new Log4Qt::PatternLayout();
        layout->setConversionPattern("%d{yyyy-MM-dd HH:mm:ss.zzz} [%p]: %m %n");
        layout->activateOptions();

        /***************************配置日志的输出位置***********/
        //输出到控制台
        Log4Qt::ConsoleAppender *consoleAppender = new Log4Qt::ConsoleAppender(layout, Log4Qt::ConsoleAppender::STDOUT_TARGET);
        consoleAppender->activateOptions();
        consoleAppender->setEncoding(QTextCodec::codecForName("UTF-8"));
        logger->addAppender(consoleAppender);

        //输出到文件(如果需要把离线处理单独保存日志文件，可以改这里)        
        Log4Qt::DailyFileAppender *dailiAppender = new Log4Qt::DailyFileAppender(layout, "logs/.log", QString("%1_yyyy-MM-dd").arg(filename));
        dailiAppender->setAppendFile(true);
        dailiAppender->activateOptions();
        dailiAppender->setEncoding(QTextCodec::codecForName("UTF-8"));
        logger->addAppender(dailiAppender);
    }

    // 确保logs目录存在
    QDir dir(QDir::currentPath() + "/logs");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString qlibpath = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
    if(qtTranslator.load("qt_zh_CN.qm",qlibpath))
        qApp->installTranslator(&qtTranslator);
    if(qtbaseTranslator.load("qtbase_zh_CN.qm",qlibpath))
        qApp->installTranslator(&qtbaseTranslator);

    qRegisterMetaType<QtMsgType>("QtMsgType");
    system_default_message_handler = qInstallMessageHandler(AppMessageHandler);

    bool isDarkTheme = true;
    settings.beginGroup("Global/Startup");
    if(settings.contains("darkTheme"))
        isDarkTheme = settings.value("darkTheme").toBool();

    QColor themeColor = settings.value("Global/Startup/themeColor",QColor(30,30,30)).value<QColor>();
    bool themeColorEnable = settings.value("Global/Startup/themeColorEnable",true).toBool();
    settings.endGroup();

    if(isDarkTheme) {
        QGoodWindow::setAppDarkTheme();
    } else {
        QGoodWindow::setAppLightTheme();
    }
    if(themeColorEnable) {
        qGoodStateHolder->setCurrentThemeDark(isDarkTheme);
        QGoodWindow::setAppCustomTheme(isDarkTheme, themeColor); // Must be >96
    }

    QGoodWindowHelper w;
    if (args.contains("-m") && args.contains("offline")){
        QApplication::setApplicationName("中子相机数据处理离线版");
        mMainWindow = new OfflineWindow(isDarkTheme, &w);
    }
    else if (args.contains("-c") && args.contains("compress")){
        QApplication::setApplicationName(QObject::tr("中子相机数据压缩与上传"));
        mMainWindow = new DataCompressWindow(isDarkTheme, &w);
    }
    else
        mMainWindow = new MainWindow(isDarkTheme, &w);
    w.setupUiHelper(mMainWindow, isDarkTheme);

    qInfo().noquote() << QObject::tr("系统启动");
    splash.finish(&w);

    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    int x = (screenRect.width() - w.width()) / 2;
    int y = (screenRect.height() - w.height()) / 2;
    w.move(x, y);
    w.setWindowState(w.windowState() | Qt::WindowMaximized);
    w.show();

    int ret = a.exec();

    //运行运行到这里，此时主窗体析构函数还没触发，所以shutdownRootLogger需要在主窗体销毁以后再做处理
    QObject::connect(&w, &QObject::destroyed, []{
        auto logger = Log4Qt::Logger::rootLogger();
        logger->removeAllAppenders();
        logger->loggerRepository()->shutdown();
    });

    return ret;
}
