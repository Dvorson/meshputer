#include "core/mesh_protocol.h"

#include <cstdio>
#include <cstring>

#include <pb_decode.h>
#include <pb_encode.h>
#include "meshtastic/admin.pb.h"
#include "meshtastic/mesh.pb.h"

namespace MeshProto {

namespace {

// FromRadio / ToRadio are several KB once every oneof member is counted;
// keep them off the 8 KB Arduino loop stack.
meshtastic_FromRadio sFromRadio;
meshtastic_ToRadio sToRadio;

std::string boundedString(const char* s, size_t cap) {
    size_t n = 0;
    while (n < cap && s[n] != '\0') ++n;
    return std::string(s, n);
}

void copyUser(const meshtastic_User& src, User& dst) {
    dst.id = boundedString(src.id, sizeof(src.id));
    dst.longName = boundedString(src.long_name, sizeof(src.long_name));
    dst.shortName = boundedString(src.short_name, sizeof(src.short_name));
    dst.hwModel = static_cast<uint32_t>(src.hw_model);
    dst.role = static_cast<uint32_t>(src.role);
    dst.isLicensed = src.is_licensed;
    dst.hasPublicKey = src.public_key.size > 0;
    static const char* kHex = "0123456789abcdef";
    dst.publicKeyHex.clear();
    for (pb_size_t i = 0; i < src.public_key.size; ++i) {
        dst.publicKeyHex += kHex[src.public_key.bytes[i] >> 4];
        dst.publicKeyHex += kHex[src.public_key.bytes[i] & 0xF];
    }
}

void copyField(char* dst, size_t cap, const std::string& src) {
    size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

size_t finishToRadio(uint8_t* buf, size_t cap) {
    pb_ostream_t stream = pb_ostream_from_buffer(buf, cap);
    if (!pb_encode(&stream, &meshtastic_ToRadio_msg, &sToRadio)) return 0;
    return stream.bytes_written;
}

// Admin packets to our own node: unicast to myNodeNum on the primary channel.
size_t encodeAdmin(uint32_t packetId, uint32_t myNodeNum,
                   const meshtastic_AdminMessage& admin, bool wantResponse,
                   uint8_t* buf, size_t cap) {
    sToRadio = meshtastic_ToRadio_init_zero;
    sToRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    meshtastic_MeshPacket& pkt = sToRadio.packet;
    pkt.to = myNodeNum;
    pkt.id = packetId;
    pkt.channel = 0;
    pkt.want_ack = false;
    pkt.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pkt.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    pkt.decoded.want_response = wantResponse;

    pb_ostream_t inner = pb_ostream_from_buffer(pkt.decoded.payload.bytes,
                                                sizeof(pkt.decoded.payload.bytes));
    if (!pb_encode(&inner, &meshtastic_AdminMessage_msg, &admin)) return 0;
    pkt.decoded.payload.size = static_cast<pb_size_t>(inner.bytes_written);
    return finishToRadio(buf, cap);
}

const char* presetName(const meshtastic_Config_LoRaConfig& lora) {
    if (!lora.use_preset) return "Custom";
    switch (lora.modem_preset) {
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:      return "LongFast";
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:      return "LongSlow";
    case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW: return "VLongSlow";
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:    return "MediumSlow";
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:    return "MediumFast";
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:     return "ShortSlow";
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:     return "ShortFast";
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:  return "LongMod";
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:    return "ShortTurbo";
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:     return "LongTurbo";
    default:                                                      return "Custom";
    }
}

void decodePacket(const meshtastic_MeshPacket& pkt, Event& out) {
    if (pkt.which_payload_variant != meshtastic_MeshPacket_decoded_tag) return;
    const meshtastic_Data& d = pkt.decoded;

    switch (d.portnum) {
    case meshtastic_PortNum_TEXT_MESSAGE_APP: {
        if (d.payload.size == 0) return;
        out.kind = EventKind::Text;
        TextMessage& t = out.text;
        t.id = pkt.id;
        t.from = pkt.from;
        t.to = pkt.to;
        t.channel = pkt.channel;
        t.text.assign(reinterpret_cast<const char*>(d.payload.bytes), d.payload.size);
        t.rxTime = pkt.rx_time;
        t.snr = pkt.rx_snr;
        t.pkiEncrypted = pkt.pki_encrypted;
        return;
    }
    case meshtastic_PortNum_NODEINFO_APP: {
        meshtastic_User user = meshtastic_User_init_zero;
        pb_istream_t s = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
        if (!pb_decode(&s, &meshtastic_User_msg, &user)) return;
        out.kind = EventKind::UserUpdate;
        out.node.num = pkt.from;
        out.node.hasUser = true;
        out.node.snr = pkt.rx_snr;
        out.node.channel = pkt.channel;
        copyUser(user, out.node.user);
        return;
    }
    case meshtastic_PortNum_ROUTING_APP: {
        if (d.request_id == 0) return;
        meshtastic_Routing routing = meshtastic_Routing_init_zero;
        pb_istream_t s = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
        if (!pb_decode(&s, &meshtastic_Routing_msg, &routing)) return;
        out.kind = EventKind::Routing;
        out.routing.from = pkt.from;
        out.routing.requestId = d.request_id;
        out.routing.error = routing.which_variant == meshtastic_Routing_error_reason_tag
                                ? static_cast<int>(routing.error_reason)
                                : kRoutingNone;
        return;
    }
    case meshtastic_PortNum_ADMIN_APP: {
        static meshtastic_AdminMessage admin;
        admin = meshtastic_AdminMessage_init_zero;
        pb_istream_t s = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
        if (!pb_decode(&s, &meshtastic_AdminMessage_msg, &admin)) return;
        out.kind = EventKind::Admin;
        out.admin.from = pkt.from;
        out.admin.sessionPasskey.assign(admin.session_passkey.bytes,
                                        admin.session_passkey.bytes + admin.session_passkey.size);
        if (admin.which_payload_variant == meshtastic_AdminMessage_get_owner_response_tag) {
            out.admin.hasOwner = true;
            copyUser(admin.get_owner_response, out.admin.owner);
        }
        return;
    }
    default:
        return;
    }
}

} // namespace

bool decodeFromRadio(const uint8_t* data, size_t len, Event& out) {
    out = Event();
    sFromRadio = meshtastic_FromRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);
    if (!pb_decode(&stream, &meshtastic_FromRadio_msg, &sFromRadio)) return false;

    const meshtastic_FromRadio& fr = sFromRadio;
    switch (fr.which_payload_variant) {
    case meshtastic_FromRadio_my_info_tag:
        out.kind = EventKind::MyInfo;
        out.myNodeNum = fr.my_info.my_node_num;
        break;

    case meshtastic_FromRadio_node_info_tag: {
        const meshtastic_NodeInfo& n = fr.node_info;
        out.kind = EventKind::NodeInfo;
        NodeInfo& node = out.node;
        node.num = n.num;
        node.hasUser = n.has_user;
        if (n.has_user) copyUser(n.user, node.user);
        node.lastHeard = n.last_heard;
        node.snr = n.snr;
        node.channel = n.channel;
        node.viaMqtt = n.via_mqtt;
        node.hasHopsAway = n.has_hops_away;
        node.hopsAway = n.hops_away;
        if (n.has_device_metrics) {
            node.hasBattery = n.device_metrics.has_battery_level;
            node.batteryLevel = n.device_metrics.battery_level;
            node.hasVoltage = n.device_metrics.has_voltage;
            node.voltage = n.device_metrics.voltage;
        }
        break;
    }

    case meshtastic_FromRadio_channel_tag: {
        const meshtastic_Channel& ch = fr.channel;
        out.kind = EventKind::Channel;
        out.channel.index = static_cast<int8_t>(ch.index);
        out.channel.role = static_cast<uint8_t>(ch.role);
        if (ch.has_settings) {
            out.channel.name = boundedString(ch.settings.name, sizeof(ch.settings.name));
            out.channel.pskLen = static_cast<uint8_t>(ch.settings.psk.size);
            out.channel.pskFirstByte = ch.settings.psk.size > 0 ? ch.settings.psk.bytes[0] : 0;
        }
        break;
    }

    case meshtastic_FromRadio_config_complete_id_tag:
        out.kind = EventKind::ConfigComplete;
        out.configId = fr.config_complete_id;
        break;

    case meshtastic_FromRadio_packet_tag:
        decodePacket(fr.packet, out);
        break;

    case meshtastic_FromRadio_config_tag:
        if (fr.config.which_payload_variant == meshtastic_Config_lora_tag) {
            out.kind = EventKind::ModemPreset;
            out.message = presetName(fr.config.payload_variant.lora);
            out.hopLimit = static_cast<uint8_t>(fr.config.payload_variant.lora.hop_limit);
        }
        break;

    case meshtastic_FromRadio_queueStatus_tag:
        out.kind = EventKind::QueueStatus;
        out.queue.res = fr.queueStatus.res;
        out.queue.free = fr.queueStatus.free;
        out.queue.maxlen = fr.queueStatus.maxlen;
        out.queue.packetId = fr.queueStatus.mesh_packet_id;
        break;

    case meshtastic_FromRadio_rebooted_tag:
        out.kind = EventKind::Rebooted;
        break;

    case meshtastic_FromRadio_clientNotification_tag:
        out.kind = EventKind::Notification;
        out.message = boundedString(fr.clientNotification.message,
                                    sizeof(fr.clientNotification.message));
        break;

    default:
        break;
    }
    return true;
}

size_t encodeWantConfig(uint32_t nonce, uint8_t* buf, size_t cap) {
    sToRadio = meshtastic_ToRadio_init_zero;
    sToRadio.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    sToRadio.want_config_id = nonce;
    return finishToRadio(buf, cap);
}

size_t encodeDisconnect(uint8_t* buf, size_t cap) {
    sToRadio = meshtastic_ToRadio_init_zero;
    sToRadio.which_payload_variant = meshtastic_ToRadio_disconnect_tag;
    sToRadio.disconnect = true;
    return finishToRadio(buf, cap);
}

size_t encodeHeartbeat(uint8_t* buf, size_t cap) {
    sToRadio = meshtastic_ToRadio_init_zero;
    sToRadio.which_payload_variant = meshtastic_ToRadio_heartbeat_tag;
    sToRadio.heartbeat = meshtastic_Heartbeat_init_zero;
    return finishToRadio(buf, cap);
}

size_t encodeText(uint32_t packetId, uint32_t to, uint8_t channel,
                  const std::string& utf8, bool wantAck,
                  uint8_t* buf, size_t cap, uint8_t hopLimit) {
    if (utf8.empty() || utf8.size() > kMaxTextBytes) return 0;

    sToRadio = meshtastic_ToRadio_init_zero;
    sToRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    meshtastic_MeshPacket& pkt = sToRadio.packet;
    pkt.to = to;
    pkt.channel = channel;
    pkt.id = packetId;
    pkt.want_ack = wantAck;
    pkt.hop_limit = hopLimit;
    pkt.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pkt.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    pkt.decoded.payload.size = static_cast<pb_size_t>(utf8.size());
    memcpy(pkt.decoded.payload.bytes, utf8.data(), utf8.size());
    return finishToRadio(buf, cap);
}

size_t encodeGetOwner(uint32_t packetId, uint32_t myNodeNum, uint8_t* buf, size_t cap) {
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_get_owner_request_tag;
    admin.get_owner_request = true;
    return encodeAdmin(packetId, myNodeNum, admin, true, buf, cap);
}

size_t encodeSetOwner(uint32_t packetId, uint32_t myNodeNum,
                      const std::string& longName, const std::string& shortName,
                      bool isLicensed, const std::vector<uint8_t>& sessionPasskey,
                      uint8_t* buf, size_t cap) {
    if (longName.empty() || longName.size() > kMaxLongNameBytes) return 0;
    if (shortName.empty() || shortName.size() > kMaxShortNameBytes) return 0;

    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
    copyField(admin.set_owner.long_name, sizeof(admin.set_owner.long_name), longName);
    copyField(admin.set_owner.short_name, sizeof(admin.set_owner.short_name), shortName);
    admin.set_owner.is_licensed = isLicensed;
    size_t keyLen = sessionPasskey.size();
    if (keyLen > sizeof(admin.session_passkey.bytes)) keyLen = sizeof(admin.session_passkey.bytes);
    memcpy(admin.session_passkey.bytes, sessionPasskey.data(), keyLen);
    admin.session_passkey.size = static_cast<pb_size_t>(keyLen);
    return encodeAdmin(packetId, myNodeNum, admin, false, buf, cap);
}

std::string nodeIdString(uint32_t num) {
    char buf[12];
    snprintf(buf, sizeof(buf), "!%08x", static_cast<unsigned>(num));
    return std::string(buf);
}

const char* hwModelName(uint32_t hwModel) {
    switch (hwModel) {
    case meshtastic_HardwareModel_UNSET: return "Unknown";
    case meshtastic_HardwareModel_TLORA_V2:
    case meshtastic_HardwareModel_TLORA_V1:
    case meshtastic_HardwareModel_TLORA_V2_1_1P6:
    case meshtastic_HardwareModel_TLORA_V2_1_1P8: return "T-LoRa";
    case meshtastic_HardwareModel_TLORA_T3_S3: return "T3-S3";
    case meshtastic_HardwareModel_TBEAM:
    case meshtastic_HardwareModel_TBEAM_V0P7:
    case meshtastic_HardwareModel_LILYGO_TBEAM_S3_CORE: return "T-Beam";
    case meshtastic_HardwareModel_T_ECHO: return "T-Echo";
    case meshtastic_HardwareModel_T_DECK: return "T-Deck";
    case meshtastic_HardwareModel_RAK4631: return "RAK4631";
    case meshtastic_HardwareModel_HELTEC_V2_0:
    case meshtastic_HardwareModel_HELTEC_V2_1: return "Heltec V2";
    case meshtastic_HardwareModel_HELTEC_V3: return "Heltec V3";
    case meshtastic_HardwareModel_HELTEC_V4: return "Heltec V4";
    case meshtastic_HardwareModel_HELTEC_WSL_V3: return "Heltec WSL3";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER:
    case meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER_V1_0: return "Heltec Trk";
    case meshtastic_HardwareModel_HELTEC_MESH_NODE_T114: return "Heltec T114";
    case meshtastic_HardwareModel_HELTEC_VISION_MASTER_T190:
    case meshtastic_HardwareModel_HELTEC_VISION_MASTER_E213:
    case meshtastic_HardwareModel_HELTEC_VISION_MASTER_E290: return "Vision Mstr";
    case meshtastic_HardwareModel_TRACKER_T1000_E: return "T1000-E";
    case meshtastic_HardwareModel_STATION_G2: return "Station G2";
    case meshtastic_HardwareModel_NANO_G2_ULTRA: return "Nano G2";
    case meshtastic_HardwareModel_SEEED_XIAO_S3: return "XIAO S3";
    case meshtastic_HardwareModel_M5STACK: return "M5Stack";
    case meshtastic_HardwareModel_M5STACK_CARDPUTER_ADV: return "Cardputer";
    case meshtastic_HardwareModel_PORTDUINO: return "Linux";
    case meshtastic_HardwareModel_ANDROID_SIM: return "Android";
    case meshtastic_HardwareModel_PRIVATE_HW: return "Private HW";
    default: return "Other";
    }
}

const char* roleName(uint32_t role) {
    switch (role) {
    case meshtastic_Config_DeviceConfig_Role_CLIENT: return "Client";
    case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE: return "Client mute";
    case meshtastic_Config_DeviceConfig_Role_ROUTER: return "Router";
    case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT: return "Router client";
    case meshtastic_Config_DeviceConfig_Role_REPEATER: return "Repeater";
    case meshtastic_Config_DeviceConfig_Role_TRACKER: return "Tracker";
    case meshtastic_Config_DeviceConfig_Role_SENSOR: return "Sensor";
    case meshtastic_Config_DeviceConfig_Role_TAK: return "TAK";
    case meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN: return "Hidden";
    case meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND: return "Lost+found";
    case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER: return "TAK tracker";
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE: return "Router late";
    case meshtastic_Config_DeviceConfig_Role_CLIENT_BASE: return "Client base";
    default: return "Other";
    }
}

} // namespace MeshProto
