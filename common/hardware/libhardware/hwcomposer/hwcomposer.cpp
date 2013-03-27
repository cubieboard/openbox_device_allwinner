/*
 * Copyright (C) 2010 The Android Open Source Project
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

//#define LOG_NDEBUG 0

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hwcomposer.h>
#include <drv_display_sun4i.h>
#include <fb.h>
#include <EGL/egl.h>

#include "hwccomposer_priv.h"
#include <cutils/properties.h> 
#include <stdlib.h>

/*****************************************************************************/
static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = 
{
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = 
{
    common: 
    {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Sample hwcomposer module",
        author: "The Android Open Source Project",
        methods: &hwc_module_methods,
    }
};


static int hwc_release(sun4i_hwc_context_t *ctx)
{
	unsigned long               args[4]={0};
    unsigned int                screen_idx;
    int ret = 0;

    LOGD("####hwc_release\n");

    for(screen_idx=0; screen_idx<2; screen_idx++)
    {
        if(((screen_idx == 0) && (ctx->mode==HWC_MODE_SCREEN0 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_BE || ctx->mode==HWC_MODE_SCREEN0_GPU))
            || ((screen_idx == 1) && (ctx->mode == HWC_MODE_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_TO_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1)))
        {
            if(ctx->video_layerhdl[screen_idx] != 0)
            {
                __disp_tv_mode_t            hdmi_mode;
                
                args[0]                         = screen_idx;
                args[1]                         = ctx->video_layerhdl[screen_idx];
                ioctl(ctx->dispfd, DISP_CMD_LAYER_CLOSE,args);

                args[0]                         = screen_idx;
                args[1]                         = ctx->video_layerhdl[screen_idx];
                ret = ioctl(ctx->dispfd, DISP_CMD_VIDEO_STOP, args);

                args[0] = screen_idx;
                hdmi_mode = (__disp_tv_mode_t)ioctl(ctx->dispfd,DISP_CMD_HDMI_GET_MODE,(unsigned long)args);
                if(hdmi_mode == DISP_TV_MOD_1080P_24HZ_3D_FP || hdmi_mode == DISP_TV_MOD_720P_50HZ_3D_FP || hdmi_mode == DISP_TV_MOD_720P_60HZ_3D_FP)
                {
                	 __disp_layer_info_t         tmpLayerAttr;
                   
                   args[0]                         = screen_idx;
                   args[1]                         = ctx->ui_layerhdl[screen_idx];
                   args[2]                         = (unsigned long) (&tmpLayerAttr);
                   ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_GET_PARA, args);

                   tmpLayerAttr.scn_win.x = ctx->org_scn_win.x;
                   tmpLayerAttr.scn_win.y = ctx->org_scn_win.y;
                   tmpLayerAttr.scn_win.width = ctx->org_scn_win.width;
                   tmpLayerAttr.scn_win.height = ctx->org_scn_win.height;
                   
                   args[0]                         = screen_idx;
                   args[1]                         = ctx->ui_layerhdl[screen_idx];
                   args[2]                         = (unsigned long) (&tmpLayerAttr);
                   ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_SET_PARA, args);
                            
                    args[0] = screen_idx;
                    ret = ioctl(ctx->dispfd,DISP_CMD_HDMI_OFF,(unsigned long)args);
            
                    args[0] = screen_idx;
                    args[1] = ctx->org_hdmi_mode;
                    ioctl(ctx->dispfd,DISP_CMD_HDMI_SET_MODE,(unsigned long)args);
                    
                    args[0] = screen_idx;
                    ret = ioctl(ctx->dispfd,DISP_CMD_HDMI_ON,(unsigned long)args);
                }
            }
        }
    }
    ctx->status[0] &= (~(HWC_STATUS_OPENED | HWC_STATUS_COMPOSITED));
    ctx->status[1] &= (~(HWC_STATUS_OPENED | HWC_STATUS_COMPOSITED));
    LOGV("####ctx->status[0]=%d in hwc_release", screen_idx,ctx->status[0]);
    LOGV("####ctx->status[1]=%d in hwc_release", screen_idx,ctx->status[1]);
    
	return ret;
}

static void hwc_computer_rect(sun4i_hwc_context_t *ctx, int screen_idx, hwc_rect_t *rect_out, hwc_rect_t *rect_in)
{
    int							ret;
    unsigned long               args[4]={0};
    unsigned int                screen_in_width;
    unsigned int                screen_in_height;
    unsigned int temp_x,temp_y,temp_w,temp_h;
    int x,y,w,h,mid_x,mid_y;
    struct fb_var_screeninfo    var;

    if(rect_in->left >= rect_in->right || rect_in->top >= rect_in->bottom)
    {
        LOGV("para error in hwc_computer_rect,(left:%d,right:%d,top:%d,bottom:%d)\n", rect_in->left, rect_in->right, rect_in->top, rect_in->bottom);
        return;
    }

    if(ctx->mode == HWC_MODE_SCREEN0_GPU)
    {
        screen_in_width                     = ctx->screen_para.app_width[screen_idx];
        screen_in_height                    = ctx->screen_para.app_height[screen_idx];
    }
    else
    {
        if(ctx->mode == HWC_MODE_SCREEN1)
        {
            ioctl(ctx->mFD_fb[1], FBIOGET_VSCREENINFO, &var);
        }
        else
        {
            ioctl(ctx->mFD_fb[0], FBIOGET_VSCREENINFO, &var);
        }
        screen_in_width                     = var.xres;
        screen_in_height                    = var.yres;
    }


    LOGV("####in:%d,%d,%d,%d;%d,%d\n", rect_in->left, rect_in->top, rect_in->right - rect_in->left, rect_in->bottom-rect_in->top,screen_in_width, screen_in_height);

    
    if(ctx->cur_3denable == true)
    {
    	rect_out->left = 0;
    	rect_out->right = ctx->screen_para.width[screen_idx];
    	rect_out->top = 0;
    	rect_out->bottom = ctx->screen_para.height[screen_idx];
    	
    	return ;
    }
    
    temp_x = rect_in->left;
    temp_w = rect_in->right - rect_in->left;
    temp_y = rect_in->top;
    temp_h = rect_in->bottom - rect_in->top;

	mid_x = temp_x + temp_w/2;
	mid_y = temp_y + temp_h/2;

	mid_x = mid_x * ctx->screen_para.valid_width[screen_idx] / screen_in_width;
	mid_y = mid_y * ctx->screen_para.valid_height[screen_idx] / screen_in_height;

    if(((mid_x - ctx->screen_para.valid_width[screen_idx]/2) > ctx->screen_para.valid_width[screen_idx]/4) ||
        ((ctx->screen_para.valid_width[screen_idx]/2 - mid_x) > ctx->screen_para.valid_width[screen_idx]/4))//preview
    {
        w = temp_w * ctx->screen_para.valid_width[screen_idx] / screen_in_width;
        h = temp_h * ctx->screen_para.valid_height[screen_idx] / screen_in_height;
        x = mid_x - (w/2);
        y = mid_y - (h/2);
        x += (ctx->screen_para.width[screen_idx] - ctx->screen_para.valid_width[screen_idx])/2;
        y += (ctx->screen_para.height[screen_idx] - ctx->screen_para.valid_height[screen_idx])/2;
    }
    else
    {
    	if(!ctx->b_video_in_valid_area)
    	{
        	if((screen_in_width == ctx->screen_para.width[screen_idx]) && (screen_in_height == ctx->screen_para.height[screen_idx]))
        	{
        	    rect_out->left = rect_in->left;
        	    rect_out->right = rect_in->right;
        	    rect_out->top = rect_in->top;
        	    rect_out->bottom = rect_in->bottom;
        	    return ;
        	}
        
       	 	mid_x += (ctx->screen_para.width[screen_idx] - ctx->screen_para.valid_width[screen_idx])/2;
       	 	mid_y += (ctx->screen_para.height[screen_idx] - ctx->screen_para.valid_height[screen_idx])/2;
		}
		
    	if(mid_x * temp_h >= mid_y * temp_w)
    	{
    	    y = 0;
    	    h = mid_y * 2;
    	    w = h * temp_w / temp_h;
    	    x = mid_x - (w/2);
    	}
    	else
    	{
    	    x = 0;
    	    w = mid_x * 2;
    	    h = w * temp_h / temp_w;
    	    y = mid_y - (h/2);
    	}
    	
    	if(ctx->b_video_in_valid_area)
    	{
       	 	x += (ctx->screen_para.width[screen_idx] - ctx->screen_para.valid_width[screen_idx])/2;
       	 	y += (ctx->screen_para.height[screen_idx] - ctx->screen_para.valid_height[screen_idx])/2;
    	}
	}
	
	temp_x = x;
	temp_y = y;
	temp_w = w;
	temp_h = h;

    rect_out->left = temp_x;
    rect_out->right = temp_x + temp_w;
    rect_out->top = temp_y;
    rect_out->bottom = temp_y + temp_h;

	LOGV("####out:%d,%d,%d,%d;%d,%d\n", temp_x, temp_y, temp_w, temp_h,ctx->screen_para.valid_width[screen_idx], ctx->screen_para.valid_height[screen_idx],
	    ctx->screen_para.width[screen_idx], ctx->screen_para.height[screen_idx]);
}

static int hwc_set_rect(hwc_composer_device_t *dev,hwc_layer_list_t* list)
{
    int 						ret = 0;
    sun4i_hwc_context_t   		*ctx = (sun4i_hwc_context_t *)dev;
    unsigned long               args[4]={0};
    int screen_idx;
    int have_overlay = 0;

	//LOGV("---------------hwc_set_rect %d", list->numHwLayers);
    
	for (size_t i=0 ; i<list->numHwLayers ; i++)         
    {         	  
        if(list->hwLayers[i].compositionType == HWC_OVERLAY)
        {
            if((ctx->cur_3d_out == HWC_3D_OUT_MODE_2D) || (ctx->cur_3d_out == HWC_3D_OUT_MODE_ANAGLAGH) || (ctx->cur_3d_out == HWC_3D_OUT_MODE_ORIGINAL))
            {
                hwc_rect_t croprect;
                hwc_rect_t displayframe_src, displayframe_dst;
                __disp_layer_info_t         layer_info;

                memcpy(&croprect, &list->hwLayers[i].sourceCrop, sizeof(hwc_rect_t));
                memcpy(&displayframe_src, &list->hwLayers[i].displayFrame, sizeof(hwc_rect_t));


                for(screen_idx=0; screen_idx<2; screen_idx++)
                {
                    if(((screen_idx == 0) && (ctx->mode==HWC_MODE_SCREEN0 || ctx->mode==HWC_MODE_SCREEN0_FE_VAR || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_BE || ctx->mode==HWC_MODE_SCREEN0_GPU))
                        || ((screen_idx == 1) && (ctx->mode==HWC_MODE_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_TO_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1)))
                    {
                        int screen_in_width;
                        int screen_in_height;
                        struct fb_var_screeninfo    var;

                        if(ctx->mode==HWC_MODE_SCREEN1)
                        {
                            ioctl(ctx->mFD_fb[1], FBIOGET_VSCREENINFO, &var);
                        }
                        else
                        {
                            ioctl(ctx->mFD_fb[0], FBIOGET_VSCREENINFO, &var);
                        }

                        if(ctx->mode == HWC_MODE_SCREEN0_GPU)
                        {
                            screen_in_width                     = ctx->screen_para.app_width[screen_idx];
                            screen_in_height                    = ctx->screen_para.app_height[screen_idx];
                        }
                        else
                        {
                            screen_in_width                     = var.xres;
                            screen_in_height                    = var.yres;
                        }

                        LOGV("####0:hwc_set_rect, src_left:%d,src_top:%d,src_right:%d,src_bottom:%d,  dst_left:%d,dst_top:%d,dst_right:%d,dst_bottom:%d\n",
                            croprect.left,croprect.top,croprect.right,croprect.bottom,displayframe_src.left,displayframe_src.top,displayframe_src.right,displayframe_src.bottom);
                        if(displayframe_src.left < 0)
                        {
                            croprect.left = croprect.left + ((0 - displayframe_src.left) * (croprect.right - croprect.left) / (displayframe_src.right - displayframe_src.left));
                            displayframe_src.left = 0;
                        }
                        if(displayframe_src.right > screen_in_width)
                        {
                            croprect.right = croprect.right - ((displayframe_src.right - screen_in_width) * (croprect.right - croprect.left) / (displayframe_src.right - displayframe_src.left));
                            displayframe_src.right = screen_in_width;
                        }
                        if(displayframe_src.top< 0)
                        {
                            croprect.top= croprect.top+ ((0 - displayframe_src.top) * (croprect.bottom- croprect.top) / (displayframe_src.bottom- displayframe_src.top));
                            displayframe_src.top= 0;
                        }
                        if(displayframe_src.bottom> screen_in_height)
                        {
                            croprect.bottom= croprect.bottom- ((displayframe_src.bottom- screen_in_height) * (croprect.bottom- croprect.top) / (displayframe_src.bottom- displayframe_src.top));
                            displayframe_src.bottom= screen_in_height;
                        }

                        LOGV("####1:hwc_set_rect, src_left:%d,src_top:%d,src_right:%d,src_bottom:%d,  dst_left:%d,dst_top:%d,dst_right:%d,dst_bottom:%d\n",
                            croprect.left,croprect.top,croprect.right,croprect.bottom,displayframe_src.left,displayframe_src.top,displayframe_src.right,displayframe_src.bottom);

                        hwc_computer_rect(ctx,screen_idx, &displayframe_dst, &displayframe_src);

                        LOGV("####2:hwc_set_rect, src_left:%d,src_top:%d,src_right:%d,src_bottom:%d,  dst_left:%d,dst_top:%d,dst_right:%d,dst_bottom:%d\n",
                            croprect.left,croprect.top,croprect.right,croprect.bottom,displayframe_dst.left,displayframe_dst.top,displayframe_dst.right,displayframe_dst.bottom);

                    	args[0] 				= screen_idx;
                    	args[1] 				= ctx->video_layerhdl[screen_idx];
                    	args[2] 				= (unsigned long) (&layer_info);
                    	args[3] 				= 0;
                    	ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_GET_PARA, args);
                    	if(ret < 0)
                    	{
                    	    LOGV("####DISP_CMD_LAYER_GET_PARA fail in hwc_set_rect, screen_idx:%d,hdl:%d\n",screen_idx,ctx->video_layerhdl[screen_idx]);
                    	}

                    	layer_info.src_win.x = croprect.left;
                    	layer_info.src_win.y = croprect.top;
                    	layer_info.src_win.width = croprect.right - croprect.left;
                    	layer_info.src_win.height = croprect.bottom - croprect.top;
                        if(ctx->cur_3d_out == HWC_3D_OUT_MODE_ANAGLAGH)
                        {
                            if(ctx->cur_3d_src == HWC_3D_SRC_MODE_SSF || ctx->cur_3d_src == HWC_3D_SRC_MODE_SSH)
                            {
                                layer_info.src_win.x /=2;
                                layer_info.src_win.width /=2;
                            }
                            if(ctx->cur_3d_src == HWC_3D_SRC_MODE_TB)
                            {
                                layer_info.src_win.y /=2;
                                layer_info.src_win.height /=2;
                            }
                        }
                    	layer_info.scn_win.x = displayframe_dst.left;
                    	layer_info.scn_win.y = displayframe_dst.top;
                    	layer_info.scn_win.width = displayframe_dst.right - displayframe_dst.left;
                    	layer_info.scn_win.height = displayframe_dst.bottom - displayframe_dst.top;
                        
                    	args[0] 				= screen_idx;
                    	args[1] 				= ctx->video_layerhdl[screen_idx];
                    	args[2] 				= (unsigned long) (&layer_info);
                    	args[3] 				= 0;
                    	ioctl(ctx->dispfd, DISP_CMD_LAYER_SET_PARA, args);
                    }
                    
                    ctx->status[screen_idx] |= HWC_STATUS_COMPOSITED;
                    LOGV("####ctx->status[%d]=%d in hwc_set_rect", screen_idx,ctx->status[screen_idx]);
                }            
                
                ctx->rect_in.left = list->hwLayers[i].sourceCrop.left;
                ctx->rect_in.right = list->hwLayers[i].sourceCrop.right;
                ctx->rect_in.top = list->hwLayers[i].sourceCrop.top;
                ctx->rect_in.bottom = list->hwLayers[i].sourceCrop.bottom;
                
                ctx->rect_out.left = list->hwLayers[i].displayFrame.left;
                ctx->rect_out.right = list->hwLayers[i].displayFrame.right;
                ctx->rect_out.top = list->hwLayers[i].displayFrame.top;
                ctx->rect_out.bottom = list->hwLayers[i].displayFrame.bottom;
            }
            have_overlay = 1;
        }     
    } 

    for(screen_idx=0; screen_idx<2; screen_idx++)
    {
        if(((screen_idx == 0) && (ctx->mode==HWC_MODE_SCREEN0 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_BE || ctx->mode==HWC_MODE_SCREEN0_GPU))
            || ((screen_idx == 1) && (ctx->mode == HWC_MODE_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_TO_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1)))
        {
        	if(have_overlay == 0)
        	{
                if(ctx->status[screen_idx] & HWC_STATUS_OPENED)
                {
	                //LOGD("####close layer in hwc_set_rect ............");
	            
	    		    args[0] 					= screen_idx;
	    		    args[1] 					= ctx->video_layerhdl[screen_idx];
	    		    args[2] 					= 0;
	    		    args[3] 					= 0;
	    		    ioctl(ctx->dispfd, DISP_CMD_LAYER_CLOSE,args);

	    		    ctx->status[screen_idx] &= (~HWC_STATUS_OPENED);
	    		    //LOGV("####ctx->status[%d]=%d in hwc_set_rect", screen_idx,ctx->status[screen_idx]);
	    	    }
            }
			else
			{
	            if((ctx->status[screen_idx] & (HWC_STATUS_OPENED | HWC_STATUS_HAVE_FRAME)) == HWC_STATUS_HAVE_FRAME)
	            {
	                //LOGD("####open layer in hwc_set_rect ............");
	            
	    		    args[0] 					= screen_idx;
	    		    args[1] 					= ctx->video_layerhdl[screen_idx];
	    		    args[2] 					= 0;
	    		    args[3] 					= 0;
	    		    ioctl(ctx->dispfd, DISP_CMD_LAYER_OPEN,args);

	    		    ctx->status[screen_idx] |= HWC_STATUS_OPENED;
	    		    //LOGV("####ctx->status[%d]=%d in hwc_set_rect", screen_idx,ctx->status[screen_idx]);
	    	    }
		    }
        }
    }

    return ret;
}


static int hwc_set_init_para(sun4i_hwc_context_t *ctx,uint32_t value,int alway_update)
{
    __disp_layer_info_t 		tmpLayerAttr;
    int                         ret;
    layerinitpara_t				*layer_info = (layerinitpara_t *)value;
	__disp_pixel_fmt_t          disp_format;
	__disp_pixel_mod_t			fb_mode = DISP_MOD_MB_UV_COMBINED;
	__disp_pixel_seq_t			disp_seq;
	__disp_cs_mode_t			disp_cs_mode;
	unsigned long               args[4]={0};
    unsigned int                screen_idx;
    __disp_colorkey_t 			ck;

    LOGD("####hwc_set_init_para,mode:%d,w:%d,h:%d,format:%d\n",ctx->mode,layer_info->w,layer_info->h,layer_info->format);

	ctx->status[0] &= (~(HWC_STATUS_OPENED | HWC_STATUS_HAVE_FRAME | HWC_STATUS_COMPOSITED));
	ctx->status[1] &= (~(HWC_STATUS_OPENED | HWC_STATUS_HAVE_FRAME | HWC_STATUS_COMPOSITED));
	//LOGV("####ctx->status[0]=%d in hwc_set_init_para", ctx->status[0]);
	//LOGV("####ctx->status[1]=%d in hwc_set_init_para", ctx->status[1]);

    if(alway_update==0 && 
        ((layer_info->w == ctx->w) && (layer_info->h == ctx->h) && (layer_info->format == ctx->format) && (layer_info->screenid == ctx->screenid)))
    {
        LOGD("####para not change\n");
        return 0;
    }
    
    for(screen_idx=0; screen_idx<2; screen_idx++)
    {
        memset(&tmpLayerAttr, 0, sizeof(__disp_layer_info_t));
        disp_seq = DISP_SEQ_UVUV;
        if (layer_info->h < 720)
        {
            tmpLayerAttr.fb.cs_mode     = DISP_BT601;
        }
        else
        {
            tmpLayerAttr.fb.cs_mode     = DISP_BT709;
        }
        switch(layer_info->format)
        {
            case HWC_FORMAT_DEFAULT:
                disp_format = DISP_FORMAT_YUV420;
                fb_mode = DISP_MOD_NON_MB_UV_COMBINED;
                break;
            case HWC_FORMAT_MBYUV420:
                disp_format = DISP_FORMAT_YUV420;
                fb_mode = DISP_MOD_MB_UV_COMBINED;
                break;
            case HWC_FORMAT_MBYUV422:
                disp_format = DISP_FORMAT_YUV422;
                fb_mode = DISP_MOD_MB_UV_COMBINED;
                break;
            case HWC_FORMAT_YUV420PLANAR:
                disp_format = DISP_FORMAT_YUV420;
                fb_mode = DISP_MOD_NON_MB_PLANAR;
                disp_seq = DISP_SEQ_P3210;
                break;
            case HWC_FORMAT_RGBA_8888:
                disp_format = DISP_FORMAT_ARGB8888;
                fb_mode = DISP_MOD_NON_MB_PLANAR;
                disp_seq = DISP_SEQ_P3210;
                break;
            default:
                disp_format = DISP_FORMAT_YUV420;
                fb_mode = DISP_MOD_NON_MB_PLANAR;
                break;
        }
        tmpLayerAttr.fb.mode            = fb_mode;
        tmpLayerAttr.fb.format          = disp_format;
        tmpLayerAttr.fb.br_swap         = 0;
        tmpLayerAttr.fb.seq             = disp_seq;
        tmpLayerAttr.fb.addr[0]         = 0;
        tmpLayerAttr.fb.addr[1]         = 0;
        tmpLayerAttr.fb.addr[2]         = 0;
        tmpLayerAttr.fb.size.width      = layer_info->w;
        tmpLayerAttr.fb.size.height     = layer_info->h;
        tmpLayerAttr.mode               = DISP_LAYER_WORK_MODE_SCALER;
        tmpLayerAttr.alpha_en           = 1;
        tmpLayerAttr.alpha_val          = 0xff;
        tmpLayerAttr.pipe               = 1;
        tmpLayerAttr.src_win.x          = ctx->rect_in.left;
        tmpLayerAttr.src_win.y          = ctx->rect_in.top;
        tmpLayerAttr.src_win.width      = ctx->rect_in.right - ctx->rect_in.left;
        tmpLayerAttr.src_win.height     = ctx->rect_in.bottom - ctx->rect_in.top;
        tmpLayerAttr.fb.b_trd_src       = 0;
        tmpLayerAttr.b_trd_out          = 0;
        tmpLayerAttr.fb.trd_mode        = DISP_3D_SRC_MODE_SSH;
        tmpLayerAttr.out_trd_mode       = DISP_3D_OUT_MODE_FP;
        tmpLayerAttr.b_from_screen      = 0;

        if(ctx->video_layerhdl[screen_idx] != 0)
        {
            args[0]                         = screen_idx;
            args[1]                         = ctx->video_layerhdl[screen_idx];
            ret = ioctl(ctx->dispfd, DISP_CMD_VIDEO_STOP, args);
            
            args[0]                         = screen_idx;
            args[1]                         = ctx->video_layerhdl[screen_idx];
            ioctl(ctx->dispfd, DISP_CMD_LAYER_RELEASE,args);

            ctx->video_layerhdl[screen_idx] = 0;
        }

        if(((screen_idx == 0) && (ctx->mode==HWC_MODE_SCREEN0 || ctx->mode==HWC_MODE_SCREEN0_FE_VAR || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_BE || ctx->mode==HWC_MODE_SCREEN0_GPU))
            || ((screen_idx == 1) && (ctx->mode==HWC_MODE_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_TO_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1)))
        {
            int fbhd;
            hwc_rect_t rect_out;
                        
            args[0]                         = screen_idx;
            ctx->video_layerhdl[screen_idx]          = (uint32_t)ioctl(ctx->dispfd, DISP_CMD_LAYER_REQUEST,args);
            if(ctx->video_layerhdl[screen_idx] == 0)
            {
                LOGE("request layer failed!\n");
                return -1;
            }

            if((screen_idx == 0) && (ctx->mode != HWC_MODE_SCREEN0_BE))
            {
                ioctl(ctx->mFD_fb[0], FBIOGET_LAYER_HDL_0, &ctx->ui_layerhdl[0]);
            }
            else
            {
                ioctl(ctx->mFD_fb[1], FBIOGET_LAYER_HDL_1, &ctx->ui_layerhdl[1]);
            }

            hwc_computer_rect(ctx, screen_idx, &rect_out,&ctx->rect_out);

            tmpLayerAttr.scn_win.x          = rect_out.left;
            tmpLayerAttr.scn_win.y          = rect_out.top;
            tmpLayerAttr.scn_win.width      = rect_out.right - rect_out.left;
            tmpLayerAttr.scn_win.height     = rect_out.bottom - rect_out.top;

        	args[0] 						= screen_idx;
        	args[1] 						= ctx->video_layerhdl[screen_idx];
        	args[2] 						= (unsigned long) (&tmpLayerAttr);
        	args[3] 						= 0;
        	ioctl(ctx->dispfd, DISP_CMD_LAYER_SET_PARA, args);

            args[0]                         = screen_idx;
            args[1]                         = ctx->video_layerhdl[screen_idx];
            ioctl(ctx->dispfd, DISP_CMD_LAYER_BOTTOM, args);
            
            ck.ck_min.alpha                 = 0xff;
            ck.ck_min.red                   = 0x00; //0x01;
            ck.ck_min.green                 = 0x00; //0x03;
            ck.ck_min.blue                  = 0x00; //0x05;
            ck.ck_max.alpha                 = 0xff;
            ck.ck_max.red                   = 0x00; //0x01;
            ck.ck_max.green                 = 0x00; //0x03;
            ck.ck_max.blue                  = 0x00; //0x05;

            ck.red_match_rule               = 2;
            ck.green_match_rule             = 2;
            ck.blue_match_rule              = 2;
            args[0]                         = screen_idx;
            args[1]                         = (unsigned long)&ck;
            ioctl(ctx->dispfd,DISP_CMD_SET_COLORKEY,(void*)args);

            args[0]                         = screen_idx;
            args[1]                         = ctx->ui_layerhdl[screen_idx];
            ioctl(ctx->dispfd,DISP_CMD_LAYER_CK_OFF,(void*)args);

        	args[0] 						= screen_idx;
        	args[1] 						= ctx->ui_layerhdl[screen_idx];
        	args[2] 						= (unsigned long) (&tmpLayerAttr);
        	args[3] 						= 0;
        	ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_GET_PARA, args);
            if(ret < 0)
            {
                LOGD("####DISP_CMD_LAYER_GET_PARA fail in hwc_set_init_para, screen_idx:%d,hdl:%d\n",screen_idx,ctx->ui_layerhdl[screen_idx]);
            }

            if(1)
            {
                args[0]                         = screen_idx;
                args[1]                         = ctx->video_layerhdl[screen_idx];
                ioctl(ctx->dispfd,DISP_CMD_LAYER_CK_ON,(void*)args);
            }
            else
            {
                args[0]                         = screen_idx;
                args[1]                         = ctx->video_layerhdl[screen_idx];
                ioctl(ctx->dispfd,DISP_CMD_LAYER_CK_OFF,(void*)args);
            }

            args[0]                         = screen_idx;
            args[1]                         = ctx->ui_layerhdl[screen_idx];
            ioctl(ctx->dispfd, DISP_CMD_LAYER_ALPHA_OFF, args);
        }
    }
    
    
    ctx->w = layer_info->w;
    ctx->h = layer_info->h;
    ctx->format = layer_info->format;
    ctx->screenid = layer_info->screenid;

	return 0;
}

static int hwc_set_frame_para(sun4i_hwc_context_t *ctx,uint32_t value)
{
    __disp_video_fb_t      		tmpFrmBufAddr;
    libhwclayerpara_t            *overlaypara;
    int                         ret;
    int                         screen_idx;
    unsigned long               args[4]={0};
    
    for(screen_idx=0; screen_idx<2; screen_idx++)
    {
        if(((screen_idx == 0) && (ctx->mode==HWC_MODE_SCREEN0 || ctx->mode==HWC_MODE_SCREEN0_FE_VAR || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_BE || ctx->mode==HWC_MODE_SCREEN0_GPU))
            || ((screen_idx == 1) && (ctx->mode==HWC_MODE_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_TO_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1)))
        {
            overlaypara                     = (libhwclayerpara_t *)value;

			//LOGV("####hwc_set_frame_para,frame_id:%d\n",overlaypara->number);

        	tmpFrmBufAddr.interlace         = (overlaypara->bProgressiveSrc?0:1);
        	tmpFrmBufAddr.top_field_first   = overlaypara->bTopFieldFirst;
        	tmpFrmBufAddr.addr[0]           = overlaypara->top_y;
        	tmpFrmBufAddr.addr[1]           = overlaypara->top_c;
        	tmpFrmBufAddr.addr[2]			= overlaypara->bottom_y;
        	tmpFrmBufAddr.addr_right[0]     = overlaypara->bottom_y;
        	tmpFrmBufAddr.addr_right[1]     = overlaypara->bottom_c;
        	tmpFrmBufAddr.addr_right[2]	    = 0;
        	tmpFrmBufAddr.id                = overlaypara->number; 
        	tmpFrmBufAddr.maf_valid         = overlaypara->maf_valid;
        	tmpFrmBufAddr.pre_frame_valid   = overlaypara->pre_frame_valid;
        	tmpFrmBufAddr.flag_addr         = overlaypara->flag_addr;
        	tmpFrmBufAddr.flag_stride       = overlaypara->flag_stride;

        	if((ctx->status[screen_idx] & HWC_STATUS_HAVE_FRAME) == 0)
        	{
                __disp_layer_info_t         layer_info;

            	LOGD("####first_frame ............");

            	args[0] 				= screen_idx;
            	args[1] 				= ctx->video_layerhdl[screen_idx];
            	args[2] 				= (unsigned long) (&layer_info);
            	args[3] 				= 0;
            	ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_GET_PARA, args);
                if(ret < 0)
                {
                    LOGD("####DISP_CMD_LAYER_GET_PARA fail in hwc_set_frame_para, screen_idx:%d,hdl:%d\n",screen_idx,ctx->video_layerhdl[screen_idx]);
                }

            	layer_info.fb.addr[0] 	= tmpFrmBufAddr.addr[0];
            	layer_info.fb.addr[1] 	= tmpFrmBufAddr.addr[1];
            	layer_info.fb.addr[2] 	= tmpFrmBufAddr.addr[2];
            	layer_info.fb.trd_right_addr[0] = tmpFrmBufAddr.addr_right[0];
            	layer_info.fb.trd_right_addr[1] = tmpFrmBufAddr.addr_right[1];
            	layer_info.fb.trd_right_addr[2] = tmpFrmBufAddr.addr_right[2];

            	args[0] 				= screen_idx;
            	args[1] 				= ctx->video_layerhdl[screen_idx];
            	args[2] 				= (unsigned long) (&layer_info);
            	args[3] 				= 0;
            	ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_SET_PARA, args);
            	
                args[0]                         = screen_idx;
                args[1]                         = ctx->video_layerhdl[screen_idx];
                ret = ioctl(ctx->dispfd, DISP_CMD_VIDEO_START, args);
        	}

            //have been composited,and have not been opened
            if((ctx->status[screen_idx] & ( HWC_STATUS_COMPOSITED | HWC_STATUS_OPENED)) == (HWC_STATUS_COMPOSITED))
            {
                LOGD("####open layer ............");
                
        		args[0] 					= screen_idx;
        		args[1] 					= ctx->video_layerhdl[screen_idx];
        		args[2] 					= 0;
        		args[3] 					= 0;
        		ioctl(ctx->dispfd, DISP_CMD_LAYER_OPEN,args);

        		ctx->status[screen_idx] |= HWC_STATUS_OPENED;
        		LOGV("####ctx->status[%d]=%d in hwc_set_frame_para", screen_idx,ctx->status[screen_idx]);
        	}

        	args[0]					= screen_idx;
            args[1]                 = ctx->video_layerhdl[screen_idx];
        	args[2]                 = (unsigned long)(&tmpFrmBufAddr);
        	args[3]                 = 0;
        	ret = ioctl(ctx->dispfd, DISP_CMD_VIDEO_SET_FB,args);
            LOGV("####DISP_CMD_VIDEO_SET_FB,%d,%d,ret:%d\n", screen_idx,ctx->video_layerhdl[screen_idx],ret);
        
            memcpy(&ctx->cur_frame_para, overlaypara,sizeof(libhwclayerpara_t));
        }
        ctx->status[screen_idx] |= HWC_STATUS_HAVE_FRAME;
        LOGV("####ctx->status[%d]=%d in hwc_set_frame_para 1", screen_idx,ctx->status[screen_idx]);
    }
    
    return 0;
}


static int hwc_get_frame_id(sun4i_hwc_context_t *ctx)
{
    int                         ret = -1;
    unsigned long               args[4]={0};

    if(ctx->mode==HWC_MODE_SCREEN0 || ctx->mode==HWC_MODE_SCREEN0_FE_VAR || ctx->mode==HWC_MODE_SCREEN0_BE || ctx->mode==HWC_MODE_SCREEN0_GPU)
    {
    	args[0] = 0;
    	args[1] = ctx->video_layerhdl[0];
    	ret = ioctl(ctx->dispfd, DISP_CMD_VIDEO_GET_FRAME_ID, args);
    }
    else if(ctx->mode==HWC_MODE_SCREEN0_TO_SCREEN1 || ctx->mode==HWC_MODE_SCREEN1)
    {
        args[0] = 1;
        args[1] = ctx->video_layerhdl[1];
        ret = ioctl(ctx->dispfd, DISP_CMD_VIDEO_GET_FRAME_ID, args);
    }
    else if(ctx->mode == HWC_MODE_SCREEN0_AND_SCREEN1)
    {
        int ret0,ret1;
        
    	args[0] = 0;
    	args[1] = ctx->video_layerhdl[0];
    	ret0 = ioctl(ctx->dispfd, DISP_CMD_VIDEO_GET_FRAME_ID, args);

        args[0] = 1;
        args[1] = ctx->video_layerhdl[1];
        ret1 = ioctl(ctx->dispfd, DISP_CMD_VIDEO_GET_FRAME_ID, args);

        ret = (ret0<ret1)?ret0:ret1;
    }

    if(ret <0)
    {
        LOGV("####hwc_get_frame_id return -1,mode:%d\n",ctx->mode);
    }
    return ret;
}

static int hwc_set3dmode(sun4i_hwc_context_t *ctx,int para)
{
	video3Dinfo_t *_3d_info = (video3Dinfo_t *)para;
	unsigned int _3d_src = _3d_info->src_mode;
	unsigned int _3d_out = _3d_info->display_mode;
    __disp_layer_info_t         layer_info;
    unsigned int                screen_idx;
    int                         ret = -1;
    unsigned long               args[4]={0};
    __disp_output_type_t        cur_out_type;
    __disp_tv_mode_t            cur_hdmi_mode;

    LOGD("####hwc_set3dmode,src:%d,out:%d,w:%d,h:%d,format:0x%x\n", _3d_src,_3d_out,_3d_info->width,_3d_info->height,_3d_info->format);
    
    for(screen_idx=0; screen_idx<2; screen_idx++)
    {
        if(((screen_idx == 0) && (ctx->mode==HWC_MODE_SCREEN0 || ctx->mode==HWC_MODE_SCREEN0_FE_VAR || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_BE || ctx->mode==HWC_MODE_SCREEN0_GPU))
            || ((screen_idx == 1) && (ctx->mode==HWC_MODE_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_TO_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1)))
        {
            args[0] = screen_idx;
            args[1] = ctx->video_layerhdl[screen_idx];
            args[2] = (unsigned long)&layer_info;
            ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_GET_PARA, args);
            if(ret < 0)
            {
                LOGD("####DISP_CMD_LAYER_GET_PARA fail in hwc_set3dmode, screen_idx:%d,hdl:%d\n",screen_idx,ctx->video_layerhdl[screen_idx]);
            }

            layer_info.fb.size.width = _3d_info->width;
            layer_info.fb.size.height = _3d_info->height;
            layer_info.src_win.x = 0;
            layer_info.src_win.y = 0;
            layer_info.src_win.width = _3d_info->width;
            layer_info.src_win.height = _3d_info->height;
            
            if(_3d_info->format == HWC_FORMAT_RGBA_8888)//·ÖÉ«
            {
                layer_info.fb.mode = DISP_MOD_NON_MB_PLANAR;
                layer_info.fb.format = DISP_FORMAT_ARGB8888;
                layer_info.fb.seq = DISP_SEQ_P3210;
            }
            else if(_3d_info->format == HWC_FORMAT_YUV420PLANAR)
            {
                layer_info.fb.mode = DISP_MOD_NON_MB_PLANAR;
                layer_info.fb.format = DISP_FORMAT_YUV420;
                layer_info.fb.seq = DISP_SEQ_P3210;
            }
            else if(_3d_info->format == HWC_FORMAT_MBYUV422)
            {
                layer_info.fb.mode = DISP_MOD_MB_UV_COMBINED;
                layer_info.fb.format = DISP_FORMAT_YUV422;
                layer_info.fb.seq = DISP_SEQ_UVUV;
            }
            else
            {
                layer_info.fb.mode = DISP_MOD_MB_UV_COMBINED;
                layer_info.fb.format = DISP_FORMAT_YUV420;
                layer_info.fb.seq = DISP_SEQ_UVUV;
            }

            if(_3d_src == HWC_3D_SRC_MODE_NORMAL)
            {
                layer_info.fb.b_trd_src = 0;
            }
            else
            {
                layer_info.fb.b_trd_src = 1;
                layer_info.fb.trd_mode = (__disp_3d_src_mode_t)_3d_src;
                layer_info.fb.trd_right_addr[0] = layer_info.fb.addr[0];
                layer_info.fb.trd_right_addr[1] = layer_info.fb.addr[1];
                layer_info.fb.trd_right_addr[2] = layer_info.fb.addr[2];
            }

            args[0] = screen_idx;
            cur_out_type = (__disp_output_type_t)ioctl(ctx->dispfd,DISP_CMD_GET_OUTPUT_TYPE,(unsigned long)args);
            if(cur_out_type == DISP_OUTPUT_TYPE_HDMI)
            {
                args[0] = screen_idx;
                cur_hdmi_mode = (__disp_tv_mode_t)ioctl(ctx->dispfd,DISP_CMD_HDMI_GET_MODE,(unsigned long)args);

                if(cur_hdmi_mode == DISP_TV_MOD_1080P_24HZ_3D_FP || cur_hdmi_mode == DISP_TV_MOD_720P_50HZ_3D_FP || cur_hdmi_mode == DISP_TV_MOD_720P_60HZ_3D_FP)
                {
                    if(_3d_out != HWC_3D_OUT_MODE_HDMI_3D_1080P24_FP && _3d_out != HWC_3D_OUT_MODE_HDMI_3D_720P50_FP && _3d_out != HWC_3D_OUT_MODE_HDMI_3D_720P60_FP)
                    {
                        __disp_layer_info_t         tmpLayerAttr;
                        
                        args[0]                         = screen_idx;
                        args[1]                         = ctx->ui_layerhdl[screen_idx];
                        args[2]                         = (unsigned long) (&tmpLayerAttr);
                        ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_GET_PARA, args);

                        tmpLayerAttr.scn_win.x = ctx->org_scn_win.x;
                        tmpLayerAttr.scn_win.y = ctx->org_scn_win.y;
                        tmpLayerAttr.scn_win.width = ctx->org_scn_win.width;
                        tmpLayerAttr.scn_win.height = ctx->org_scn_win.height;
                        
                        args[0]                         = screen_idx;
                        args[1]                         = ctx->ui_layerhdl[screen_idx];
                        args[2]                         = (unsigned long) (&tmpLayerAttr);
                        ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_SET_PARA, args);
                        
                        args[0] = screen_idx;
                        ret = ioctl(ctx->dispfd,DISP_CMD_HDMI_OFF,(unsigned long)args);

                        args[0] = screen_idx;
                        args[1] = ctx->org_hdmi_mode;
                        ioctl(ctx->dispfd,DISP_CMD_HDMI_SET_MODE,(unsigned long)args);
                        
                        args[0] = screen_idx;
                        ret = ioctl(ctx->dispfd,DISP_CMD_HDMI_ON,(unsigned long)args);
                    }
                }
                else
                {
                    if(_3d_out == HWC_3D_OUT_MODE_HDMI_3D_1080P24_FP || _3d_out == HWC_3D_OUT_MODE_HDMI_3D_720P50_FP || _3d_out == HWC_3D_OUT_MODE_HDMI_3D_720P60_FP)
                    {
                        ctx->org_hdmi_mode = cur_hdmi_mode;
                        
                        __disp_layer_info_t 		tmpLayerAttr;
                        
                        args[0]                         = screen_idx;
                        args[1]                         = ctx->ui_layerhdl[screen_idx];
                        args[2]                         = (unsigned long) (&tmpLayerAttr);
                        ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_GET_PARA, args);
                        
                        ctx->org_scn_win.x = tmpLayerAttr.scn_win.x;
                        ctx->org_scn_win.y = tmpLayerAttr.scn_win.y;
                        ctx->org_scn_win.width = tmpLayerAttr.scn_win.width;
                        ctx->org_scn_win.height = tmpLayerAttr.scn_win.height;
                        
                        tmpLayerAttr.scn_win.x = 0;
                        tmpLayerAttr.scn_win.y = 0;
                        if(_3d_out == HWC_3D_OUT_MODE_HDMI_3D_1080P24_FP)
                        {
                            tmpLayerAttr.scn_win.width = 1920;
                            tmpLayerAttr.scn_win.height = 1080;
                        }
                        else
                        {
                            tmpLayerAttr.scn_win.width = 1280;
                            tmpLayerAttr.scn_win.height = 720;
                        }
                        args[0]                         = screen_idx;
                        args[1]                         = ctx->ui_layerhdl[screen_idx];
                        args[2]                         = (unsigned long) (&tmpLayerAttr);
                        ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_SET_PARA, args);
                        
                        args[0] = screen_idx;
                        ret = ioctl(ctx->dispfd,DISP_CMD_HDMI_OFF,(unsigned long)args);
                        
                        args[0] = screen_idx;
                        if(_3d_out == HWC_3D_OUT_MODE_HDMI_3D_1080P24_FP)
                        {
                            args[1] = DISP_TV_MOD_1080P_24HZ_3D_FP;
                        }
                        else if(_3d_out == HWC_3D_OUT_MODE_HDMI_3D_720P50_FP)
                        {
                            args[1] = DISP_TV_MOD_720P_50HZ_3D_FP;
                        }
                        else if(_3d_out == HWC_3D_OUT_MODE_HDMI_3D_720P60_FP)
                        {
                            args[1] = DISP_TV_MOD_720P_60HZ_3D_FP;
                        }
                        ioctl(ctx->dispfd,DISP_CMD_HDMI_SET_MODE,(unsigned long)args);
                        
                        args[0] = screen_idx;
                        ret = ioctl(ctx->dispfd,DISP_CMD_HDMI_ON,(unsigned long)args);
                    }
                }
            }
            
            if(_3d_out == HWC_3D_OUT_MODE_HDMI_3D_1080P24_FP || _3d_out == HWC_3D_OUT_MODE_HDMI_3D_720P50_FP || _3d_out == HWC_3D_OUT_MODE_HDMI_3D_720P60_FP)
            {
                if(cur_out_type == DISP_OUTPUT_TYPE_HDMI)
                {
                    layer_info.b_trd_out = 1;
                    layer_info.out_trd_mode = DISP_3D_OUT_MODE_FP;
                }
                else
                {
                    layer_info.fb.b_trd_src = 0;
                    layer_info.b_trd_out = 0;
                }
            }
            else if(_3d_out == HWC_3D_OUT_MODE_ORIGINAL || _3d_out == HWC_3D_OUT_MODE_ANAGLAGH)
            {
                layer_info.fb.b_trd_src = 0;
                layer_info.b_trd_out = 0;
            }
            else if(_3d_out == HWC_3D_OUT_MODE_LI || _3d_out == HWC_3D_OUT_MODE_CI_1 || _3d_out == HWC_3D_OUT_MODE_CI_2 || 
                    _3d_out == HWC_3D_OUT_MODE_CI_3 || _3d_out == HWC_3D_OUT_MODE_CI_4)
            {
                layer_info.b_trd_out = 1;
                layer_info.out_trd_mode = (__disp_3d_out_mode_t)_3d_out;
            }
            else
            {
                layer_info.b_trd_out = 0;
            }

            if(layer_info.b_trd_out)
            {
                unsigned int w,h;
                
                args[0] = screen_idx;
                w = ioctl(ctx->dispfd,DISP_CMD_SCN_GET_WIDTH,(unsigned long)args);
                h = ioctl(ctx->dispfd,DISP_CMD_SCN_GET_HEIGHT,(unsigned long)args);

                layer_info.scn_win.x = 0;
                layer_info.scn_win.y = 0;
                layer_info.scn_win.width = w;
                layer_info.scn_win.height = h;
            }
            
            args[0] = screen_idx;
            args[1] = ctx->video_layerhdl[screen_idx];
            args[2] = (unsigned long)&layer_info;
            ioctl(ctx->dispfd, DISP_CMD_LAYER_SET_PARA, args);
        }
    }

    ctx->cur_3d_w = _3d_info->width;
    ctx->cur_3d_h = _3d_info->height;
    ctx->cur_3d_format = _3d_info->format;
    ctx->cur_3d_src = _3d_info->src_mode;
    ctx->cur_3d_out = _3d_info->display_mode;
    ctx->cur_3denable = layer_info.b_trd_out;
    return 0;
}


static int hwc_set_3d_parallax(sun4i_hwc_context_t *ctx,uint32_t value)
{
    __disp_layer_info_t         layer_info;
    unsigned int                screen_idx;
    int                         ret = -1;
    unsigned long               args[4]={0};

    LOGD("####hwc_set_3d_parallax:%d\n", value);
    
    for(screen_idx=0; screen_idx<2; screen_idx++)
    {
        if(((screen_idx == 0) && (ctx->mode==HWC_MODE_SCREEN0 || ctx->mode==HWC_MODE_SCREEN0_FE_VAR || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_BE || ctx->mode==HWC_MODE_SCREEN0_GPU))
            || ((screen_idx == 1) && (ctx->mode==HWC_MODE_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_TO_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1)))
        {
            args[0] = screen_idx;
            args[1] = ctx->video_layerhdl[screen_idx];
            args[2] = (unsigned long)&layer_info;
            ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_GET_PARA, args);
            if(ret < 0)
            {
                LOGD("####DISP_CMD_LAYER_GET_PARA fail in hwc_set_3d_parallax, screen_idx:%d,hdl:%d\n",screen_idx,ctx->video_layerhdl[screen_idx]);
            }

            if(layer_info.fb.b_trd_src && (layer_info.fb.trd_mode==DISP_3D_SRC_MODE_SSF || layer_info.fb.trd_mode==DISP_3D_SRC_MODE_SSH) && layer_info.b_trd_out)
            {
                args[0] = screen_idx;
                args[1] = ctx->video_layerhdl[screen_idx];
                args[2] = (unsigned long)&layer_info;
                ret = ioctl(ctx->dispfd, DISP_CMD_LAYER_SET_PARA, args);
            }
        }
    }
    
    return 0;
}


static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) 
{

    for (size_t i=0 ; i<list->numHwLayers ; i++) 
    {
        if((list->hwLayers[i].format == HWC_FORMAT_MBYUV420)
            ||(list->hwLayers[i].format == HWC_FORMAT_MBYUV422)
            ||(list->hwLayers[i].format == HWC_FORMAT_YUV420PLANAR)
            ||(list->hwLayers[i].format == HWC_FORMAT_DEFAULT))
    	{
        	list->hwLayers[i].compositionType = HWC_OVERLAY;
    	}
    	else
    	{
        	list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
    	}
    }
        
    return 0;
}

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
    EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
    if (!sucess) 
    {
        return HWC_EGL_ERROR;
    }

    return hwc_set_rect(dev,list);
}

static int hwc_set_mode(sun4i_hwc_context_t *ctx,uint32_t value)
{
    layerinitpara_t layer_para;
    
    LOGD("####hwc_set_mode:%d\n", value);

    if(value == ctx->mode)
    {
        LOGD("####mode not change\n");
        return 0;
    }

    layer_para.w = ctx->w;
    layer_para.h = ctx->h;
    layer_para.format = ctx->format;
    layer_para.screenid = ctx->screenid;
    ctx->mode = value;
    hwc_set_init_para(ctx, (uint32_t)&layer_para, 1);

    hwc_set_frame_para(ctx, (uint32_t)&ctx->cur_frame_para);

    if(ctx->cur_3d_out == HWC_3D_OUT_MODE_ANAGLAGH)
    {
        video3Dinfo_t _3d_info;

        _3d_info.width = ctx->cur_3d_w;
        _3d_info.height = ctx->cur_3d_h;
        _3d_info.format = ctx->cur_3d_format;
        _3d_info.src_mode = ctx->cur_3d_src;
        _3d_info.display_mode = ctx->cur_3d_out;
        
        hwc_set3dmode(ctx, (int)&_3d_info);
    }

    return 0;
}

static int hwc_set_screen_para(sun4i_hwc_context_t *ctx,uint32_t value)
{
    screen_para_t *screen_info = (screen_para_t *)value;
    
    LOGV("####hwc_set_screen_para,%d,%d,%d,%d,%d,%d",screen_info->app_width[0],screen_info->app_height[0],screen_info->width[0],screen_info->height[0],
    screen_info->valid_width[0],screen_info->valid_height[0]);
    
    memcpy(&ctx->screen_para,screen_info,sizeof(screen_para_t));
    
    return 0;
}

static int hwc_setparameter(hwc_composer_device_t *dev,uint32_t param,uint32_t value)
{
	int 						ret = 0;
    sun4i_hwc_context_t   		*ctx = (sun4i_hwc_context_t *)dev;
	
    if(param == HWC_LAYER_SETINITPARA)
    {
    	ret = hwc_set_init_para(ctx,value, 0);
    }
    else if(param == HWC_LAYER_SETFRAMEPARA)
    {
    	ret = hwc_set_frame_para(ctx,value);
    }
    else if(param == HWC_LAYER_GETCURFRAMEPARA)
    {
    	ret = hwc_get_frame_id(ctx);
    }
    else if(param == HWC_LAYER_SETMODE)
    {
        ret = hwc_set_mode(ctx, value);
    }
	else if(param == HWC_LAYER_SET3DMODE)
	{
	    ret = hwc_set3dmode(ctx,value);
	}
	else if(param == HWC_LAYER_SET_3D_PARALLAX)
	{
	    ret = hwc_set_3d_parallax(ctx,value);
	}
	else if(param == HWC_LAYER_SET_SCREEN_PARA)
	{
	    ret = hwc_set_screen_para(ctx,value);
	}
	else if(param == HWC_LAYER_SHOW)
	{
		if(value == 0)
		{
	        ret = hwc_release(ctx);
		}
		else if(value == 2)
		{
			int screen_idx = 0;
			unsigned long args[4]={0};
			
			for(screen_idx=0; screen_idx<2; screen_idx++)
			{
				if(((screen_idx == 0) && (ctx->mode==HWC_MODE_SCREEN0 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_BE || ctx->mode==HWC_MODE_SCREEN0_GPU))
					|| ((screen_idx == 1) && (ctx->mode == HWC_MODE_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_TO_SCREEN1 || ctx->mode==HWC_MODE_SCREEN0_AND_SCREEN1)))
				{
					if(ctx->status[screen_idx] & HWC_STATUS_OPENED)
					{
						LOGD("####close layer in HWC_LAYER_SHOW ............");
					
						args[0] 					= screen_idx;
						args[1] 					= ctx->video_layerhdl[screen_idx];
						args[2] 					= 0;
						args[3] 					= 0;
						ioctl(ctx->dispfd, DISP_CMD_LAYER_CLOSE,args);
		
						ctx->status[screen_idx] &= (~HWC_STATUS_OPENED);
						//LOGV("####ctx->status[%d]=%d in hwc_set_rect", screen_idx,ctx->status[screen_idx]);
					}
				}
			}
		}
	}


    return ( ret );
}


static uint32_t hwc_getparameter(hwc_composer_device_t *dev,uint32_t param)
{
    return 0;
}

static int hwc_init(sun4i_hwc_context_t *ctx)
{
	unsigned long               arg[4]={0};
    __disp_init_t init_para;

    ctx->dispfd                 = open("/dev/disp", O_RDWR);
    if (ctx->dispfd < 0)
    {
        LOGE("Failed to open disp device\n");
        return  -1;
    }
    
    ctx->mFD_fb[0] = open("/dev/graphics/fb0", O_RDWR);
    if (ctx->mFD_fb[0] < 0)
    {
        LOGE("Failed to open fb0 device\n");
        return  -1;
    }
    
    ctx->mFD_fb[1]                 = open("/dev/graphics/fb1", O_RDWR);
    if (ctx->mFD_fb[1] < 0)
    {
        LOGE("Failed to open fb1 device\n");
        return  -1;
    }
    
    ctx->mode = 0;

    arg[0] = (unsigned long)&init_para;
    ioctl(ctx->dispfd,DISP_CMD_GET_DISP_INIT_PARA,(unsigned long)arg);

    arg[0] = 0;
    ctx->screen_para.app_width[0] = ioctl(ctx->dispfd,DISP_CMD_SCN_GET_WIDTH,(unsigned long)arg);
    ctx->screen_para.app_height[0] = ioctl(ctx->dispfd,DISP_CMD_SCN_GET_HEIGHT,(unsigned long)arg);
    ctx->screen_para.width[0] = ioctl(ctx->dispfd,DISP_CMD_SCN_GET_WIDTH,(unsigned long)arg);
    ctx->screen_para.height[0] = ioctl(ctx->dispfd,DISP_CMD_SCN_GET_HEIGHT,(unsigned long)arg);
    ctx->screen_para.valid_width[0] = ioctl(ctx->dispfd,DISP_CMD_SCN_GET_WIDTH,(unsigned long)arg);
    ctx->screen_para.valid_height[0] = ioctl(ctx->dispfd,DISP_CMD_SCN_GET_HEIGHT,(unsigned long)arg);

    if(init_para.disp_mode == DISP_INIT_MODE_SCREEN0)
    {
    	ctx->mode = HWC_MODE_SCREEN0;
	}
	else if(init_para.disp_mode == DISP_INIT_MODE_SCREEN1)
    {
    	ctx->mode = HWC_MODE_SCREEN1;
	}
    else if(init_para.disp_mode == DISP_INIT_MODE_SCREEN0_PARTLY)
    {
    	ctx->mode = HWC_MODE_SCREEN0_GPU;
	}
	
    char value[PROPERTY_VALUE_MAX];
    int r = property_get("ro.sw.videotrimming", value, "0");
	LOGD("####ro.sw.videotrimming is %s", value);
	ctx->b_video_in_valid_area = atoi(value);
            
    return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    sun4i_hwc_context_t* ctx = (sun4i_hwc_context_t*)dev;

    hwc_release(ctx);

    return 0;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) 
    {
        sun4i_hwc_context_t *dev;
        dev = (sun4i_hwc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag      = HARDWARE_DEVICE_TAG;
        dev->device.common.version  = 0;
        dev->device.common.module   = const_cast<hw_module_t*>(module);
        dev->device.common.close    = hwc_device_close;

        dev->device.prepare         = hwc_prepare;
        dev->device.set             = hwc_set;
        dev->device.setparameter    = hwc_setparameter;
        dev->device.getparameter    = hwc_getparameter;

        *device = &dev->device.common;
        status = 0;


        hwc_init(dev);
    }
    return status;
}
