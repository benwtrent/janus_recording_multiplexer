janus_recording_multiplexer
===========================

This will multiplex audio and video rtp dumps created by the Janus Recorder.

Very simple gstreamer pipeline that grabs the frames from the provided *.mjr files and outputs to a *.avi file.

It utilizes hardware accelerated h264 encoding so that a constant framerate can be used...no gstreamer multiplexers really work with multiplexing audio and video when the video framerate is variable.

This is very quick and dirty...much cleaning up needs to be done.
