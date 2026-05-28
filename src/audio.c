#include "audio.h"

#include <gst/gst.h>

struct _AudioPlayer {
  GstElement *playbin;
  GstElement *warmup_pipeline;
  guint bus_watch_id;
  AudioErrorCallback error_callback;
  gpointer error_user_data;
};

static char *
friendly_audio_error(const GError *error, const char *debug)
{
  const char *message = error ? error->message : "Audio playback failed";
  if (g_strrstr(message, "missing a plug-in") ||
      (debug && g_strrstr(debug, "Missing decoder"))) {
    return g_strdup("Audio playback needs a GStreamer runtime plugin. Install gst-plugins-good; if MP3 still fails, also install gst-plugins-ugly and gst-libav.");
  }
  return g_strdup(message);
}

static gboolean
audio_bus_callback(GstBus *bus, GstMessage *message, gpointer user_data)
{
  (void)bus;
  AudioPlayer *player = user_data;

  switch (GST_MESSAGE_TYPE(message)) {
  case GST_MESSAGE_ERROR: {
    GError *error = NULL;
    gchar *debug = NULL;
    gst_message_parse_error(message, &error, &debug);
    if (player->error_callback) {
      g_autofree char *friendly_message = friendly_audio_error(error, debug);
      player->error_callback(friendly_message, player->error_user_data);
    }
    g_clear_error(&error);
    g_free(debug);
    gst_element_set_state(player->playbin, GST_STATE_READY);
    break;
  }
  case GST_MESSAGE_EOS:
    gst_element_set_state(player->playbin, GST_STATE_READY);
    break;
  default:
    break;
  }

  return G_SOURCE_CONTINUE;
}

AudioPlayer *
audio_player_new(void)
{
  AudioPlayer *player = g_new0(AudioPlayer, 1);
  player->playbin = gst_element_factory_make("playbin", "pronunciation-player");
  GError *warmup_error = NULL;
  player->warmup_pipeline =
      gst_parse_launch("audiotestsrc is-live=true wave=silence volume=0 ! "
                       "audioconvert ! audioresample ! autoaudiosink sync=false",
                       &warmup_error);
  if (player->warmup_pipeline) {
    gst_element_set_state(player->warmup_pipeline, GST_STATE_PLAYING);
  } else {
    g_clear_error(&warmup_error);
  }
  if (player->playbin) {
    GstBus *bus = gst_element_get_bus(player->playbin);
    player->bus_watch_id = gst_bus_add_watch(bus, audio_bus_callback, player);
    gst_object_unref(bus);
  }
  return player;
}

void
audio_player_free(AudioPlayer *player)
{
  if (!player) {
    return;
  }
  if (player->bus_watch_id != 0) {
    g_source_remove(player->bus_watch_id);
  }
  if (player->playbin) {
    gst_element_set_state(player->playbin, GST_STATE_NULL);
    gst_object_unref(player->playbin);
  }
  if (player->warmup_pipeline) {
    gst_element_set_state(player->warmup_pipeline, GST_STATE_NULL);
    gst_object_unref(player->warmup_pipeline);
  }
  g_free(player);
}

void
audio_player_play(AudioPlayer *player,
                  const char *uri,
                  AudioErrorCallback callback,
                  gpointer user_data)
{
  if (!player || !player->playbin || !uri || uri[0] == '\0') {
    if (callback) {
      callback("Audio playback is unavailable", user_data);
    }
    return;
  }

  player->error_callback = callback;
  player->error_user_data = user_data;

  gst_element_set_state(player->playbin, GST_STATE_READY);
  g_object_set(player->playbin, "uri", uri, NULL);

  GstStateChangeReturn state_result =
      gst_element_set_state(player->playbin, GST_STATE_PAUSED);
  if (state_result == GST_STATE_CHANGE_ASYNC) {
    state_result = gst_element_get_state(player->playbin,
                                         NULL,
                                         NULL,
                                         250 * GST_MSECOND);
  }
  if (state_result == GST_STATE_CHANGE_FAILURE) {
    gst_element_set_state(player->playbin, GST_STATE_READY);
    if (callback) {
      callback("Pronunciation audio could not be prepared for playback", user_data);
    }
    return;
  }

  gst_element_set_state(player->playbin, GST_STATE_PLAYING);
}
