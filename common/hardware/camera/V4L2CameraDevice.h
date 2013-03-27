/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HW_EMULATOR_CAMERA_EMULATED_FAKE_CAMERA_DEVICE_H
#define HW_EMULATOR_CAMERA_EMULATED_FAKE_CAMERA_DEVICE_H

/*
 * Contains declaration of a class V4L2CameraDevice that encapsulates
 * a fake camera device.
 */

#include <ui/Rect.h>
#include "Converters.h"
#include "V4L2Camera.h"
#include "OSAL_Queue.h"
#include <type_camera.h>

namespace android {

class CameraHardwareDevice;

/* Encapsulates a fake camera device.
 * Fake camera device emulates a camera device by providing frames containing
 * a black and white checker board, moving diagonally towards the 0,0 corner.
 * There is also a green, or red square that bounces inside the frame, changing
 * its color when bouncing off the 0,0 corner.
 */
class V4L2CameraDevice : public V4L2Camera {
public:
    /* Constructs V4L2CameraDevice instance. */
    explicit V4L2CameraDevice(CameraHardwareDevice* camera_hal, int id);

    /* Destructs V4L2CameraDevice instance. */
    ~V4L2CameraDevice();

    /***************************************************************************
     * V4L2Camera device abstract interface implementation.
     * See declarations of these methods in V4L2Camera class for
     * information on each of these methods.
     **************************************************************************/

public:
    /* Connects to the camera device.
     * Since there is no real device to connect to, this method does nothing,
     * but changes the state.
     */
    status_t connectDevice();

    /* Disconnects from the camera device.
     * Since there is no real device to disconnect from, this method does
     * nothing, but changes the state.
     */
    status_t disconnectDevice();

    /* Starts the camera device. */
    status_t startDevice(int width, int height, uint32_t pix_fmt);

    /* Stops the camera device. */
    status_t stopDevice();

    /* Gets current preview fame into provided buffer. */
    status_t getPreviewFrame(void* buffer);

    /***************************************************************************
     * Worker thread management overrides.
     * See declarations of these methods in V4L2Camera class for
     * information on each of these methods.
     **************************************************************************/

protected:
    /* Implementation of the worker thread routine.
     * This method simply sleeps for a period of time defined by the FPS property
     * of the fake camera (simulating frame frequency), and then calls emulated
     * camera's onNextFrameAvailable method.
     */
    bool inWorkerThread();

	// -------------------------------------------------------------------------
	// extended interfaces here <***** star *****>
	// -------------------------------------------------------------------------
	
	class DoPreviewThread : public Thread {
        V4L2CameraDevice* mV4l2CameraDevice;
    public:
        DoPreviewThread(V4L2CameraDevice* dev) :
			Thread(false),
			mV4l2CameraDevice(dev) {
		}
        void startThread() {
			run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
		void stopThread() {
			LOGD("================ wait preview thread exit");
			requestExitAndWait();
			LOGD("================ preview thread exit ok");
        }
        virtual bool threadLoop() {
			return mV4l2CameraDevice->previewThread();
        }
    };

	class DoPictureThread : public Thread {
        V4L2CameraDevice* mV4l2CameraDevice;
    public:
        DoPictureThread(V4L2CameraDevice* dev) :
			Thread(false),
			mV4l2CameraDevice(dev) {
		}
		void startThread() {
			run("CameraPictrueThread", PRIORITY_URGENT_DISPLAY);
		}
		
		void stopThread() {
			LOGD("================ wait preview thread exit");
			requestExitAndWait();
			LOGD("================ preview thread exit ok");
        }
		virtual bool threadLoop() {
			return mV4l2CameraDevice->pictureThread();
		}
    };

public:

	bool previewThread();
	bool pictureThread();

	virtual status_t Initialize();
	int setV4L2DeviceName(char * pname);			// set device node name, such as "/dev/video0"
	int setV4L2DeviceID(int device_id);				// set different device id on the same CSI
	int tryFmt(int format);							// check if driver support this format
	int tryFmtSize(int * width, int * height);		// check if driver support this size
	int setFrameRate(int rate);						// set frame rate from camera.cfg
	int getFrameRate();								// get v4l2 device current frame rate
	int setCameraFacing(int facing);

	int setImageEffect(int effect);
	int setWhiteBalance(int wb);
	int setExposure(int exp);
	int setFlashMode(int mode);
	
	int enumSize(char * pSize, int len);
	int setAutoFocusMode(int af);
	int setAutoFocusCtrl(int af_ctrl, void * areas);
	int getAutoFocusStatus(int af_ctrl);
	
	void releasePreviewFrame(int index);			// Q buffer for encoder

	int getCurrentFaceFrame(void * frame);

	inline void setCrop(int new_zoom, int max_zoom)
	{
		mLastZoom = mNewZoom;
		mNewZoom = new_zoom;
		mMaxZoom = max_zoom;
	}

	inline int getCaptureFormat()
	{
		return mCaptureFormat;
	}

	inline void setHwEncoder(bool hw)
	{
		mUseHwEncoder = hw;
	}
	
private:
	int openCameraDev();
	void closeCameraDev();
	int v4l2SetVideoParams(int width, int height, uint32_t pix_fmt);
	int v4l2setCaptureParams(struct v4l2_streamparm * params);
	int v4l2ReqBufs();
	int v4l2QueryBuf();
	int v4l2StartStreaming(); 
	int v4l2StopStreaming(); 
	int v4l2UnmapBuf();

	int v4l2WaitCameraReady();
	int getPreviewFrame(v4l2_buffer *buf);
	
	void dealWithVideoFrameSW(V4L2BUF_t * pBuf);
	void dealWithVideoFrameHW(V4L2BUF_t * pBuf);
	void dealWithVideoFrameTest(V4L2BUF_t * pBuf);
	
	/* Checks if it's the time to push new frame to the preview window.
	 * Note that this method must be called while object is locked. */
	bool isPreviewTime();

#if USE_MP_CONVERT
	// use for YUYV to YUV420C
	void YUYVToYUV420C(const void* yuyv, void *yuv420, int width, int height);
#endif

private:
	// -------------------------------------------------------------------------
	// private data
	// -------------------------------------------------------------------------
	
	// camera id
	int								mCameraID;
	
	// v4l2 device handle
	int								mCamFd; 

	// device node name
	char							mDeviceName[16];

	// device id on the CSI, used when two camera device shared with one CSI
	int								mDeviceID;

	int								mFrameRate;

	// camera facing back / front
	int								mCameraFacing;

	typedef struct v4l2_mem_map_t{
		void *	mem[NB_BUFFER]; 
		int 	length;
	}v4l2_mem_map_t;
	v4l2_mem_map_t					mMapMem;

	// actually buffer counts
	int								mBufferCnt;

	// HW preview failed, should use SW preview
	bool							mPreviewUseHW;
	
	/* Timestamp (abs. microseconds) when last frame has been pushed to the
     * preview window. */
    uint64_t                        mLastPreviewed;

    /* Preview frequency in microseconds. */
    uint32_t                        mPreviewAfter;

	bool							mUseHwEncoder;

	typedef struct bufferManagerQ_t
	{
		int			buf_vir_addr[NB_BUFFER];
		int			buf_phy_addr[NB_BUFFER];
		int			write_id;
		int			read_id;
		int			buf_unused;
	}bufferManagerQ_t;

	pthread_mutex_t					mTakePhotoMutex;
	pthread_cond_t					mTakePhotoCond;
	
	Rect							mRectCrop;
	int								mNewZoom;
	int								mLastZoom;
	int								mMaxZoom;

	int								mCaptureFormat;		// for usb camera
	int								mVideoFormat;		// for usb camera
	bufferManagerQ_t				mVideoBuffer;		// for usb camera
	int                             mTakingPictureFrame;//for usb camera

#if USE_MP_CONVERT
	int 							mG2DHandle;
#endif

	OSAL_QUEUE						mQueueBuffer;
	V4L2BUF_t						mV4l2buf[NB_BUFFER];

	sp<DoPreviewThread>				mPreviewThread;
	sp<DoPictureThread>				mPictureThread;

	V4L2BUF_t *						mCurrentV4l2buf;
	bool							mFaceDetectionEnable;

	pthread_mutex_t 				mPreviewMutex;
	pthread_cond_t					mPreviewCond;
};	

}; /* namespace android */

#endif  /* HW_EMULATOR_CAMERA_EMULATED_FAKE_CAMERA_DEVICE_H */
