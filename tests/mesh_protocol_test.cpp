#include "check.h"

#include <cstring>

#include <pb_decode.h>
#include <pb_encode.h>
#include "core/mesh_protocol.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/mesh.pb.h"

using namespace MeshProto;

namespace {

meshtastic_ToRadio decodeToRadio(const uint8_t* buf, size_t len) {
    static meshtastic_ToRadio tr;
    tr = meshtastic_ToRadio_init_zero;
    pb_istream_t s = pb_istream_from_buffer(buf, len);
    bool ok = pb_decode(&s, &meshtastic_ToRadio_msg, &tr);
    CHECK(ok);
    return tr;
}

meshtastic_AdminMessage decodeAdmin(const meshtastic_Data& d) {
    meshtastic_AdminMessage a = meshtastic_AdminMessage_init_zero;
    pb_istream_t s = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
    bool ok = pb_decode(&s, &meshtastic_AdminMessage_msg, &a);
    CHECK(ok);
    return a;
}

std::vector<uint8_t> encodeFromRadio(const meshtastic_FromRadio& fr) {
    std::vector<uint8_t> buf(1024);
    pb_ostream_t s = pb_ostream_from_buffer(buf.data(), buf.size());
    bool ok = pb_encode(&s, &meshtastic_FromRadio_msg, &fr);
    CHECK(ok);
    buf.resize(s.bytes_written);
    return buf;
}

template <typename Msg>
void packInto(meshtastic_Data& d, const pb_msgdesc_t* desc, const Msg& msg) {
    pb_ostream_t s = pb_ostream_from_buffer(d.payload.bytes, sizeof(d.payload.bytes));
    bool ok = pb_encode(&s, desc, &msg);
    CHECK(ok);
    d.payload.size = static_cast<pb_size_t>(s.bytes_written);
}

Event decode(const meshtastic_FromRadio& fr) {
    std::vector<uint8_t> bytes = encodeFromRadio(fr);
    Event ev;
    bool ok = decodeFromRadio(bytes.data(), bytes.size(), ev);
    CHECK(ok);
    return ev;
}

meshtastic_FromRadio packetFrame(uint32_t from, uint32_t to, meshtastic_PortNum port) {
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_packet_tag;
    fr.packet.from = from;
    fr.packet.to = to;
    fr.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    fr.packet.decoded.portnum = port;
    return fr;
}

} // namespace

TEST(encode_text_dm_with_ack) {
    uint8_t buf[kMaxToRadioBytes];
    size_t n = encodeText(0x1234, 0xA1B2C3D4, 0, "hello mesh", true, buf, sizeof(buf));
    CHECK(n > 0);
    meshtastic_ToRadio tr = decodeToRadio(buf, n);
    CHECK_EQ(tr.which_payload_variant, (pb_size_t)meshtastic_ToRadio_packet_tag);
    CHECK_EQ(tr.packet.to, 0xA1B2C3D4u);
    CHECK_EQ(tr.packet.id, 0x1234u);
    CHECK_EQ(tr.packet.channel, 0);
    CHECK(tr.packet.want_ack);
    CHECK_EQ(tr.packet.hop_limit, 3);
    CHECK_EQ((int)tr.packet.decoded.portnum, (int)meshtastic_PortNum_TEXT_MESSAGE_APP);
    CHECK_EQ(std::string((const char*)tr.packet.decoded.payload.bytes, tr.packet.decoded.payload.size),
             std::string("hello mesh"));
}

TEST(encode_text_channel_broadcast) {
    uint8_t buf[kMaxToRadioBytes];
    size_t n = encodeText(7, kBroadcast, 2, "all", true, buf, sizeof(buf));
    meshtastic_ToRadio tr = decodeToRadio(buf, n);
    CHECK_EQ(tr.packet.to, kBroadcast);
    CHECK_EQ(tr.packet.channel, 2);
}

TEST(encode_text_rejects_empty_and_oversize) {
    uint8_t buf[kMaxToRadioBytes];
    CHECK_EQ(encodeText(1, 2, 0, "", true, buf, sizeof(buf)), (size_t)0);
    std::string big(kMaxTextBytes + 1, 'x');
    CHECK_EQ(encodeText(1, 2, 0, big, true, buf, sizeof(buf)), (size_t)0);
    std::string max(kMaxTextBytes, 'x');
    CHECK(encodeText(1, 2, 0, max, true, buf, sizeof(buf)) > 0);
}

TEST(encode_want_config_and_disconnect) {
    uint8_t buf[kMaxToRadioBytes];
    size_t n = encodeWantConfig(0xBEEF, buf, sizeof(buf));
    meshtastic_ToRadio tr = decodeToRadio(buf, n);
    CHECK_EQ(tr.which_payload_variant, (pb_size_t)meshtastic_ToRadio_want_config_id_tag);
    CHECK_EQ(tr.want_config_id, 0xBEEFu);

    n = encodeDisconnect(buf, sizeof(buf));
    tr = decodeToRadio(buf, n);
    CHECK_EQ(tr.which_payload_variant, (pb_size_t)meshtastic_ToRadio_disconnect_tag);
    CHECK(tr.disconnect);
}

TEST(encode_get_owner_targets_local_node) {
    uint8_t buf[kMaxToRadioBytes];
    size_t n = encodeGetOwner(99, 0x0A0B0C0D, buf, sizeof(buf));
    meshtastic_ToRadio tr = decodeToRadio(buf, n);
    CHECK_EQ(tr.packet.to, 0x0A0B0C0Du);
    CHECK_EQ((int)tr.packet.decoded.portnum, (int)meshtastic_PortNum_ADMIN_APP);
    CHECK(tr.packet.decoded.want_response);
    meshtastic_AdminMessage a = decodeAdmin(tr.packet.decoded);
    CHECK_EQ(a.which_payload_variant, (pb_size_t)meshtastic_AdminMessage_get_owner_request_tag);
}

TEST(encode_set_owner_carries_names_and_passkey) {
    uint8_t buf[kMaxToRadioBytes];
    std::vector<uint8_t> key = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t n = encodeSetOwner(5, 0x11223344, "Cardputer Anna", "ANNA", false, key, buf, sizeof(buf));
    CHECK(n > 0);
    meshtastic_ToRadio tr = decodeToRadio(buf, n);
    CHECK_EQ(tr.packet.to, 0x11223344u);
    meshtastic_AdminMessage a = decodeAdmin(tr.packet.decoded);
    CHECK_EQ(a.which_payload_variant, (pb_size_t)meshtastic_AdminMessage_set_owner_tag);
    CHECK_EQ(std::string(a.set_owner.long_name), std::string("Cardputer Anna"));
    CHECK_EQ(std::string(a.set_owner.short_name), std::string("ANNA"));
    CHECK_EQ(a.set_owner.id[0], '\0');  // never overwrite the node id
    CHECK_EQ(a.session_passkey.size, (pb_size_t)8);
    CHECK_EQ(a.session_passkey.bytes[7], 8);
}

TEST(encode_set_owner_validates_lengths) {
    uint8_t buf[kMaxToRadioBytes];
    std::vector<uint8_t> none;
    CHECK_EQ(encodeSetOwner(1, 1, "", "AB", false, none, buf, sizeof(buf)), (size_t)0);
    CHECK_EQ(encodeSetOwner(1, 1, "Name", "", false, none, buf, sizeof(buf)), (size_t)0);
    CHECK_EQ(encodeSetOwner(1, 1, "Name", "ABCDE", false, none, buf, sizeof(buf)), (size_t)0);
    CHECK_EQ(encodeSetOwner(1, 1, std::string(40, 'n'), "AB", false, none, buf, sizeof(buf)), (size_t)0);
    CHECK(encodeSetOwner(1, 1, std::string(39, 'n'), "ABCD", false, none, buf, sizeof(buf)) > 0);
}

TEST(decode_my_info) {
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    fr.my_info.my_node_num = 0xDA6573C0;  // above 2^31 on purpose
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::MyInfo);
    CHECK_EQ(ev.myNodeNum, 0xDA6573C0u);
}

TEST(decode_node_info_with_user) {
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    meshtastic_NodeInfo& n = fr.node_info;
    n.num = 0x12345678;
    n.has_user = true;
    strcpy(n.user.id, "!12345678");
    strcpy(n.user.long_name, "Base Camp");
    strcpy(n.user.short_name, "BC");
    n.user.hw_model = meshtastic_HardwareModel_HELTEC_V3;
    n.user.public_key.size = 32;
    n.has_hops_away = true;
    n.hops_away = 2;
    n.snr = 6.5f;
    n.last_heard = 1760000000;
    n.has_device_metrics = true;
    n.device_metrics.has_battery_level = true;
    n.device_metrics.battery_level = 87;
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::NodeInfo);
    CHECK_EQ(ev.node.num, 0x12345678u);
    CHECK(ev.node.hasUser);
    CHECK_EQ(ev.node.user.longName, std::string("Base Camp"));
    CHECK_EQ(ev.node.user.shortName, std::string("BC"));
    CHECK_EQ(ev.node.user.id, std::string("!12345678"));
    CHECK_EQ(std::string(hwModelName(ev.node.user.hwModel)), std::string("Heltec V3"));
    CHECK(ev.node.user.hasPublicKey);
    CHECK(ev.node.hasHopsAway);
    CHECK_EQ(ev.node.hopsAway, 2);
    CHECK_EQ(ev.node.batteryLevel, 87u);
    CHECK_EQ(ev.node.lastHeard, 1760000000u);
}

TEST(decode_channel) {
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_channel_tag;
    fr.channel.index = 1;
    fr.channel.role = meshtastic_Channel_Role_SECONDARY;
    fr.channel.has_settings = true;
    strcpy(fr.channel.settings.name, "friends");
    fr.channel.settings.psk.size = 16;
    fr.channel.settings.psk.bytes[0] = 0xAB;
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::Channel);
    CHECK_EQ((int)ev.channel.index, 1);
    CHECK_EQ(ev.channel.role, 2);
    CHECK_EQ(ev.channel.name, std::string("friends"));
    CHECK_EQ(ev.channel.pskLen, 16);
}

TEST(decode_config_complete) {
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    fr.config_complete_id = 4242;
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::ConfigComplete);
    CHECK_EQ(ev.configId, 4242u);
}

TEST(decode_text_packet) {
    meshtastic_FromRadio fr = packetFrame(0xAAAA0001, 0xBBBB0002, meshtastic_PortNum_TEXT_MESSAGE_APP);
    fr.packet.id = 77;
    fr.packet.channel = 0;
    fr.packet.rx_time = 1760000123;
    fr.packet.pki_encrypted = true;
    const char* msg = "hi \xF0\x9F\x91\x8D";
    fr.packet.decoded.payload.size = (pb_size_t)strlen(msg);
    memcpy(fr.packet.decoded.payload.bytes, msg, strlen(msg));
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::Text);
    CHECK_EQ(ev.text.from, 0xAAAA0001u);
    CHECK_EQ(ev.text.to, 0xBBBB0002u);
    CHECK_EQ(ev.text.id, 77u);
    CHECK_EQ(ev.text.text, std::string(msg));
    CHECK_EQ(ev.text.rxTime, 1760000123u);
    CHECK(ev.text.pkiEncrypted);
}

TEST(decode_nodeinfo_app_is_identity_update) {
    meshtastic_FromRadio fr = packetFrame(0xCAFE0001, kBroadcast, meshtastic_PortNum_NODEINFO_APP);
    meshtastic_User u = meshtastic_User_init_zero;
    strcpy(u.id, "!cafe0001");
    strcpy(u.long_name, "Renamed Peer");
    strcpy(u.short_name, "RP");
    packInto(fr.packet.decoded, &meshtastic_User_msg, u);
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::UserUpdate);
    CHECK_EQ(ev.node.num, 0xCAFE0001u);
    CHECK_EQ(ev.node.user.longName, std::string("Renamed Peer"));
    CHECK_EQ(ev.node.user.shortName, std::string("RP"));
}

TEST(decode_routing_ack_and_nak) {
    meshtastic_FromRadio fr = packetFrame(0xD0D0D0D0, 0x1, meshtastic_PortNum_ROUTING_APP);
    fr.packet.decoded.request_id = 0x5555;
    meshtastic_Routing r = meshtastic_Routing_init_zero;
    r.which_variant = meshtastic_Routing_error_reason_tag;
    r.error_reason = meshtastic_Routing_Error_NONE;
    packInto(fr.packet.decoded, &meshtastic_Routing_msg, r);
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::Routing);
    CHECK_EQ(ev.routing.requestId, 0x5555u);
    CHECK_EQ(ev.routing.from, 0xD0D0D0D0u);
    CHECK_EQ(ev.routing.error, (int)kRoutingNone);

    r.error_reason = meshtastic_Routing_Error_MAX_RETRANSMIT;
    packInto(fr.packet.decoded, &meshtastic_Routing_msg, r);
    ev = decode(fr);
    CHECK_EQ(ev.routing.error, (int)kRoutingMaxRetransmit);
}

TEST(decode_admin_owner_response_with_passkey) {
    meshtastic_FromRadio fr = packetFrame(0x10, 0x10, meshtastic_PortNum_ADMIN_APP);
    meshtastic_AdminMessage a = meshtastic_AdminMessage_init_zero;
    a.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;
    strcpy(a.get_owner_response.long_name, "Old Name");
    strcpy(a.get_owner_response.short_name, "OLD");
    a.session_passkey.size = 8;
    for (int i = 0; i < 8; ++i) a.session_passkey.bytes[i] = (uint8_t)(0xA0 + i);
    packInto(fr.packet.decoded, &meshtastic_AdminMessage_msg, a);
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::Admin);
    CHECK(ev.admin.hasOwner);
    CHECK_EQ(ev.admin.owner.longName, std::string("Old Name"));
    CHECK_EQ(ev.admin.sessionPasskey.size(), (size_t)8);
    CHECK_EQ(ev.admin.sessionPasskey[0], 0xA0);
}

TEST(decode_lora_preset_names_the_default_channel) {
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_config_tag;
    fr.config.which_payload_variant = meshtastic_Config_lora_tag;
    fr.config.payload_variant.lora.use_preset = true;
    fr.config.payload_variant.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    fr.config.payload_variant.lora.hop_limit = 5;
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::ModemPreset);
    CHECK_EQ(ev.hopLimit, 5);
    CHECK_EQ(ev.message, std::string("MediumFast"));

    fr.config.payload_variant.lora.use_preset = false;
    CHECK_EQ(decode(fr).message, std::string("Custom"));
}

TEST(decode_encrypted_packet_is_ignored) {
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_packet_tag;
    fr.packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    fr.packet.encrypted.size = 4;
    Event ev = decode(fr);
    CHECK(ev.kind == EventKind::Ignored);
}

TEST(decode_rejects_garbage) {
    const uint8_t junk[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
    Event ev;
    CHECK(!decodeFromRadio(junk, sizeof(junk), ev));
}

TEST(node_id_string_format) {
    CHECK_EQ(nodeIdString(0xDA6573C0), std::string("!da6573c0"));
    CHECK_EQ(nodeIdString(0x1), std::string("!00000001"));
}
