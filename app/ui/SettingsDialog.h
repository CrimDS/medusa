#pragma once

#include "ui/EmoteStore.h"

#include <QDialog>
#include <QMap>
#include <QList>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QFrame;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QTableWidget;
class QWidget;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void themeApplied();

private slots:
    void chooseChatLogFolder();
    void chooseThemeColor();
    void resetThemeEditor();
    void saveThemeToFile();
    void loadThemeFromFile();
    void applyThemeSettings();
    void openEmoteFolder();
    void addEmote();
    void editSelectedEmote();
    void removeSelectedEmote();
    void restoreDefaultEmotes();
    void saveSettings();

private:
    QWidget *createAccountPage(QWidget *parent);
    QWidget *createChatPage(QWidget *parent);
    void loadSettings();
    QString normalizedThemeColor(const QString &text) const;
    void loadThemeSettings();
    void updateThemePreview();
    bool themeSettingsChanged(QSettings &settings) const;
    bool saveThemeSettings(QSettings &settings);
    void populateEmoteTable(const QList<EmoteEntry> &emotes);
    QList<EmoteEntry> emotesFromTable() const;
    void editEmoteRow(int row);

    struct ThemeColorControl {
        QString key;
        QLineEdit *field = nullptr;
        QPushButton *button = nullptr;
    };

    QCheckBox *m_rememberAccount = nullptr;
    QCheckBox *m_rememberPassword = nullptr;
    QCheckBox *m_autoLogin = nullptr;
    QLineEdit *m_accountName = nullptr;
    QLineEdit *m_metaServer = nullptr;
    QSpinBox *m_metaPort = nullptr;
    QCheckBox *m_chatLogging = nullptr;
    QCheckBox *m_loadChatHistory = nullptr;
    QLineEdit *m_chatLogFolder = nullptr;
    QPushButton *m_chatLogFolderButton = nullptr;
    QCheckBox *m_discordTextRelay = nullptr;
    QCheckBox *m_discordImageRelay = nullptr;
    QCheckBox *m_discordGifRelay = nullptr;
    QComboBox *m_themeBase = nullptr;
    QFontComboBox *m_themeFont = nullptr;
    QFrame *m_themePreview = nullptr;
    QLabel *m_themePreviewTitle = nullptr;
    QLabel *m_themePreviewMuted = nullptr;
    QLabel *m_themePreviewChat = nullptr;
    QLabel *m_themePreviewStatus = nullptr;
    QLabel *m_themePreviewAccent = nullptr;
    QPushButton *m_themePreviewButton = nullptr;
    QMap<QString, QSpinBox *> m_themeSizeControls;
    QVector<ThemeColorControl> m_themeColorControls;
    QList<EmoteEntry> m_loadedEmotes;
    QTableWidget *m_emoteTable = nullptr;
};
