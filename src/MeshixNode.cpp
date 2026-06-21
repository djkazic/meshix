#include "MeshixNode.h"
#include <SPIFFS.h>
#include <helpers/IdentityStore.h>
#include "target.h"

extern unsigned int encode_base64(const unsigned char* input, unsigned int input_length, unsigned char* output);

#define PUBLIC_CHANNEL_PSK "izOH6cXN6mrJ5e26oRXNcg=="

#define CHAT_MAGIC 0x4D584331  // "MXC1"

#define CHAN_MAGIC 0x4D584831  // "MXH1"

void MeshixNode::begin() {
  BaseChatMesh::begin();
  loadOrCreateIdentity();
  addChannel("Public", PUBLIC_CHANNEL_PSK);
  loadChannels();
  loadChat();
}

void MeshixNode::loadChannels() {
  File f = SPIFFS.open("/chans.bin", "r");
  if (!f) return;
  uint32_t magic = 0;
  f.read((uint8_t*)&magic, sizeof(magic));
  if (magic == CHAN_MAGIC) {
    uint8_t n = 0;
    f.read(&n, 1);
    for (int i = 0; i < n; i++) {
      char name[33];
      uint8_t len;
      uint8_t secret[32];
      if (f.read((uint8_t*)name, 32) != 32) break;
      name[32] = 0;
      f.read(&len, 1);
      f.read(secret, 32);
      char b64[48];
      encode_base64(secret, len, (unsigned char*)b64);
      addChannel(name, b64);
    }
  }
  f.close();
}

void MeshixNode::saveChannels() {
  File f = SPIFFS.open("/chans.bin", "w");
  if (!f) return;
  uint32_t magic = CHAN_MAGIC;
  f.write((uint8_t*)&magic, sizeof(magic));

  ChannelDetails d;
  int total = channelCount();
  uint8_t n = total > 1 ? total - 1 : 0;
  f.write(&n, 1);
  for (int i = 1; i < total; i++) {
    getChannel(i, d);
    uint8_t len = 16;
    for (int b = 16; b < 32; b++)
      if (d.channel.secret[b]) { len = 32; break; }
    f.write((uint8_t*)d.name, 32);
    f.write(&len, 1);
    f.write(d.channel.secret, 32);
  }
  f.close();
}

void MeshixNode::loadChat() {
  File f = SPIFFS.open("/chat.bin", "r");
  if (!f) return;
  uint32_t magic = 0;
  f.read((uint8_t*)&magic, sizeof(magic));
  if (magic == CHAT_MAGIC) {
    f.read((uint8_t*)&_msg_n, sizeof(_msg_n));
    f.read((uint8_t*)&_msg_head, sizeof(_msg_head));
    f.read((uint8_t*)_msgs, sizeof(_msgs));
    if (_msg_n < 0 || _msg_n > MSG_CAP || _msg_head < 0 || _msg_head >= MSG_CAP) {
      _msg_n = 0;
      _msg_head = MSG_CAP - 1;
    }
  }
  f.close();
}

void MeshixNode::saveChat() {
  File f = SPIFFS.open("/chat.bin", "w");
  if (!f) return;
  uint32_t magic = CHAT_MAGIC;
  f.write((uint8_t*)&magic, sizeof(magic));
  f.write((uint8_t*)&_msg_n, sizeof(_msg_n));
  f.write((uint8_t*)&_msg_head, sizeof(_msg_head));
  f.write((uint8_t*)_msgs, sizeof(_msgs));
  f.close();
}

void MeshixNode::persistTick() {
  if (_chat_dirty && millis() - _chat_flush > 4000) {
    saveChat();
    _chat_dirty = false;
    _chat_flush = millis();
  }
}

void MeshixNode::loadOrCreateIdentity() {
  IdentityStore store(SPIFFS, "/id");
  store.begin();

  if (store.load("self", self_id, _name, sizeof(_name)) && _name[0]) {
    return;
  }

  self_id = radio_new_identity();
  uint8_t* pk = self_id.pub_key;
  snprintf(_name, sizeof(_name), "Meshix-%02X%02X", pk[0], pk[1]);
  store.save("self", self_id, _name);
}

int MeshixNode::pushMsg(uint8_t kind, int chan, const uint8_t* peer6, bool mine, const char* text) {
  _msg_head = (_msg_head + 1) % MSG_CAP;
  if (_msg_n < MSG_CAP) _msg_n++;
  Msg& m = _msgs[_msg_head];
  m.ts = now();
  m.kind = kind;
  m.chan = chan;
  m.mine = mine;
  m.acked = false;
  if (peer6) memcpy(m.peer, peer6, 6);
  else memset(m.peer, 0, 6);
  strncpy(m.text, text, sizeof(m.text) - 1);
  m.text[sizeof(m.text) - 1] = 0;
  _rev++;
  _chat_dirty = true;
  return _msg_head;
}

int MeshixNode::gather(uint8_t kind, int chan, const uint8_t* peer6, const Msg** out, int max) {
  int count = 0;
  for (int i = 0; i < _msg_n && count < max; i++) {
    int idx = (_msg_head - i + MSG_CAP) % MSG_CAP;
    const Msg& m = _msgs[idx];
    if (m.kind != kind) continue;
    if (kind == CONV_CHANNEL && m.chan != chan) continue;
    if (kind == CONV_CONTACT && memcmp(m.peer, peer6, 6) != 0) continue;
    out[count++] = &m;
  }
  return count;
}

void MeshixNode::setName(const char* name) {
  if (!name[0]) return;
  strncpy(_name, name, sizeof(_name) - 1);
  _name[sizeof(_name) - 1] = 0;
  IdentityStore store(SPIFFS, "/id");
  store.begin();
  store.save("self", self_id, _name);
}

void MeshixNode::advertSelf() {
  mesh::Packet* pkt = createSelfAdvert(_name);
  if (pkt) {
    sendFlood(pkt);
    Serial.printf("advertised as %s\n", _name);
  }
}

bool MeshixNode::addNamedChannel(const char* name, const char* psk_base64) {
  if (addChannel(name, psk_base64) == NULL) return false;
  saveChannels();
  return true;
}

bool MeshixNode::addHashtagChannel(const char* name) {
  uint8_t secret[16];
  mesh::Utils::sha256(secret, sizeof(secret), (const uint8_t*)name, strlen(name));
  char b64[28];
  encode_base64(secret, sizeof(secret), (unsigned char*)b64);
  if (addChannel(name, b64) == NULL) return false;
  saveChannels();
  return true;
}

int MeshixNode::channelCount() {
  ChannelDetails d;
  int n = 0;
  while (getChannel(n, d) && d.name[0]) n++;
  return n;
}

bool MeshixNode::channelName(int idx, char* out, int n) {
  ChannelDetails d;
  if (!getChannel(idx, d)) return false;
  strncpy(out, d.name, n - 1);
  out[n - 1] = 0;
  return true;
}

bool MeshixNode::contactName(int idx, char* out, int n) {
  ContactInfo c;
  if (!getContactByIdx(idx, c)) return false;
  strncpy(out, c.name, n - 1);
  out[n - 1] = 0;
  return true;
}

bool MeshixNode::contactPeer(int idx, uint8_t out[6]) {
  ContactInfo c;
  if (!getContactByIdx(idx, c)) return false;
  memcpy(out, c.id.pub_key, 6);
  return true;
}

bool MeshixNode::sendChannel(int chan, const char* text) {
  ChannelDetails ch;
  if (!getChannel(chan, ch)) return false;
  if (!sendGroupMessage(now(), ch.channel, _name, text, strlen(text))) return false;
  pushMsg(CONV_CHANNEL, chan, NULL, true, text);
  return true;
}

bool MeshixNode::sendContact(int contactIdx, const char* text) {
  ContactInfo c;
  if (!getContactByIdx(contactIdx, c)) return false;

  uint32_t expected_ack, est_timeout;
  int res = sendMessage(c, now(), 0, text, expected_ack, est_timeout);
  if (res == MSG_SEND_FAILED) return false;

  _pending_msg = pushMsg(CONV_CONTACT, 0, c.id.pub_key, true, text);
  _pending_ack = expected_ack;
  _pending_contact = lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
  return true;
}

void MeshixNode::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  Serial.printf("contact %s: %s (%d hops)\n", is_new ? "NEW" : "updated", contact.name, path_len);
  _rev++;
}

void MeshixNode::onContactPathUpdated(const ContactInfo& contact) {
  Serial.printf("path updated for %s (%d hops)\n", contact.name, contact.out_path_len);
}

ContactInfo* MeshixNode::processAck(const uint8_t* data) {
  uint32_t crc;
  memcpy(&crc, data, 4);
  if (_pending_ack && crc == _pending_ack) {
    if (_pending_msg >= 0) _msgs[_pending_msg].acked = true;
    _pending_ack = 0;
    _pending_msg = -1;
    _rev++;
    _chat_dirty = true;
    Serial.println("ack received: delivered");
    return _pending_contact;
  }
  return NULL;
}

void MeshixNode::onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) {
  Serial.printf("[msg] %s: %s\n", contact.name, text);
  maybeSyncClock(sender_timestamp);
  pushMsg(CONV_CONTACT, 0, contact.id.pub_key, false, text);
  notifyContact(contact, text);
  _inbound++;
}

void MeshixNode::notifyContact(const ContactInfo& c, const char* text) {
  _notify.kind = CONV_CONTACT;
  memcpy(_notify.peer, c.id.pub_key, 6);
  strncpy(_notify.title, c.name, sizeof(_notify.title) - 1);
  _notify.title[sizeof(_notify.title) - 1] = 0;
  strncpy(_notify.preview, text, sizeof(_notify.preview) - 1);
  _notify.preview[sizeof(_notify.preview) - 1] = 0;
  _have_notify = true;
}

void MeshixNode::notifyChannel(int idx, const char* text) {
  _notify.kind = CONV_CHANNEL;
  _notify.chan = idx;
  channelName(idx, _notify.title, sizeof(_notify.title));
  strncpy(_notify.preview, text, sizeof(_notify.preview) - 1);
  _notify.preview[sizeof(_notify.preview) - 1] = 0;
  _have_notify = true;
}

bool MeshixNode::takeNotify(Notify& out) {
  if (!_have_notify) return false;
  out = _notify;
  _have_notify = false;
  return true;
}

void MeshixNode::maybeSyncClock(uint32_t ts) {
  static const uint32_t YEAR_2025 = 1735689600UL;
  if (ts > YEAR_2025 && now() < YEAR_2025) {
    getRTCClock()->setCurrentTime(ts);
  }
}

int MeshixNode::contactIndexByPeer(const uint8_t peer[6]) {
  ContactInfo c;
  int n = getNumContacts();
  for (int i = 0; i < n; i++) {
    if (getContactByIdx(i, c) && memcmp(c.id.pub_key, peer, 6) == 0) return i;
  }
  return -1;
}

void MeshixNode::onCommandDataRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) {
  Serial.printf("[cmd] %s: %s\n", contact.name, text);
}

void MeshixNode::onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t* sender_prefix, const char* text) {
  Serial.printf("[signed] %s: %s\n", contact.name, text);
  maybeSyncClock(sender_timestamp);
  pushMsg(CONV_CONTACT, 0, contact.id.pub_key, false, text);
  notifyContact(contact, text);
  _inbound++;
}

void MeshixNode::onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char* text) {
  int idx = findChannelIdx(channel);
  if (idx < 0) idx = 0;
  Serial.printf("[channel %d] %s\n", idx, text);
  maybeSyncClock(timestamp);
  pushMsg(CONV_CHANNEL, idx, NULL, false, text);
  notifyChannel(idx, text);
  _inbound++;
}

uint8_t MeshixNode::onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) {
  return 0;
}

void MeshixNode::onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) {}

uint32_t MeshixNode::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return 12000 + (pkt_airtime_millis * 2);
}

uint32_t MeshixNode::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  return 4000 + (pkt_airtime_millis * 2 * (path_len + 1));
}

void MeshixNode::onSendTimeout() {
  Serial.println("send timeout: no ack");
}
