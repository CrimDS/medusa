#include "ui/HelpDialogs.h"

#include "net/ProfileFlags.h"

#include "ui/LegacyIcons.h"
#include "ui/ThemeManager.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>

void showGameCQAboutDialog(QWidget *parent)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("About GameCQ");
    dialog->resize(560, 420);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(14);

    auto *header = new QHBoxLayout;
    auto *icon = new QLabel(dialog);
    icon->setPixmap(legacyIcon("GCQL").pixmap(40, 40));
    icon->setFixedSize(48, 48);
    icon->setAlignment(Qt::AlignCenter);
    header->addWidget(icon);

    auto *titleBlock = new QVBoxLayout;
    auto *title = new QLabel("GameCQ 1 ⅜", dialog);
    title->setObjectName("launchTitle");
    auto *subtitle = new QLabel("DarkSpace lobby client, installer, launcher, and community hub.", dialog);
    subtitle->setObjectName("sectionLabel");
    subtitle->setWordWrap(true);
    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);
    header->addLayout(titleBlock, 1);
    layout->addLayout(header);

    auto *body = new QTextBrowser(dialog);
    body->setOpenExternalLinks(true);
    body->setStyleSheet(QString("QTextBrowser { background:%1; color:%2; border:1px solid %3; }")
                            .arg(ThemeManager::colorName("pageBg"),
                                 ThemeManager::colorName("text"),
                                 ThemeManager::colorName("border")));
    body->setHtml(QString(R"HTML(
<html><head><style>
body{background:%1;color:%2;font-family:'%8','Segoe UI',Tahoma,sans-serif;margin:0;line-height:1.42;}
h2{font-size:13px;margin:16px 0 6px 0;color:%4;text-transform:uppercase;}
p{margin:0 0 10px 0;color:%2;}
ul{margin:4px 0 10px 20px;padding:0;color:%2;}
li{margin:3px 0;}
.muted{color:%3;}
.strong{color:%5;font-weight:700;}
a{color:%6;}
</style></head><body>
<p><span class="strong">GameCQ 1 ⅜</span> is the modern C++/Qt client for the DarkSpace community.</p>
<h2>Build</h2>
<p class="muted">Qt %7<br></p>
<h2>Credits</h2>
<p>DarkSpace and the original GameCQ/GCQL client were created by PaleStar. Thanks to those that lead the way. </p>
</body></html>
)HTML")
                      .arg(ThemeManager::colorName("pageBg"),
                           ThemeManager::colorName("text"),
                           ThemeManager::colorName("textMuted"),
                           ThemeManager::colorName("primary"),
                           ThemeManager::colorName("textStrong"),
                           ThemeManager::colorName("blue"),
                           QString::fromLatin1(qVersion()),
                           ThemeManager::fontFamily().toHtmlEscaped()));
    layout->addWidget(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog->open();
}

void showChatCommandsHelpDialog(QWidget *parent, quint32 sessionId, const QString &profileName, quint32 profileFlags, bool canModerate, bool canUseStaff)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Chat Commands");
    dialog->resize(760, 560);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *browser = new QTextBrowser(dialog);
    browser->setOpenExternalLinks(false);
    browser->setStyleSheet(QString("QTextBrowser { background:%1; color:%2; border:1px solid %3; }")
                               .arg(ThemeManager::colorName("pageBg"),
                                    ThemeManager::colorName("text"),
                                    ThemeManager::colorName("border")));

    QString html;
    auto appendEscaped = [&html](const QString &text) {
        html += text.toHtmlEscaped();
    };
    auto addRow = [&html](const QString &command, const QString &description) {
        html += "<tr><td><code>";
        html += command.toHtmlEscaped();
        html += "</code></td><td>";
        html += description.toHtmlEscaped();
        html += "</td></tr>";
    };
    auto beginSection = [&html](const QString &title, const QString &subtitle = {}) {
        html += "<h2>";
        html += title.toHtmlEscaped();
        html += "</h2>";
        if (!subtitle.isEmpty()) {
            html += "<p class=\"hint\">";
            html += subtitle.toHtmlEscaped();
            html += "</p>";
        }
        html += "<table>";
    };
    auto endSection = [&html]() {
        html += "</table>";
    };

    const bool isLoggedIn = sessionId != 0;
    const bool isAdmin = (profileFlags & kAdministratorFlag) != 0;
    const bool isDeveloper = (profileFlags & kDeveloperFlag) != 0;
    const bool isEvent = (profileFlags & kEventFlag) != 0;
    const bool isServer = (profileFlags & kServerFlag) != 0;

    QStringList access;
    access << "Player";
    if (canModerate) {
        access << "Moderator";
    }
    if (isAdmin) {
        access << "Admin";
    }
    if (canUseStaff) {
        access << "Staff";
    }
    if (isDeveloper) {
        access << "Developer";
    }
    if (isEvent) {
        access << "Event";
    }
    if (isServer) {
        access << "Server";
    }

    html += QString("<html><head><style>"
                    "body{background:%1;color:%2;font-family:%8,Segoe UI,Tahoma,sans-serif;margin:0;}"
                    "h1{font-size:22px;margin:0 0 6px 0;color:%5;}"
                    "h2{font-size:14px;margin:18px 0 6px 0;color:%4;text-transform:uppercase;}"
                    "p{color:%3;margin:0 0 8px 0;line-height:1.35;}"
                    "p.hint{color:%7;}"
                    "table{width:100%;border-collapse:collapse;margin-bottom:4px;}"
                    "td{border-top:1px solid %6;padding:7px 8px;vertical-align:top;}"
                    "td:first-child{width:44%;white-space:nowrap;color:%9;}"
                    "code{color:%9;font-family:Cascadia Mono,Consolas,monospace;font-size:12px;}"
                    "</style></head><body>")
                .arg(ThemeManager::colorName("pageBg"),
                     ThemeManager::colorName("text"),
                     ThemeManager::colorName("textMuted"),
                     ThemeManager::colorName("primary"),
                     ThemeManager::colorName("textStrong"),
                     ThemeManager::colorName("border"),
                     ThemeManager::colorName("textDim"),
                     ThemeManager::fontFamily(),
                     ThemeManager::colorName("warning"));
    html += "<h1>Chat Commands</h1>";
    html += "<p>";
    appendEscaped(isLoggedIn
                      ? QString("Showing commands for %1. Access: %2.").arg(profileName, access.join(", "))
                      : QString("Showing regular player commands. Staff commands appear after logging into an account with matching flags."));
    html += "</p>";

    beginSection("Emote Placeholders", "Used in Options > Emotes before the emote text is sent.");
    addRow("$s", "Sender/current user name.");
    addRow("$d", "Destination or targeted room member name.");
    addRow("$t", "Legacy shortcut for Two Weeks&trade;.");
    endSection();

    beginSection("Player Commands");
    addRow("/?", "Display server chat help.");
    addRow("/me [message]", "Send an action/emote line.");
    addRow("/send [target] [message]", "Send a private or group message.");
    addRow("/tell [target] [message]", "Alias-style equivalent of /send.");
    addRow("/report [playername|@id] [message]", "Report a user or issue to moderators.");
    addRow("/away", "Set your lobby status to away.");
    addRow("/back", "Clear away status.");
    endSection();

    beginSection("/send and /tell Targets");
    addRow("friends", "Send to your friends.");
    addRow("clan", "Send to online fleet/clan members.");
    addRow("clanadmin", "Send as fleet admin, if you are a fleet admin.");
    addRow("clanoffline", "Send to fleet/clan members, including offline delivery.");
    addRow("user name", "Send to a matched account name.");
    addRow("@user id", "Send to a specific user id.");
    addRow("fleet [@fleet id | fleet name]", "Send to a specific fleet/clan.");
    endSection();

    if (canModerate) {
        beginSection("Moderator Commands");
        addRow("/broadcast [message]", "Broadcast a message to all clients.");
        addRow("/modtalk [message]", "Send a message to moderators.");
        addRow("/moderate", "Toggle moderated status for the current room.");
        addRow("/modsend [user name|@user id] [message]", "Send an official moderator message to a user.");
        addRow("/modsay [message]", "Send an official moderator message to the current room.");
        addRow("/mute [user name|@user id]", "Mute a user.");
        addRow("/unmute [user name|@user id]", "Remove mute status from a user.");
        addRow("/check [user name|@user id]", "Check watchlist and fake status entries.");
        addRow("/watch [user name|@user id] [reason]", "Add a watchlist entry.");
        addRow("/push [user name|@user id|$IP] [reason]", "Short disconnect/temporary ban.");
        addRow("/kick [user name|@user id|$IP] [reason]", "10 minute ban.");
        addRow("/ban [user name|@user id|$IP] [reason]", "24 hour ban.");
        addRow("/banned", "List bans expiring within 24 hours.");
        addRow("/bannedlong", "List all bans.");
        addRow("/unban [banId]", "Remove a ban.");
        addRow("/clones [user name|@user id]", "Check related or duplicate accounts.");
        addRow("/servers", "Find users and servers sharing machine id or IP.");
        addRow("/logroom [@roomId] [limit] [offset]", "Display room chat logs.");
        addRow("/session [user name|@user id]", "Display session information for a user.");
        endSection();
    }

    if (canUseStaff) {
        beginSection("Staff Commands");
        addRow("/staffsend [message]", "Send a message to staff.");
        endSection();
    }

    if (isDeveloper) {
        beginSection("Developer Commands");
        addRow("/devtalk [message]", "Send a message to developers.");
        endSection();
    }

    if (isAdmin) {
        beginSection("Admin Commands");
        addRow("/reban [banId] [days]", "Extend an existing ban.");
        addRow("/loguser [user name|@user id] [limit] [offset]", "Display all chat sent or received by a user, including private messages.");
        endSection();
    }

    if (isEvent || isAdmin) {
        beginSection("Event/Admin Visibility Commands");
        addRow("/hide", "Set status to hidden.");
        addRow("/unhide", "Remove hidden status.");
        endSection();
    }

    if (isServer) {
        beginSection("Server Commands");
        addRow("/notice [message]", "Send a server notice event.");
        endSection();
    }

    html += "</body></html>";
    browser->setHtml(html);
    layout->addWidget(browser, 1);

    auto *close = new QPushButton("Close", dialog);
    close->setIcon(legacyIcon("cancel"));
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(close);
    layout->addLayout(buttonRow);

    dialog->open();
}
