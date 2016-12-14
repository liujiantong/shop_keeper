#include "sk_video_surv.hpp"
#include "sk_blob_tracker.hpp"

#include <math.h>
#include <string>
#include <vector>

#include <opencv/cv.h>
#include <opencv/highgui.h>

#include "picort.h"

#define _ROTATION_INVARIANT_DETECTION_
#ifndef _ROTATION_INVARIANT_DETECTION_
#define _INLINE_BINTEST_
#endif

#ifndef QCUTOFF
#define QCUTOFF 		2.5f
#endif
#ifndef MINSIZE
#define MINSIZE 		30
#endif
#ifndef SCALEFACTOR
#define SCALEFACTOR 	1.2f
#endif
#ifndef STRIDEFACTOR
#define STRIDEFACTOR 	0.1f
#endif
#ifndef NO_OVERLAPRATIO
#define NO_OVERLAPRATIO 1.2f
#endif


static const int default_fg_area	= 3600;


#define NONE_DETECTED(opt)   ( (opt) == none_detection )
#define MOTION_DETECTED(opt) ( ((opt)&motion_detection) == motion_detection )
#define FACE_DETECTED(opt)   ( ((opt)&face_detection) == face_detection )
#define PEOPLE_DETECTED(opt) ( ((opt)&people_detection) == people_detection )
#define BODY_DETECTED(opt)   ( ((opt)&body_detection) == body_detection )
#define OBJECT_DETECTED(opt) ( ((opt)&object_detection) == object_detection )


#define ADD_RECT_PNTS(pnts, r) { \
	pnts[0] = cv::Point2d(r.x, r.y); \
	pnts[1] = cv::Point2d(r.x, r.y + r.height); \
	pnts[2] = cv::Point2d(r.x + r.width, r.y + r.height); \
	pnts[3] = cv::Point2d(r.x + r.width, r.y); \
}


/* compare 2 rects to check if they should be merged */
static bool compare_rect(const cv::Rect& ra, const cv::Rect& rb);
static inline bool rect_polygon_intersect(std::vector<cv::Point> &cntr, cv::Rect r);


sk_video_surv::sk_video_surv(int opt, int ipc_n) : zmq_push(NULL), ipc_num(ipc_n), \
fgdet(NULL), facedet(NULL), tracker(NULL), bodydet(NULL), objdet(NULL) {
	if (MOTION_DETECTED(opt)) {
		fgdet = new fg_detector;
	}

	if (FACE_DETECTED(opt)) {
		if (fgdet == NULL) fgdet = new fg_detector;
		facedet = new pico_face_detector;
		tracker = new ms_blob_tracker(ipc_num);
	}

	//focus_rect = cv::Rect(0,0,-1,-1);
} // sk_video_surv constructora


void sk_video_surv::set_action(int opt, cv::Rect& r, int direct) {
	if (MOTION_DETECTED(opt)) {
		if (fgdet == NULL) fgdet = new fg_detector;
	}

	if (FACE_DETECTED(opt)) {
		if (fgdet == NULL) fgdet = new fg_detector;
		if (facedet == NULL) facedet = new pico_face_detector;
		if (tracker == NULL) {
			tracker = new ms_blob_tracker(ipc_num);
			tracker->set_push(zmq_push);
		}
	}
	
	if (BODY_DETECTED(opt)) {
		if (fgdet == NULL) fgdet = new fg_detector;
		if (bodydet == NULL) bodydet = new body_detector;
		if (tracker == NULL) {
			tracker = new ms_blob_tracker(ipc_num);
			tracker->set_push(zmq_push);
		}
	}

	if (OBJECT_DETECTED(opt)) {
		if (fgdet == NULL) fgdet = new fg_detector;
		if (objdet == NULL) objdet = new object_detector;
		if (tracker == NULL) {
			tracker = new ms_blob_tracker(ipc_num);
			tracker->set_push(zmq_push);
		}
	}

	vs_item_t itm;
	itm.vs_type = opt; itm.rect = r; itm.pn = 4; itm.direct = direct;
	ADD_RECT_PNTS(itm.pnts, r);
	vs_list.push_back(itm);
} // sk_video_surv::set_action


void sk_video_surv::set_action(int opt, std::vector<cv::Point>& pnts) {
	if (MOTION_DETECTED(opt)) {
		if (fgdet == NULL) fgdet = new fg_detector;
	}

	if (OBJECT_DETECTED(opt)) {
		if (fgdet == NULL) fgdet = new fg_detector;
		if (objdet == NULL) objdet = new object_detector;
		if (tracker == NULL) {
			tracker = new ms_blob_tracker(ipc_num);
			tracker->set_push(zmq_push);
		}
	}

	vs_item_t itm;
	itm.vs_type = opt; itm.pn = pnts.size();

	for (int i = 0; i < itm.pn; i++) {
		// sk_debug("Point(%d, %d)\n", pnts[i].x, pnts[i].y);
		itm.pnts[i] = pnts[i];
	}
	itm.rect = cv::boundingRect(pnts);

	vs_list.push_back(itm);
} //  sk_video_surv::set_action


void sk_video_surv::release() {
	if (fgdet)   delete fgdet;
	if (facedet) delete facedet;
	if (tracker) delete tracker;
	if (objdet)	 delete objdet;
	//sk_debug("release done\n");
} // sk_video_surv::release


void sk_video_surv::connect_main(zctx_t *zmq_ctx, const char *endpnt) {
	assert(zmq_ctx && endpnt);

	zmq_push = zsocket_new(zmq_ctx, ZMQ_PUSH);
    int rc = zsocket_connect(zmq_push, endpnt);
    assert(rc == 0);

    if (tracker) tracker->set_push(zmq_push);
} // sk_video_surv::connect_main


#define ALARM_AREA  400
void sk_video_surv::process(IplImage *image) {
	cv::Mat img_bgr, roi_bgr, bg_img_gray, roi_gray;
	img_bgr = cv::cvarrToMat(image);

	cv::cvtColor(img_bgr, bg_img_gray, CV_BGR2GRAY);

	if (fgdet) fgdet->update_model(bg_img_gray);
	
	if (vs_list.empty()) return;

	if (!fgdet->detected()) {
		if (tracker) tracker->clean();
		if (facedet) facedet->clean();
		if (bodydet) bodydet->clean();
		if (objdet)  objdet->clean();
		return;
	}
    
    if (!fgdet->detect_started()) return;


	vs_item_vector::const_iterator vsitr = vs_list.begin();
	for (; vsitr!=vs_list.end(); ++vsitr) {
		if (MOTION_DETECTED(vsitr->vs_type) && fgdet) {
			bool alarm = false;

			std::vector<cv::Point> cntr(vsitr->pn);
			for (int i = 0; i < vsitr->pn; i++) {
				cntr.push_back(vsitr->pnts[i]);
			}
			rect_vector rv = fgdet->get_detected();
			rect_vector::const_iterator rvitr = rv.begin();
			for (; rvitr != rv.end(); ++rvitr) {
				cv::Rect rs_rect = (vsitr->rect & *rvitr);
				if (rs_rect.area() > ALARM_AREA) {
					if (rect_polygon_intersect(cntr, *rvitr)) {
						alarm = true;
						break;
					}
				}
			}
			if (alarm) {
				send_message(MSG_ALARM);
			}
		}

		else if (FACE_DETECTED(vsitr->vs_type) && facedet) {
			// print_rect("detect face now: ", vsitr->rect);
			cv::Rect f_rect = vsitr->rect;
			roi_bgr = img_bgr(f_rect);
			roi_gray = bg_img_gray(f_rect);

			cv::equalizeHist(roi_gray, roi_gray);
			IplImage bgimg = roi_gray;


			rect_vector contours = fgdet->get_detected();
			rect_vector search_rects;
			facedet->calc_search_rects(contours, search_rects, f_rect);

			// sk_debug("contours.size: %lu, search_rects size: %lu\n", contours.size(), search_rects.size());

			//FIXME: complex logic here
			vs_item_t vsitm = *vsitr;
			if (!tracker->has_blobs()) {
				facedet->detect(&bgimg, search_rects);
				tracker->track_faces(roi_bgr, facedet->get_detected(), vsitm);
			}
			else {
				rect_vector out_rects;
				tracker->process(roi_bgr, search_rects, out_rects);
				facedet->detect(&bgimg, out_rects);

				ms_blob_vector blobs_tochk;
				tracker->get_blobs_tocheck(blobs_tochk);
				facedet->double_detect(&bgimg, blobs_tochk, f_rect);
				tracker->add_blobs_tocheck(blobs_tochk);

				tracker->track_faces(roi_bgr, facedet->get_detected(), vsitm);
			}
		}

		else if (BODY_DETECTED(vsitr->vs_type) && bodydet) {
			//print_rect("detecting body now: ", vsitr->rect);
			cv::Rect f_rect = vsitr->rect;
			roi_bgr = img_bgr(f_rect);
			roi_gray = bg_img_gray(f_rect);

			cv::equalizeHist(roi_gray, roi_gray);
			IplImage bgimg = roi_gray;

			rect_vector contours = fgdet->get_detected();
			rect_vector search_rects;
			bodydet->calc_search_rects(contours, search_rects, f_rect);

			// sk_debug("contours.size: %lu, search_rects size: %lu\n", contours.size(), search_rects.size());

			vs_item_t vsitm = *vsitr;
			if (!tracker->has_blobs()) {
				bodydet->detect(&bgimg, search_rects);
				tracker->track_faces(roi_bgr, bodydet->get_detected(), vsitm);
			}
			else {
				rect_vector out_rects;
				tracker->process(roi_bgr, search_rects, out_rects);
				bodydet->detect(&bgimg, out_rects);

				ms_blob_vector blobs_tochk;
				tracker->get_blobs_tocheck(blobs_tochk);
				bodydet->double_detect(&bgimg, blobs_tochk, f_rect);
				tracker->add_blobs_tocheck(blobs_tochk);

				tracker->track_faces(roi_bgr, bodydet->get_detected(), vsitm);
			}
		}

		else if (OBJECT_DETECTED(vsitr->vs_type) && objdet) {
			cv::Rect f_rect = vsitr->rect;
			roi_bgr = img_bgr(f_rect);
            IplImage bgimg = roi_bgr;

			//FIXME: rect range
			rect_vector contours = fgdet->get_detected();
			rect_vector search_rects;
			objdet->calc_search_rects(contours, search_rects, f_rect);
			//sk_debug("search_rects.size: %lu\n", search_rects.size());

			vs_item_t vsitm = *vsitr;
			if (!tracker->has_blobs()) {
				//sk_debug("track blobs: 0\n");
				ms_blob_vector blobs_tochk;
				ms_blob_vector blobs = tracker->get_blobs();
				objdet->detect(&bgimg, search_rects, blobs, blobs_tochk);
				tracker->track_faces(roi_bgr, objdet->get_detected(), vsitm);
			}
			else {
				//sk_debug("track blobs: %lu\n", tracker->get_blobs().size());
				rect_vector out_rects;
				if (search_rects.empty()) {
					// sk_debug("search_rects empty, clean objdet\n");
					objdet->clean();
                    return;
				}
                
                tracker->process(roi_bgr, search_rects, out_rects);
                
				ms_blob_vector blobs_tochk;
				ms_blob_vector blobs = tracker->get_blobs();
				tracker->get_blobs_tocheck(blobs_tochk);
				objdet->detect(&bgimg, out_rects, blobs, blobs_tochk);
				//objdet->double_detect(&bgimg, blobs_tochk, f_rect);
				tracker->add_blobs_tocheck(blobs_tochk);

				tracker->track_faces(roi_bgr, objdet->get_detected(), vsitm);
			}
		}
	} // for loop to check vs_list
} // sk_video_surv::process


void sk_video_surv::draw_detected(IplImage *image) {
	if (fgdet == NULL) return;

	cv::Mat img = cv::cvarrToMat(image);
    //cv::rectangle(img, focus_rect, cv::Scalar(0, 0, 255));

#ifdef SK_RELEASE_VERSION
    if (!tracker) {
#endif
        rect_vector rects = fgdet->get_detected();
        std::vector<cv::Rect>::const_iterator itr = rects.begin();
        for (; itr != rects.end(); ++itr) {
            cv::rectangle(img, *itr, cv::Scalar(0, 255, 0), 2);
        }
#ifdef SK_RELEASE_VERSION
    }
#endif

    vs_item_vector::const_iterator vsitr = vs_list.begin();
    for (; vsitr != vs_list.end(); ++vsitr) {
    	cv::Rect f_rect = vsitr->rect;
		const cv::Point* pts = &vsitr->pnts[0];
		if (MOTION_DETECTED(vsitr->vs_type))
			cv::polylines(img, &pts, &vsitr->pn, 1, true, cv::Scalar(0, 0, 255), 2, CV_AA);
		else
			cv::polylines(img, &pts, &vsitr->pn, 1, true, cv::Scalar(0, 255, 255), 2, CV_AA);

		
		if ( !(FACE_DETECTED(vsitr->vs_type) || 
			   BODY_DETECTED(vsitr->vs_type) || 
			   OBJECT_DETECTED(vsitr->vs_type)) ) continue;

		if (tracker && tracker->has_blobs()) {
			ms_blob_vector blobs = tracker->get_blobs();
			ms_blob_vector::const_iterator b_itr = blobs.begin();
			for (; b_itr != blobs.end(); ++b_itr) {
		        cv::rectangle(img, b_itr->track_win+f_rect.tl(), cv::Scalar(255, 0, 0), 2);
			}
		}
	}

} // sk_video_surv::draw_detected


void inline fg_detector::detect_fg() {
	if (!detect_start) return;

	det_rects.clear();
    //cv::threshold(fg_mask, fg_mask, 80, 255, CV_THRESH_BINARY);
    cv::threshold(fg_mask, fg_mask, 60, 255, CV_THRESH_BINARY);

	std::vector<std::vector<cv::Point> > contours;
	cv::findContours(fg_mask, contours, CV_RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

	rect_vector rects;
	std::vector<std::vector<cv::Point> >::const_iterator it = contours.begin();
	for (; it!=contours.end(); ++it) {
		cv::Rect rect = cv::boundingRect(*it);
		//sk_debug("rect.area: %d\n", rect.area());
        
        // Reset detect_start if ipc switch ultra-red mode
        if (rect.width > fg_mask.cols * 0.9 && rect.height > fg_mask.rows * 0.9) {
            sk_warning("IPC switch mode, reset detect_start\n");
            detect_start = false;
            learn_frame_num = 0;
            return;
        }
        
		if (rect.area() < default_fg_area 
			/*&& (rect.width < fg_mask.cols * 0.8 || rect.height < fg_mask.rows * 0.8)*/) {
			continue;
		}
		rects.push_back(rect);
	}

	if (rects.empty()) return;

	//clustering all rects
	std::vector<int> labels;
    int cluster_num = cv::partition(rects, labels, compare_rect);
    for (int cluster_idx=0; cluster_idx<cluster_num; ++cluster_idx) {
        cv::Rect rect_result(-1, -1, -1, -1);

        int rect_idx = 0;
        std::vector<int>::const_iterator itr = labels.begin();
        for (; itr!=labels.end(); ++itr, ++rect_idx) {
            if (*itr != cluster_idx) continue;

            if (rect_result.height < 0)
                rect_result = rects[rect_idx];
            else
                rect_result |= rects[rect_idx];
        }

        if (rect_result.width > 0 && rect_result.width < fg_mask.cols) {
        	det_rects.push_back(rect_result);
        }

    } // next cluster loop
    
	//if (!det_rects.empty()) sk_debug("det_rects contours:%lu\n", det_rects.size());
} // fg_detector::detect_fg


void fg_detector::update_model(cv::Mat &img) {
	if (!detect_start && learn_frame_num++ > default_history) {
		sk_info("Foreground detection started =====>\n");
		detect_start = true;
	}

	// update the model
	if (!detect_start) {
		//(*bg_model)(img, fg_mask, learning_rate);
		(*bg_model)(img, fg_mask, 0.6);
	}
	else {
		frame_counter++;
		//if ((frame_counter %= 157) == 0 && !detected()) {
		(*bg_model)(img, fg_mask, (frame_counter % 17) == 0 ? learning_rate : 0);
		//(*bg_model)(img, fg_mask, learning_rate);
	}

    //cv::Mat elem;
	cv::Mat elem = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(fg_mask, fg_mask, cv::MORPH_OPEN, elem);
    cv::morphologyEx(fg_mask, fg_mask, cv::MORPH_CLOSE, elem);

    detect_fg();
} // fg_detector::update_model


const char pico_face_detector::appearance_finder[] = {
	#include "facefinder.ea"
};


inline int pico_face_detector::_detected(
	IplImage *roi_img, std::vector<face_rect_t> &face_rects, cv::Rect &chck_box) {
	unsigned char* pixels;
	int nrows, ncols, ldim;

	#define MAXNDETECTIONS 64
	int ndetections;
	float qs[MAXNDETECTIONS], rs[MAXNDETECTIONS], cs[MAXNDETECTIONS], ss[MAXNDETECTIONS];

	// get relevant image data
	pixels = (unsigned char*)roi_img->imageData;

	nrows = roi_img->height;
	ncols = roi_img->width;
	ldim = roi_img->widthStep;

	//sk_debug("nrows:%d, ncols:%d\n", nrows, ncols);

	// actually, all the smart stuff happens here
#ifndef _ROTATION_INVARIANT_DETECTION_
	ndetections =     find_objects(0.0f, rs, cs, ss, qs, MAXNDETECTIONS, (void *)appearance_finder, 
		pixels, nrows, ncols, ldim, SCALEFACTOR, STRIDEFACTOR, MINSIZE, MIN(nrows, ncols), 1);
#else
	ndetections = find_objects_rot(0.0f, rs, cs, ss, qs, MAXNDETECTIONS, (void *)appearance_finder, 
		pixels, nrows, ncols, ldim, SCALEFACTOR, STRIDEFACTOR, MINSIZE, MIN(nrows, ncols), 1);
#endif

	//if (ndetections > 0) sk_debug("Found %d ndetections\n", ndetections);

	if (ndetections == 1) {
		face_rect_t frect0 = { qs[0], cs[0] + chck_box.x, rs[0] + chck_box.y, ss[0] };
		face_rects.push_back(frect0);
	}
	else if (ndetections > 1) {
		face_rect_t frect0 = { qs[0], cs[0] + chck_box.x, rs[0] + chck_box.y, ss[0] };
		face_rects.push_back(frect0);

		#if 0
		for (int i=1; i<ndetections; ++i) {
			float overlaped = ( fabs(rs[i]-rs[i-1])+fabs(cs[i]-cs[i-1]) )/ss[i];
			//sk_debug("overlaped=%f\n", overlaped);
			if (overlaped > NO_OVERLAPRATIO) {
				face_rect_t frect = {qs[i], cs[i]+box.x, rs[i]+box.y, ss[i]};
				face_rects.push_back(frect);
			}
		}
		#endif
	}

	return face_rects.size();
}


void pico_face_detector::double_detect(IplImage* frame, ms_blob_vector &blobs, cv::Rect &focus_rct) {
	if (blobs.empty()) return;

	sk_debug("++++++> Has %lu face to double check\n", blobs.size());
	std::vector<face_rect_t> face_rects;
	cv::Mat mat_gray = cv::cvarrToMat(frame);

	ms_blob_vector::iterator itr = blobs.begin();
	for (; itr != blobs.end(); ++itr) {
		if (itr->track_win.width < MINSIZE || itr->track_win.height < MINSIZE) continue;
		
		cv::Mat roi = mat_gray(itr->track_win);
		IplImage roi_img = roi;

		int fnum = _detected(&roi_img, face_rects, itr->track_win);
		if (fnum > 0) {
			face_rect_t frect = face_rects[0];
			cv::Rect r(frect.center_x-frect.width, frect.center_y-frect.width, frect.width, frect.width);
			r &= cv::Rect(0, 0, focus_rct.width, focus_rct.height);
			itr->track_win = r;
			itr->confidence = 1;
			itr->suspicious = 0;
			print_rect("+++++> double check OK, rect: ", r);
		}
		else {
			itr->suspicious += 1;
			sk_debug("blob: %d, suspicious: %d\n", itr->blob_id, itr->suspicious);
		}
	} // for loop for check all fg
} // pico_face_detector::double_detect


void pico_face_detector::detect(IplImage* frame, rect_vector &boxes) {
	assert(frame && frame->depth == IPL_DEPTH_8U && frame->nChannels == 1);
	
	the_faces.clear();
	if (boxes.empty()) return;

	std::vector<face_rect_t> face_rects;
	//cv::Mat mat_gray = cv::cvarrToMat(frame).clone();
	cv::Mat mat_gray = cv::cvarrToMat(frame);

	rect_vector::const_iterator itr = boxes.begin();
	for (; itr != boxes.end(); ++itr) {
		if (itr->width < MINSIZE || itr->height < MINSIZE) continue;
		
		cv::Rect box = *itr;
		cv::Mat roi = mat_gray(box);
		IplImage roi_img = roi;

		_detected(&roi_img, face_rects, box);
	} // for loop for check all fg

	// if (face_rects.size() > 0) sk_debug("Face counter=%d =======>\n", (int)face_rects.size());

	std::vector<face_rect_t>::const_iterator f_itr = face_rects.begin();
	for (; f_itr != face_rects.end(); ++f_itr) {
		//sk_debug("f_itr->confidence:%f\n", f_itr->confidence);
		if (f_itr->confidence >= QCUTOFF) {
			float radius = f_itr->width / 2;
			cv::Rect r(f_itr->center_x-radius, f_itr->center_y-radius, f_itr->width, f_itr->width);
			the_faces.push_back(r);
		}
	}
	if (the_faces.size() > 0) sk_debug("Found %lu Refined Face =======>\n", the_faces.size());
} // pico_face_detector::detect


void pico_face_detector::calc_search_rects(const rect_vector &in_rects, rect_vector &out_rects, const cv::Rect &f_rect) {
	rect_vector::const_iterator itr = in_rects.begin();
	for (; itr != in_rects.end(); ++itr) {
		cv::Rect r = *itr;
		cv::Rect _r = r & f_rect;
		if (_r.area() == 0) continue;

#if 0
		r.x -= cvRound(r.width*0.10);
        r.width = cvRound(r.width*1.20);
        r.y -= cvRound(r.height*0.10);
        r.height = cvRound(r.height*1.20);
        
        if (r.x < 0) r.x = 0;
        if (r.y < 0) r.y = 0;
#endif

        r &= f_rect;
		r = cv::Rect(r.x-f_rect.x, r.y-f_rect.y, r.width, r.height);
        out_rects.push_back(r);
	}
} // pico_face_detector::calc_search_rects



void people_detector::detect(IplImage* frame, rect_vector &rects) {
	the_people.clear();
	
	rect_vector found;
    // run the detector with default parameters. to get a higher hit-rate
    // (and more false alarms, respectively), decrease the hitThreshold and
    // groupThreshold (set groupThreshold to 0 to turn off the grouping completely).
    hog->detectMultiScale(frame, found, 0, cv::Size(8,8), cv::Size(32,32), 1.05, 2);
    for (int i=0; i<found.size(); i++) {
        cv::Rect r = found[i];

        int j = 0;
        for (; j<found.size(); j++) {
            if (j != i && (r & found[j]) == r)
                break;
        }

        if (j == found.size() ) {
            the_people.push_back(r);
        }
    }
} // people_detector::detect



#define MIN_BODY_SIZE cv::Size(100, 100)
void body_detector::detect(IplImage* frame, rect_vector &rects) {
	the_bodies.clear();

	//cv::Mat frame_gray = cv::cvarrToMat(frame).clone();
	cv::Mat frame_gray = cv::cvarrToMat(frame);
	//-- Detect body
	body_cascade->detectMultiScale(frame_gray, the_bodies, 1.1, 3, (0 | CV_HAAR_SCALE_IMAGE), MIN_BODY_SIZE );

	if (!the_bodies.empty()) sk_debug("Found body: %lu =======>\n", the_bodies.size());
} // body_detector::detect


inline int body_detector::_detected(
	IplImage *roi_img, std::vector<face_rect_t> &face_rects, cv::Rect &chck_box) {

	rect_vector bodies;
	cv::Mat roi_gray = cv::cvarrToMat(roi_img);
	//-- Detect body
	body_cascade->detectMultiScale(roi_gray, bodies, 1.1, 3, (0 | CV_HAAR_SCALE_IMAGE), MIN_BODY_SIZE);

	if (bodies.empty()) {
		sk_debug("double-check body: Not Found\n");
		return 0;
	}

	cv::Rect r = bodies[0];
	face_rect_t frect0 = { 1.0, r.x + chck_box.x + r.width/2, \
		r.y + chck_box.y + r.height/2, std::min(r.width, r.height) };
	face_rects.push_back(frect0);

	return 1;
} // body_detector::_detected


void body_detector::calc_search_rects(const rect_vector &in_rects, rect_vector &out_rects, const cv::Rect &f_rect) {
	rect_vector::const_iterator itr = in_rects.begin();
	for (; itr != in_rects.end(); ++itr) {
		cv::Rect r = *itr;
		cv::Rect _r = r & f_rect;
		if (_r.area() == 0) continue;

		r.x -= cvRound(r.width*0.10);
		r.width = cvRound(r.width*1.20);
		r.y -= cvRound(r.height*0.10);
		r.height = cvRound(r.height*1.20);

		if (r.x < 0) r.x = 0;
		if (r.y < 0) r.y = 0;
		if (r.width > r.height) r.height = r.width;
		else r.width = r.height;

		r &= f_rect;
		r = cv::Rect(r.x-f_rect.x, r.y-f_rect.y, r.width, r.height);
		out_rects.push_back(r);
	}
} // body_detector::calc_search_rects


void body_detector::double_detect(IplImage* frame, ms_blob_vector &blobs, cv::Rect &focus_rct) {
	if (blobs.empty()) return;

	sk_debug("++++++> Has %lu body to double check\n", blobs.size());
	std::vector<face_rect_t> face_rects;
	cv::Mat mat_gray = cv::cvarrToMat(frame);

	ms_blob_vector::iterator itr = blobs.begin();
	for (; itr != blobs.end(); ++itr) {
		if (itr->track_win.width < MINSIZE || itr->track_win.height < MINSIZE) continue;

		cv::Mat roi = mat_gray(itr->track_win);
		IplImage roi_img = roi;

		int fnum = _detected(&roi_img, face_rects, itr->track_win);
		if (fnum > 0) {
			face_rect_t frect = face_rects[0];
			cv::Rect r(frect.center_x - frect.width, frect.center_y - frect.width, frect.width, frect.width);
			r &= cv::Rect(0, 0, focus_rct.width, focus_rct.height);
			itr->track_win = r;
			itr->confidence = 1;
			itr->suspicious = 0;
			//print_rect("+++++> double check OK, rect: ", r);
		}
		else {
			itr->suspicious += 1;
			//sk_debug("blob: %d, suspicious: %d\n", itr->blob_id, itr->suspicious);
		}
	} // for loop for check all fg
} // body_detector::double_detect


#define MIN_OBJ_AREA    8100
//#define MIN_OBJ_AREA    6400
//#define HW_RATIO1       1.35f
//#define HW_RATIO2       0.95f
void object_detector::detect(IplImage* frame, rect_vector &rects, ms_blob_vector &blobs, ms_blob_vector &blobs2chk) {
	if (rects.empty()) return;
    
    the_objects.clear();
    //sk_debug("detect: check %d rects at first\n", int(rects.size()));

	rect_vector::const_iterator itr = rects.begin();
	for (; itr != rects.end(); ++itr) {
        //cv::Point itr_c = RECT_CENTER(*itr);
        
		if (itr->area() > MIN_OBJ_AREA /*&& (float(itr->height) / itr->width) > HW_RATIO1*/) {
			// FIXME: check if it exists in the objects
			bool found = false;
			ms_blob_vector::const_iterator mitr = blobs.begin();
            
			for (; mitr != blobs.end(); ++mitr) {
                cv::Rect nrect = mitr->track_win & *itr;
				if (nrect.area() > (mitr->track_win.area() * 0.5f)) {
                    sk_debug("The FG rect existed in track blobs\n");
					found = true;
					break;
				}
			}
            if (found) continue;
            
			ms_blob_vector::const_iterator mitr1 = blobs2chk.begin();
			for (; mitr1 != blobs2chk.end(); ++mitr1) {
                cv::Rect nrect = mitr1->track_win & *itr;
				if (nrect.area() > (mitr1->track_win.area() * 0.5f)) {
					sk_debug("The FG rect existed in tochk blobs\n");
					found = true;
					break;
				}
			}
            
			if (!found) {
				int h = std::min(int(itr->width * 1.05), int(itr->height / 2.0));
				//int h = std::max(int(itr->width * 1.05), int(itr->height / 2.75));
				cv::Rect rectup(itr->x, itr->y, itr->width, h);
				the_objects.push_back(rectup);
			}
		}
	}
	//if (!the_objects.empty()) sk_debug("Found %lu object(s) =======>\n", the_objects.size());
} // object_detector::detect


#define EDGE_DISTANCE  0
void object_detector::double_detect(IplImage* frame, ms_blob_vector &blobs, cv::Rect &focus_rct) {
	if (blobs.empty()) return;

	sk_debug("++++++> Has %lu object(s) to double check\n", blobs.size());

	ms_blob_vector::iterator itr = blobs.begin();
	for (; itr != blobs.end(); ++itr) {
		if (itr->track_win.width < MINSIZE || itr->track_win.height < MINSIZE) {
            itr->suspicious++;
            continue;
        }

		cv::Rect r = itr->track_win;
		if (r.area() > MIN_OBJ_AREA /*&& (float(r.height) / r.width) > HW_RATIO2*/) {
			//FIXME: if on edge, dont recover it.
			if ( /*((r.width > 0.8 * focus_rct.width) || (r.height > 0.8 * focus_rct.height)) && */
				(r.x <= EDGE_DISTANCE || r.y <= EDGE_DISTANCE ||
				(focus_rct.width - (r.x + r.width)) < EDGE_DISTANCE ||
				(focus_rct.height - (r.x + r.width)) < EDGE_DISTANCE)) {
				itr->suspicious++;
				print_rect("too close focus rect's edge, r: ", r);
			}
			else {
				r &= cv::Rect(0, 0, focus_rct.width, focus_rct.height);
				itr->track_win = r;
				itr->confidence = 1;
				itr->suspicious = 0;
				print_rect("+++++> double check OK, rect: ", r);
			}
		}
		else {
			itr->suspicious++;
			sk_debug("blob: %d, suspicious: %d\n", itr->blob_id, itr->suspicious);
		}
	} // for loop for check all fg
} // object_detector::double_detect


void object_detector::calc_search_rects(const rect_vector &in_rects, /*out*/rect_vector &out_rects, const cv::Rect &f_rect) {
	rect_vector::const_iterator itr = in_rects.begin();
	for (; itr != in_rects.end(); ++itr) {
		cv::Rect r = *itr;
		// print_rect("FG rect: ", r);
        
        // TODO: need better solution to filter blob
		//if ( !RECT_CONTAIN(f_rect, r) ) continue;
		cv::Rect _r = r & f_rect;
		if (_r.area() < r.area() * 0.75f) continue;

		r.x -= cvRound(r.width*0.05);
		r.width = cvRound(r.width*1.10);
		r.y -= cvRound(r.height*0.05);
		r.height = cvRound(r.height*1.10);

		if (r.x < 0) r.x = 0;
		if (r.y < 0) r.y = 0;

		r &= f_rect;
		r = cv::Rect(r.x-f_rect.x, r.y-f_rect.y, r.width, r.height);
		//print_rect("FG rect after correction: ", r);
		out_rects.push_back(r);
	}
} // object_detector::calc_search_rects


void ocv_face_detector::detect(IplImage* frame, rect_vector &rects) {
	the_faces.clear();

	//cv::Mat frame_gray = cv::cvarrToMat(frame).clone();
	cv::Mat frame_gray = cv::cvarrToMat(frame);
	//-- Detect faces
	face_cascade->detectMultiScale(frame_gray, the_faces, 
		1.1, 3, 0|CV_HAAR_SCALE_IMAGE, cv::Size(50, 50) );

	if (!the_faces.empty()) sk_debug("Found faces: %lu =======>\n", the_faces.size());
} // ocv_face_detector::detect



static bool compare_rect(const cv::Rect& ra, const cv::Rect& rb) {
    cv::Point2f  pa, pb;

    pa.x = ra.x + ra.width*0.5f;
    pa.y = ra.y + ra.height*0.5f;
    pb.x = rb.x + rb.width*0.5f;
    pb.y = rb.y + rb.height*0.5f;
    float w = (ra.width+rb.width)*0.5f;
    float h = (ra.height+rb.height)*0.5f;

    float dx = (float)(fabs(pa.x - pb.x) - w);
    float dy = (float)(fabs(pa.y - pb.y) - h);

    //float wt = 0;
    float wt = cv::max(ra.width, rb.width) * 0.3f;
    float ht = cv::max(ra.height, rb.height) * 0.3f;
    return (dx < wt && dy < ht);
} // compare_rect


static inline bool rect_polygon_intersect(std::vector<cv::Point> &cntr, cv::Rect r) {
	cv::Point corners[4];
	corners[0] = r.tl();
	corners[1] = r.br();
	corners[2] = cv::Point(r.x, r.y + r.height);
	corners[3] = cv::Point(r.x + r.width, r.y);

	for (int i = 0; i < 4; i++) {
		if (cv::pointPolygonTest(cntr, corners[i], false) > 0)
			return true;
	}

	return false;
} // rect_polygon_intersect

