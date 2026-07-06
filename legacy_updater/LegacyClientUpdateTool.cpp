#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>

#include "File/FileDisk.h"
#include "Network/MirrorClient.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string narrow(const std::wstring &value)
{
    if (value.empty()) {
        return {};
    }

    const int length = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, result.data(), length, nullptr, nullptr);
    return result;
}

std::vector<std::string> commandArguments()
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    if (!argv) {
        return args;
    }

    args.reserve(argc > 0 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) {
        args.push_back(narrow(argv[i]));
    }
    LocalFree(argv);
    return args;
}

void writeStdout(const std::string &line)
{
    const std::string output = line + "\r\n";
    DWORD written = 0;
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        WriteFile(handle, output.data(), static_cast<DWORD>(output.size()), &written, nullptr);
    } else {
        OutputDebugStringA(output.c_str());
    }
}

class WinsockSession {
public:
    bool start()
    {
        return WSAStartup(MAKEWORD(2, 2), &m_data) == 0;
    }

    ~WinsockSession()
    {
        WSACleanup();
    }

private:
    WSADATA m_data = {};
};

class LegacyUpdateClient final : public MirrorClient {
public:
    explicit LegacyUpdateClient(const std::string &installPath)
    {
        std::error_code error;
        std::filesystem::path path(installPath.empty() ? "." : installPath);
        std::filesystem::create_directories(path, error);
        std::filesystem::path logPath = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
        m_log.open(logPath / "GameCQLegacyUpdate.log", std::ios::out | std::ios::trunc);
    }

    void emit(const std::string &line)
    {
        writeStdout(line);
        if (m_log.is_open()) {
            m_log << line << '\n';
            m_log.flush();
        }
    }

    void onStatus(const char *status) override
    {
        emit(std::string("STATUS: ") + (status ? status : ""));
    }

    void onError(const char *error) override
    {
        emit(std::string("ERROR: ") + (error ? error : ""));
    }

    void onProgress(dword totalBytes, dword bytes) override
    {
        const DWORD now = GetTickCount();
        if (now - m_lastProgressTick < 1000 && bytes < totalBytes) {
            return;
        }
        m_lastProgressTick = now;
        emit("PROGRESS: " + std::to_string(bytes) + " / " + std::to_string(totalBytes));
    }

    void onFileProgress(const char *file, dword totalBytes, dword bytes) override
    {
        if (totalBytes < kVerboseFileThresholdBytes) {
            return;
        }

        const DWORD now = GetTickCount();
        if (now - m_lastFileTick < 2000) {
            return;
        }
        m_lastFileTick = now;
        emit(std::string("FILE: ") + (file ? file : "") + " " + std::to_string(bytes) + " / " + std::to_string(totalBytes));
    }

    bool onFileDownload(const char *file) override
    {
        (void)file;
        return true;
    }

    int synchronizeFast(bool planOnly = false)
    {
        if (!m_bLocalCatalog) {
            loadLocalCatalog();
        }

        Catalog oldCatalog = m_LocalCatalog;
        m_bLocalDirty = true;
        m_nTotalBytes = 0;
        m_nBytes = 0;

        onStatus("Updating local catalog...");
        checkCatalog(m_MirrorInto, m_LocalCatalog, false);

        const dword remoteCrc = getCRC();
        if (remoteCrc != 0 && remoteCrc == getLocalCRC()) {
            return 0;
        }

        if (!getCatalog()) {
            onError("Failed to get remote catalog!");
            return -1;
        }

        onStatus("Checking local files...");
        CatalogIt localIt = m_LocalCatalog.head();
        while (localIt.valid()) {
            CharString key(localIt.key());
            Item item = *localIt;
            localIt.next();

            CatalogIt remote = m_Catalog.find(key);
            if (remote.valid()) {
                continue;
            }

            m_LocalCatalog.remove(key);
            if (onFileDelete(item.name)) {
                FileDisk::deleteFile(m_MirrorInto + item.name);
            }
        }

        onStatus("Checking remote files...");
        Array<CharString> batch;
        dword batchBytes = 0;
        int downloadCount = 0;
        int batchCount = 0;
        unsigned long long plannedBytes = 0;

        auto flushBatch = [&]() -> bool {
            if (batch.size() <= 0) {
                return true;
            }

            ++batchCount;
            if (planOnly) {
                emit("PLAN: Batch " + std::to_string(batchCount) + " would request " + std::to_string(batch.size()) + " legacy files.");
                batch.release();
                batchBytes = 0;
                return true;
            }

            emit("STATUS: Requesting " + std::to_string(batch.size()) + " legacy files as one batch.");
            dword job = getFiles(batch);
            if (job == 0 || waitJob(job, kBatchTimeoutMs) <= 0) {
                emit("WARN: Batch request failed; retrying this batch one file at a time.");
                if (!downloadSequential(batch)) {
                    batch.release();
                    batchBytes = 0;
                    return false;
                }
            }

            batch.release();
            batchBytes = 0;
            return true;
        };

        CatalogIt remoteIt = m_Catalog.head();
        while (remoteIt.valid()) {
            CharString key(remoteIt.key());
            Item item = *remoteIt;
            remoteIt.next();

            CatalogIt local = m_LocalCatalog.find(key);
            if (local.valid()) {
                if ((*local).crc == item.crc) {
                    continue;
                }

                CatalogIt old = oldCatalog.find(key);
                if (old.valid() && (*old).crc == (*local).crc) {
                    // The remote file changed and the local file still matches our old catalog.
                    // Let it download below.
                } else {
                    // Preserve locally modified files, matching legacy non-upload sync behavior.
                    continue;
                }
            } else {
                const dword localCrc = checksum(m_MirrorInto + item.name);
                if (localCrc != 0 && localCrc == item.crc) {
                    m_LocalCatalog[key] = item;
                    m_bLocalDirty = true;
                    continue;
                }
            }

            if (!onFileDownload(item.name)) {
                continue;
            }

            if (batch.size() >= kMaxBatchFiles || (batchBytes > 0 && batchBytes + item.size > kMaxBatchBytes)) {
                if (!flushBatch()) {
                    return -1;
                }
            }

            plannedBytes += item.size;
            if (!planOnly) {
                m_nTotalBytes += item.size;
            }
            batchBytes += item.size;
            batch.push(item.name);
            ++downloadCount;
        }

        saveLocalCatalog();

        if (downloadCount == 0) {
            return 0;
        }

        if (planOnly) {
            if (!flushBatch()) {
                return -1;
            }
            emit("PLAN: " + std::to_string(downloadCount) + " legacy files (" + std::to_string(plannedBytes) + " bytes) would be downloaded in " + std::to_string(batchCount) + " batch request(s).");
            return 0;
        }

        emit("STATUS: Downloading " + std::to_string(downloadCount) + " legacy files in batched mirror requests.");
        if (!flushBatch()) {
            return -1;
        }

        saveLocalCatalog();
        return 1;
    }

private:
    bool downloadSequential(const Array<CharString> &files)
    {
        for (int i = 0; i < files.size(); ++i) {
            dword job = getFile(files[i]);
            if (job == 0 || waitJob(job, kFileTimeoutMs) <= 0) {
                return false;
            }
        }
        return true;
    }

    static constexpr int kMaxBatchFiles = 8192;
    static constexpr dword kMaxBatchBytes = 1024u * 1024u * 1024u;
    static constexpr dword kVerboseFileThresholdBytes = 1024u * 1024u;
    static constexpr int kFileTimeoutMs = 3600 * 1000;
    static constexpr int kBatchTimeoutMs = 86400 * 1000;

    std::ofstream m_log;
    DWORD m_lastProgressTick = 0;
    DWORD m_lastFileTick = 0;
};

int runLegacyUpdate()
{
    const std::vector<std::string> args = commandArguments();
    const bool probeOnly = !args.empty() && args.front() == "--probe";
    const bool planOnly = !args.empty() && args.front() == "--plan";
    const size_t offset = (probeOnly || planOnly) ? 1 : 0;
    const size_t count = args.size() - offset;
    if ((args.size() < offset) || (count != 3 && count != 4)) {
        writeStdout("Usage: GameCQLegacyUpdate [--probe|--plan] <Path> <Mirror Address> <Mirror Port> [SID]");
        return 2;
    }

    const std::string &installPath = args[offset];
    const std::string &address = args[offset + 1];
    const int port = std::strtol(args[offset + 2].c_str(), nullptr, 10);
    const dword sessionId = count == 4 ? static_cast<dword>(std::strtoul(args[offset + 3].c_str(), nullptr, 10)) : 0;

    LegacyUpdateClient client(installPath);
    WinsockSession winsock;
    if (!winsock.start()) {
        client.emit("ERROR: Failed to initialize WinSock.");
        return 10;
    }

    client.emit("STATUS: Opening legacy GCQL mirror " + address + ":" + std::to_string(port));
    if (!client.open(address.c_str(), port, installPath.c_str(), nullptr, false)) {
        client.emit("ERROR: Failed to connect or complete legacy mirror handshake.");
        return 20;
    }

    if (sessionId != 0) {
        client.emit("STATUS: Logging into legacy mirror with lobby session ID.");
        if (!client.login(sessionId)) {
            client.emit("WARN: Session login failed; continuing like legacy ClientUpdate.");
        }
    }

    if (probeOnly) {
        const dword crc = client.getCRC();
        client.close();
        if (crc == 0) {
            client.emit("ERROR: Legacy mirror probe connected but did not receive a catalog CRC.");
            return 25;
        }
        client.emit("STATUS: Legacy mirror probe succeeded. Remote catalog CRC: " + std::to_string(crc));
        return 0;
    }

    const int syncResult = client.synchronizeFast(planOnly);
    if (planOnly && syncResult == 0) {
        client.close();
        return 0;
    }
    if (syncResult == 0) {
        client.emit("STATUS: Original D9 client is already current.");
        client.close();
        return 0;
    }
    if (syncResult > 0) {
        client.emit("STATUS: Original D9 client update complete.");
        client.close();
        return 1;
    }

    client.close();
    client.emit("ERROR: Legacy mirror sync job failed or timed out.");
    return 30;
}

} // namespace

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return runLegacyUpdate();
}
