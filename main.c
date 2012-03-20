//      main.c
//      
//      Copyright 2010 Ilya <ilya@laptop>
//      
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.
//      
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//      
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
//      MA 02110-1301, USA.


#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include <celt/celt.h>
#include <celt/celt_types.h>

#include <time.h>


#include "types.h"
#include "wavfile.h"
#include "ao.h"




int main(int argc, char** argv)
{
	int current, pos;

	celt_int16 * sample_buffer;
	unsigned char * encoded_buffer;
	double * out_buffer;
	int sample_rate;
	int sample_bits;
	int sample_channels;
	int sample_buffer_size;
	int sample;
	int sample_msec;
	int sample_size;
	float freq;
	double ampl, phase, frequency;
	int i, j, k;
	uint32_t frames;  // FIXME! 4 byte!

	FILE * fd;

	int bad;
	int badbytes, lostbytes, totalbytes;
	
	struct Chord chord;


	// Sound output through libao

	struct AOutput * device;


	CELTMode * cm;
	CELTEncoder * ce;
	CELTDecoder * cd;
	int * error;
	int compressed;

	srand(time(NULL));
	
	// Opening sound file for reading source data

	fd = fopen("rex.wav", "rb");
	fseek(fd, 40, SEEK_SET);
	fread(&frames, sizeof(uint64_t), 1, fd);
	frames = 800000000;
	printf("%d\n", frames);

	// Hardcoded parameters for DSP

	sample_rate = 44100; // Desc. frequency, Hz
	sample_bits = 16; // Bits per sample
	sample_channels = 2; // Stereo
//	sample_msec = 10; // Size of sample, 100 ms for 10 Hz FFT freq. resolution

	sample_size = 1024;

	//sample_size = (int)(sample_rate * (sample_msec / 1000.0F));
	sample_buffer_size =  sample_size * sample_bits/8 * sample_channels;
	sample_buffer = (celt_int16 *) calloc(sample_buffer_size, sizeof(char));
	encoded_buffer = (unsigned char *) calloc(sample_buffer_size, sizeof(char));
	out_buffer = (double *) calloc(sample_buffer_size / (sample_bits/8), sizeof(double));


	device = aout_init();

	cm = celt_mode_create(sample_rate, sample_size, NULL);
	//printf("1\n");
	ce = celt_encoder_create(cm, sample_channels, NULL);

	cd = celt_decoder_create(cm, sample_channels, NULL);
	//printf("2\n");
	celt_encoder_ctl(ce, CELT_SET_COMPLEXITY(10));
	celt_encoder_ctl(ce, CELT_SET_PREDICTION(2));
	celt_encoder_ctl(ce, CELT_SET_VBR_RATE(160000));

	// Reading util end of file (frames - number of frames in WAV)

	badbytes = totalbytes = lostbytes = 0;

	for (i = 0; i < (sample_rate / sample_size) * (frames / (sample_rate * sample_channels * (sample_bits / 8))) ; ++i) {

		// Reading sample from file
		read_sample_into_buffer(fd, (char *)sample_buffer, sample_size);
		printf("%d %d \n", (sample_rate / sample_size) * (frames / (sample_rate * sample_channels * (sample_bits / 8))) , i);

		printf("Sample_buffer_size: %d\n", sample_buffer_size);
		compressed = celt_encode(ce, sample_buffer, NULL, encoded_buffer, 1024);

		printf("Compressed bytes: %d, bitrate: %d kbps\n", compressed, (int)((sample_rate * sample_bits * sample_channels / 1024) * (compressed * 1.0F / sample_buffer_size)));

		memset(sample_buffer, 0, sample_buffer_size);


		if (rand() > (int)(0.95 * RAND_MAX)) {
			bad = rand() % (compressed / 4) + 5;
			printf("Packet error, wiped %d bytes\n", bad);
			memcpy(encoded_buffer + 2 * (compressed / 3) + 4, encoded_buffer, bad);
			badbytes += bad;
		}

		if (rand() > (int)(0.0075 * RAND_MAX)) {
		
		celt_decode(cd, encoded_buffer, compressed, sample_buffer);
		

		aout_play(device, sample_buffer, sample_buffer_size);
		} else {
			usleep(sample_size * 1000000 / sample_rate);
			printf("Packet was lost\n");
			lostbytes += compressed;
		}
		totalbytes += compressed;
		printf("Total kbytes: %d, lost: %d (%.3f proc), damaged: %d (%.3f proc)\n", totalbytes / 1024, lostbytes / 1024, lostbytes * 100.0F / totalbytes , badbytes / 1024, badbytes * 100.0F / totalbytes );
	}

	
	aout_close(device);

	return 0;
}
