/*
 * sound.c
 *
 *  Created on: Mar 20, 2014
 *      Author: lifeng
 */

#include <stdlib.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <linux/input.h>
#include <fcntl.h>

#include "sound.h"

#include "../lib/block_manager.h"
#include "../lib/block_filter.h"
#include "../lib/block.h"

#include "../storage/record_manager.h"

#define DEFAULT_FORMAT		SND_PCM_FORMAT_A_LAW
#define DEFAULT_SPEED 		8000
#define SOUND_NAME	("default")

#ifdef __x86_64
#define INPUT_NAME	("/dev/input/event7")
#define KEY_CODE	KEY_ESC
#else
#define INPUT_NAME	("/dev/input/event0")
#define KEY_CODE	(257)
#endif

static struct sound* pgsound = NULL;

void start_capture() {
	if (pgsound != NULL) {
		pgsound->state = SOUND_CAPTURE;
	}
}

void stop_capture() {
	if (pgsound != NULL) {
		pgsound->state = SOUND_IDLE;
	}
}

void start_play() {
	if (pgsound != NULL) {
		pgsound->state = SOUND_PLAY;
	}
}

void stop_play() {
	if (pgsound != NULL) {
		pgsound->state = SOUND_IDLE;
	}
}

static ssize_t pcm_read(struct sound* psound, u_char *data, size_t rcount) {
	ssize_t r;
	size_t result = 0;
	size_t count = rcount;
	snd_pcm_t * handle = psound->handle;
	if (count != psound->chunk_size) {
		count = psound->chunk_size;
	}

	while (count > 0) {

		r = snd_pcm_readi(handle, data, count);

		if (r == -EAGAIN || (r >= 0 && (size_t) r < count)) {

			snd_pcm_wait(handle, 100);
		} else if (r == -EPIPE) {
			//xrun();
		} else if (r == -ESTRPIPE) {
			//suspend();
		} else if (r < 0) {
			//error(_("read error: %s"), snd_strerror(r));
			//prg_exit(EXIT_FAILURE);
		}
		if (r > 0) {

			result += r;
			count -= r;
			data += r * (psound->bits_per_frame) / 8;
		}
	}
	return rcount;
}

void capture_loop(struct sound*psound) {

	int num = 0;
	int chunk_size = psound->chunk_size;
	u_char *audiobuf = psound->audiobuf;
	struct block * pblock = NULL;
	int length = 0;
	struct record_manager * record_manager = get_record_manager();
	while (1) {

		if (pblock == NULL) {
			pblock = get_block(psound->manager.fliters, 0, BLOCK_EMPTY);
			if (pblock != NULL) {
				pblock->data_length = 0;
				init_wave_block(pblock); //???
				length = 16; //record_header
			}
			if (psound->state != SOUND_CAPTURE) {
				break;
			}
		}
		if (pcm_read(psound, audiobuf, chunk_size) != chunk_size) {
			printf("capture err\n");
			break;
		} else {
			if (pblock != NULL) {
				memcpy(pblock->data + length, audiobuf, chunk_size);
				length += chunk_size;
				pblock->data_length = pblock->data_length + chunk_size;
				if (psound->state != SOUND_CAPTURE) {
					printf("store:%d\n", length);
					set_wave_block_length(pblock, length - 16);
					store_wave_data(record_manager, pblock);
					pblock = NULL;
					break;
				}
				if (length + chunk_size + 512 > pblock->block_size) { //为对齐需要，留有512空余
					set_wave_block_length(pblock, length - 16);
					store_wave_data(record_manager, pblock);
					pblock = NULL;
					printf("OK:%d\n", length);
				}

			}

		}

	}
}

static void set_params(struct sound *psound) {
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_uframes_t buffer_size;
	int err;
	size_t n;
	unsigned int rate;
	snd_pcm_uframes_t start_threshold, stop_threshold;
	unsigned buffer_time = 0;

	unsigned period_time = 0;
	snd_pcm_uframes_t period_frames = 0;
	snd_pcm_uframes_t buffer_frames = 0;
	int bits_per_sample, bits_per_frame;
	snd_pcm_uframes_t chunk_size = 0;
	snd_pcm_t *handle = psound->handle;

	snd_pcm_hw_params_alloca(&params);
	snd_pcm_sw_params_alloca(&swparams);
	err = snd_pcm_hw_params_any(handle, params);
	if (err < 0)
		return;

	err = snd_pcm_hw_params_set_access(handle, params,
			SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0)
		return;
	err = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_A_LAW);
	if (err < 0)
		return;
	err = snd_pcm_hw_params_set_channels(handle, params, 1);
	if (err < 0)
		return;

	rate = DEFAULT_SPEED;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0);
	assert(err >= 0);

	if (buffer_time == 0 && buffer_frames == 0) {
		err = snd_pcm_hw_params_get_buffer_time_max(params, &buffer_time, NULL);
		assert(err >= 0);
		if (buffer_time > 500000)
			buffer_time = 500000;
	}
	if (period_time == 0 && period_frames == 0) {
		if (buffer_time > 0)
			period_time = buffer_time / 4;
		else
			period_frames = buffer_frames / 4;
	}
	if (period_time > 0)
		err = snd_pcm_hw_params_set_period_time_near(handle, params,
				&period_time, NULL);
	else
		err = snd_pcm_hw_params_set_period_size_near(handle, params,
				&period_frames, 0);
	assert(err >= 0);
	if (buffer_time > 0) {
		err = snd_pcm_hw_params_set_buffer_time_near(handle, params,
				&buffer_time, 0);
	} else {
		err = snd_pcm_hw_params_set_buffer_size_near(handle, params,
				&buffer_frames);
	}
	assert(err >= 0);

	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		return;
	}
	snd_pcm_hw_params_get_period_size(params, &chunk_size, NULL);
	snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	psound->chunk_size = chunk_size;
	if (chunk_size == buffer_size) {
		return;
	}
	snd_pcm_sw_params_current(handle, swparams);

	if (snd_pcm_sw_params(handle, swparams) < 0) {
		return;
	}

	bits_per_sample = snd_pcm_format_physical_width(SND_PCM_FORMAT_A_LAW);
	bits_per_frame = bits_per_sample * 1;
	psound->bits_per_frame = bits_per_frame;
	int chunk_bytes = chunk_size * bits_per_frame / 8;
	if (psound->audiobuf == NULL) {
		psound->audiobuf = malloc(chunk_bytes);
	}

}

static void play(struct sound * psound) {

}
static void captue(struct sound * psound) {
	int err;
	int open_mode = 0;
	err = snd_pcm_open(&(psound->handle), SOUND_NAME, SND_PCM_STREAM_CAPTURE,
			open_mode);
	if (err < 0)
		return;

	set_params(psound);
	capture_loop(psound);
	snd_pcm_close(psound->handle);
}

static void update_state(struct sound * psound) {
	if (psound->ppt == 1) {
		if (psound->state != SOUND_PLAY) {
			psound->state = SOUND_CAPTURE;
		}
	} else if (psound->ppt == 0) {
		if (psound->state != SOUND_PLAY) {
			psound->state = SOUND_IDLE;
		}
	}
}

void * sound_proc(void * args) {
	struct sound * psound = (struct sound*) args;
	while (1) {
		usleep(100000);
		update_state(psound);
		if (psound->state == SOUND_CAPTURE) {
			captue(psound);

		} else if (psound->state == SOUND_PLAY) {
			play(psound);
		}

	}
}

void * key_check(void *args) {
	struct sound * psound = (struct sound*) args;
	struct input_event key_event;
	int fd = open(INPUT_NAME, O_RDWR);
	if (fd > 0) {
		while (1) {
			if (read(fd, &key_event, sizeof(key_event)) == sizeof(key_event)) {
				if (key_event.type == EV_KEY) {

					if (key_event.code == KEY_CODE) {
						psound->ppt = key_event.value > 0 ? 1 : 0;
						update_state(psound);
					}

				}

			}

		}
		close(fd);
	}

	return NULL;
}

void start_sound() {
	if (pgsound == NULL) {
		pgsound = malloc(sizeof(struct sound));
		if (pgsound != NULL) {
			memset(pgsound, 0, sizeof(struct sound));
			pgsound->state = SOUND_IDLE;
			add_block_filter(&(pgsound->manager), NULL, CAPTURE_BUFFER_SIZE, 2);
			pthread_create(&(pgsound->thread), NULL, sound_proc, pgsound);
			pthread_create(&(pgsound->thread_key), NULL, key_check, pgsound);
		}
	}
}
