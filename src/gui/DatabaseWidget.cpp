/*
 *  Copyright (C) 2010 Felix Geyer <debfx@fobos.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "DatabaseWidget.h"

#include <QAction>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QSplitter>
#include <QLabel>
#include <QProcess>
#include <QHeaderView>
#include <QApplication>

#include "autotype/AutoType.h"
#include "core/Config.h"
#include "core/EntrySearcher.h"
#include "core/FilePath.h"
#include "core/Group.h"
#include "core/Metadata.h"
#include "core/Tools.h"
#include "gui/ChangeMasterKeyWidget.h"
#include "gui/Clipboard.h"
#include "gui/DatabaseOpenWidget.h"
#include "gui/DatabaseSettingsWidget.h"
#include "gui/KeePass1OpenWidget.h"
#include "gui/MessageBox.h"
#include "gui/UnlockDatabaseWidget.h"
#include "gui/entry/EditEntryWidget.h"
#include "gui/entry/EntryView.h"
#include "gui/group/EditGroupWidget.h"
#include "gui/group/GroupView.h"

DatabaseWidget::DatabaseWidget(Database* db, QWidget* parent)
    : QStackedWidget(parent)
    , m_db(db)
    , m_newGroup(nullptr)
    , m_newEntry(nullptr)
    , m_newParent(nullptr)
{
    m_mainWidget = new QWidget(this);
    QLayout* layout = new QHBoxLayout(m_mainWidget);
    m_splitter = new QSplitter(m_mainWidget);
    m_splitter->setChildrenCollapsible(false);

    QWidget* rightHandSideWidget = new QWidget(m_splitter);

    m_groupView = new GroupView(db, m_splitter);
    m_groupView->setObjectName("groupView");
    m_groupView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupView, SIGNAL(customContextMenuRequested(QPoint)),
            SLOT(emitGroupContextMenuRequested(QPoint)));

    m_entryView = new EntryView(rightHandSideWidget);
    m_entryView->setObjectName("entryView");
    m_entryView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_entryView->setGroup(db->rootGroup());
    connect(m_entryView, SIGNAL(customContextMenuRequested(QPoint)),
            SLOT(emitEntryContextMenuRequested(QPoint)));

    // Add a notification for when we are searching
    m_searchingLabel = new QLabel();
    m_searchingLabel->setText(tr("Searching..."));
    m_searchingLabel->setAlignment(Qt::AlignCenter);
    m_searchingLabel->setStyleSheet("background-color: rgb(255, 253, 160);"
                                    "border: 2px solid rgb(190, 190, 190);"
                                    "border-radius: 5px;");

    QVBoxLayout* vLayout = new QVBoxLayout(rightHandSideWidget);
    vLayout->setMargin(0);
    vLayout->addWidget(m_searchingLabel);
    vLayout->addWidget(m_entryView);

    m_searchingLabel->setVisible(false);

    rightHandSideWidget->setLayout(vLayout);

    setTabOrder(m_entryView, m_groupView);

    m_splitter->addWidget(m_groupView);
    m_splitter->addWidget(rightHandSideWidget);

    m_splitter->setStretchFactor(0, 30);
    m_splitter->setStretchFactor(1, 70);

    layout->addWidget(m_splitter);
    m_mainWidget->setLayout(layout);

    m_editEntryWidget = new EditEntryWidget();
    m_editEntryWidget->setObjectName("editEntryWidget");
    m_historyEditEntryWidget = new EditEntryWidget();
    m_editGroupWidget = new EditGroupWidget();
    m_editGroupWidget->setObjectName("editGroupWidget");
    m_changeMasterKeyWidget = new ChangeMasterKeyWidget();
    m_changeMasterKeyWidget->headlineLabel()->setText(tr("Change master key"));
    QFont headlineLabelFont = m_changeMasterKeyWidget->headlineLabel()->font();
    headlineLabelFont.setBold(true);
    headlineLabelFont.setPointSize(headlineLabelFont.pointSize() + 2);
    m_changeMasterKeyWidget->headlineLabel()->setFont(headlineLabelFont);
    m_databaseSettingsWidget = new DatabaseSettingsWidget();
    m_databaseSettingsWidget->setObjectName("databaseSettingsWidget");
    m_databaseOpenWidget = new DatabaseOpenWidget();
    m_databaseOpenWidget->setObjectName("databaseOpenWidget");
    m_keepass1OpenWidget = new KeePass1OpenWidget();
    m_keepass1OpenWidget->setObjectName("keepass1OpenWidget");
    m_unlockDatabaseWidget = new UnlockDatabaseWidget();
    m_unlockDatabaseWidget->setObjectName("unlockDatabaseWidget");
    addWidget(m_mainWidget);
    addWidget(m_editEntryWidget);
    addWidget(m_editGroupWidget);
    addWidget(m_changeMasterKeyWidget);
    addWidget(m_databaseSettingsWidget);
    addWidget(m_historyEditEntryWidget);
    addWidget(m_databaseOpenWidget);
    addWidget(m_keepass1OpenWidget);
    addWidget(m_unlockDatabaseWidget);

    connect(m_splitter, SIGNAL(splitterMoved(int,int)), SIGNAL(splitterSizesChanged()));
    connect(m_entryView->header(), SIGNAL(sectionResized(int,int,int)), SIGNAL(entryColumnSizesChanged()));
    connect(m_groupView, SIGNAL(groupChanged(Group*)), this, SLOT(clearLastGroup(Group*)));
    connect(m_groupView, SIGNAL(groupChanged(Group*)), SIGNAL(groupChanged()));
    connect(m_groupView, SIGNAL(groupChanged(Group*)), m_entryView, SLOT(setGroup(Group*)));
    connect(m_entryView, SIGNAL(entryActivated(Entry*, EntryModel::ModelColumn)),
            SLOT(entryActivationSignalReceived(Entry*, EntryModel::ModelColumn)));
    connect(m_entryView, SIGNAL(entrySelectionChanged()), SIGNAL(entrySelectionChanged()));
    connect(m_editEntryWidget, SIGNAL(editFinished(bool)), SLOT(switchToView(bool)));
    connect(m_editEntryWidget, SIGNAL(historyEntryActivated(Entry*)), SLOT(switchToHistoryView(Entry*)));
    connect(m_historyEditEntryWidget, SIGNAL(editFinished(bool)), SLOT(switchBackToEntryEdit()));
    connect(m_editGroupWidget, SIGNAL(editFinished(bool)), SLOT(switchToView(bool)));
    connect(m_changeMasterKeyWidget, SIGNAL(editFinished(bool)), SLOT(updateMasterKey(bool)));
    connect(m_databaseSettingsWidget, SIGNAL(editFinished(bool)), SLOT(switchToView(bool)));
    connect(m_databaseOpenWidget, SIGNAL(editFinished(bool)), SLOT(openDatabase(bool)));
    connect(m_keepass1OpenWidget, SIGNAL(editFinished(bool)), SLOT(openDatabase(bool)));
    connect(m_unlockDatabaseWidget, SIGNAL(editFinished(bool)), SLOT(unlockDatabase(bool)));
    connect(this, SIGNAL(currentChanged(int)), this, SLOT(emitCurrentModeChanged()));

    m_searchCaseSensitive = false;
    m_searchCurrentGroup = false;

    setCurrentWidget(m_mainWidget);
}

DatabaseWidget::~DatabaseWidget()
{
}

DatabaseWidget::Mode DatabaseWidget::currentMode() const
{
    if (currentWidget() == nullptr) {
        return DatabaseWidget::None;
    }
    else if (currentWidget() == m_mainWidget) {
        return DatabaseWidget::ViewMode;
    }
    else if (currentWidget() == m_unlockDatabaseWidget) {
        return DatabaseWidget::LockedMode;
    }
    else {
        return DatabaseWidget::EditMode;
    }
}

bool DatabaseWidget::isInEditMode() const
{
    return currentMode() == DatabaseWidget::EditMode;
}

bool DatabaseWidget::isEditWidgetModified() const
{
    if (currentWidget() == m_editEntryWidget) {
        return m_editEntryWidget->hasBeenModified();
    }
    else {
        // other edit widget don't have a hasBeenModified() method yet
        // assume that they already have been modified
        return true;
    }
}

QList<int> DatabaseWidget::splitterSizes() const
{
    return m_splitter->sizes();
}

void DatabaseWidget::setSplitterSizes(const QList<int>& sizes)
{
    m_splitter->setSizes(sizes);
}

QList<int> DatabaseWidget::entryHeaderViewSizes() const
{
    QList<int> sizes;

    for (int i = 0; i < m_entryView->header()->count(); i++) {
        sizes.append(m_entryView->header()->sectionSize(i));
    }

    return sizes;
}

void DatabaseWidget::setEntryViewHeaderSizes(const QList<int>& sizes)
{
    if (sizes.size() != m_entryView->header()->count()) {
        Q_ASSERT(false);
        return;
    }

    for (int i = 0; i < sizes.size(); i++) {
        m_entryView->header()->resizeSection(i, sizes[i]);
    }
}

void DatabaseWidget::clearAllWidgets()
{
    m_editEntryWidget->clear();
    m_historyEditEntryWidget->clear();
    m_editGroupWidget->clear();
}

void DatabaseWidget::emitCurrentModeChanged()
{
    Q_EMIT currentModeChanged(currentMode());
}

Database* DatabaseWidget::database()
{
    return m_db;
}

void DatabaseWidget::createEntry()
{
    if (!m_groupView->currentGroup()) {
        Q_ASSERT(false);
        return;
    }

    m_newEntry = new Entry();
    m_newEntry->setUuid(Uuid::random());
    m_newEntry->setUsername(m_db->metadata()->defaultUserName());
    m_newParent = m_groupView->currentGroup();
    setIconFromParent();
    switchToEntryEdit(m_newEntry, true);
}

void DatabaseWidget::setIconFromParent()
{
    if (!config()->get("UseGroupIconOnEntryCreation").toBool()) {
        return;
    }

    if (m_newParent->iconNumber() == Group::DefaultIconNumber && m_newParent->iconUuid().isNull()) {
        return;
    }

    if (m_newParent->iconUuid().isNull()) {
        m_newEntry->setIcon(m_newParent->iconNumber());
    }
    else {
        m_newEntry->setIcon(m_newParent->iconUuid());
    }
}

void DatabaseWidget::replaceDatabase(Database* db)
{
    Database* oldDb = m_db;
    m_db = db;
    m_groupView->changeDatabase(m_db);
    Q_EMIT databaseChanged(m_db);
    delete oldDb;
}

void DatabaseWidget::cloneEntry()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    Entry* entry = currentEntry->clone(Entry::CloneNewUuid | Entry::CloneResetTimeInfo);
    entry->setGroup(currentEntry->group());
    m_entryView->setFocus();
    m_entryView->setCurrentEntry(entry);
}

void DatabaseWidget::deleteEntries()
{
    const QModelIndexList selected = m_entryView->selectionModel()->selectedRows();

    if (selected.isEmpty()) {
        Q_ASSERT(false);
        return;
    }

    // get all entry pointers as the indexes change when removing multiple entries
    QList<Entry*> selectedEntries;
    for (const QModelIndex& index : selected) {
        selectedEntries.append(m_entryView->entryFromIndex(index));
    }

    bool inRecylceBin = Tools::hasChild(m_db->metadata()->recycleBin(), selectedEntries.first());
    if (inRecylceBin || !m_db->metadata()->recycleBinEnabled()) {
        QMessageBox::StandardButton result;

        if (selected.size() == 1) {
            result = MessageBox::question(
                this, tr("Delete entry?"),
                tr("Do you really want to delete the entry \"%1\" for good?")
                .arg(selectedEntries.first()->title()),
                QMessageBox::Yes | QMessageBox::No);
        }
        else {
            result = MessageBox::question(
                this, tr("Delete entries?"),
                tr("Do you really want to delete %1 entries for good?")
                .arg(selected.size()),
                QMessageBox::Yes | QMessageBox::No);
        }

        if (result == QMessageBox::Yes) {
            for (Entry* entry : asConst(selectedEntries)) {
                delete entry;
            }
        }
    }
    else {
        QMessageBox::StandardButton result;

        if (selected.size() == 1) {
            result = MessageBox::question(
                this, tr("Move entry to recycle bin?"),
                tr("Do you really want to move entry \"%1\" to the recycle bin?")
                .arg(selectedEntries.first()->title()),
                QMessageBox::Yes | QMessageBox::No);
        }
        else {
            result = MessageBox::question(
                this, tr("Move entries to recycle bin?"),
                tr("Do you really want to move %n entry(s) to the recycle bin?", 0, selected.size()),
                QMessageBox::Yes | QMessageBox::No);
        }

        if (result == QMessageBox::No) {
            return;
        }

        for (Entry* entry : asConst(selectedEntries)) {
            m_db->recycleEntry(entry);
        }
    }
}

void DatabaseWidget::copyTitle()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->title());
}

void DatabaseWidget::copyUsername()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->username());
}

void DatabaseWidget::copyPassword()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->password());
}

void DatabaseWidget::copyURL()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->url());
}

void DatabaseWidget::copyNotes()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->notes());
}

void DatabaseWidget::copyAttribute(QAction* action)
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->attributes()->value(action->text()));
}

void DatabaseWidget::setClipboardTextAndMinimize(const QString& text)
{
    clipboard()->setText(text);
    if (config()->get("MinimizeOnCopy").toBool()) {
        window()->showMinimized();
    }
}

void DatabaseWidget::performAutoType()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    autoType()->performAutoType(currentEntry, window());
}

void DatabaseWidget::openUrl()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    openUrlForEntry(currentEntry);
}

void DatabaseWidget::openUrlForEntry(Entry* entry)
{
    QString urlString = entry->resolvePlaceholders(entry->url());
    if (urlString.isEmpty()) {
        return;
    }

    if (urlString.startsWith("cmd://")) {
        if (urlString.length() > 6) {
            QProcess::startDetached(urlString.mid(6));
        }
    }
    else {
        QUrl url = QUrl::fromUserInput(urlString);
        QDesktopServices::openUrl(url);
    }
}

void DatabaseWidget::createGroup()
{
    if (!m_groupView->currentGroup()) {
        Q_ASSERT(false);
        return;
    }

    m_newGroup = new Group();
    m_newGroup->setUuid(Uuid::random());
    m_newParent = m_groupView->currentGroup();
    switchToGroupEdit(m_newGroup, true);
}

void DatabaseWidget::deleteGroup()
{
    Group* currentGroup = m_groupView->currentGroup();
    if (!currentGroup || !canDeleteCurrentGroup()) {
        Q_ASSERT(false);
        return;
    }

    bool inRecylceBin = Tools::hasChild(m_db->metadata()->recycleBin(), currentGroup);
    bool isRecycleBin = (currentGroup == m_db->metadata()->recycleBin());
    bool isRecycleBinSubgroup = Tools::hasChild(currentGroup, m_db->metadata()->recycleBin());
    if (inRecylceBin || isRecycleBin || isRecycleBinSubgroup || !m_db->metadata()->recycleBinEnabled()) {
        QMessageBox::StandardButton result = MessageBox::question(
            this, tr("Delete group?"),
            tr("Do you really want to delete the group \"%1\" for good?")
            .arg(currentGroup->name()),
            QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::Yes) {
            delete currentGroup;
        }
    }
    else {
        m_db->recycleGroup(currentGroup);
    }
}

int DatabaseWidget::addWidget(QWidget* w)
{
    w->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    int index = QStackedWidget::addWidget(w);

    adjustSize();

    return index;
}

void DatabaseWidget::setCurrentIndex(int index)
{
    // use setCurrentWidget() instead
    // index is not reliable
    Q_UNUSED(index);
    Q_ASSERT(false);
}

void DatabaseWidget::setCurrentWidget(QWidget* widget)
{
    if (currentWidget()) {
        currentWidget()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    }

    QStackedWidget::setCurrentWidget(widget);

    if (currentWidget()) {
        currentWidget()->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    adjustSize();
}

void DatabaseWidget::switchToView(bool accepted)
{
    if (m_newGroup) {
        if (accepted) {
            m_newGroup->setParent(m_newParent);
            m_groupView->setCurrentGroup(m_newGroup);
            m_groupView->expandGroup(m_newParent);
        }
        else {
            delete m_newGroup;
        }

        m_newGroup = nullptr;
        m_newParent = nullptr;
    }
    else if (m_newEntry) {
        if (accepted) {
            m_newEntry->setGroup(m_newParent);
            m_entryView->setFocus();
            m_entryView->setCurrentEntry(m_newEntry);
        }
        else {
            delete m_newEntry;
        }

        m_newEntry = nullptr;
        m_newParent = nullptr;
    }

    setCurrentWidget(m_mainWidget);
}

void DatabaseWidget::switchToHistoryView(Entry* entry)
{
    m_historyEditEntryWidget->loadEntry(entry, false, true, m_editEntryWidget->entryTitle(), m_db);
    setCurrentWidget(m_historyEditEntryWidget);
}

void DatabaseWidget::switchBackToEntryEdit()
{
    setCurrentWidget(m_editEntryWidget);
}

void DatabaseWidget::switchToEntryEdit(Entry* entry)
{
    switchToEntryEdit(entry, false);
}

void DatabaseWidget::switchToEntryEdit(Entry* entry, bool create)
{
    Group* group = m_groupView->currentGroup();
    if (!group) {
        Q_ASSERT(m_entryView->inEntryListMode());
        group = m_lastGroup;
    }
    Q_ASSERT(group);

    m_editEntryWidget->loadEntry(entry, create, false, group->name(), m_db);
    setCurrentWidget(m_editEntryWidget);
}

void DatabaseWidget::switchToGroupEdit(Group* group, bool create)
{
    m_editGroupWidget->loadGroup(group, create, m_db);
    setCurrentWidget(m_editGroupWidget);
}

void DatabaseWidget::updateMasterKey(bool accepted)
{
    if (accepted) {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        bool result = m_db->setKey(m_changeMasterKeyWidget->newMasterKey());
        QApplication::restoreOverrideCursor();

        if (!result) {
            MessageBox::critical(this, tr("Error"), tr("Unable to calculate master key"));
            return;
        }
    }
    else if (!m_db->hasKey()) {
        Q_EMIT closeRequest();
        return;
    }

    setCurrentWidget(m_mainWidget);
}

void DatabaseWidget::openDatabase(bool accepted)
{
    if (accepted) {
        replaceDatabase(static_cast<DatabaseOpenWidget*>(sender())->database());
        setCurrentWidget(m_mainWidget);
        Q_EMIT unlockedDatabase();

        // We won't need those anymore and KeePass1OpenWidget closes
        // the file in its dtor.
        delete m_databaseOpenWidget;
        m_databaseOpenWidget = nullptr;
        delete m_keepass1OpenWidget;
        m_keepass1OpenWidget = nullptr;
    }
    else {
        if (m_databaseOpenWidget->database()) {
            delete m_databaseOpenWidget->database();
        }
        Q_EMIT closeRequest();
    }
}

void DatabaseWidget::unlockDatabase(bool accepted)
{
    if (!accepted) {
        Q_EMIT closeRequest();
        return;
    }

    replaceDatabase(static_cast<DatabaseOpenWidget*>(sender())->database());

    const QList<Group*> groups = m_db->rootGroup()->groupsRecursive(true);
    for (Group* group : groups) {
        if (group->uuid() == m_groupBeforeLock) {
            m_groupView->setCurrentGroup(group);
            break;
        }
    }

    m_groupBeforeLock = Uuid();
    setCurrentWidget(m_mainWidget);
    m_unlockDatabaseWidget->clearForms();
    Q_EMIT unlockedDatabase();
}

void DatabaseWidget::entryActivationSignalReceived(Entry* entry, EntryModel::ModelColumn column)
{
    if (column == EntryModel::Url && !entry->url().isEmpty()) {
        openUrlForEntry(entry);
    }
    else {
        switchToEntryEdit(entry);
    }
}

void DatabaseWidget::switchToEntryEdit()
{
    Entry* entry = m_entryView->currentEntry();
    Q_ASSERT(entry);
    if (!entry) {
        return;
    }

    switchToEntryEdit(entry, false);
}

void DatabaseWidget::switchToGroupEdit()
{
    Group* group = m_groupView->currentGroup();
    Q_ASSERT(group);
    if (!group) {
        return;
    }

    switchToGroupEdit(group, false);
}

void DatabaseWidget::switchToMasterKeyChange()
{
    m_changeMasterKeyWidget->clearForms();
    setCurrentWidget(m_changeMasterKeyWidget);
}

void DatabaseWidget::switchToDatabaseSettings()
{
    m_databaseSettingsWidget->load(m_db);
    setCurrentWidget(m_databaseSettingsWidget);
}

void DatabaseWidget::switchToOpenDatabase(const QString& fileName)
{
    updateFilename(fileName);
    m_databaseOpenWidget->load(fileName);
    setCurrentWidget(m_databaseOpenWidget);
}

void DatabaseWidget::switchToOpenDatabase(const QString& fileName, const QString& password,
                                          const QString& keyFile)
{
    updateFilename(fileName);
    switchToOpenDatabase(fileName);
    m_databaseOpenWidget->enterKey(password, keyFile);
}

void DatabaseWidget::switchToImportKeepass1(const QString& fileName)
{
    updateFilename(fileName);
    m_keepass1OpenWidget->load(fileName);
    setCurrentWidget(m_keepass1OpenWidget);
}

void DatabaseWidget::search(const QString& searchtext)
{
    if (searchtext.isEmpty())
    {
        endSearch();
        return;
    }

    Q_EMIT searchModeAboutToActivate();

    if (!isInSearchMode())
    {
        m_lastGroup = m_groupView->currentGroup();
        Q_ASSERT(m_lastGroup);
    }

    Group* searchGroup = m_searchCurrentGroup ? m_lastGroup : m_db->rootGroup();
    Qt::CaseSensitivity sensitivity = m_searchCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    QList<Entry*> searchResult = EntrySearcher().search(searchtext, searchGroup, sensitivity);

    m_entryView->setEntryList(searchResult);
    m_lastSearchText = searchtext;

    // Display a label detailing our search results
    if (searchResult.size() > 0) {
        m_searchingLabel->setText(tr("Search Results (%1)").arg(searchResult.size()));
    }
    else {
        m_searchingLabel->setText(tr("No Results"));
    }

    m_searchingLabel->setVisible(true);

    Q_EMIT searchModeActivated();
}

void DatabaseWidget::setSearchCaseSensitive(bool state)
{
    m_searchCaseSensitive = state;

    if (isInSearchMode())
        search(m_lastSearchText);
}

void DatabaseWidget::setSearchCurrentGroup(bool state)
{
    m_searchCurrentGroup = state;

    if (isInSearchMode())
        search(m_lastSearchText);
}

QString DatabaseWidget::getCurrentSearch()
{
    return m_lastSearchText;
}

void DatabaseWidget::endSearch()
{
    if (isInSearchMode())
    {
        Q_ASSERT(m_lastGroup);

        Q_EMIT listModeAboutToActivate();

        m_groupView->setCurrentGroup(m_lastGroup);
        m_entryView->setGroup(m_lastGroup);

        Q_EMIT listModeActivated();
    }

    m_searchingLabel->setVisible(false);
    m_searchingLabel->setText(tr("Searching..."));

    m_lastSearchText.clear();
}

void DatabaseWidget::emitGroupContextMenuRequested(const QPoint& pos)
{
    Q_EMIT groupContextMenuRequested(m_groupView->viewport()->mapToGlobal(pos));
}

void DatabaseWidget::emitEntryContextMenuRequested(const QPoint& pos)
{
    Q_EMIT entryContextMenuRequested(m_entryView->viewport()->mapToGlobal(pos));
}

bool DatabaseWidget::dbHasKey() const
{
    return m_db->hasKey();
}

bool DatabaseWidget::canDeleteCurrentGroup() const
{
    bool isRootGroup = m_db->rootGroup() == m_groupView->currentGroup();
    return !isRootGroup;
}

bool DatabaseWidget::isInSearchMode() const
{
    return m_entryView->inEntryListMode();
}

void DatabaseWidget::clearLastGroup(Group* group)
{
    if (group) {
        m_lastGroup = nullptr;
    }
}

void DatabaseWidget::lock()
{
    Q_ASSERT(currentMode() != DatabaseWidget::LockedMode);

    if (m_groupView->currentGroup()) {
        m_groupBeforeLock = m_groupView->currentGroup()->uuid();
    }
    else {
        m_groupBeforeLock = m_db->rootGroup()->uuid();
    }

    clearAllWidgets();
    m_unlockDatabaseWidget->load(m_filename);
    setCurrentWidget(m_unlockDatabaseWidget);
    Database* newDb = new Database();
    newDb->metadata()->setName(m_db->metadata()->name());
    replaceDatabase(newDb);
}

void DatabaseWidget::updateFilename(const QString& fileName)
{
    m_filename = fileName;
}

int DatabaseWidget::numberOfSelectedEntries() const
{
    return m_entryView->numberOfSelectedEntries();
}

QStringList DatabaseWidget::customEntryAttributes() const
{
    Entry* entry = m_entryView->currentEntry();
    if (!entry) {
        return QStringList();
    }

    return entry->attributes()->customKeys();
}

bool DatabaseWidget::isGroupSelected() const
{
    return m_groupView->currentGroup() != nullptr;
}

bool DatabaseWidget::currentEntryHasTitle()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return false;
    }
    return !currentEntry->title().isEmpty();
}

bool DatabaseWidget::currentEntryHasUsername()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return false;
    }
    return !currentEntry->username().isEmpty();
}

bool DatabaseWidget::currentEntryHasPassword()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return false;
    }
    return !currentEntry->password().isEmpty();
}

bool DatabaseWidget::currentEntryHasUrl()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return false;
    }
    return !currentEntry->url().isEmpty();
}

bool DatabaseWidget::currentEntryHasNotes()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return false;
    }
    return !currentEntry->notes().isEmpty();
}
