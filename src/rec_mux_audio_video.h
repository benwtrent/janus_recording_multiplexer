/*
 * rec_mux_audio_video.h
 *
 *  Created on: Oct 21, 2014
 *      Author: bentrent
 *
 *  This contains the declarations for rec_mux_audio_video.h.
 */

/**
 * This function will merge two *.mjr files. Both as assumed to be of that type.
 * One has to be audio and the other is video.
 * Audio is assumed to be PCMA and video Vp8.
 * They will merge to a .avi file given in the destination. Please include extension for the file name currently
 * They will have a constant frame rate and will be encoded H264/PCMA.
 */
int rec_mux_audio_video(const char* audio_file, const char* video_file, const char* dest);
