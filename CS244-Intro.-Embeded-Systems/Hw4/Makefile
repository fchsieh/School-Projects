CC = g++
UTILS = canny_util.c calcpsnr.c
CFLAGS = `pkg-config --cflags --libs opencv` -lpthread -O3

all: camera image video

camera: camera_canny_psnr.cpp
	$(CC) $(CFLAGS) -I. $(UTILS) camera_canny_psnr.cpp -o psnr

video: video_canny_psnr.cpp
	$(CC) $(CFLAGS) -I. $(UTILS) video_canny_psnr.cpp -o psnr_video

image: image_canny_psnr.cpp
	$(CC) $(CFLAGS) -I. $(UTILS) image_canny_psnr.cpp -o psnr_image
	
clean:
	rm -rf EDGE_*
	rm -rf RAW_*
	rm psnr_* psnr_image_* psnr_video_*
