#ifndef __SK_VIDEO_SURV_HPP__
#define __SK_VIDEO_SURV_HPP__

#include "sk.hpp"

#include <time.h>
#include <string>
#include <czmq.h>
#include <vector>

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/objdetect/objdetect.hpp>



static const int default_history   		= 260;
//static const int default_history   	= 150;
static const int default_mixtures   	= 5;
//static const double default_bg_ratio  = 0.9;
//static const double default_bg_ratio  = 0.8;
static const double default_bg_ratio 	= 0.75;
static const double learning_rate       = 0.01f;


// forward
class fg_detector;
class people_detector;
class body_detector;
class object_detector;
class pico_face_detector;
class ocv_face_detector;
class ms_blob_tracker;


typedef enum {
	none_detection   = 0,
	motion_detection = 1,
	face_detection 	 = 2,
	people_detection = 4,
	body_detection   = 8,
	object_detection = 16,
} sk_vs_type;


class sk_video_surv {

protected:
	sk_video_surv(int opt, int ipc_n);

public:
	static sk_video_surv * create_video_surv(int opt, int ipc_n) {
		return new sk_video_surv(opt, ipc_n);
	};

	void process(IplImage *image);

	void draw_detected(IplImage *image);

	void connect_main(zctx_t *zmq_ctx, const char *endpnt);

	inline void send_message(char *msg) {
		if (zmq_push) zstr_send(zmq_push, msg);
	};

	void set_action(int opt, cv::Rect& r, int direct=-1);
	void set_action(int opt, std::vector<cv::Point>& pnts);
	inline void reset_actions(int atype) {
		if (atype == 0) vs_list.clear();
		else {
			std::vector<vs_item_t> vlist;
			std::vector<vs_item_t>::const_iterator itr = vs_list.begin();
			for (; itr != vs_list.end(); ++itr) {
				if (atype == 1) {
					if (itr->vs_type != motion_detection) {
						//sk_debug("keep action with vs_type:%d\n", itr->vs_type);
						vlist.push_back(*itr);
					}
				}
				else {
					if (itr->vs_type == motion_detection) {
						//sk_debug("keep action with vs_type:%d\n", itr->vs_type);
						vlist.push_back(*itr);
					}
				}
			}

			vs_list = vlist;
			sk_debug("vs_list.size: %lu\n", vs_list.size());
		}
	};

	void release();

private:
	void *zmq_push;

	int ipc_num;
	std::string alarm_endpnt;

	std::vector<vs_item_t> vs_list;

	fg_detector *fgdet;
	pico_face_detector *facedet;
	ms_blob_tracker *tracker;
	body_detector *bodydet;
	object_detector *objdet;
	//people_detector *peopledet;
};


class sk_detector {
	virtual bool detected() = 0;
	virtual rect_vector get_detected() = 0;
};


class fg_detector : public sk_detector {
public:
	fg_detector() : learn_frame_num(0), detect_start(false), frame_counter(0) {
		//sk_debug("fg_detector started\n");
		bg_model = 
		new cv::BackgroundSubtractorMOG(default_history, default_mixtures, default_bg_ratio);
	};

	fg_detector(int history, int nmixtures, double bg_ratio) : learn_frame_num(0), detect_start(false), frame_counter(0) {
		bg_model = new cv::BackgroundSubtractorMOG(history, nmixtures, bg_ratio);
	};

	virtual ~fg_detector() {
		delete bg_model;
	};


	rect_vector get_detected() {
		return det_rects;
	};
	bool detected() {
		return !det_rects.empty();
	};

	cv::Mat* get_mask() {
        return &fg_mask;
    };
    
    inline bool detect_started() {
        return detect_start;
    };

	void update_model(cv::Mat &img);

private:
	void inline detect_fg();

	int learn_frame_num;
	bool detect_start;
	int frame_counter;

	cv::Mat fg_mask;
	cv::BackgroundSubtractorMOG *bg_model;
	rect_vector det_rects;
};


class pico_face_detector : public sk_detector {
public:
	pico_face_detector() {
		sk_debug("pico_face_detector started\n");
		the_faces.reserve(8);
	};
	virtual ~pico_face_detector() {};

	void detect(IplImage* frame, rect_vector &rects);
	void double_detect(IplImage* frame, ms_blob_vector &blobs, cv::Rect &focus_rct);

	inline void clean() {
		the_faces.clear();
	};
	rect_vector get_detected() {
		return the_faces;
	};
	bool detected() {
		return !the_faces.empty();
	};
	
	void calc_search_rects(const rect_vector &in_v, /*out*/rect_vector &out_v, const cv::Rect &focus_rect);

private:
	inline int _detected(IplImage *roi_img, /*out*/std::vector<face_rect_t> &face_rects, cv::Rect &chck_box);
	rect_vector the_faces;
	// a structure that encodes object appearance
	const static char appearance_finder[];
};


class people_detector : public sk_detector {
public:
	people_detector() {
		hog = new cv::HOGDescriptor;
    	hog->setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());
	}

	virtual ~people_detector() {
		delete hog;
	}

	void detect(IplImage* frame, rect_vector &rects);

	rect_vector get_detected() {
		return the_people;
	}

	bool detected() {
		return !the_people.empty();
	}

private:
	rect_vector the_people;
	cv::HOGDescriptor *hog;
};


class object_detector : public sk_detector {
public:
	object_detector() {
		the_objects.reserve(8);
	};
	virtual ~object_detector() {};

	void detect(IplImage* frame, rect_vector &rects, ms_blob_vector &blobs, ms_blob_vector &blob2chk);
	void double_detect(IplImage* frame, ms_blob_vector &blobs, cv::Rect &focus_rct);
	void calc_search_rects(const rect_vector &in_v, /*out*/rect_vector &out_v, const cv::Rect &focus_rect);

	rect_vector get_detected() {
		return the_objects;
	};
	bool detected() {
		return !the_objects.empty();
	};

	inline void clean() {
		the_objects.clear();
	};

private:
	rect_vector the_objects;

};


#define body_cascade_name	"haarcascade_mcs_upperbody.xml"
class body_detector : public sk_detector {
public:
	body_detector() {
		body_cascade = new cv::CascadeClassifier;
		if (!body_cascade->load(body_cascade_name)) {
			sk_error("load body cascade:%s failed\n", body_cascade_name);
			exit(1);
		} 
	};

	virtual ~body_detector() {
		delete body_cascade;
	};


	void detect(IplImage* frame, rect_vector &rects);
	void double_detect(IplImage* frame, ms_blob_vector &blobs, cv::Rect &focus_rct);
	void calc_search_rects(const rect_vector &in_v, /*out*/rect_vector &out_v, const cv::Rect &focus_rect);

	rect_vector get_detected() {
		return the_bodies;
	};

	bool detected() {
		return !the_bodies.empty();
	};

	inline void clean() {
		the_bodies.clear();
	};

private:
	inline int _detected(IplImage *roi_img, /*out*/std::vector<face_rect_t> &face_rects, cv::Rect &chck_box);
	cv::CascadeClassifier *body_cascade;
	rect_vector the_bodies;
};


#define face_cascade_name   "haarcascade_frontalface_alt2.xml"
class ocv_face_detector : public sk_detector {
public:
	ocv_face_detector() {
		face_cascade = new cv::CascadeClassifier;
		if (!face_cascade->load(face_cascade_name)) {
			sk_debug("load face cascade failed\n");
			exit(1);
		}
	};

	virtual ~ocv_face_detector() {
		delete face_cascade;
	};


	void detect(IplImage* frame, rect_vector &rects);

	rect_vector get_detected() {
		return the_faces;
	};

	bool detected() {
		return !the_faces.empty();
	};

private:
	cv::CascadeClassifier *face_cascade;
	rect_vector the_faces;
};


#endif

