#pragma once

#include <QObject>

#include "net/MetaClientTypes.h"

class MetaClientWorker;
class QThread;

class MetaClientBridge final : public QObject {
    Q_OBJECT

public:
    explicit MetaClientBridge(QObject *parent = nullptr);
    ~MetaClientBridge() override;

    // UI-thread facade for the legacy MetaClient. Calls are forwarded to
    // MetaClientWorker on its own thread; results come back through signals.
    void connectAndLogin(const QString &address, int port, const QString &account, const QString &password);
    void sendChatMessage(const QString &message);
    void requestRooms();
    void joinRoom(quint32 roomId, const QString &password, const QString &name);
    void createRoom(const QString &name, const QString &password);
    void requestFriends();
    void requestFleet();
    void requestStaff();
    void requestServers(const QString &nameFilter = {}, quint32 gameId = 0, quint32 type = 0);
    void findProfiles(const QString &nameFilter);
    void requestIgnores();
    void addFriend(quint32 userId, const QString &name);
    void deleteFriend(quint32 userId, const QString &name);
    void addIgnore(quint32 userId, const QString &name);
    void deleteIgnore(quint32 userId, const QString &name);
    void changeName(const QString &name);
    void shutdown();

signals:
    void connectionStateChanged(const QString &state);
    void loginSucceeded(const QString &displayName, quint32 flags, quint32 sessionId);
    void loginFailed(int result, const QString &message);
    void gameSelected(const QString &name, quint32 gameId);
    void gameLinksChanged(const GameLinks &links);
    void roomJoined(quint32 roomId, const QString &name);
    void roomsChanged(const QList<ChatRoomInfo> &rooms);
    void chatMessagesReceived(const QList<ChatMessage> &messages);
    void roomMembersChanged(const QList<RoomMember> &members);
    void friendsChanged(const QList<RoomMember> &friends);
    void fleetChanged(const QList<RoomMember> &members);
    void staffChanged(const QList<RoomMember> &staff);
    void serversChanged(const QList<ServerInfo> &servers);
    void profilesFound(const QString &nameFilter, const QList<RoomMember> &profiles);
    void ignoresChanged(const QList<RoomMember> &ignores);
    void profileChanged(const QString &displayName, quint32 flags);
    void actionSucceeded(const QString &context, const QString &message);
    void errorOccurred(const QString &context, const QString &message);

private:
    QThread *m_thread = nullptr;
    MetaClientWorker *m_worker = nullptr;
};
