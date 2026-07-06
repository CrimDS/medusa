#include "launcher/LaunchEntryDialog.h"

#include "launcher/LaunchMetadata.h"
#include "ui/LegacyIcons.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QTimer>
#include <QtConcurrent>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

namespace {

QFrame *makeLaunchEntryPanel(const QString &objectName)
{
    auto *panel = new QFrame;
    panel->setObjectName(objectName);
    panel->setFrameShape(QFrame::NoFrame);
    return panel;
}

} // namespace
SteamSearchResult chooseMetadataSearchResult(QWidget *parent, const QString &initialQuery, const SteamLocalApp &localSteamApp)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Identify");
    dialog.resize(560, 460);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *header = new QLabel("Search metadata providers, then choose the best match.", &dialog);
    header->setObjectName("sectionLabel");
    header->setWordWrap(true);
    layout->addWidget(header);

    auto *searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(0, 0, 0, 0);
    searchRow->setSpacing(8);
    auto *search = new QLineEdit(&dialog);
    search->setPlaceholderText("Search by game or program name");
    search->setText(initialQuery);
    searchRow->addWidget(search, 1);
    auto *searchButton = new QPushButton("Search", &dialog);
    searchButton->setObjectName("launchSmallButton");
    searchRow->addWidget(searchButton);
    layout->addLayout(searchRow);

    auto *results = new QListWidget(&dialog);
    results->setObjectName("launchList");
    results->setSelectionMode(QAbstractItemView::SingleSelection);
    results->setIconSize(QSize(18, 18));
    layout->addWidget(results, 1);

    auto *status = new QLabel(&dialog);
    status->setObjectName("sectionLabel");
    status->setWordWrap(true);
    layout->addWidget(status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Use Selected");
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(buttons);

    auto addResult = [&](const SteamSearchResult &result, const QString &providerLabel, bool suggested) {
        if (result.appId.isEmpty() || result.name.isEmpty()) {
            return;
        }
        for (int row = 0; row < results->count(); ++row) {
            if (results->item(row)->data(Qt::UserRole).toString() == result.appId) {
                return;
            }
        }

        auto *item = new QListWidgetItem(QString("%1%2\nSteam app %3")
                                             .arg(suggested ? QString("Suggested: ") : QString(),
                                                  result.name,
                                                  result.appId),
                                         results);
        item->setData(Qt::UserRole, result.appId);
        item->setData(Qt::UserRole + 1, result.name);
        item->setData(Qt::UserRole + 2, providerLabel);
        item->setToolTip(QString("%1 metadata from Steam").arg(result.name));
        item->setSizeHint(QSize(0, 48));
        item->setIcon(legacyIcon("games"));
    };

    QPointer<QFutureWatcher<SteamSearchBatch>> activeSearchWatcher;
    int searchGeneration = 0;

    auto runSearch = [&]() {
        const QString query = search->text().trimmed();
        const int generation = ++searchGeneration;
        results->clear();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        searchButton->setEnabled(false);

        if (localSteamApp.isValid()) {
            addResult({localSteamApp.appId, localSteamApp.name.isEmpty() ? query : localSteamApp.name}, "Steam", true);
        }

        if (query.isEmpty()) {
            status->setText("Enter a name to search.");
            searchButton->setEnabled(true);
            return;
        }

        status->setText(QString("Searching Steam metadata for \"%1\"...").arg(query));
        if (results->count() > 0) {
            results->setCurrentRow(0);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        }

        if (activeSearchWatcher) {
            activeSearchWatcher->disconnect(&dialog);
            QObject::connect(activeSearchWatcher, &QFutureWatcher<SteamSearchBatch>::finished, activeSearchWatcher, &QObject::deleteLater);
            activeSearchWatcher = nullptr;
        }

        auto *watcher = new QFutureWatcher<SteamSearchBatch>(&dialog);
        activeSearchWatcher = watcher;
        QObject::connect(watcher, &QFutureWatcher<SteamSearchBatch>::finished, &dialog, [&, watcher, generation]() {
            const SteamSearchBatch batch = watcher->result();
            watcher->deleteLater();
            if (generation != searchGeneration) {
                return;
            }

            activeSearchWatcher = nullptr;
            searchButton->setEnabled(true);

            for (const SteamSearchResult &match : batch.matches) {
                addResult(match, "Steam", false);
            }

            if (results->count() > 0) {
                if (!results->currentItem()) {
                    results->setCurrentRow(0);
                }
                buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
                status->setText(QString("%1 result%2. Pick the closest match, or edit the search and try again.")
                                    .arg(results->count())
                                    .arg(results->count() == 1 ? QString() : QString("s")));
            } else {
                status->setText(batch.error.isEmpty()
                                    ? QString("No metadata matches were found for \"%1\".").arg(batch.query)
                                    : QString("No metadata matches were found. %1").arg(batch.error));
            }
        });

        watcher->setFuture(QtConcurrent::run([query]() {
            SteamSearchBatch batch;
            batch.query = query;
            batch.matches = steamSearchMatches(nullptr, query, &batch.error);
            return batch;
        }));
    };

    QObject::connect(searchButton, &QPushButton::clicked, &dialog, runSearch);
    QObject::connect(search, &QLineEdit::returnPressed, &dialog, runSearch);
    QObject::connect(results, &QListWidget::currentItemChanged, &dialog, [buttons](QListWidgetItem *current) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(current != nullptr);
    });
    QObject::connect(results, &QListWidget::itemDoubleClicked, &dialog, [&dialog](QListWidgetItem *) {
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (results->currentItem()) {
            dialog.accept();
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QTimer::singleShot(0, &dialog, runSearch);
    if (dialog.exec() != QDialog::Accepted || !results->currentItem()) {
        return {};
    }

    SteamSearchResult selected;
    selected.appId = results->currentItem()->data(Qt::UserRole).toString();
    selected.name = results->currentItem()->data(Qt::UserRole + 1).toString();
    return selected;
}

bool editCustomLaunchEntry(QWidget *parent, LaunchEntry &entry, bool isNew, QString *bannerArtwork, QString *capsuleArtwork)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(isNew ? "Add Game or Software" : "Edit Game or Software");
    dialog.resize(780, 640);
    QString identifiedSteamAppId = entry.steamAppId.trimmed();
    QString selectedServerProvider = entry.serverProvider.trimmed().toLower();

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    auto *header = makeLaunchEntryPanel("launchInfoOverlay");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(14, 12, 14, 12);
    headerLayout->setSpacing(14);

    auto *titleColumn = new QVBoxLayout;
    auto *title = new QLabel(isNew ? "Add to Launch" : QString("Edit %1").arg(entry.name), header);
    title->setObjectName("launchTitle");
    auto *subtitle = new QLabel("Select the .exe for the game or program you want to add. Hit Identify to try and search to automatically fill in details and art work, or add your own.", header);
    subtitle->setObjectName("launchSubtitle");
    subtitle->setWordWrap(true);
    titleColumn->addWidget(title);
    titleColumn->addWidget(subtitle);
    headerLayout->addLayout(titleColumn, 1);
    layout->addWidget(header);

    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = makeLaunchEntryPanel("launchDetailPanel");
    auto *form = new QFormLayout(body);
    form->setContentsMargins(14, 14, 14, 14);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    auto makeFileRow = [&](QLineEdit *field, const QString &buttonText, const std::function<void()> &browse) {
        auto *row = new QWidget(body);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto *button = new QPushButton(buttonText, row);
        QObject::connect(button, &QPushButton::clicked, &dialog, browse);
        rowLayout->addWidget(field, 1);
        rowLayout->addWidget(button);
        return row;
    };

    auto *name = new QLineEdit(body);
    name->setPlaceholderText("Display name");
    name->setText(entry.name);

    auto *platform = new QComboBox(body);
    const QStringList platforms = {
        "Official",
        "Standalone",
        "Steam",
        "Epic Games",
        "GOG",
        "Xbox / Windows",
        "EA app",
        "Ubisoft Connect",
        "Battle.net",
        "itch.io",
        "Other",
    };
    platform->addItems(platforms);
    const QString entryPlatform = launchPlatformForEntry(entry);
    int platformIndex = platform->findText(entryPlatform, Qt::MatchFixedString);
    if (platformIndex < 0) {
        platform->addItem(entryPlatform);
        platformIndex = platform->count() - 1;
    }
    platform->setCurrentIndex(platformIndex);
    platform->setToolTip("Where this title is installed or managed. Identify may use Steam metadata without changing this platform.");

    auto *executable = new QLineEdit(body);
    executable->setPlaceholderText("Program executable");
    executable->setText(QDir::toNativeSeparators(entry.executable));
    form->addRow("Executable", makeFileRow(executable, "Browse", [&]() {
        const QString start = executable->text().trimmed().isEmpty() ? QString() : QFileInfo(executable->text()).absolutePath();
        const QString selected = QFileDialog::getOpenFileName(&dialog, "Program Executable", start, "Programs (*.exe);;All files (*.*)");
        if (selected.isEmpty()) {
            return;
        }

        executable->setText(QDir::toNativeSeparators(selected));
        const QString defaultName = QFileInfo(selected).completeBaseName();
        if (name->text().trimmed().isEmpty()) {
            name->setText(defaultName);
        }
        if (isNew) {
            title->setText(QString("Add %1").arg(defaultName));
        }
        const QString detectedPlatform = detectedLaunchPlatform(selected);
        if (!detectedPlatform.isEmpty() && platform->currentText() == "Standalone") {
            platform->setCurrentText(detectedPlatform);
        }
    }));

    auto *workingDirectory = new QLineEdit(body);
    workingDirectory->setPlaceholderText("Defaults to executable folder");
    workingDirectory->setText(QDir::toNativeSeparators(entry.workingDirectory));
    form->addRow("Working folder", makeFileRow(workingDirectory, "Browse", [&]() {
        const QString start = workingDirectory->text().trimmed().isEmpty() ? QFileInfo(executable->text()).absolutePath() : workingDirectory->text();
        const QString selected = QFileDialog::getExistingDirectory(&dialog, "Working Folder", start);
        if (!selected.isEmpty()) {
            workingDirectory->setText(QDir::toNativeSeparators(selected));
        }
    }));

    form->addRow("Name", name);
    form->addRow("Platform", platform);

    auto *category = new QComboBox(body);
    category->addItems({"Games", "Software", "Tools"});
    const QString entryCategory = normalizedLaunchCategory(entry.category, launchKindForEntry(entry));
    const int categoryIndex = qMax(0, category->findText(entryCategory, Qt::MatchFixedString));
    category->setCurrentIndex(categoryIndex);
    form->addRow("Category", category);

    auto *group = new QComboBox(body);
    group->setEditable(true);
    group->addItems(launchLibraryGroups());
    const QString entryGroup = launchGroupForEntry(entry);
    if (group->findText(entryGroup, Qt::MatchFixedString) < 0) {
        group->addItem(entryGroup);
    }
    group->setCurrentText(entryGroup);
    form->addRow("Group", group);

    auto *arguments = new QLineEdit(body);
    arguments->setPlaceholderText("Optional command-line arguments");
    arguments->setText(entry.commandLine);
    form->addRow("Arguments", arguments);

    auto *description = new QPlainTextEdit(body);
    description->setPlaceholderText("Optional description shown on the launch page");
    description->setPlainText(entry.description);
    description->setMinimumHeight(96);
    form->addRow("Description", description);

    auto *banner = new QLineEdit(body);
    banner->setPlaceholderText("Optional wide banner/background image");
    if (!isNew) {
        banner->setText(QDir::toNativeSeparators(findLaunchArtwork(entry, {"banner", "hero", "background", "wide"})));
    }
    form->addRow("Banner", makeFileRow(banner, "Choose", [&]() {
        const QString selected = QFileDialog::getOpenFileName(&dialog, "Banner Artwork", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp);;All files (*.*)");
        if (!selected.isEmpty()) {
            banner->setText(QDir::toNativeSeparators(selected));
        }
    }));

    auto *capsule = new QLineEdit(body);
    capsule->setPlaceholderText("Optional library capsule/cover image");
    if (!isNew) {
        capsule->setText(QDir::toNativeSeparators(findLaunchArtwork(entry, {"capsule", "cover", "library", "grid", "portrait"})));
    }
    form->addRow("Capsule", makeFileRow(capsule, "Choose", [&]() {
        const QString selected = QFileDialog::getOpenFileName(&dialog, "Capsule Artwork", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp);;All files (*.*)");
        if (!selected.isEmpty()) {
            capsule->setText(QDir::toNativeSeparators(selected));
        }
    }));

    auto *folderActions = new QHBoxLayout;
    auto *identify = new QPushButton("Identify", body);
    QObject::connect(identify, &QPushButton::clicked, &dialog, [&]() {
        const QString executablePath = QDir::cleanPath(QDir::fromNativeSeparators(executable->text().trimmed()));
        if (executablePath.isEmpty() || !QFileInfo::exists(executablePath) || !QFileInfo(executablePath).isFile()) {
            QMessageBox::warning(&dialog, "Identify", "Choose an existing executable first.");
            executable->setFocus();
            return;
        }

        SteamLocalApp steamApp = detectSteamAppForExecutable(executablePath);
        const bool detectedLocalSteamInstall = steamApp.isValid();
        if (detectedLocalSteamInstall) {
            platform->setCurrentText("Steam");
        } else {
            const QString detectedPlatform = detectedLaunchPlatform(executablePath);
            if (!detectedPlatform.isEmpty() && platform->currentText() == "Standalone") {
                platform->setCurrentText(detectedPlatform);
            }
        }
        const QString fallbackName = name->text().trimmed().isEmpty() ? QFileInfo(executablePath).completeBaseName() : name->text().trimmed();
        if (name->text().trimmed().isEmpty()) {
            name->setText(fallbackName);
        }

        const SteamSearchResult match = chooseMetadataSearchResult(&dialog, steamApp.isValid() && !steamApp.name.isEmpty() ? steamApp.name : fallbackName, steamApp);
        if (!match.appId.isEmpty()) {
            steamApp.appId = match.appId;
            steamApp.name = match.name;
        } else {
            return;
        }
        if (!steamApp.isValid()) {
            return;
        }
        identifiedSteamAppId = steamApp.appId.trimmed();
        selectedServerProvider = "steam";

        LaunchEntry detectedEntry = entry;
        detectedEntry.name = steamApp.name.trimmed().isEmpty() ? QFileInfo(executablePath).completeBaseName() : steamApp.name.trimmed();
        detectedEntry.variant = platform->currentText();
        detectedEntry.category = "Games";
        detectedEntry.kind = "game";
        detectedEntry.source = platform->currentText();
        detectedEntry.group = group->currentText().trimmed();
        detectedEntry.executable = executablePath;
        detectedEntry.steamAppId = identifiedSteamAppId;
        detectedEntry.serverProvider = selectedServerProvider;
        detectedEntry.workingDirectory = workingDirectory->text().trimmed().isEmpty()
            ? QFileInfo(executablePath).absolutePath()
            : QDir::cleanPath(QDir::fromNativeSeparators(workingDirectory->text().trimmed()));

        identify->setEnabled(false);
        identify->setText("Identifying...");
        auto *watcher = new QFutureWatcher<SteamIdentifyResult>(&dialog);
        QObject::connect(watcher, &QFutureWatcher<SteamIdentifyResult>::finished, &dialog, [&, watcher, steamApp, detectedEntry, detectedLocalSteamInstall, executablePath]() {
            const SteamIdentifyResult result = watcher->result();
            watcher->deleteLater();
            identify->setEnabled(true);
            identify->setText("Identify");

            const SteamRemoteMetadata metadata = result.metadata;
            const QString detectedName = metadata.name.trimmed().isEmpty() ? steamApp.name.trimmed() : metadata.name.trimmed();
            if (!detectedName.isEmpty()) {
                name->setText(detectedName);
                title->setText(isNew ? QString("Add %1").arg(detectedName) : QString("Edit %1").arg(detectedName));
            }
            category->setCurrentText("Games");
            if (!metadata.shortDescription.trimmed().isEmpty()) {
                description->setPlainText(metadata.shortDescription.trimmed());
            }
            if (!result.bannerPath.isEmpty()) {
                banner->setText(QDir::toNativeSeparators(result.bannerPath));
            }
            if (!result.capsulePath.isEmpty()) {
                capsule->setText(QDir::toNativeSeparators(result.capsulePath));
            }

            QStringList results;
            results << QString("%1 Steam app %2")
                           .arg(detectedLocalSteamInstall ? QString("Detected") : QString("Matched"),
                                steamApp.appId);
            if (!metadata.shortDescription.isEmpty()) {
                results << "description";
            }
            if (!result.bannerPath.isEmpty() || !result.capsulePath.isEmpty()) {
                results << "artwork";
            }
            if (!metadata.newsItems.isEmpty()) {
                results << "news metadata";
            }
            if (!result.error.isEmpty() && !metadata.hasStoreData()) {
                results << QString("store lookup warning: %1").arg(result.error);
            }
            if (results.size() == 1 && !result.error.isEmpty()) {
                results << result.error;
            }
            QMessageBox::information(&dialog, "Identify", results.join("\n"));
        });
        watcher->setFuture(QtConcurrent::run([detectedEntry, steamApp]() {
            SteamIdentifyResult result;
            result.metadata = fetchSteamRemoteMetadata(nullptr, steamApp.appId, &result.error);

            LaunchEntry artworkEntry = detectedEntry;
            const QString detectedName = result.metadata.name.trimmed().isEmpty() ? steamApp.name.trimmed() : result.metadata.name.trimmed();
            if (!detectedName.isEmpty()) {
                artworkEntry.name = detectedName;
            }

            if (!result.metadata.backgroundImageUrl.isEmpty()) {
                result.bannerPath = downloadedLaunchArtworkPath(nullptr, artworkEntry, result.metadata.backgroundImageUrl, "banner");
            }
            if (result.bannerPath.isEmpty() && !result.metadata.headerImageUrl.isEmpty()) {
                result.bannerPath = downloadedLaunchArtworkPath(nullptr, artworkEntry, result.metadata.headerImageUrl, "banner");
            }

            const QString capsuleUrl = result.metadata.capsuleImageUrl.isEmpty() ? result.metadata.headerImageUrl : result.metadata.capsuleImageUrl;
            if (!capsuleUrl.isEmpty()) {
                result.capsulePath = downloadedLaunchArtworkPath(nullptr, artworkEntry, capsuleUrl, "capsule");
            }

            writeSteamMetadataFiles(artworkEntry, result.metadata);
            return result;
        }));
    });
    auto *openArtworkFolder = new QPushButton("Open Artwork Folder", body);
    QObject::connect(openArtworkFolder, &QPushButton::clicked, &dialog, [&]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(launchArtworkFolder(entry, true)));
    });
    folderActions->addStretch(1);
    folderActions->addWidget(identify);
    folderActions->addWidget(openArtworkFolder);
    form->addRow({}, folderActions);

    scroll->setWidget(body);
    layout->addWidget(scroll, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString executablePath = QDir::cleanPath(QDir::fromNativeSeparators(executable->text().trimmed()));
        if (executablePath.isEmpty() || !QFileInfo::exists(executablePath) || !QFileInfo(executablePath).isFile()) {
            QMessageBox::warning(&dialog, "Launch Entry", "Choose an existing executable.");
            executable->setFocus();
            return;
        }

        const QString displayName = name->text().trimmed();
        if (displayName.isEmpty()) {
            QMessageBox::warning(&dialog, "Launch Entry", "Enter a display name.");
            name->setFocus();
            return;
        }

        const QString categoryText = category->currentText().trimmed();
        if (categoryText.isEmpty()) {
            QMessageBox::warning(&dialog, "Launch Entry", "Choose a category.");
            category->setFocus();
            return;
        }

        QString workDir = QDir::cleanPath(QDir::fromNativeSeparators(workingDirectory->text().trimmed()));
        if (workDir.isEmpty()) {
            workDir = QFileInfo(executablePath).absolutePath();
        }
        if (!QFileInfo(workDir).isDir()) {
            QMessageBox::warning(&dialog, "Launch Entry", "Choose an existing working folder.");
            workingDirectory->setFocus();
            return;
        }

        entry.name = displayName;
        const QString platformText = normalizedLaunchPlatform(platform->currentText());
        entry.variant = platformText;
        entry.kind = categoryText == "Games" ? QString("game") : QString("software");
        entry.category = normalizedLaunchCategory(categoryText, entry.kind);
        entry.source = platformText;
        entry.group = group->currentText().trimmed();
        entry.executable = executablePath;
        entry.workingDirectory = workDir;
        entry.commandLine = arguments->text().trimmed();
        entry.customEntry = true;
        entry.description = description->toPlainText().trimmed();
        entry.steamAppId = identifiedSteamAppId;
        entry.serverProvider = selectedServerProvider;

        if (bannerArtwork) {
            *bannerArtwork = QDir::cleanPath(QDir::fromNativeSeparators(banner->text().trimmed()));
        }
        if (capsuleArtwork) {
            *capsuleArtwork = QDir::cleanPath(QDir::fromNativeSeparators(capsule->text().trimmed()));
        }

        dialog.accept();
    });
    layout->addWidget(buttons);

    return dialog.exec() == QDialog::Accepted;
}

