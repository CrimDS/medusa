/****************************************************************************
** Meta object code from reading C++ file 'MetaClientWorker.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../app/net/MetaClientWorker.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MetaClientWorker.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN16MetaClientWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto MetaClientWorker::qt_create_metaobjectdata<qt_meta_tag_ZN16MetaClientWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MetaClientWorker",
        "connectionStateChanged",
        "",
        "state",
        "loginSucceeded",
        "displayName",
        "flags",
        "sessionId",
        "loginFailed",
        "result",
        "message",
        "gameSelected",
        "name",
        "gameId",
        "gameLinksChanged",
        "GameLinks",
        "links",
        "roomJoined",
        "roomId",
        "roomsChanged",
        "QList<ChatRoomInfo>",
        "rooms",
        "chatMessagesReceived",
        "QList<ChatMessage>",
        "messages",
        "roomMembersChanged",
        "QList<RoomMember>",
        "members",
        "friendsChanged",
        "friends",
        "fleetChanged",
        "staffChanged",
        "staff",
        "serversChanged",
        "QList<ServerInfo>",
        "servers",
        "profilesFound",
        "nameFilter",
        "profiles",
        "ignoresChanged",
        "ignores",
        "profileChanged",
        "actionSucceeded",
        "context",
        "errorOccurred",
        "connectAndLogin",
        "address",
        "port",
        "account",
        "password",
        "sendChatMessage",
        "requestRooms",
        "joinRoom",
        "createRoom",
        "requestFriends",
        "requestFleet",
        "requestStaff",
        "requestServers",
        "type",
        "findProfiles",
        "requestIgnores",
        "addFriend",
        "userId",
        "deleteFriend",
        "addIgnore",
        "deleteIgnore",
        "changeName",
        "shutdown",
        "pump"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'connectionStateChanged'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'loginSucceeded'
        QtMocHelpers::SignalData<void(const QString &, quint32, quint32)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::UInt, 6 }, { QMetaType::UInt, 7 },
        }}),
        // Signal 'loginFailed'
        QtMocHelpers::SignalData<void(int, const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 9 }, { QMetaType::QString, 10 },
        }}),
        // Signal 'gameSelected'
        QtMocHelpers::SignalData<void(const QString &, quint32)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 12 }, { QMetaType::UInt, 13 },
        }}),
        // Signal 'gameLinksChanged'
        QtMocHelpers::SignalData<void(const GameLinks &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 15, 16 },
        }}),
        // Signal 'roomJoined'
        QtMocHelpers::SignalData<void(quint32, const QString &)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::UInt, 18 }, { QMetaType::QString, 12 },
        }}),
        // Signal 'roomsChanged'
        QtMocHelpers::SignalData<void(const QList<ChatRoomInfo> &)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 20, 21 },
        }}),
        // Signal 'chatMessagesReceived'
        QtMocHelpers::SignalData<void(const QList<ChatMessage> &)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 23, 24 },
        }}),
        // Signal 'roomMembersChanged'
        QtMocHelpers::SignalData<void(const QList<RoomMember> &)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 26, 27 },
        }}),
        // Signal 'friendsChanged'
        QtMocHelpers::SignalData<void(const QList<RoomMember> &)>(28, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 26, 29 },
        }}),
        // Signal 'fleetChanged'
        QtMocHelpers::SignalData<void(const QList<RoomMember> &)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 26, 27 },
        }}),
        // Signal 'staffChanged'
        QtMocHelpers::SignalData<void(const QList<RoomMember> &)>(31, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 26, 32 },
        }}),
        // Signal 'serversChanged'
        QtMocHelpers::SignalData<void(const QList<ServerInfo> &)>(33, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 34, 35 },
        }}),
        // Signal 'profilesFound'
        QtMocHelpers::SignalData<void(const QString &, const QList<RoomMember> &)>(36, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 37 }, { 0x80000000 | 26, 38 },
        }}),
        // Signal 'ignoresChanged'
        QtMocHelpers::SignalData<void(const QList<RoomMember> &)>(39, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 26, 40 },
        }}),
        // Signal 'profileChanged'
        QtMocHelpers::SignalData<void(const QString &, quint32)>(41, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::UInt, 6 },
        }}),
        // Signal 'actionSucceeded'
        QtMocHelpers::SignalData<void(const QString &, const QString &)>(42, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 43 }, { QMetaType::QString, 10 },
        }}),
        // Signal 'errorOccurred'
        QtMocHelpers::SignalData<void(const QString &, const QString &)>(44, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 43 }, { QMetaType::QString, 10 },
        }}),
        // Slot 'connectAndLogin'
        QtMocHelpers::SlotData<void(const QString &, int, const QString &, const QString &)>(45, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 46 }, { QMetaType::Int, 47 }, { QMetaType::QString, 48 }, { QMetaType::QString, 49 },
        }}),
        // Slot 'sendChatMessage'
        QtMocHelpers::SlotData<void(const QString &)>(50, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Slot 'requestRooms'
        QtMocHelpers::SlotData<void()>(51, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'joinRoom'
        QtMocHelpers::SlotData<void(quint32, const QString &, const QString &)>(52, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::UInt, 18 }, { QMetaType::QString, 49 }, { QMetaType::QString, 12 },
        }}),
        // Slot 'createRoom'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(53, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 12 }, { QMetaType::QString, 49 },
        }}),
        // Slot 'requestFriends'
        QtMocHelpers::SlotData<void()>(54, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestFleet'
        QtMocHelpers::SlotData<void()>(55, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestStaff'
        QtMocHelpers::SlotData<void()>(56, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestServers'
        QtMocHelpers::SlotData<void(const QString &, quint32, quint32)>(57, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 37 }, { QMetaType::UInt, 13 }, { QMetaType::UInt, 58 },
        }}),
        // Slot 'findProfiles'
        QtMocHelpers::SlotData<void(const QString &)>(59, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 37 },
        }}),
        // Slot 'requestIgnores'
        QtMocHelpers::SlotData<void()>(60, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'addFriend'
        QtMocHelpers::SlotData<void(quint32, const QString &)>(61, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::UInt, 62 }, { QMetaType::QString, 12 },
        }}),
        // Slot 'deleteFriend'
        QtMocHelpers::SlotData<void(quint32, const QString &)>(63, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::UInt, 62 }, { QMetaType::QString, 12 },
        }}),
        // Slot 'addIgnore'
        QtMocHelpers::SlotData<void(quint32, const QString &)>(64, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::UInt, 62 }, { QMetaType::QString, 12 },
        }}),
        // Slot 'deleteIgnore'
        QtMocHelpers::SlotData<void(quint32, const QString &)>(65, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::UInt, 62 }, { QMetaType::QString, 12 },
        }}),
        // Slot 'changeName'
        QtMocHelpers::SlotData<void(const QString &)>(66, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 12 },
        }}),
        // Slot 'shutdown'
        QtMocHelpers::SlotData<void()>(67, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'pump'
        QtMocHelpers::SlotData<void()>(68, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MetaClientWorker, qt_meta_tag_ZN16MetaClientWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MetaClientWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16MetaClientWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16MetaClientWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN16MetaClientWorkerE_t>.metaTypes,
    nullptr
} };

void MetaClientWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MetaClientWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->connectionStateChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->loginSucceeded((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[3]))); break;
        case 2: _t->loginFailed((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->gameSelected((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[2]))); break;
        case 4: _t->gameLinksChanged((*reinterpret_cast<std::add_pointer_t<GameLinks>>(_a[1]))); break;
        case 5: _t->roomJoined((*reinterpret_cast<std::add_pointer_t<quint32>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 6: _t->roomsChanged((*reinterpret_cast<std::add_pointer_t<QList<ChatRoomInfo>>>(_a[1]))); break;
        case 7: _t->chatMessagesReceived((*reinterpret_cast<std::add_pointer_t<QList<ChatMessage>>>(_a[1]))); break;
        case 8: _t->roomMembersChanged((*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[1]))); break;
        case 9: _t->friendsChanged((*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[1]))); break;
        case 10: _t->fleetChanged((*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[1]))); break;
        case 11: _t->staffChanged((*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[1]))); break;
        case 12: _t->serversChanged((*reinterpret_cast<std::add_pointer_t<QList<ServerInfo>>>(_a[1]))); break;
        case 13: _t->profilesFound((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[2]))); break;
        case 14: _t->ignoresChanged((*reinterpret_cast<std::add_pointer_t<QList<RoomMember>>>(_a[1]))); break;
        case 15: _t->profileChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[2]))); break;
        case 16: _t->actionSucceeded((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 17: _t->errorOccurred((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 18: _t->connectAndLogin((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 19: _t->sendChatMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 20: _t->requestRooms(); break;
        case 21: _t->joinRoom((*reinterpret_cast<std::add_pointer_t<quint32>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 22: _t->createRoom((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 23: _t->requestFriends(); break;
        case 24: _t->requestFleet(); break;
        case 25: _t->requestStaff(); break;
        case 26: _t->requestServers((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<quint32>>(_a[3]))); break;
        case 27: _t->findProfiles((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 28: _t->requestIgnores(); break;
        case 29: _t->addFriend((*reinterpret_cast<std::add_pointer_t<quint32>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 30: _t->deleteFriend((*reinterpret_cast<std::add_pointer_t<quint32>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 31: _t->addIgnore((*reinterpret_cast<std::add_pointer_t<quint32>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 32: _t->deleteIgnore((*reinterpret_cast<std::add_pointer_t<quint32>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 33: _t->changeName((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 34: _t->shutdown(); break;
        case 35: _t->pump(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< GameLinks >(); break;
            }
            break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<ChatRoomInfo> >(); break;
            }
            break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<ChatMessage> >(); break;
            }
            break;
        case 8:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<RoomMember> >(); break;
            }
            break;
        case 9:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<RoomMember> >(); break;
            }
            break;
        case 10:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<RoomMember> >(); break;
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
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<ServerInfo> >(); break;
            }
            break;
        case 13:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
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
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QString & )>(_a, &MetaClientWorker::connectionStateChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QString & , quint32 , quint32 )>(_a, &MetaClientWorker::loginSucceeded, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(int , const QString & )>(_a, &MetaClientWorker::loginFailed, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QString & , quint32 )>(_a, &MetaClientWorker::gameSelected, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const GameLinks & )>(_a, &MetaClientWorker::gameLinksChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(quint32 , const QString & )>(_a, &MetaClientWorker::roomJoined, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QList<ChatRoomInfo> & )>(_a, &MetaClientWorker::roomsChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QList<ChatMessage> & )>(_a, &MetaClientWorker::chatMessagesReceived, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QList<RoomMember> & )>(_a, &MetaClientWorker::roomMembersChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QList<RoomMember> & )>(_a, &MetaClientWorker::friendsChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QList<RoomMember> & )>(_a, &MetaClientWorker::fleetChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QList<RoomMember> & )>(_a, &MetaClientWorker::staffChanged, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QList<ServerInfo> & )>(_a, &MetaClientWorker::serversChanged, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QString & , const QList<RoomMember> & )>(_a, &MetaClientWorker::profilesFound, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QList<RoomMember> & )>(_a, &MetaClientWorker::ignoresChanged, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QString & , quint32 )>(_a, &MetaClientWorker::profileChanged, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QString & , const QString & )>(_a, &MetaClientWorker::actionSucceeded, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (MetaClientWorker::*)(const QString & , const QString & )>(_a, &MetaClientWorker::errorOccurred, 17))
            return;
    }
}

const QMetaObject *MetaClientWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MetaClientWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16MetaClientWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int MetaClientWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 36)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 36;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 36)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 36;
    }
    return _id;
}

// SIGNAL 0
void MetaClientWorker::connectionStateChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void MetaClientWorker::loginSucceeded(const QString & _t1, quint32 _t2, quint32 _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2, _t3);
}

// SIGNAL 2
void MetaClientWorker::loginFailed(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}

// SIGNAL 3
void MetaClientWorker::gameSelected(const QString & _t1, quint32 _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2);
}

// SIGNAL 4
void MetaClientWorker::gameLinksChanged(const GameLinks & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void MetaClientWorker::roomJoined(quint32 _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}

// SIGNAL 6
void MetaClientWorker::roomsChanged(const QList<ChatRoomInfo> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void MetaClientWorker::chatMessagesReceived(const QList<ChatMessage> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void MetaClientWorker::roomMembersChanged(const QList<RoomMember> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void MetaClientWorker::friendsChanged(const QList<RoomMember> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void MetaClientWorker::fleetChanged(const QList<RoomMember> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void MetaClientWorker::staffChanged(const QList<RoomMember> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1);
}

// SIGNAL 12
void MetaClientWorker::serversChanged(const QList<ServerInfo> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1);
}

// SIGNAL 13
void MetaClientWorker::profilesFound(const QString & _t1, const QList<RoomMember> & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1, _t2);
}

// SIGNAL 14
void MetaClientWorker::ignoresChanged(const QList<RoomMember> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1);
}

// SIGNAL 15
void MetaClientWorker::profileChanged(const QString & _t1, quint32 _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 15, nullptr, _t1, _t2);
}

// SIGNAL 16
void MetaClientWorker::actionSucceeded(const QString & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 16, nullptr, _t1, _t2);
}

// SIGNAL 17
void MetaClientWorker::errorOccurred(const QString & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 17, nullptr, _t1, _t2);
}
QT_WARNING_POP
