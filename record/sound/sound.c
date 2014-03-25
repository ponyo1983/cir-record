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

#include "../storage/record.h"

#include "../config/config.h"

#include "../serial/frame_manager.h"

#include "../global.h"

#include "g726.h"

#define DEFAULT_FORMAT		SND_PCM_FORMAT_A_LAW
#define DEFAULT_SPEED 		8000
#define SOUND_NAME	("default")

#ifdef __x86_64
#define INPUT_NAME	("/dev/input/event5")
#define KEY_CODE	KEY_ESC

const char* selem_name[2] = { "Master", "Capture" };
#else
#define INPUT_NAME	("/dev/input/event0")
#define KEY_CODE	(257)
const char* selem_name[2] = {"Digital", "Line Input"};
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

void start_play(int port) {
	if (pgsound != NULL) {
		pgsound->state = SOUND_PLAY;
		pgsound->request_port = port;
	}
}

void stop_play() {
	if (pgsound != NULL) {
		pgsound->state = SOUND_IDLE;
	}
}

/*
 * The parameter volume is to be given in the range [0, 100]
 */
void set_volume(long volume, int dir) {
	long min, max;
	snd_mixer_t *handle;
	snd_mixer_selem_id_t *sid;
	const char *card = "default";
	dir = dir > 0 ? 1 : 0;
	snd_mixer_open(&handle, 0);
	snd_mixer_attach(handle, card);
	snd_mixer_selem_register(handle, NULL, NULL);
	snd_mixer_load(handle);

	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, selem_name[dir]);
	snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);

	if (dir == 0) {
		snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
		snd_mixer_selem_set_playback_volume_all(elem, volume * max / 100);
	} else {
		snd_mixer_selem_get_capture_volume_range(elem, &min, &max);
		snd_mixer_selem_set_capture_volume_all(elem, volume * max / 100);
	}

	snd_mixer_close(handle);
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

static void set_params(struct sound *psound) {
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_uframes_t buffer_size;
	int err;

	unsigned int rate;
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
	//snd_pcm_hw_params_free(params);
	//snd_pcm_sw_params_free(swparams);
}

static void playback(struct sound* psound, u_char *wav_buffer, int length) {

	int written = 0;
	int chunk_size = psound->chunk_size;

	while ((written < length) && (psound->state == SOUND_PLAY)) {
		if (snd_pcm_writei(psound->handle, wav_buffer + written, chunk_size)
				<= 0)
			break;
		written += chunk_size;
	}
	//snd_pcm_nonblock(psound->handle, 0);
	//snd_pcm_drain(psound->handle);

}

/*
 * 按照G726 rate=2 G726_encode()函数的格式要求存储数据
 */
void capture_loop(struct sound*psound) {

	int chunk_size = psound->chunk_size;
	u_char *audiobuf = psound->audiobuf;
	struct block * pblock = NULL;
	int length = 0;
	int i;
	short *g762_in;
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
				//memcpy(pblock->data + length, audiobuf, chunk_size);
				g762_in = (short *) (pblock->data + length);
				for (i = 0; i < chunk_size; i++) {
					g762_in[i] = audiobuf[i];

				}
				length += chunk_size * 2;
				pblock->data_length = pblock->data_length + chunk_size * 2;
				if (psound->state != SOUND_CAPTURE) {
					printf("store:%d\n", length);
					set_wave_block_length(pblock, (length - 16) / 8); //g726压缩
					store_wave_data(record_manager, pblock);
					pblock = NULL;
					break;
				}
				if (length + chunk_size * 2 + 512 > pblock->block_size) { //为对齐需要，留有512空余
					set_wave_block_length(pblock, (length - 16) / 8);
					store_wave_data(record_manager, pblock); ////g726压缩
					pblock = NULL;
					printf("OK:%d\n", length);
				}

			}

		}

	}
}

static void play(struct sound * psound) {

	struct record_manager* record_manager = get_record_manager();
	int i, sound_num;
	struct block * pblock;
	struct block * bkblock;
	struct block *tmpblock = NULL;
	int err;
	struct record_header * header;
	int open_mode = 0;
	char buffer[3] = { 0, 0, 0 };
	short * g726_buffer;
	G726_state state;

	for (sound_num = 0; sound_num < 5; sound_num++) {
		printf("play num:%d\n", sound_num);
		if (psound->state != SOUND_PLAY)
			break;

		pblock = get_block(psound->manager.fliters, 0, BLOCK_EMPTY);
		if (pblock != NULL) {
			request_playback(record_manager, sound_num, pblock);

			bkblock = get_block(psound->manager.fliters, 5000, BLOCK_FULL);
			if (bkblock != NULL) {

				header = (struct record_header*) bkblock->data;
				printf("start play wave size:%d\n", header->wave_size);
				if (header->wave_size > 0) {
					tmpblock = get_block(psound->manager.fliters, 0,
							BLOCK_EMPTY);
					if (tmpblock != NULL) {

						g726_buffer = (short*) tmpblock->data;

						for (i = 0; i < header->wave_size; i++) {
							g726_buffer[i * 4] = (bkblock->data[16 + i] >> 0)
									& 0x03;
							g726_buffer[i * 4 + 1] =
									(bkblock->data[16 + i] >> 2) & 0x03;
							g726_buffer[i * 4 + 2] =
									(bkblock->data[16 + i] >> 4) & 0x03;
							g726_buffer[i * 4 + 3] =
									(bkblock->data[16 + i] >> 6) & 0x03;
						}
						G726_decode(g726_buffer, (short*) (bkblock->data + 16),
								header->wave_size * 4, "1", 2, 0, &state);
						g726_buffer = (short*) (bkblock->data + 16);
						for (i = 0; i < header->wave_size * 4; i++) {
							bkblock->data[16 + i] = (u_char) ((g726_buffer[i]
									& 0xff));
						}

						err = snd_pcm_open(&(psound->handle), SOUND_NAME,
								SND_PCM_STREAM_PLAYBACK, open_mode);
						if (err < 0)
							break;
						set_params(psound);
						err = snd_pcm_prepare(psound->handle);
						if(err<0) break;
						playback(psound, bkblock->data + 16,
								header->wave_size * 4);
						put_block(tmpblock, BLOCK_EMPTY);
						snd_pcm_close(psound->handle);
					}
				}

				put_block(bkblock, BLOCK_EMPTY);
			} else {
				put_block(pblock, BLOCK_EMPTY);

			}

		}

	}

	struct frame_manager *manager_control = get_frame_manager(CONTROL_MANAGER);
	send_frame(manager_control, RECORD_ADDRESS, psound->request_port, 1,
			(char) 0x92, buffer, 3);

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
	int vol = get_playback_volume();
	set_volume(vol, 0);
	vol = get_capture_volume();
	set_volume(vol, 1);
	while (1) {
		usleep(100000);
		update_state(psound);
		if (psound->state == SOUND_CAPTURE) {
			captue(psound);
			psound->state = SOUND_IDLE;

		} else if (psound->state == SOUND_PLAY) {
			play(psound);
			psound->state = SOUND_IDLE;
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
			add_block_filter(&(pgsound->manager), NULL, CAPTURE_BUFFER_SIZE, 5);
			pthread_create(&(pgsound->thread), NULL, sound_proc, pgsound);
			pthread_create(&(pgsound->thread_key), NULL, key_check, pgsound);
		}
	}
}
