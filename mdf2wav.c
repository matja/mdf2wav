/*
Convert a RAW CDDA (Red Book / Mode 2) disk image with subchannel data to WAV
files for each track using the subcode 'P' channel to identify track positions.

Each output file will be named "track_XX.wav" and written in the current
directory, where XX is the track number starting from 01.

compile:
make

run:
./mdf2wav < disk.mdf

*/

#define _ISOC99_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* RIFF/WAVE format constants */
#define WAV_HEADER_SIZE 44
#define WAV_SUBCHUNK1_PCM 16
#define WAV_FORMAT_PCM 1

/* CDDA constants */
#define DATA_SIZE 2352
#define SUBCODE_SIZE 96
#define BLOCK_SIZE (DATA_SIZE+SUBCODE_SIZE)
#define SUBCODE_P (1 << 7)
static const uint32_t sample_rate = 44100;
static const uint16_t bits_per_sample = 16;
static const uint16_t num_channels = 2;

/* misc constants */
#define INVALID_FD (-1)
#define BITS_PER_BYTE 8

static void write_le16(char *p, uint16_t v) {
  p[0] = (v & 0xff);
  p[1] = ((v >> 8) & 0xff);
}

static void write_le32(char *p, uint32_t v) {
  p[0] = (v & 0xff);
  p[1] = ((v >> 8) & 0xff);
  p[2] = ((v >> 16) & 0xff);
  p[3] = ((v >> 24) & 0xff);
}

static void write_wav_header(int fd, uint32_t num_samples) {
  char wav_header[WAV_HEADER_SIZE];
  uint32_t subchunk1_size = WAV_SUBCHUNK1_PCM;
  uint16_t audio_format = WAV_FORMAT_PCM;
  uint16_t block_align = num_channels * (bits_per_sample / 8);
  uint32_t byte_rate = sample_rate * block_align;
  uint32_t subchunk2_size = num_samples * block_align;
  uint32_t chunk_size = 36 + subchunk2_size;

  memset(wav_header, 0, sizeof(wav_header));
  memcpy(wav_header, "RIFF", 4);
  write_le32(wav_header + 4, chunk_size);
  memcpy(wav_header + 8, "WAVE", 4);
  memcpy(wav_header + 12, "fmt ", 4);
  write_le32(wav_header + 16, subchunk1_size);
  write_le16(wav_header + 20, audio_format);
  write_le16(wav_header + 22, num_channels);
  write_le32(wav_header + 24, sample_rate);
  write_le32(wav_header + 28, byte_rate);
  write_le16(wav_header + 32, block_align);
  write_le16(wav_header + 34, bits_per_sample);
  memcpy(wav_header + 36, "data", 4);
  write_le32(wav_header + 40, subchunk2_size);

  lseek(fd, SEEK_SET, 0);
  write(fd, wav_header, WAV_HEADER_SIZE);
}

/*
check if this block is the start of a new track
(subcode channel P will be all 1's)
*/
static int is_track_start(const char *buf) {
  size_t i;
  for (i = DATA_SIZE; i < BLOCK_SIZE; i++) {
    if (!(buf[i] & SUBCODE_P)) return 0;
  }
  return 1;
}

struct State {
  char buf[BLOCK_SIZE];
  char output_file_name[256];
  unsigned long long start_offset, end_offset, offset;
  int track_fd;
  unsigned track_number;
  unsigned num_samples;
};

static int start_track(struct State *state) {
  state->num_samples = 0;
  state->start_offset = state->offset;
  snprintf(state->output_file_name, sizeof(state->output_file_name),
    "track_%02u.wav", state->track_number);
  state->track_fd = open(state->output_file_name,
    O_CREAT | O_EXCL |O_WRONLY, 0644);
  if (state->track_fd < 0) {
    if (errno == EEXIST) {
      fprintf(stderr, "%s: file \"%s\" already exists, won't overwrite\n",
        __func__, state->output_file_name);
    } else {
      fprintf(stderr, "%s: \"%s\": %s\n", __func__, state->output_file_name,
        strerror(errno)); 
    }
    return 0;
  }
  write_wav_header(state->track_fd, state->num_samples);
  return 1;
}

static void close_track(struct State *state) {
  unsigned long long duration_s = 0;
  /* if a track file is not already open, then exit */
  if (state->track_fd == INVALID_FD) {
    return;
  }

  /* output some diagnostic information about this track */
  state->end_offset = state->offset;
  duration_s = (
      ((state->end_offset - state->start_offset) * DATA_SIZE) / BLOCK_SIZE
    ) / (sample_rate * (bits_per_sample / BITS_PER_BYTE) * num_channels);

  fprintf(stderr, "%s: duration_s:%llu start_offset:%llu end_offset:%llu\n",
    state->output_file_name,
    (unsigned long long)duration_s,
    (unsigned long long)state->start_offset,
    (unsigned long long)state->end_offset
  );

  /* update the header with the size and close the file */
  write_wav_header(state->track_fd, state->num_samples);
  close(state->track_fd);
}

static void update_track(struct State *state) {
  /* if we have a file open, write the PCM data and update the sample count.
     the sample count is used later to update the .wav header */
  if (state->track_fd != INVALID_FD) {
    write(state->track_fd, state->buf, DATA_SIZE);
    state->num_samples += DATA_SIZE /
      ((bits_per_sample / BITS_PER_BYTE) * num_channels);
  }
}

int main(void) {
  struct State state;
  ssize_t count = 0;

  memset(&state, 0, sizeof(state));
  state.track_fd = INVALID_FD;

  while (1) {
    /* read a sector of raw CDDA + subcode channels */
    count = read(0, &state.buf, BLOCK_SIZE);
    if (count <= 0) {
      /* end of file, done */
      break;
    }

    if (is_track_start(state.buf)) {
      state.track_number++;
      close_track(&state);
      if (!start_track(&state)) {
        break;
      }
    }

    update_track(&state);
    state.offset += BLOCK_SIZE;
  }

  close_track(&state);
  return 0;
}
