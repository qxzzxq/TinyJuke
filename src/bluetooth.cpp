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
// Linear volume control replaces the lib's default exponential curve so
// BT loudness tracks the encoder linearly, matching the WAV playback path.
static A2DPLinearVolumeControl linearVol;
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
static uint32_t  s_lastPeerPollMs = 0;

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
    // First-connect race: the controller often hasn't completed the Remote
    // Name Request yet, so get_peer_name() returns empty. handleBluetoothLoop
    // re-polls until the name shows up.
    const char *peer = a2dp_sink.get_peer_name();
    copyIfAscii(peer, s_peerName, sizeof(s_peerName));
    s_lastPeerPollMs = 0;
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
//  Stream reader — heartbeat only. Volume scaling is delegated to the
//  library's A2DPLinearVolumeControl, installed in initBluetoothMode.
// ----------------------------------------------------------------

static void onStreamData(const uint8_t * /*data*/, uint32_t len) {
  if (len == 0) return;
  s_lastFrameMs = millis();
}

// AVRCP "absolute volume" change pushed by the peer (e.g. user moves the
// phone's BT slider). The lib reports the new value in the 0..127 range.
static void onAvrcVolumeChange(int volume0_127) {
  if (volume0_127 < 0) volume0_127 = 0;
  if (volume0_127 > 127) volume0_127 = 127;
  int newLocal = (volume0_127 * 100) / 127;
  if (newLocal != volumeLevel) {
    volumeLevel = newLocal;
    s_lastVolumeSent = newLocal;   // suppress echo back via set_volume()
  }
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
  s_lastPeerPollMs = 0;
  s_title[0]      = '\0';
  s_artist[0]     = '\0';
  s_peerName[0]   = '\0';

  // Hand the I2S peripheral over to the BT library; it installs and
  // manages I2S internally (config + start + sample-rate negotiation).
  i2sDeinit();
  // The lib's stream_reader (i2s_output=true) path will install I2S in
  // start(); we don't need to prime it ourselves.

  i2s_pin_config_t pins = {};
  pins.bck_io_num   = I2S_BCLK;
  pins.ws_io_num    = I2S_LRC;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  a2dp_sink.set_pin_config(pins);

  // Linear volume curve so loudness tracks the encoder 1:1 like WAV mode.
  a2dp_sink.set_volume_control(&linearVol);

  a2dp_sink.set_stream_reader(onStreamData, true);
  a2dp_sink.set_avrc_metadata_callback(onAvrcMetadata);
  a2dp_sink.set_on_connection_state_changed(onConnectionStateChanged);
  a2dp_sink.set_on_audio_state_changed(onAudioStateChanged);
  a2dp_sink.set_avrc_rn_volumechange(onAvrcVolumeChange);
  // Page the last-paired peer on start() so re-entering BT mode reconnects
  // the phone automatically instead of forcing the user to pick the device
  // from the phone's BT menu every time.
  a2dp_sink.set_auto_reconnect(true);

#ifdef DEV_MODE
  Serial.println("[BT] start() — auto_reconnect=true");
#endif
  a2dp_sink.start((char *)BT_DEVICE_NAME);
#ifdef DEV_MODE
  esp_bd_addr_t *last = a2dp_sink.get_last_peer_address();
  if (last) {
    Serial.printf("[BT] last peer in NVS: %02X:%02X:%02X:%02X:%02X:%02X\n",
      (*last)[0], (*last)[1], (*last)[2], (*last)[3], (*last)[4], (*last)[5]);
  }
#endif

  // Push our current local volume to the library + peer (AVRCP notify).
  uint8_t btVol = (uint8_t)((volumeLevel * 127) / 100);
  a2dp_sink.set_volume(btVol);
  s_lastVolumeSent = volumeLevel;

  s_running = true;
}

void stopBluetoothMode() {
  if (!s_running) return;

  // Drop the peer first so the A2DP/AVRCP deinit chain sees a clean state.
  // Without this, end() cascades "Failed to deinit avrc ct/tg/source" errors.
  if (s_connected) {
    a2dp_sink.disconnect();
    delay(300);  // let the disconnect propagate through the BT stack
  }

  // end(false) tears down the stack but keeps the controller memory mapped
  // so a later start() can succeed. end(true) releases controller memory
  // and the library explicitly refuses restart after that.
  //
  // Important: end() unconditionally calls clean_last_connection(), which
  // wipes the saved peer address from NVS *if* reconnect_status is still
  // AutoReconnect. That would defeat the auto-reconnect we want on the next
  // initBluetoothMode(). Flip to NoReconnect first so the NVS entry is
  // preserved; we re-enable AutoReconnect on the next start().
  a2dp_sink.set_auto_reconnect(false);
#ifdef DEV_MODE
  Serial.println("[BT] stopBluetoothMode: end(false), NVS preserved");
#endif
  a2dp_sink.end(false);
  delay(100);

  i2sPrime();            // re-drive amp pins to suppress touch noise
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

  // Encoder-driven volume change: push to library + AVRCP-notify the peer
  // so the phone's BT volume slider stays in sync.
  if (volumeLevel != s_lastVolumeSent) {
    uint8_t btVol = (uint8_t)((volumeLevel * 127) / 100);
    a2dp_sink.set_volume(btVol);
    s_lastVolumeSent = volumeLevel;
  }

  // Peer-name late arrival: poll get_peer_name() at ~500 ms cadence until
  // we have one. iOS/Android often deliver the name a beat after the A2DP
  // connection callback fires on first pairing.
  if (s_connected && s_peerName[0] == '\0' && (now - s_lastPeerPollMs) >= 500) {
    s_lastPeerPollMs = now;
    const char *peer = a2dp_sink.get_peer_name();
    if (peer && *peer) {
      copyIfAscii(peer, s_peerName, sizeof(s_peerName));
      if (s_peerName[0] != '\0') s_metadataDirty = true;
    }
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
