#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

struct ChatMessage {
    quint32 recipientId = 0;
    quint32 roomId = 0;
    QString author;
    quint32 authorId = 0;
    quint32 time = 0;
    QString text;
};

struct RoomMember {
    quint32 userId = 0;
    QString name;
    QString status;
    quint32 flags = 0;
};

struct ChatRoomInfo {
    quint32 roomId = 0;
    QString name;
    quint32 members = 0;
    quint32 language = 0;
    quint32 flags = 0;
};

struct ServerInfo {
    quint32 gameId = 0;
    quint32 serverId = 0;
    quint32 type = 0;
    quint32 flags = 0;
    QString name;
    QString shortDescription;
    QString description;
    QString address;
    int port = 0;
    int maxClients = 0;
    int clients = 0;
    quint32 lastUpdate = 0;
    QString data;
    QString population;
};

struct GameLinks {
    QString home;
    QString download;
    QString manual;
    QString clans;
    QString profile;
    QString news;
    QString forum;
};

Q_DECLARE_METATYPE(ChatMessage)
Q_DECLARE_METATYPE(QList<ChatMessage>)
Q_DECLARE_METATYPE(RoomMember)
Q_DECLARE_METATYPE(QList<RoomMember>)
Q_DECLARE_METATYPE(ChatRoomInfo)
Q_DECLARE_METATYPE(QList<ChatRoomInfo>)
Q_DECLARE_METATYPE(ServerInfo)
Q_DECLARE_METATYPE(QList<ServerInfo>)
Q_DECLARE_METATYPE(GameLinks)
