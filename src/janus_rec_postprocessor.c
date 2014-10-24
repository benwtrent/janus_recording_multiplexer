/*
 just a main function to run the worker...check out rec_mux_audio_video.c/h for more info
 */

#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>
#include "rec_mux_audio_video.h"
int main(int argc, char *argv[]) {

	if(argc != 4) {
		printf("Please supply audio, video, and destination\nExample: ./janus_rec_postprocessing audio.mjr video.mjr destination.avi\n");
		return -1;
	}
	char *audiosrc = argv[1];
	char *videosrc = argv[2];
	char *destination = argv[3];
	printf("Audio Source File: %s, Video source File: %s, Destination: %s\n", audiosrc, videosrc, destination);
	gst_init(NULL, NULL);
	rec_mux_audio_video(audiosrc, videosrc, destination);
	printf("Done\n");
	return EXIT_SUCCESS;
}
