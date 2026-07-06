#pragma once

#include <QtGlobal>

constexpr quint32 kAdministratorFlag = 0x00000001;
constexpr quint32 kServerFlag = 0x00000002;
constexpr quint32 kModeratorFlag = 0x00000004;
constexpr quint32 kSubscribedFlag = 0x00000040;
constexpr quint32 kDeveloperFlag = 0x00000800;
constexpr quint32 kEventFlag = 0x00001000;
constexpr quint32 kAwayFlag = 0x00010000;
constexpr quint32 kMutedFlag = 0x00020000;
constexpr quint32 kHiddenFlag = 0x00040000;
constexpr quint32 kStaffFlags = kAdministratorFlag | kModeratorFlag | kDeveloperFlag;
constexpr quint32 kModeratorFlags = kAdministratorFlag | kModeratorFlag;