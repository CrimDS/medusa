#include "ui/MainWindow.h"

#include "launcher/HttpManifestUpdater.h"
#include "launcher/LaunchCatalog.h"
#include "launcher/LaunchEntryDialog.h"
#include "ui/LegacyIcons.h"
#include "ui/MainWindowLaunchSupport.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QUrl>

using namespace gamecq::launch_ui;

void MainWindow::openSelectedLaunchArtworkFolder()
{
    const LaunchEntry entry = findLaunchEntry(selectedLaunchId());
    if (!hasLaunchEntry(entry)) {
        statusBar()->showMessage("Select something in the launcher first.", 3000);
        return;
    }

    const QString folder = launchArtworkFolder(entry, true);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    statusBar()->showMessage(QString("Opened artwork folder for %1.").arg(entry.name), 3000);
}

void MainWindow::chooseD12InstallFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "D12 Install Folder", QDir::toNativeSeparators(d12InstallRoot()));
    if (folder.isEmpty()) {
        return;
    }

    QSettings().setValue("Launch/D12InstallRoot", QDir::cleanPath(QDir::fromNativeSeparators(folder)));
    populateLaunchList();
    updateLaunchDetails();
    statusBar()->showMessage("Updated the D12 install folder.", 3000);
}

void MainWindow::chooseLaunchArtwork(const QString &kind)
{
    const LaunchEntry entry = findLaunchEntry(selectedLaunchId());
    if (!hasLaunchEntry(entry)) {
        statusBar()->showMessage("Select something in the launcher first.", 3000);
        return;
    }

    const QString source = QFileDialog::getOpenFileName(this, QString("Choose %1 Artwork").arg(kind), {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp);;All files (*.*)");
    if (source.isEmpty()) {
        return;
    }

    if (!copyLaunchArtwork(this, entry, source, kind)) {
        return;
    }

    updateLaunchDetails();
    statusBar()->showMessage(QString("Updated %1 artwork for %2.").arg(kind, entry.name), 3000);
}

void MainWindow::showAddLaunchEntryDialog()
{
    LaunchEntry entry;
    entry.id = newCustomLaunchId();
    entry.variant = "Standalone";
    entry.category = "Games";
    entry.kind = "game";
    entry.source = "Standalone";
    entry.customEntry = true;

    QString bannerArtwork;
    QString capsuleArtwork;
    if (!editCustomLaunchEntry(this, entry, true, &bannerArtwork, &capsuleArtwork)) {
        return;
    }

    launchArtworkFolder(entry, true);
    if (!copyLaunchArtwork(this, entry, bannerArtwork, "banner") || !copyLaunchArtwork(this, entry, capsuleArtwork, "capsule")) {
        return;
    }

    QList<LaunchEntry> entries = customLaunchCatalog();
    entries.append(entry);
    writeCustomLaunchCatalog(entries);

    populateLaunchList();
    statusBar()->showMessage(QString("Added %1 to Launch.").arg(entry.name), 4000);
}

void MainWindow::showEditLaunchEntryDialog()
{
    LaunchEntry entry = findLaunchEntry(selectedLaunchId());
    if (!hasLaunchEntry(entry) || !entry.customEntry) {
        statusBar()->showMessage("Select a user-added game or program first.", 3000);
        return;
    }

    QString bannerArtwork;
    QString capsuleArtwork;
    if (!editCustomLaunchEntry(this, entry, false, &bannerArtwork, &capsuleArtwork)) {
        return;
    }

    QList<LaunchEntry> entries = customLaunchCatalog();
    for (LaunchEntry &custom : entries) {
        if (custom.id != entry.id) {
            continue;
        }

        if (!copyLaunchArtwork(this, entry, bannerArtwork, "banner") || !copyLaunchArtwork(this, entry, capsuleArtwork, "capsule")) {
            return;
        }
        custom = entry;
        writeCustomLaunchCatalog(entries);
        populateLaunchList();
        updateLaunchDetails();
        statusBar()->showMessage(QString("Updated %1.").arg(entry.name), 3000);
        return;
    }

    statusBar()->showMessage("Could not update that launch entry.", 4000);
}


void MainWindow::updateSelectedLaunchEntry()
{
    const LaunchEntry entry = findLaunchEntry(selectedLaunchId());
    if (!hasLaunchEntry(entry)) {
        statusBar()->showMessage("Select something to install or update first.", 3000);
        return;
    }

    if (entry.stubOnly) {
        statusBar()->showMessage(QString("%1 %2 is stubbed for now.").arg(entry.name, entry.variant), 4000);
        return;
    }

    if (entry.httpManifestUpdater) {
        startHttpManifestUpdate(entry.id);
        return;
    }

    if (usesLegacyMirrorUpdater(entry)) {
        startLegacyMirrorUpdate(entry.id);
        return;
    }

    statusBar()->showMessage("This launch entry does not have an updater.", 3000);
}

void MainWindow::deleteSelectedLaunchEntry()
{
    const LaunchEntry entry = findLaunchEntry(selectedLaunchId());
    if (!hasLaunchEntry(entry)) {
        statusBar()->showMessage("Select something to delete first.", 3000);
        return;
    }

    if (entry.customEntry) {
        if (QMessageBox::question(this, "Remove Launch Entry", QString("Remove %1 from Launch?").arg(entry.name)) != QMessageBox::Yes) {
            return;
        }

        if (deleteCustomLaunchEntry(entry.id)) {
            populateLaunchList();
            statusBar()->showMessage(QString("Removed %1 from Launch.").arg(entry.name), 4000);
        } else {
            statusBar()->showMessage("Could not remove that Launch entry.", 4000);
        }
        return;
    }

    if (entry.stubOnly) {
        statusBar()->showMessage(QString("%1 %2 is stubbed for now.").arg(entry.name, entry.variant), 4000);
        return;
    }

    const bool d12Running = m_d12Updater && m_d12Updater->isRunning() && entry.httpManifestUpdater;
    const bool legacyRunning = m_legacyMirrorProcess && m_legacyMirrorProcess->state() != QProcess::NotRunning && usesLegacyMirrorUpdater(entry);
    if (d12Running || legacyRunning) {
        statusBar()->showMessage("Wait for the current install or repair to finish before deleting files.", 5000);
        return;
    }

    const QString root = launchInstallRoot(entry);
    if (root.isEmpty() || !QFileInfo::exists(root)) {
        statusBar()->showMessage(QString("%1 %2 is not installed.").arg(entry.name, entry.variant), 4000);
        populateLaunchList();
        return;
    }

    QString reason;
    if (!isSafeBuiltInDeleteRoot(entry, root, &reason)) {
        QMessageBox::warning(this,
                             "Delete Blocked",
                             QString("GameCQ will not delete this folder:\n\n%1\n\n%2").arg(QDir::toNativeSeparators(root), reason));
        return;
    }

    const QString message = QString("Delete the installed files for %1 %2?\n\n%3\n\nThis removes the whole install folder used by this launch entry.")
                                .arg(entry.name, entry.variant, QDir::toNativeSeparators(root));
    QMessageBox confirm(QMessageBox::Warning, "Delete Installed Files", message, QMessageBox::NoButton, this);
    QPushButton *deleteButton = confirm.addButton("Delete Files", QMessageBox::DestructiveRole);
    deleteButton->setIcon(legacyIcon("ico00002"));
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != deleteButton) {
        return;
    }

    statusBar()->showMessage(QString("Deleting %1 %2 files...").arg(entry.name, entry.variant), 5000);
    if (!QDir(root).removeRecursively()) {
        QMessageBox::warning(this,
                             "Delete Failed",
                             QString("Could not delete all files in:\n\n%1\n\nClose any running game/client processes and try again.").arg(QDir::toNativeSeparators(root)));
        populateLaunchList();
        refreshLaunchStatus();
        return;
    }

    populateLaunchList();
    statusBar()->showMessage(QString("Deleted installed files for %1 %2.").arg(entry.name, entry.variant), 6000);
}



