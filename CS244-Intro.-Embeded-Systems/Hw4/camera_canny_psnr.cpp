/**
 */
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <iostream>

#include "calcpsnr.h"
#include "canny_util.h"
#include "opencv2/opencv.hpp"
using namespace std;
using namespace cv;

/* Pi Camera MAX resolution: 2592x1944 (default: 640x480) */
/* ground_crew.h264: 1280x720 */
/* tiger_face.jpg: 888x900 */

#define WIDTH 160
#define HEIGHT 120
#define NFRAME 30.0

struct thread_args {
  int start;
  int end;
  double local_PSNR;
};

void *cal_psnr(void *arguments) {
  struct thread_args *args = (struct thread_args *)arguments;
  int start = args->start;
  int end = args->end;
  char raw_image_name[128];
  char edge_image_name[128];
  double PSNR = 0.0;

  for (int i = start; i < end; i++) {
    /* Get the edge image name */
    sprintf(edge_image_name, "EDGE_CAMERA/EDGE_%03d.pgm", i);
    /* Get the raw image name */
    sprintf(raw_image_name, "RAW_CAMERA/RAW_%03d.pgm", i);
    PSNR = PSNR + calcpsnr(raw_image_name, edge_image_name);
  }
  args->local_PSNR = PSNR;
  return NULL;
}

int main(int argc, char **argv) {
  char *dirfilename;       /* Name of the output gradient direction image */
  char outfilename[128];   /* Name of the output "edge" image */
  char composedfname[128]; /* Name of the output "direction" image */
  unsigned char *image;    /* The input image */
  unsigned char *edge;     /* The output edge image */
  int rows, cols;          /* The dimensions of the image. */
  float sigma,             /* Standard deviation of the Gaussian kernel. */
      tlow,                /* Fraction of the high threshold in hysteresis. */
      thigh;               /* High hysteresis threshold control. The actual
                              threshold is the (100 * thigh) percentage point
                              in the histogram of the magnitude of the
                              gradient image that passes non-maximal
                              suppression. */
  int count;               /* Frame count iterator */

  // create directories for saving images
  struct stat st = {0};
  if (stat("EDGE_CAMERA", &st) == -1) {
    mkdir("EDGE_CAMERA", 0755);
  }
  if (stat("RAW_CAMERA", &st) == -1) {
    mkdir("RAW_CAMERA", 0755);
  }

  /****************************************************************************
   * Get the command line arguments.
   ****************************************************************************/
  if (argc < 4) {
    fprintf(stderr, "\n<USAGE> %s sigma tlow thigh [writedirim]\n", argv[0]);
    fprintf(stderr, "      sigma:      Standard deviation of the gaussian");
    fprintf(stderr, " blur kernel.\n");
    fprintf(stderr, "      tlow:       Fraction (0.0-1.0) of the high ");
    fprintf(stderr, "edge strength threshold.\n");
    fprintf(stderr, "      thigh:      Fraction (0.0-1.0) of the distribution");
    fprintf(stderr, " of non-zero edge\n                  strengths for ");
    fprintf(stderr, "hysteresis. The fraction is used to compute\n");
    fprintf(stderr, "                  the high edge strength threshold.\n");
    fprintf(stderr, "      writedirim: Optional argument to output ");
    fprintf(stderr, "a floating point");
    fprintf(stderr, " direction image.\n\n");
    exit(1);
  }

  sigma = atof(argv[1]);
  tlow = atof(argv[2]);
  thigh = atof(argv[3]);
  rows = HEIGHT;
  cols = WIDTH;

  if (argc == 5)
    dirfilename = (char *)"dummy";
  else
    dirfilename = NULL;

  VideoCapture cap;
  // open the default camera (/dev/video0) OR a video OR an image
  // Check VideoCapture documentation for more details
  if (!cap.open(0)) {
    //   if(!cap.open("ground_crew.h264")){
    //   if(!cap.open("tiger_face.jpg")){
    printf("Failed to open media\n");
    return 0;
  }
  //	 cap.set(CV_CAP_PROP_FRAME_WIDTH, WIDTH); // Set input resolution when
  // the video is captured from /dev/video*, i.e. the webcam.
  //   cap.set(CV_CAP_PROP_FRAME_HEIGHT,HEIGHT);
  printf("Media Input: %.0f, %.0f\n", cap.get(CV_CAP_PROP_FRAME_WIDTH),
         cap.get(CV_CAP_PROP_FRAME_HEIGHT));

  // For low-end CPUs, may wait a while until camera stabilizes
  // printf("Sleep 3 seconds for camera stabilization...\n");
  usleep(3 * 1e6);
  printf("=== Start Canny Edge Detection: %.0f frames ===\n", NFRAME);

  Mat frame, grayframe;

  count = 0;
  struct timeval start, end;
  gettimeofday(&start, NULL);
  while (count < NFRAME) {
    // capture
    cap >> frame;
    resize(frame, frame, Size(WIDTH, HEIGHT), 0, 0, INTER_LINEAR);
    // extract the image in gray format
    cvtColor(frame, grayframe, CV_BGR2GRAY);
    image = grayframe.data;

    /****************************************************************************
     * Perform the edge detection. All of the work takes place here.
     ****************************************************************************/
    if (VERBOSE) printf("Starting Canny edge detection.\n");
    if (dirfilename != NULL) {
      sprintf(composedfname, "camera_s_%3.2f_l_%3.2f_h_%3.2f_%d.fim", sigma,
              tlow, thigh, count);
      dirfilename = composedfname;
    }

    /****************************************************************************
     *Apply Canny Edge Detection Algorithm
     ****************************************************************************/
    canny(image, rows, cols, sigma, tlow, thigh, &edge, dirfilename);

    /****************************************************************************
     * Write out the edge image to a file.
     ****************************************************************************/
    sprintf(outfilename, "EDGE_CAMERA/EDGE_%03d.pgm", count);
    if (VERBOSE)
      printf("Writing the edge image in the file %s.\n", outfilename);
    if (write_pgm_image(outfilename, edge, rows, cols, NULL, 255) == 0) {
      fprintf(stderr, "Error writing the edge image, %s.\n", outfilename);
      exit(1);
    }

    /****************************************************************************
     * Write out the captured image to a file.
     ****************************************************************************/
    sprintf(outfilename, "RAW_CAMERA/RAW_%03d.pgm", count);
    if (VERBOSE)
      printf("Writing the edge image in the file %s.\n", outfilename);
    if (write_pgm_image(outfilename, image, rows, cols, NULL, 255) == 0) {
      fprintf(stderr, "Error writing the edge image, %s.\n", outfilename);
      exit(1);
    }
    count++;
  }  // end of while loop

  /****************************************************************************
   * Get FPS information.
   ****************************************************************************/
  gettimeofday(&end, NULL);
  double time_elapsed = ((end.tv_sec * 1000000 + end.tv_usec) -
                         (start.tv_sec * 1000000 + start.tv_usec));
  printf("=== Finish Canny Edge Detection ===\n");
  printf("Total Elapsed Time : %lf sec.\n", time_elapsed / 1000000);
  printf("FPS: %4f.\n", NFRAME / (time_elapsed / 1000000));

  /****************************************************************************
   * Read the edge detected files and raw_images and calculate PSNR.
   ****************************************************************************/
  pthread_t threads[4];
  struct thread_args args[4];
  int iret;
  double PSNR = 0.0;

  if (VERBOSE) printf("Calculating PSNR value...\n");
  // save arguments
  int img_per_t = (int)NFRAME / 4;
  for (int i = 0; i < 4; i++) {
    args[i].start = img_per_t * i;
    args[i].end = img_per_t * (i + 1);
    if (i == 3) {
      args[i].end = NFRAME;
    }
  }

  // launch thread
  for (int i = 0; i < 4; i++) {
    if ((iret = pthread_create(&threads[i], NULL, &cal_psnr, &args[i]))) {
      printf("Thread creation failed: %d\n", iret);
    }
  }
  for (int i = 0; i < 4; i++) pthread_join(threads[i], NULL);
  for (int i = 0; i < 4; i++) PSNR += args[i].local_PSNR;

  PSNR = PSNR / NFRAME;
  printf("Average PSNR value = %3.2f \n", PSNR);

  // free resources
  //    delete image;
  return 0;
}
