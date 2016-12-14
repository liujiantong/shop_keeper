#include "sk_blob_tracker.hpp"

#include <opencv2/video/tracking.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <czmq.h>


#define MAX_SUSPICIOUS  45

const float hranges[] = {0, 180};
const float* phranges = hranges;

const int vmin = 35, vmax = 240;
const int smin = 25, smax = 240;

const int hist_size = 18;
//const int kernel_size = 25;
const int default_face_area = 400;


/**
 1. use meanShift instead of CamShift
 2. use BG-Weighted Histogram
 3. remove checks for large tracking windows
 */
     
 //calculate kernel based histogram
 inline static void calc_kernel_hist(const cv::Mat &image, cv::Mat &mask, /*out*/cv::Mat &hist /*, int h*/) {
     int nr = image.rows;
     int nch = image.channels();    // nchannel == 1
     int nc = image.cols * nch;
     int nr_2 = image.rows / 2;
     int nc_2 = image.cols / 2;
     float bin_w = (float(hranges[1]) / hist_size) + 0.01f;
     
     int h_c = int(nc_2 * 0.7f);
     int h_r = int(nr_2 * 0.7f);
     //int h2 = 2*h*h;
     int h2 = h_c*h_c + h_r*h_r;
    
     for (int j=0; j<nr; j++) {
         if (abs(j-nr_2) > h_r) continue;
         
         int delta_r = (j-nr_2) * (j-nr_2);
         const uchar *data = image.ptr<uchar>(j);
         const uchar *data_mask = mask.ptr<uchar>(j);
         
         for (int i=0; i<nc; i++) {
             if (data_mask[i] == 0 || abs(i-nc_2) > h_c) continue;
             
             int delta_c = (i-nc_2) * (i-nc_2);
             /* Gaussian kernel */
             //double w = exp( -(delta_r + delta_c)/h2 );
             //double w = exp( -(delta_c + delta_c)/h2 );
             
             /* Epanechnikov kernel */
             int delta = (delta_r + delta_c)/h2;
             if (delta >= 1) continue;
             double w = 1 - delta;
             int bin_idx = int(data[i/* *nch */] / bin_w);
             hist.at<float>(bin_idx) += w;
         }
     }
 } // kernel_hist


void ms_blob_tracker::process(cv::Mat &image, const rect_vector &in_rects, rect_vector &out_rects) {
	//sk_debug("the_blobs size:%lu\n", the_blobs.size());
	cv::Mat hsv, hue, mask, backproj;

	cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
	cv::inRange(hsv, cv::Scalar(0, smin, vmin), cv::Scalar(180, smax, vmax), mask);

    int ch[] = {0, 0};
    hue.create(hsv.size(), hsv.depth());
    cv::mixChannels(&hsv, 1, &hue, 1, ch, 1);

    static cv::TermCriteria criteria(CV_TERMCRIT_EPS|CV_TERMCRIT_ITER, 10, 1);

    ms_blob_vector msblobs;
    ms_blob_vector dbl_chk_blobs;
    ms_blob_vector::iterator itr = the_blobs.begin();

    for (; itr != the_blobs.end(); ++itr) {
    	sk_debug("Tracking person:%d\n", itr->blob_id);
		//if (itr->track_win.width == 0) continue;
		//float wh_ratio = float(itr->track_win.height) / itr->track_win.width;

		//if (itr->track_win.width > (0.7f*hsv.cols) || itr->track_win.height > (0.7f*hsv.rows) || // check face
		if (itr->track_win.area() < default_face_area /*|| wh_ratio < 1.0*/) { // check object
    		sk_info("We lost person: %d, (w:%d, h:%d) area too small\n", \
				itr->blob_id, itr->track_win.width, itr->track_win.height);
			//if (itr->blob_id > 0) sk_stats_sqlite::insert_lost_blob(itr->blob_id);
            ms_track_blob b = {itr->blob_id, itr->track_win, itr->hist, 0, 0, itr->x_var, itr->y_var};
            dbl_chk_blobs.push_back(b);
        	continue;
    	}
		
    	cv::calcBackProject(&hue, 1, 0, itr->hist, backproj, &phranges);
        backproj &= mask;

        //TODO: when we lost object
        int nonzero = cv::countNonZero(backproj);
        //sk_debug("nonzero=%d, track_win:(%d, %d)\n", nonzero, itr->track_win.width, itr->track_win.height);
        if ( (nonzero < (itr->track_win.area() * 0.15f)) /*&& nonzero < 1000*/) {
        	sk_info("We lost person: %d, backproj too small\n", itr->blob_id);
            //if (itr->blob_id > 0) sk_stats_sqlite::insert_lost_blob(itr->blob_id);
            ms_track_blob b = {itr->blob_id, itr->track_win, itr->hist, 0, 0, itr->x_var, itr->y_var};
            dbl_chk_blobs.push_back(b);
        	continue;
        }
        //FIXME: if found another face

        cv::Rect tr(itr->track_win.x, itr->track_win.y, itr->track_win.width, itr->track_win.height);
        //cv::RotatedRect track_box = cv::CamShift(backproj, tr, criteria);
        //cv::Rect track_win = track_box.boundingRect();
        cv::meanShift(backproj, tr, criteria);
        cv::Rect track_win = tr;
        
        //sk_debug("track_win:(%d,%d,%d,%d)\n", track_win.x, track_win.y, track_win.width, track_win.height);
       
       UPDATE_BLOB_MVAR(*itr, tr);
       ms_track_blob track_blob = {itr->blob_id, track_win, itr->hist, 1, itr->suspicious, itr->x_var, itr->y_var};
       msblobs.push_back(track_blob);
    }

    //sk_debug("msblobs size:%lu\n", msblobs.size());

    the_blobs = msblobs;
    the_blobs_tocheck = dbl_chk_blobs;

    rect_vector::const_iterator r_itr = in_rects.begin();
    for (; r_itr != in_rects.end(); ++r_itr) {
    	//sk_debug("in_rect (%d,%d,%d,%d)\n", r_itr->x, r_itr->y, r_itr->width, r_itr->height);
    	// bool found = false, small_one = false;
        
        cv::Point r_itr_c = RECT_CENTER(*r_itr);
        bool found = false;
    	for (itr = the_blobs.begin(); itr != the_blobs.end(); ++itr) {
            cv::Rect nrect = itr->track_win & *r_itr;
            cv::Point itr_c = RECT_CENTER(itr->track_win);
    		if (nrect.area() > (itr->track_win.area() * 0.5f) ||
                abs(r_itr_c.x - itr_c.x) < abs(itr->x_var * MVAR_RANGE) ||
                abs(r_itr_c.y - itr_c.y) < abs(itr->y_var * MVAR_RANGE)) {
    			found = true;
    			break;
    		}
    	}
    	
        // if (!found || (found && small_one)) {
    	if (!found) {
    		out_rects.push_back(*r_itr);
    	}
    }

    //sk_debug("the_blobs=%lu, in_rects has %lu, out_rects has %lu\n", the_blobs.size(), in_rects.size(), out_rects.size());
} // ms_blob_tracker::process


#define EDGE_GAP 30
int ms_blob_tracker::track_faces(cv::Mat &image, const rect_vector &faces, vs_item_t &vs_itm) {
	if (faces.empty()) return 0;
    
    int edge_gap = std::min(std::min(vs_itm.rect.width, vs_itm.rect.height) / 5, EDGE_GAP);

	ms_blob_vector all_blobs = the_blobs;
	all_blobs.insert(all_blobs.end(), the_blobs_tocheck.begin(), the_blobs_tocheck.end());
    sk_debug("all_blobs size: %lu\n", all_blobs.size());

	//check if face exists
	rect_vector new_faces_noalert;
	rect_vector new_faces_alert;
	rect_vector::const_iterator itr = faces.begin();
	for (; itr != faces.end(); ++itr) {
		print_rect("check blob: ", *itr);
		bool found = false;
        cv::Point itr_c = RECT_CENTER(*itr);
        sk_debug("itr_c(%d, %d)\n", itr_c.x, itr_c.y);
        
		ms_blob_vector::iterator b_itr = all_blobs.begin();
        
		for (; b_itr != all_blobs.end(); ++b_itr) {
			print_rect("the existing blob: ", b_itr->track_win);
            sk_debug("the existing blob x_var:%d, y_var:%d\n", b_itr->x_var, b_itr->y_var);
            
            cv::Rect nrect = *itr & b_itr->track_win;
            cv::Point b_itr_c = RECT_CENTER(b_itr->track_win);
            sk_debug("b_itr_c(%d, %d)\n", b_itr_c.x, b_itr_c.y);
            
            if (nrect.area() > (b_itr->track_win.area() * 0.5f) ||
                abs(b_itr_c.x - itr_c.x) < abs(b_itr->x_var * MVAR_RANGE) ||
                abs(b_itr_c.y - itr_c.y) < abs(b_itr->y_var * MVAR_RANGE)) {
				found = true;
                b_itr->confidence = 1.0;
				break;
			}
		}
        
		if (!found) {
			sk_debug("Found new face to watch, direction: %d\n", vs_itm.direct);
			if (vs_itm.direct == -1) new_faces_alert.push_back(*itr);
			else {
				if (vs_itm.direct == right_direct) {
					if ((itr->x + itr->width/2) > (vs_itm.rect.width/2) ||
                        abs(itr->x + itr->width - vs_itm.rect.width) < edge_gap) {
						sk_debug("Found person entering from right\n");
						new_faces_alert.push_back(*itr);
					}
					else new_faces_noalert.push_back(*itr);
				}
				else if (vs_itm.direct == up_direct) {
					if ((itr->y + itr->height/2) < (vs_itm.rect.height/2) ||
                        abs(itr->y - vs_itm.rect.y) < edge_gap) {
						sk_debug("Found person entering from upside\n");
						new_faces_alert.push_back(*itr);
					}
					else new_faces_noalert.push_back(*itr);
				}
				else if (vs_itm.direct == left_direct) {
					if ((itr->x + itr->width/2) < (vs_itm.rect.width/2) ||
                        abs(itr->x - vs_itm.rect.x) < edge_gap) {
						sk_debug("Found person entering from left\n");
						new_faces_alert.push_back(*itr);
					}
					else new_faces_noalert.push_back(*itr);
				}
				else if (vs_itm.direct == down_direct) {
					if ((itr->y + itr->height/2) > (vs_itm.rect.height/2) ||
                        abs(itr->y + itr->height - vs_itm.rect.height) < edge_gap) {
						sk_debug("Found person entering from downside\n");
						new_faces_alert.push_back(*itr);
					}
					else new_faces_noalert.push_back(*itr);
				}
			}
		}
	}

	if (new_faces_noalert.empty() && new_faces_alert.empty()) return 0;

	// send message here
	if (zmq_push != NULL && !new_faces_alert.empty()) {
		//sk_debug("=======> Send welcome message\n");
		zstr_send(zmq_push, MSG_FACE);
	}


	cv::Mat hsv, hue, hist, mask;
	cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
	cv::inRange(hsv, cv::Scalar(0, smin, vmin), cv::Scalar(180, smax, vmax), mask);

    int ch[] = {0, 0};
	hue.create(hsv.size(), hsv.depth());
	cv::mixChannels(&hsv, 1, &hue, 1, ch, 1);

    std::vector<int> blob_ids;
	itr = new_faces_alert.begin();
	for (; itr != new_faces_alert.end(); ++itr) {
        cv::Mat roi(hue, *itr);
        cv::Mat maskroi(mask, *itr);
		//cv::calcHist(&roi, 1, 0, maskroi, hist, 1, &hist_size, &phranges);
        //cv::normalize(hist, hist, 0, 255, CV_MINMAX);
        hist = cv::Mat::zeros(hist_size, 1, CV_32FC1);
        //calc_kernel_hist(roi, maskroi, hist, kernel_size);
        calc_kernel_hist(roi, maskroi, hist);
        cv::normalize(hist, hist, 0, 255, CV_MINMAX);

		ms_track_blob blob = {next_blob_id++, *itr, hist, 1, 0, 0, 0};
		the_blobs.push_back(blob);
        blob_ids.push_back(blob.blob_id);
		sk_info("=========> Add New Person:%d to track, (%d,%d,%d,%d)\n", blob.blob_id, itr->x,itr->y,itr->width,itr->height);
	}
    sk_stats_sqlite::insert_blobs(blob_ids, new_face_type);

	itr = new_faces_noalert.begin();
	for (; itr != new_faces_noalert.end(); ++itr) {
		cv::Mat roi(hue, *itr);
		cv::Mat maskroi(mask, *itr);
		//cv::calcHist(&roi, 1, 0, maskroi, hist, 1, &hist_size, &phranges);
        //cv::normalize(hist, hist, 0, 255, CV_MINMAX);
        hist = cv::Mat::zeros(hist_size, 1, CV_32FC1);
        //calc_kernel_hist(roi, maskroi, hist, kernel_size);
        calc_kernel_hist(roi, maskroi, hist);
		cv::normalize(hist, hist, 0, 255, CV_MINMAX);

		//ms_track_blob blob = { -1, *itr, hist, 1, 0 };
		ms_track_blob blob = { 0, *itr, hist, 1, 0, 0, 0 };
		the_blobs.push_back(blob);
		sk_info("=========> Add Person:%d to track, (%d,%d,%d,%d)\n", blob.blob_id, itr->x, itr->y, itr->width, itr->height);
	}

    return int(new_faces_alert.size() + new_faces_noalert.size());
} // ms_blob_tracker::track_faces


/* add blobs to check to the_blobs_tocheck again */
void ms_blob_tracker::add_blobs_tocheck(ms_blob_vector &bs) {
    //std::vector<int> v;
    ms_blob_vector::const_iterator itr = bs.begin();
    for (; itr != bs.end(); ++itr) {
        //sk_debug("blob: %d, suspicious: %d\n", itr->blob_id, itr->suspicious);
        if (/*itr->confidence > 0.9 ||*/ itr->suspicious < MAX_SUSPICIOUS) {
            the_blobs_tocheck.push_back(*itr);
        }
        else {
			//if (itr->blob_id > 0) v.push_back(itr->blob_id);
            sk_info("We lost person: %d\n", itr->blob_id);
            //print_rect("lost person rect: ", itr->track_win);
        }
    }

    //sk_stats_sqlite::insert_blobs(v, lost_face_type);
} // ms_blob_tracker::add_blobs_tocheck


void ms_blob_tracker::clean() {
    sk_debug("++++++++ ms_blob_tracker clean\n");
    //the_blobs_tocheck.clear();
    
    ms_blob_vector bv2chk;
    ms_blob_vector::iterator itr = the_blobs_tocheck.begin();
    for (; itr != the_blobs_tocheck.end(); ++itr) {
        itr->suspicious += 3;
        if (itr->suspicious < MAX_SUSPICIOUS)
            bv2chk.push_back(*itr);
    }
    the_blobs_tocheck = bv2chk;
    
    the_blobs_tocheck.insert(the_blobs_tocheck.end(), the_blobs.begin(), the_blobs.end());    
    the_blobs.clear();
} // ms_blob_tracker::clean



