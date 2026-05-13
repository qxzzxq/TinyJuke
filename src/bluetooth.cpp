#include "bluetooth.h"
#include "config.h"
#include "audio.h"
#include "encoder.h"
#include "timer_logic.h"
#include "tags.h"
#include <BluetoothA2DPSink.h>
#include <driver/i2s.h>

// ----------------------------------------------------------------
//  State
// ----------------------------------------------------------------

static BluetoothA2DPSink a2dp_sink;
static PN532 *s_nfc = nullptr;

static bool      s_running        = false;
static bool      s_connected      = false;
static char      s_peerName[64]   = "";

static char      s_title[64]      = "";
static char      s_artist[64]     = "";
static bool      s_metadataDirty  = false;

static uint8_t   s_lastUid[10]    = {0};
static uint8_t   s_lastUidLen     = 0;
static bool      s_tagPresent     = false;
static uint32_t  s_lastNfcPollMs  = 0;

static uint32_t  s_lastFrameMs    = 0;
static uint32_t  s_streamStartMs  = 0;
static bool      s_streamingNow   = false;
static int       s_lastVolumeSent = -1;

static bool      s_sleepFired     = false;

// ----------------------------------------------------------------
//  ASCII validation — used to filter AVRCP metadata. Latin-1 / UTF-8
//  text shows up as garbage in our 7-bit font, so anything outside
//  the printable ASCII range collapses to "" and the screen falls
//  back to the generic "Bluetooth" label.
// ----------------------------------------------------------------

static bool isPrintableAscii(const char *s) {
  if (!s || !*s) return false;
  for (const char *p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

static void copyIfAscii(const char *src, char *dst, size_t dstSize) {
  if (isPrintableAscii(src)) {
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
  } else {
    dst[0] = '\0';
  }
}

// ----------------------------------------------------------------
//  AVRCP / connection callbacks
// ----------------------------------------------------------------

static void onAvrcMetadata(uint8_t id, const uint8_t *text) {
  const char *str = (const char *)text;
  char before_t[64]; strncpy(before_t, s_title, sizeof(before_t));
  char before_a[64]; strncpy(before_a, s_artist, sizeof(before_a));

  if (id == ESP_AVRC_MD_ATTR_TITLE) {
    copyIfAscii(str, s_title, sizeof(s_title));
  } else if (id == ESP_AVRC_MD_ATTR_ARTIST) {
    copyIfAscii(str, s_artist, sizeof(s_artist));
  } else {
    return;
  }

  if (strcmp(before_t, s_title) != 0 || strcmp(before_a, s_artist) != 0)
    s_metadataDirty = true;
}

static void onConnectionStateChanged(esp_a2d_connection_state_t state, void *) {
  bool wasConnected = s_connected;
  s_connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
  if (!s_connected) {
    s_peerName[0] = '\0';
    s_title[0]    = '\0';
    s_artist[0]   = '\0';
    s_streamingNow = false;
  } else {
    const char *peer = a2dp_sink.get_peer_name();
    copyIfAscii(peer, s_peerName, sizeof(s_peerName));
  }
  if (wasConnected != s_connected) s_metadataDirty = true;
}

static void onAudioStateChanged(esp_a2d_audio_state_t state, void *) {
  bool wasStreaming = s_streamingNow;
  s_streamingNow = (state == ESP_A2D_AUDIO_STATE_STARTED);
  if (s_streamingNow && !wasStreaming) {
    s_streamStartMs = millis();
    s_lastFrameMs   = millis();
  }
}

// ----------------------------------------------------------------
//  Stream reader — runs on the BT data path. We don't mutate the
//  buffer (the library handles I2S output itself), but use it as a
//  heartbeat for power-save and the sleep timer.
// ----------------------------------------------------------------

static void onStreamData(const uint8_t * /*data*/, uint32_t len) {
  if (len == 0) return;
  s_lastFrameMs = millis();
}

// ----------------------------------------------------------------
//  Public API
// ----------------------------------------------------------------

void initBluetoothMode(PN532 &nfc) {
  if (s_running) return;
  s_nfc = &nfc;

  // Reset visible state — fresh session every time the user enters BT mode.
  s_connected     = false;
  s_streamingNow  = false;
  s_metadataDirty = true;
  s_sleepFired    = false;
  s_tagPresent    = false;
  s_lastUidLen    = 0;
  s_lastNfcPollMs = 0;
  s_streamStartMs = 0;
  s_lastFrameMs   = 0;
  s_lastVolumeSent = -1;
  s_title[0]      = '\0';
  s_artist[0]     = '\0';
  s_peerName[0]   = '\0';

  // Hand the I2S peripheral over to the BT stack.
  i2sDeinit();

  // Pin config (legacy i2s_pin_config_t) — matches our MAX98357A wiring.
  i2s_pin_config_t pins = {};
  pins.bck_io_num   = I2S_BCLK;
  pins.ws_io_num    = I2S_LRC;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  a2dp_sink.set_pin_config(pins);

  a2dp_sink.set_stream_reader(onStreamData, false);
  a2dp_sink.set_avrc_metadata_callback(onAvrcMetadata);
  a2dp_sink.set_on_connection_state_changed(onConnectionStateChanged);
  a2dp_sink.set_on_audio_state_changed(onAudioStateChanged);
  a2dp_sink.set_auto_reconnect(false);

  a2dp_sink.start((char *)BT_DEVICE_NAME);

  // Sync the initial software volume into the BT stack.
  a2dp_sink.set_volume((uint8_t)volumeLevel);
  s_lastVolumeSent = volumeLevel;

  s_running = true;
}

void stopBluetoothMode() {
  if (!s_running) return;
  a2dp_sink.end(true);   // releases I2S internally
  i2sPrime();            // re-drive the amp pins to suppress touch noise
  s_running       = false;
  s_connected     = false;
  s_streamingNow  = false;
  s_tagPresent    = false;
}

bool btIsConnected() { return s_connected; }

bool btIsStreaming() {
  if (!s_streamingNow) return false;
  // Treat ~500 ms of silence as "not streaming" so the power-save heartbeat
  // and sleep timer correctly observe a paused phone.
  return (millis() - s_lastFrameMs) < 500;
}

bool btTagDetected()     { return s_tagPresent; }
bool btSleepFired()      { return s_sleepFired; }

bool btMetadataChanged() {
  bool d = s_metadataDirty;
  s_metadataDirty = false;
  return d;
}

const char *btTrackTitle()  { return s_title; }
const char *btTrackArtist() { return s_artist; }
const char *btPeerName()    { return s_peerName; }

// ----------------------------------------------------------------
//  Per-iteration servicing — called from guiLoop() on the BT screen
// ----------------------------------------------------------------

void handleBluetoothLoop() {
  if (!s_running) return;

  uint32_t now = millis();

  // 1) Volume sync — encoder may have changed `volumeLevel` from any screen.
  if (volumeLevel != s_lastVolumeSent) {
    a2dp_sink.set_volume((uint8_t)volumeLevel);
    s_lastVolumeSent = volumeLevel;
  }

  // 2) Power-save heartbeat — while audio is actively streaming, keep the
  //    activity timer fresh so the screen stays on. When paused, the timer
  //    drains and the FSM (or backlight logic) will eventually dim the
  //    display. Encoder activity also resets the timer elsewhere.
  if (btIsStreaming()) resetActivityTimer();

  // 3) Sleep timer — fires only while audio is actually playing.
  if (sleepTimerMinutes > 0 && s_streamStartMs > 0 && btIsStreaming()) {
    uint32_t elapsed = now - s_streamStartMs;
    if (sleepTimerShouldFire(sleepTimerMinutes, elapsed)) {
      // Match audio.cpp behavior: clear the one-shot timer when it fires.
      sleepTimerMinutes = 0;
      saveSleepTimer();
      s_sleepFired = true;
      // Stop the audio output. The GUI will tear down BT entirely on the
      // next tick.
      s_streamingNow = false;
    }
  }

  // 4) NFC poll at ~300 ms cadence — sets s_tagPresent so the GUI can show
  //    the "switch to jukebox?" prompt.
  if (s_nfc && (now - s_lastNfcPollMs) >= 300) {
    s_lastNfcPollMs = now;
    uint8_t uid[10] = {0};
    uint8_t uidLen  = 0;
    bool found = s_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 30);
    if (found && uidLen > 0 && uidLen <= 10) {
      s_tagPresent = true;
      memcpy(s_lastUid, uid, uidLen);
      s_lastUidLen = uidLen;
    } else {
      s_tagPresent = false;
      s_lastUidLen = 0;
    }
  }
}
