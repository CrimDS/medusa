#include "ui/LaunchPresentation.h"

#include "ui/LaunchWidgets.h"
#include "ui/LegacyIcons.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QStyle>
#include <QWidget>

namespace {

void polishObjectName(QWidget *widget)
{
    if (!widget) {
        return;
    }

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QIcon builtinLaunchIcon(const LaunchEntry &entry)
{
    if (entry.id.startsWith("darkspace-d9-beta")) {
        return legacyIcon("game2");
    }
    if (entry.id.startsWith("darkspace-d9-")) {
        return legacyIcon("1");
    }
    if (entry.id.startsWith("darkspace-d12-")) {
        return legacyIcon("dsIco");
    }
    if (entry.id == "resourcer") {
        return legacyIcon("tool");
    }
    return {};
}

} // namespace

QIcon launchProgramIcon(const LaunchEntry &entry)
{
    if (!entry.customEntry) {
        const QIcon icon = builtinLaunchIcon(entry);
        if (!icon.isNull()) {
            return icon;
        }
    }

    const QString executable = resolveLaunchPath(entry, entry.executable);
    const QFileInfo info(executable);
    if (!info.exists()) {
        return {};
    }

    QFileIconProvider provider;
    return provider.icon(info);
}

QIcon launchListIcon(const QIcon &icon)
{
    if (icon.isNull()) {
        return {};
    }

    QPixmap pixmap = icon.pixmap(QSize(24, 24), QIcon::Normal, QIcon::Off);
    if (pixmap.isNull()) {
        pixmap = icon.pixmap(24, 24);
    }
    if (pixmap.isNull()) {
        return icon;
    }

    QIcon fixedIcon;
    fixedIcon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    fixedIcon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
    fixedIcon.addPixmap(pixmap, QIcon::Selected, QIcon::Off);
    return fixedIcon;
}

void setLaunchFeatureBackground(QFrame *frame, const LaunchEntry &entry)
{
    if (!frame) {
        return;
    }

    const QString imagePath = findLaunchArtwork(entry, {"banner", "hero", "background", "wide"});
    QPixmap pixmap;
    if (!imagePath.isEmpty()) {
        pixmap.load(imagePath);
    }

    if (auto *feature = dynamic_cast<LaunchFeatureFrame *>(frame)) {
        feature->setBackgroundArt(pixmap);
    }
    frame->setToolTip(imagePath.isEmpty() ? launchArtworkFolder(entry) : QDir::toNativeSeparators(imagePath));
    frame->setProperty("hasArt", !pixmap.isNull());
    polishObjectName(frame);
}

void setLaunchCapsuleArtwork(QLabel *label, const LaunchEntry &entry)
{
    if (!label) {
        return;
    }

    const QString imagePath = findLaunchArtwork(entry, {"capsule", "cover", "library", "grid", "portrait"});
    QPixmap pixmap;
    if (!imagePath.isEmpty()) {
        pixmap.load(imagePath);
    }

    const QString fallbackText = QString("%1\n%2").arg(entry.name, entry.variant);
    if (auto *artLabel = dynamic_cast<LaunchArtLabel *>(label)) {
        artLabel->setLaunchArt(pixmap, fallbackText);
    } else {
        label->setPixmap(pixmap);
        label->setText(pixmap.isNull() ? fallbackText : QString());
    }
    label->setToolTip(imagePath.isEmpty() ? launchArtworkFolder(entry) : QDir::toNativeSeparators(imagePath));
    label->setProperty("hasArt", !pixmap.isNull());
    polishObjectName(label);
}

QString launchStatus(const LaunchEntry &entry)
{
    if (entry.stubOnly) {
        return "Stubbed";
    }

    const QString executable = resolveLaunchPath(entry, entry.executable);
    if (QFileInfo::exists(executable)) {
        return "Ready";
    }

    if (!entry.updater.isEmpty() && QFileInfo::exists(resolveLaunchPath(entry, entry.updater))) {
        return "Launcher ready";
    }

    if (entry.httpManifestUpdater) {
        return "Install required";
    }

    if (usesLegacyMirrorUpdater(entry)) {
        return "Install required";
    }

    return "Not installed";
}

QString launchStatusPillName(const QString &status)
{
    const QString lower = status.toLower();
    if (lower.contains("ready") && !lower.contains("not")) {
        return "pillReady";
    }
    if (lower.contains("install") || lower.contains("missing") || lower.contains("not installed") || lower.contains("failed")) {
        return "pillMissing";
    }
    if (lower.contains("check") || lower.contains("updat") || lower.contains("download") || lower.contains("shared") || lower.contains("extract")) {
        return "pillUpdating";
    }
    if (lower.contains("stub")) {
        return "pillNeutral";
    }
    return "pillNeutral";
}

bool isPrimaryDarkSpaceLaunchEntry(const LaunchEntry &entry)
{
    return entry.id == "darkspace-d9-live" || entry.id == "darkspace-d12-live";
}

bool shouldShowLaunchStatusText(const LaunchEntry &entry, const QString &status)
{
    return status.compare("Ready", Qt::CaseInsensitive) != 0 || isPrimaryDarkSpaceLaunchEntry(entry);
}