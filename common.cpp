#include "common.h"

////////////////////////////////////////////////////////////////////////////////
//
// OpenCV stuff
//

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <vector>
#include <iostream>
#include <unistd.h>

using namespace cv;

std::vector<Point2f> src;
std::vector<Point2f> dst;

// im: identity matrix
Mat im = (Mat_<float>(3,3) << 1280.0/1920.0, 0, 0, 0, 720.0/1080.0, 0, 0, 0, 1 );
// pm: perspective matrix
Mat pm = im;

void opencv_init() {

  cv::FileStorage file("perspective.xml", cv::FileStorage::READ);
  file["perspective"] >> pm;
  if (!file.isOpened()) pm = im;
}

Mat calcPerspective() {

  Mat result;

  dst.push_back(Point2f(   0,  0));
  dst.push_back(Point2f(1280,  0));
  dst.push_back(Point2f(1280,720));
  dst.push_back(Point2f(   0,720));

  result = getPerspectiveTransform(src,dst);

  cv::FileStorage file("perspective.xml", cv::FileStorage::WRITE);
  file << "perspective" << result;

  src.clear();
  dst.clear();

  return result;
}

////////////////////////////////////////////////////////////////////////////////
//
// plane model stuff
//

#include <Eigen/Core>
#include <SimpleRansac.h>
#include <PlaneModel.h>

PlaneModel<float> plane;

////////////////////////////////////////////////////////////////////////////////
//
// GStreamer stuff
//

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/navigation.h>

#include <stdint.h>
#include <string.h>

#include <immintrin.h>

bool find_plane = true;
bool filter = true;
bool *quit;

float distance = 1;

char* gstpipe = NULL;

GstElement *gpipeline, *appsrc, *conv, *videosink;

gboolean pad_event(GstPad *pad, GstObject *parent, GstEvent *event) {

  if (GST_EVENT_TYPE (event) != GST_EVENT_NAVIGATION)
    return gst_pad_event_default(pad,parent,event);

  double x,y; int b;
  const gchar* key;

  switch (gst_navigation_event_get_type(event)) {

    // calibration: top (left, right), bottom (left, right)
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
      gst_navigation_event_parse_mouse_button_event(event,&b,&x,&y);
      src.push_back(Point2f(1920.0*x/1280.0,1080.0*y/720.0));
      break;

    case GST_NAVIGATION_EVENT_KEY_PRESS:
      gst_navigation_event_parse_key_event(event,&key);

      // reset perspective matrix
      if (key == std::string("space"))
        pm = im;

      // find largest plane
      if (key == std::string("p"))
        find_plane = true;

      // subtract plane
      if (key == std::string("f"))
        filter = !filter;

      // quit
      if (key == std::string("q"))
        *quit = true;

      // change plane distance threshold
      if (key == std::string( "plus")) distance += 0.2;
      if (key == std::string("minus")) distance -= 0.2;

      std::cout << "current distance: " << distance << std::endl;

      break;

    default:
      return false;
  }

  if (src.size() == 4) {
    Mat r = calcPerspective();
    pm = r; //set_matrix(perspective,r.ptr<gdouble>(0));
  }

  return true;
}

void gstreamer_init(gint argc, gchar *argv[], const char* type) {

  /* init GStreamer */
  gst_init (&argc, &argv);

  /* setup pipeline */
  gpipeline = gst_pipeline_new ("pipeline");
  appsrc = gst_element_factory_make ("appsrc", "source");

  // attach event listener to appsrc pad
  GstPad* srcpad = gst_element_get_static_pad (appsrc, "src");
  gst_pad_set_event_function( srcpad, (GstPadEventFunction)pad_event );

  // create pipeline from string
  const char* pipe_desc = gstpipe ? gstpipe : "videoconvert ! fpsdisplaysink sync=false";
  std::cout << "creating pipeline: " << pipe_desc << std::endl;
  videosink = gst_parse_bin_from_description(pipe_desc,TRUE,NULL);

  /* setup */
  g_object_set (G_OBJECT (appsrc), "caps",
    gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, type,
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
      "framerate", GST_TYPE_FRACTION, 0, 1,
    NULL), NULL);
  gst_bin_add_many (GST_BIN (gpipeline), appsrc, videosink, NULL);
  gst_element_link_many (appsrc, videosink, NULL);

  /* setup appsrc */
  g_object_set (G_OBJECT (appsrc),
    "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
    "format", GST_FORMAT_TIME,
    "is-live", TRUE,
    "block", TRUE,
    "do-timestamp", TRUE,
    NULL);

  /* play */
  gst_element_set_state (gpipeline, GST_STATE_PLAYING);
}

void buffer_destroy(gpointer data) {
  cv::Mat* done = (cv::Mat*)data;
  delete done;
}

GstFlowReturn prepare_buffer(guint size, gpointer data, void* frame) {

  GstBuffer *buffer = gst_buffer_new_wrapped_full( (GstMemoryFlags)0, data, size, 0, size, frame, buffer_destroy );

  return gst_app_src_push_buffer((GstAppSrc*)appsrc, buffer);
}

void gstreamer_cleanup() {
  /* clean up */
  gst_element_set_state (gpipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (gpipeline));
}
