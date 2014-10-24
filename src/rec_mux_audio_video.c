/*
 * rec_mux_audio_video.c
 *
 *  Created on: Oct 21, 2014
 *      Author: bentrent (much "borrowed" from meetecho...designed for their RTP dumps)
 */

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include "pp-rtp.h"
#include <arpa/inet.h>
#include <sys/types.h>
#include <endian.h>
#include "janus_rtp.h"
#include <stdio.h>
#include <sys/time.h>


static janus_pp_frame_packet *video_packet_list = NULL, *audio_packet_list =
NULL, *video_last = NULL, *audio_last = NULL, *video_walk = NULL, *audio_walk = NULL;

//copy pasta from janus recording post processing
//TODO clean it up...tons of code reuse...
static int populate_video_list(const char* source) {
	FILE *file = fopen(source, "rb");
	if (!file) {
		printf("Could not open file %s\n", source);
		return -1;
	}
	fseek(file, 0L, SEEK_END);
	long fsize = ftell(file);
	printf("VIdoe file size: %lu\n", fsize);
	fseek(file, 0L, SEEK_SET);
	int bytes = 0, skip = 0;
	long offset = 0;
	uint16_t len = 0, count = 0;
	uint32_t first_ts = 0, last_ts = 0, reset = 0; /* To handle whether there's a timestamp reset in the recording */
	char prebuffer[1500];
	memset(prebuffer, 0, 1500);
	/* Let's look for timestamp resets first */
	while (offset < fsize) {
		/* Read frame header */
		skip = 0;
		fseek(file, offset, SEEK_SET);
		bytes = fread(prebuffer, sizeof(char), 8, file);
		if (bytes != 8 || prebuffer[0] != 'M') {
			return 1;
		}
		offset += 8;
		bytes = fread(&len, sizeof(uint16_t), 1, file);
		len = ntohs(len);
		offset += 2;
		if (len == 5) {
			/* This is the main header */
			bytes = fread(prebuffer, sizeof(char), 5, file);
			if (prebuffer[0] != 'v') {
				printf("wrong file type processed %s\n", source);
				return 1;
			}
			offset += len;
			continue;
		} else if (len < 12) {
			/* Not RTP, skip */
			offset += len;
			continue;
		}
		/* Only read RTP header */
		bytes = fread(prebuffer, sizeof(char), 16, file);
		janus_pp_rtp_header *rtp = (janus_pp_rtp_header *) prebuffer;
		if (last_ts == 0) {
			first_ts = ntohl(rtp->timestamp);
			if (first_ts > 1000 * 1000) /* Just used to check whether a packet is pre- or post-reset */
				first_ts -= 1000 * 1000;
		} else {
			if (ntohl(rtp->timestamp) < last_ts) {
				/* The new timestamp is smaller than the next one, is it a timestamp reset or simply out of order? */
				if (last_ts - ntohl(rtp->timestamp) > 2 * 1000 * 1000 * 1000) {
					reset = ntohl(rtp->timestamp);
				}
			} else if (ntohl(rtp->timestamp) < reset) {
				reset = ntohl(rtp->timestamp);
			}
		}
		last_ts = ntohl(rtp->timestamp);
		/* Skip data for now */
		offset += len;
	}
	/* Now let's parse the frames and order them */
	offset = 0;
	while (offset < fsize) {
		/* Read frame header */
		skip = 0;
		fseek(file, offset, SEEK_SET);
		bytes = fread(prebuffer, sizeof(char), 8, file);
		prebuffer[8] = '\0';
		offset += 8;
		bytes = fread(&len, sizeof(uint16_t), 1, file);
		len = ntohs(len);
		offset += 2;
		if (len < 12) {
			/* Not RTP, skip */
			offset += len;
			continue;
		}
		/* Only read RTP header */
		bytes = fread(prebuffer, sizeof(char), 16, file);
		janus_pp_rtp_header *rtp = (janus_pp_rtp_header *) prebuffer;
		if (rtp->extension) {
			janus_pp_rtp_header_extension *ext =
					(janus_pp_rtp_header_extension *) (prebuffer + 12);
			skip = 4 + ntohs(ext->length) * 4;
		}
		/* Generate frame packet and insert in the ordered list */
		janus_pp_frame_packet *p = calloc(1, sizeof(janus_pp_frame_packet));
		if (p == NULL) {
			fclose(file);
			return -1;
		}
		p->seq = ntohs(rtp->seq_number);
		if (reset == 0) {
			/* Simple enough... */
			p->ts = ntohl(rtp->timestamp);
		} else {
			/* Is this packet pre- or post-reset? */
			if (ntohl(rtp->timestamp) > first_ts) {
				/* Pre-reset... */
				p->ts = ntohl(rtp->timestamp);
			} else {
				/* Post-reset... */
				uint64_t max32 = UINT32_MAX;
				max32++;
				p->ts = max32 + ntohl(rtp->timestamp);
			}
		}
		p->len = len;
		p->offset = offset;
		p->skip = skip;
		p->next = NULL;
		p->prev = NULL;
		if (video_packet_list == NULL) {
			/* First element becomes the list itself (and the last item), at least for now */
			video_packet_list = p;
			video_last = p;
		} else {
			/* Check where we should insert this, starting from the end */
			int added = 0;
			janus_pp_frame_packet *tmp = video_last;
			while (tmp) {
				if (tmp->ts < p->ts) {
					/* The new timestamp is greater than the last one we have, append */
					added = 1;
					if (tmp->next != NULL) {
						/* We're inserting */
						tmp->next->prev = p;
						p->next = tmp->next;
					} else {
						/* Update the last packet */
						video_last = p;
					}
					tmp->next = p;
					p->prev = tmp;
					break;
				} else if (tmp->ts == p->ts) {
					/* Same timestamp, check the sequence number */
					if (tmp->seq < p->seq && (abs(tmp->seq - p->seq) < 10000)) {
						/* The new sequence number is greater than the last one we have, append */
						added = 1;
						if (tmp->next != NULL) {
							/* We're inserting */
							tmp->next->prev = p;
							p->next = tmp->next;
						} else {
							/* Update the last packet */
							video_last = p;
						}
						tmp->next = p;
						p->prev = tmp;
						break;
					} else if (tmp->seq > p->seq
							&& (abs(tmp->seq - p->seq) > 10000)) {
						/* The new sequence number (resetted) is greater than the last one we have, append */
						added = 1;
						if (tmp->next != NULL) {
							/* We're inserting */
							tmp->next->prev = p;
							p->next = tmp->next;
						} else {
							/* Update the last packet */
							video_last = p;
						}
						tmp->next = p;
						p->prev = tmp;
						break;
					}
				}
				/* If either the timestamp ot the sequence number we just got is smaller, keep going back */
				tmp = tmp->prev;
			}
			if (!added) {
				/* We reached the start */
				p->next = video_packet_list;
				video_packet_list->prev = p;
				video_packet_list = p;
			}
		}
		/* Skip data for now */
		offset += len;
		count++;
	}
	fclose(file);
	return 0;
}

static int populate_audio_list(const char* source) {
	FILE *file = fopen(source, "rb");

	if (!file) {
		printf("Could not open file %s\n", source);
		return -1;
	}
	fseek(file, 0L, SEEK_END);
	long fsize = ftell(file);
	printf("Audio file size: %lu\n", fsize);
	fseek(file, 0L, SEEK_SET);
	int bytes = 0, skip = 0;
	long offset = 0;
	uint16_t len = 0, count = 0;
	uint32_t first_ts = 0, last_ts = 0, reset = 0; /* To handle whether there's a timestamp reset in the recording */
	char prebuffer[1500];
	memset(prebuffer, 0, 1500);
	/* Let's look for timestamp resets first */
	while (offset < fsize) {
		/* Read frame header */
		skip = 0;
		fseek(file, offset, SEEK_SET);
		bytes = fread(prebuffer, sizeof(char), 8, file);
		if (bytes != 8 || prebuffer[0] != 'M') {
			return 1;
		}
		offset += 8;
		bytes = fread(&len, sizeof(uint16_t), 1, file);
		len = ntohs(len);
		offset += 2;
		if (len == 5) {
			/* This is the main header */
			bytes = fread(prebuffer, sizeof(char), 5, file);
			if (prebuffer[0] != 'a') {
				printf("wrong file type processed %s\n", source);
				return 1;
			}
			offset += len;
			continue;
		} else if (len < 12) {
			/* Not RTP, skip */
			offset += len;
			continue;
		}
		/* Only read RTP header */
		bytes = fread(prebuffer, sizeof(char), 16, file);
		janus_pp_rtp_header *rtp = (janus_pp_rtp_header *) prebuffer;
		if (last_ts == 0) {
			first_ts = ntohl(rtp->timestamp);
			if (first_ts > 1000 * 1000) /* Just used to check whether a packet is pre- or post-reset */
				first_ts -= 1000 * 1000;
		} else {
			if (ntohl(rtp->timestamp) < last_ts) {
				/* The new timestamp is smaller than the next one, is it a timestamp reset or simply out of order? */
				if (last_ts - ntohl(rtp->timestamp) > 2 * 1000 * 1000 * 1000) {
					reset = ntohl(rtp->timestamp);
				}
			} else if (ntohl(rtp->timestamp) < reset) {
				reset = ntohl(rtp->timestamp);
			}
		}
		last_ts = ntohl(rtp->timestamp);
		/* Skip data for now */
		offset += len;
	}
	/* Now let's parse the frames and order them */
	offset = 0;
	while (offset < fsize) {
		/* Read frame header */
		skip = 0;
		fseek(file, offset, SEEK_SET);
		bytes = fread(prebuffer, sizeof(char), 8, file);
		prebuffer[8] = '\0';
		offset += 8;
		bytes = fread(&len, sizeof(uint16_t), 1, file);
		len = ntohs(len);
		offset += 2;
		if (len < 12) {
			/* Not RTP, skip */
			offset += len;
			continue;
		}
		/* Only read RTP header */
		bytes = fread(prebuffer, sizeof(char), 16, file);
		janus_pp_rtp_header *rtp = (janus_pp_rtp_header *) prebuffer;
		if (rtp->extension) {
			janus_pp_rtp_header_extension *ext =
					(janus_pp_rtp_header_extension *) (prebuffer + 12);
			skip = 4 + ntohs(ext->length) * 4;
		}
		/* Generate frame packet and insert in the ordered list */
		janus_pp_frame_packet *p = calloc(1, sizeof(janus_pp_frame_packet));
		if (p == NULL) {
			fclose(file);
			return -1;
		}
		p->seq = ntohs(rtp->seq_number);
		if (reset == 0) {
			/* Simple enough... */
			p->ts = ntohl(rtp->timestamp);
		} else {
			/* Is this packet pre- or post-reset? */
			if (ntohl(rtp->timestamp) > first_ts) {
				/* Pre-reset... */
				p->ts = ntohl(rtp->timestamp);
			} else {
				/* Post-reset... */
				uint64_t max32 = UINT32_MAX;
				max32++;
				p->ts = max32 + ntohl(rtp->timestamp);
			}
		}
		p->len = len;
		p->offset = offset;
		p->skip = skip;
		p->next = NULL;
		p->prev = NULL;
		if (audio_packet_list == NULL) {
			/* First element becomes the list itself (and the last item), at least for now */
			audio_packet_list = p;
			audio_last = p;
		} else {
			/* Check where we should insert this, starting from the end */
			int added = 0;
			janus_pp_frame_packet *tmp = audio_last;
			while (tmp) {
				if (tmp->ts < p->ts) {
					/* The new timestamp is greater than the last one we have, append */
					added = 1;
					if (tmp->next != NULL) {
						/* We're inserting */
						tmp->next->prev = p;
						p->next = tmp->next;
					} else {
						/* Update the last packet */
						audio_last = p;
					}
					tmp->next = p;
					p->prev = tmp;
					break;
				} else if (tmp->ts == p->ts) {
					/* Same timestamp, check the sequence number */
					if (tmp->seq < p->seq && (abs(tmp->seq - p->seq) < 10000)) {
						/* The new sequence number is greater than the last one we have, append */
						added = 1;
						if (tmp->next != NULL) {
							/* We're inserting */
							tmp->next->prev = p;
							p->next = tmp->next;
						} else {
							/* Update the last packet */
							audio_last = p;
						}
						tmp->next = p;
						p->prev = tmp;
						break;
					} else if (tmp->seq > p->seq
							&& (abs(tmp->seq - p->seq) > 10000)) {
						/* The new sequence number (resetted) is greater than the last one we have, append */
						added = 1;
						if (tmp->next != NULL) {
							/* We're inserting */
							tmp->next->prev = p;
							p->next = tmp->next;
						} else {
							/* Update the last packet */
							audio_last = p;
						}
						tmp->next = p;
						p->prev = tmp;
						break;
					}
				}
				/* If either the timestamp ot the sequence number we just got is smaller, keep going back */
				tmp = tmp->prev;
			}
			if (!added) {
				/* We reached the start */
				p->next = audio_packet_list;
				audio_packet_list->prev = p;
				audio_packet_list = p;
			}
		}
		/* Skip data for now */
		offset += len;
		count++;
	}
	fclose(file);
	return 0;
}

int rec_mux_audio_video(const char* audio_file, const char* video_file,
		const char* dest) {

	if(strlen(audio_file) == 0 || strlen(video_file) == 0)
		return -1;

	int audio_ret = 0, video_ret = 0;
	audio_ret = populate_audio_list(audio_file);
	video_ret = populate_video_list(video_file);
	if( audio_ret != 0 || video_ret != 0)
		return -1; //something went wrong...

	video_walk = video_packet_list;
	audio_walk = audio_packet_list;
	FILE *audiofd = fopen(audio_file, "rb");
	FILE *videofd = fopen(video_file, "rb");
	if(!audiofd || !videofd) //could not open the files.
		return -1;
	//TODO figure out a way to do this without decoding it, setting a static framerate/resolution, then re-encoding again...
	/**
	 * Treats the stream as "live" and time stamps each packet. This is so the audio and video will sync in the file.
	 * Vaapiencode demands the given resolution... the framerate could conceivably be increased or decreased
	 * No gstreamer multiplexer I have found supports multiplexing audio and video when the video framerate is variable
	 */
	char* pipelinestr = g_strdup_printf("appsrc block=true do-timestamp=true format=3 is-live=true name=video_appsrc "
			"caps=\"application/x-rtp, media=(string)video, payload=(int)100, clock-rate=(int)90000, encoding-name=(string)VP8-DRAFT-IETF-01\" ! "
	        "queue ! rtpvp8depay ! vp8dec ! videoconvert ! videorate ! videoscale ! video/x-raw, height=480, width=640, framerate=15/1 ! "
	        "vaapiencode_h264 ! vaapiparse_h264 ! mux.video_0 avimux name=mux ! queue ! filesink sync=true location=%s qos=false "
	        "appsrc format=3 do-timestamp=true is-live=true block=true name=audio_appsrc "
	        "caps=\"application/x-rtp, media=(string)audio, payload=(int)8, clock-rate=(int)8000, encoding-name=(string)PCMA\" ! "
	        "rtppcmadepay ! mux.audio_0", dest);
	GError* pipeErr = NULL;
	GstElement* pipeline = gst_parse_launch(pipelinestr, &pipeErr);
	g_free(pipelinestr);
	if(pipeErr)
	{
		printf("App src pipeline failed\n");
		g_error_free(pipeErr);
		if(pipeline)
			gst_object_unref(pipeline);
		fclose(audiofd);
		fclose(videofd);
		return -1;
	}
	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	GstElement* videoappsrc = gst_bin_get_by_name(GST_BIN(pipeline), "video_appsrc");
	GstElement* audioappsrc = gst_bin_get_by_name(GST_BIN(pipeline), "audio_appsrc");

	if(videoappsrc && audioappsrc)
	{
		//much of this is taken directly from Janus Rec play plugin
		gboolean asent = FALSE, vsent = FALSE;
		struct timeval now, abefore, vbefore;
		time_t d_s, d_us;
		gettimeofday(&now, NULL);
		gettimeofday(&abefore, NULL);
		gettimeofday(&vbefore, NULL);
		int64_t ts_diff = 0, passed = 0;
		int audiobytes = 0, videobytes = 0, audiolen = 0, videolen = 0;
		while(video_walk || audio_walk)
		{
			if(!asent && !vsent) {
				usleep(5000);
			}

			asent = FALSE;
			vsent = FALSE;
			if(audio_walk)
			{
				if(audio_walk == audio_packet_list)
				{
					audiolen = audio_walk->len;
					fseek(audiofd, audio_walk->offset, SEEK_SET);
					char* buffer = calloc(audio_walk->len, sizeof(char));
					audiobytes = fread(buffer, sizeof(char), audiolen, audiofd);
					if(audiobytes != audiolen)
						printf("could not read all the needed audio bytes\n");
					rtp_header *rtp = (rtp_header *)buffer;
					rtp->type = 8; //assuming pcma
					GstBuffer* gstbuf = gst_buffer_new_wrapped(buffer, audiolen);
					gst_app_src_push_buffer(GST_APP_SRC(audioappsrc), gstbuf);
					gettimeofday(&now, NULL);
					abefore.tv_sec = now.tv_sec;
					abefore.tv_usec = now.tv_usec;
					asent = TRUE;
					audio_walk = audio_walk->next;
				} else {
					ts_diff = audio_walk->ts - audio_walk->prev->ts;
					ts_diff = (ts_diff*1000)/8;	/* FIXME Again, we're assuming signal channel PCMA and it's 48khz */
					/* Check if it's time to send */
					gettimeofday(&now, NULL);
					d_s = now.tv_sec - abefore.tv_sec;
					d_us = now.tv_usec - abefore.tv_usec;
					if(d_us < 0) {
						d_us += 1000000;
						--d_s;
					}
					passed = d_s*1000000 + d_us;
					if(passed < (ts_diff-5000)) {
						asent = FALSE;
					} else {
						/* Update the reference time */
						abefore.tv_usec += ts_diff%1000000;
						if(abefore.tv_usec > 1000000) {
							abefore.tv_sec++;
							abefore.tv_usec -= 1000000;
						}
						if(ts_diff/1000000 > 0) {
							abefore.tv_sec += ts_diff/1000000;
							abefore.tv_usec -= ts_diff/1000000;
						}
						/* Send now */
						audiolen = audio_walk->len;
						fseek(audiofd, audio_walk->offset, SEEK_SET);
						char* buffer = calloc(audiolen, sizeof(char));
						audiobytes = fread(buffer, sizeof(char), audiolen, audiofd);
						if(audiobytes != audiolen)
							printf("could not read all the needed audio bytes\n");
						/* Update payload type */
						rtp_header *rtp = (rtp_header *)buffer;
						rtp->type = 8;	/* FIXME We assume it's PCMA */
						GstBuffer* gstbuf = gst_buffer_new_wrapped(buffer, audiolen);
						gst_app_src_push_buffer(GST_APP_SRC(audioappsrc), gstbuf);
						asent = TRUE;
						audio_walk = audio_walk->next;
						//End of the stream. Send the EOS to the appsrc
						if(!audio_walk)
							gst_app_src_end_of_stream (GST_APP_SRC(audioappsrc));

					}
				}
			}
			if(video_walk)
			{
				if(video_walk == video_packet_list)
				{
					uint64_t ts = video_walk->ts;
					while(video_walk && video_walk->ts == ts) {
						fseek(videofd, video_walk->offset, SEEK_SET);
						videolen = video_walk->len;
						char* buffer = calloc(videolen, sizeof(char));
						videobytes = fread(buffer, sizeof(char), videolen, videofd);
						if(videobytes != videolen)
							printf("could not read all the needed video bytes\n");
						/* Update payload type */
						rtp_header *rtp = (rtp_header *)buffer;
						rtp->type = 100;	/* FIXME We assume it's VP8 */
						GstBuffer* gstbuf = gst_buffer_new_wrapped(buffer, videolen);
						gst_app_src_push_buffer(GST_APP_SRC(videoappsrc), gstbuf);
						video_walk = video_walk->next;
						//End of the stream. Send the EOS to the appsrc
						if(!video_walk)
							gst_app_src_end_of_stream (GST_APP_SRC(videoappsrc));
					}
					vsent = TRUE;
					gettimeofday(&now, NULL);
					vbefore.tv_sec = now.tv_sec;
					vbefore.tv_usec = now.tv_usec;
				} else {
					ts_diff = video_walk->ts - video_walk->prev->ts;
					ts_diff = (ts_diff * 1000) / 90;
					/* Check if it's time to send */
					gettimeofday(&now, NULL);
					d_s = now.tv_sec - vbefore.tv_sec;
					d_us = now.tv_usec - vbefore.tv_usec;
					if (d_us < 0) {
						d_us += 1000000;
						--d_s;
					}
					passed = d_s * 1000000 + d_us;
					if (passed < (ts_diff - 5000)) {
						vsent = FALSE;
					} else {
						/* Update the reference time */
						vbefore.tv_usec += ts_diff % 1000000;
						if (vbefore.tv_usec > 1000000) {
							vbefore.tv_sec++;
							vbefore.tv_usec -= 1000000;
						}
						if (ts_diff / 1000000 > 0) {
							vbefore.tv_sec += ts_diff / 1000000;
							vbefore.tv_usec -= ts_diff / 1000000;
						}
						/* There may be multiple packets with the same timestamp, send them all */
						uint64_t ts = video_walk->ts;
						while (video_walk && video_walk->ts == ts) {
							/* Send now */
							videolen = video_walk->len;
							char* buffer = calloc(videolen, sizeof(char));
							fseek(videofd, video_walk->offset, SEEK_SET);
							videobytes = fread(buffer, sizeof(char), videolen,
									videofd);
							if (videobytes != videolen)
								printf("could not read all the needed video bytes\n");
							/* Update payload type */
							rtp_header *rtp = (rtp_header *) buffer;
							rtp->type = 100; /* FIXME We assume it's VP8 */
							GstBuffer* gstbuf = gst_buffer_new_wrapped(buffer, videolen);
							gst_app_src_push_buffer(GST_APP_SRC(videoappsrc), gstbuf);
							video_walk = video_walk->next;
							//End of the stream. Send the EOS to the appsrc
							if(!video_walk)
								gst_app_src_end_of_stream (GST_APP_SRC(videoappsrc));
						}
						vsent = TRUE;
					}
				}
			}
		}
		printf("Done pushing the buffers\n");
	}
	gst_object_unref(videoappsrc);
	gst_object_unref(audioappsrc);
	//Gotta push the EOS to the file sink...for some reason the appsrc EOS does not signal EOS to the file sink
	GstBus* bus_src = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
	gst_element_send_event(pipeline, gst_event_new_eos());
	//Waiting for the EOS to actually be processed...will block for a time
	GstMessage* msg_src = gst_bus_timed_pop_filtered(bus_src, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
	if(msg_src)
		gst_message_unref(msg_src);
	if(bus_src)
		gst_object_unref(bus_src);

	gst_element_set_state(pipeline, GST_STATE_PAUSED);
	gst_element_set_state(pipeline, GST_STATE_READY);
	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(pipeline);

	//close our file handles...
	fclose(audiofd);
	fclose(videofd);
	return 0;
}
