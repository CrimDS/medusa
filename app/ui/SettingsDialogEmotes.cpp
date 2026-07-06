#include "ui/SettingsDialog.h"

#include "ui/EmoteStore.h"

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QLineEdit *makeLineEdit(const QString &placeholder = {})
{
    auto *field = new QLineEdit;
    field->setPlaceholderText(placeholder);
    return field;
}

bool editEmoteEntry(QWidget *parent, EmoteEntry &entry, const QString &title)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(520);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    auto *name = makeLineEdit("Wave");
    name->setText(entry.name);
    auto *text = makeLineEdit("/me waves to $d...");
    text->setText(entry.text);
    form->addRow("Name", name);
    form->addRow("Text", text);
    layout->addLayout(form);

    auto *hint = new QLabel("Placeholders: $s sender, $d target, $t Two Weeks. Use /me for action-style emotes.", &dialog);
    hint->setObjectName("sectionLabel");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (name->text().trimmed().isEmpty() || text->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, "Emote", "Emotes need both a name and text.");
            return;
        }

        entry.name = name->text().trimmed();
        entry.text = text->text().trimmed();
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    return dialog.exec() == QDialog::Accepted;
}


} // namespace

void SettingsDialog::populateEmoteTable(const QList<EmoteEntry> &emotes)
{
    m_emoteTable->setRowCount(0);
    for (const EmoteEntry &emote : emotes) {
        const int row = m_emoteTable->rowCount();
        m_emoteTable->insertRow(row);
        m_emoteTable->setItem(row, 0, new QTableWidgetItem(emote.name));
        m_emoteTable->setItem(row, 1, new QTableWidgetItem(emote.text));
    }
    if (m_emoteTable->rowCount() > 0) {
        m_emoteTable->selectRow(0);
    }
}

QList<EmoteEntry> SettingsDialog::emotesFromTable() const
{
    QList<EmoteEntry> emotes;
    for (int row = 0; row < m_emoteTable->rowCount(); ++row) {
        EmoteEntry entry;
        if (auto *name = m_emoteTable->item(row, 0)) {
            entry.name = name->text().trimmed();
        }
        if (auto *text = m_emoteTable->item(row, 1)) {
            entry.text = text->text().trimmed();
        }
        if (!entry.name.isEmpty() && !entry.text.isEmpty()) {
            emotes.append(entry);
        }
    }
    return emotes;
}

void SettingsDialog::editEmoteRow(int row)
{
    if (row < 0 || row >= m_emoteTable->rowCount()) {
        return;
    }

    EmoteEntry entry;
    entry.name = m_emoteTable->item(row, 0)->text();
    entry.text = m_emoteTable->item(row, 1)->text();
    if (!editEmoteEntry(this, entry, "Edit Emote")) {
        return;
    }

    m_emoteTable->item(row, 0)->setText(entry.name);
    m_emoteTable->item(row, 1)->setText(entry.text);
    m_emoteTable->selectRow(row);
}

void SettingsDialog::openEmoteFolder()
{
    const QString folder = EmoteStore::storageFolder();
    QDir().mkpath(folder);
    if (!QFile::exists(EmoteStore::storageFilePath())) {
        EmoteStore::saveEmotes(emotesFromTable());
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void SettingsDialog::addEmote()
{
    EmoteEntry entry{"New Emote", "/me "};
    if (!editEmoteEntry(this, entry, "Add Emote")) {
        return;
    }

    const int row = m_emoteTable->rowCount();
    m_emoteTable->insertRow(row);
    m_emoteTable->setItem(row, 0, new QTableWidgetItem(entry.name));
    m_emoteTable->setItem(row, 1, new QTableWidgetItem(entry.text));
    m_emoteTable->selectRow(row);
}

void SettingsDialog::editSelectedEmote()
{
    editEmoteRow(m_emoteTable->currentRow());
}

void SettingsDialog::removeSelectedEmote()
{
    const int row = m_emoteTable->currentRow();
    if (row < 0) {
        return;
    }

    m_emoteTable->removeRow(row);
    if (m_emoteTable->rowCount() > 0) {
        m_emoteTable->selectRow(qMin(row, m_emoteTable->rowCount() - 1));
    }
}

void SettingsDialog::restoreDefaultEmotes()
{
    if (QMessageBox::question(this, "Restore Emotes", "Restore the default emote list?") != QMessageBox::Yes) {
        return;
    }

    populateEmoteTable(EmoteStore::defaultEmotes());
}


