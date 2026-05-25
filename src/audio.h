#ifndef MINI_DICT_AUDIO_H
#define MINI_DICT_AUDIO_H

#include <glib.h>

typedef struct _AudioPlayer AudioPlayer;

typedef void (*AudioErrorCallback)(const char *message, gpointer user_data);

AudioPlayer *audio_player_new(void);
void audio_player_free(AudioPlayer *player);
void audio_player_play(AudioPlayer *player,
                       const char *uri,
                       AudioErrorCallback callback,
                       gpointer user_data);

#endif
