#include "opencv2/highgui/highgui.hpp"
#include <czmq.h>
#include <stdio.h>
//#include <unistd.h>
#include <assert.h>

#include "sk.hpp"


#define DEFAULT_ADMIN_PORT   8964


int main(int argc, char** argv) {

    int adm_port = DEFAULT_ADMIN_PORT;
    if (argc == 3) {
        int p = atoi(argv[2]);
        if (p > 0 && strcmp("-p", argv[1]) == 0)
            adm_port = p;
    }

    char url_buf[64];
    snprintf(url_buf, 64, "tcp://localhost:%d", adm_port);
    
    zctx_t *context = zctx_new();
    void *requester = zsocket_new(context, ZMQ_REQ);
    int rc = zsocket_connect(requester, url_buf);
    zstr_send(requester, "info");
    char *rsp_str = zstr_recv(requester);
    assert(rsp_str != NULL);
    int width, height, fps, port, msg_port;
    sscanf (rsp_str, "%d %d %d %d %d", &width, &height, &fps, &port, &msg_port);
    zstr_free(&rsp_str);

#if 0
    zstr_send(requester, "store");
    rsp_str = zstr_recv(requester);
    printf("store cmd rsp_str: %s\n", rsp_str);
    zstr_free(&rsp_str);
#endif

#if 0
    zstr_send(requester, "reset");
    rsp_str = zstr_recv(requester);
    printf("reset cmd rsp_str: %s\n", rsp_str);
    zstr_free(&rsp_str);
#endif

//#if 0
	//zstr_send(requester, "track:150 5 60 250 0"); //for small track range
    zstr_send(requester, "track:150 5 150 250 0");
	//zstr_send(requester, "track:165 0 140 184 0");
	//zstr_send(requester, "track_body:165 0 140 184 2");
	//zstr_send(requester, "track_body:300 0 256 336 2");
    //zstr_send(requester, "track_body:14 0 256 336 0");
    rsp_str = zstr_recv(requester);
    assert(strcmp(rsp_str, "OK") == 0);
    zstr_free(&rsp_str);
//#endif

#if 0
	zstr_send(requester, "alarm_cntr:21,63,143,68,137,232,16,205");
	//zstr_send(requester, "alarm_cntr:390,0,360,168,390,336,600,336,570,0");
    // zstr_send(requester, "alarm:370,0,256,336");
    rsp_str = zstr_recv(requester);
    assert(strcmp(rsp_str, "OK") == 0);
    zstr_free(&rsp_str);
#endif

#if 0
    zstr_send(requester, "clean:10");
    rsp_str = zstr_recv(requester);
    assert(strcmp(rsp_str, "OK") == 0);
    zstr_free(&rsp_str);
#endif

    zsocket_destroy(context, requester);

    void *msg_sub = zsocket_new(context, ZMQ_SUB);
    snprintf(url_buf, 64, "tcp://localhost:%d", msg_port);
    rc = zsocket_connect(msg_sub, url_buf);
    assert(rc == 0);
    zsocket_set_subscribe (msg_sub, "");


    void *subscriber = zsocket_new(context, ZMQ_SUB);
    snprintf(url_buf, 64, "tcp://localhost:%d", port);
    rc = zsocket_connect(subscriber, url_buf);
    assert(rc == 0);
    zsocket_set_subscribe (subscriber, "");
    

    IplImage *image = cvCreateImageHeader(cvSize(width, height), IPL_DEPTH_8U, 3);
    size_t buf_size = image->imageSize;
    int width_step = image->widthStep;
    char *buffer = (char *)malloc(buf_size);

    cvNamedWindow("opencv", CV_WINDOW_AUTOSIZE);

    while (!zctx_interrupted) {
        char *msg = zstr_recv_nowait(msg_sub);
        if (msg != NULL) {
            if (strcmp(msg, MSG_ALARM) == 0) {
                printf("Recv: alarm\a\n");
            }
            else if (strcmp(msg, MSG_FACE) == 0) {
                printf("Welcome =============>\a\n");
            }
            else if (strcmp(msg, MSG_SHUTDOWN) == 0) {
                printf("recv: %s\n", msg);
                zctx_interrupted = 1;
            }
            else {
                printf("recv: %s\n", msg);
            }
            zstr_free(&msg);
        }


        // zeromq version
        int rec_size = zmq_recv(subscriber, buffer, buf_size, ZMQ_DONTWAIT);
        if (rec_size == -1) {
#ifndef __WINDOWS__
            if (errno != EAGAIN) printf("zmq_recv err: %s\n", zmq_strerror(errno));
#endif
            continue;
        }
        cvSetData(image, buffer, width_step);
        cvShowImage("opencv", image);

		char _key = (char)cvWaitKey(30);
		if (_key == 27 || _key == 'q' || _key == 'Q') break;
    }

    zctx_destroy(&context);
    free(buffer);

    cvReleaseImageHeader(&image);
    cvDestroyAllWindows();

    return 0;
}

