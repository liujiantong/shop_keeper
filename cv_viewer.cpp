#include "opencv2/highgui/highgui.hpp"
#include <zmq.h>
#include <string.h>
#include <stdio.h>
//#include <unistd.h>
#include <assert.h>


void *context;
void *subscriber;


static inline int fetch_frame(char *buf, size_t buf_size) {
    int rec_size = zmq_recv(subscriber, buf, buf_size, 0);
    if (rec_size == -1) {
        printf("zmq_recv err: %s\n", zmq_strerror(errno));
        return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    // init zeromq subscriber
    context = zmq_ctx_new();
    subscriber = zmq_socket(context, ZMQ_SUB);
    int rc = zmq_connect(subscriber, "tcp://localhost:8964");
    assert(rc == 0);
    rc = zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);
    assert (rc == 0);

    IplImage *image = cvCreateImageHeader(cvSize(800, 600), IPL_DEPTH_8U, 3);
    //IplImage *image = cvCreateImageHeader(cvSize(1280, 720), IPL_DEPTH_8U, 3);
    size_t buf_size = image->imageSize;
    int width_step = image->widthStep;
    char *buffer = (char *)malloc(buf_size);

    cvNamedWindow("opencv", CV_WINDOW_AUTOSIZE);

    while (1) {
        if (fetch_frame(buffer, buf_size) == -1) continue;

        cvSetData(image, buffer, width_step);
        cvShowImage("opencv", image);

        if(cvWaitKey(20) >= 0) break;
    }

    printf("clear all\n");
    zmq_close(subscriber);
    zmq_ctx_destroy(context);

    free(buffer);
    cvReleaseImageHeader(&image);
    cvDestroyAllWindows();

    return 0;
}
