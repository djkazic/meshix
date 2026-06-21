#include "UI.h"
#include <SPIFFS.h>

#define COL_BG     0x0000
#define COL_FG     0xFFFF
#define COL_BAR    0x18C3
#define COL_PANEL  0x10A2
#define COL_KEY    0x3186
#define COL_ACCENT 0x051F
#define COL_GREEN  0x05E0
#define COL_GOLD   0xFE60
#define COL_GRAY   0x8410
#define COL_RED    0xF800

static const char* const LAYER_LO[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
static const char* const LAYER_SY[3] = {"1234567890", "@#&%*-+/=", "_,.:;!?()"};
static const int KB_TOP = 74;
static const int KB_ROW_H = 41;

static const char* const CANNED_MSGS[] = {
  "OK", "On my way", "Yes", "No", "Running late", "Need help", "Where are you?", "Acknowledged",
};
static const int CANNED_COUNT = sizeof(CANNED_MSGS) / sizeof(CANNED_MSGS[0]);

static const int LIST_TOP = 28;
static const int LIST_ROW_H = 29;
static const int BACK_H = 30;
static const uint8_t BL_BRIGHT = 160;

struct PrefsBlob {
  uint8_t magic;
  uint8_t clock24;
  int8_t tz;
  uint8_t sleep_secs;
  uint8_t favN;
  uint8_t favs[8][6];
};

static const uint8_t SLEEP_OPTS[] = {15, 30, 60, 120, 0};
static const int SLEEP_OPT_COUNT = sizeof(SLEEP_OPTS);

void UI::fmtTime(uint32_t ts, char* out) {
  if (ts < 1000000000UL) {
    strcpy(out, "--:--");
    return;
  }
  long local = (long)ts + _tz * 3600L;
  long s = ((local % 86400L) + 86400L) % 86400L;
  int h = s / 3600;
  int m = (s % 3600) / 60;
  if (_clock24) {
    snprintf(out, 8, "%02d:%02d", h, m);
  } else {
    int hh = h % 12;
    if (hh == 0) hh = 12;
    snprintf(out, 8, "%d:%02d%c", hh, m, h < 12 ? 'a' : 'p');
  }
}

void UI::loadPrefs() {
  File f = SPIFFS.open("/prefs.bin", "r");
  if (!f) return;
  PrefsBlob p;
  if (f.read((uint8_t*)&p, sizeof(p)) == sizeof(p) && p.magic == 0x4F) {
    _clock24 = p.clock24;
    _tz = p.tz;
    _sleep_secs = p.sleep_secs;
    _favN = p.favN > MAX_FAVS ? MAX_FAVS : p.favN;
    memcpy(_favs, p.favs, sizeof(_favs));
  }
  f.close();
}

void UI::savePrefs() {
  PrefsBlob p{};
  p.magic = 0x4F;
  p.clock24 = _clock24;
  p.tz = _tz;
  p.sleep_secs = _sleep_secs;
  p.favN = _favN;
  memcpy(p.favs, _favs, sizeof(_favs));
  File f = SPIFFS.open("/prefs.bin", "w");
  if (!f) return;
  f.write((uint8_t*)&p, sizeof(p));
  f.close();
}

bool UI::isFav(const uint8_t peer[6]) {
  for (int i = 0; i < _favN; i++)
    if (memcmp(_favs[i], peer, 6) == 0) return true;
  return false;
}

void UI::toggleFav(const uint8_t peer[6]) {
  for (int i = 0; i < _favN; i++) {
    if (memcmp(_favs[i], peer, 6) == 0) {
      for (int j = i; j < _favN - 1; j++) memcpy(_favs[j], _favs[j + 1], 6);
      _favN--;
      savePrefs();
      return;
    }
  }
  if (_favN < MAX_FAVS) {
    memcpy(_favs[_favN++], peer, 6);
    savePrefs();
  }
}

void UI::begin() {
  _tft.init();
  _tft.setRotation(2);
  _tft.setBrightness(BL_BRIGHT);
  _canvas.setColorDepth(16);
  _canvas.setPsram(true);
  _canvas.createSprite(240, 240);
  loadPrefs();
  _last_activity = millis();
  _dirty = true;
}

UI::Screen UI::backTarget() {
  switch (_screen) {
    case COMPOSE: return _entry == ENTRY_MESSAGE ? THREAD : SETTINGS;
    case CANNED: return THREAD;
    case CONTACTS: return SETTINGS;
    default: return HOME;
  }
}

void UI::go(Screen s) {
  _screen = s;
  _dirty = true;
}

void UI::wake() {
  _asleep = false;
  _tft.setBrightness(BL_BRIGHT);
  _dirty = true;
}

int UI::buildHome(uint8_t kinds[], int idxs[], int max) {
  int n = 0;
  int nchan = _mesh.channelCount();
  for (int i = 0; i < nchan && n < max; i++) {
    kinds[n] = CONV_CHANNEL;
    idxs[n] = i;
    n++;
  }
  int nc = _mesh.contactCount();
  for (int i = 0; i < nc && n < max; i++) {
    uint8_t p[6];
    if (_mesh.contactPeer(i, p) && isFav(p)) {
      kinds[n] = CONV_CONTACT;
      idxs[n] = i;
      n++;
    }
  }
  return n;
}

void UI::openConversation(uint8_t kind, int idx) {
  _conv_kind = kind;
  if (kind == CONV_CHANNEL) {
    _conv_chan = idx;
    _mesh.channelName(idx, _conv_name, sizeof(_conv_name));
  } else {
    _mesh.contactName(idx, _conv_name, sizeof(_conv_name));
    _mesh.contactPeer(idx, _conv_peer);
  }
  _thread_scroll = 0;
  go(THREAD);
}

void UI::startEntry(uint8_t purpose, const char* title) {
  _entry = purpose;
  strncpy(_entry_title, title, sizeof(_entry_title) - 1);
  _entry_title[sizeof(_entry_title) - 1] = 0;
  _compose_len = 0;
  _compose[0] = 0;
  _shift = false;
  _sym = false;
  go(COMPOSE);
}

void UI::doneEntry() {
  switch (_entry) {
    case ENTRY_MESSAGE:
      send(_compose);
      go(THREAD);
      break;
    case ENTRY_CHAN_NAME:
      if (_compose[0] == '#') {
        if (!_mesh.addHashtagChannel(_compose)) Serial.println("addchan failed");
        go(SETTINGS);
      } else {
        strncpy(_new_chan, _compose, sizeof(_new_chan) - 1);
        _new_chan[sizeof(_new_chan) - 1] = 0;
        startEntry(ENTRY_CHAN_PSK, "Channel PSK (base64)");
      }
      break;
    case ENTRY_CHAN_PSK:
      if (!_mesh.addNamedChannel(_new_chan, _compose)) Serial.println("addchan failed");
      go(SETTINGS);
      break;
    case ENTRY_NAME:
      _mesh.setName(_compose);
      go(SETTINGS);
      break;
  }
}

void UI::send(const char* text) {
  if (!text[0]) return;
  bool ok = _conv_kind == CONV_CHANNEL ? _mesh.sendChannel(_conv_chan, text)
                                       : _mesh.sendContactByPeer(_conv_peer, text);
  if (!ok) Serial.println("ui: send failed");
}

void UI::drawStatusBar() {
  _canvas.fillRect(0, 0, 240, 24, COL_BAR);
  _canvas.setTextColor(COL_FG, COL_BAR);
  _canvas.setTextSize(1);

  if (_screen == HOME) {
    _canvas.setTextDatum(lgfx::middle_left);
    _canvas.drawString(_mesh.nodeName(), 6, 12);
    _canvas.setTextColor(COL_GRAY, COL_BAR);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.drawString("v", 120, 12);
    _canvas.setTextColor(COL_FG, COL_BAR);
  } else {
    const char* title = _screen == SETTINGS ? "Settings"
                        : _screen == CONTACTS ? "Contacts"
                        : _screen == COMPOSE ? _entry_title
                                             : _conv_name;
    char t[34];
    snprintf(t, sizeof(t), "< %.28s", title);
    _canvas.setTextDatum(lgfx::middle_left);
    _canvas.drawString(t, 6, 12);
  }

  char clk[8];
  fmtTime(_mesh.now(), clk);
  _canvas.setTextDatum(lgfx::middle_right);
  _canvas.drawString(clk, 176, 12);

  int pct = _board.batteryPercent();
  char b[8];
  snprintf(b, sizeof(b), "%d%%", pct);
  _canvas.drawString(b, 212, 12);

  int bx = 216, by = 7, bw = 18, bh = 10;
  _canvas.drawRect(bx, by, bw, bh, COL_FG);
  _canvas.fillRect(bx + bw, by + 3, 2, 4, COL_FG);
  uint16_t fc = _board.isCharging() ? COL_GREEN : (pct <= 15 ? COL_RED : COL_FG);
  _canvas.fillRect(bx + 1, by + 1, (bw - 2) * pct / 100, bh - 2, fc);
}

void UI::drawHome() {
  _canvas.fillRect(0, 24, 240, 216, COL_BG);
  _canvas.setTextDatum(lgfx::middle_left);
  _canvas.setTextSize(2);

  uint8_t kinds[16];
  int idxs[16];
  int n = buildHome(kinds, idxs, 16);
  int y = LIST_TOP;
  char name[24];
  for (int i = 0; i < n && y < 208; i++) {
    bool chan = kinds[i] == CONV_CHANNEL;
    if (chan) _mesh.channelName(idxs[i], name, sizeof(name));
    else _mesh.contactName(idxs[i], name, sizeof(name));
    _canvas.fillRect(4, y, 232, LIST_ROW_H - 2, COL_PANEL);
    _canvas.fillRect(4, y, 4, LIST_ROW_H - 2, chan ? COL_ACCENT : COL_GREEN);
    _canvas.setTextColor(COL_FG, COL_PANEL);
    _canvas.drawString(name, 18, y + LIST_ROW_H / 2);
    y += LIST_ROW_H;
  }
}

int UI::composeLines(const Msg* m, char lines[][46], int maxlines) {
  char ts[8];
  fmtTime(m->ts, ts);
  const char* who = m->mine ? "me" : (_conv_kind == CONV_CONTACT ? _conv_name : "");
  snprintf(lines[0], 46, "%s %s", ts, who);
  int nl = 1;

  char body[140];
  if (m->mine && _conv_kind == CONV_CONTACT) {
    snprintf(body, sizeof(body), "%s %s", m->text, m->acked ? "[ok]" : "[..]");
  } else {
    snprintf(body, sizeof(body), "%s", m->text);
  }

  const int WRAP = 36;
  int len = strlen(body), i = 0;
  while (i < len && nl < maxlines) {
    int take = len - i > WRAP ? WRAP : len - i;
    if (i + take < len) {
      int br = take;
      while (br > 0 && body[i + br] != ' ') br--;
      if (br > 0) take = br;
    }
    memcpy(lines[nl], body + i, take);
    lines[nl][take] = 0;
    nl++;
    i += take;
    while (i < len && body[i] == ' ') i++;
  }
  return nl;
}

void UI::drawThread() {
  _canvas.fillRect(0, 24, 240, 216, COL_BG);
  _canvas.setTextDatum(lgfx::top_left);
  _canvas.setTextSize(1);

  const Msg* msgs[48];
  int n = _mesh.gather(_conv_kind, _conv_chan, _conv_peer, msgs, 48);
  if (n == 0) {
    _canvas.setTextColor(COL_GRAY, COL_BG);
    _canvas.drawString("no messages", 8, 28);
  }

  const int VP = 15;
  char lines[5][46];
  int total = 0;
  for (int k = n - 1; k >= 0; k--) total += composeLines(msgs[k], lines, 5);

  int maxscroll = total > VP ? total - VP : 0;
  if (_thread_scroll > maxscroll) _thread_scroll = maxscroll;
  int topLine = total - VP - _thread_scroll;
  if (topLine < 0) topLine = 0;

  int gl = 0, y = 28;
  for (int k = n - 1; k >= 0; k--) {
    int c = composeLines(msgs[k], lines, 5);
    for (int li = 0; li < c; li++) {
      if (gl >= topLine && y < 194) {
        uint16_t col = li == 0 ? (msgs[k]->mine ? COL_GREEN : COL_ACCENT) : COL_FG;
        _canvas.setTextColor(col, COL_BG);
        _canvas.drawString(lines[li], 8, y);
        y += 11;
      }
      gl++;
    }
  }

  _canvas.fillRect(4, 200, 114, 38, COL_KEY);
  _canvas.fillRect(122, 200, 114, 38, COL_KEY);
  _canvas.setTextColor(COL_FG, COL_KEY);
  _canvas.setTextDatum(lgfx::middle_center);
  _canvas.setTextSize(2);
  _canvas.drawString("Type", 61, 219);
  _canvas.drawString("Canned", 179, 219);
}

void UI::drawCompose() {
  _canvas.fillRect(0, 24, 240, 216, COL_BG);
  _canvas.fillRect(0, 24, 240, 48, COL_PANEL);
  _canvas.setTextDatum(lgfx::middle_left);
  _canvas.setTextSize(1);
  int off = _compose_len > 34 ? _compose_len - 34 : 0;
  char shown[40];
  snprintf(shown, sizeof(shown), "%s_", _compose + off);
  _canvas.setTextColor(COL_FG, COL_PANEL);
  _canvas.drawString(shown, 8, 48);

  const int kw = 24;
  const char* const* layer = _sym ? LAYER_SY : LAYER_LO;
  for (int row = 0; row < 3; row++) {
    const char* r = layer[row];
    int len = strlen(r);
    int startx = (240 - len * kw) / 2;
    int ky = KB_TOP + row * KB_ROW_H;
    for (int i = 0; i < len; i++) {
      char c = r[i];
      if (!_sym && _shift && c >= 'a' && c <= 'z') c -= 32;
      int kx = startx + i * kw;
      _canvas.fillRect(kx + 1, ky + 1, kw - 2, KB_ROW_H - 2, COL_KEY);
      char s[2] = {c, 0};
      _canvas.setTextColor(COL_FG, COL_KEY);
      _canvas.setTextDatum(lgfx::middle_center);
      _canvas.setTextSize(2);
      _canvas.drawString(s, kx + kw / 2, ky + KB_ROW_H / 2);
    }
  }

  int ky = KB_TOP + 3 * KB_ROW_H;
  int h = KB_ROW_H - 2;
  _canvas.fillRect(1, ky + 1, 42, h, COL_KEY);
  _canvas.fillRect(45, ky + 1, 38, h, _shift ? COL_ACCENT : COL_KEY);
  _canvas.fillRect(85, ky + 1, 70, h, COL_KEY);
  _canvas.fillRect(157, ky + 1, 38, h, COL_KEY);
  _canvas.fillRect(197, ky + 1, 42, h, COL_GREEN);
  _canvas.setTextDatum(lgfx::middle_center);
  _canvas.setTextSize(2);
  _canvas.setTextColor(COL_FG, COL_KEY);
  _canvas.drawString(_sym ? "abc" : "123", 22, ky + KB_ROW_H / 2);
  _canvas.setTextColor(COL_FG, _shift ? COL_ACCENT : COL_KEY);
  _canvas.drawString("sh", 64, ky + KB_ROW_H / 2);
  _canvas.setTextColor(COL_FG, COL_KEY);
  _canvas.drawString("space", 120, ky + KB_ROW_H / 2);
  _canvas.drawString("del", 176, ky + KB_ROW_H / 2);
  _canvas.setTextColor(COL_FG, COL_GREEN);
  _canvas.drawString("ok", 218, ky + KB_ROW_H / 2);
}

void UI::drawCanned() {
  _canvas.fillRect(0, 24, 240, 216, COL_BG);
  int rh = 210 / CANNED_COUNT;
  _canvas.setTextDatum(lgfx::middle_left);
  _canvas.setTextSize(2);
  for (int i = 0; i < CANNED_COUNT; i++) {
    int y = 30 + i * rh;
    _canvas.fillRect(4, y + 1, 232, rh - 2, i & 1 ? COL_PANEL : COL_KEY);
    _canvas.setTextColor(COL_FG, i & 1 ? COL_PANEL : COL_KEY);
    _canvas.drawString(CANNED_MSGS[i], 14, y + rh / 2);
  }
}

void UI::drawSettings() {
  _canvas.fillRect(0, 24, 240, 216, COL_BG);

  const int H = 27;
  _canvas.setTextSize(2);

  _canvas.fillRect(8, 28, 224, H, COL_KEY);
  _canvas.fillRect(8, 57, 224, H, COL_KEY);
  _canvas.fillRect(8, 86, 224, H, COL_KEY);
  _canvas.fillRect(8, 115, 224, H, COL_KEY);
  _canvas.setTextColor(COL_FG, COL_KEY);
  _canvas.setTextDatum(lgfx::middle_left);
  _canvas.drawString("Name", 16, 41);
  _canvas.drawString("Advertise self", 16, 70);
  _canvas.drawString("Add channel", 16, 99);
  _canvas.drawString("Contacts", 16, 128);
  _canvas.setTextDatum(lgfx::middle_right);
  _canvas.drawString(_mesh.nodeName(), 224, 41);

  _canvas.fillRect(8, 144, 224, H, COL_PANEL);
  _canvas.setTextColor(COL_FG, COL_PANEL);
  _canvas.setTextDatum(lgfx::middle_left);
  _canvas.drawString("Clock", 16, 157);
  _canvas.setTextDatum(lgfx::middle_right);
  _canvas.drawString(_clock24 ? "24h" : "12h", 224, 157);

  _canvas.fillRect(8, 173, 224, H, COL_PANEL);
  _canvas.setTextDatum(lgfx::middle_left);
  _canvas.drawString("UTC offset", 16, 186);
  _canvas.fillRect(150, 175, 24, 22, COL_KEY);
  _canvas.fillRect(208, 175, 24, 22, COL_KEY);
  _canvas.setTextDatum(lgfx::middle_center);
  _canvas.drawString("-", 162, 186);
  _canvas.drawString("+", 220, 186);
  char tz[8];
  snprintf(tz, sizeof(tz), "%+dh", _tz);
  _canvas.drawString(tz, 191, 186);

  _canvas.fillRect(8, 202, 224, H, COL_PANEL);
  _canvas.setTextDatum(lgfx::middle_left);
  _canvas.drawString("Sleep", 16, 215);
  char sl[8];
  if (_sleep_secs == 0) strcpy(sl, "off");
  else if (_sleep_secs < 60) snprintf(sl, sizeof(sl), "%ds", _sleep_secs);
  else snprintf(sl, sizeof(sl), "%dm", _sleep_secs / 60);
  _canvas.setTextDatum(lgfx::middle_right);
  _canvas.drawString(sl, 224, 215);
}

void UI::drawContacts() {
  _canvas.fillRect(0, 24, 240, 216, COL_BG);
  int n = _mesh.contactCount();
  if (n == 0) {
    _canvas.setTextDatum(lgfx::top_left);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COL_GRAY, COL_BG);
    _canvas.drawString("no contacts yet", 12, 32);
    return;
  }
  _canvas.setTextSize(2);
  char name[24];
  uint8_t p[6];
  int y = 30;
  for (int i = 0; i < n && y < 234; i++) {
    _mesh.contactName(i, name, sizeof(name));
    _mesh.contactPeer(i, p);
    bool fav = isFav(p);
    _canvas.fillRect(4, y, 232, 26, COL_PANEL);
    _canvas.setTextDatum(lgfx::middle_left);
    _canvas.setTextColor(COL_FG, COL_PANEL);
    _canvas.drawString(name, 14, y + 13);
    _canvas.setTextDatum(lgfx::middle_right);
    _canvas.setTextColor(fav ? COL_GOLD : COL_GRAY, COL_PANEL);
    _canvas.drawString(fav ? "*" : "+", 226, y + 13);
    y += 28;
  }
}

void UI::draw() {
  drawStatusBar();
  switch (_screen) {
    case HOME: drawHome(); break;
    case THREAD: drawThread(); break;
    case COMPOSE: drawCompose(); break;
    case CANNED: drawCanned(); break;
    case SETTINGS: drawSettings(); break;
    case CONTACTS: drawContacts(); break;
  }
  if (_popup) drawPopup();
  _canvas.pushSprite(0, 0);
  _dirty = false;
}

void UI::drawPopup() {
  _canvas.fillRect(6, 28, 228, 48, COL_ACCENT);
  _canvas.drawRect(6, 28, 228, 48, COL_FG);
  _canvas.setTextColor(COL_FG, COL_ACCENT);
  _canvas.setTextDatum(lgfx::top_left);
  _canvas.setTextSize(2);
  char title[20];
  snprintf(title, sizeof(title), "%.16s", _pop.title);
  _canvas.drawString(title, 14, 33);
  _canvas.setTextSize(1);
  char prev[40];
  snprintf(prev, sizeof(prev), "%.36s", _pop.preview);
  _canvas.drawString(prev, 14, 58);
}

void UI::onTap(int x, int y) {
  if (_popup) {
    bool inBanner = y >= 28 && y < 76;
    _popup = false;
    _dirty = true;
    if (inBanner) {
      if (_pop.kind == CONV_CHANNEL) {
        openConversation(CONV_CHANNEL, _pop.chan);
      } else {
        int idx = _mesh.contactIndexByPeer(_pop.peer);
        if (idx >= 0) openConversation(CONV_CONTACT, idx);
      }
    }
    return;
  }
  if (_screen != HOME && y < BACK_H) {
    go(backTarget());
    return;
  }
  switch (_screen) {
    case HOME: onHomeTap(x, y); break;
    case THREAD: onThreadTap(x, y); break;
    case COMPOSE: onComposeTap(x, y); break;
    case CANNED: onCannedTap(x, y); break;
    case SETTINGS: onSettingsTap(x, y); break;
    case CONTACTS: onContactsTap(x, y); break;
  }
}

void UI::onHomeTap(int x, int y) {
  if (y < LIST_TOP) return;
  uint8_t kinds[16];
  int idxs[16];
  int n = buildHome(kinds, idxs, 16);
  int row = (y - LIST_TOP) / LIST_ROW_H;
  if (row >= 0 && row < n) openConversation(kinds[row], idxs[row]);
}

void UI::onThreadTap(int x, int y) {
  if (y < 200) return;
  if (x < 118) startEntry(ENTRY_MESSAGE, _conv_name);
  else go(CANNED);
}

void UI::onComposeTap(int x, int y) {
  if (y < KB_TOP) return;
  int row = (y - KB_TOP) / KB_ROW_H;
  if (row > 3) row = 3;

  if (row < 3) {
    const char* const* layer = _sym ? LAYER_SY : LAYER_LO;
    const char* r = layer[row];
    int len = strlen(r);
    int startx = (240 - len * 24) / 2;
    int idx = (x - startx) / 24;
    if (idx < 0) idx = 0;
    if (idx >= len) idx = len - 1;
    char c = r[idx];
    if (!_sym && _shift && c >= 'a' && c <= 'z') c -= 32;
    if (_compose_len < sizeof(_compose) - 1) {
      _compose[_compose_len++] = c;
      _compose[_compose_len] = 0;
    }
  } else if (x < 44) {
    _sym = !_sym;
  } else if (x < 84) {
    _shift = !_shift;
  } else if (x < 156) {
    if (_compose_len < sizeof(_compose) - 1) {
      _compose[_compose_len++] = ' ';
      _compose[_compose_len] = 0;
    }
  } else if (x < 196) {
    if (_compose_len > 0) _compose[--_compose_len] = 0;
  } else {
    doneEntry();
    return;
  }
  _dirty = true;
}

void UI::onCannedTap(int x, int y) {
  if (y < 30) return;
  int rh = 210 / CANNED_COUNT;
  int idx = (y - 30) / rh;
  if (idx >= 0 && idx < CANNED_COUNT) {
    send(CANNED_MSGS[idx]);
    go(THREAD);
  }
}

void UI::onSettingsTap(int x, int y) {
  if (y >= 28 && y < 55) {
    startEntry(ENTRY_NAME, "Node name");
  } else if (y >= 57 && y < 84) {
    _mesh.advertSelf();
  } else if (y >= 86 && y < 113) {
    startEntry(ENTRY_CHAN_NAME, "Channel name");
  } else if (y >= 115 && y < 142) {
    go(CONTACTS);
  } else if (y >= 144 && y < 171) {
    _clock24 = !_clock24;
    savePrefs();
    _dirty = true;
  } else if (y >= 173 && y < 200) {
    if (x >= 150 && x < 174 && _tz > -12) {
      _tz--;
      savePrefs();
      _dirty = true;
    } else if (x >= 208 && x <= 232 && _tz < 14) {
      _tz++;
      savePrefs();
      _dirty = true;
    }
  } else if (y >= 202 && y < 229) {
    int cur = 0;
    for (int i = 0; i < SLEEP_OPT_COUNT; i++)
      if (SLEEP_OPTS[i] == _sleep_secs) cur = i;
    _sleep_secs = SLEEP_OPTS[(cur + 1) % SLEEP_OPT_COUNT];
    savePrefs();
    _dirty = true;
  }
}

void UI::onContactsTap(int x, int y) {
  if (y < 30) return;
  int idx = (y - 30) / 28;
  if (idx >= 0 && idx < _mesh.contactCount()) {
    uint8_t p[6];
    if (_mesh.contactPeer(idx, p)) {
      toggleFav(p);
      _dirty = true;
    }
  }
}

void UI::loop() {
  int32_t x, y;
  bool touched = _tft.getTouch(&x, &y);
  if (touched) {
    _last_activity = millis();
    if (!_touch_down) {
      _touch_down = true;
      _start_x = x;
      _start_y = y;
      _wake_consumed = _asleep;
      if (_asleep) wake();
    }
    _last_x = x;
    _last_y = y;
  } else if (_touch_down) {
    _touch_down = false;
    int dx = _last_x - _start_x;
    int dy = _last_y - _start_y;
    if (_wake_consumed) {
      // ignore the touch that woke the screen
    } else if (_screen == HOME && dy > 50 && abs(dy) > abs(dx)) {
      go(SETTINGS);
    } else if (_screen == THREAD && abs(dy) > 30 && abs(dy) > abs(dx)) {
      _thread_scroll += dy > 0 ? 3 : -3;
      if (_thread_scroll < 0) _thread_scroll = 0;
      _dirty = true;
    } else if (abs(dx) < 20 && abs(dy) < 20 && millis() - _last_tap > 120) {
      _last_tap = millis();
      onTap(_start_x, _start_y);
    }
  }

  if (!_asleep && _sleep_secs > 0 && millis() - _last_activity > (uint32_t)_sleep_secs * 1000) {
    _asleep = true;
    _tft.setBrightness(0);
  }

  if (_mesh.inboundRev() != _seen_inbound) {
    _seen_inbound = _mesh.inboundRev();
    _board.buzz();
    Notify note;
    if (_mesh.takeNotify(note)) {
      bool viewing = _screen == THREAD && note.kind == _conv_kind &&
                     (note.kind == CONV_CHANNEL ? note.chan == _conv_chan
                                                : memcmp(note.peer, _conv_peer, 6) == 0);
      if (!viewing) {
        _pop = note;
        _popup = true;
        _popup_since = millis();
      }
    }
    if (_asleep) wake();
    _last_activity = millis();
    _dirty = true;
  }

  if (_popup && millis() - _popup_since > 6000) {
    _popup = false;
    _dirty = true;
  }

  if (_mesh.rev() != _seen_rev) {
    _seen_rev = _mesh.rev();
    if (_screen == HOME || _screen == THREAD || _screen == CONTACTS) _dirty = true;
  }

  if (!_asleep && millis() - _last_status > 10000) {
    _last_status = millis();
    _dirty = true;
  }

  if (_dirty && !_asleep) draw();
}
