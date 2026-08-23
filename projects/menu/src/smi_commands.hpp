#pragma once
#include <switchu/smi_protocol.hpp>
#include <switchu/smi_helpers.hpp>
#include <switchu/music_protocol.hpp>
#include <switch.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace switchu::menu::smi_cmd {

static Result pushOutStorage(const void* data, size_t size) {
    AppletStorage stor{};
    Result rc = appletCreateStorage(&stor, static_cast<s64>(size));
    if (R_FAILED(rc)) return rc;

    rc = appletStorageWrite(&stor, 0, data, size);
    if (R_FAILED(rc)) { appletStorageClose(&stor); return rc; }

    rc = appletPushInteractiveOutData(&stor);
    appletStorageClose(&stor);
    return rc;
}

static Result popInStorage(void* out, size_t maxSize, size_t* outActual) {
    AppletStorage stor{};
    Result rc = appletPopInteractiveInData(&stor);
    if (R_FAILED(rc)) return rc;

    s64 sz = 0;
    appletStorageGetSize(&stor, &sz);
    size_t toRead = (size_t)sz < maxSize ? (size_t)sz : maxSize;
    rc = appletStorageRead(&stor, 0, out, toRead);
    appletStorageClose(&stor);
    if (outActual) *outActual = toRead;
    return rc;
}

inline Result sendSimple(smi::SystemMessage msg) {
    smi::CommandHeader hdr{smi::kCommandMagic, static_cast<uint32_t>(msg)};
    return pushOutStorage(&hdr, sizeof(hdr));
}

inline Result launchApplication(uint64_t titleId, AccountUid uid) {
    uint8_t buf[sizeof(smi::CommandHeader) + sizeof(smi::LaunchAppArgs)]{};
    auto* hdr = reinterpret_cast<smi::CommandHeader*>(buf);
    auto* args = reinterpret_cast<smi::LaunchAppArgs*>(buf + sizeof(smi::CommandHeader));

    hdr->magic    = smi::kCommandMagic;
    hdr->message  = static_cast<uint32_t>(smi::SystemMessage::LaunchApplication);
    args->title_id = titleId;
    std::memcpy(args->user_uid, &uid, sizeof(uid));

    return pushOutStorage(buf, sizeof(buf));
}

inline Result launchUserPage(AccountUid uid) {
    uint8_t buf[sizeof(smi::CommandHeader) + sizeof(smi::UserArgs)]{};
    auto* hdr = reinterpret_cast<smi::CommandHeader*>(buf);
    auto* args = reinterpret_cast<smi::UserArgs*>(buf + sizeof(smi::CommandHeader));

    hdr->magic    = smi::kCommandMagic;
    hdr->message  = static_cast<uint32_t>(smi::SystemMessage::LaunchUserPage);
    std::memcpy(args->user_uid, &uid, sizeof(uid));

    return pushOutStorage(buf, sizeof(buf));
}

inline Result resumeApplication() {
    return sendSimple(smi::SystemMessage::ResumeApplication);
}

inline Result terminateApplication() {
    return sendSimple(smi::SystemMessage::TerminateApplication);
}

inline Result requestQlaunchSettingsHandoff() {
    return sendSimple(smi::SystemMessage::RequestQlaunchSettingsHandoff);
}

inline Result sendPayload(smi::SystemMessage msg, const void* payload, size_t payloadSize) {
    std::vector<uint8_t> buf(sizeof(smi::CommandHeader) + payloadSize, 0);
    auto* hdr = reinterpret_cast<smi::CommandHeader*>(buf.data());
    hdr->magic = smi::kCommandMagic;
    hdr->message = static_cast<uint32_t>(msg);
    if (payload && payloadSize > 0)
        std::memcpy(buf.data() + sizeof(smi::CommandHeader), payload, payloadSize);
    return pushOutStorage(buf.data(), buf.size());
}

inline Result musicReloadQueue() { return sendSimple(smi::SystemMessage::MusicReloadQueue); }
inline Result musicTogglePause() { return sendSimple(smi::SystemMessage::MusicTogglePause); }
inline Result musicPause()       { return sendSimple(smi::SystemMessage::MusicPause); }
inline Result musicResume()      { return sendSimple(smi::SystemMessage::MusicResume); }
inline Result musicNext()        { return sendSimple(smi::SystemMessage::MusicNext); }
inline Result musicPrevious()    { return sendSimple(smi::SystemMessage::MusicPrevious); }
inline Result musicStop()        { return sendSimple(smi::SystemMessage::MusicStop); }
inline Result musicClearSession(){ return sendSimple(smi::SystemMessage::MusicClearSession); }

inline Result musicPlayIndex(int index) {
    switchu::music::IndexArgs args{};
    args.index = index;
    return sendPayload(smi::SystemMessage::MusicPlayIndex, &args, sizeof(args));
}

inline Result musicSeek(uint64_t positionMs) {
    switchu::music::SeekArgs args{};
    args.position_ms = positionMs;
    return sendPayload(smi::SystemMessage::MusicSeek, &args, sizeof(args));
}

inline Result musicSetVolume(float volume) {
    switchu::music::VolumeArgs args{};
    args.volume = volume;
    return sendPayload(smi::SystemMessage::MusicSetVolume, &args, sizeof(args));
}

inline Result musicSetShuffle(bool enabled) {
    switchu::music::ToggleArgs args{};
    args.enabled = enabled ? 1 : 0;
    return sendPayload(smi::SystemMessage::MusicSetShuffle, &args, sizeof(args));
}

inline Result musicSetRepeat(switchu::music::RepeatMode mode) {
    switchu::music::RepeatArgs args{};
    args.mode = static_cast<uint8_t>(mode);
    return sendPayload(smi::SystemMessage::MusicSetRepeat, &args, sizeof(args));
}

inline Result getMusicStatus(switchu::music::Status& out) {
    Result rc = sendSimple(smi::SystemMessage::MusicGetStatus);
    if (R_FAILED(rc)) return rc;

    uint8_t buf[smi::kStorageSize]{};
    for (int retry = 0; retry < 120; ++retry) {
        size_t actual = 0;
        rc = popInStorage(buf, sizeof(buf), &actual);
        if (R_FAILED(rc)) {
            svcSleepThread(5'000'000ULL);
            continue;
        }
        if (actual < sizeof(smi::CommandHeader))
            continue;
        smi::CommandHeader hdr{};
        std::memcpy(&hdr, buf, sizeof(hdr));
        if (hdr.magic != smi::kCommandMagic)
            continue;
        // Ignore stale generic responses from older system commands. Music
        // status is tagged with its own command ID so it can be recognized.
        if (hdr.message != static_cast<uint32_t>(smi::SystemMessage::MusicGetStatus))
            continue;
        if (actual >= sizeof(smi::CommandHeader) + sizeof(out)) {
            std::memcpy(&out, buf + sizeof(smi::CommandHeader), sizeof(out));
            return 0;
        }
    }
    return MAKERESULT(Module_Libnx, 0xFC);
}

inline Result enterSleep()  { return sendSimple(smi::SystemMessage::EnterSleep); }
inline Result shutdown()    { return sendSimple(smi::SystemMessage::Shutdown); }
inline Result reboot()      { return sendSimple(smi::SystemMessage::Reboot); }
inline Result menuReady()   { return sendSimple(smi::SystemMessage::MenuReady); }
inline Result menuClosing() { return sendSimple(smi::SystemMessage::MenuClosing); }

struct AppEntry {
    uint64_t titleId;
    uint32_t nameLen;
    uint32_t iconLen;
    uint32_t viewFlags = 0;
    bool startupUserKnown = false;
    uint8_t startupUserAccount = 1;
    uint8_t startupUserAccountOption = 0;
    std::string name;
    std::vector<uint8_t> icon;
};

inline Result getAppList(std::vector<AppEntry>& outList, bool waitForDaemon = true) {
    outList.clear();
    std::ifstream file;
    const int retries = waitForDaemon ? 20 : 1;
    for (int retry = 0; retry < retries && !file.is_open(); ++retry) {
        file.open("sdmc:/config/SwitchU/applist.bin", std::ios::binary);
        if (!file.is_open() && waitForDaemon) svcSleepThread(50'000'000ULL);
    }
    if (!file.is_open()) return MAKERESULT(Module_Libnx, 0xFE);

    uint32_t count = 0;
    if (!file.read(reinterpret_cast<char*>(&count), sizeof(count)))
        return MAKERESULT(Module_Libnx, 0xFD);

    for (uint32_t i = 0; i < count; ++i) {
        smi::AppEntryHeader eh{};
        if (!file.read(reinterpret_cast<char*>(&eh), sizeof(eh))) break;

        AppEntry ent{};
        ent.titleId = eh.title_id;
        ent.nameLen = eh.name_len;
        ent.iconLen = eh.icon_data_len;
        ent.viewFlags = eh.view_flags;
        ent.startupUserKnown = eh.startup_user_known != 0;
        ent.startupUserAccount = eh.startup_user_account;
        ent.startupUserAccountOption = eh.startup_user_account_option;

        if (eh.name_len > 0) {
            ent.name.resize(eh.name_len);
            if (!file.read(ent.name.data(), static_cast<std::streamsize>(eh.name_len))) break;
        }

        if (eh.icon_data_len > 0) {
            ent.icon.resize(eh.icon_data_len);
            if (!file.read(reinterpret_cast<char*>(ent.icon.data()), static_cast<std::streamsize>(eh.icon_data_len))) break;
        }

        outList.push_back(std::move(ent));
    }

    return 0;
}

inline Result getSystemStatus(smi::SystemStatus& out) {
    Result rc = sendSimple(smi::SystemMessage::GetSystemStatus);
    if (R_FAILED(rc)) return rc;

    uint8_t buf[smi::kStorageSize]{};
    size_t actual = 0;
    for (int retry = 0; retry < 200; retry++) {
        rc = popInStorage(buf, sizeof(buf), &actual);
        if (R_SUCCEEDED(rc)) break;
        svcSleepThread(10'000'000ULL);
    }
    if (R_FAILED(rc)) return rc;

    if (actual >= sizeof(smi::CommandHeader) + sizeof(smi::SystemStatus)) {
        std::memcpy(&out, buf + sizeof(smi::CommandHeader), sizeof(smi::SystemStatus));
    }
    return 0;
}

}
