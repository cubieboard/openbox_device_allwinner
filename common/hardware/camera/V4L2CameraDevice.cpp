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

/*
 * Contains implementation of a class V4L2CameraDevice that encapsulates
 * fake camera device.
 */
// #define LOG_NDEBUG 0
#define LOG_TAG "V4L2CameraDevice"
#include "CameraDebug.h"

#include <fcntl.h> 
#include <sys/mman.h> 
#include <videodev2.h>
#include <linux/videodev.h> 
#include <g2d_driver.h>

#include "CameraHardwareDevice.h"
#include "V4L2CameraDevice.h"

namespace android {
	
static void calculateCrop(Rect * rect, int new_zoom, int max_zoom, int width, int height)
{
	if (max_zoom == 0)
	{
		rect->left		= 0;
		rect->top		= 0;
		rect->right 	= width;
		rect->bottom	= height;
	}
	else
	{
		int new_ratio = (new_zoom * 2 * 100 / max_zoom + 100);
		rect->left		= (width - (width * 100) / new_ratio)/2;
		rect->top		= (height - (height * 100) / new_ratio)/2;
		rect->right 	= rect->left + (width * 100) / new_ratio;
		rect->bottom	= rect->top  + (height * 100) / new_ratio;
	}
	
	// LOGD("crop: [%d, %d, %d, %d]", rect->left, rect->top, rect->right, rect->bottom);
}

static void YUYVToNV12(const void* yuyv, void *nv12, int width, int height)
{
	uint8_t* Y	= (uint8_t*)nv12;
	uint8_t* UV = (uint8_t*)Y + width * height;
	
	for(int i = 0; i < height; i += 2)
	{
		for (int j = 0; j < width; j++)
		{
			*(uint8_t*)((uint8_t*)Y + i * width + j) = *(uint8_t*)((uint8_t*)yuyv + i * width * 2 + j * 2);
			*(uint8_t*)((uint8_t*)Y + (i + 1) * width + j) = *(uint8_t*)((uint8_t*)yuyv + (i + 1) * width * 2 + j * 2);
			*(uint8_t*)((uint8_t*)UV + ((i * width) >> 1) + j) = *(uint8_t*)((uint8_t*)yuyv + i * width * 2 + j * 2 + 1);
		}
	}
}

static void YUYVToNV21(const void* yuyv, void *nv21, int width, int height)
{
	uint8_t* Y	= (uint8_t*)nv21;
	uint8_t* VU = (uint8_t*)Y + width * height;
	
	for(int i = 0; i < height; i += 2)
	{
		for (int j = 0; j < width; j++)
		{
			*(uint8_t*)((uint8_t*)Y + i * width + j) = *(uint8_t*)((uint8_t*)yuyv + i * width * 2 + j * 2);
			*(uint8_t*)((uint8_t*)Y + (i + 1) * width + j) = *(uint8_t*)((uint8_t*)yuyv + (i + 1) * width * 2 + j * 2);

			if (j % 2)
			{
				if (j < width - 1)
				{
					*(uint8_t*)((uint8_t*)VU + ((i * width) >> 1) + j) = *(uint8_t*)((uint8_t*)yuyv + i * width * 2 + (j + 1) * 2 + 1);
				}
			}
			else
			{
				if (j > 1)
				{
					*(uint8_t*)((uint8_t*)VU + ((i * width) >> 1) + j) = *(uint8_t*)((uint8_t*)yuyv + i * width * 2 + (j - 1) * 2 + 1); 		
				}
			}
		}
	}
}

#if USE_MP_CONVERT
void V4L2CameraDevice::YUYVToYUV420C(const void* yuyv, void *yuv420, int width, int height)
{
	g2d_blt		blit_para;
	int 		err;
	
	blit_para.src_image.addr[0]      = (int)yuyv;
	blit_para.src_image.addr[1]      = (int)yuyv + width * height;
	blit_para.src_image.w            = width;	      /* src buffer width in pixel units */
	blit_para.src_image.h            = height;	      /* src buffer height in pixel units */
	blit_para.src_image.format       = G2D_FMT_IYUV422;
	blit_para.src_image.pixel_seq    = G2D_SEQ_NORMAL;          /* not use now */
	blit_para.src_rect.x             = 0;						/* src rect->x in pixel */
	blit_para.src_rect.y             = 0;						/* src rect->y in pixel */
	blit_para.src_rect.w             = width;			/* src rect->w in pixel */
	blit_para.src_rect.h             = height;			/* src rect->h in pixel */

	blit_para.dst_image.addr[0]      = (int)yuv420;
	blit_para.dst_image.addr[1]      = (int)yuv420 + width * height;
	blit_para.dst_image.w            = width;	      /* dst buffer width in pixel units */			
	blit_para.dst_image.h            = height;	      /* dst buffer height in pixel units */
	blit_para.dst_image.format       = G2D_FMT_PYUV420UVC;
	blit_para.dst_image.pixel_seq    = (mVideoFormat == V4L2_PIX_FMT_NV12) ? G2D_SEQ_NORMAL : G2D_SEQ_VUVU;          /* not use now */
	blit_para.dst_x                  = 0;					/* dst rect->x in pixel */
	blit_para.dst_y                  = 0;					/* dst rect->y in pixel */
	blit_para.color                  = 0xff;          		/* fix me*/
	blit_para.alpha                  = 0xff;                /* globe alpha */ 

	blit_para.flag = G2D_BLT_NONE; // G2D_BLT_FLIP_HORIZONTAL;

	err = ioctl(mG2DHandle , G2D_CMD_BITBLT ,(unsigned long)&blit_para);				
	if(err < 0) 	
	{			
		LOGE("ioctl, G2D_CMD_BITBLT failed");
		return;
	}
}
#endif

V4L2CameraDevice::V4L2CameraDevice(CameraHardwareDevice* camera_hal, int id)
    : V4L2Camera(camera_hal),
      mCameraID(id),
      mCamFd(0),
      mDeviceID(-1),
      mCameraFacing(0),
      mBufferCnt(NB_BUFFER),
      mPreviewUseHW(false),
      mLastPreviewed(0),
      mPreviewAfter(0),
      mUseHwEncoder(false),
	  mNewZoom(0),
	  mLastZoom(0),
	  mMaxZoom(0xffffffff),
	  mCaptureFormat(V4L2_PIX_FMT_NV21),
	  mVideoFormat(V4L2_PIX_FMT_NV21),
	  mTakingPictureFrame(0)
#if USE_MP_CONVERT
	  ,mG2DHandle(0)
#endif
	  ,mCurrentV4l2buf(NULL)
	  ,mFaceDetectionEnable(false)
{
	F_LOG;
	memset(mDeviceName, 0, sizeof(mDeviceName));
	
	pthread_mutex_init(&mTakePhotoMutex, NULL);
	pthread_cond_init(&mTakePhotoCond, NULL);

	memset(&mRectCrop, 0, sizeof(Rect));

	OSAL_QueueCreate(&mQueueBuffer, NB_BUFFER);
	
	pthread_mutex_init(&mPreviewMutex, NULL);
	pthread_cond_init(&mPreviewCond, NULL);
	
	mPreviewThread = new DoPreviewThread(this);
	mPictureThread = new DoPictureThread(this);

	mPreviewThread->startThread();
	mPictureThread->startThread();
}

V4L2CameraDevice::~V4L2CameraDevice()
{
	F_LOG;
	OSAL_QueueTerminate(&mQueueBuffer);

	if (mPreviewThread != NULL)
	{
		mPreviewThread->stopThread();
		pthread_cond_signal(&mPreviewCond);
		mPreviewThread.clear();
		mPreviewThread = 0;
	}

	if (mPictureThread != NULL)
	{
		mPictureThread->stopThread();
		pthread_cond_signal(&mTakePhotoCond);
		mPictureThread.clear();
		mPictureThread = 0;
	}

	pthread_mutex_destroy(&mPreviewMutex);
	pthread_cond_destroy(&mPreviewCond);
	pthread_mutex_destroy(&mTakePhotoMutex);
	pthread_cond_destroy(&mTakePhotoCond);
}

// 
status_t V4L2CameraDevice::Initialize()
{
	F_LOG;

	return V4L2Camera::Initialize();
}


/****************************************************************************
 * V4L2Camera device abstract interface implementation.
 ***************************************************************************/

status_t V4L2CameraDevice::connectDevice()
{
	F_LOG;

    Mutex::Autolock locker(&mObjectLock);
    if (!isInitialized()) {
        LOGE("%s: Fake camera device is not initialized.", __FUNCTION__);
        return EINVAL;
    }
    if (isConnected()) {
        LOGW("%s: Fake camera device is already connected.", __FUNCTION__);
        return NO_ERROR;
    }

	// open v4l2 camera device
	int ret = openCameraDev();
	if (ret != OK)
	{
		return ret;
	}
	
#if USE_MP_CONVERT
	// open MP driver
	mG2DHandle = open("/dev/g2d", O_RDWR, 0);
	if (mG2DHandle < 0)
	{
		LOGE("open /dev/g2d failed");
		return -1;
	}
	LOGV("open /dev/g2d OK");
#endif 

	ret = cedarx_hardware_init(2);// CEDARX_HARDWARE_MODE_VIDEO
	if (ret < 0)
	{
		LOGE("cedarx_hardware_init failed");
		return -1;
	}
	LOGV("cedarx_hardware_init ok");

    /* There is no device to connect to. */
    mState = ECDS_CONNECTED;

    return NO_ERROR;
}

status_t V4L2CameraDevice::disconnectDevice()
{
	F_LOG;

    Mutex::Autolock locker(&mObjectLock);
    if (!isConnected()) {
        LOGW("%s: Fake camera device is already disconnected.", __FUNCTION__);
        return NO_ERROR;
    }
    if (isStarted()) {
        LOGE("%s: Cannot disconnect from the started device.", __FUNCTION__);
        return EINVAL;
    }

	// close v4l2 camera device
	closeCameraDev();
	
#if USE_MP_CONVERT
	if(mG2DHandle != NULL)
	{
		close(mG2DHandle);
		mG2DHandle = NULL;
	}
#endif

	int ret = cedarx_hardware_exit(2);// CEDARX_HARDWARE_MODE_VIDEO
	if (ret < 0)
	{
		LOGE("cedarx_hardware_exit failed\n");
	}
	LOGD("cedarx_hardware_exit ok");

    /* There is no device to disconnect from. */
    mState = ECDS_INITIALIZED;

    return NO_ERROR;
}

status_t V4L2CameraDevice::startDevice(int width,
                                       int height,
                                       uint32_t pix_fmt)
{
	LOGV("%s, wxh: %dx%d, fmt: %d", __FUNCTION__, width, height, pix_fmt);

    Mutex::Autolock locker(&mObjectLock);
    if (!isConnected()) {
        LOGE("%s: Fake camera device is not connected.", __FUNCTION__);
        return EINVAL;
    }
    if (isStarted()) {
        LOGE("%s: Fake camera device is already started.", __FUNCTION__);
        return EINVAL;
    }
	
	mVideoFormat = pix_fmt;

	mCurrentV4l2buf = NULL;
	mFaceDetectionEnable = false;

	// set capture mode
	struct v4l2_streamparm params;
  	params.parm.capture.timeperframe.numerator = 1;
	params.parm.capture.timeperframe.denominator = mFrameRate;
	params.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (mTakingPicture)
	{
		params.parm.capture.capturemode = V4L2_MODE_IMAGE;
	}
	else
	{
		params.parm.capture.capturemode = V4L2_MODE_VIDEO;
	}
	v4l2setCaptureParams(&params);

	// set v4l2 device parameters
	v4l2SetVideoParams(width, height, pix_fmt);

	// set fps
	v4l2setCaptureParams(&params);
	
	// v4l2 request buffers
	v4l2ReqBufs();

	// v4l2 query buffers
	v4l2QueryBuf();
	
	// stream on the v4l2 device
	v4l2StartStreaming();

    /* Initialize the base class. */
    const status_t res =
        V4L2Camera::commonStartDevice(mFrameWidth, mFrameHeight, pix_fmt);
    if (res == NO_ERROR) {
		mState = ECDS_STARTED;
    }
	
	mPreviewAfter = 1000000 / 40;
	if (mFrameWidth * mFrameHeight > 640 * 480)
	{
		mPreviewAfter = 1000000 / 27;
	}

/*
	if (mFrameWidth * mFrameHeight > 640 * 480)
	{
		LOGD("use hw preview: %d x %d", mFrameWidth, mFrameHeight);
		mPreviewUseHW = true;
	}
*/	
    return res;
}

status_t V4L2CameraDevice::stopDevice()
{
	LOGV("stopDevice");

    Mutex::Autolock locker(&mObjectLock);
    if (!isStarted()) {
        LOGW("%s: camera device is not started.", __FUNCTION__);
        return NO_ERROR;
    }
	
	mFaceDetectionEnable = false;
		
	// v4l2 device stop stream
	v4l2StopStreaming();

    V4L2Camera::commonStopDevice();

	mCurrentV4l2buf = NULL;

	// v4l2 device unmap buffers
    v4l2UnmapBuf();

	mPreviewUseHW = false;

	for(int i = 0; i < NB_BUFFER; i++)
	{
		memset(&mV4l2buf[i], 0, sizeof(V4L2BUF_t));
	}
	
    mState = ECDS_CONNECTED;

    return NO_ERROR;
}

/****************************************************************************
 * Worker thread management overrides.
 ***************************************************************************/

int V4L2CameraDevice::v4l2WaitCameraReady()
{
	fd_set fds;		
	struct timeval tv;
	int r;

	FD_ZERO(&fds);
	FD_SET(mCamFd, &fds);		
	
	/* Timeout */
	tv.tv_sec  = 2;
	tv.tv_usec = 0;
	
	r = select(mCamFd + 1, &fds, NULL, NULL, &tv);
	if (r == -1) 
	{
		LOGE("select err");
		return -1;
	} 
	else if (r == 0) 
	{
		LOGE("select timeout");
		return -1;
	}

	return 0;
}

bool V4L2CameraDevice::inWorkerThread()
{
	/* Wait till FPS timeout expires, or thread exit message is received. */
	int ret = v4l2WaitCameraReady();
	if (ret != 0)
	{
		LOGW("wait v4l2 buffer time out");
		return __LINE__;
	}

	// get one video frame
	struct v4l2_buffer buf;
	memset(&buf, 0, sizeof(v4l2_buffer));
	ret = getPreviewFrame(&buf);
	if (ret != OK)
	{
		usleep(10000);
		return ret;
	}
	
	/* Timestamp the current frame, and notify the camera HAL about new frame. */
	// mCurFrameTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);
	mCurFrameTimestamp = (int64_t)((int64_t)buf.timestamp.tv_usec + (((int64_t)buf.timestamp.tv_sec) * 1000000));

	calculateCrop(&mRectCrop, mNewZoom, mMaxZoom, mFrameWidth, mFrameHeight);
	mCameraHAL->setCrop(&mRectCrop, mNewZoom);

	if (mCaptureFormat == V4L2_PIX_FMT_YUYV)
	{
#if USE_MP_CONVERT
		YUYVToYUV420C((void*)buf.m.offset, 
					  (void*)mVideoBuffer.buf_phy_addr[buf.index],
					  mFrameWidth, 
					  mFrameHeight);
#else
		if (mVideoFormat == V4L2_PIX_FMT_NV21)
		{
			YUYVToNV21(mMapMem.mem[buf.index], 
					   (void*)mVideoBuffer.buf_vir_addr[buf.index], 
					   mFrameWidth, 
					   mFrameHeight);
		}
		else
		{
			YUYVToNV12(mMapMem.mem[buf.index], 
					   (void*)mVideoBuffer.buf_vir_addr[buf.index], 
					   mFrameWidth, 
					   mFrameHeight);
		}
#endif
	}

	// V4L2BUF_t for preview and HW encoder
	V4L2BUF_t v4l2_buf;
	if (mCaptureFormat == V4L2_PIX_FMT_YUYV)
	{
		v4l2_buf.addrPhyY		= mVideoBuffer.buf_phy_addr[buf.index]; 
		v4l2_buf.addrVirY		= mVideoBuffer.buf_vir_addr[buf.index]; 
	}
	else
	{
		v4l2_buf.addrPhyY		= buf.m.offset;
		v4l2_buf.addrVirY		= (unsigned int)mMapMem.mem[buf.index];
	}
	v4l2_buf.index				= buf.index;
	v4l2_buf.timeStamp			= mCurFrameTimestamp;
	v4l2_buf.width				= mFrameWidth;
	v4l2_buf.height				= mFrameHeight;
	v4l2_buf.crop_rect.left		= mRectCrop.left;
	v4l2_buf.crop_rect.top		= mRectCrop.top;
	v4l2_buf.crop_rect.width	= mRectCrop.right - mRectCrop.left;
	v4l2_buf.crop_rect.height	= mRectCrop.bottom - mRectCrop.top;
	v4l2_buf.format				= mVideoFormat;

	// LOGV("DQBUF: addrPhyY: %x, id: %d, time: %lld", v4l2_buf.addrPhyY, buf.index, mCurFrameTimestamp);
	
	memcpy(&mV4l2buf[v4l2_buf.index], &v4l2_buf, sizeof(V4L2BUF_t));
	mCurrentV4l2buf = &mV4l2buf[v4l2_buf.index];
	
	if (mTakingPicture)
	{
		
		if (mCaptureFormat == V4L2_PIX_FMT_YUYV)
		{
			mTakingPictureFrame ++;
			if(mTakingPictureFrame == 2)
			{
				pthread_cond_signal(&mTakePhotoCond);
				return false;
			}
			releasePreviewFrame(v4l2_buf.index);
			return true;
		}
		else
		{
			pthread_cond_signal(&mTakePhotoCond);
			return false;
		}
		
	}
	else
	{
		ret = OSAL_Queue(&mQueueBuffer, &mV4l2buf[v4l2_buf.index]);
		if (ret != 0)
		{
			LOGW("queue full");
			releasePreviewFrame(v4l2_buf.index);
		}
		
		pthread_mutex_lock(&mPreviewMutex);
		pthread_cond_signal(&mPreviewCond);
		mFaceDetectionEnable = true;
		pthread_mutex_unlock(&mPreviewMutex);
	}
	
    return true;
}

bool V4L2CameraDevice::pictureThread()
{
	pthread_mutex_lock(&mTakePhotoMutex);
	pthread_cond_wait(&mTakePhotoCond, &mTakePhotoMutex);
	int64_t lasttime = systemTime();
	mCameraHAL->onTakingPicture(mCurrentV4l2buf, this, true);
	int64_t nowtime = systemTime();
	LOGV("taking picture use time : %lld(ms)", (nowtime - lasttime)/1000000);
	lasttime = nowtime;
	pthread_mutex_unlock(&mTakePhotoMutex);
	
	pthread_mutex_lock(&mTakePhotoEndMutex);
	mTakingPicture = false;
	pthread_cond_signal(&mTakePhotoEndCond);
	pthread_mutex_unlock(&mTakePhotoEndMutex);
	mTakingPictureFrame = 0;
	setThreadRunning(true);
	
	return true;
}

bool V4L2CameraDevice::previewThread()
{
	bool ret = false;
	V4L2BUF_t * pbuf = (V4L2BUF_t *)OSAL_Dequeue(&mQueueBuffer);
	if (pbuf == NULL)
	{
		// LOGD("OSAL_Dequeue no buffer, sleep...");
		pthread_cond_wait(&mPreviewCond, &mPreviewMutex);
		return true;
	}

    Mutex::Autolock locker(&mObjectLock);
	if (mMapMem.mem[pbuf->index] == NULL)
	{
		return true;
	}

	if (mVideoFormat != pbuf->format)
	{
		LOGW("format do not match, discard this frame");
		releasePreviewFrame(pbuf->index);
		return true;
	}

	if ((pbuf->width != mFrameWidth)
		|| (pbuf->height!= mFrameHeight))
	{
		LOGW("size do not match, discard this frame");
		releasePreviewFrame(pbuf->index);
		return true;
	}

	// callback
	if (mUseHwEncoder)
	{
		mCameraHAL->onNextFrameCB(pbuf, mCurFrameTimestamp, this, true);
	}
	else
	{
		mCameraHAL->onNextFrameCB((void*)pbuf->addrVirY, mCurFrameTimestamp, this, false);
	}
	
	// preview
	if (mPreviewUseHW)
	{
		ret = mCameraHAL->onNextFramePreview((void*)pbuf, pbuf->format, mCurFrameTimestamp, this, true);
		if (!ret)
		{
			releasePreviewFrame(pbuf->index);
			mPreviewUseHW = false;
			return true;
		}
	}
	else
	{
		if (isPreviewTime())
		{
			// mCameraHAL->onNextFramePreview(mMapMem.mem[pbuf->index], pbuf->format, mCurFrameTimestamp, this, false);
			mCameraHAL->onNextFramePreview((void*)pbuf->addrVirY, pbuf->format, mCurFrameTimestamp, this, false);
		}
	}
	
	releasePreviewFrame(pbuf->index);
	
	setThreadRunning(true);

	return true;
}

int V4L2CameraDevice::getCurrentFaceFrame(void * frame)
{
	if (frame == NULL)
	{
		LOGE("getCurrentFrame: error in null pointer");
		return -1;
	}

	if (mCurrentV4l2buf == NULL
		|| mCurrentV4l2buf->addrVirY == 0)
	{
		LOGW("preview thread dose not started");
		return -1;
	}
	
	pthread_mutex_lock(&mPreviewMutex);
	if (mFaceDetectionEnable)
	{
		memcpy(frame, (void*)mCurrentV4l2buf->addrVirY, mFrameWidth * mFrameHeight);
	}
	else
	{
		LOGW("face detection disable");
		pthread_mutex_unlock(&mPreviewMutex);
		return -1;
	}
	pthread_mutex_unlock(&mPreviewMutex);

	return 0;
}


// -----------------------------------------------------------------------------
// extended interfaces here <***** star *****>
// -----------------------------------------------------------------------------
int V4L2CameraDevice::openCameraDev()
{
	// open V4L2 device
	mCamFd = open(mDeviceName, O_RDWR | O_NONBLOCK, 0);
	if (mCamFd == -1) 
	{ 
        LOGE("ERROR opening %s: %s", mDeviceName, strerror(errno)); 
		return -1; 
	} 

	struct v4l2_input inp;
	inp.index = mDeviceID;
	if (-1 == ioctl (mCamFd, VIDIOC_S_INPUT, &inp))
	{
		LOGE("VIDIOC_S_INPUT error!");
		return -1;
	}

	// check v4l2 device capabilities
	int ret = -1;
	struct v4l2_capability cap; 
	ret = ioctl (mCamFd, VIDIOC_QUERYCAP, &cap); 
    if (ret < 0) 
	{ 
        LOGE("Error opening device: unable to query device."); 
        return -1; 
    } 

    if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) 
	{ 
        LOGE("Error opening device: video capture not supported."); 
        return -1; 
    } 
  
    if ((cap.capabilities & V4L2_CAP_STREAMING) == 0) 
	{ 
        LOGE("Capture device does not support streaming i/o"); 
        return -1; 
    } 
	
	// try to support this format: NV21, YUYV
	// we do not support mjpeg camera now
	if (tryFmt(V4L2_PIX_FMT_NV21) == OK)
	{
		mCaptureFormat = V4L2_PIX_FMT_NV21;
		LOGV("capture format: V4L2_PIX_FMT_NV21");
	}
	else if(tryFmt(V4L2_PIX_FMT_YUYV) == OK)
	{
		mCaptureFormat = V4L2_PIX_FMT_YUYV;		// maybe usb camera
		LOGV("capture format: V4L2_PIX_FMT_YUYV");
	}
	else
	{
		LOGE("driver should surpport NV21/NV12 or YUYV format, but it not!");
		return -1;
	}

	return OK;
}

void V4L2CameraDevice::closeCameraDev()
{
	F_LOG;
	
	if (mCamFd != NULL)
	{
		close(mCamFd);
		mCamFd = NULL;
	}
}

int V4L2CameraDevice::v4l2SetVideoParams(int width, int height, uint32_t pix_fmt)
{
	int ret = UNKNOWN_ERROR;
	struct v4l2_format format;

	LOGV("%s, line: %d, w: %d, h: %d, pfmt: %d", 
		__FUNCTION__, __LINE__, width, height, pix_fmt);
	
	memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    format.fmt.pix.width  = width; 
    format.fmt.pix.height = height; 
    if (mCaptureFormat == V4L2_PIX_FMT_YUYV)
	{
    	format.fmt.pix.pixelformat = mCaptureFormat; 
	}
	else
	{
		format.fmt.pix.pixelformat = pix_fmt; 
	}
	format.fmt.pix.field = V4L2_FIELD_NONE;
	
	ret = ioctl(mCamFd, VIDIOC_S_FMT, &format); 
	if (ret < 0) 
	{ 
		LOGE("VIDIOC_S_FMT Failed: %s", strerror(errno)); 
		return ret; 
	} 
	
	mFrameWidth = format.fmt.pix.width;
	mFrameHeight= format.fmt.pix.height;
	LOGV("camera params: w: %d, h: %d, pfmt: %d, pfield: %d", 
		mFrameWidth, mFrameHeight, pix_fmt, V4L2_FIELD_NONE);

	return OK;
}

int V4L2CameraDevice::v4l2ReqBufs()
{
	F_LOG;
	int ret = UNKNOWN_ERROR;
	struct v4l2_requestbuffers rb; 

	if (mTakingPicture)
	{
		mBufferCnt = 1;
	}
	else
	{
		mBufferCnt = NB_BUFFER;
	}

	LOGV("TO VIDIOC_REQBUFS count: %d", mBufferCnt);
	
	memset(&rb, 0, sizeof(rb));
    rb.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    rb.memory = V4L2_MEMORY_MMAP; 
    rb.count  = mBufferCnt; 
	
	ret = ioctl(mCamFd, VIDIOC_REQBUFS, &rb); 
    if (ret < 0) 
	{ 
        LOGE("Init: VIDIOC_REQBUFS failed: %s", strerror(errno)); 
		return ret;
    } 

	if (mBufferCnt != rb.count)
	{
		mBufferCnt = rb.count;
		LOGD("VIDIOC_REQBUFS count: %d", mBufferCnt);
	}

	return OK;
}

int V4L2CameraDevice::v4l2QueryBuf()
{
	F_LOG;
	int ret = UNKNOWN_ERROR;
	struct v4l2_buffer buf;
	
	for (int i = 0; i < mBufferCnt; i++) 
	{  
        memset (&buf, 0, sizeof (struct v4l2_buffer)); 
		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
		buf.memory = V4L2_MEMORY_MMAP; 
		buf.index  = i; 
		
		ret = ioctl (mCamFd, VIDIOC_QUERYBUF, &buf); 
        if (ret < 0) 
		{ 
            LOGE("Unable to query buffer (%s)", strerror(errno)); 
            return ret; 
        } 
 
        mMapMem.mem[i] = mmap (0, buf.length, 
                            PROT_READ | PROT_WRITE, 
                            MAP_SHARED, 
                            mCamFd, 
                            buf.m.offset); 
		mMapMem.length = buf.length;
		LOGV("index: %d, mem: %x, len: %x, offset: %x", i, (int)mMapMem.mem[i], buf.length, buf.m.offset);
 
        if (mMapMem.mem[i] == MAP_FAILED) 
		{ 
			LOGE("Unable to map buffer (%s)", strerror(errno)); 
            return -1; 
        } 

		// start with all buffers in queue
        ret = ioctl(mCamFd, VIDIOC_QBUF, &buf); 
        if (ret < 0) 
		{ 
            LOGE("VIDIOC_QBUF Failed"); 
            return ret; 
        } 

		int buffer_len = mFrameWidth * mFrameHeight * 3 / 2;
		mVideoBuffer.buf_vir_addr[i] = (int)cedara_phymalloc_map(buffer_len, 1024);
		mVideoBuffer.buf_phy_addr[i] = cedarv_address_vir2phy((void*)mVideoBuffer.buf_vir_addr[i]);
		mVideoBuffer.buf_phy_addr[i] |= 0x40000000;
		LOGV("video buffer: index: %d, vir: %x, phy: %x, len: %x", 
				i, mVideoBuffer.buf_vir_addr[i], mVideoBuffer.buf_phy_addr[i], buffer_len);
		
		memset((void*)mVideoBuffer.buf_vir_addr[i], 0x10, mFrameWidth * mFrameHeight);
		memset((void*)mVideoBuffer.buf_vir_addr[i] + mFrameWidth * mFrameHeight, 
				0x80, mFrameWidth * mFrameHeight / 2);
	} 

	return OK;
}

int V4L2CameraDevice::v4l2StartStreaming()
{
	F_LOG;
	int ret = UNKNOWN_ERROR; 
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
	
  	ret = ioctl (mCamFd, VIDIOC_STREAMON, &type); 
	if (ret < 0) 
	{ 
		LOGE("StartStreaming: Unable to start capture: %s", strerror(errno)); 
		return ret; 
	} 

	return OK;
}

int V4L2CameraDevice::v4l2StopStreaming()
{
	F_LOG;
	int ret = UNKNOWN_ERROR; 
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
	
	ret = ioctl (mCamFd, VIDIOC_STREAMOFF, &type); 
	if (ret < 0) 
	{ 
		LOGE("StopStreaming: Unable to stop capture: %s", strerror(errno)); 
		return ret; 
	} 
	LOGV("V4L2Camera::v4l2StopStreaming OK");

	return OK;
}

int V4L2CameraDevice::v4l2UnmapBuf()
{
	F_LOG;
	int ret = UNKNOWN_ERROR;
	
	for (int i = 0; i < mBufferCnt; i++) 
	{
		ret = munmap(mMapMem.mem[i], mMapMem.length);
        if (ret < 0) 
		{
            LOGE("v4l2CloseBuf Unmap failed"); 
			return ret;
		}

		cedara_phyfree_map((void*)mVideoBuffer.buf_vir_addr[i]);
		mVideoBuffer.buf_phy_addr[i] = 0;
	}
	mVideoBuffer.buf_unused = NB_BUFFER;
	mVideoBuffer.read_id = 0;
	mVideoBuffer.read_id = 0;
	
	return OK;
}

void V4L2CameraDevice::releasePreviewFrame(int index)
{
	int ret = UNKNOWN_ERROR;
	struct v4l2_buffer buf;
	
	memset(&buf, 0, sizeof(v4l2_buffer));
	buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    buf.memory = V4L2_MEMORY_MMAP; 
	buf.index = index;
	
	// LOGV("r ID: %d", buf.index);
    ret = ioctl(mCamFd, VIDIOC_QBUF, &buf); 
    if (ret != 0) 
	{
		// comment for temp, to do
        // LOGE("releasePreviewFrame: VIDIOC_QBUF Failed: index = %d, ret = %d, %s", 
		//	buf.index, ret, strerror(errno)); 
    }
}

int V4L2CameraDevice::getPreviewFrame(v4l2_buffer *buf)
{
	int ret = UNKNOWN_ERROR;
	
	buf->type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    buf->memory = V4L2_MEMORY_MMAP; 
 
    ret = ioctl(mCamFd, VIDIOC_DQBUF, buf); 
    if (ret < 0) 
	{ 
        LOGW("GetPreviewFrame: VIDIOC_DQBUF Failed"); 
        return __LINE__; 			// can not return false
    }

	return OK;
}

int V4L2CameraDevice::tryFmt(int format)
{	
	struct v4l2_fmtdesc fmtdesc;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	for(int i = 0; i < 12; i++)
	{
		fmtdesc.index = i;
		if (-1 == ioctl (mCamFd, VIDIOC_ENUM_FMT, &fmtdesc))
		{
			break;
		}
		LOGV("format index = %d, name = %s, v4l2 pixel format = %x\n",
			i, fmtdesc.description, fmtdesc.pixelformat);

		if (fmtdesc.pixelformat == format)
		{
			return OK;
		}
	}

	return -1;
}

int V4L2CameraDevice::tryFmtSize(int * width, int * height)
{
	F_LOG;
	int ret = -1;
	struct v4l2_format fmt;

	LOGV("V4L2Camera::TryFmtSize: w: %d, h: %d", *width, *height);

	LOGD("do not use tryFmtSize, return.");			// here to do
	return 0;

	memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    fmt.fmt.pix.width  = *width; 
    fmt.fmt.pix.height = *height; 
    if (mCaptureFormat == V4L2_PIX_FMT_YUYV)
	{
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	}
	else
	{
    	fmt.fmt.pix.pixelformat = mVideoFormat; 
	}
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	ret = ioctl(mCamFd, VIDIOC_TRY_FMT, &fmt); 
	if (ret < 0) 
	{ 
		LOGE("VIDIOC_TRY_FMT Failed: %s", strerror(errno)); 
		return ret; 
	} 

	// driver surpport this size
	*width = fmt.fmt.pix.width; 
    *height = fmt.fmt.pix.height; 

	return 0;
}

// set device node name, such as "/dev/video0"
int V4L2CameraDevice::setV4L2DeviceName(char * pname)
{
	F_LOG;
	if(pname == NULL)
	{
		return UNKNOWN_ERROR;
	}

	strncpy(mDeviceName, pname, strlen(pname));
	LOGV("%s: %s", __FUNCTION__, mDeviceName);

	return OK;
}

// set different device id on the same CSI
int V4L2CameraDevice::setV4L2DeviceID(int device_id)
{
	F_LOG;
	mDeviceID = device_id;
	return OK;
}

int V4L2CameraDevice::setFrameRate(int rate)
{
	mFrameRate = rate;
	return OK;
}

int V4L2CameraDevice::getFrameRate()
{
	F_LOG;
	int ret = -1;

	struct v4l2_streamparm parms;
	parms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = ioctl (mCamFd, VIDIOC_G_PARM, &parms);
	if (ret < 0) 
	{
		LOGE("VIDIOC_G_PARM getFrameRate error\n");
		return ret;
	}

	int numerator = parms.parm.capture.timeperframe.numerator;
	int denominator = parms.parm.capture.timeperframe.denominator;
	
	LOGV("frame rate: numerator = %d, denominator = %d\n", numerator, denominator);

	return denominator / numerator;
}

int V4L2CameraDevice::setCameraFacing(int facing)
{
	mCameraFacing = facing;

	return OK;
}

int V4L2CameraDevice::setImageEffect(int effect)
{
	F_LOG;
	int ret = -1;
	struct v4l2_control ctrl;

	ctrl.id = V4L2_CID_COLORFX;
	ctrl.value = effect;
	ret = ioctl(mCamFd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0)
		LOGV("setImageEffect failed!");
	else 
		LOGV("setImageEffect ok");

	return ret;
}

int V4L2CameraDevice::setWhiteBalance(int wb)
{
	struct v4l2_control ctrl;
	int ret = -1;

	ctrl.id = V4L2_CID_DO_WHITE_BALANCE;
	ctrl.value = wb;
	ret = ioctl(mCamFd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0)
		LOGV("setWhiteBalance failed!");
	else 
		LOGV("setWhiteBalance ok");

	return ret;
}

int V4L2CameraDevice::setExposure(int exp)
{
	F_LOG;
	int ret = -1;
	struct v4l2_control ctrl;

	ctrl.id = V4L2_CID_EXPOSURE;
	ctrl.value = exp;
	ret = ioctl(mCamFd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0)
		LOGV("setExposure failed!");
	else 
		LOGV("setExposure ok");

	return ret;
}

// flash mode
int V4L2CameraDevice::setFlashMode(int mode)
{
	F_LOG;
	int ret = -1;
	struct v4l2_control ctrl;

	ctrl.id = V4L2_CID_CAMERA_FLASH_MODE;
	ctrl.value = mode;
	ret = ioctl(mCamFd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0)
		LOGV("setFlashMode failed!");
	else 
		LOGV("setFlashMode ok");

	return ret;
}

int V4L2CameraDevice::enumSize(char * pSize, int len)
{
	struct v4l2_frmsizeenum size_enum;
	size_enum.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	size_enum.pixel_format = mCaptureFormat;

	if (pSize == NULL)
	{
		LOGE("error input params");
		return -1;
	}

	char str[16];
	memset(str, 0, 16);
	memset(pSize, 0, len);
	
	for(int i = 0; i < 20; i++)
	{
		size_enum.index = i;
		if (-1 == ioctl (mCamFd, VIDIOC_ENUM_FRAMESIZES, &size_enum))
		{
			break;
		}
		// LOGV("format index = %d, size_enum: %dx%d", i, size_enum.discrete.width, size_enum.discrete.height);
		sprintf(str, "%dx%d", size_enum.discrete.width, size_enum.discrete.height);
		if (i != 0)
		{
			strcat(pSize, ",");
		}
		strcat(pSize, str);
	}

	return OK;
}

// af mode
int V4L2CameraDevice::setAutoFocusMode(int af_mode)
{
	F_LOG;
	int ret = -1;
	struct v4l2_control ctrl;

	ctrl.id = V4L2_CID_CAMERA_AF_MODE;
	ctrl.value = af_mode;
	ret = ioctl(mCamFd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0)
		LOGV("setAutoFocusMode failed!");
	else 
		LOGV("setAutoFocusMode ok");

	return ret;
}

// af ctrl
int V4L2CameraDevice::setAutoFocusCtrl(int af_ctrl, void *areas)
{
	F_LOG;
	int ret = -1;
	struct v4l2_control ctrl;

	ctrl.id = V4L2_CID_CAMERA_AF_CTRL;
	ctrl.value = af_ctrl;
	ctrl.user_pt = (unsigned int)areas;
	ret = ioctl(mCamFd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0)
		LOGE("setAutoFocusCtrl failed!");
	else 
		LOGV("setAutoFocusCtrl ok");

	return ret;
}

int V4L2CameraDevice::getAutoFocusStatus(int af_ctrl)
{
	F_LOG;
	int ret = -1;
	struct v4l2_control ctrl;

	ctrl.id = V4L2_CID_CAMERA_AF_CTRL;
	ctrl.value = af_ctrl;
	ret = ioctl(mCamFd, VIDIOC_G_CTRL, &ctrl);
	if (ret >= 0)
		LOGV("getAutoFocusCtrl ok");

	return ret;
}

int V4L2CameraDevice::v4l2setCaptureParams(struct v4l2_streamparm * params)
{
	F_LOG;
	int ret = -1;

	ret = ioctl(mCamFd, VIDIOC_S_PARM, params);
	if (ret < 0)
		LOGE("v4l2setCaptureParams failed!");
	else 
		LOGV("v4l2setCaptureParams ok");

	return ret;
}

bool V4L2CameraDevice::isPreviewTime()
{
	// F_LOG;
    timeval cur_time;
    gettimeofday(&cur_time, NULL);
    const uint64_t cur_mks = cur_time.tv_sec * 1000000LL + cur_time.tv_usec;
    if ((cur_mks - mLastPreviewed) >= mPreviewAfter) {
        mLastPreviewed = cur_mks;
        return true;
    }
    return false;
}

}; /* namespace android */
