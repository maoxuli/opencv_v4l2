/*
 * opencv_v4l2 - opencv_main.cpp file
 *
 * Copyright (c) 2017-2018, e-con Systems India Pvt. Ltd.  All rights reserved.
 *
 */

#include <opencv2/opencv.hpp>
#include <iostream>
#include <sys/time.h>
#include <boost/thread.hpp>

#include <chrono> 
using namespace std::chrono; 

using namespace std;
using namespace cv;

unsigned int GetTickCount()
{
    struct timeval tv;
    if(gettimeofday(&tv, NULL) != 0)
            return 0;
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// recording thread 
bool _stop = false;
boost::thread _thread;

boost::mutex _mutex; 
cv::Mat _frame; 
bool _updated = false; 

cv::VideoWriter writer; 

void ThreadFunc() 
{
    unsigned int start, end, fps = 0; 
    start = GetTickCount();
    while (!_stop)
    {
        {
            boost::mutex::scoped_lock lock(_mutex);
            if (_updated)
            {
                // auto T1 = high_resolution_clock::now(); 
                writer << _frame; // save a new frame to file 
                // auto T2 = high_resolution_clock::now(); 
                //writer << frame; // save a new frame to file 
                // auto D2 = duration_cast<milliseconds>(T2 - T1); 
                // cout << "record: " << D2.count() << endl;
                _updated = false;
                fps++;
            }
        }
        end = GetTickCount();
        if ((end - start) >= 1000) {
            cout << "record fps = " << fps << endl ;
            fps = 0;
            start = end;
        }
        boost::this_thread::sleep_for(boost::chrono::milliseconds(10));
    }
}

int main(int argc, char **argv)
{
    unsigned int width, height, framerate, id;
    if (argc == 4)
    {
        /*
        * Courtesy: https://stackoverflow.com/a/2797823
        */
        string width_str = argv[1];
        string height_str = argv[2];
        string framerate_str = argv[3];
        string id_str = argv[4];
        try {
            size_t pos;
            width = stoi(width_str, &pos);
            if (pos < width_str.size()) {
                cerr << "Trailing characters after width: " << width_str << '\n';
            }

            height = stoi(height_str, &pos);
            if (pos < height_str.size()) {
                cerr << "Trailing characters after height: " << height_str << '\n';
            }

            framerate = stoi(framerate_str, &pos);
            if (pos < framerate_str.size()) {
                cerr << "Trailing characters after framerate: " << framerate_str << '\n';
            }

            id = stoi(id_str, &pos);
            if (pos < id_str.size()) {
                cerr << "Trailing characters after id: " << id_str << '\n';
            }
        } catch (invalid_argument const &ex) {
            cerr << "Invalid width, height, framerate, or camera ID\n";
            return EXIT_FAILURE;
        } catch (out_of_range const &ex) {
            cerr << "Width, Height, Framerate, or camera ID out of range\n";
            return EXIT_FAILURE;
        }
    }
    else
    {
        cout << "Note: This program accepts (only) four arguments.\n";
        cout << "First arg: width, Second arg: height, third arg: framerate, fourth arg: camera_id\n";
        cout << "No arguments given. Assuming default values. width: 1920; height: 1080; framerate: 30; camera_id: 0\n";
        width = 1920;
        height = 1080;
        framerate = 30;
        id = 0; 
    }

    int capture_width = width;
    int capture_height = height;
    int capture_framerate = framerate;
    int camera_id = id;
    std::string input_pipline = "nvarguscamerasrc ! video/x-raw(memory:NVMM), sensor_id=(int)" + std::to_string(camera_id) + ", " +
                                "width=(int)" + std::to_string(capture_width) + ", " +
                                "height=(int)" + std::to_string(capture_height) + ", format=(string)NV12, " + 
                                "framerate=(fraction)" + std::to_string(capture_framerate) + "/1 ! " + 
                                "nvvidconv flip-method=0 ! video/x-raw, format=(string)BGRx ! " +  
                                "videoconvert ! video/x-raw, format=(string)BGR ! appsink";
    cv::VideoCapture cap(input_pipline, cv::CAP_GSTREAMER);

    if(!cap.isOpened())  // check if we succeeded
    {
        std::cerr << "Failed to open camera: " << camera_id << std::endl; 
        return EXIT_FAILURE;
    }

    capture_width = cap.get(CAP_PROP_FRAME_WIDTH);
    capture_height = cap.get(CAP_PROP_FRAME_HEIGHT);
    cout << "Camera: " << camera_id << " Width: " << capture_width << " Height: " << capture_height << '\n';
    cout << "FPS: " << cap.get(CAP_PROP_FPS) << '\n';

    std::string output_pipeline = std::string("appsrc ! video/x-raw, format=(string)BGR ! videoconvert ! video/x-raw, format=BGRx ! ") + 
                                  "nvvidconv ! video/x-raw(memory:NVMM), format=(string)I420 ! " + 
                                  //"nvvidconv ! video/x-raw(memory:NVMM), format=(string)NV12 ! " +
                                  "omxh264enc ! matroskamux ! queue ! " +
                                  "filesink location=output_" + std::to_string(camera_id) + ".mkv";

    // std::string output_pipeline = "appsrc ! videoconvert ! omxh264enc ! mpegtsmux ! filesink location=output.ts"; 
    int codec = cv::VideoWriter::fourcc('X', '2', '6', '4'); 
    writer.open(output_pipeline, cv::CAP_GSTREAMER, codec, (double)capture_framerate, cv::Size(capture_width, capture_height)); 

    if(!writer.isOpened())  // check if we succeeded
    {
        std::cerr << "Failed to open file for camera: " << camera_id << std::endl; 
        return EXIT_FAILURE;
    }

#ifdef ENABLE_DISPLAY
    /*
        * Using a window with OpenGL support to display the frames improves the performance
        * a lot. It is essential for achieving better performance.
        */
    #ifdef ENABLE_GL_DISPLAY
    namedWindow("preview", WINDOW_OPENGL);
    #else
    namedWindow("preview");
    #endif
    cout << "Note: Click 'Esc' key to exit the window.\n";
#endif

    /*
    * Re-using the frame matrix(ces) instead of creating new ones (i.e., declaring 'Mat frame'
    * (and cuda::GpuMat gpu_frame) outside the 'while (1)' loop instead of declaring it
    * within the loop) improves the performance for higher resolutions.
    */
    Mat frame;
#if defined(ENABLE_DISPLAY) && defined(ENABLE_GL_DISPLAY) && defined(ENABLE_GPU_UPLOAD)
    cuda::GpuMat gpu_frame;
#endif

    // start to capture image in a separate thread 
    _thread = boost::thread(ThreadFunc);

    unsigned int start, end , fps = 0;
    start = GetTickCount();
    while (1) 
    {
        cap >> frame; // get a new frame from camera
        if (frame.empty())
        {
            cerr << "Empty frame received from camera!\n";
            return EXIT_FAILURE;
        }
        {
            boost::mutex::scoped_lock lock(_mutex);
            _frame = frame.clone(); 
            _updated = true;
        }
        // auto T1 = high_resolution_clock::now(); 
        // auto D1 = duration_cast<milliseconds>(T1 - T0); 
        // cout << "capture: " << D1.count() << endl;

    #ifdef ENABLE_DISPLAY
        /*
        * It is possible to use a GpuMat for display (imshow) only
        * when window is created with OpenGL support. So,
        * ENABLE_GL_DISPLAY is also required for gpu_frame.
        *
        * Ref: https://docs.opencv.org/3.4.2/d7/dfc/group__highgui.html#ga453d42fe4cb60e5723281a89973ee563
        */
        #if defined(ENABLE_GL_DISPLAY) && defined(ENABLE_GPU_UPLOAD)
        /*
        * Uploading the frame matrix to a cv::cuda::GpuMat and using it to display (via cv::imshow) also
        * contributes to better and consistent performance.
        */
        gpu_frame.upload(frame);
        imshow("preview", gpu_frame);
        #else
        imshow("preview", frame);
        #endif
        if(waitKey(1) == 27) break;
    #endif
        fps++;
        end = GetTickCount();
        if ((end - start) >= 1000) {
            cout << "capture fps = " << fps << endl ;
            fps = 0;
            start = end;
        }
    }

    _stop = true;
    _thread.join(); 

    // the camera will be deinitialized automatically in VideoCapture destructor
    return 0;
}
