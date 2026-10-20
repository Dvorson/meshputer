#include "settings.h"

#include <Preferences.h>

namespace {

const char* const kDefaultCanned[kMaxCannedResponses] = {
    "OK", "On my way", "At location", "Need help",
    "Emergency SOS", "Call me", "Yes", "No",
};

Settings sInstance;
bool sLoaded = false;

std::string getStr(Preferences& p, const char* key, const std::string& def) {
    return std::string(p.getString(key, def.c_str()).c_str());
}

void cannedKey(char* buf, size_t cap, int i) { snprintf(buf, cap, "can%d", i); }

} // namespace

Settings& Settings::get() {
    if (!sLoaded) {
        sInstance.resetToDefaults();
        sInstance.load();
        sLoaded = true;
    }
    return sInstance;
}

void Settings::resetToDefaults() {
    *this = Settings();
    for (int i = 0; i < kMaxCannedResponses; ++i) canned[i] = kDefaultCanned[i];
}

void Settings::load() {
    Preferences p;
    p.begin("meshputer", true);
    soundEnabled = p.getBool("sndOn", soundEnabled);
    volume = p.getUChar("vol", volume);
    blePin = getStr(p, "pin", blePin);
    autoConnect = p.getBool("autoCon", autoConnect);
    radioAddr = getStr(p, "radAddr", radioAddr);
    radioAddrType = p.getUChar("radType", radioAddrType);
    radioName = getStr(p, "radName", radioName);
    lastNodeNum = p.getULong("lastNode", lastNodeNum);
    internetEnabled = p.getBool("inet", internetEnabled);
    wifiSsid = getStr(p, "wfssid", wifiSsid);
    wifiPassword = getStr(p, "wfpass", wifiPassword);
    nostrSecretHex = getStr(p, "nsec", nostrSecretHex);
    brightness = p.getUChar("bright", brightness);
    dimAfterSec = p.getUShort("dimSec", dimAfterSec);
    screenOffSec = p.getUShort("scrTmo", screenOffSec);
    themeIndex = p.getUChar("theme", themeIndex);
    for (int i = 0; i < kMaxCannedResponses; ++i) {
        char key[8];
        cannedKey(key, sizeof(key), i);
        canned[i] = getStr(p, key, canned[i]);
    }
    p.end();
}

void Settings::save() {
    Preferences p;
    p.begin("meshputer", false);
    p.putBool("sndOn", soundEnabled);
    p.putUChar("vol", volume);
    p.putString("pin", blePin.c_str());
    p.putBool("autoCon", autoConnect);
    p.putString("radAddr", radioAddr.c_str());
    p.putUChar("radType", radioAddrType);
    p.putString("radName", radioName.c_str());
    p.putULong("lastNode", lastNodeNum);
    p.putBool("inet", internetEnabled);
    p.putString("wfssid", wifiSsid.c_str());
    p.putString("wfpass", wifiPassword.c_str());
    p.putString("nsec", nostrSecretHex.c_str());
    p.putUChar("bright", brightness);
    p.putUShort("dimSec", dimAfterSec);
    p.putUShort("scrTmo", screenOffSec);
    p.putUChar("theme", themeIndex);
    for (int i = 0; i < kMaxCannedResponses; ++i) {
        char key[8];
        cannedKey(key, sizeof(key), i);
        p.putString(key, canned[i].c_str());
    }
    p.end();
}
