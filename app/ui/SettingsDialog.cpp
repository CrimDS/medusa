#include "ui/SettingsDialog.h"

#include "core/AppPaths.h"
#include "security/CredentialStore.h"
#include "ui/ThemeManager.h"
#include "ui/SettingsDialogThemeSupport.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFont>
#include <QFontComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr auto kDefaultMetaServer = "meta-server.palestar.com";
constexpr int kDefaultMetaPort = 9000;

using namespace gamecq::settings_theme;

QLineEdit *makeLineEdit(const QString &placeholder = {})
{
    auto *field = new QLineEdit;
    field->setPlaceholderText(placeholder);
    return field;
}

QString defaultChatLogFolder()
{
    const QString root = AppPaths::localDataRoot("GameCQ", QDir::homePath() + "/GameCQ");
    return QDir(root).filePath("ChatLogs");
}

bool emotesEqual(const QList<EmoteEntry> &left, const QList<EmoteEntry> &right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (int i = 0; i < left.size(); ++i) {
        if (left.at(i).name != right.at(i).name || left.at(i).text != right.at(i).text) {
            return false;
        }
    }
    return true;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Options");
    resize(660, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *tabs = new QTabWidget(this);

    tabs->addTab(createAccountPage(tabs), "Account");
    tabs->addTab(createChatPage(tabs), "Chat");

    auto *themePage = new QWidget(tabs);
    auto *themeLayout = new QVBoxLayout(themePage);
    themeLayout->setContentsMargins(0, 0, 0, 0);
    themeLayout->setSpacing(8);

    auto *themeScroll = new QScrollArea(themePage);
    themeScroll->setWidgetResizable(true);
    themeScroll->setFrameShape(QFrame::NoFrame);
    auto *themeBody = new QWidget(themeScroll);
    auto *themeBodyLayout = new QHBoxLayout(themeBody);
    themeBodyLayout->setContentsMargins(0, 0, 8, 0);
    themeBodyLayout->setSpacing(12);

    auto *themeEditorColumn = new QWidget(themeBody);
    auto *themeEditorLayout = new QVBoxLayout(themeEditorColumn);
    themeEditorLayout->setContentsMargins(0, 0, 0, 0);
    themeEditorLayout->setSpacing(10);

    auto *presetPanel = makeThemePanel("Preset", "Pick a base style, then tune only the values you care about.", themeEditorColumn);
    auto *presetLayout = qobject_cast<QVBoxLayout *>(presetPanel->layout());
    auto *presetForm = new QFormLayout;
    presetForm->setContentsMargins(0, 0, 0, 0);
    presetForm->setHorizontalSpacing(12);
    presetForm->setVerticalSpacing(8);
    m_themeBase = new QComboBox(presetPanel);
    for (const QString &themeId : ThemeManager::baseThemeIds()) {
        m_themeBase->addItem(ThemeManager::baseThemeLabel(themeId), themeId);
    }
    presetForm->addRow("Base preset", m_themeBase);
    m_themeFont = new QFontComboBox(presetPanel);
    presetForm->addRow("Font family", m_themeFont);
    presetLayout->addLayout(presetForm);
    themeEditorLayout->addWidget(presetPanel);

    auto *sizesPanel = makeThemePanel("Typography", "Compact controls for the text sizes used around the client.", themeEditorColumn);
    auto *sizesLayout = qobject_cast<QVBoxLayout *>(sizesPanel->layout());
    auto *sizesGrid = new QGridLayout;
    sizesGrid->setContentsMargins(0, 0, 0, 0);
    sizesGrid->setHorizontalSpacing(14);
    sizesGrid->setVerticalSpacing(8);
    int sizeIndex = 0;
    for (const ThemeManager::NumericRole &role : ThemeManager::numericRoles()) {
        auto *label = makeThemeLabel(role.label);
        auto *spin = new QSpinBox(sizesPanel);
        spin->setRange(role.minimum, role.maximum);
        spin->setSuffix(" px");
        spin->setMaximumWidth(86);
        m_themeSizeControls.insert(role.key, spin);
        connect(spin, &QSpinBox::valueChanged, this, [this](int) {
            updateThemePreview();
        });

        const int row = sizeIndex / 2;
        const int column = (sizeIndex % 2) * 2;
        sizesGrid->addWidget(label, row, column);
        sizesGrid->addWidget(spin, row, column + 1);
        ++sizeIndex;
    }
    sizesGrid->setColumnStretch(0, 1);
    sizesGrid->setColumnStretch(2, 1);
    sizesLayout->addLayout(sizesGrid);
    themeEditorLayout->addWidget(sizesPanel);

    const QStringList colorGroups = {"Surfaces", "Text", "Accents"};
    for (const QString &group : colorGroups) {
        const QString subtitle = group == "Surfaces"
            ? "Window, panel, input, border, hover, and selection colors."
            : group == "Text" ? "Main text, strong text, muted text, and dim labels."
                              : "Action, status, warning, link, and developer colors.";
        auto *groupPanel = makeThemePanel(group, subtitle, themeEditorColumn);
        auto *groupLayout = qobject_cast<QVBoxLayout *>(groupPanel->layout());
        auto *colorGrid = new QGridLayout;
        colorGrid->setContentsMargins(0, 0, 0, 0);
        colorGrid->setHorizontalSpacing(12);
        colorGrid->setVerticalSpacing(8);

        int colorIndex = 0;
        for (const ThemeManager::ColorRole &role : ThemeManager::colorRoles()) {
            if (colorGroupForKey(role.key) != group) {
                continue;
            }

            auto *cell = new QWidget(groupPanel);
            auto *cellLayout = new QHBoxLayout(cell);
            cellLayout->setContentsMargins(0, 0, 0, 0);
            cellLayout->setSpacing(6);

            auto *label = makeThemeLabel(role.label);
            label->setMinimumWidth(118);
            auto *field = makeLineEdit("#RRGGBB");
            field->setMaximumWidth(94);
            field->setMinimumWidth(88);
            auto *choose = new QPushButton("Pick", cell);
            choose->setMaximumWidth(58);
            choose->setMinimumWidth(58);
            choose->setProperty("themeColorKey", role.key);
            connect(choose, &QPushButton::clicked, this, &SettingsDialog::chooseThemeColor);
            connect(field, &QLineEdit::textChanged, this, [this, choose](const QString &text) {
                setColorButtonPreview(choose, text);
                updateThemePreview();
            });

            cellLayout->addWidget(label, 1);
            cellLayout->addWidget(field);
            cellLayout->addWidget(choose);

            const int row = colorIndex / 2;
            const int column = colorIndex % 2;
            colorGrid->addWidget(cell, row, column);
            ++colorIndex;
            m_themeColorControls.push_back({role.key, field, choose});
        }

        colorGrid->setColumnStretch(0, 1);
        colorGrid->setColumnStretch(1, 1);
        groupLayout->addLayout(colorGrid);
        themeEditorLayout->addWidget(groupPanel);
    }

    auto *themeHint = makeThemeLabel("Use Apply to preview changes in the client, or OK to save and close. Reset reloads the selected preset.", "sectionLabel");
    themeHint->setWordWrap(true);
    themeEditorLayout->addWidget(themeHint);
    themeEditorLayout->addStretch(1);

    auto *previewPanel = makeThemePanel("Preview", "A quick read on the current values before saving.", themeBody);
    previewPanel->setMinimumWidth(290);
    previewPanel->setMaximumWidth(360);
    auto *previewLayout = qobject_cast<QVBoxLayout *>(previewPanel->layout());
    m_themePreview = new QFrame(previewPanel);
    m_themePreview->setObjectName("themePreviewSurface");
    auto *previewSurfaceLayout = new QVBoxLayout(m_themePreview);
    previewSurfaceLayout->setContentsMargins(12, 12, 12, 12);
    previewSurfaceLayout->setSpacing(8);
    m_themePreviewTitle = makeThemeLabel("GameCQ Theme", "themePreviewTitle");
    m_themePreviewMuted = makeThemeLabel("Logged in as [S.W]Crim  Status Online", "themePreviewMuted");
    m_themePreviewChat = makeThemeLabel("17:36 Welcome to DarkSpace 1.744", "themePreviewChat");
    m_themePreviewChat->setWordWrap(true);
    m_themePreviewStatus = makeThemeLabel("FREE  Online  00:00:49", "themePreviewStatus");
    m_themePreviewAccent = makeThemeLabel("Primary accent / link sample", "themePreviewAccent");
    m_themePreviewButton = new QPushButton("Action Button", m_themePreview);
    previewSurfaceLayout->addWidget(m_themePreviewTitle);
    previewSurfaceLayout->addWidget(m_themePreviewMuted);
    previewSurfaceLayout->addWidget(m_themePreviewChat);
    previewSurfaceLayout->addWidget(m_themePreviewStatus);
    previewSurfaceLayout->addWidget(m_themePreviewAccent);
    previewSurfaceLayout->addWidget(m_themePreviewButton);
    previewSurfaceLayout->addStretch(1);
    previewLayout->addWidget(m_themePreview);
    previewLayout->addStretch(1);

    themeBodyLayout->addWidget(themeEditorColumn, 1);
    themeBodyLayout->addWidget(previewPanel, 0);
    themeScroll->setWidget(themeBody);
    themeLayout->addWidget(themeScroll, 1);
    const int themeTabIndex = tabs->addTab(themePage, "Theme");

    auto *emotesPage = new QWidget(tabs);
    auto *emotesLayout = new QVBoxLayout(emotesPage);
    emotesLayout->setContentsMargins(0, 0, 0, 0);
    emotesLayout->setSpacing(8);
    m_emoteTable = new QTableWidget(0, 2, emotesPage);
    m_emoteTable->setHorizontalHeaderLabels({"Name", "Text"});
    m_emoteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_emoteTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_emoteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_emoteTable->verticalHeader()->setVisible(false);
    m_emoteTable->horizontalHeader()->setStretchLastSection(true);
    m_emoteTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    connect(m_emoteTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
        if (item) {
            editEmoteRow(item->row());
        }
    });
    emotesLayout->addWidget(m_emoteTable, 1);

    auto *emoteButtons = new QHBoxLayout;
    auto *addEmoteButton = new QPushButton("Add", emotesPage);
    auto *editEmoteButton = new QPushButton("Edit", emotesPage);
    auto *removeEmoteButton = new QPushButton("Remove", emotesPage);
    auto *openEmoteFolderButton = new QPushButton("Open Folder", emotesPage);
    auto *defaultEmotesButton = new QPushButton("Defaults", emotesPage);
    connect(addEmoteButton, &QPushButton::clicked, this, &SettingsDialog::addEmote);
    connect(editEmoteButton, &QPushButton::clicked, this, &SettingsDialog::editSelectedEmote);
    connect(removeEmoteButton, &QPushButton::clicked, this, &SettingsDialog::removeSelectedEmote);
    connect(openEmoteFolderButton, &QPushButton::clicked, this, &SettingsDialog::openEmoteFolder);
    connect(defaultEmotesButton, &QPushButton::clicked, this, &SettingsDialog::restoreDefaultEmotes);
    emoteButtons->addWidget(addEmoteButton);
    emoteButtons->addWidget(editEmoteButton);
    emoteButtons->addWidget(removeEmoteButton);
    emoteButtons->addStretch(1);
    emoteButtons->addWidget(openEmoteFolderButton);
    emoteButtons->addWidget(defaultEmotesButton);
    emotesLayout->addLayout(emoteButtons);

    tabs->addTab(emotesPage, "Emotes");

    layout->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    auto *loadThemeButton = new QPushButton("Load Theme...", this);
    auto *saveThemeButton = new QPushButton("Save Theme...", this);
    auto *resetThemeButton = new QPushButton("Reset to Preset", this);
    buttons->addButton(loadThemeButton, QDialogButtonBox::ActionRole);
    buttons->addButton(saveThemeButton, QDialogButtonBox::ActionRole);
    buttons->addButton(resetThemeButton, QDialogButtonBox::ActionRole);
    connect(loadThemeButton, &QPushButton::clicked, this, &SettingsDialog::loadThemeFromFile);
    connect(saveThemeButton, &QPushButton::clicked, this, &SettingsDialog::saveThemeToFile);
    connect(resetThemeButton, &QPushButton::clicked, this, &SettingsDialog::resetThemeEditor);
    if (QPushButton *applyButton = buttons->button(QDialogButtonBox::Apply)) {
        connect(applyButton, &QPushButton::clicked, this, &SettingsDialog::applyThemeSettings);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::saveSettings);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    loadSettings();
    connect(m_themeBase, &QComboBox::currentIndexChanged, this, [this](int) {
        resetThemeEditor();
    });
    connect(m_themeFont, &QFontComboBox::currentFontChanged, this, [this](const QFont &) {
        updateThemePreview();
    });
    connect(tabs, &QTabWidget::currentChanged, this, [this, themeTabIndex, loadThemeButton, saveThemeButton, resetThemeButton](int index) {
        const bool themeSelected = index == themeTabIndex;
        for (QPushButton *button : {loadThemeButton, saveThemeButton, resetThemeButton}) {
            button->setVisible(themeSelected);
        }
        if (themeSelected) {
            resize(qMax(width(), 1040), qMax(height(), 620));
        }
    });
    const bool themeSelected = tabs->currentIndex() == themeTabIndex;
    for (QPushButton *button : {loadThemeButton, saveThemeButton, resetThemeButton}) {
        button->setVisible(themeSelected);
    }
}

void SettingsDialog::loadSettings()
{
    QSettings settings;
    m_rememberAccount->setChecked(settings.value("Account/RememberName", false).toBool());
    m_rememberPassword->setChecked(settings.value("Account/RememberPassword", false).toBool());
    m_autoLogin->setEnabled(m_rememberPassword->isChecked());
    m_autoLogin->setChecked(settings.value("Account/AutoLogin", false).toBool() && m_rememberPassword->isChecked());
    m_accountName->setText(settings.value("Account/Name").toString());
    m_metaServer->setText(settings.value("Account/MetaServer", kDefaultMetaServer).toString());
    m_metaPort->setValue(settings.value("Account/MetaPort", kDefaultMetaPort).toInt());

    m_chatLogging->setChecked(settings.value("Chat/LoggingEnabled", false).toBool());
    m_loadChatHistory->setChecked(settings.value("Chat/LoadHistoryOnLogin", false).toBool() && m_chatLogging->isChecked());
    m_loadChatHistory->setEnabled(m_chatLogging->isChecked());
    m_chatLogFolder->setText(QDir::toNativeSeparators(settings.value("Chat/LogFolder", defaultChatLogFolder()).toString()));
    m_chatLogFolder->setEnabled(m_chatLogging->isChecked());
    m_chatLogFolderButton->setEnabled(m_chatLogging->isChecked());
    m_discordTextRelay->setChecked(settings.value("Chat/DiscordTextRelayEnabled", true).toBool());
    m_discordImageRelay->setChecked(settings.value("Chat/DiscordImageRelayEnabled", true).toBool());
    m_discordGifRelay->setChecked(settings.value("Chat/DiscordGifRelayEnabled", true).toBool());
    loadThemeSettings();
    m_loadedEmotes = EmoteStore::loadEmotes();
    populateEmoteTable(m_loadedEmotes);
}

QString SettingsDialog::normalizedThemeColor(const QString &text) const
{
    return normalizedHexColor(text);
}

void SettingsDialog::chooseChatLogFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "Chat Log Folder", m_chatLogFolder->text());
    if (!folder.isEmpty()) {
        m_chatLogFolder->setText(QDir::toNativeSeparators(folder));
    }
}

void SettingsDialog::saveSettings()
{
    QSettings settings;
    const QString oldAccount = settings.value("Account/Name").toString();
    const QString oldServer = settings.value("Account/MetaServer", kDefaultMetaServer).toString();
    const int oldPort = settings.value("Account/MetaPort", kDefaultMetaPort).toInt();
    const bool oldRememberPassword = settings.value("Account/RememberPassword", false).toBool();

    const QString account = m_accountName->text().trimmed();
    const QString server = m_metaServer->text().trimmed().isEmpty() ? QString(kDefaultMetaServer) : m_metaServer->text().trimmed();
    const bool rememberPassword = m_rememberPassword->isChecked() && !account.isEmpty();
    const bool rememberAccount = m_rememberAccount->isChecked() || rememberPassword || m_autoLogin->isChecked();
    const bool autoLogin = rememberPassword && m_autoLogin->isChecked();

    const bool accountKeyChanged = oldAccount.compare(account, Qt::CaseInsensitive) != 0
        || oldServer.compare(server, Qt::CaseInsensitive) != 0
        || oldPort != m_metaPort->value();
    if (oldRememberPassword && (!rememberPassword || accountKeyChanged)) {
        CredentialStore::deletePassword(oldServer, oldPort, oldAccount);
    }

    settings.setValue("Account/RememberName", rememberAccount);
    settings.setValue("Account/RememberPassword", rememberPassword);
    settings.setValue("Account/AutoLogin", autoLogin);
    if (rememberAccount) {
        settings.setValue("Account/Name", m_accountName->text().trimmed());
    } else {
        settings.remove("Account/Name");
    }
    settings.setValue("Account/MetaServer", server);
    settings.setValue("Account/MetaPort", m_metaPort->value());
    settings.setValue("Chat/LoggingEnabled", m_chatLogging->isChecked());
    settings.setValue("Chat/LoadHistoryOnLogin", m_chatLogging->isChecked() && m_loadChatHistory->isChecked());
    settings.setValue("Chat/LogFolder", QDir::cleanPath(QDir::fromNativeSeparators(m_chatLogFolder->text().trimmed().isEmpty() ? defaultChatLogFolder() : m_chatLogFolder->text().trimmed())));
    settings.setValue("Chat/DiscordTextRelayEnabled", m_discordTextRelay->isChecked());
    settings.setValue("Chat/DiscordImageRelayEnabled", m_discordImageRelay->isChecked());
    settings.setValue("Chat/DiscordGifRelayEnabled", m_discordGifRelay->isChecked());
    const bool themeChanged = themeSettingsChanged(settings);
    if (!saveThemeSettings(settings)) {
        return;
    }
    const QList<EmoteEntry> emotes = emotesFromTable();
    if (!emotesEqual(emotes, m_loadedEmotes)) {
        EmoteStore::saveEmotes(emotes);
        m_loadedEmotes = emotes;
    }

    if (themeChanged) {
        emit themeApplied();
    }
    accept();
}


