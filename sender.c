#include <stdlib.h>
#include <string.h>
#include <celt/celt.h>
#include <celt/celt_types.h>

#include <time.h>
#include <sys/time.h>
 #include <stdint.h>

#include <math.h>
 
#include <arpa/inet.h>
 #include <netinet/in.h>
 #include <stdio.h>
 #include <sys/types.h>
#include <sys/socket.h>
 #include <unistd.h>

#include "types.h"
#include "wavfile.h"

	#define SYSFREQ 1E6
 
 #define BUFLEN 512
 #define NPACK 10
 #define PORT 9930

 void diep(char *s)
 {
  perror(s);
  exit(1);
  }

#define SRV_IP "192.168.100.20"
/* diep(), #includes and #defines like in the server */

uint64_t getcount(void) {
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec * SYSFREQ + t.tv_usec;
}


int main(void)
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
	FILE * fd;
int j, k;
	uint32_t frames;  // FIXME! 4 byte!

	uint64_t start;
 struct sockaddr_in si_other;
 int s, i, slen=sizeof(si_other);
  char buf[BUFLEN];

	CELTMode * cm;
	CELTEncoder * ce;
	int * error;
	int compressed;

	int jitter = 0;
	int64_t t;


	double dr = 0, de = 0;
	
  	srand(time(NULL));

if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
  diep("socket");

 memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
 si_other.sin_port = htons(PORT);
  if (inet_aton(SRV_IP, &si_other.sin_addr)==0) {
   fprintf(stderr, "inet_aton() failed\n");
    exit(1);
  }

	fd = fopen("else.wav", "rb");
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
	cm = celt_mode_create(sample_rate, sample_size, NULL);
	//printf("1\n");
	ce = celt_encoder_create(cm, sample_channels, NULL);

	celt_encoder_ctl(ce, CELT_SET_COMPLEXITY(10));
	celt_encoder_ctl(ce, CELT_SET_PREDICTION(2));
	celt_encoder_ctl(ce, CELT_SET_VBR_RATE(160000));
  
 for (i = 0; i < (sample_rate / sample_size) * (frames / (sample_rate * sample_channels * (sample_bits / 8))); ++i) {
		start = getcount();
		
		read_sample_into_buffer(fd, (char *)sample_buffer, sample_size);
		printf("%d %d \n", (sample_rate / sample_size) * (frames / (sample_rate * sample_channels * (sample_bits / 8))) , i);

		printf("Sample_buffer_size: %d\n", sample_buffer_size);
		compressed = celt_encode(ce, sample_buffer, NULL, encoded_buffer, 1024);

		printf("Compressed bytes: %d, bitrate: %d kbps\n", compressed, (int)((sample_rate * sample_bits * sample_channels / 1024) * (compressed * 1.0F / sample_buffer_size)));

		*((uint16_t *)sample_buffer) = compressed;
		memcpy((char *)sample_buffer + 2, encoded_buffer, compressed);

		printf("Sending packet %d\n", i);
		if (sendto(s, sample_buffer, compressed + 2, 0, &si_other, slen) == -1)
			diep("sendto()");
		usleep(sample_size * 975000 / sample_rate); //+ jitter);
		printf("Planned time: %.2f ms, real time: %.2f ms, needed time: %.2f\n", (sample_size * 920000 / sample_rate + jitter) / 1000.0F, (getcount() - start) / 1000.0F,
				 (sample_size * 920000 / sample_rate) / 1000.0F);
		
		dr += (getcount() - start) / 1000.0F;
		de += (sample_size * 920000 / sample_rate) / 1000.0F;
		if ((abs(t = ((sample_size * 920000 / sample_rate) - (getcount() - start))) > (int)(1E-6 * (sample_size * 920000 / sample_rate) + 250 )) ){//|| (fabs(dr - de) > 1E-6 * de)) {
			if (t > 0 || (de > dr)) {
				jitter += 2;
				printf("Sending packes too fast, increasing jitter: %d\n", jitter);
			} else {
				jitter -= 2;
				printf("Sending packes too slow, decreasing jitter: %d\n", jitter);
			}
		} //else// {
			//jitter = 0;
		//}
  }

  close(s);
  return 0;
 }
 