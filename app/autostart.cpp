#include "autostart.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace {
const char* kName = "COD Redemption Hunter";
QString command() {
    return QStringLiteral("\"%1\" --minimized").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}
#ifndef Q_OS_WIN
QString desktopFile() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/cod-redemption-hunter.desktop";
}
#endif
}  // namespace

namespace autostart {

bool enabled() {
#ifdef Q_OS_WIN
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    return run.contains(kName);
#else
    return QFile::exists(desktopFile());
#endif
}

bool setEnabled(bool on, QString* error) {
#ifdef Q_OS_WIN
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (on) run.setValue(kName, command());
    else run.remove(kName);
    run.sync();
    if (run.status() != QSettings::NoError) {
        if (error) *error = "Windows refused to change the sign-in list.";
        return false;
    }
    return true;
#else
    if (!on) return QFile::remove(desktopFile()) || !QFile::exists(desktopFile());
    QDir().mkpath(QFileInfo(desktopFile()).absolutePath());
    QFile f(desktopFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream(&f) << "[Desktop Entry]\nType=Application\nName=" << kName << "\nExec=" << command()
                    << "\nX-GNOME-Autostart-enabled=true\n";
    return true;
#endif
}

}  // namespace autostart
