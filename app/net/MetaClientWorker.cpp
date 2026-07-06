#include "net/MetaClientWorker.h"

#include "net/GameProtocolConstants.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

#include "System/Locale.h"

namespace {
constexpr int TcpProbeTimeoutMs = 5000;
constexpr int TcpDisconnectTimeoutMs = 1000;

bool isPublicServerRequest(quint32 gameId, quint32 type)
{
    return type == MetaClient::GAME_SERVER && (gameId == 0 || gameId == kDarkSpaceGameId);
}

bool isPublicServerRow(const MetaClient::Server &server)
{
    return server.type == MetaClient::GAME_SERVER && server.gameId == kDarkSpaceGameId;
}

QString decodeLegacyText(const char *text)
{
    const QByteArray bytes(text ? text : "");
    // Most modern relay text is UTF-8, but older server fields may be local
    // codepage or Latin-1. Prefer UTF-8 and fall back only when decoding fails.
    QString decoded = QString::fromUtf8(bytes);
    if (!decoded.contains(QChar::ReplacementCharacter)) {
        return decoded;
    }

    decoded = QString::fromLocal8Bit(bytes);
    if (!decoded.contains(QChar::ReplacementCharacter)) {
        return decoded;
    }

    return QString::fromLatin1(bytes);
}

QString probeTcpEndpoint(const QString &address, int port)
{
    QTcpSocket socket;
    socket.connectToHost(address, static_cast<quint16>(port));

    if (!socket.waitForConnected(TcpProbeTimeoutMs)) {
        return socket.errorString();
    }

    socket.disconnectFromHost();
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.waitForDisconnected(TcpDisconnectTimeoutMs);
    }

    return {};
}

int darkSpaceDisplayMaxClients(const QString &name, const QString &shortDescription, int reportedMaxClients)
{
    if (name.compare("Hydra", Qt::CaseInsensitive) != 0) {
        return reportedMaxClients;
    }

    static const QRegularExpression sectorsExpression(R"((\d+)\s+Sectors?)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = sectorsExpression.match(shortDescription);
    if (match.hasMatch()) {
        bool ok = false;
        const int sectors = match.captured(1).toInt(&ok);
        if (ok && sectors > 0) {
            return sectors * 64;
        }
    }

    return reportedMaxClients == 64 ? 768 : reportedMaxClients;
}
}

MetaClientWorker::MetaClientWorker(QObject *parent)
    : QObject(parent)
{
}

MetaClientWorker::~MetaClientWorker()
{
    shutdown();
}

void MetaClientWorker::connectAndLogin(const QString &address, int port, const QString &account, const QString &password)
{
    // Reset all session-derived state before opening a new MetaClient
    // connection. The legacy object caches chat and room data internally.
    stopPumpTimer();
    m_meta.close();
    m_meta.flushChat();
    m_activeRoomId = 0;
    m_nextChatIndex = 0;
    m_memberSnapshotKey.clear();

    const QString trimmedAddress = address.trimmed();
    const QByteArray addressBytes = trimmedAddress.toUtf8();
    const QByteArray accountBytes = account.trimmed().toUtf8();
    const QByteArray passwordBytes = password.toUtf8();

    emit connectionStateChanged("Connecting");
    // Probe TCP first so connection failures report a plain network error
    // instead of looking like a failed Medusa key exchange.
    const QString probeError = probeTcpEndpoint(trimmedAddress, port);
    if (!probeError.isEmpty()) {
        emit connectionStateChanged("Disconnected");
        emit loginFailed(MetaClient::RESULT_ERROR,
                         QString("TCP connection to %1:%2 failed: %3")
                             .arg(trimmedAddress)
                             .arg(port)
                             .arg(probeError));
        return;
    }

    const int openResult = m_meta.open(addressBytes.constData(), port);
    if (openResult != MetaClient::RESULT_OKAY) {
        emit connectionStateChanged("Disconnected");
        emit loginFailed(openResult,
                         QString("Connected to %1:%2, but the Medusa key exchange did not complete.")
                             .arg(trimmedAddress)
                             .arg(port));
        return;
    }

    emit connectionStateChanged("Logging in");
    const int loginResult = m_meta.login(accountBytes.constData(), passwordBytes.constData());
    if (loginResult != MetaClient::LOGIN_OKAY) {
        m_meta.close();
        emit connectionStateChanged("Disconnected");
        emit loginFailed(loginResult, resultMessage(loginResult));
        return;
    }

    const MetaClient::Profile &profile = m_meta.profile();
    emit loginSucceeded(toQString(profile.name), profile.flags, profile.sessionId);

    emit connectionStateChanged("Selecting game");
    if (!selectDefaultGame()) {
        emit errorOccurred("MetaClient", "Logged in, but failed to select the Darkspace lobby.");
        emit connectionStateChanged("Online");
        startPumpTimer();
        return;
    }

    emit connectionStateChanged("Joining chat");
    if (!joinDefaultRoom()) {
        emit errorOccurred("MetaClient", "Logged in, but failed to join a lobby chat room.");
        emit connectionStateChanged("Online");
        drainChat();
        startPumpTimer();
        return;
    }

    emit connectionStateChanged("Online");
    drainChat();
    startPumpTimer();
}

void MetaClientWorker::sendChatMessage(const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (!m_meta.loggedIn()) {
        emit errorOccurred("Chat", "You are not logged in.");
        return;
    }

    if (m_activeRoomId == 0) {
        emit errorOccurred("Chat", "No lobby room is joined yet.");
        return;
    }

    const QByteArray messageBytes = trimmed.toUtf8();
    m_meta.sendChat(m_activeRoomId, messageBytes.constData());
    m_meta.update();
    // Old GCQL echoes sent messages through the MetaClient chat queue. Drain
    // immediately so the UI sees the local echo without waiting for the pump.
    drainChat();
}

void MetaClientWorker::requestRooms()
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Rooms", "You are not logged in.");
        emit roomsChanged({});
        return;
    }

    Array<MetaClient::Room> rooms;
    if (m_meta.getRooms(rooms) < 0) {
        emit errorOccurred("Rooms", "Failed to load lobby rooms.");
        emit roomsChanged({});
        return;
    }

    QList<ChatRoomInfo> result;
    for (int i = 0; i < rooms.size(); ++i) {
        result.append(toChatRoomInfo(rooms[i]));
    }

    emit roomsChanged(result);
}

void MetaClientWorker::joinRoom(quint32 roomId, const QString &password, const QString &name)
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Rooms", "You are not logged in.");
        return;
    }

    if (roomId == 0) {
        emit errorOccurred("Rooms", "Invalid room selected.");
        return;
    }

    if (m_activeRoomId == roomId) {
        emit roomJoined(m_activeRoomId, name);
        emitRoomMembersIfChanged();
        return;
    }

    const QByteArray passwordBytes = password.toUtf8();
    if (m_activeRoomId != 0) {
        m_meta.leaveRoom(m_activeRoomId);
    }

    m_meta.sendLocalChat(CharString().format("/Joining '<b>%s</b>'...", name.toUtf8().constData()));
    const dword joinedRoomId = m_meta.joinRoom(roomId, passwordBytes.constData());
    if (joinedRoomId == 0) {
        emit errorOccurred("Rooms", QString("Failed to join %1.").arg(name));
        drainChat();
        return;
    }

    m_activeRoomId = joinedRoomId;
    m_memberSnapshotKey.clear();

    const QString roomName = name.isEmpty() ? QString("Room @%1").arg(roomId) : name;
    m_meta.sendStatus(CharString().format("Chatting in '%s'", roomName.toUtf8().constData()));
    emit roomJoined(m_activeRoomId, roomName);
    drainChat();
    emitRoomMembersIfChanged();
}

void MetaClientWorker::createRoom(const QString &name, const QString &password)
{
    const QString trimmedName = name.trimmed();
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Rooms", "You are not logged in.");
        return;
    }

    if (trimmedName.isEmpty()) {
        emit errorOccurred("Rooms", "Room name is required.");
        return;
    }

    const QByteArray nameBytes = trimmedName.toUtf8();
    const QByteArray passwordBytes = password.toUtf8();

    if (m_activeRoomId != 0) {
        m_meta.leaveRoom(m_activeRoomId);
    }

    const int createdRoomId = m_meta.createRoom(nameBytes.constData(), passwordBytes.constData());
    if (createdRoomId <= 0) {
        emit errorOccurred("Rooms", QString("Failed to create %1.").arg(trimmedName));
        return;
    }

    m_activeRoomId = static_cast<quint32>(createdRoomId);
    m_memberSnapshotKey.clear();

    m_meta.sendLocalChat(CharString().format("/Created '<b>%s</b>'...", nameBytes.constData()));
    m_meta.sendStatus(CharString().format("Chatting in '%s'", nameBytes.constData()));
    emit roomJoined(m_activeRoomId, trimmedName);
    drainChat();
    emitRoomMembersIfChanged();
}

void MetaClientWorker::requestFriends()
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Friends", "You are not logged in.");
        emit friendsChanged({});
        return;
    }

    Array<MetaClient::ShortProfile> friends;
    if (m_meta.getFriends(friends) < 0) {
        emit errorOccurred("Friends", "Failed to load your friend list.");
        emit friendsChanged({});
        return;
    }

    QList<RoomMember> result;
    for (int i = 0; i < friends.size(); ++i) {
        result.append(toRoomMember(friends[i]));
    }

    emit friendsChanged(result);
}

void MetaClientWorker::requestFleet()
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Fleet", "You are not logged in.");
        emit fleetChanged({});
        return;
    }

    const dword clanId = m_meta.profile().clanId;
    if (clanId == 0) {
        emit fleetChanged({});
        return;
    }

    Array<MetaClient::ShortProfile> members;
    if (m_meta.getClan(clanId, members) < 0) {
        emit errorOccurred("Fleet", "Failed to load your fleet roster.");
        emit fleetChanged({});
        return;
    }

    const dword selfId = m_meta.profile().userId;
    QList<RoomMember> result;
    for (int i = 0; i < members.size(); ++i) {
        if (members[i].userId != selfId) {
            result.append(toRoomMember(members[i]));
        }
    }

    emit fleetChanged(result);
}

void MetaClientWorker::requestStaff()
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Staff", "You are not logged in.");
        emit staffChanged({});
        return;
    }

    if ((m_meta.profile().flags & MetaClient::STAFF) == 0) {
        emit staffChanged({});
        return;
    }

    Array<MetaClient::ShortProfile> staff;
    if (m_meta.getStaffOnline(staff) < 0) {
        emit errorOccurred("Staff", "Failed to load online staff.");
        emit staffChanged({});
        return;
    }

    const dword selfId = m_meta.profile().userId;
    QList<RoomMember> result;
    for (int i = 0; i < staff.size(); ++i) {
        if (staff[i].userId != selfId) {
            result.append(toRoomMember(staff[i]));
        }
    }

    emit staffChanged(result);
}

void MetaClientWorker::requestServers(const QString &nameFilter, quint32 gameId, quint32 type)
{
    if (!m_meta.connected()) {
        emit errorOccurred("Servers", "You are not connected to the meta-server.");
        emit serversChanged({});
        return;
    }

    const bool canSeeStaffServers = m_meta.loggedIn() && (m_meta.profile().flags & MetaClient::STAFF) != 0;
    quint32 effectiveGameId = gameId;
    quint32 effectiveType = type;
    if (!canSeeStaffServers && !isPublicServerRequest(effectiveGameId, effectiveType)) {
        // Non-staff clients should never query staff/dev server categories, even
        // if a stale UI control or crafted request asks for them.
        effectiveGameId = 0;
        effectiveType = MetaClient::GAME_SERVER;
    }

    Array<MetaClient::Server> servers;
    const QByteArray filterBytes = nameFilter.trimmed().toUtf8();
    if (m_meta.getServers(filterBytes.constData(), effectiveGameId, effectiveType, servers) < 0) {
        emit errorOccurred("Servers", "Failed to load server list.");
        emit serversChanged({});
        return;
    }

    QList<ServerInfo> result;
    for (int i = 0; i < servers.size(); ++i) {
        if (!canSeeStaffServers && !isPublicServerRow(servers[i])) {
            continue;
        }
        result.append(toServerInfo(servers[i]));
    }

    emit serversChanged(result);
}

void MetaClientWorker::findProfiles(const QString &nameFilter)
{
    const QString trimmed = nameFilter.trimmed();
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Find User", "You are not logged in.");
        return;
    }
    if (trimmed.isEmpty()) {
        emit profilesFound(trimmed, {});
        return;
    }
    if (trimmed.size() < 2) {
        emit errorOccurred("Find User", "Search pattern must be at least 2 characters.");
        return;
    }

    if (trimmed.startsWith('@')) {
        bool ok = false;
        const dword userId = trimmed.mid(1).toUInt(&ok);
        if (!ok || userId == 0) {
            emit errorOccurred("Find User", "Invalid user ID.");
            return;
        }

        MetaClient::Profile profile;
        if (m_meta.getProfile(userId, profile) < 0) {
            emit errorOccurred("Find User", QString("Failed to find user %1.").arg(trimmed));
            return;
        }

        RoomMember result;
        result.userId = profile.userId;
        result.name = toQString(profile.name);
        result.status = toQString(profile.status);
        result.flags = profile.flags;
        emit profilesFound(trimmed, {result});
        return;
    }

    Array<MetaClient::ShortProfile> profiles;
    const QString pattern = QStringLiteral("%") + trimmed + QStringLiteral("%");
    const QByteArray filterBytes = pattern.toUtf8();
    if (m_meta.getProfiles(filterBytes.constData(), profiles) < 0) {
        emit errorOccurred("Find User", QString("Failed to search for %1.").arg(trimmed));
        return;
    }

    QList<RoomMember> result;
    for (int i = 0; i < profiles.size(); ++i) {
        result.append(toRoomMember(profiles[i]));
    }

    emit profilesFound(trimmed, result);
}

void MetaClientWorker::requestIgnores()
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Ignored Users", "You are not logged in.");
        return;
    }

    Array<MetaClient::ShortProfile> ignores;
    if (m_meta.getIgnores(ignores) < 0) {
        emit errorOccurred("Ignored Users", "Failed to load ignored users.");
        return;
    }

    QList<RoomMember> result;
    for (int i = 0; i < ignores.size(); ++i) {
        result.append(toRoomMember(ignores[i]));
    }

    emit ignoresChanged(result);
}

void MetaClientWorker::addFriend(quint32 userId, const QString &name)
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Friends", "You are not logged in.");
        return;
    }
    if (userId == 0) {
        emit errorOccurred("Friends", "Invalid user selected.");
        return;
    }

    if (m_meta.addFriend(userId) < 0) {
        emit errorOccurred("Friends", QString("Failed to add %1 to your friend list.").arg(name));
        return;
    }

    emit actionSucceeded("Friends", QString("Added %1 to your friend list.").arg(name));
    requestFriends();
}

void MetaClientWorker::deleteFriend(quint32 userId, const QString &name)
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Friends", "You are not logged in.");
        return;
    }
    if (userId == 0) {
        emit errorOccurred("Friends", "Invalid user selected.");
        return;
    }

    if (m_meta.deleteFriend(userId) < 0) {
        emit errorOccurred("Friends", QString("Failed to remove %1 from your friend list.").arg(name));
        return;
    }

    emit actionSucceeded("Friends", QString("Removed %1 from your friend list.").arg(name));
    requestFriends();
}

void MetaClientWorker::addIgnore(quint32 userId, const QString &name)
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Ignored Users", "You are not logged in.");
        return;
    }
    if (userId == 0) {
        emit errorOccurred("Ignored Users", "Invalid user selected.");
        return;
    }

    if (m_meta.addIgnore(userId) < 0) {
        emit errorOccurred("Ignored Users", QString("Failed to ignore %1.").arg(name));
        return;
    }

    emit actionSucceeded("Ignored Users", QString("Ignored %1.").arg(name));
    requestIgnores();
}

void MetaClientWorker::deleteIgnore(quint32 userId, const QString &name)
{
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Ignored Users", "You are not logged in.");
        return;
    }
    if (userId == 0) {
        emit errorOccurred("Ignored Users", "Invalid user selected.");
        return;
    }

    if (m_meta.deleteIgnore(userId) < 0) {
        emit errorOccurred("Ignored Users", QString("Failed to remove %1 from ignored users.").arg(name));
        return;
    }

    emit actionSucceeded("Ignored Users", QString("Removed %1 from ignored users.").arg(name));
    requestIgnores();
}

void MetaClientWorker::changeName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!m_meta.loggedIn()) {
        emit errorOccurred("Account", "You are not logged in.");
        return;
    }
    if (trimmed.size() < 2) {
        emit errorOccurred("Account", "Name must be at least 2 characters.");
        return;
    }

    const QByteArray nameBytes = trimmed.toUtf8();
    const int result = m_meta.changeName(nameBytes.constData());
    if (result == MetaClient::LOGIN_OKAY) {
        const MetaClient::Profile &profile = m_meta.profile();
        const QString displayName = toQString(profile.name);
        emit profileChanged(displayName, profile.flags);
        emit actionSucceeded("Account", QString("Name changed to %1. Use this next time you log in.").arg(displayName));
        return;
    }

    if (result == MetaClient::LOGIN_ILLEGAL) {
        emit errorOccurred("Account", "Failed to change name: illegal words or characters.");
    } else if (result == MetaClient::LOGIN_DUPLICATE_LOGIN) {
        emit errorOccurred("Account", "Failed to change name: that name is already being used.");
    } else {
        emit errorOccurred("Account", "Failed to change name. Try again later.");
    }
}

void MetaClientWorker::shutdown()
{
    stopPumpTimer();
    if (m_meta.loggedIn()) {
        m_meta.logoff();
    }
    m_meta.close();
}

void MetaClientWorker::pump()
{
    if (!m_meta.connected()) {
        stopPumpTimer();
        emit connectionStateChanged("Disconnected");
        return;
    }

    if (!m_meta.update()) {
        stopPumpTimer();
        emit connectionStateChanged("Disconnected");
        emit errorOccurred("MetaClient", "The meta-server connection was closed.");
        return;
    }

    drainChat();
    emitRoomMembersIfChanged();
}

bool MetaClientWorker::selectDefaultGame()
{
    Array<MetaClient::Game> games;
    if (m_meta.getGames(games) < 0 || games.size() < 1) {
        return false;
    }

    MetaClient::Game selected = games[0];
    for (int i = 0; i < games.size(); ++i) {
        if (games[i].id == m_preferredGameId) {
            selected = games[i];
            break;
        }
    }

    if (m_meta.selectGame(selected.id) < 0) {
        return false;
    }

    emit gameLinksChanged(toGameLinks(selected));
    emit gameSelected(toQString(selected.name), selected.id);
    return true;
}

bool MetaClientWorker::joinDefaultRoom()
{
    Array<MetaClient::Room> rooms;
    if (m_meta.getRooms(rooms) < 0 || rooms.size() < 1) {
        return false;
    }

    const dword currentLanguage = MetaClient::LCIDToLanguage(Locale::locale().LCID());
    int selectedIndex = -1;

    if (m_preferredRoomId != 0) {
        for (int i = 0; i < rooms.size(); ++i) {
            if (rooms[i].roomId == m_preferredRoomId
                && (rooms[i].flags & MetaClient::FLAG_ROOM_PASSWORD) == 0) {
                selectedIndex = i;
                break;
            }
        }
    }

    if (selectedIndex < 0) {
        for (int i = 0; i < rooms.size(); ++i) {
            if ((rooms[i].flags & MetaClient::FLAG_ROOM_PASSWORD) == 0 && rooms[i].language == currentLanguage) {
                selectedIndex = i;
                break;
            }
        }
    }

    if (selectedIndex < 0) {
        for (int i = 0; i < rooms.size(); ++i) {
            if ((rooms[i].flags & MetaClient::FLAG_ROOM_PASSWORD) == 0) {
                selectedIndex = i;
                break;
            }
        }
    }

    if (selectedIndex < 0) {
        selectedIndex = 0;
    }

    const MetaClient::Room &room = rooms[selectedIndex];
    const dword joinedRoomId = m_meta.joinRoom(room.roomId, "");
    if (joinedRoomId == 0) {
        return false;
    }

    m_activeRoomId = joinedRoomId;
    m_memberSnapshotKey.clear();

    const QString roomName = toQString(room.name);
    m_meta.sendStatus(CharString().format("Chatting in '%s'", room.name.cstr()));
    emit roomJoined(m_activeRoomId, roomName);
    emitRoomMembersIfChanged();
    return true;
}

void MetaClientWorker::ensurePumpTimer()
{
    if (m_pumpTimer) {
        return;
    }

    m_pumpTimer = new QTimer(this);
    m_pumpTimer->setInterval(50);
    m_pumpTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pumpTimer, &QTimer::timeout, this, &MetaClientWorker::pump);
}

void MetaClientWorker::startPumpTimer()
{
    ensurePumpTimer();
    m_pumpTimer->start();
}

void MetaClientWorker::stopPumpTimer()
{
    if (m_pumpTimer) {
        m_pumpTimer->stop();
    }
}

void MetaClientWorker::drainChat()
{
    QList<ChatMessage> pending;

    // MetaClient's chat buffer is append-only for the current session. Track the
    // next unread index rather than flushing so history remains available to the
    // legacy object while the UI receives each message once.
    m_meta.lock();
    const int count = m_meta.chatCount();
    for (; m_nextChatIndex < count; ++m_nextChatIndex) {
        const MetaClient::Chat &chat = m_meta.chat(m_nextChatIndex);
        ChatMessage message;
        message.recipientId = chat.recpId;
        message.roomId = chat.roomId;
        message.author = toQString(chat.author);
        message.authorId = chat.authorId;
        message.time = chat.time;
        message.text = toQString(chat.text);
        pending.append(message);
    }
    m_meta.unlock();

    if (!pending.isEmpty()) {
        emit chatMessagesReceived(pending);
    }
}

void MetaClientWorker::emitRoomMembersIfChanged()
{
    if (m_activeRoomId == 0) {
        return;
    }

    if (!m_meta.waitPlayers(m_activeRoomId, false)) {
        return;
    }

    QList<RoomMember> members;
    QString snapshotKey;

    m_meta.lock();
    const int count = m_meta.playerCount(m_activeRoomId);
    for (int i = 0; i < count; ++i) {
        const RoomMember member = toRoomMember(m_meta.player(m_activeRoomId, i));
        members.append(member);
        snapshotKey += QString("%1:%2:%3:%4|")
                           .arg(member.userId)
                           .arg(member.flags)
                           .arg(member.name, member.status);
    }
    m_meta.unlock();

    if (snapshotKey == m_memberSnapshotKey) {
        return;
    }

    // Emit only real membership/status changes. This avoids repainting the
    // member list on every 50 ms pump tick.
    m_memberSnapshotKey = snapshotKey;
    emit roomMembersChanged(members);
}

ChatRoomInfo MetaClientWorker::toChatRoomInfo(const MetaClient::Room &room)
{
    ChatRoomInfo info;
    info.roomId = room.roomId;
    info.name = toQString(room.name);
    info.members = room.members;
    info.language = room.language;
    info.flags = room.flags;
    return info;
}

ServerInfo MetaClientWorker::toServerInfo(const MetaClient::Server &server)
{
    ServerInfo info;
    info.gameId = server.gameId;
    info.serverId = server.id;
    info.type = server.type;
    info.flags = server.flags;
    info.name = toQString(server.name);
    info.shortDescription = toQString(server.shortDescription);
    info.description = toQString(server.description);
    info.address = toQString(server.address);
    info.port = server.port;
    info.maxClients = server.maxClients;
    info.clients = server.clients;
    info.lastUpdate = server.lastUpdate;
    info.data = toQString(server.data);
    if (server.gameId == kDarkSpaceGameId && server.type == MetaClient::GAME_SERVER) {
        info.maxClients = darkSpaceDisplayMaxClients(info.name, info.shortDescription, info.maxClients);
    }
    info.population = info.maxClients > 0
        ? QString("%1 / %2").arg(info.clients).arg(info.maxClients)
        : QString::fromUtf8(MetaClient::populationText(server.clients, server.maxClients));
    return info;
}

GameLinks MetaClientWorker::toGameLinks(const MetaClient::Game &game)
{
    GameLinks links;
    links.home = toQString(game.home);
    links.download = toQString(game.download);
    links.manual = toQString(game.manual);
    links.clans = toQString(game.clans);
    links.profile = toQString(game.profile);
    links.news = toQString(game.news);
    links.forum = toQString(game.forum);
    return links;
}

RoomMember MetaClientWorker::toRoomMember(const MetaClient::ShortProfile &profile)
{
    RoomMember member;
    member.userId = profile.userId;
    member.name = toQString(profile.name);
    member.status = toQString(profile.status);
    member.flags = profile.flags;
    return member;
}

QString MetaClientWorker::resultMessage(int result)
{
    switch (result) {
    case MetaClient::LOGIN_FAILED:
        return "Bad username or password.";
    case MetaClient::LOGIN_BANNED:
        return "This account is banned.";
    case MetaClient::LOGIN_DUPLICATE_LOGIN:
        return "This account is already logged in.";
    case MetaClient::LOGIN_ILLEGAL:
        return "The account name contains illegal characters or words.";
    case MetaClient::LOGIN_ERROR:
    default:
        return "Login failed due to a meta-server error.";
    }
}

QString MetaClientWorker::toQString(const CharString &value)
{
    return decodeLegacyText(value.cstr());
}
