# CS216 Final Project Code Description

## 1. Video Preprocessing

The first part is basically partitioning the video into frames and saving them into a folder. Also, when reading the video input, we resize the frame so that we can reduce the processing time.

## 2. Video segmentation with MRF

### MRF: Using code from Hw3

This part is basically the same as code from Hw3. There are no major changes to the code.

### Show example segmenting result

This part is to demo why we are using the parameters we defined in the report, showing some segmenting result under different conditions.

### Video segmentation

This part is the main function that segments the video. First, we get segmentation result of each frame, and then we calculate the contours of each frame using OpenCV. Finally, we draw the contours of each frame on the original frame and then output the results as a mp4 file.

### Draw some result frames for the report

This part is for demo purposes, we draw some randomly selected frames and then output the results for the final report.
