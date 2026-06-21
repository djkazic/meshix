#pragma once

#include "TWatchLGFX.h"
#include "../MeshixNode.h"
#include "../board/TWatchS3Board.h"

class UI {
  enum Screen { HOME, THREAD, COMPOSE, CANNED, SETTINGS, CONTACTS, CHANNELS };
  enum Entry { ENTRY_MESSAGE, ENTRY_CHAN_NAME, ENTRY_CHAN_PSK, ENTRY_NAME };
  static const int MAX_FAVS = 8;

  TWatchLGFX& _tft;
  lgfx::LGFX_Sprite _canvas;
  MeshixNode& _mesh;
  TWatchS3Board& _board;

  Screen _screen;
  bool _dirty;

  uint8_t _conv_kind;
  int _conv_chan;
  uint8_t _conv_peer[6];
  char _conv_name[24];

  uint8_t _entry;
  char _entry_title[24];
  char _new_chan[24];
  char _compose[80];
  uint8_t _compose_len;
  bool _shift;
  bool _sym;

  bool _clock24;
  int _tz;
  uint8_t _sleep_secs;
  uint8_t _favs[MAX_FAVS][6];
  int _favN;

  uint32_t _last_activity;
  bool _asleep;
  bool _wake_consumed;

  bool _popup;
  uint32_t _popup_since;
  Notify _pop;

  uint32_t _seen_rev;
  uint32_t _seen_inbound;
  int _thread_scroll;
  uint32_t _last_status;
  bool _touch_down;
  uint32_t _last_tap;
  int16_t _start_x, _start_y, _last_x, _last_y;

  void draw();
  void drawStatusBar();
  void drawHome();
  void drawThread();
  void drawCompose();
  void drawCanned();
  void drawSettings();
  void drawContacts();
  void drawChannels();
  void drawPopup();

  void onTap(int x, int y);
  void onHomeTap(int x, int y);
  void onThreadTap(int x, int y);
  void onComposeTap(int x, int y);
  void onCannedTap(int x, int y);
  void onSettingsTap(int x, int y);
  void onContactsTap(int x, int y);
  void onChannelsTap(int x, int y);

  int composeLines(const Msg* m, char lines[][46], int maxlines);
  Screen backTarget();
  void go(Screen s);
  void wake();
  int buildHome(uint8_t kinds[], int idxs[], int max);
  void openConversation(uint8_t kind, int idx);
  void startEntry(uint8_t purpose, const char* title);
  void doneEntry();
  void send(const char* text);
  void fmtTime(uint32_t ts, char* out);
  bool isFav(const uint8_t peer[6]);
  void toggleFav(const uint8_t peer[6]);
  void loadPrefs();
  void savePrefs();

public:
  UI(TWatchLGFX& tft, MeshixNode& mesh, TWatchS3Board& board)
    : _tft(tft), _canvas(&tft), _mesh(mesh), _board(board), _screen(HOME), _dirty(true),
      _conv_kind(CONV_CHANNEL), _conv_chan(0),
      _entry(ENTRY_MESSAGE), _compose_len(0), _shift(false), _sym(false),
      _clock24(true), _tz(0), _sleep_secs(30), _favN(0),
      _last_activity(0), _asleep(false), _wake_consumed(false),
      _popup(false), _popup_since(0),
      _seen_rev(0), _seen_inbound(0), _thread_scroll(0), _last_status(0),
      _touch_down(false), _last_tap(0), _start_x(0), _start_y(0), _last_x(0), _last_y(0) {
    _conv_peer[0] = 0;
    _conv_name[0] = 0;
    _entry_title[0] = 0;
    _new_chan[0] = 0;
    _compose[0] = 0;
  }

  void begin();
  void loop();
  bool isAsleep() const { return _asleep; }
};
