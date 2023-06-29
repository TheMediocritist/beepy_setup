//-------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2015 Andrew Duncan
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <bsd/libutil.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic pop

#include "syslogUtilities.h"

//-------------------------------------------------------------------------

#define DEFAULT_OUTPUT "/dev/fb1"
#define DEFAULT_INPUT "/dev/fb0"
#define DEFAULT_DISPLAY_NUMBER 0
#define DEFAULT_FPS 10

#define SCREEN_WIDTH 400
#define SCREEN_HEIGHT 240
#define SCREEN_BPP 16

//-------------------------------------------------------------------------

volatile bool run = true;

//-------------------------------------------------------------------------

int convertPixel16(uint16_t pxl){ //, uint8_t bpp) {
	// Extract red, green, and blue components
	uint8_t r = (pxl >> 11) & 0x1F;
	uint8_t g = (pxl >> 5) & 0x3F;
	uint8_t b = pxl & 0x1F;

	// Expand the 5-bit and 6-bit values to 8-bit range
	r = (r << 3) | (r >> 2);
	g = (g << 2) | (g >> 4);
	b = (b << 3) | (b >> 2);

	// Convert to grayscale
	uint8_t gray =
		(uint8_t)(0.299 * r +
				  0.587 * g +
				  0.114 * b);
	//printf("value: %u, r: %u, g: %u, b: %u, gray: %u\n", pxl, r, g, b, gray);
	return gray;
}

void printUsage(FILE *fp, const char *name){
	fprintf(fp, "\n");
	fprintf(fp, "Usage: %s <options>\n", name);
	fprintf(fp, "\n");
	fprintf(fp, "    --daemon - start in the background as a daemon\n");
	fprintf(fp, "    --device <device> - framebuffer device");
	fprintf(fp, " (default %s)\n", DEFAULT_OUTPUT);
	fprintf(fp, "    --display <number> - Raspberry Pi display number");
	fprintf(fp, " (default %d)\n", DEFAULT_DISPLAY_NUMBER);
	fprintf(fp, "    --fps <fps> - set desired frames per second");
	fprintf(fp, " (default %d frames per second)\n", DEFAULT_FPS);
	fprintf(fp, "    --pidfile <pidfile> - create and lock PID file");
	fprintf(fp, " (if being run as a daemon)\n");
	fprintf(fp, "    --once - copy only one time, then exit\n");
	fprintf(fp, "    --help - print usage and exit\n");
	fprintf(fp, "\n");
}

//-------------------------------------------------------------------------

static void signalHandler(int signalNumber){
	switch (signalNumber){
	case SIGINT:
	case SIGTERM:
		run = false;
		break;
	};
}

//-------------------------------------------------------------------------

int main(int argc, char *argv[]){
	
	const char *program = basename(argv[0]);
	int fps = DEFAULT_FPS;
	suseconds_t frameDuration =  1000000 / fps;
	bool isDaemon = false;
	bool once = false;
	const char *pidfile = NULL;
	static const char *sopts = "df:hn:p:D:";
	
	static struct option lopts[] = 
	{
		{ "daemon", no_argument, NULL, 'd' },
		{ "fps", required_argument, NULL, 'f' },
		{ "help", no_argument, NULL, 'h' },
		{ "display", required_argument, NULL, 'n' },
		{ "pidfile", required_argument, NULL, 'p' },
		{ "device", required_argument, NULL, 'D' },
		{ "once", no_argument, NULL, 'o' },
		{ NULL, no_argument, NULL, 0 }
	};

	int opt = 0;

	while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) != -1){
		switch (opt){
		case 'd':
			isDaemon = true;
			break;
		case 'f':
			fps = atoi(optarg);
			if (fps > 0){
				frameDuration = 1000000 / fps;
			}
			else{
				fps = 1000000 / frameDuration;
			}
			break;
		case 'h':
			printUsage(stdout, program);
			exit(EXIT_SUCCESS);
			break;
		case 'n':
			break;
		case 'p':
			pidfile = optarg;
			break;
		case 'o':
			once = true;
			break;
		case 'D':
			break;
		default:
			printUsage(stderr, program);
			exit(EXIT_FAILURE);
			break;
		}
	}

	//---------------------------------------------------------------------

	struct pidfh *pfh = NULL;

	if (isDaemon){
		if (pidfile != NULL){
			pid_t otherpid;
			pfh = pidfile_open(pidfile, 0600, &otherpid);

			if (pfh == NULL){
				fprintf(stderr,
						"%s is already running %jd\n",
						program,
						(intmax_t)otherpid);
				exit(EXIT_FAILURE);
			}
		}

		if (daemon(0, 0) == -1){
			fprintf(stderr, "Cannot daemonize\n");
			exitAndRemovePidFile(EXIT_FAILURE, pfh);
		}

		if (pfh){
			pidfile_write(pfh);
		}

		openlog(program, LOG_PID, LOG_USER);
	}

	if (signal(SIGINT, signalHandler) == SIG_ERR){
		perrorLog(isDaemon, program, "installing SIGINT signal handler");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	if (signal(SIGTERM, signalHandler) == SIG_ERR){
		perrorLog(isDaemon, program, "installing SIGTERM signal handler");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	// Open framebuffers and map to memory
	
	struct fb_fix_screeninfo finfo_fb0, finfo_fb1;
	struct fb_var_screeninfo vinfo_fb0, vinfo_fb1;
	
	int fb0fd = open("/dev/fb0", O_RDWR);
	int fb1fd = open("/dev/fb1", O_RDWR);
	
	if (fb0fd == -1 || fb1fd == -1){
		perrorLog(isDaemon, program, "cannot open framebuffer devices");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}
	
	if (ioctl(fb0fd, FBIOGET_FSCREENINFO, &finfo_fb0) == -1 || ioctl(fb1fd, FBIOGET_FSCREENINFO, &finfo_fb1) == -1){
		perrorLog(isDaemon, program, "cannot get framebuffer fixed information");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}
	
	if (ioctl(fb0fd, FBIOGET_VSCREENINFO, &vinfo_fb0) == -1 || ioctl(fb1fd, FBIOGET_VSCREENINFO, &vinfo_fb1) == -1){
		perrorLog(isDaemon, program, "cannot get framebuffer variable information");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}
	
	// Set HDMI display to 400 x 240 x 16 bits per pixel
	vinfo_fb0.xres = SCREEN_WIDTH;
	vinfo_fb0.yres = SCREEN_HEIGHT;
	vinfo_fb0.bits_per_pixel = SCREEN_BPP;
	
	if (ioctl(fb0fd, FBIOPUT_VSCREENINFO, &vinfo_fb0) == -1){
		perror("Failed to put FBIOPUT_VSCREENINFO on /dev/fb0\n");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}
	
	if (vinfo_fb0.xres != SCREEN_WIDTH || vinfo_fb0.yres != SCREEN_HEIGHT || vinfo_fb0.bits_per_pixel != SCREEN_BPP){
		perror("Failed to set the requested screen size: 400 x 240 at 16 bpp\n");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}
	
	uint32_t len_fb0 = finfo_fb0.smem_len;
	uint32_t len_fb1 = finfo_fb0.smem_len;
	
	// Map framebuffers memory
	uint16_t *fb0_data = mmap(0, len_fb0, PROT_READ | PROT_WRITE, MAP_SHARED, fb0fd, 0);
	uint8_t *fb1_data = mmap(0, len_fb1, PROT_READ | PROT_WRITE, MAP_SHARED, fb1fd, 0);
	
	if (fb1_data == MAP_FAILED || fb0_data == MAP_FAILED){
		perrorLog(isDaemon, program, "cannot map framebuffers into memory");
		//exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}
	
	//memset(fb1_data, 0, finfo_fb1.smem_len);
	
	//---------------------------------------------------------------------
	
	// Create 2 additional buffers the same size as /dev/fb0
	uint16_t *back_buffer = malloc(len_fb0);
	uint16_t *front_buffer = malloc(len_fb0);

	if ((back_buffer == NULL) || (front_buffer == NULL)){
		perrorLog(isDaemon, program, "cannot allocate offscreen buffers");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	memset(back_buffer, 0, len_fb0);

	//---------------------------------------------------------------------

	messageLog(isDaemon,
				program,
				LOG_INFO,
				"snag \n copying from /dev/fb0 [%d x %d, %d bpp] (%d)\n copying to   /dev/fb1 [%d x %d, %d bpp] (%d)",
				vinfo_fb0.xres,
				vinfo_fb0.yres,
				vinfo_fb0.bits_per_pixel,
				finfo_fb0.smem_len,
				vinfo_fb1.xres,
				vinfo_fb1.yres,
				vinfo_fb1.bits_per_pixel,
				finfo_fb1.smem_len);

	//---------------------------------------------------------------------

	struct timeval start_time;
	struct timeval end_time;
	struct timeval elapsed_time;

	//---------------------------------------------------------------------

	uint32_t pixels = vinfo_fb1.xres * vinfo_fb1.yres;
	uint32_t pixel;
	uint16_t x, y;
	uint8_t bit;

	while (run)
	{
		gettimeofday(&start_time, NULL);

		//-----------------------------------------------------------------

		// Read /dev/fb0 data to front_buffer
		memcpy(front_buffer, fb0_data, len_fb0);

		uint8_t *fb1_8pixels = fb1_data;
		uint16_t *front_buffer_pixel = front_buffer;
		uint16_t *back_buffer_pixel = back_buffer;
		pixel = 0;
		
		//char byte_buffer = 0;
		for (y = 0; y < 240; y++){
			for (x = 0; x < 50; x++){                // each x = 8 bits in /dev/fb1
				for (bit = 0; bit < 8; bit++){
					
					// Check if pixel has changed since last buffered
					if (*front_buffer_pixel != *back_buffer_pixel){
						
						// If pixel changed, convert rgb to grayscale 0-255
						uint8_t gray = convertPixel16(*front_buffer_pixel);
						
						// Threshold gray, and set /dev/fb1 pixel to either 1 or 0
						if (gray > 127) {
							fb1_data[pixel] = 1;
						}
						else{
							//*fb1_8pixels &=  ~(1 << (7 - bit));
							fb1_data[pixel] = 0;
						}
					}
					++front_buffer_pixel;
					++back_buffer_pixel;
					++pixel;
				}
				++fb1_8pixels;
			}
		}

		// Flip buffers
		back_buffer = front_buffer;

		//-----------------------------------------------------------------

		if (once){
			messageLog(isDaemon, program, LOG_INFO, "ran once, exiting now");
			break;
		}

		gettimeofday(&end_time, NULL);
		timersub(&end_time, &start_time, &elapsed_time);

		if (elapsed_time.tv_sec == 0){
			if (elapsed_time.tv_usec < frameDuration){
				usleep(frameDuration -  elapsed_time.tv_usec);
			}
		}
	}

	//---------------------------------------------------------------------

	free(front_buffer);
	free(back_buffer);
	memset(fb1_data, 0, sizeof(fb1_data));
	memset(fb0_data, 0, sizeof(fb0_data));
	munmap(fb1_data, len_fb1);
	munmap(fb0_data, len_fb0);
	close(fb1fd);
	close(fb0fd);

	messageLog(isDaemon, program, LOG_INFO, "exiting");

	if (isDaemon)
	{
		closelog();
	}

	if (pfh)
	{
		pidfile_remove(pfh);
	}

	//---------------------------------------------------------------------

	return 0 ;
}