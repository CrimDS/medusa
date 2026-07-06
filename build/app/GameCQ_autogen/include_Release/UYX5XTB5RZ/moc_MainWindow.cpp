/****************************************************************************
** Meta object code from reading C++ file 'MainWindow.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../app/ui/MainWindow.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MainWindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto MainWindow::qt_create_metaobjectdata<qt_meta_tag_ZN10MainWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MainWindow",
        "setActiveTab",
        "",
        "index",
        "postLocalChatLine",
        "showLoginDialog",
        "onLoginSucceeded",
        "displayName",
        "flags",
        "sessionId",
        "onLoginFailed",
        "result",
        "message",
        "onGameSelected",
        "name",
        "gameId",
        "onGameLinksChanged",
        "GameLinks",
        "links",
        "onRoomJoined",
        "roomId",
        "onProfileChanged",
        "onRoomsChanged",
        "QList<ChatRoomInfo>",
        "rooms",
        "onChatMessagesReceived",
        "QList<ChatMessage>",
        "messages",
        "onRoomMembersChanged",
        "QList<RoomMember>",
        "members",
        "onFriendsChanged",
        "friends",
        "onFleetChanged",
        "onStaffChanged",
        "staff",
        "onServersChanged",
        "QList<ServerInfo>",
        "servers",
        "showMemberContextMenu",
        "QPoint",
        "position",
        "showLaunchContextMenu",
        "showChangeNameDialog",
        "showFindUserDialog",
        "showFriendsDialog",
        "showIgnoredUsersDialog",
        "showOptionsDialog",
        "showAboutDialog",
        "showChatCommandsDialog"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'setActiveTab'
        QtMocHelpers::SlotData<void(int)>(1, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Slot 'postLocalChatLine'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showLoginDialog'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onLoginSucceeded'
        QtMocHelpers::SlotData<void(const QString &, quint32, quint32)>(6, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 7 }, { QMetaType::UInt, 8 }, { QMetaType::UInt, 9 },
        }}),
        // Slot 'onLoginFailed'
        QtMocHelpers::SlotData<void(int, const QString &)>(10, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 11 }, { QMetaType::QString, 12 },
        }}),
        // Slot 'onGameSelected'
        QtMocHelpers::SlotData<void(const QString &, quint32)>(13, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 14 }, { QMetaType::UInt, 15 },
        }}),
        // Slot 'onGameLinksChanged'
        QtMocHelpers::SlotData<void(const GameLinks &)>(16, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 17, 18 },
        }}),
        // Slot 'onRoomJoined'
        QtMocHelpers::SlotData<void(quint32, const QString &)>(19, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::UInt, 20 }, { QMetaType::QString, 14 },
        }}),
        // Slot 'onProfileChanged'
        QtMocHelpers::SlotData<void(const QString &, quint32)>(21, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 7 }, { QMetaType::UInt, 8 },
        }}),
        // Slot 'onRoomsChanged'
        QtMocHelpers::SlotData<void(const QList<ChatRoomInfo> &)>(22, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 23, 24 },
        }}),
        // Slot 'onChatMessagesReceived'
        QtMocHelpers::SlotData<void(const QList<ChatMessage> &)>(25, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 26, 27 },
        }}),
        // Slot 'onRoomMembersChanged'
        QtMocHelpers::SlotData<void(const QList<RoomMember> &)>(28, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 29, 30 },
        }}),
        // Slot 'onFriendsChanged'
        QtMocHelpers::SlotData<void(const QList<RoomMember> &)>(31, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 29, 32 },
        }}),
        // Slot 'onFleetChanged'
        QtMocHelpers::SlotData<void(const QList<RoomMember> &)>(33, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 29, 30 },
        }}),
        // Slot 'onStaffChanged'
        QtMocHelpers::SlotData<void(const QList<RoomMember> &)>(34, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 29, 35 },
        }}),
        // Slot 'onServersChanged'
        QtMocHelpers::SlotData<void(const QList<ServerInfo> &)>(36, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 37, 38 },
        }}),
        // Slot 'showMemberContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(39, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 40, 41 },
        }}),
        // Slot 'showLaunchContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(42, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 40, 41 },
        }}),
        // Slot 'showChangeNameDialog'
        QtMocHelpers::SlotData<void()>(43, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showFindUserDialog'
        QtMocHelpers::SlotData<void()>(44, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showFriendsDialog'
        QtMocHelpers::SlotData<void()>(45, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showIgnoredUsersDialog'
        QtMocHelpers::SlotData<void()>(46, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showOptionsDialog'
        QtMocHelpers::SlotData<void()>(47, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showAboutDialog'
        QtMocHelpers::SlotData<void()>(48, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showChatCommandsDialog'
        QtMocHelpers::SlotData<void()>(49, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MainWindow, qt_meta_tag_ZN10MainWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10MainWindowE_t>.metaTypes,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setActiveTab((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->postLocalChatLine(); break;
        case 2: _t->showLoginDialog(); break;
        case 3: _t->onLoginSucceeded((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[3]))); break;
        case 4: _t->onLoginFailed((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 5: _t->onGameSelected((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[2]))); break;
        case 6: _t->onGameLinksChanged((*reinterpret_cast<std::add_pointer_t<GameLinks>>(_a[1]))); break;
        case 7: _t->onRoomJoined((*reinterpret_cast<std::add_pointer_t<quint32>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 8: _t->onProfileChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[2]))); break;
        case 9: _t->onRoomsChanged((*reinterpret_cast<std::add_pointer_t<QList<ChatRoomInfo>>>(_a[1]))); break;
        case 10: _t->onChatMessagesReceived((*reinterpret_cast<std::add_pointer_t<QList<ChatMessage>>>(_a[1]))); break;
        case 11: _t->onRoomMembersChanged((*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[1]))); break;
        case 12: _t->onFriendsChanged((*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[1]))); break;
        case 13: _t->onFleetChanged((*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[1]))); break;
        case 14: _t->onStaffChanged((*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[1]))); break;
        case 15: _t->onServersChanged((*reinterpret_cast<std::add_pointer_t<QList<ServerInfo>>>(_a[1]))); break;
        case 16: _t->showMemberContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 17: _t->showLaunchContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 18: _t->showChangeNameDialog(); break;
        case 19: _t->showFindUserDialog(); break;
        case 20: _t->showFriendsDialog(); break;
        case 21: _t->showIgnoredUsersDialog(); break;
        case 22: _t->showOptionsDialog(); break;
        case 23: _t->showAboutDialog(); break;
        case 24: _t->showChatCommandsDialog(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< GameLinks >(); break;
            }
            break;
        case 9:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<ChatRoomInfo> >(); break;
            }
            break;
        case 10:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<ChatMessage> >(); break;
            }
            break;
        case 11:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<RoomMember> >(); break;
            }
            break;
        case 12:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<RoomMember> >(); break;
            }
            break;
        case 13:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<RoomMember> >(); break;
            }
            break;
        case 14:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<RoomMember> >(); break;
            }
            break;
        case 15:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<ServerInfo> >(); break;
            }
            break;
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 25)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 25;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 25)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 25;
    }
    return _id;
}
QT_WARNING_POP
