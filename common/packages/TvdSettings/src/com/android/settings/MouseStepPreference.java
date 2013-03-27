package com.android.settings;
/*
 ************************************************************************************
 *                                    Android Settings

 *                       (c) Copyright 2006-2010, huanglong Allwinner 
 *                                 All Rights Reserved
 *
 * File       : MouseStepPreference.java
 * By         : huanglong
 * Version    : v1.0
 * Date       : 2012-04-10
 * Description: 
 * Update     : date                author      version     notes
 *           
 ************************************************************************************
 */

import android.content.Context;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.preference.SeekBarDialogPreference;
import android.provider.Settings;
import android.provider.Settings.SettingNotFoundException;
import android.util.AttributeSet;
import android.util.Log;
import android.view.DisplayManager;
import android.view.IWindowManager;
import android.view.View;
import android.widget.SeekBar;

public class MouseStepPreference extends SeekBarDialogPreference implements 
        SeekBar.OnSeekBarChangeListener{

    private SeekBar mSeekBar;
    
    private int OldValue;
    
    private int MAXIMUM_VALUE = 100;
    private int MINIMUM_VALUE = 5;
    
    public MouseStepPreference(Context context, AttributeSet attrs) {
        super(context, attrs);
        setDialogLayoutResource(R.layout.preference_mouse_move_dialog);
        //setDialogIcon(R.drawable.ic_settings_saturation);        
    }
    
    protected void onBindDialogView(View view){
        super.onBindDialogView(view);
        
        
        mSeekBar = getSeekBar(view);
        mSeekBar.setMax(MAXIMUM_VALUE-MINIMUM_VALUE);
        try{
            OldValue = getSysInt();
        }catch(SettingNotFoundException snfe){
            OldValue = MAXIMUM_VALUE;
        }
        Log.d("staturation","" + OldValue);
        mSeekBar.setProgress(OldValue - MINIMUM_VALUE);
        mSeekBar.setOnSeekBarChangeListener(this);
    }
    
    public void onProgressChanged(SeekBar seekBar, int progress,
            boolean fromTouch){
        setMouseStep(progress + MINIMUM_VALUE);
    }
    @Override
    protected void onDialogClosed(boolean positiveResult){
        super.onDialogClosed(positiveResult);
        if(positiveResult){
            putSysInt(mSeekBar.getProgress() + MINIMUM_VALUE);            
        }else{
            setMouseStep(OldValue);
        }
    }
    
    private int getSysInt() throws SettingNotFoundException
    {
        return Settings.System.getInt(getContext().getContentResolver(), 
                Settings.System.MOUSE_ADVANCE,MINIMUM_VALUE);
    }
    private boolean putSysInt(int value)
    {
    	IWindowManager windowManager = IWindowManager.Stub.asInterface(ServiceManager.getService("window"));
        if(windowManager == null)
                Log.e("MouseStepPreference", "windowManager == null");
        else{
                try{
                        windowManager.keySetMouseDistance(value);
                }
                catch(RemoteException e){
                        Log.e("MouseStepPreference", "set mouse distance Failed!");
                }        
        }
        return Settings.System.putInt(getContext().getContentResolver(), 
                Settings.System.MOUSE_ADVANCE,value);
    }
    private void setMouseStep(int value) {
        //TODO:this set the correct preview function.
    }
    
    /*implements method in SeekBar.OnSeekBarChangeListener*/
    @Override
    public void onStartTrackingTouch(SeekBar arg0) {
        // NA
        
    }
    /*implements method in SeekBar.OnSeekBarChangeListener*/
    @Override
    public void onStopTrackingTouch(SeekBar arg0) {
        // NA
        
    }

}
