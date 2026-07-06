#pragma once

#include <QObject>
#include <QString>

#include "GCQ/MetaClient.h"
#include "net/MetaClientTypes.h"

class QTimer;

// Owns the legacy MetaClient object on a worker thread. The old client expects
// periodic polling, so pump() drains chat/member state and emits Qt-friendly
// snapshots instead of exposing legacy containers to the UI.
class MetaClientWorker final : public QObject {
    Q_OBJECT

public:
    explicit MetaClientWorker(QObject *parent = nullptr);
    ~MetaClientWorker() override;

public slots:
    void connectAndLogin(const QString &address, int port, const QString &account, const QString &password);
    void sendChatMessage(const QString &message);
    void requestRooms();
    void joinRoom(quint32 roomId, const QString &password, const QString &name);
    void createRoom(const QString &name, const QString &password);
    void requestFriends();
    void requestFleet();
    void requestStaff();
    void requestServers(const QString &nameFilter, quint32 gameId, quint32 type);
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

private slots:
    void pump();

private:
    bool selectDefaultGame();
    bool joinDefaultRoom();
    void ensurePumpTimer();
    void startPumpTimer();
    void stopPumpTimer();
    void drainChat();
    void emitRoomMembersIfChanged();
    static ChatRoomInfo toChatRoomInfo(const MetaClient::Room &room);
    static ServerInfo toServerInfo(const MetaClient::Server &server);
    static GameLinks toGameLinks(const MetaClient::Game &game);
    static RoomMember toRoomMember(const MetaClient::ShortProfile &profile);

    static QString resultMessage(int result);
    static QString toQString(const CharString &value);

    MetaClient m_meta;
    QTimer *m_pumpTimer = nullptr;
    quint32 m_preferredGameId = 1;
    quint32 m_preferredRoomId = 0;
    quint32 m_activeRoomId = 0;
    int m_nextChatIndex = 0;
    QString m_memberSnapshotKey;
};
