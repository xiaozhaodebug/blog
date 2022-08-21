all:
	gcc h264.c -I/usr/ffmpeg4.1/ffmpeg/include -I/usr/ffmpeg4.1/x264/include -L/usr/ffmpeg4.1/x264/lib -L/usr/ffmpeg4.1/ffmpeg/lib -o av_video_h264 -lavformat -lavcodec -lavutil -lavfilter -lavdevice -lswresample -lswscale -lpthread -lSDL2 -lSDL -lm -lasound
clean:
	rm av_video_h264 *.yuv *.h264