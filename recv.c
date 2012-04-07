#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <time.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sys/mman.h>
#include <unistd.h>

#include <pthread.h>

#include "celt/celt.h"
#include "celt/celt_types.h"

#include "types.h"
#include "ao.h"


#define PORT		9930

#define BUFSIZE		18UL	// 2 ^ N

#define SYSFREQ		1E6	// 1000000 per sec

#define report_exceptional_condition() abort ()

pthread_mutex_t mdata = PTHREAD_MUTEX_INITIALIZER;



struct ring_buffer {
	void *		address;

	unsigned long	count_bytes;
	unsigned long	write_offset_bytes;
	unsigned long	read_offset_bytes;
	unsigned long	elem_count;
	unsigned long	max_size;
};


struct thread_data {
	struct ring_buffer *	rbuf;
	struct AOutput *	device;
	int			sample_buffer_size;
};

//Warning order should be at least 12 for Linux
void ring_buffer_create(struct ring_buffer * buffer, unsigned long order) {
	char path[] = "/dev/shm/ring-buffer-XXXXXX";
	int file_descriptor;
	void * address;
	int status;

	if ((file_descriptor = mkstemp(path)) < 0)
		report_exceptional_condition ();

	if (status = unlink(path))
		report_exceptional_condition ();

	buffer->count_bytes = 1UL << order;
	buffer->write_offset_bytes = 0;
	buffer->read_offset_bytes = 0;

	if (status = ftruncate(file_descriptor, buffer->count_bytes))
		report_exceptional_condition();

	buffer->address = mmap(NULL, buffer->count_bytes << 1, PROT_NONE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (buffer->address == MAP_FAILED)
		report_exceptional_condition();

	address =
		mmap(buffer->address, buffer->count_bytes, PROT_READ | PROT_WRITE,
			MAP_FIXED | MAP_SHARED, file_descriptor, 0);

	if (address != buffer->address)
		report_exceptional_condition();

	address = mmap(buffer->address + buffer->count_bytes,
			buffer->count_bytes, PROT_READ | PROT_WRITE,
			MAP_FIXED | MAP_SHARED, file_descriptor, 0);

	if (address != buffer->address + buffer->count_bytes)
		report_exceptional_condition();

	if (status = close(file_descriptor))
		report_exceptional_condition();

	buffer->elem_count = 0;
	buffer->max_size = buffer->count_bytes >> 12UL;
}

void ring_buffer_free(struct ring_buffer * buffer) {
	int status;

	status = munmap(buffer->address, buffer->count_bytes << 1);
	if (status)
		report_exceptional_condition();
}

void * ring_buffer_write_address (struct ring_buffer * buffer) {
  /*** void pointer arithmetic is a constraint violation. ***/
	return buffer->address + buffer->write_offset_bytes;
}

void ring_buffer_write_advance(struct ring_buffer * buffer,
				unsigned long count_bytes) {
	buffer->write_offset_bytes += count_bytes;
	buffer->elem_count++;
}

void * ring_buffer_read_address(struct ring_buffer * buffer) {
	if (buffer->elem_count > 0)
		return buffer->address + buffer->read_offset_bytes;
	else
		return NULL;
}

void ring_buffer_read_advance(struct ring_buffer * buffer,
				unsigned long count_bytes) {
	buffer->read_offset_bytes += count_bytes;
	buffer->elem_count--;

	if (buffer->read_offset_bytes >= buffer->count_bytes) {
		buffer->read_offset_bytes -= buffer->count_bytes;
		buffer->write_offset_bytes -= buffer->count_bytes;
	}
}

unsigned long ring_buffer_count_bytes(struct ring_buffer * buffer) {
	return buffer->write_offset_bytes - buffer->read_offset_bytes;
}

unsigned long ring_buffer_count_free_bytes(struct ring_buffer * buffer) {
	return buffer->count_bytes - ring_buffer_count_bytes(buffer);
}

unsigned long ring_buffer_count_length(struct ring_buffer * buffer) {
	return buffer->elem_count;
}

void ring_buffer_clear(struct ring_buffer * buffer) {
	buffer->write_offset_bytes = 0;
	buffer->read_offset_bytes = 0;
}

void diep(char * s) {
	perror(s);
	exit(1);
}

uint64_t getcount(void) {
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec * SYSFREQ + t.tv_usec;
}

static void * thread_func(void * vptr_args) {
	struct thread_data * tdata = vptr_args;
	int * tmpbuf = (int *) malloc(tdata->sample_buffer_size * sizeof(char));
	int has_data;
	
	printf("Playing thread has started\n");
	
	while (1) {
		has_data = 0;

		pthread_mutex_lock(&mdata);
		if (ring_buffer_read_address(tdata->rbuf) != NULL) {
			memcpy(tmpbuf, ring_buffer_read_address(tdata->rbuf),
				tdata->sample_buffer_size);
			ring_buffer_read_advance(tdata->rbuf,
				tdata->sample_buffer_size);
			has_data++;
		}
		pthread_mutex_unlock(&mdata);
		if (has_data) {

			aout_play(tdata->device, tmpbuf, tdata->sample_buffer_size);
		} else
			usleep(25);
	}
	return NULL;
}

int main(int argc, char ** argv) {

	int is_debug = 0;
	celt_int16 * sample_buffer;
	unsigned char * encoded_buffer;
	int sample_rate;
	int sample_bits;
	int sample_channels;
	int sample_buffer_size;
	int sample_size;
	int k, s, slen = sizeof(struct sockaddr_in);

	uint64_t count = 0, sum = 0, start = 0, min = 1E9, max = 0, curr;

	CELTMode * cm;
	CELTDecoder * cd;
	int compressed;
	
	struct sockaddr_in si_me, si_other;
	struct ring_buffer rbuf;
	struct thread_data tdata;
	
	pthread_t thread;

	if (argc > 1) {
		if (!strcmp(argv[1], "-v")) {
			++is_debug;
			printf("Running in debug mode\n");
		}
	}
   
	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		diep("Unable to open listening UDP socket\n");

	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(PORT);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(s, (struct sockaddr *)(&si_me), sizeof(si_me)) == -1)
		diep("Unable to bind to listening UDP socket\n");

	// Hardcoded parameters for DSP

	sample_rate = 44100; // Desc. frequency, Hz
	sample_bits = 16; // Bits per sample
	sample_channels = 2; // Stereo

	sample_size = 1024;
	sample_buffer_size =  sample_size * sample_bits/8 * sample_channels;
	sample_buffer = (celt_int16 *) calloc(sample_buffer_size, sizeof(char));
	encoded_buffer = (unsigned char *) calloc(sample_buffer_size, sizeof(char));

	// Initializing CELT decoder
	cm = celt_mode_create(sample_rate, sample_size, NULL);
	cd = celt_decoder_create(cm, sample_channels, NULL);

	// Creating ring buffer of 256 kbytes (64 samples)
	ring_buffer_create(&rbuf, BUFSIZE);

	// Filling thread structure
	tdata.device = aout_init();
	tdata.rbuf = &rbuf;
	tdata.sample_buffer_size = sample_buffer_size;

	if (pthread_create(&thread, NULL, thread_func, &tdata) != 0)
		diep("Unable to start playing thread\n");

   
	for (;;) {
		start = getcount();
		++count;
		if ((k = recvfrom(s, encoded_buffer, sample_buffer_size, 0,
			(struct sockaddr *)(&si_other), &slen)) == -1)
			diep("Error during call recvfrom()");
		if (is_debug)
			printf("Received %llu packet from %s:%d, data size: %d\n",
				(long long unsigned int)count,
				inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), k);

		compressed = *((uint16_t *)encoded_buffer);
		celt_decode(cd, (char *)encoded_buffer + 2, compressed, sample_buffer);

		pthread_mutex_lock(&mdata);
		if (ring_buffer_count_length(&rbuf) < ((1UL << BUFSIZE) >> 12UL) - 1) {
			memcpy(ring_buffer_write_address(&rbuf), sample_buffer,
			       sample_buffer_size);
			ring_buffer_write_advance(&rbuf, sample_buffer_size);
			if (is_debug)
				printf("Buffer size:%lu\n", ring_buffer_count_length(&rbuf));
		} else
			if (is_debug)
				printf("Frames arrive so fast, buffer overrun, skipping\n");
		pthread_mutex_unlock(&mdata);
	
		if (is_debug) {
			curr = getcount() - start;
			sum += curr;
			if (curr < min && count > 10)
				min = curr;

			if (curr > max && count > 10)
				max = curr;

			printf("Current time: %.3f, min jitter: %.3f, max jitter: %.3f, mean: %.3f\n",
				curr / 1000.0F, min / 1000.0F, max / 1000.0F, sum * 0.001F / count);
			printf("Jitter: low : %.3f, high : %.3f\n", sum * 0.001F / count - min / 1000.0F,
				max / 1000.0F - sum * 0.001F / count);
		}
	}

	close(s);
	ring_buffer_free(&rbuf);
	return 0;
}

