#include "net/MetaClientBridge.h"

#include "net/MetaClientWorker.h"

#include <QMetaObject>
#include <QThread>

#include <utility>

namespace {

template <typename Function>
void invokeWorker(MetaClientWorker *worker, Function &&function)
{
    QMetaObject::invokeMethod(
        worker,
        [worker, function = std::forward<Function>(function)]() mutable {
            function(worker);
        },
        Qt::QueuedConnection);
}

} // namespace

MetaClientBridge::MetaClientBridge(QObject *parent)
    : QObject(parent)
    , m_thread(new QThread(this))
    , m_worker(new MetaClientWorker)
{
    qRegisterMetaType<ChatMessage>("ChatMessage");
    qRegisterMetaType<QList<ChatMessage>>("QList<ChatMessage>");
    qRegisterMetaType<RoomMember>("RoomMember");
    qRegisterMetaType<QList<RoomMember>>("QList<RoomMember>");
    qRegisterMetaType<ChatRoomInfo>("ChatRoomInfo");
    qRegisterMetaType<QList<ChatRoomInfo>>("QList<ChatRoomInfo>");
    qRegisterMetaType<ServerInfo>("ServerInfo");
    qRegisterMetaType<QList<ServerInfo>>("QList<ServerInfo>");
    qRegisterMetaType<GameLinks>("GameLinks");

    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &MetaClientWorker::connectionStateChanged, this, &MetaClientBridge::connectionStateChanged);
    connect(m_worker, &MetaClientWorker::loginSucceeded, this, &MetaClientBridge::loginSucceeded);
    connect(m_worker, &MetaClientWorker::loginFailed, this, &MetaClientBridge::loginFailed);
    connect(m_worker, &MetaClientWorker::gameSelected, this, &MetaClientBridge::gameSelected);
    connect(m_worker, &MetaClientWorker::gameLinksChanged, this, &MetaClientBridge::gameLinksChanged);
    connect(m_worker, &MetaClientWorker::roomJoined, this, &MetaClientBridge::roomJoined);
    connect(m_worker, &MetaClientWorker::roomsChanged, this, &MetaClientBridge::roomsChanged);
    connect(m_worker, &MetaClientWorker::chatMessagesReceived, this, &MetaClientBridge::chatMessagesReceived);
    connect(m_worker, &MetaClientWorker::roomMembersChanged, this, &MetaClientBridge::roomMembersChanged);
    connect(m_worker, &MetaClientWorker::friendsChanged, this, &MetaClientBridge::friendsChanged);
    connect(m_worker, &MetaClientWorker::fleetChanged, this, &MetaClientBridge::fleetChanged);
    connect(m_worker, &MetaClientWorker::staffChanged, this, &MetaClientBridge::staffChanged);
    connect(m_worker, &MetaClientWorker::serversChanged, this, &MetaClientBridge::serversChanged);
    connect(m_worker, &MetaClientWorker::profilesFound, this, &MetaClientBridge::profilesFound);
    connect(m_worker, &MetaClientWorker::ignoresChanged, this, &MetaClientBridge::ignoresChanged);
    connect(m_worker, &MetaClientWorker::profileChanged, this, &MetaClientBridge::profileChanged);
    connect(m_worker, &MetaClientWorker::actionSucceeded, this, &MetaClientBridge::actionSucceeded);
    connect(m_worker, &MetaClientWorker::errorOccurred, this, &MetaClientBridge::errorOccurred);

    m_thread->start();
}

MetaClientBridge::~MetaClientBridge()
{
    shutdown();
}

void MetaClientBridge::connectAndLogin(const QString &address, int port, const QString &account, const QString &password)
{
    invokeWorker(m_worker, [address, port, account, password](MetaClientWorker *worker) {
            worker->connectAndLogin(address, port, account, password);
        });
}

void MetaClientBridge::sendChatMessage(const QString &message)
{
    invokeWorker(m_worker, [message](MetaClientWorker *worker) {
            worker->sendChatMessage(message);
        });
}

void MetaClientBridge::requestRooms()
{
    invokeWorker(m_worker, [](MetaClientWorker *worker) { worker->requestRooms(); });
}

void MetaClientBridge::joinRoom(quint32 roomId, const QString &password, const QString &name)
{
    invokeWorker(m_worker, [roomId, password, name](MetaClientWorker *worker) {
            worker->joinRoom(roomId, password, name);
        });
}

void MetaClientBridge::createRoom(const QString &name, const QString &password)
{
    invokeWorker(m_worker, [name, password](MetaClientWorker *worker) {
            worker->createRoom(name, password);
        });
}

void MetaClientBridge::requestFriends()
{
    invokeWorker(m_worker, [](MetaClientWorker *worker) { worker->requestFriends(); });
}

void MetaClientBridge::requestFleet()
{
    invokeWorker(m_worker, [](MetaClientWorker *worker) { worker->requestFleet(); });
}

void MetaClientBridge::requestStaff()
{
    invokeWorker(m_worker, [](MetaClientWorker *worker) { worker->requestStaff(); });
}

void MetaClientBridge::requestServers(const QString &nameFilter, quint32 gameId, quint32 type)
{
    invokeWorker(m_worker, [nameFilter, gameId, type](MetaClientWorker *worker) {
            worker->requestServers(nameFilter, gameId, type);
        });
}

void MetaClientBridge::findProfiles(const QString &nameFilter)
{
    invokeWorker(m_worker, [nameFilter](MetaClientWorker *worker) {
            worker->findProfiles(nameFilter);
        });
}

void MetaClientBridge::requestIgnores()
{
    invokeWorker(m_worker, [](MetaClientWorker *worker) { worker->requestIgnores(); });
}

void MetaClientBridge::addFriend(quint32 userId, const QString &name)
{
    invokeWorker(m_worker, [userId, name](MetaClientWorker *worker) {
            worker->addFriend(userId, name);
        });
}

void MetaClientBridge::deleteFriend(quint32 userId, const QString &name)
{
    invokeWorker(m_worker, [userId, name](MetaClientWorker *worker) {
            worker->deleteFriend(userId, name);
        });
}

void MetaClientBridge::addIgnore(quint32 userId, const QString &name)
{
    invokeWorker(m_worker, [userId, name](MetaClientWorker *worker) {
            worker->addIgnore(userId, name);
        });
}

void MetaClientBridge::deleteIgnore(quint32 userId, const QString &name)
{
    invokeWorker(m_worker, [userId, name](MetaClientWorker *worker) {
            worker->deleteIgnore(userId, name);
        });
}

void MetaClientBridge::changeName(const QString &name)
{
    invokeWorker(m_worker, [name](MetaClientWorker *worker) {
            worker->changeName(name);
        });
}

void MetaClientBridge::shutdown()
{
    if (!m_thread || !m_thread->isRunning()) {
        return;
    }

    QMetaObject::invokeMethod(m_worker, &MetaClientWorker::shutdown, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
}
