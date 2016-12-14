#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/rational.h>
#include <libavutil/intreadwrite.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
}

#include "sk.hpp"
#include "wingetopt.h"

#include <czmq.h>

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc_c.h>

#include "sk_video_surv.hpp"
#include "sk_stats.hpp"



#define ZMQ_SND_HWM     1
#define ZMQ_ENDPOINT    "tcp://127.0.0.1:*"
#define ZMQ_A_ENDPOINT  "tcp://127.0.0.1:%d"
#define ZMQ_W_INPROC    "inproc://write_video"
#define ZMQ_S_INPROC    "inproc://send_video"
#define ZMQ_V_INPROC    "inproc://video_surv"
#define ZMQ_F_INPROC    "inproc://swap_video_file"

//#define ZMQ_THREAD_SIZE           5
#define ZMQ_THREAD_SIZE             6
#define ZMQ_TIMER_REPEAT_MS         20
#define ZMQ_MON_TIMER_REPEAT_MS     10000
#define DEFAULT_ADMIN_PORT          8964
#define DEFAULT_ZMQ_LINGER          30
//#define MAX_V_ROLLING_CNT         10
#define VCAP_TIMER_TIMEOUT_MS       300


//#define RTSP_URL      "rtsp://192.168.0.118:554/user=admin_password=tlJwpbo6_channel=1_stream=0.sdp?real_stream"
#define RTSP_URL      "rtsp://admin:admin@192.168.0.101:554"

#define VIDEO_SUB_DIR   "ipc_video"
#define OUT_VIDEO_FILE  "%s/%s/ipc_%s_%ld.avi"
#define OUT_VIDEO_CAP   "%s/%s/cap_%s_%ld.jpg"
#define DEFAULT_IPC_ID  "dingdianbao"


#define VSEG_TIME_S     60

#define MAX_PATH_LEN    256
#define LOG_SUB_DIR     "logs"
#define LOG_FILE_NAME   "ipc_%s.log"

FILE *log_file        = NULL;


typedef struct cb_av_ctx {
    struct SwsContext *sws_ctx;
    AVPacket *pPacket;
    AVFrame *pFrame;
    AVFrame *p_rgb_frame;
} cb_av_ctx;


cb_av_ctx *av_ctx;

AVFormatContext *g_in_fmtctx = NULL;
AVFormatContext *g_out_fmtctx = NULL;

AVStream *g_in_video_st;
AVStream *g_out_video_st;

//AVStream *g_in_audio_st;
//AVStream *g_out_audio_st;

AVOutputFormat* g_out_fmt;
AVPacket *g_packet_buf;


IplImage *p_cv_image;
IplImage *p_snd_image;
IplImage *p_tmp_image;

//CvVideoWriter *g_video_writer;
//int audio_pts = 0, audio_dts = 0;

int video_pts = 0;
int video_dts = 0;
int last_video_pts = 0;
int in_video_st_idx = -1;

uint8_t *g_frame_buf;
int g_frame_width, g_frame_height;
int g_fps = 0;
int g_out_width, g_out_height;
int g_snd_width, g_snd_height;

float g_sa_ratio = 1.0;


//int g_avi_file_num = 0;
time_t g_avi_time  = 0;
int g_frame_number = 0;
int reopening_nvideo = 0;

time_t g_vcap_time = 0;
time_t g_alrm_time = 0;


char *g_rtsp_url   = NULL;
char *g_ipc_id     = NULL;
char *g_ipc_ip     = NULL;
char *g_home_dir   = ".";
int  g_ipc_ip_n    = 0;

int g_pub_port = 0, g_pub_port_i = 0, g_adm_port = 0, g_store_video = 0;
int g_seg_frames = VSEG_TIME_S * 15;

#define MAX_FRM_WIDTH  800
//#define MAX_SND_WIDTH  640
#define MAX_SND_WIDTH  352

#define ANA_RECT(x,y,w,h)   cv::Rect(g_sa_ratio*(x), g_sa_ratio*(y), g_sa_ratio*(w), g_sa_ratio*(h))


zloop_t *zmq_loop;
zctx_t  *zmq_ctx;
void    *zmq_pub;
void    *zmq_pub_i;

void    *zmq_write_push;
void    *zmq_write_pull;
void    *zmq_send_push;
void    *zmq_send_pull;
void    *zmq_file_push;
void    *zmq_file_pull;
void    *zmq_vsurv_pull;
void    *zmq_admin_rep;

zmonitor_t *zmq_mon;
int zmon_timer_id;
time_t zmon_start_time = 0;

int     ztimer_id;
int     sk_started = 0;


sk_video_surv *video_surv = NULL;

logger_id_t LOGID  = logger_id_unknown;
logger_id_t LOGID0 = logger_id_unknown;



static int init_input_video();
static int create_video_ost(char *outfile);
static int open_output_video(char *outfile);
static int reopen_output_video(char *outfile);
static void init_output_video(AVFormatContext *in_ctx, AVFormatContext *out_ctx, AVStream *ost);

static void write_video_stream(AVPacket *pkt);
static inline int get_fps(AVStream *in_st);
static inline void optimal_size(int max_w, int w, int h, int *pw, int *ph);


static int pub_monitor_cb(zloop_t *loop, int timer_id, void *arg);
static int next_frame_cb(zloop_t *loop, int timer_id, void *arg);
static int write_frame_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg);
static int send_frame_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg);
static int new_videofile_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg);
static int video_surv_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg);
static int admin_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg);
static void close_signal_cb(int signum);

static int cap_scrnshot(zloop_t *loop, int timer_id, void *arg);


static inline void analyze_video(IplImage *img, int draw);


static inline void parse_args(int argc, char** argv);
static inline void display_usage(char **argv);
static inline void get_next_avi_name(time_t ts, char* fname);
static inline void scrnshot_name(long cap_time, char* fname);

static inline void init_logger();
static inline void close_logger();

static inline void init_zmq();
static inline void init_admin_zsock();
static void start_zmq_loop(cb_av_ctx *av_zcb);



int main(int argc, char** argv) {
    parse_args(argc, argv);

    init_logger();

    // int rc = zsys_dir_create("%s/%s", ".", VIDEO_SUB_DIR);
    int rc = zsys_dir_create("%s/%s", g_home_dir, VIDEO_SUB_DIR);
    assert(rc == 0);

    sk_stats_sqlite::create_db_if_need(g_home_dir);

    init_zmq();
    init_admin_zsock();

    if (init_input_video() != 0) {
        sk_error("init_input_video error\n");
        return -1;
    }

    if (g_store_video) {
		char fname[MAX_PATH_LEN];
		g_avi_time = time(NULL);
        get_next_avi_name(g_avi_time, fname);
        if (create_video_ost(fname) != 0) {
            return -1;
        }

        sk_stats_sqlite::insert_nvideo(fname, new_video_type);
    }


    struct SwsContext *sws_ctx = NULL;
    AVFrame *p_frame = NULL;
    AVFrame *p_rgb_frame = NULL;

    AVPacket input_pkt;
    g_packet_buf = (AVPacket *)malloc(sizeof(AVPacket));

    // Allocate video frame
    p_frame = av_frame_alloc();
    p_rgb_frame = av_frame_alloc();
    av_init_packet(&input_pkt);

    sws_ctx =
        sws_getCachedContext(
            NULL,
            g_frame_width, g_frame_height,
            g_in_video_st->codec->pix_fmt,
            g_frame_width, g_frame_height,
            PIX_FMT_BGR24,
            SWS_BICUBIC,
            NULL, NULL, NULL
        );

    //g_video_writer = cvCreateVideoWriter(
    //    "testcv.avi", CV_FOURCC('x','v','i','d'), g_fps, cvSize(g_frame_width, g_frame_height), 1);
    //sk_info("create g_video_writer\n");

    // Determine required buffer size and allocate buffer
    int numBytes = avpicture_get_size(PIX_FMT_BGR24, g_frame_width, g_frame_height);
    g_frame_buf = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
    p_cv_image = cvCreateImageHeader(cvSize(g_frame_width, g_frame_height), IPL_DEPTH_8U, 3);
    p_snd_image = cvCreateImage(cvSize(g_out_width, g_out_height), IPL_DEPTH_8U, 3);
    p_tmp_image = cvCreateImage(cvSize(g_snd_width, g_snd_height), IPL_DEPTH_8U, 3);

    av_ctx = (cb_av_ctx *)malloc(sizeof(cb_av_ctx));
    av_ctx->sws_ctx = sws_ctx;
    av_ctx->pFrame = p_frame;
    av_ctx->pPacket = &input_pkt;
    av_ctx->p_rgb_frame = p_rgb_frame;

    // init_zmq();

    video_surv = sk_video_surv::create_video_surv(none_detection, g_ipc_ip_n);
    video_surv->connect_main(zmq_ctx, ZMQ_V_INPROC);

    start_zmq_loop(av_ctx);

    return 0;
} // main


static inline void _startup_info() {
    time_t now;
    struct tm *localnow;
    char date[64] = "";
    time(&now);
    localnow = localtime(&now);
    strftime(date, 64, "%Y-%m-%d %H:%M:%S", localnow);

    logger(LOGID0, LOGGER_INFO, "\n\n");
    logger(LOGID0, LOGGER_INFO, "#===========================================================================================================\n");
    logger(LOGID0, LOGGER_INFO, "#\tSK LooooooooooooooooooooooooooooP started\n");
    logger(LOGID0, LOGGER_INFO, "#\tConnnected to IPC: %s\n", g_rtsp_url);
    logger(LOGID0, LOGGER_INFO, "#\tGot video info: %dx%d@%d\n", g_frame_width, g_frame_height, g_fps);
    logger(LOGID0, LOGGER_INFO, "#\tListening to Admin Port: %d\n", g_adm_port);
    logger(LOGID0, LOGGER_INFO, "#\tPublishing video data to Port: %d\n", g_pub_port);
    logger(LOGID0, LOGGER_INFO, "#\tPublishing message to Port: %d\n", g_pub_port_i);
    logger(LOGID0, LOGGER_INFO, "#\tStart time: %s\n", date);
    logger(LOGID0, LOGGER_INFO, "#===========================================================================================================\n");
    logger(LOGID0, LOGGER_INFO, "\n\n");

    logger_output_flush();

    logger_id_release(LOGID0);
}


static inline void init_zmq() {
    zsys_handler_set(close_signal_cb);
    zmq_ctx = zctx_new();
    zctx_set_iothreads(zmq_ctx, ZMQ_THREAD_SIZE);
    zctx_set_sndhwm(zmq_ctx, ZMQ_SND_HWM);
    //zctx_set_linger(zmq_ctx, DEFAULT_ZMQ_LINGER);
}


static inline void init_admin_zsock() {
    char url_buf[128];
    snprintf(url_buf, 128, ZMQ_A_ENDPOINT, g_adm_port);
    zmq_admin_rep = zsocket_new(zmq_ctx, ZMQ_REP);
    int rc = zsocket_bind(zmq_admin_rep, url_buf);
    assert(rc > 0);
}


static void start_zmq_loop(cb_av_ctx *av_zcb) {
    zmq_loop = zloop_new();

    // init_admin_zsock();

    zmq_pub_i = zsocket_new(zmq_ctx, ZMQ_PUB);
    zsocket_set_sndhwm(zmq_pub_i, 128);
    zmq_mon = zmonitor_new(zmq_ctx, zmq_pub_i, ZMQ_EVENT_ACCEPTED|ZMQ_EVENT_DISCONNECTED|ZMQ_EVENT_CLOSED);
    // zmonitor_set_verbose(zmq_mon, true);
    g_pub_port_i = zsocket_bind(zmq_pub_i, ZMQ_ENDPOINT);
    assert(g_pub_port_i > 0);

    zmq_pub = zsocket_new(zmq_ctx, ZMQ_PUB);
    zsocket_set_sndhwm(zmq_pub, ZMQ_SND_HWM);
    g_pub_port = zsocket_bind(zmq_pub, ZMQ_ENDPOINT);
    assert(g_pub_port > 0);

    zmq_write_push = zsocket_new(zmq_ctx, ZMQ_PUSH);
    int rc = zsocket_bind(zmq_write_push, ZMQ_W_INPROC);
    assert(rc == 0);
    zmq_write_pull = zsocket_new(zmq_ctx, ZMQ_PULL);
    rc = zsocket_connect(zmq_write_pull, ZMQ_W_INPROC);
    assert(rc == 0);

    zmq_send_push = zsocket_new(zmq_ctx, ZMQ_PUSH);
    rc = zsocket_bind(zmq_send_push, ZMQ_S_INPROC);
    assert(rc == 0);
    zmq_send_pull = zsocket_new(zmq_ctx, ZMQ_PULL);
    rc = zsocket_connect(zmq_send_pull, ZMQ_S_INPROC);
    assert(rc == 0);

    zmq_file_push = zsocket_new(zmq_ctx, ZMQ_PUSH);
    rc = zsocket_bind(zmq_file_push, ZMQ_F_INPROC);
    assert(rc == 0);
    zmq_file_pull = zsocket_new(zmq_ctx, ZMQ_PULL);
    rc = zsocket_connect(zmq_file_pull, ZMQ_F_INPROC);
    assert(rc == 0);

    zmq_vsurv_pull = zsocket_new(zmq_ctx, ZMQ_PULL);
    rc = zsocket_bind(zmq_vsurv_pull, ZMQ_V_INPROC);
    assert(rc == 0);

    _startup_info();

    ztimer_id = zloop_timer(zmq_loop, ZMQ_TIMER_REPEAT_MS, 0, next_frame_cb, (void *)av_zcb);
    assert(ztimer_id > -1);

    zmon_start_time = time(NULL);
    zmon_timer_id = zloop_timer(zmq_loop, ZMQ_MON_TIMER_REPEAT_MS, 0, pub_monitor_cb, NULL);
    assert(zmon_timer_id > -1);

    zmq_pollitem_t w_poller = { zmq_write_pull, 0, ZMQ_POLLIN };
    zmq_pollitem_t s_poller = { zmq_send_pull, 0, ZMQ_POLLIN };
    zmq_pollitem_t f_poller = { zmq_file_pull, 0, ZMQ_POLLIN };
    zmq_pollitem_t v_poller = { zmq_vsurv_pull, 0, ZMQ_POLLIN };
    zmq_pollitem_t a_poller = { zmq_admin_rep, 0, ZMQ_POLLIN };

    zloop_poller(zmq_loop, &w_poller, write_frame_cb, g_packet_buf);
    zloop_poller(zmq_loop, &s_poller, send_frame_cb, p_snd_image);
    zloop_poller(zmq_loop, &f_poller, new_videofile_cb, NULL);
    zloop_poller(zmq_loop, &v_poller, video_surv_cb, NULL);
    zloop_poller(zmq_loop, &a_poller, admin_cb, NULL);

    sk_started = 1;

    zloop_start(zmq_loop);
    zloop_destroy(&zmq_loop);
} // start_zmq_loop


static int init_input_video() {
    avcodec_register_all();
    av_register_all();
    avformat_network_init();

    // input device from osx camera
    //avdevice_register_all();
    //AVInputFormat *ifmt = av_find_input_format("avfoundation");

    g_in_fmtctx = avformat_alloc_context();
    in_video_st_idx = -1;

    //open rtsp
    AVDictionary *opts = NULL;
    //av_dict_set(&opts, "threads", "auto", 0);
    av_dict_set(&opts, "threads", "2", 0);
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);

    //if (avformat_open_input(&g_in_fmtctx, "0", ifmt, &opts) != 0) {
    if (avformat_open_input(&g_in_fmtctx, g_rtsp_url, NULL, &opts) != 0) {
        av_dict_free(&opts);
        return -1;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(g_in_fmtctx, NULL) < 0) {
        return -1;
    }

    //search video stream
    #if 0
    for (int i =0; i<g_in_fmtctx->nb_streams; i++) {
		if (g_in_fmtctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            in_video_st_idx = i;
    }
    #endif

	in_video_st_idx = av_find_best_stream(g_in_fmtctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (in_video_st_idx < 0) {
        return -1;
    }

    g_in_video_st = g_in_fmtctx->streams[in_video_st_idx];
    g_frame_width = g_in_video_st->codec->width;
    g_frame_height = g_in_video_st->codec->height;

    optimal_size(MAX_FRM_WIDTH, g_frame_width, g_frame_height, &g_out_width, &g_out_height);
    optimal_size(MAX_SND_WIDTH, g_frame_width, g_frame_height, &g_snd_width, &g_snd_height);
	g_sa_ratio = (float)g_out_width / g_snd_width;
	// sk_debug("g_sa_ratio: %f\n", g_sa_ratio);

    if (g_fps == 0) {
        g_fps = get_fps(g_in_video_st);
        g_seg_frames = VSEG_TIME_S * g_fps;
    }

    // g_out_fmt = av_guess_format(NULL, outfile, NULL);
    // g_out_fmtctx = avformat_alloc_context();
    // g_out_fmtctx->oformat = g_out_fmt;

    AVCodec *p_codec = avcodec_find_decoder(g_in_video_st->codec->codec_id);
    if (p_codec == NULL) {
        sk_error("Unsupported codec!\n");
        return -1; // Codec not found
    }

    return avcodec_open2(g_in_video_st->codec, p_codec, NULL/*&opts*/);
} // init_input_video


static int create_video_ost(char *fname) {
    g_out_fmt = av_guess_format(NULL, fname, NULL);
    g_out_fmtctx = avformat_alloc_context();
    g_out_fmtctx->oformat = g_out_fmt;

    if (open_output_video(fname) < 0) {
        sk_error("Could NOT open video output file: %s\n", fname);
        return -1;
    }

    init_output_video(g_in_fmtctx, g_out_fmtctx, g_out_video_st);
    return 0;
} // create_video_ost


static int reopen_output_video(char *outfile) {
    //sk_info("reopen_output_video ====>\n");

    AVFormatContext *tmp_fmt_ctx = avformat_alloc_context();
    tmp_fmt_ctx->oformat = g_out_fmt;

    if (avio_open2(&tmp_fmt_ctx->pb, outfile, AVIO_FLAG_WRITE, NULL, NULL) < 0) {
        avio_close(tmp_fmt_ctx->pb);
        avformat_free_context(tmp_fmt_ctx);
        sk_error("reopen_output_video: %s failed\n", outfile);
        return -1;
    }

    AVStream *tmp_video_st = avformat_new_stream(tmp_fmt_ctx, g_in_video_st->codec->codec);
    if (tmp_video_st == NULL) {
        avio_close(tmp_fmt_ctx->pb);
        avformat_free_context(tmp_fmt_ctx);
        avcodec_close(tmp_video_st->codec);
        sk_info("Failed to Allocate Output Video Stream\n");
        return -1;
    }

    avcodec_copy_context(tmp_video_st->codec, g_in_video_st->codec);
    avcodec_flush_buffers(g_in_video_st->codec);
    //re-init output video
    init_output_video(g_in_fmtctx, tmp_fmt_ctx, tmp_video_st);

    av_write_trailer(g_out_fmtctx);

    avcodec_close(g_out_video_st->codec);
    avio_close(g_out_fmtctx->pb);
    //ff_free_stream(g_out_fmtctx, g_out_video_st);
    avformat_free_context(g_out_fmtctx);
    sk_info("reopen_output_video done ====>\n");

    video_pts = 0;
    video_dts = 0;
    last_video_pts = 0;
	g_frame_number = 0;

    g_out_video_st = tmp_video_st;
    g_out_fmtctx = tmp_fmt_ctx;

    return 0;
} // reopen_output_video


static int open_output_video(char *outfile) {
    av_dump_format(g_out_fmtctx, 0, outfile, 1);

    if (avio_open2(&g_out_fmtctx->pb, outfile, AVIO_FLAG_WRITE, NULL, NULL) < 0) {
        sk_error("Failed to open output format context\n");
        return -1;
    }

    g_out_video_st = avformat_new_stream(g_out_fmtctx, g_in_video_st->codec->codec);
    if (g_out_video_st == NULL) {
        sk_error("Failed to Allocate Output Video Stream\n");
        return -1;
    }

    if (avcodec_copy_context(g_out_video_st->codec, g_in_video_st->codec) != 0) {
        sk_error("Failed to Copy video context\n");
        return -1;
    }

    return 0;
} // open_output_video


// initialize output video codec
static void init_output_video(AVFormatContext *in_ctx, AVFormatContext *out_ctx, AVStream *out_video_st) {
    //sk_info("Init_video_codec...\n");

    // if (g_fps == 0) {
    //     g_fps = get_fps(g_in_video_st);
    //     g_seg_frames = VSEG_TIME_S * g_fps;
    // }

    // how to setting video stream parameter?
    out_video_st->sample_aspect_ratio.den = g_in_video_st->codec->sample_aspect_ratio.den;
    out_video_st->sample_aspect_ratio.num = g_in_video_st->codec->sample_aspect_ratio.num;
    out_video_st->codec->codec_id         = g_in_video_st->codec->codec_id;
    out_video_st->codec->time_base.num    = 1;
    out_video_st->codec->time_base.den    = g_fps * (g_in_video_st->codec->ticks_per_frame);
    out_video_st->time_base.num           = 1;
    //out_video_st->time_base.den         = 1000;
    out_video_st->time_base.den           = g_fps;
    out_video_st->r_frame_rate.num        = g_fps;
    out_video_st->r_frame_rate.den        = 1;
    out_video_st->avg_frame_rate.den      = 1;
    out_video_st->avg_frame_rate.num      = g_fps;
    out_video_st->codec->width            = g_frame_width;
    out_video_st->codec->height           = g_frame_height;
    out_video_st->codec->flags            |= CODEC_FLAG_GLOBAL_HEADER;

    avformat_write_header(out_ctx, NULL);
} // init_output_video


// write video stream
static void write_video_stream(AVPacket *pkt) {
    AVPacket *av_pkt = pkt;

    if( !av_pkt || sizeof(*av_pkt) == 0 ) {
        sk_error("av_pkt error: don't write to stream\n");
        return;
    }

    av_rescale_q(av_pkt->pts, g_in_video_st->time_base, g_in_video_st->codec->time_base);
    av_rescale_q(av_pkt->dts, g_in_video_st->time_base, g_in_video_st->codec->time_base);
    //av_pkt->pts *= g_in_video_st->codec->ticks_per_frame;
    //av_pkt->dts *= g_in_video_st->codec->ticks_per_frame;

    AVPacket outpkt;
    av_init_packet(&outpkt);

    if (av_pkt->pts != AV_NOPTS_VALUE) {
        if (last_video_pts == video_pts) {
            video_pts++;
            last_video_pts = video_pts;
        }
        outpkt.pts = video_pts;
    }
    else {
        outpkt.pts = AV_NOPTS_VALUE;
    }

    if (av_pkt->dts == AV_NOPTS_VALUE)
        outpkt.dts = AV_NOPTS_VALUE;
    else
        outpkt.dts = video_pts;

    outpkt.data = av_pkt->data;
    outpkt.size = av_pkt->size;
    outpkt.stream_index = av_pkt->stream_index;
    outpkt.flags |= AV_PKT_FLAG_KEY;
    last_video_pts = video_pts;

    int ret = av_interleaved_write_frame(g_out_fmtctx, &outpkt);
    if (ret < 0) {
#ifdef __WINDOWS__
        sk_error("Failed write to output stream: %d\n", (ret));
#elif defined __LINUX__
        sk_error("Failed write to output stream: %d\n", (ret));
#else
        sk_error("Failed write to output stream: %s\n", av_err2str(ret));
#endif
    }
    else {
        g_out_video_st->codec->frame_number++;
    }

    av_free_packet(&outpkt);
} // write_video_stream


#define DEFAULT_FPS   15.0
#define EPS_ZERO      0.000025
static int get_fps(AVStream *in_st) {
    double fps = av_q2d(in_st->avg_frame_rate);
    if (fps < EPS_ZERO) fps = av_q2d(in_st->r_frame_rate);
	if (fps < EPS_ZERO) fps = 1.0 / av_q2d(in_st->codec->time_base);
	if (fps < EPS_ZERO || int(fps) == -2147483648) fps = DEFAULT_FPS;
    return (int)round(fps);
} // get_fps


// monitor video publish connected event
static int pub_monitor_cb(zloop_t *loop, int timer_id, void *arg) {
    zmsg_t *msg = zmonitor_recv(zmq_mon);

    if (msg == NULL) {
        if (zmon_start_time > 0 && (time(NULL) - zmon_start_time) > 60) {
            // stop application
            sk_info("Oh, no client connected to video data Q. Quit...\n");
            zloop_timer_end(zmq_loop, zmon_timer_id);
            zmonitor_destroy(&zmq_mon);
            close_signal_cb(SIGINT);
        }
    }
    else {
        char *string = zmsg_popstr(msg);
        int event = atoi(string);
        zstr_free(&string);
        zmsg_destroy(&msg);

        if (event == ZMQ_EVENT_ACCEPTED) {
            sk_info("Well, client connected.\n");
            zmon_start_time = 0;
        }
        else { //if (event == ZMQ_EVENT_DISCONNECTED || event == ZMQ_EVENT_CLOSED) {
            sk_debug("Well, client disconnected. Reset zmon_start_time.\n");
            zmon_start_time = time(NULL);
        }
    }

    return 0;
} // pub_monitor_cb


// heartbeat function
static int next_frame_cb(zloop_t *loop, int timer_id, void *arg) {
    if (zctx_interrupted == 1) return 0;

    cb_av_ctx *ctx = (cb_av_ctx *)arg;
    AVPacket* inpkt = ctx->pPacket;

    if (av_read_frame(g_in_fmtctx, inpkt) < 0) {
        //av_free_packet(inpkt);  //FIXME
        return 0;
    }

    if (inpkt->stream_index != in_video_st_idx) {
		sk_debug("inpkt->stream_index != in_video_st_idx\n");
        av_free_packet(inpkt);
        //return -1;
		return 0;
    }

    int got_frame = 0;
    avcodec_decode_video2(g_in_video_st->codec, ctx->pFrame, &got_frame, inpkt);
    //if (avcodec_decode_video2(g_in_video_st->codec, ctx->pFrame, &got_frame, inpkt) < 0) {}

    //write_video_stream(ctx->pPacket);
    av_copy_packet(g_packet_buf, inpkt);
    zsocket_signal(zmq_write_push);

    // Did we get a video frame?
    if (got_frame) {
        // avcodec_flush_buffers(g_in_video_st->codec); //FIXME
        // Assign appropriate parts of buffer to image planes in p_rgb_frame
        // Note that p_rgb_frame is an AVFrame, but AVFrame is a superset of AVPicture
        avpicture_fill((AVPicture *)ctx->p_rgb_frame, g_frame_buf, PIX_FMT_BGR24,
                        g_frame_width, g_frame_height);

        sws_scale(
            ctx->sws_ctx,
            (uint8_t const * const *)ctx->pFrame->data,
            ctx->pFrame->linesize,
            0, g_frame_height,
            ctx->p_rgb_frame->data,
            ctx->p_rgb_frame->linesize
        );

        //fill opencv image
        cvSetData(p_cv_image, ctx->p_rgb_frame->data[0], ctx->p_rgb_frame->linesize[0]);
        //assert(p_cv_image->imageData != NULL);
        cvResize(p_cv_image, p_snd_image, CV_INTER_LINEAR);

        zsocket_signal(zmq_send_push);

        analyze_video(p_snd_image, 1);

        //Swap output video file
        //char ptype = av_get_picture_type_char(ctx->pFrame->pict_type);
        //if (g_frame_number++ > AVI_FRAME_N && ptype == 'I') {
        if (g_frame_number++ > g_seg_frames) {
            if (g_store_video && !reopening_nvideo) {
				reopening_nvideo = 1;
				char fname[MAX_PATH_LEN], nfname[MAX_PATH_LEN];
                get_next_avi_name(g_avi_time, fname);
				//get_next_avi_name(++g_avi_file_num, nfname);
				g_avi_time = time(NULL);
				get_next_avi_name(g_avi_time, nfname);

                zstr_send(zmq_file_push, nfname);
                zstr_sendf(zmq_pub_i, "%s:%s", MSG_NVIDEO, fname);

				sk_debug("video file: %s written down \n", fname);
                sk_stats_sqlite::insert_nvideo(fname, end_video_type);
            }
        }
    }
    av_free_packet(inpkt);

    return 0;
} //next_frame_cb


// FIXME: cause Segmentation fault
static void close_signal_cb(int signum) {
    zctx_interrupted = 1;
    zsys_interrupted = 1;

    sk_info("\nSIGINT Received, Stop SK LoooooooooooooooooooP =====>\n\n\n");

    //notify all clients
    zstr_send(zmq_pub_i, MSG_SHUTDOWN);
    zloop_timer_end(zmq_loop, ztimer_id);
    //zloop_destroy(&zmq_loop);

    zclock_sleep(500);

    if (video_surv != NULL) {
        video_surv->release();
        video_surv = NULL;
    }

    av_free(av_ctx->pFrame);
    av_free(av_ctx->p_rgb_frame);
    av_free_packet(av_ctx->pPacket);
    sws_freeContext(av_ctx->sws_ctx);
    free(av_ctx);

    if (g_store_video) {
        av_write_trailer(g_out_fmtctx);
        avio_close(g_out_fmtctx->pb);
        avformat_free_context(g_out_fmtctx);

		char fname[MAX_PATH_LEN];
        get_next_avi_name(g_avi_time, fname);
        sk_stats_sqlite::insert_nvideo(fname, end_video_type);
    }

    if (g_ipc_ip != NULL) free(g_ipc_ip);

    avio_close(g_in_fmtctx->pb);
    avformat_free_context(g_in_fmtctx);

    av_free(g_frame_buf);
    free(g_packet_buf);

#ifndef __WINDOWS__
    cvReleaseImage(&p_cv_image);
    cvReleaseImage(&p_snd_image);
    cvReleaseImage(&p_tmp_image);
#endif

    //sk_debug("Destroy ZMQ Finally =====>\n");
    zctx_destroy(&zmq_ctx);
    close_logger();
} // close_signal_cb


#define CMD_ARG_DELIM   " ,\t\n\r"
static int admin_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg) {
    char *cmd_arg0 = zstr_recv(poller->socket);
	if (cmd_arg0 == NULL || strlen(cmd_arg0) < 3) {
		zstr_send(poller->socket, "ERR");
		if (cmd_arg0 != NULL) zstr_free(&cmd_arg0);
		return 0;
	}

    sk_info("Recv cmd: %s\n", cmd_arg0);

    if (!sk_started) {
		sk_info("Starting, ignore all cmds\n");
        zstr_send(poller->socket, "Starting");
        zstr_free(&cmd_arg0);
        return 0;
    }


    int rc = 0;
    char *cmd = strtok(cmd_arg0, ":");

    if (strcmp(cmd, CMD_INFO) == 0) {
        zstr_sendf(poller->socket, "%d %d %d %d %d",
            g_snd_width, g_snd_height, (int)(g_fps+0.1), g_pub_port, g_pub_port_i);
        sk_info("cmd %s: video_size: (%d,%d), video_port: %d, msg_port: %d\n",
            CMD_INFO, g_snd_width, g_snd_height, g_pub_port, g_pub_port_i);
    }
    else if (strcmp(cmd, CMD_STORE) == 0) {
        if (g_store_video == 0) {
			char fname[MAX_PATH_LEN];
			g_avi_time = time(NULL);
            get_next_avi_name(g_avi_time, fname);
            if (create_video_ost(fname) == 0) {
                g_store_video = 1;
                zstr_send(poller->socket, "OK");
                sk_info("create_video_ost OK\n");
                sk_stats_sqlite::insert_nvideo(fname, new_video_type);
            }
            else {
                zstr_send(poller->socket, "ERR");
                sk_error("create_video_ost error\n");
            }
        }
        else {
            sk_info("video output stream created already!\n");
            zstr_send(poller->socket, "CRT");
        }
    }
    else if (strcmp(cmd, CMD_STATS) == 0) {
        //sk_stats_sqlite *stats = new sk_stats_sqlite;
        int count = sk_stats_sqlite::count_blobs_today();

        char str[32];
        snprintf(str, 32, "%d", count);
        zstr_send(poller->socket, str);
        sk_info("cmd %s: count=%d\n", CMD_STATS, count);
    }
	else if (strcmp(cmd, CMD_ALARM_CNTR) == 0) {
		//TODO:
		if (video_surv == NULL) {
			zstr_send(poller->socket, "ERR");
			rc = -1;
			sk_error("cmd error: video_surv is NULL\n");
		}
		else {
			char *x, *y;
			std::vector<cv::Point> v;
			while (v.size() < 32 &&  (x = strtok(NULL, CMD_ARG_DELIM)) != NULL) {
				if ((y = strtok(NULL, CMD_ARG_DELIM)) != NULL) {
					v.push_back(cv::Point(atoi(x)*g_sa_ratio, atoi(y)*g_sa_ratio));
				}
				else break;
			}

			video_surv->set_action(motion_detection, v);
			zstr_send(poller->socket, "OK");
		}
	}
    else if (strcmp(cmd, CMD_ALARM) == 0) {
        if (video_surv == NULL) {
            zstr_send(poller->socket, "ERR");
            rc = -1;
            sk_error("cmd error: video_surv is NULL\n");
        }
        else {
            char *x = strtok(NULL, CMD_ARG_DELIM); char *y = strtok(NULL, CMD_ARG_DELIM);
            char *w = strtok(NULL, CMD_ARG_DELIM); char *h = strtok(NULL, CMD_ARG_DELIM);

			if (!(x && y && w && h)) {
				zstr_free(&cmd_arg0);
				zstr_send(poller->socket, "ERR");
				return -1;
			}

			cv::Rect r = ANA_RECT(atoi(x), atoi(y), atoi(w), atoi(h));
			if ((r.x + r.width) > g_out_width || (r.y + r.height) > g_out_height) {
				zstr_send(poller->socket, "ERR");
				rc = -1;
				sk_error("cmd track error: (%d,%d,%d,%d)\n", r.x, r.y, r.width, r.height);
            }
            else {
				video_surv->set_action(motion_detection, r);
				zstr_send(poller->socket, "OK");
				sk_info("cmd %s: focus_rect(%d,%d,%d,%d)\n", CMD_ALARM, r.x, r.y, r.width, r.height);
            }
        }
    }
	else if (strcmp(cmd, CMD_TRACK) == 0 || strcmp(cmd, CMD_TRACK_FACE) == 0 || strcmp(cmd, CMD_TRACK_BODY) == 0) {
        if (video_surv != NULL) {
            char *x = strtok(NULL, CMD_ARG_DELIM); char *y = strtok(NULL, CMD_ARG_DELIM);
            char *w = strtok(NULL, CMD_ARG_DELIM); char *h = strtok(NULL, CMD_ARG_DELIM);
			char *dir = strtok(NULL, CMD_ARG_DELIM);
			if (!(x && y && w && h)) {
				zstr_send(poller->socket, "ERR");
				zstr_free(&cmd_arg0);
				return -1;
			}

            cv::Rect r = ANA_RECT(atoi(x), atoi(y), atoi(w), atoi(h));
			if ((r.x + r.width) > g_out_width || (r.y + r.height) > g_out_height) {
				zstr_send(poller->socket, "ERR");
				sk_error("cmd %s error: (%d,%d,%d,%d)\n", cmd, r.x, r.y, r.width, r.height);
			}
			else {
				int direct = (dir != NULL) ? atoi(dir) : -1;
				int det_cmd = face_detection;
				if (strcmp(cmd, CMD_TRACK) == 0) det_cmd = object_detection;
				else if (strcmp(cmd, CMD_TRACK_BODY) == 0) det_cmd = body_detection;

				video_surv->set_action(det_cmd, r, direct);
				zstr_send(poller->socket, "OK");
				sk_info("cmd %s: focus_rect(%d,%d,%d,%d)\n", cmd, r.x, r.y, r.width, r.height);
			}
        }
        else {
            zstr_send(poller->socket, "ERR");
            rc = -1;
            sk_error("cmd error: video_surv is NULL\n");
        }
    }
    else if (strcmp(cmd, CMD_RESET) == 0) {
		char *arg = strtok(NULL, CMD_ARG_DELIM);
		if (arg == NULL) {
			video_surv->reset_actions(0);
		}
		else if (strcmp(arg, CMD_ALARM) == 0) {
			video_surv->reset_actions(motion_detection);
		}
		else if (strcmp(arg, CMD_TRACK) == 0) {
			video_surv->reset_actions(face_detection);
		}
        zstr_send(poller->socket, "OK");
    }
    else if (strcmp(cmd, CMD_CLEAN) == 0) {
        char *xday = strtok(NULL, CMD_ARG_DELIM);
        if (xday == NULL) xday = "30";

        zstr_send(poller->socket, "OK");
        sk_stats_sqlite::clean_history(atoi(xday));
    }
    else if (strcmp(cmd, CMD_SHUTDOWN) == 0) {
        zstr_send(poller->socket, "OK");
        close_signal_cb(SIGINT);
    }
    else {
        sk_warning("cmd: %s not support\n", cmd);
        zstr_send(poller->socket, "Unsupported cmd");
    }
    zstr_free(&cmd_arg0);

	logger_output_flush();
    return rc;
} // admin_cb


#define MSG_INTERVAL_S  8
static int video_surv_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg) {
    char *msg = zstr_recv(poller->socket);
    //TODO: process message from video surveilance and send it to gui
	if (strcmp(msg, MSG_ALARM) == 0) {
		time_t now = time(NULL);
		if ((now - g_alrm_time) > MSG_INTERVAL_S) {
			zstr_send(zmq_pub_i, MSG_ALARM);
			g_alrm_time = now;
		}
	}
    else if (strcmp(msg, MSG_OBJ) == 0 || strcmp(msg, MSG_BODY) == 0 || strcmp(msg, MSG_FACE) == 0) {
		zstr_send(zmq_pub_i, MSG_FACE);
        zloop_timer(zmq_loop, VCAP_TIMER_TIMEOUT_MS, 1, cap_scrnshot, (void *)msg);
    }
	else {
		zstr_send(zmq_pub_i, msg);
	}

	sk_warning("video_surv_cb recv: %s\n", msg);
    zstr_free(&msg);
    return 0;
} // video_surv_cb


static int new_videofile_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg) {
    char *fname = zstr_recv(poller->socket);
    int rc = reopen_output_video(fname);

    sk_stats_sqlite::insert_nvideo(fname, new_video_type);
    zstr_free(&fname);

    #if 0
    if (rc < 0) {
        g_avi_file_num--;
        return -1;
    }
    #endif

    //g_frame_number = 0;
	reopening_nvideo = 0;
    return 0;
} // new_videofile_cb


static int write_frame_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg) {
    if (zctx_interrupted == 1) return 0;

    int rc = zsocket_wait(poller->socket);
    assert(rc == 0);

	AVPacket *pkt = (AVPacket *)arg;
	if (!g_store_video) {
		av_free_packet(pkt);
		return 0;
	}

    write_video_stream(pkt);
    av_free_packet(pkt);
    return 0;
} // write_frame_cb


static int send_frame_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg) {
    if (zctx_interrupted == 1) return 0;

    int rc = zsocket_wait(poller->socket);
    assert(rc == 0);

    IplImage *img = (IplImage *)arg;
    cvResize(img, p_tmp_image, CV_INTER_LINEAR);
	assert(p_tmp_image->width == MAX_SND_WIDTH);

    int size = zmq_send(zmq_pub, p_tmp_image->imageData, p_tmp_image->imageSize, 0);
    if (size < 0) {
        sk_error("Send frame failed: %s\n", zmq_strerror(errno));
        return -1;
    }
    return 0;
} // send_frame_cb


static int cap_scrnshot(zloop_t *loop, int timer_id, void *arg) {
    time_t now = time(NULL);
	if ((now - g_vcap_time) > MSG_INTERVAL_S) { // more than 8 sec
        g_vcap_time = now;

        int params[] = { 65 };
		char fname[MAX_PATH_LEN];
        scrnshot_name((long)g_vcap_time, fname);
        cvSaveImage(fname, p_cv_image, params);

        zstr_sendf(zmq_pub_i, "%s:%s", MSG_VCAPTURE, fname);
    }

    zloop_timer_end(loop, timer_id);
    return 0;
} // cap_scrnshot


static inline void get_next_avi_name(time_t ts, char* fname) {
    //snprintf(fname, 64, OUT_VIDEO_FILE, VIDEO_SUB_DIR, g_ipc_id, long(ts));
	snprintf(fname, MAX_PATH_LEN, OUT_VIDEO_FILE, g_home_dir, VIDEO_SUB_DIR, g_ipc_id, long(ts));
	//snprintf(fname, 64, OUT_VIDEO_FILE, VIDEO_SUB_DIR, g_ipc_ip, (fnum % MAX_V_ROLLING_CNT));
	//snprintf(fname, 64, OUT_VIDEO_FILE, VIDEO_SUB_DIR, g_ipc_ip, fnum);
}


static inline void scrnshot_name(long cap_time, char* fname) {
	//snprintf(fname, 64, OUT_VIDEO_CAP, VIDEO_SUB_DIR, g_ipc_id, cap_time);
    snprintf(fname, MAX_PATH_LEN, OUT_VIDEO_CAP, g_home_dir, VIDEO_SUB_DIR, g_ipc_id, cap_time);
	//snprintf(fname, 64, OUT_VIDEO_CAP, VIDEO_SUB_DIR, g_ipc_ip_n, cap_time);
}


static inline void optimal_size(int max_width, int w, int h, int *pw, int *ph) {
    if (w < max_width) {
        *pw = w; *ph = h;
    }
    else {
        float r = max_width/(float)w;
        *pw = max_width; *ph = (int)(r * h);
    }
}


static inline void parse_args(int argc, char** argv) {
    g_adm_port = DEFAULT_ADMIN_PORT;
    g_rtsp_url = RTSP_URL; //this line to be removed

    int oc;
    while ((oc = getopt(argc, argv, ":p:r:i:d:s")) != -1) {
        switch (oc) {
        case 'p':
            g_adm_port = atoi(optarg);
            break;
        case 'r':
            g_rtsp_url = optarg;
            break;
		case 'i':
			g_ipc_id = optarg;
			break;
        case 'd':
            g_home_dir = optarg;
            break;
        case 's':
            g_store_video = 1;
            break;
        case ':':
            printf("%s: option `-%c' requires an argument\n", argv[0], optopt);
            if (optopt == 'r') {
                display_usage(argv);
                exit(1);
            }
            break;
        case '?':
        default:
            printf("%s: option `-%c' is invalid: ignored\n", argv[0], optopt);
            break;
        }
    }

    if (g_rtsp_url == NULL) {
        display_usage(argv);
        exit(1);
    }

	if (g_ipc_id == NULL) {
		g_ipc_id = DEFAULT_IPC_ID;
	}


    #define RTSP_PATTERN  "(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)"

    zrex_t *rex = zrex_new(RTSP_PATTERN);
    assert(rex && zrex_valid(rex));

    bool matched = zrex_matches(rex, g_rtsp_url);
    if (matched) {
        int rc = zrex_hits(rex);
        //assert(rc == 5);

        g_ipc_ip = (char *)malloc(32);
        if (rc == 5) {
            g_ipc_ip_n = atoi(zrex_hit(rex, 4));
			snprintf(g_ipc_ip, 32, "%03d%03d%03d%03d", \
				atoi(zrex_hit(rex, 1)), atoi(zrex_hit(rex, 2)), atoi(zrex_hit(rex, 3)), g_ipc_ip_n);
        }
        else {
            srand((unsigned)time(NULL));
            g_ipc_ip_n = rand() % INIT_BLOB_ID;
            snprintf(g_ipc_ip, 32, "%06d", g_ipc_ip_n);
        }
    }
    //for (int i=0; i<rc; i++) printf("hit %d: %s\n", i, zrex_hit(rex, i));
    zrex_destroy(&rex);
} // parse_args


static inline void display_usage(char **argv) {
    printf("Usage: %s -r rtsp_url -p admin_port(default value: 8964) -s(store video)\n", argv[0]);
}


static inline void analyze_video(IplImage *img, int draw) {
    if (video_surv == NULL || zctx_interrupted == 1) return;

    video_surv->process(img);
    if (draw) {
        video_surv->draw_detected(img);
    }
} // analyze_video


static inline void init_logger() {
    logger_init();

#ifdef SK_RELEASE_VERSION
    // int rc = zsys_dir_create("%s/%s", ".", LOG_SUB_DIR);
    int rc = zsys_dir_create("%s/%s", g_home_dir, LOG_SUB_DIR);
    assert(rc == 0);

    char fname_buf[MAX_PATH_LEN];
    snprintf(fname_buf, MAX_PATH_LEN, "%s/" LOG_SUB_DIR "/" LOG_FILE_NAME, g_home_dir, g_ipc_ip);
    log_file = fopen(fname_buf, "w");
    // log_file = fopen(fname_buf, "a");
    logger_output_register(log_file);
    logger_output_level_set(log_file, LOGGER_INFO);
#else
    logger_output_register(stdout);
    logger_output_level_set(stdout, LOGGER_DEBUG);
#endif

    LOGID = logger_id_request("SK_LOG_ID");
    logger_id_enable(LOGID);
#if defined(SK_RELEASE_VERSION) && !defined(LOGGER_FORCE_FLUSH)
	//logger_id_level_set(LOGID, LOGGER_DEBUG);
	logger_id_level_set(LOGID, LOGGER_INFO);
#else
    logger_id_level_set(LOGID, LOGGER_DEBUG);
#endif

#ifdef SK_RELEASE_VERSION
	logger_id_prefix_set(LOGID, (LOGGER_PFX_DATE | LOGGER_PFX_LEVEL));
#else
	logger_id_prefix_set(LOGID, (LOGGER_PFX_DATE | LOGGER_PFX_LEVEL | LOGGER_PFX_FILE | LOGGER_PFX_LINE));
#endif

#ifndef __WINDOWS__
    logger_color_prefix_enable();
    logger_color_message_enable();
    logger_id_color_console_set(LOGID, LOGGER_FG_GREEN, LOGGER_BG_BLACK, LOGGER_ATTR_BRIGHT | LOGGER_ATTR_UNDERLINE);
#endif

    LOGID0 = logger_id_request("SK_LOG0_ID");
    logger_id_enable(LOGID0);
    logger_id_level_set(LOGID0, LOGGER_INFO);
    logger_id_prefix_set(LOGID0, LOGGER_PFX_EMPTY);
    //logger_id_color_console_set(LOGID0, LOGGER_FG_GREEN, LOGGER_BG_BLACK, LOGGER_ATTR_BRIGHT);

} // init_logger


static inline void close_logger() {
	logger_output_flush();

    logger_id_release(LOGID);
    //logger_id_release(LOGID0);

    if (log_file) {
        logger_output_deregister(log_file);
        fclose(log_file);
    }

#ifndef SK_RELEASE_VERSION
    logger_output_deregister(stdout);
#endif
} // close_logger
