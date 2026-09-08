#include "app/AppController.h"
#include "device/SimulatedInstrument.h"
#include "device/IInstrumentPlugin.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QPluginLoader>
#include <QMessageBox>
#include <QScreen>
#include <QShortcut>
#include <QTimer>
#include <memory>

int main(int argc, char *argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("宁波新芝生物科技股份有限公司");
    QCoreApplication::setOrganizationDomain("scientz.com");
    QCoreApplication::setApplicationName("飞秒质谱工作站");
#ifdef QITEST_APP_VERSION
    QCoreApplication::setApplicationVersion(QITEST_APP_VERSION);
#else
    QCoreApplication::setApplicationVersion("development");
#endif
    app.setWindowIcon(QIcon(":/qitest/resources/brand/qitest-app.svg"));
    // Match the instrument build: closing the last window exits and releases AI.
    // Keeping a hidden Mac process alive can reopen an outdated in-memory build.
    app.setQuitOnLastWindowClosed(true);

    qRegisterMetaType<qitest::InstrumentHealth>();
    qRegisterMetaType<qitest::InstrumentTelemetry>();
    qRegisterMetaType<qitest::AnalysisResult>();
    qRegisterMetaType<qitest::AppController::Phase>();
    qRegisterMetaType<qitest::RunSummary>();

    // 厂家联调入口：环境变量指定插件绝对路径；没有指定才使用模拟器。
    // 已指定的驱动加载失败必须报错退出，不能悄悄切回模拟让用户误以为接通。
    // driverLoader 的生命周期覆盖 controller，避免适配器使用期间插件被卸载。
    std::unique_ptr<qitest::IInstrumentAdapter> instrument;
    QPluginLoader driverLoader;
    const QString pluginPath = qEnvironmentVariable("QITEST_INSTRUMENT_PLUGIN");
    if (pluginPath.isEmpty()) instrument = std::make_unique<qitest::SimulatedInstrument>();
    else {
        driverLoader.setFileName(pluginPath);
        auto *factory = qobject_cast<qitest::IInstrumentPlugin *>(driverLoader.instance());
        if (factory) instrument.reset(factory->createAdapter());
        if (!instrument) {
            QMessageBox::critical(nullptr, "仪器驱动未加载", "请检查厂家驱动与当前系统、Qt 和编译器是否匹配。\n"
                + driverLoader.errorString());
            return 2;
        }
    }
    qitest::AppController controller(std::move(instrument));
    qitest::MainWindow window(&controller);
    // 登录/进入工作站后异步导入；每个进程仅一次，已存在的谱图按内容哈希跳过。
    bool customerSamplesRequested = false;
    QObject::connect(&controller, &qitest::AppController::sessionChanged, &window, [&] {
        if (customerSamplesRequested) return;
        customerSamplesRequested = true;
        QTimer::singleShot(0, &controller, &qitest::AppController::loadBundledCustomerSamples);
    });
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &window,
        [&window](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive || window.isVisible()) return;
#if defined(Q_OS_WIN)
            window.showMaximized();
#else
            window.show();
#endif
            window.raise();
            window.activateWindow();
        });
    // Windows fills the available desktop but keeps the system close controls.
    // Mac previews the 4:3 client area
    // in a normal, resizable window instead of filling a wider desktop.
    auto *fullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), &window);
    QObject::connect(fullScreenShortcut, &QShortcut::activated, &window, [&window] {
        if (window.isFullScreen()) window.showNormal();
        else window.showFullScreen();
    });
    auto *leaveFullScreen = new QShortcut(QKeySequence(Qt::Key_Escape), &window);
    QObject::connect(leaveFullScreen, &QShortcut::activated, &window, [&window] {
        if (window.isFullScreen()) window.showNormal();
    });
#if defined(Q_OS_WIN)
    window.showMaximized();
#else
    window.resize(1024, 768);
    window.show();
#endif
    return app.exec();
}
