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
 * Contains implementation of a class PreviewWindow that encapsulates
 * functionality of a preview window set via set_preview_window camera HAL API.
 */

#define LOG_TAG "PreviewWindow"
#include "CameraDebug.h"

#include <ui/Rect.h>
#include <ui/GraphicBufferMapper.h>
#include <type_camera.h>
#include <hwcomposer.h>
#ifdef __SUN4I__
#include <drv_display_sun4i.h>
#else
#include <drv_display_sun5i.h>
#endif

#include "V4L2Camera.h"
#include "PreviewWindow.h"

namespace android {

static void NV12ToNV21(const void* nv12, void* nv21, int width, int height)
{	
	char * src_uv = (char *)nv12 + width * height;
	char * dst_uv = (char *)nv21 + width * height;

	memcpy(nv21, nv12, width * height);

	for(int i = 0; i < width * height / 2; i += 2)
	{
		*(dst_uv + i) = *(src_uv + i + 1);
		*(dst_uv + i + 1) = *(src_uv + i);
	}
}

static void NV12ToNV21_shift(const void* nv12, void* nv21, int width, int height)
{	
	char * src_uv = (char *)nv12 + width * height;
	char * dst_uv = (char *)nv21 + width * height;

	memcpy(nv21, nv12, width * height);
	memcpy(dst_uv, src_uv + 1, width * height / 2 - 1);
}

PreviewWindow::PreviewWindow()
    : mPreviewWindow(NULL),
      mPreviewFrameWidth(0),
      mPreviewFrameHeight(0),
      mPreviewEnabled(false),
      mOverlayFirstFrame(true),
      mShouldAdjustDimensions(true),
      mLayerFormat(-1),
      mScreenID(0),
      mNewCrop(false)
{
	F_LOG;
	memset(&mRectCrop, 0, sizeof(mRectCrop));
}

PreviewWindow::~PreviewWindow()
{
	F_LOG;
	mPreviewWindow = NULL;
	mPreviewWindow->perform = NULL;
}

/****************************************************************************
 * Camera API
 ***************************************************************************/

status_t PreviewWindow::setPreviewWindow(struct preview_stream_ops* window,
                                         int preview_fps)
{
    LOGV("%s: current: %p -> new: %p", __FUNCTION__, mPreviewWindow, window);
	
    status_t res = NO_ERROR;
    Mutex::Autolock locker(&mObjectLock);

    /* Reset preview info. */
    mPreviewFrameWidth = mPreviewFrameHeight = 0;

    if (window != NULL) {
        /* The CPU will write each frame to the preview window buffer.
         * Note that we delay setting preview window buffer geometry until
         * frames start to come in. */
        res = window->set_usage(window, GRALLOC_USAGE_SW_WRITE_OFTEN);
        if (res != NO_ERROR) {
            window = NULL;
            res = -res; // set_usage returns a negative errno.
            LOGE("%s: Error setting preview window usage %d -> %s",
                 __FUNCTION__, res, strerror(res));
        }
    }
    mPreviewWindow = window;

    return res;
}

status_t PreviewWindow::startPreview()
{
    LOGV("%s", __FUNCTION__);

    Mutex::Autolock locker(&mObjectLock);
    mPreviewEnabled = true;
	mOverlayFirstFrame = true;
	
    return NO_ERROR;
}

void PreviewWindow::stopPreview()
{
    LOGV("%s", __FUNCTION__);

    Mutex::Autolock locker(&mObjectLock);
    mPreviewEnabled = false;
	mOverlayFirstFrame = false;
}

/****************************************************************************
 * Public API
 ***************************************************************************/
bool PreviewWindow::onNextFrameAvailable(const void* frame,
										 int video_fmt,
										 nsecs_t timestamp,
										 V4L2Camera* camera_dev,
                                         bool bUseMataData)
{
	if (bUseMataData)
	{
		return onNextFrameAvailableHW(frame, video_fmt, timestamp, camera_dev);
	}
	else
	{
		return onNextFrameAvailableSW(frame, video_fmt, timestamp, camera_dev);
	}
}

bool PreviewWindow::onNextFrameAvailableHW(const void* frame,
										 int video_fmt,
                                         nsecs_t timestamp,
                                         V4L2Camera* camera_dev)
{
    int res;
	V4L2BUF_t * pv4l2_buf = (V4L2BUF_t *)frame;
	
    Mutex::Autolock locker(&mObjectLock);

	if (!isPreviewEnabled() || mPreviewWindow == NULL) 
	{
        return true;
    }
	
    /* Make sure that preview window dimensions are OK with the camera device */
    if (adjustPreviewDimensions(camera_dev) || mShouldAdjustDimensions) 
	{
        LOGD("%s: Adjusting preview windows %p geometry to %dx%d",
             __FUNCTION__, mPreviewWindow, mPreviewFrameWidth,
             mPreviewFrameHeight);
        res = mPreviewWindow->set_buffers_geometryex(mPreviewWindow,
                                                   mPreviewFrameWidth,
                                                   mPreviewFrameHeight,
                                                   HWC_FORMAT_DEFAULT,
                                                   mScreenID);
        if (res != NO_ERROR) {
            LOGE("%s: Error in set_buffers_geometry %d -> %s",
                 __FUNCTION__, -res, strerror(-res));

			mShouldAdjustDimensions = true;
            return false;
        }

		mPreviewWindow->perform(mPreviewWindow, NATIVE_WINDOW_SETPARAMETER, HWC_LAYER_SETFORMAT, mLayerFormat);
		mShouldAdjustDimensions = false;

		mPreviewWindow->set_crop(mPreviewWindow, 
			mRectCrop.left, mRectCrop.top, mRectCrop.right, mRectCrop.bottom);

		mNewCrop = false;

		LOGV("first hw: [%d, %d, %d, %d]", mRectCrop.left,
										mRectCrop.top,
										mRectCrop.right,
										mRectCrop.bottom);
    }

	libhwclayerpara_t overlay_para;

	overlay_para.bProgressiveSrc = 1;
	overlay_para.bTopFieldFirst = 1;
	overlay_para.pVideoInfo.frame_rate = 25000;

	overlay_para.top_y 		= (unsigned int)pv4l2_buf->addrPhyY;
	overlay_para.top_c 		= (unsigned int)pv4l2_buf->addrPhyY + mPreviewFrameWidth * mPreviewFrameHeight;
	overlay_para.bottom_y 	= 0;
	overlay_para.bottom_c 	= 0;
	overlay_para.number 	= 0;

	if (mOverlayFirstFrame)
	{
		LOGV("first frame true");
		overlay_para.first_frame_flg = 1;
		mOverlayFirstFrame = false;
	}
	else
	{
		overlay_para.first_frame_flg = 0;
	}
	
	// LOGV("addrY: %x, addrC: %x, WXH: %dx%d", overlay_para.top_y, overlay_para.top_c, mPreviewFrameWidth, mPreviewFrameHeight);

	if (mNewCrop)
	{
		mPreviewWindow->set_crop(mPreviewWindow, 
			mRectCrop.left, mRectCrop.top, mRectCrop.right, mRectCrop.bottom);

		mNewCrop = false;
	}

	res = mPreviewWindow->perform(mPreviewWindow, NATIVE_WINDOW_SETPARAMETER, HWC_LAYER_SETFRAMEPARA, (uint32_t)&overlay_para);
	if (res != OK)
	{
		LOGE("NATIVE_WINDOW_SETPARAMETER failed");
		return false;
	}

	if (mLayerShowHW == 0)
	{
		showLayer(true);
	}

	return true;
}

bool PreviewWindow::onNextFrameAvailableSW(const void* frame,
										 int video_fmt,
                                         nsecs_t timestamp,
                                         V4L2Camera* camera_dev)
{
    int res;
    Mutex::Autolock locker(&mObjectLock);

	// LOGD("%s, timestamp: %lld", __FUNCTION__, timestamp);

    if (!isPreviewEnabled() || mPreviewWindow == NULL) 
	{
        return true;
    }

    /* Make sure that preview window dimensions are OK with the camera device */
    if (adjustPreviewDimensions(camera_dev) || mShouldAdjustDimensions) {
        /* Need to set / adjust buffer geometry for the preview window.
         * Note that in the emulator preview window uses only RGB for pixel
         * formats. */
        LOGD("%s: Adjusting preview windows %p geometry to %dx%d",
             __FUNCTION__, mPreviewWindow, mPreviewFrameWidth,
             mPreviewFrameHeight);
		
		int format = HAL_PIXEL_FORMAT_YCrCb_420_SP;
		LOGV("preview format: HAL_PIXEL_FORMAT_YCrCb_420_SP");

        res = mPreviewWindow->set_buffers_geometry(mPreviewWindow,
                                                   mPreviewFrameWidth,
                                                   mPreviewFrameHeight,
												   format);
        if (res != NO_ERROR) {
            LOGE("%s: Error in set_buffers_geometry %d -> %s",
                 __FUNCTION__, -res, strerror(-res));
            // return false;
        }
		mShouldAdjustDimensions = false;

		res = mPreviewWindow->set_buffer_count(mPreviewWindow, 3);
		if (res != 0) 
		{
	        LOGE("native_window_set_buffer_count failed: %s (%d)", strerror(-res), -res);

	        if ( ENODEV == res ) {
	            LOGE("Preview surface abandoned!");
	            mPreviewWindow = NULL;
	        }

	        return false;
	    }

		mPreviewWindow->set_crop(mPreviewWindow, 
			mRectCrop.left, mRectCrop.top, mRectCrop.right, mRectCrop.bottom);

		mNewCrop = false;

		LOGV("first sw: [%d, %d, %d, %d]", mRectCrop.left,
										mRectCrop.top,
										mRectCrop.right,
										mRectCrop.bottom);
    }

    /*
     * Push new frame to the preview window.
     */

    /* Dequeue preview window buffer for the frame. */
    buffer_handle_t* buffer = NULL;
    int stride = 0;
    res = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buffer, &stride);
    if (res != NO_ERROR || buffer == NULL) {
        LOGE("%s: Unable to dequeue preview window buffer: %d -> %s",
            __FUNCTION__, -res, strerror(-res));

		int undequeued = 0;
		mPreviewWindow->get_min_undequeued_buffer_count(mPreviewWindow, &undequeued);
		LOGW("now undequeued: %d", undequeued);
		
        return false;
    }

    /* Let the preview window to lock the buffer. */
    res = mPreviewWindow->lock_buffer(mPreviewWindow, buffer);
    if (res != NO_ERROR) {
        LOGE("%s: Unable to lock preview window buffer: %d -> %s",
             __FUNCTION__, -res, strerror(-res));
        mPreviewWindow->cancel_buffer(mPreviewWindow, buffer);
        return false;
    }

    /* Now let the graphics framework to lock the buffer, and provide
     * us with the framebuffer data address. */
    void* img = NULL;
    const Rect rect(mPreviewFrameWidth, mPreviewFrameHeight);
    GraphicBufferMapper& grbuffer_mapper(GraphicBufferMapper::get());
    res = grbuffer_mapper.lock(*buffer, GRALLOC_USAGE_SW_WRITE_OFTEN, rect, &img);
    if (res != NO_ERROR) {
        LOGE("%s: grbuffer_mapper.lock failure: %d -> %s",
             __FUNCTION__, res, strerror(res));
        mPreviewWindow->cancel_buffer(mPreviewWindow, buffer);
        return false;
    }

	if (mNewCrop)
	{
		mPreviewWindow->set_crop(mPreviewWindow, 
			mRectCrop.left, mRectCrop.top, mRectCrop.right, mRectCrop.bottom);

		mNewCrop = false;
	}

	if (video_fmt == V4L2_PIX_FMT_NV21)
	{
		memcpy(img, frame, mPreviewFrameWidth * mPreviewFrameHeight * 3/2);
	}
	else
	{
		NV12ToNV21_shift(frame, img, mPreviewFrameWidth, mPreviewFrameHeight);
	}
	mPreviewWindow->enqueue_buffer(mPreviewWindow, buffer);

    grbuffer_mapper.unlock(*buffer);

	return true;
}

/***************************************************************************
 * Private API
 **************************************************************************/

bool PreviewWindow::adjustPreviewDimensions(V4L2Camera* camera_dev)
{
	// F_LOG;
    /* Match the cached frame dimensions against the actual ones. */
    if (mPreviewFrameWidth == camera_dev->getFrameWidth() &&
        mPreviewFrameHeight == camera_dev->getFrameHeight()) {
        /* They match. */
        return false;
    }

    /* They don't match: adjust the cache. */
    mPreviewFrameWidth = camera_dev->getFrameWidth();
    mPreviewFrameHeight = camera_dev->getFrameHeight();

	mShouldAdjustDimensions = false;
    return true;
}

int PreviewWindow::showLayer(bool on)
{
	LOGV("%s, %s", __FUNCTION__, on ? "on" : "off");
	mLayerShowHW = on ? 1 : 0;
	if (mPreviewWindow != NULL && mPreviewWindow->perform != NULL)
	{
		mPreviewWindow->perform(mPreviewWindow, NATIVE_WINDOW_SETPARAMETER, HWC_LAYER_SHOW, mLayerShowHW);
	}
	return OK;
}

int PreviewWindow::setLayerFormat(int fmt)
{
	LOGV("%s, %d", __FUNCTION__, fmt);
	mLayerFormat = fmt;	
	mShouldAdjustDimensions = true;
	return OK;
}

int PreviewWindow::setScreenID(int id)
{
	LOGV("%s, id: %d", __FUNCTION__, id);
	mScreenID = id;
	if (mPreviewWindow != NULL && mPreviewWindow->perform != NULL)
	{
		mPreviewWindow->perform(mPreviewWindow, NATIVE_WINDOW_SETPARAMETER, HWC_LAYER_SETMODE, mScreenID);
	}
	return OK;
}

}; /* namespace android */
