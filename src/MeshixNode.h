#pragma once

#include <helpers/BaseChatMesh.h>

#define CONV_CHANNEL 0
#define CONV_CONTACT 1

struct Msg {
  uint32_t ts;
  uint8_t kind;
  uint8_t chan;
  uint8_t peer[6];
  bool mine;
  bool acked;
  char text[104];
};

struct Notify {
  uint8_t kind;
  int chan;
  uint8_t peer[6];
  char title[24];
  char preview[48];
};

class MeshixNode : public BaseChatMesh {
  static const int MSG_CAP = 48;
  char _name[32];
  Msg _msgs[MSG_CAP];
  int _msg_n;
  int _msg_head;
  uint32_t _rev;
  uint32_t _pending_ack;
  int _pending_msg;
  ContactInfo* _pending_contact;
  bool _chat_dirty;
  uint32_t _chat_flush;
  uint32_t _inbound;
  Notify _notify;
  bool _have_notify;

  void loadOrCreateIdentity();
  void loadChat();
  void saveChat();
  void loadChannels();
  void saveChannels();
  void notifyContact(const ContactInfo& c, const char* text);
  void notifyChannel(int idx, const char* text);
  void maybeSyncClock(uint32_t ts);
  int pushMsg(uint8_t kind, int chan, const uint8_t* peer6, bool mine, const char* text);

protected:
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo& contact) override;
  ContactInfo* processAck(const uint8_t* data) override;
  void onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override;
  void onCommandDataRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override;
  void onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t* sender_prefix, const char* text) override;
  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char* text) override;
  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) override;
  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override;
  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;

public:
  MeshixNode(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::PacketManager& mgr, mesh::MeshTables& tables)
    : BaseChatMesh(radio, ms, rng, rtc, mgr, tables), _msg_n(0), _msg_head(MSG_CAP - 1), _rev(0),
      _pending_ack(0), _pending_msg(-1), _pending_contact(NULL), _chat_dirty(false),
      _chat_flush(0), _inbound(0), _have_notify(false) {}

  void begin();
  void persistTick();
  const char* nodeName() const { return _name; }
  void setName(const char* name);
  uint32_t now() { return getRTCClock()->getCurrentTime(); }

  void advertSelf();
  bool addNamedChannel(const char* name, const char* psk_base64);
  bool addHashtagChannel(const char* name);

  int channelCount();
  bool channelName(int idx, char* out, int n);
  int contactCount() { return getNumContacts(); }
  bool contactName(int idx, char* out, int n);
  bool contactPeer(int idx, uint8_t out[6]);

  bool sendChannel(int chan, const char* text);
  bool sendContact(int contactIdx, const char* text);

  uint32_t rev() const { return _rev; }
  uint32_t inboundRev() const { return _inbound; }
  bool takeNotify(Notify& out);
  int contactIndexByPeer(const uint8_t peer[6]);
  int gather(uint8_t kind, int chan, const uint8_t* peer6, const Msg** out, int max);
};
