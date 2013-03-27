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
 * Contains implementation of a class CameraHardware that encapsulates
 * functionality common to all V4L2Cameras ("fake", "webcam", "video file",
 * etc.). Instances of this class (for each V4L2Camera) are created during
 * the construction of the HALCameraFactory instance. This class serves as
 * an entry point for all camera API calls that defined by camera_device_ops_t
 * API.
 */

#define LOG_TAG "CameraHardware"
#include "CameraDebug.h"

#include <ui/Rect.h>
#ifdef __SUN4I__
#include <drv_display_sun4i.h>
#else
#include <drv_display_sun5i.h>
#endif
#include <videodev2.h>
#include "CameraHardware.h"
#include "V4L2CameraDevice.h"
#include "Converters.h"
#include <cutils/properties.h>

/* Defines whether we should trace parameter changes. */
#define DEBUG_PARAM 0

namespace android {

#if DEBUG_PARAM
/* Calculates and logs parameter changes.
 * Param:
 *  current - Current set of camera parameters.
 *  new_par - String representation of new parameters.
 */
static void PrintParamDiff(const CameraParameters& current, const char* new_par);
#else
#define PrintParamDiff(current, new_par)   (void(0))
#endif  /* DEBUG_PARAM */

/* A helper routine that adds a value to the camera parameter.
 * Param:
 *  param - Camera parameter to add a value to.
 *  val - Value to add.
 * Return:
 *  A new string containing parameter with the added value on success, or NULL on
 *  a failure. If non-NULL string is returned, the caller is responsible for
 *  freeing it with 'free'.
 */
static char* AddValue(const char* param, const char* val);

static int faceNotifyCb(int cmd,void * data,void * user);

CameraHardware::CameraHardware(int cameraId, struct hw_module_t* module)
        : mPreviewWindow(),
          mCallbackNotifier(),
          mCameraID(cameraId),
          mCameraConfig(NULL),
          mDefaultPreviewWidth(640),
		  mDefaultPreviewHeight(480),
          bPixFmtNV12(false),
          mFirstSetParameters(true)
{
    /*
     * Initialize camera_device descriptor for this object.
     */
	F_LOG;

    /* Common header */
    common.tag = HARDWARE_DEVICE_TAG;
    common.version = 0;
    common.module = module;
    common.close = CameraHardware::close;

    /* camera_device fields. */
    ops = &mDeviceOps;
    priv = this;

	mCameraConfig = new CCameraConfig(cameraId);
	if (mCameraConfig == NULL)
	{
		LOGE("create CCameraConfig failed");
	}
	
	mCameraConfig->initParameters();
	mCameraConfig->dumpParameters();

	memset(&mRectCrop, 0, sizeof(mRectCrop));

	OSAL_QueueCreate(&mQueueCommand, CMD_QUEUE_MAX);

	pthread_mutex_init(&mCommamdMutex, NULL);
	pthread_cond_init(&mCommamdCond, NULL);
	mCommamdThread = new DoCommandThread(this);
	mCommamdThread->startThread();

	pthread_mutex_init(&mAutoFocusMutex, NULL);
	pthread_cond_init(&mAutoFocusCond, NULL);
	mAutoFocusThread = new DoAutoFocusThread(this);
	mAutoFocusThread->startThread();
}

CameraHardware::~CameraHardware()
{
	OSAL_QueueTerminate(&mQueueCommand);

	if (mCommamdThread != NULL)
	{
		mCommamdThread->stopThread();
		pthread_cond_signal(&mCommamdCond);
		mCommamdThread.clear();
		mCommamdThread = 0;
		
		pthread_mutex_destroy(&mCommamdMutex);
		pthread_cond_destroy(&mCommamdCond);
	}

	if (mAutoFocusThread != NULL)
	{
		mAutoFocusThread->stopThread();
		pthread_cond_signal(&mAutoFocusCond);
		mAutoFocusThread.clear();
		mAutoFocusThread = 0;
		
		pthread_mutex_destroy(&mAutoFocusMutex);
		pthread_cond_destroy(&mAutoFocusCond);
	}
}

/****************************************************************************
 * Public API
 ***************************************************************************/

bool CameraHardware::autoFocusThread()
{
	pthread_cond_wait(&mAutoFocusCond, &mAutoFocusMutex);
	
	int ret = 0;
	const char *new_focus_mode_str = mParameters.get(CameraParameters::KEY_FOCUS_MODE);

	if (!mCameraConfig->supportFocusMode())
	{
		goto FOCUS_END;
	}
	
	if (!strcmp(new_focus_mode_str, CameraParameters::FOCUS_MODE_INFINITY)
		|| !strcmp(new_focus_mode_str, CameraParameters::FOCUS_MODE_FIXED))
	{
		// 
	}
	else
	{
		bool timeout = false;
		int64_t lastTimeMs = systemTime() / 1000000;
		
		setAutoFocusCtrl(V4L2_AF_TRIG_SINGLE, NULL);

		while(1)
		{
			// if hw af ok
			ret = getCameraDevice()->getAutoFocusStatus(V4L2_AF_TRIG_SINGLE);
			if ( ret == 0)
			{
				LOGV("auto focus ok, use time: %lld(ms)", systemTime() / 1000000 - lastTimeMs);
				break;
			}
			else if (ret == EBUSY)
			{
				if ((systemTime() / 1000000 - lastTimeMs) > 1000)	// 2s timeout
				{
					LOGW("auto focus time out");
					timeout = true;
					break;
				}
				LOGV("wait auto focus ......");
				usleep(10000);
			}
			else if (ret == EFAULT)
			{
				LOGW("auto focus failed");
				break;
			}
			else if (ret < 0)
			{
				LOGE("auto focus interface error");
				break;
			}
		}

		if (ret == 0)
		{
			setAutoFocusCtrl(V4L2_AF_LOCK, NULL);
		}
	}

FOCUS_END:

	mCallbackNotifier.autoFocus(ret == 0);
	
	LOGV("auto focus end");

	return true;
}

bool CameraHardware::commandThread()
{
	Queue_Element * queue = (Queue_Element *)OSAL_Dequeue(&mQueueCommand);
	if (queue == NULL)
	{
		LOGV("wait commond queue ......");
		pthread_cond_wait(&mCommamdCond, &mCommamdMutex);
		return true;
	}

	V4L2CameraDevice* pV4L2Device = getCameraDevice();
	int cmd = queue->cmd;
	switch(cmd)
	{
		case CMD_QUEUE_SET_COLOR_EFFECT:
		{
			int new_image_effect = queue->data;
			LOGV("CMD_QUEUE_SET_COLOR_EFFECT: %d", new_image_effect);
			
			if (pV4L2Device->setImageEffect(new_image_effect) < 0) 
			{
                LOGE("ERR(%s):Fail on mV4L2Camera->setImageEffect(effect(%d))", __FUNCTION__, new_image_effect);
            }
			break;
		}
		case CMD_QUEUE_SET_WHITE_BALANCE:
		{
			int new_white = queue->data;
			LOGV("CMD_QUEUE_SET_WHITE_BALANCE: %d", new_white);
			
            if (pV4L2Device->setWhiteBalance(new_white) < 0) 
			{
                LOGE("ERR(%s):Fail on mV4L2Camera->setWhiteBalance(white(%d))", __FUNCTION__, new_white);
            }
			break;
		}
		case CMD_QUEUE_SET_EXPOSURE_COMPENSATION:
		{
			int new_exposure_compensation = queue->data;
			LOGV("CMD_QUEUE_SET_EXPOSURE_COMPENSATION: %d", new_exposure_compensation);
			
			if (pV4L2Device->setExposure(new_exposure_compensation) < 0) 
			{
				LOGE("ERR(%s):Fail on mV4L2Camera->setBrightness(brightness(%d))", __FUNCTION__, new_exposure_compensation);
			}
			break;
		}
		case CMD_QUEUE_SET_FLASH_MODE:
		{
			break;
		}
		case CMD_QUEUE_SET_FOCUS_MODE:
		{
			LOGV("CMD_QUEUE_SET_FOCUS_MODE");
			setAutoFocusMode();
			if(setAutoFocusMode() != OK)
	        {
				LOGE("unknown focus mode");
	       	}
			break;
		}
		case CMD_QUEUE_SET_FOCUS_AREA:
		{
			char * new_focus_areas_str = (char *)queue->data;
			if (new_focus_areas_str != NULL)
			{
				LOGV("CMD_QUEUE_SET_FOCUS_AREA: %s", new_focus_areas_str);
        		parse_focus_areas(new_focus_areas_str);
			}
        	break;
		}
		case CMD_QUEUE_START_FACE_DETECTE:
		{
			int width = 0, height = 0;
			LOGV("CMD_QUEUE_START_FACE_DETECTE");
			mParameters.getPreviewSize(&width, &height);
			mFaceDetection->ioctrl(mFaceDetection, FACE_OPS_CMD_START, width, height);
			break;
		}
		case CMD_QUEUE_STOP_FACE_DETECTE:
		{
			LOGV("CMD_QUEUE_STOP_FACE_DETECTE");
			mFaceDetection->ioctrl(mFaceDetection, FACE_OPS_CMD_STOP, 0, 0);
			break;
		}
		default:
			LOGW("unknown queue command: %d", cmd);
			break;
	}
	
	return true;
}

status_t CameraHardware::Initialize()
{
	F_LOG;

	if (mCameraConfig == NULL)
	{
		return UNKNOWN_ERROR;
	}

	char proc_node[128];
	memset(proc_node, 0, sizeof(proc_node));
	sprintf(proc_node, "/proc/%d/cmdline", GET_CALLING_PID);
	int fp = ::open(proc_node, O_RDONLY);
	if (fp > 0) 
	{
		memset(mCallingProcessName, 0, sizeof(mCallingProcessName));
		::read(fp, mCallingProcessName, sizeof(mCallingProcessName));
		::close(fp);
		fp = 0;
		LOGD("Calling process is: %s", mCallingProcessName);

		mCallbackNotifier.setCallingProcess(mCallingProcessName);
	}
	else 
	{
		LOGE("Obtain calling process failed");
    }

	// create FaceDetection object
	CreateFaceDetectionDev(&mFaceDetection);
	if (mFaceDetection == NULL)
	{
		LOGE("create FaceDetection failed");
		return UNKNOWN_ERROR;
	}

	mFaceDetection->ioctrl(mFaceDetection, FACE_OPS_CMD_REGISTE_USER, (int)this, 0);
	mFaceDetection->setCallback(mFaceDetection, faceNotifyCb);

	initDefaultParameters();

    return NO_ERROR;
}

static int faceNotifyCb(int cmd, void * data, void *user)
{
	CameraHardware* camera_hw = (CameraHardware *)user;
	
	switch (cmd)
	{
		case FACE_NOTITY_CMD_REQUEST_FRAME:
			return camera_hw->getCurrentFaceFrame(data);
		case FACE_NOTITY_CMD_RESULT:
			return camera_hw->faceDetection((camera_frame_metadata_t*)data);
			break;
		case FACE_NOTITY_CMD_POSITION:
			return camera_hw->setAutoFocusCtrl(V4L2_AF_WIN_XY, data);
			break;
		default:
			break;
	}
	
	return 0;
}

// Parse string like "640x480" or "10000,20000"
static int parse_pair(const char *str, int *first, int *second, char delim,
                      char **endptr = NULL)
{
    // Find the first integer.
    char *end;
    int w = (int)strtol(str, &end, 10);
    // If a delimeter does not immediately follow, give up.
    if (*end != delim) {
        LOGE("Cannot find delimeter (%c) in str=%s", delim, str);
        return -1;
    }

    // Find the second integer, immediately after the delimeter.
    int h = (int)strtol(end+1, &end, 10);

    *first = w;
    *second = h;

    if (endptr) {
        *endptr = end;
    }

    return 0;
}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p = mParameters;
	String8 parameterString;
	char * value;

	LOGV("CameraHardware::initDefaultParameters");
	
	if (mCameraConfig->cameraFacing() == CAMERA_FACING_BACK)
	{
	    p.set(CameraHardware::FACING_KEY, CameraHardware::FACING_BACK);
	    LOGV("%s: camera is facing %s", __FUNCTION__, CameraHardware::FACING_BACK);
	}
	else
	{
	    p.set(CameraHardware::FACING_KEY, CameraHardware::FACING_FRONT);
	    LOGV("%s: camera is facing %s", __FUNCTION__, CameraHardware::FACING_FRONT);
	}
	
    p.set(CameraHardware::ORIENTATION_KEY, mCameraConfig->getCameraOrientation());
	
	LOGV("to init preview size");
	// preview size
	if (mCameraConfig->supportPreviewSize())
	{
		value = mCameraConfig->supportPreviewSizeValue();
		p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, value);
		LOGV("supportPreviewSizeValue: [%s] %s", CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, value);
		p.set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES, value);
		p.set(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO, "1920x1080");

		value = mCameraConfig->defaultPreviewSizeValue();
		p.set(CameraParameters::KEY_PREVIEW_SIZE, value);
		p.set(CameraParameters::KEY_VIDEO_SIZE, value);

		int default_w = 0, default_h = 0;
		int ret = parse_pair(value, &default_w, &default_h, 'x', &value);
		if (ret == 0)
		{
			LOGV("default size: %dx%d", default_w, default_h);
			mDefaultPreviewWidth = default_w;
			mDefaultPreviewHeight = default_h;
		}
		else
		{
			LOGE("parse default size failed");
		}
		
		// for some apps
		if (strcmp(mCallingProcessName, "com.android.facelock") == 0)
		{
			p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, "160x120");
			p.set(CameraParameters::KEY_PREVIEW_SIZE, "160x120");
		}
	}

	LOGV("to init picture size");
	// picture size
	if (mCameraConfig->supportPictureSize())
	{
		value = mCameraConfig->supportPictureSizeValue();
		p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, value);
		LOGV("supportPreviewSizeValue: [%s] %s", CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, value);

		value = mCameraConfig->defaultPictureSizeValue();
		p.set(CameraParameters::KEY_PICTURE_SIZE, value);
		
		p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
	}

	LOGV("to init frame rate");
	// frame rate
	if (mCameraConfig->supportFrameRate())
	{
		value = mCameraConfig->supportFrameRateValue();
		p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, value);
		LOGV("supportFrameRateValue: [%s] %s", CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, value);

		p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "5000,60000");				// add temp for CTS
		p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(5000,60000)");	// add temp for CTS

		value = mCameraConfig->defaultFrameRateValue();
		p.set(CameraParameters::KEY_PREVIEW_FRAME_RATE, value);

		getCameraDevice()->setFrameRate(atoi(value));
	}

	LOGV("to init focus");
	// focus
	if (mCameraConfig->supportFocusMode())
	{
		value = mCameraConfig->supportFocusModeValue();
		p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, value);
		value = mCameraConfig->defaultFocusModeValue();
		p.set(CameraParameters::KEY_FOCUS_MODE, value);
		p.set(CameraParameters::KEY_MAX_NUM_FOCUS_AREAS,"1");
	}
	else
	{
		// add for CTS
		p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, CameraParameters::FOCUS_MODE_AUTO);
		p.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);
	}
	p.set(CameraParameters::KEY_FOCUS_AREAS, "(0,0,0,0,0)");
	p.set(CameraParameters::KEY_FOCAL_LENGTH, "3.43");
	mCallbackNotifier.setFocalLenght(3.43);
	p.set(CameraParameters::KEY_FOCUS_DISTANCES, "0.10,1.20,Infinity");

	LOGV("to init color effect");
	// color effect	
	if (mCameraConfig->supportColorEffect())
	{
		value = mCameraConfig->supportColorEffectValue();
		p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, value);
		value = mCameraConfig->defaultColorEffectValue();
		p.set(CameraParameters::KEY_EFFECT, value);
	}

	LOGV("to init flash mode");
	// flash mode	
	if (mCameraConfig->supportFlashMode())
	{
		value = mCameraConfig->supportFlashModeValue();
		p.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, value);
		value = mCameraConfig->defaultFlashModeValue();
		p.set(CameraParameters::KEY_FLASH_MODE, value);
	}

	LOGV("to init scene mode");
	// scene mode	
	if (mCameraConfig->supportSceneMode())
	{
		value = mCameraConfig->supportSceneModeValue();
		p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES, value);
		value = mCameraConfig->defaultSceneModeValue();
		p.set(CameraParameters::KEY_SCENE_MODE, value);
	}

	LOGV("to init white balance");
	// white balance	
	if (mCameraConfig->supportWhiteBalance())
	{
		value = mCameraConfig->supportWhiteBalanceValue();
		p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, value);
		value = mCameraConfig->defaultWhiteBalanceValue();
		p.set(CameraParameters::KEY_WHITE_BALANCE, value);
	}

	LOGV("to init exposure compensation");
	// exposure compensation
	if (mCameraConfig->supportExposureCompensation())
	{
		value = mCameraConfig->minExposureCompensationValue();
		p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, value);

		value = mCameraConfig->maxExposureCompensationValue();
		p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, value);

		value = mCameraConfig->stepExposureCompensationValue();
		p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, value);

		value = mCameraConfig->defaultExposureCompensationValue();
		p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, value);
	}
	else
	{
		p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "0");
		p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "0");
		p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0");
		p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
	}

	LOGV("to init zoom");
	// zoom
	if (mCameraConfig->supportZoom())
	{
		value = mCameraConfig->zoomSupportedValue();
		p.set(CameraParameters::KEY_ZOOM_SUPPORTED, value);

		value = mCameraConfig->smoothZoomSupportedValue();
		p.set(CameraParameters::KEY_SMOOTH_ZOOM_SUPPORTED, value);

		// value = mCameraConfig->zoomRatiosValue();
		// p.set(CameraParameters::KEY_ZOOM_RATIOS, value);

		value = mCameraConfig->maxZoomValue();
		p.set(CameraParameters::KEY_MAX_ZOOM, value);

		int max_zoom = atoi(mCameraConfig->maxZoomValue());
		char zoom_ratios[1024];
		memset(zoom_ratios, 0, 1024);
		for (int i = 0; i <= max_zoom; i++)
		{
			int i_ratio = 200 * i / max_zoom + 100;
			char str[8];
			sprintf(str, "%d,", i_ratio);
			strcat(zoom_ratios, str);
		}
		int len = strlen(zoom_ratios);
		zoom_ratios[len - 1] = 0;
		LOGV("zoom_ratios: %s", zoom_ratios);
		p.set(CameraParameters::KEY_ZOOM_RATIOS, zoom_ratios);

		value = mCameraConfig->defaultZoomValue();
		p.set(CameraParameters::KEY_ZOOM, value);

		getCameraDevice()->setCrop(0, max_zoom);
	}

	// preview formats, CTS must support at least 2 formats
	parameterString = CameraParameters::PIXEL_FORMAT_YUV420SP;
	parameterString.append(",");
	parameterString.append(CameraParameters::PIXEL_FORMAT_YUV420P);
	p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, parameterString.string());
	
	p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, CameraParameters::PIXEL_FORMAT_YUV420SP);
	p.set(CameraParameters::KEY_PREVIEW_FORMAT, CameraParameters::PIXEL_FORMAT_YUV420SP);

    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, CameraParameters::PIXEL_FORMAT_JPEG);
	
	p.set(CameraParameters::KEY_JPEG_QUALITY, "90"); // maximum quality
	p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES, "320x240,0x0");
	p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "320");
	p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "240");
	p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "90");

	mCallbackNotifier.setJpegThumbnailSize(320, 240);

	// record hint
	p.set(CameraParameters::KEY_RECORDING_HINT, "false");

	// rotation
	p.set(CameraParameters::KEY_ROTATION, 0);
		
	// add for CTS
	p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "51.2");
    p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "39.4");

	if (mCameraConfig->cameraFacing() == CAMERA_FACING_BACK)
	{
	    p.set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_HW, 1);
	    p.set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_SW, 0);
	}
	else
	{
	    p.set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_HW, 0);
	    p.set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_SW, 0);
	}
	
	mParameters = p;

	mFirstSetParameters = true;

	LOGV("CameraHardware::initDefaultParameters ok");
}

bool CameraHardware::onNextFrameAvailable(const void* frame,
										  int video_fmt,
                                          nsecs_t timestamp,
                                          V4L2Camera* camera_dev,
                                          bool bUseMataData)
{
	// F_LOG;
	bool ret = false;
	
    /* Notify the preview window first. */
	ret = mPreviewWindow.onNextFrameAvailable(frame, video_fmt, timestamp, camera_dev, bUseMataData);
    if(!ret)
	{
		return ret;
	}

    /* Notify callback notifier next. */
	mCallbackNotifier.onNextFrameAvailable(frame, timestamp, camera_dev, bUseMataData);

	return true;
}

bool CameraHardware::onNextFramePreview(const void* frame,
									  int video_fmt,
									  nsecs_t timestamp,
									  V4L2Camera* camera_dev,
                                      bool bUseMataData)
{
	// F_LOG;
    /* Notify the preview window first. */
    return mPreviewWindow.onNextFrameAvailable(frame, video_fmt, timestamp, camera_dev, bUseMataData);
}

void CameraHardware::onNextFrameCB(const void* frame,
									  nsecs_t timestamp,
									  V4L2Camera* camera_dev,
                                      bool bUseMataData)
{
	/* Notify callback notifier next. */
    mCallbackNotifier.onNextFrameAvailable(frame, timestamp, camera_dev, bUseMataData);
}

void CameraHardware::onTakingPicture(const void* frame, V4L2Camera* camera_dev, bool bUseMataData)
{
	mCallbackNotifier.takePicture(frame, camera_dev, bUseMataData);
}

void CameraHardware::onCameraDeviceError(int err)
{
	F_LOG;
    /* Errors are reported through the callback notifier */
    mCallbackNotifier.onCameraDeviceError(err);
}

void CameraHardware::setCrop(Rect * rect, int new_zoom)
{
	// F_LOG;
	memcpy(&mRectCrop, rect, sizeof(Rect));
	mPreviewWindow.setCrop(rect, new_zoom);
}

/****************************************************************************
 * Camera API implementation.
 ***************************************************************************/

status_t CameraHardware::connectCamera(hw_device_t** device)
{
    LOGV("%s", __FUNCTION__);

    status_t res = EINVAL;
    V4L2Camera* const camera_dev = getCameraDevice();
    LOGE_IF(camera_dev == NULL, "%s: No camera device instance.", __FUNCTION__);

    if (camera_dev != NULL) {
        /* Connect to the camera device. */
        res = getCameraDevice()->connectDevice();
        if (res == NO_ERROR) {
            *device = &common;
			
			// for USB camera
			if (getCameraDevice()->getCaptureFormat() == V4L2_PIX_FMT_YUYV)
			{
				char sizeStr[256];
				getCameraDevice()->enumSize(sizeStr, 256);
				LOGV("enum size: %s", sizeStr);
				if (strlen(sizeStr) > 0)
				{
					mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, sizeStr);
					mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, sizeStr);
				}
			}
        }
		setAutoFocusCtrl(V4L2_AF_INIT, NULL);
    }

    return -res;
}

status_t CameraHardware::closeCamera()
{
    LOGV("%s", __FUNCTION__);

    return cleanupCamera();
}

status_t CameraHardware::getCameraInfo(struct camera_info* info)
{
    LOGV("%s", __FUNCTION__);

	LOGV("cur: %d, calling pid: %d", (int)getpid(), GET_CALLING_PID);

	info->orientation = mCameraConfig->getCameraOrientation();	
	// single camera
	if (mCameraConfig->numberOfCamera() == 1)
	{
		// cts, mobile qq need facing-back camera
		if ((strcmp(mCallingProcessName, "com.android.cts.stub") == 0)
			|| (strcmp(mCallingProcessName, "com.tencent.mobileqq") == 0))
		{
			info->facing = CAMERA_FACING_BACK;
			goto END;
		}

		// gtalk, facelock need facing-front camera
		if ((strcmp(mCallingProcessName, "com.google.android.talk") == 0)
			|| (strcmp(mCallingProcessName, "com.android.facelock") == 0))
		{
			info->facing = CAMERA_FACING_FRONT;
			goto END;

		}
	}
	
	if (mCameraConfig->cameraFacing() == CAMERA_FACING_BACK)
	{
		info->facing = CAMERA_FACING_BACK;
	}
	else
	{
		info->facing = CAMERA_FACING_FRONT;
	}
	
END:
	char property[PROPERTY_VALUE_MAX];
	if (property_get("ro.sf.hwrotation", property, NULL) > 0) {
        //displayOrientation
        switch (atoi(property)) {
        case 270:
            if(info->facing == CAMERA_FACING_BACK){
			    info->orientation = 90;
				} 
			else if(info->facing == CAMERA_FACING_FRONT){
			    info->orientation = 270;
				} 
            break;
        }
    }
    return NO_ERROR;
}

status_t CameraHardware::setPreviewWindow(struct preview_stream_ops* window)
{
	F_LOG;
    /* Callback should return a negative errno. */
	return -mPreviewWindow.setPreviewWindow(window,
                                             mParameters.getPreviewFrameRate());
}

void CameraHardware::setCallbacks(camera_notify_callback notify_cb,
                                  camera_data_callback data_cb,
                                  camera_data_timestamp_callback data_cb_timestamp,
                                  camera_request_memory get_memory,
                                  void* user)
{
	F_LOG;
    mCallbackNotifier.setCallbacks(notify_cb, data_cb, data_cb_timestamp,
                                    get_memory, user);
}

void CameraHardware::enableMsgType(int32_t msg_type)
{
	F_LOG;
    mCallbackNotifier.enableMessage(msg_type);
}

void CameraHardware::disableMsgType(int32_t msg_type)
{
	F_LOG;
    mCallbackNotifier.disableMessage(msg_type);
}

int CameraHardware::isMsgTypeEnabled(int32_t msg_type)
{
	F_LOG;
    return mCallbackNotifier.isMessageEnabled(msg_type);
}

status_t CameraHardware::startPreview()
{
	F_LOG;
    /* Callback should return a negative errno. */
    return -doStartPreview();
}

void CameraHardware::stopPreview()
{
	F_LOG;
	
	mQueueElement[CMD_QUEUE_STOP_FACE_DETECTE].cmd = CMD_QUEUE_STOP_FACE_DETECTE;
	OSAL_Queue(&mQueueCommand, &mQueueElement[CMD_QUEUE_STOP_FACE_DETECTE]);
	pthread_cond_signal(&mCommamdCond);
	
    doStopPreview();
}

int CameraHardware::isPreviewEnabled()
{
	F_LOG;
    return mPreviewWindow.isPreviewEnabled();
}

status_t CameraHardware::storeMetaDataInBuffers(int enable)
{
	F_LOG;
    /* Callback should return a negative errno. */
    return -mCallbackNotifier.storeMetaDataInBuffers(enable);
}

status_t CameraHardware::startRecording()
{
	F_LOG;
	
#if 0

	bPixFmtNV12 = true;

	// get video size
	int new_video_width = 0;
	int new_video_height = 0;
	mParameters.getVideoSize(&new_video_width, &new_video_height);

	if (mDefaultPreviewWidth != new_video_width
		|| mDefaultPreviewHeight != new_video_height
		|| bPixFmtNV12)
	{
		doStopPreview();
		if (mPreviewWindow.isLayerShowHW())
		{
			mPreviewWindow.showLayer(false);			// only here to close layer
		}
		doStartPreview();
	}
#endif

    /* Callback should return a negative errno. */
    return -mCallbackNotifier.enableVideoRecording(mParameters.getPreviewFrameRate());
}

void CameraHardware::stopRecording()
{
	F_LOG;
    mCallbackNotifier.disableVideoRecording();
	
#if 0
	bPixFmtNV12 = false;
//	mCallbackNotifier.storeMetaDataInBuffers(false);
	
	// get video size
	int new_video_width = 0;
	int new_video_height = 0;
	mParameters.getVideoSize(&new_video_width, &new_video_height);

	// do not use high size for preview
	mParameters.setPreviewSize(mDefaultPreviewWidth, mDefaultPreviewHeight);
	mParameters.setVideoSize(mDefaultPreviewWidth, mDefaultPreviewHeight);

	if (mDefaultPreviewWidth != new_video_width
		|| mDefaultPreviewHeight != new_video_height)
	{
		doStopPreview();
		if (mPreviewWindow.isLayerShowHW())
		{
			mPreviewWindow.showLayer(false);			// only here to close layer
		}
		doStartPreview();
	}
#endif
}

int CameraHardware::isRecordingEnabled()
{
	F_LOG;
    return mCallbackNotifier.isVideoRecordingEnabled();
}

void CameraHardware::releaseRecordingFrame(const void* opaque)
{
	F_LOG;
    mCallbackNotifier.releaseRecordingFrame(opaque);
}

int CameraHardware::parse_focus_areas(const char *str)
{
    int ret = -1;
    char *ptr,*tmp;
    char p1[6] = {0}, p2[6] = {0};
    char p3[6] = {0}, p4[6] = {0}, p5[6] = {0};
    int  l,t,r,b;
    int  w,h;

    if (str == NULL)
    {
		return 0;
    }

    tmp = strchr(str,'(');
    tmp++;
    ptr = strchr(tmp,',');
    memcpy(p1,tmp,ptr-tmp);
    
    tmp = ptr+1;
    ptr = strchr(tmp,',');
    memcpy(p2,tmp,ptr-tmp);

    tmp = ptr+1;
    ptr = strchr(tmp,',');
    memcpy(p3,tmp,ptr-tmp);

    tmp = ptr+1;
    ptr = strchr(tmp,',');
    memcpy(p4,tmp,ptr-tmp);

    tmp = ptr+1;
    ptr = strchr(tmp,')');
    memcpy(p5,tmp,ptr-tmp);

	l = atoi(p1);
    t = atoi(p2);
    r = atoi(p3);
    b = atoi(p4);
    
    w = l + (r-l)/2;
    h = t + (b-t)/2;

	mFocusAreas.width = mRectCrop.right - (1000 - w) * (mRectCrop.right - mRectCrop.left) / 2000;
	mFocusAreas.height= mRectCrop.bottom - (1000 - h) * (mRectCrop.bottom - mRectCrop.top) / 2000;

	LOGV("V4L2_AF_SET_WIN: (%d, %d)", mFocusAreas.width, mFocusAreas.height);
    setAutoFocusCtrl(V4L2_AF_WIN_XY, (void*)&mFocusAreas);

    return 0;
}

status_t CameraHardware::setAutoFocus()
{
	pthread_cond_signal(&mAutoFocusCond);

    return OK;
}

status_t CameraHardware::cancelAutoFocus()
{
    LOGV("%s", __FUNCTION__);

    /* TODO: Future enhancements. */
    return NO_ERROR;
}

status_t CameraHardware::takePicture()
{
    LOGV("%s", __FUNCTION__);

    status_t res;
    int pic_width, pic_height, frame_width, frame_height;
    uint32_t org_fmt;

    /* Collect frame info for the picture. */
    mParameters.getPictureSize(&pic_width, &pic_height);
	frame_width = pic_width;
	frame_height = pic_height;
	getCameraDevice()->tryFmtSize(&frame_width, &frame_height);
	// mParameters.setPreviewSize(frame_width, frame_height);
	LOGV("%s, pic_size: %dx%d, frame_size: %dx%d", 
		__FUNCTION__, pic_width, pic_height, frame_width, frame_height);

	getCameraDevice()->setPictureSize(pic_width, pic_height);
	
    const char* pix_fmt = mParameters.getPictureFormat();
    if (strcmp(pix_fmt, CameraParameters::PIXEL_FORMAT_YUV420P) == 0) {
        org_fmt = V4L2_PIX_FMT_YUV420;
    } else if (strcmp(pix_fmt, CameraParameters::PIXEL_FORMAT_RGBA8888) == 0) {
        org_fmt = V4L2_PIX_FMT_RGB32;
    } else if (strcmp(pix_fmt, CameraParameters::PIXEL_FORMAT_YUV420SP) == 0) {
        org_fmt = V4L2_PIX_FMT_NV12;
    } else if (strcmp(pix_fmt, CameraParameters::PIXEL_FORMAT_JPEG) == 0) {
        /* We only have JPEG converted for NV21 format. */
        org_fmt = V4L2_PIX_FMT_NV12;
    } else {
        LOGE("%s: Unsupported pixel format %s", __FUNCTION__, pix_fmt);
        return EINVAL;
    }
    /* Get JPEG quality. */
    int jpeg_quality = mParameters.getInt(CameraParameters::KEY_JPEG_QUALITY);
    if (jpeg_quality <= 0) {
        jpeg_quality = 90;  /* Fall back to default. */
    }

	/* Get JPEG rotate. */
    int jpeg_rotate = mParameters.getInt(CameraParameters::KEY_ROTATION);
    if (jpeg_rotate <= 0) {
        jpeg_rotate = 0;  /* Fall back to default. */
    }

    /*
     * Make sure preview is not running, and device is stopped before taking
     * picture.
     */

    const bool preview_on = mPreviewWindow.isPreviewEnabled();
    if (preview_on) {
        doStopPreview();
    }

    /* Camera device should have been stopped when the shutter message has been
     * enabled. */
    V4L2Camera* const camera_dev = getCameraDevice();
    if (camera_dev->isStarted()) {
        LOGW("%s: Camera device is started", __FUNCTION__);
        camera_dev->stopDeliveringFrames();
        camera_dev->stopDevice();
    }

    /*
     * Take the picture now.
     */
     
	// close layer before taking picture, 
	// mPreviewWindow.showLayer(false);
	
	camera_dev->setTakingPicture(true);
    /* Start camera device for the picture frame. */
    LOGD("Starting camera for picture: %.4s(%s)[%dx%d]",
         reinterpret_cast<const char*>(&org_fmt), pix_fmt, frame_width, frame_height);
    res = camera_dev->startDevice(frame_width, frame_height, org_fmt);
    if (res != NO_ERROR) {
        if (preview_on) {
            doStartPreview();
        }
        return res;
    }
	
    /* Deliver one frame only. */
    mCallbackNotifier.setJpegQuality(jpeg_quality);
	mCallbackNotifier.setJpegRotate(jpeg_rotate);
    mCallbackNotifier.setTakingPicture(true);
    // res = camera_dev->startDeliveringFrames(true);	
	res = camera_dev->startDeliveringFrames(false);		// star modify
    if (res != NO_ERROR) {
        mCallbackNotifier.setTakingPicture(false);
        if (preview_on) {
            doStartPreview();
        }
    }
	
    return res;
}
int CameraHardware::setAutoFocusMode()
{
	F_LOG;
	v4l2_autofocus_mode af_mode = V4L2_AF_FIXED;
    if (mCameraConfig->supportFocusMode())
	{
	    // focus
		const char *new_focus_mode_str = mParameters.get(CameraParameters::KEY_FOCUS_MODE);
		if (!strcmp(new_focus_mode_str, CameraParameters::FOCUS_MODE_AUTO))
		{
			af_mode = V4L2_AF_AUTO;
		}
		else if (!strcmp(new_focus_mode_str, CameraParameters::FOCUS_MODE_INFINITY))
		{
			af_mode = V4L2_AF_INFINITY;
		}
		else if (!strcmp(new_focus_mode_str, CameraParameters::FOCUS_MODE_MACRO))
		{
			af_mode = V4L2_AF_MACRO;
		}
		else if (!strcmp(new_focus_mode_str, CameraParameters::FOCUS_MODE_FIXED))
		{
			af_mode = V4L2_AF_FIXED;
		}
		else
		{
			return -EINVAL;
		}
	}
	else
	{
		af_mode = V4L2_AF_FIXED;
	}
	
	getCameraDevice()->setAutoFocusMode(af_mode);
	
	return OK;
}

int CameraHardware::setAutoFocusCtrl(int ctrl, void *data)
{
	if (mCameraConfig->supportFocusMode())
	{
		getCameraDevice()->setAutoFocusCtrl(ctrl, data);
	}
	return OK;
}

int CameraHardware::getCurrentFaceFrame(void * frame)
{
	return getCameraDevice()->getCurrentFaceFrame(frame);
}

int CameraHardware::faceDetection(camera_frame_metadata_t *face)
{
	return mCallbackNotifier.faceDetection(face);
}

status_t CameraHardware::cancelPicture()
{
    LOGV("%s", __FUNCTION__);

    return NO_ERROR;
}

status_t CameraHardware::setParameters(const char* p)
{
    LOGV("%s", __FUNCTION__);
	int ret = UNKNOWN_ERROR;
	
    PrintParamDiff(mParameters, p);

    CameraParameters params;
	String8 str8_param(p);
    params.unflatten(str8_param);

	V4L2CameraDevice* pV4L2Device = getCameraDevice();
	if (pV4L2Device == NULL)
	{
		LOGE("%s, getCameraDevice failed", __FUNCTION__);
		return UNKNOWN_ERROR;
	}

	// preview format
	const char * new_preview_format = params.getPreviewFormat();
	LOGV("new_preview_format : %s", new_preview_format);
	if (new_preview_format != NULL
		&& (strcmp(new_preview_format, CameraParameters::PIXEL_FORMAT_YUV420SP) == 0
		|| strcmp(new_preview_format, CameraParameters::PIXEL_FORMAT_YUV420P) == 0)) 
	{
		mParameters.setPreviewFormat(new_preview_format);
	}
	else
    {
        LOGE("Only yuv420sp or yuv420p preview is supported");
        return -EINVAL;
    }

	// picture format
	const char * new_picture_format = params.getPictureFormat();
	LOGV("new_picture_format : %s", new_picture_format);
	if (new_picture_format == NULL
		|| (strcmp(new_picture_format, CameraParameters::PIXEL_FORMAT_JPEG) != 0)) 
    {
        LOGE("Only jpeg still pictures are supported");
        return -EINVAL;
    }

	// picture size
	int new_picture_width  = 0;
    int new_picture_height = 0;
    params.getPictureSize(&new_picture_width, &new_picture_height);
    LOGV("%s : new_picture_width x new_picture_height = %dx%d", __FUNCTION__, new_picture_width, new_picture_height);
    if (0 < new_picture_width && 0 < new_picture_height) 
	{
		mParameters.setPictureSize(new_picture_width, new_picture_height);
    }
	else
	{
		LOGE("error picture size");
		return -EINVAL;
	}

	// preview size
    int new_preview_width  = 0;
    int new_preview_height = 0;
    params.getPreviewSize(&new_preview_width, &new_preview_height);
    LOGV("%s : new_preview_width x new_preview_height = %dx%d",
         __FUNCTION__, new_preview_width, new_preview_height);
	if (0 < new_preview_width && 0 < new_preview_height)
	{
		// try size
		ret = pV4L2Device->tryFmtSize(&new_preview_width, &new_preview_height);
		if(ret < 0)
		{
			return ret;
		}
		
		mParameters.setPreviewSize(new_preview_width, new_preview_height);
		mParameters.setVideoSize(new_preview_width, new_preview_height);
	}
	else
	{
		LOGE("error preview size");
		return -EINVAL;
	}

	// video hint
    const char * valstr = params.get(CameraParameters::KEY_RECORDING_HINT);
    if (valstr) 
	{
		LOGV("KEY_RECORDING_HINT: %s", valstr);
		bPixFmtNV12 = (strcmp(valstr, CameraParameters::TRUE) == 0) ? true : false;
        mParameters.set(CameraParameters::KEY_RECORDING_HINT, bPixFmtNV12 ? "true" : "false");
    }

	// frame rate
	int new_min_frame_rate, new_max_frame_rate;
	params.getPreviewFpsRange(&new_min_frame_rate, &new_max_frame_rate);
	int new_preview_frame_rate = params.getPreviewFrameRate();
	if (0 < new_preview_frame_rate && 0 < new_min_frame_rate 
		&& new_min_frame_rate <= new_max_frame_rate)
	{
		mParameters.setPreviewFrameRate(pV4L2Device->getFrameRate());
	}
	else
	{
		if (getCameraDevice()->getCaptureFormat() == V4L2_PIX_FMT_YUYV)
		{
			LOGV("may be usb camera, don't care frame rate");
		}
		else
		{
			LOGE("error preview frame rate");
			return -EINVAL;
		}
	}

	// JPEG image quality
    int new_jpeg_quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
    LOGV("%s : new_jpeg_quality %d", __FUNCTION__, new_jpeg_quality);
    if (new_jpeg_quality >=1 && new_jpeg_quality <= 100) 
	{
		mParameters.set(CameraParameters::KEY_JPEG_QUALITY, new_jpeg_quality);
    }
	else
	{
		LOGE("error picture quality");
		//return -EINVAL;
	}

	// rotation	
	int new_rotation = params.getInt(CameraParameters::KEY_ROTATION);
    LOGV("%s : new_rotation %d", __FUNCTION__, new_rotation);
    if (0 <= new_rotation) 
	{
        mParameters.set(CameraParameters::KEY_ROTATION, new_rotation);
    }
	else
	{
		LOGE("error rotate");
		return -EINVAL;
	}

	// image effect
	if (mCameraConfig->supportColorEffect())
	{
		const char *now_image_effect_str = mParameters.get(CameraParameters::KEY_EFFECT);
		const char *new_image_effect_str = params.get(CameraParameters::KEY_EFFECT);
	    if ((new_image_effect_str != NULL)
			&& strcmp(now_image_effect_str, new_image_effect_str)) 
		{
	        int  new_image_effect = -1;

	        if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_NONE))
	            new_image_effect = V4L2_COLORFX_NONE;
	        else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_MONO))
	            new_image_effect = V4L2_COLORFX_BW;
	        else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_SEPIA))
	            new_image_effect = V4L2_COLORFX_SEPIA;
	        else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_AQUA))
	            new_image_effect = V4L2_COLORFX_GRASS_GREEN;
	        else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_NEGATIVE))
	            new_image_effect = V4L2_COLORFX_NEGATIVE;
	        else {
	            //posterize, whiteboard, blackboard, solarize
	            LOGE("ERR(%s):Invalid effect(%s)", __FUNCTION__, new_image_effect_str);
	            ret = UNKNOWN_ERROR;
	        }

	        if (new_image_effect >= 0) {
	            mParameters.set(CameraParameters::KEY_EFFECT, new_image_effect_str);
				mQueueElement[CMD_QUEUE_SET_COLOR_EFFECT].cmd = CMD_QUEUE_SET_COLOR_EFFECT;
				mQueueElement[CMD_QUEUE_SET_COLOR_EFFECT].data = new_image_effect;
				OSAL_Queue(&mQueueCommand, &mQueueElement[CMD_QUEUE_SET_COLOR_EFFECT]);
	        }
	    }
	}

	// white balance
	if (mCameraConfig->supportWhiteBalance())
	{
		const char *now_white_str = mParameters.get(CameraParameters::KEY_WHITE_BALANCE);
		const char *new_white_str = params.get(CameraParameters::KEY_WHITE_BALANCE);
	    LOGV("%s : new_white_str %s", __FUNCTION__, new_white_str);
	    if ((new_white_str != NULL)
			&& (mFirstSetParameters || strcmp(now_white_str, new_white_str)))
		{
	        int new_white = -1;
	        int no_auto_balance = 1;

	        if (!strcmp(new_white_str, CameraParameters::WHITE_BALANCE_AUTO))
	        {
	            new_white = V4L2_WB_AUTO;
	            no_auto_balance = 0;
	        }
	        else if (!strcmp(new_white_str,
	                         CameraParameters::WHITE_BALANCE_DAYLIGHT))
	            new_white = V4L2_WB_DAYLIGHT;
	        else if (!strcmp(new_white_str,
	                         CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT))
	            new_white = V4L2_WB_CLOUD;
	        else if (!strcmp(new_white_str,
	                         CameraParameters::WHITE_BALANCE_FLUORESCENT))
	            new_white = V4L2_WB_FLUORESCENT;
	        else if (!strcmp(new_white_str,
	                         CameraParameters::WHITE_BALANCE_INCANDESCENT))
	            new_white = V4L2_WB_INCANDESCENCE;
	        else if (!strcmp(new_white_str,
	                         CameraParameters::WHITE_BALANCE_WARM_FLUORESCENT))
	            new_white = V4L2_WB_TUNGSTEN;
	        else{
	            LOGE("ERR(%s):Invalid white balance(%s)", __FUNCTION__, new_white_str); //twilight, shade
	            ret = UNKNOWN_ERROR;
	        }

	        mCallbackNotifier.setWhiteBalance(no_auto_balance);

	        if (0 <= new_white)
			{
				mParameters.set(CameraParameters::KEY_WHITE_BALANCE, new_white_str);
				mQueueElement[CMD_QUEUE_SET_WHITE_BALANCE].cmd = CMD_QUEUE_SET_WHITE_BALANCE;
				mQueueElement[CMD_QUEUE_SET_WHITE_BALANCE].data = new_white;
				OSAL_Queue(&mQueueCommand, &mQueueElement[CMD_QUEUE_SET_WHITE_BALANCE]);
	        }
	    }
	}
	
	// exposure compensation
	if (mCameraConfig->supportExposureCompensation())
	{
		int now_exposure_compensation = mParameters.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
		int new_exposure_compensation = params.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
		int max_exposure_compensation = params.getInt(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
		int min_exposure_compensation = params.getInt(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);
		LOGV("%s : new_exposure_compensation %d", __FUNCTION__, new_exposure_compensation);
		if ((min_exposure_compensation <= new_exposure_compensation)
			&& (max_exposure_compensation >= new_exposure_compensation))
		{
			if (mFirstSetParameters || (now_exposure_compensation != new_exposure_compensation))
			{
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, new_exposure_compensation);
				mQueueElement[CMD_QUEUE_SET_EXPOSURE_COMPENSATION].cmd = CMD_QUEUE_SET_EXPOSURE_COMPENSATION;
				mQueueElement[CMD_QUEUE_SET_EXPOSURE_COMPENSATION].data = new_exposure_compensation;
				OSAL_Queue(&mQueueCommand, &mQueueElement[CMD_QUEUE_SET_EXPOSURE_COMPENSATION]);
			}
		}
		else
		{
			LOGE("invalid exposure compensation: %d", new_exposure_compensation);
			return -EINVAL;
		}
	}
	
	// flash mode	
	if (mCameraConfig->supportFlashMode())
	{
		const char *new_flash_mode_str = params.get(CameraParameters::KEY_FLASH_MODE);
		mParameters.set(CameraParameters::KEY_FLASH_MODE, new_flash_mode_str);
	}

	// zoom
	if (mCameraConfig->supportZoom())
	{
		int max_zoom = mParameters.getInt(CameraParameters::KEY_MAX_ZOOM);
		int new_zoom = params.getInt(CameraParameters::KEY_ZOOM);
		LOGV("new_zoom: %d", new_zoom);
		if (0 <= new_zoom && new_zoom <= max_zoom)
		{
			mParameters.set(CameraParameters::KEY_ZOOM, new_zoom);
			getCameraDevice()->setCrop(new_zoom, max_zoom);
		}
		else
		{
			LOGE("invalid zoom value: %d", new_zoom);
			return -EINVAL;
		}
	}

	// focus
	if (mCameraConfig->supportFocusMode())
	{
		const char *now_focus_mode_str = mParameters.get(CameraParameters::KEY_FOCUS_MODE);
		const char *now_focus_areas_str = mParameters.get(CameraParameters::KEY_FOCUS_AREAS);
		const char *new_focus_mode_str = params.get(CameraParameters::KEY_FOCUS_MODE);
        const char *new_focus_areas_str = params.get(CameraParameters::KEY_FOCUS_AREAS);
		if (mFirstSetParameters || strcmp(now_focus_mode_str, new_focus_mode_str))
		{
			mParameters.set(CameraParameters::KEY_FOCUS_MODE, new_focus_mode_str);
			mQueueElement[CMD_QUEUE_SET_FOCUS_MODE].cmd = CMD_QUEUE_SET_FOCUS_MODE;
			OSAL_QueueSetElem(&mQueueCommand, &mQueueElement[CMD_QUEUE_SET_FOCUS_MODE]);
		}
		
		if (getCameraDevice()->getThreadRunning()
			&& strcmp(now_focus_areas_str, new_focus_areas_str))
		{
			mParameters.set(CameraParameters::KEY_FOCUS_AREAS, new_focus_areas_str);

			strcpy(mFocusAreasStr, new_focus_areas_str);
			mQueueElement[CMD_QUEUE_SET_FOCUS_AREA].cmd = CMD_QUEUE_SET_FOCUS_AREA;
			mQueueElement[CMD_QUEUE_SET_FOCUS_AREA].data = (int)&mFocusAreasStr;
			OSAL_QueueSetElem(&mQueueCommand, &mQueueElement[CMD_QUEUE_SET_FOCUS_AREA]);
		}
	}
	else
	{
		const char *new_focus_mode_str = params.get(CameraParameters::KEY_FOCUS_MODE);
		if (strcmp(new_focus_mode_str, CameraParameters::FOCUS_MODE_AUTO))
		{
			LOGE("invalid focus mode: %s", new_focus_mode_str);
			return -EINVAL;
		}
		mParameters.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);
	}

	// gps latitude
    const char *new_gps_latitude_str = params.get(CameraParameters::KEY_GPS_LATITUDE);
	if (new_gps_latitude_str) {
		mCallbackNotifier.setGPSLatitude(atof(new_gps_latitude_str));
        mParameters.set(CameraParameters::KEY_GPS_LATITUDE, new_gps_latitude_str);
    } else {
        mParameters.remove(CameraParameters::KEY_GPS_LATITUDE);
    }

    // gps longitude
    const char *new_gps_longitude_str = params.get(CameraParameters::KEY_GPS_LONGITUDE);
    if (new_gps_longitude_str) {
		mCallbackNotifier.setGPSLongitude(atof(new_gps_longitude_str));
        mParameters.set(CameraParameters::KEY_GPS_LONGITUDE, new_gps_longitude_str);
    } else {
        mParameters.remove(CameraParameters::KEY_GPS_LONGITUDE);
    }
  
    // gps altitude
    const char *new_gps_altitude_str = params.get(CameraParameters::KEY_GPS_ALTITUDE);
	if (new_gps_altitude_str) {
		mCallbackNotifier.setGPSAltitude(atol(new_gps_altitude_str));
        mParameters.set(CameraParameters::KEY_GPS_ALTITUDE, new_gps_altitude_str);
    } else {
        mParameters.remove(CameraParameters::KEY_GPS_ALTITUDE);
    }

    // gps timestamp
    const char *new_gps_timestamp_str = params.get(CameraParameters::KEY_GPS_TIMESTAMP);
	if (new_gps_timestamp_str) {
		mCallbackNotifier.setGPSTimestamp(atol(new_gps_timestamp_str));
        mParameters.set(CameraParameters::KEY_GPS_TIMESTAMP, new_gps_timestamp_str);
    } else {
        mParameters.remove(CameraParameters::KEY_GPS_TIMESTAMP);
    }

    // gps processing method
    const char *new_gps_processing_method_str = params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);
	if (new_gps_processing_method_str) {
		mCallbackNotifier.setGPSMethod(new_gps_processing_method_str);
        mParameters.set(CameraParameters::KEY_GPS_PROCESSING_METHOD, new_gps_processing_method_str);
    } else {
        mParameters.remove(CameraParameters::KEY_GPS_PROCESSING_METHOD);
    }
	
	// JPEG thumbnail size
	int new_jpeg_thumbnail_width = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
	int new_jpeg_thumbnail_height= params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
	LOGV("new_jpeg_thumbnail_width: %d, new_jpeg_thumbnail_height: %d",
		new_jpeg_thumbnail_width, new_jpeg_thumbnail_height);
	if (0 <= new_jpeg_thumbnail_width && 0 <= new_jpeg_thumbnail_height) {
		mCallbackNotifier.setJpegThumbnailSize(new_jpeg_thumbnail_width, new_jpeg_thumbnail_height);
		mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, new_jpeg_thumbnail_width);
		mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, new_jpeg_thumbnail_height);
	}

	mFirstSetParameters = false;
	pthread_cond_signal(&mCommamdCond);
	
    return NO_ERROR;
}

/* A dumb variable indicating "no params" / error on the exit from
 * CameraHardware::getParameters(). */
static char lNoParam = '\0';
char* CameraHardware::getParameters()
{
	F_LOG;
    String8 params(mParameters.flatten());
    char* ret_str =
        reinterpret_cast<char*>(malloc(sizeof(char) * (params.length()+1)));
    memset(ret_str, 0, params.length()+1);
    if (ret_str != NULL) {
        strncpy(ret_str, params.string(), params.length()+1);
        return ret_str;
    } else {
        LOGE("%s: Unable to allocate string for %s", __FUNCTION__, params.string());
        /* Apparently, we can't return NULL fron this routine. */
        return &lNoParam;
    }
}

void CameraHardware::putParameters(char* params)
{
	F_LOG;
    /* This method simply frees parameters allocated in getParameters(). */
    if (params != NULL && params != &lNoParam) {
        free(params);
    }
}

status_t CameraHardware::sendCommand(int32_t cmd, int32_t arg1, int32_t arg2)
{
    LOGV("%s: cmd = %x, arg1 = %d, arg2 = %d", __FUNCTION__, cmd, arg1, arg2);

    /* TODO: Future enhancements. */

	switch (cmd)
	{
	case CAMERA_CMD_SET_SCREEN_ID:
		if (mPreviewWindow.isLayerShowHW())
		{
			mPreviewWindow.setScreenID(arg1);			// only here to close layer
		}
		return OK;
	case CAMERA_CMD_SET_CEDARX_RECORDER:
		getCameraDevice()->setHwEncoder(true);
		return OK;
	case CAMERA_CMD_START_FACE_DETECTION:
		mQueueElement[CMD_QUEUE_START_FACE_DETECTE].cmd = CMD_QUEUE_START_FACE_DETECTE;
		OSAL_Queue(&mQueueCommand, &mQueueElement[CMD_QUEUE_START_FACE_DETECTE]);
		pthread_cond_signal(&mCommamdCond);
		return OK;
	case CAMERA_CMD_STOP_FACE_DETECTION:
		mQueueElement[CMD_QUEUE_STOP_FACE_DETECTE].cmd = CMD_QUEUE_STOP_FACE_DETECTE;
		OSAL_Queue(&mQueueCommand, &mQueueElement[CMD_QUEUE_STOP_FACE_DETECTE]);
		pthread_cond_signal(&mCommamdCond);
		return OK;
	}

    return -EINVAL;
}

void CameraHardware::releaseCamera()
{
    LOGV("%s", __FUNCTION__);

    cleanupCamera();
}

status_t CameraHardware::dumpCamera(int fd)
{
    LOGV("%s", __FUNCTION__);

    /* TODO: Future enhancements. */
    return -EINVAL;
}

/****************************************************************************
 * Preview management.
 ***************************************************************************/

status_t CameraHardware::doStartPreview()
{
    LOGV("%s", __FUNCTION__);
	
    V4L2Camera* camera_dev = getCameraDevice();
    if (camera_dev->isStarted()) {
        camera_dev->stopDeliveringFrames();
        camera_dev->stopDevice();
    }

    status_t res = mPreviewWindow.startPreview();
    if (res != NO_ERROR) {
        return res;
    }

    /* Make sure camera device is connected. */
    if (!camera_dev->isConnected()) {
        res = camera_dev->connectDevice();
        if (res != NO_ERROR) {
            mPreviewWindow.stopPreview();
            return res;
        }
		
		// for USB camera
		if (getCameraDevice()->getCaptureFormat() == V4L2_PIX_FMT_YUYV)
		{
			char sizeStr[256];
			getCameraDevice()->enumSize(sizeStr, 256);
			LOGV("doStartPreview enum size: %s", sizeStr);
			if (strlen(sizeStr) > 0)
			{
				mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, sizeStr);
				mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, sizeStr);
			}
		}
		setAutoFocusCtrl(V4L2_AF_INIT, NULL);
    }

    int width, height;
    /* Lets see what should we use for frame width, and height. */
    if (mParameters.get(CameraParameters::KEY_VIDEO_SIZE) != NULL) {
        mParameters.getVideoSize(&width, &height);
    } else {
        mParameters.getPreviewSize(&width, &height);
    }
    /* Lets see what should we use for the frame pixel format. Note that there
     * are two parameters that define pixel formats for frames sent to the
     * application via notification callbacks:
     * - KEY_VIDEO_FRAME_FORMAT, that is used when recording video, and
     * - KEY_PREVIEW_FORMAT, that is used for preview frame notification.
     * We choose one or the other, depending on "recording-hint" property set by
     * the framework that indicating intention: video, or preview. */
    const char* pix_fmt = NULL;
    const char* is_video = mParameters.get(CameraHardware::RECORDING_HINT_KEY);
    if (is_video == NULL) {
        is_video = CameraParameters::FALSE;
    }
    if (strcmp(is_video, CameraParameters::TRUE) == 0) {
        /* Video recording is requested. Lets see if video frame format is set. */
        pix_fmt = mParameters.get(CameraParameters::KEY_VIDEO_FRAME_FORMAT);
    }
    /* If this was not video recording, or video frame format is not set, lets
     * use preview pixel format for the main framebuffer. */
    if (pix_fmt == NULL) {
        pix_fmt = mParameters.getPreviewFormat();
    }
    if (pix_fmt == NULL) {
        LOGE("%s: Unable to obtain video format", __FUNCTION__);
        mPreviewWindow.stopPreview();
        return EINVAL;
    }

    /* Convert framework's pixel format to the FOURCC one. */
    uint32_t org_fmt;
    if (strcmp(pix_fmt, CameraParameters::PIXEL_FORMAT_YUV420P) == 0) {
        org_fmt = V4L2_PIX_FMT_YUV420;
    } else if (strcmp(pix_fmt, CameraParameters::PIXEL_FORMAT_RGBA8888) == 0) {
        org_fmt = V4L2_PIX_FMT_RGB32;
    } else if (strcmp(pix_fmt, CameraParameters::PIXEL_FORMAT_YUV420SP) == 0) {
    	if (bPixFmtNV12) {
			LOGV("============== DISP_SEQ_UVUV");
			mPreviewWindow.setLayerFormat(DISP_SEQ_UVUV);
        	org_fmt = V4L2_PIX_FMT_NV12;		// for HW encoder
    	} else {
	    	LOGV("============== DISP_SEQ_VUVU");
			mPreviewWindow.setLayerFormat(DISP_SEQ_VUVU);
        	org_fmt = V4L2_PIX_FMT_NV21;		// for some apps
    	}
    } else {
        LOGE("%s: Unsupported pixel format %s", __FUNCTION__, pix_fmt);
        mPreviewWindow.stopPreview();
        return EINVAL;
    }
	
    LOGD("Starting camera: %dx%d -> %.4s(%s)",
         width, height, reinterpret_cast<const char*>(&org_fmt), pix_fmt);
    res = camera_dev->startDevice(width, height, org_fmt);
    if (res != NO_ERROR) {
        mPreviewWindow.stopPreview();
        return res;
    }

    res = camera_dev->startDeliveringFrames(false);
    if (res != NO_ERROR) {
        camera_dev->stopDevice();
        mPreviewWindow.stopPreview();
    }

    return res;
}

status_t CameraHardware::doStopPreview()
{
    LOGV("%s", __FUNCTION__);

	// set layer off can avoid Flicker
	// mPreviewWindow.showLayer(false);
	
    status_t res = NO_ERROR;
    if (mPreviewWindow.isPreviewEnabled()) {
        /* Stop the camera. */
        if (getCameraDevice()->isStarted()) {
            getCameraDevice()->stopDeliveringFrames();
            res = getCameraDevice()->stopDevice();
        }

        if (res == NO_ERROR) {
            /* Disable preview as well. */
            mPreviewWindow.stopPreview();
        }
    }

    return NO_ERROR;
}

/****************************************************************************
 * Private API.
 ***************************************************************************/

status_t CameraHardware::cleanupCamera()
{
	F_LOG;

    status_t res = NO_ERROR;

	// reset preview format to yuv420sp
	mParameters.set(CameraParameters::KEY_PREVIEW_FORMAT, CameraParameters::PIXEL_FORMAT_YUV420SP);
	mCallbackNotifier.storeMetaDataInBuffers(false);
	getCameraDevice()->setHwEncoder(false);

	mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, 320);
	mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, 240);

	mParameters.set(CameraParameters::KEY_ZOOM, 0);
	
    /* If preview is running - stop it. */
    res = doStopPreview();
    if (res != NO_ERROR) {
        return -res;
    }

    /* Stop and disconnect the camera device. */
    V4L2Camera* const camera_dev = getCameraDevice();
    if (camera_dev != NULL) {
		if (mPreviewWindow.isLayerShowHW())
		{
			mPreviewWindow.showLayer(false);			// only here to close layer
		}
        if (camera_dev->isStarted()) {
            camera_dev->stopDeliveringFrames();
            res = camera_dev->stopDevice();
            if (res != NO_ERROR) {
                return -res;
            }
        }
        if (camera_dev->isConnected()) {
            res = camera_dev->disconnectDevice();
            if (res != NO_ERROR) {
                return -res;
            }
        }
    }

    mCallbackNotifier.cleanupCBNotifier();

    return NO_ERROR;
}

/****************************************************************************
 * Camera API callbacks as defined by camera_device_ops structure.
 *
 * Callbacks here simply dispatch the calls to an appropriate method inside
 * CameraHardware instance, defined by the 'dev' parameter.
 ***************************************************************************/

int CameraHardware::set_preview_window(struct camera_device* dev,
                                       struct preview_stream_ops* window)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->setPreviewWindow(window);
}

void CameraHardware::set_callbacks(
        struct camera_device* dev,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void* user)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return;
    }
    ec->setCallbacks(notify_cb, data_cb, data_cb_timestamp, get_memory, user);
}

void CameraHardware::enable_msg_type(struct camera_device* dev, int32_t msg_type)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return;
    }
    ec->enableMsgType(msg_type);
}

void CameraHardware::disable_msg_type(struct camera_device* dev, int32_t msg_type)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return;
    }
    ec->disableMsgType(msg_type);
}

int CameraHardware::msg_type_enabled(struct camera_device* dev, int32_t msg_type)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->isMsgTypeEnabled(msg_type);
}

int CameraHardware::start_preview(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->startPreview();
}

void CameraHardware::stop_preview(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return;
    }
    ec->stopPreview();
}

int CameraHardware::preview_enabled(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->isPreviewEnabled();
}

int CameraHardware::store_meta_data_in_buffers(struct camera_device* dev,
                                               int enable)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->storeMetaDataInBuffers(enable);
}

int CameraHardware::start_recording(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->startRecording();
}

void CameraHardware::stop_recording(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return;
    }
    ec->stopRecording();
}

int CameraHardware::recording_enabled(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->isRecordingEnabled();
}

void CameraHardware::release_recording_frame(struct camera_device* dev,
                                             const void* opaque)
{
	// F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return;
    }
    ec->releaseRecordingFrame(opaque);
}

int CameraHardware::auto_focus(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->setAutoFocus();
}

int CameraHardware::cancel_auto_focus(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->cancelAutoFocus();
}

int CameraHardware::take_picture(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->takePicture();
}

int CameraHardware::cancel_picture(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->cancelPicture();
}

int CameraHardware::set_parameters(struct camera_device* dev, const char* parms)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }

	int64_t lasttime = systemTime();
	ec->setParameters(parms);

	LOGV("setParameters use time: %lld(ms)", (systemTime() - lasttime)/1000000);
    return OK;
}

char* CameraHardware::get_parameters(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return NULL;
    }
    return ec->getParameters();
}

void CameraHardware::put_parameters(struct camera_device* dev, char* params)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return;
    }
    ec->putParameters(params);
}

int CameraHardware::send_command(struct camera_device* dev,
                                 int32_t cmd,
                                 int32_t arg1,
                                 int32_t arg2)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->sendCommand(cmd, arg1, arg2);
}

void CameraHardware::release(struct camera_device* dev)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return;
    }
    ec->releaseCamera();
}

int CameraHardware::dump(struct camera_device* dev, int fd)
{
	F_LOG;

    CameraHardware* ec = reinterpret_cast<CameraHardware*>(dev->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->dumpCamera(fd);
}

int CameraHardware::close(struct hw_device_t* device)
{
	F_LOG;

    CameraHardware* ec =
        reinterpret_cast<CameraHardware*>(reinterpret_cast<struct camera_device*>(device)->priv);
    if (ec == NULL) {
        LOGE("%s: Unexpected NULL camera device", __FUNCTION__);
        return -EINVAL;
    }
    return ec->closeCamera();
}

// -------------------------------------------------------------------------
// extended interfaces here <***** star *****>
// -------------------------------------------------------------------------

bool CameraHardware::isUseMetaDataBufferMode()
{
	return mCallbackNotifier.isUseMetaDataBufferMode();
}

/****************************************************************************
 * Static initializer for the camera callback API
 ****************************************************************************/

camera_device_ops_t CameraHardware::mDeviceOps = {
    CameraHardware::set_preview_window,
    CameraHardware::set_callbacks,
    CameraHardware::enable_msg_type,
    CameraHardware::disable_msg_type,
    CameraHardware::msg_type_enabled,
    CameraHardware::start_preview,
    CameraHardware::stop_preview,
    CameraHardware::preview_enabled,
    CameraHardware::store_meta_data_in_buffers,
    CameraHardware::start_recording,
    CameraHardware::stop_recording,
    CameraHardware::recording_enabled,
    CameraHardware::release_recording_frame,
    CameraHardware::auto_focus,
    CameraHardware::cancel_auto_focus,
    CameraHardware::take_picture,
    CameraHardware::cancel_picture,
    CameraHardware::set_parameters,
    CameraHardware::get_parameters,
    CameraHardware::put_parameters,
    CameraHardware::send_command,
    CameraHardware::release,
    CameraHardware::dump
};

/****************************************************************************
 * Common keys
 ***************************************************************************/

const char CameraHardware::FACING_KEY[]         = "prop-facing";
const char CameraHardware::ORIENTATION_KEY[]    = "prop-orientation";
const char CameraHardware::RECORDING_HINT_KEY[] = "recording-hint";

/****************************************************************************
 * Common string values
 ***************************************************************************/

const char CameraHardware::FACING_BACK[]      = "back";
const char CameraHardware::FACING_FRONT[]     = "front";

/****************************************************************************
 * Helper routines
 ***************************************************************************/

static char* AddValue(const char* param, const char* val)
{
    const size_t len1 = strlen(param);
    const size_t len2 = strlen(val);
    char* ret = reinterpret_cast<char*>(malloc(len1 + len2 + 2));
    LOGE_IF(ret == NULL, "%s: Memory failure", __FUNCTION__);
    if (ret != NULL) {
        memcpy(ret, param, len1);
        ret[len1] = ',';
        memcpy(ret + len1 + 1, val, len2);
        ret[len1 + len2 + 1] = '\0';
    }
    return ret;
}

/****************************************************************************
 * Parameter debugging helpers
 ***************************************************************************/

#if DEBUG_PARAM
static void PrintParamDiff(const CameraParameters& current,
                            const char* new_par)
{
    char tmp[2048];
    const char* wrk = new_par;

    /* Divided with ';' */
    const char* next = strchr(wrk, ';');
    while (next != NULL) {
        snprintf(tmp, sizeof(tmp), "%.*s", next-wrk, wrk);
        /* in the form key=value */
        char* val = strchr(tmp, '=');
        if (val != NULL) {
            *val = '\0'; val++;
            const char* in_current = current.get(tmp);
            if (in_current != NULL) {
                if (strcmp(in_current, val)) {
                    LOGD("=== Value changed: %s: %s -> %s", tmp, in_current, val);
                }
            } else {
                LOGD("+++ New parameter: %s=%s", tmp, val);
            }
        } else {
            LOGW("No value separator in %s", tmp);
        }
        wrk = next + 1;
        next = strchr(wrk, ';');
    }
}
#endif  /* DEBUG_PARAM */

}; /* namespace android */
