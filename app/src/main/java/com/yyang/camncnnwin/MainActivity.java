package com.yyang.camncnnwin;

import android.Manifest;
import android.app.NativeActivity;
import android.content.Context;
import android.content.Intent;
import android.content.res.AssetManager;
import android.content.pm.PackageManager;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.net.Uri;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;

import static android.hardware.camera2.CameraMetadata.LENS_FACING_BACK;

import java.io.FileNotFoundException;


public class MainActivity extends NativeActivity implements ActivityCompat.OnRequestPermissionsResultCallback
{
    volatile MainActivity _saved_instance;

    private final String DBG_TAG = "CAM2NCNN2WIN";
    private static final int PERMISSION_REQUEST_CODE_CAMERA = 2;

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        _saved_instance  = this;

        setImmersiveSticky();
        View decor_view = getWindow().getDecorView();
        decor_view.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
            @Override
            public void onSystemUiVisibilityChange(int visibility)
            {
                setImmersiveSticky();
            }});
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        setImmersiveSticky();
    }

    @Override
    protected void onPause()
    {
        super.onPause();
    }

    @Override
    protected void onDestroy()
    {
        super.onDestroy();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           @NonNull String[] permissions,
                                           @NonNull int[] grantResults)
    {
        if (PERMISSION_REQUEST_CODE_CAMERA != requestCode)
        {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
            return;
        }

        if(grantResults.length == 2)
        {
            NotifyCameraPermission(grantResults[0] == PackageManager.PERMISSION_GRANTED &&
                    grantResults[1] == PackageManager.PERMISSION_GRANTED);
        }
    }

    private boolean isCamera2Device()
    {
        CameraManager camMgr = (CameraManager)getSystemService(Context.CAMERA_SERVICE);
        boolean camera2Dev = true;
        try
        {
            String[] cameraIds = camMgr.getCameraIdList();
            if (cameraIds.length != 0 )
            {
                for (String id : cameraIds)
                {
                    CameraCharacteristics characteristics = camMgr.getCameraCharacteristics(id);
                    int deviceLevel = characteristics.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL);
                    int facing = characteristics.get(CameraCharacteristics.LENS_FACING);
                    if (deviceLevel == CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY && facing == LENS_FACING_BACK)
                    {
                        camera2Dev =  false;
                    }
                }
            }
        }
        catch (CameraAccessException e)
        {
            e.printStackTrace();
            camera2Dev = false;
        }

        return camera2Dev;
    }

    public void RequestCamera()
    {
        if(!isCamera2Device())
        {
            Log.e(DBG_TAG, "found legacy camera device, this demo needs camera2 device");
            return;
        }

        String[] accessPermissions = new String[]
                {
                    Manifest.permission.CAMERA,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
                };

        boolean needRequire  = false;

        for(String access : accessPermissions)
        {
            int curPermission = ActivityCompat.checkSelfPermission(this, access);
            if(curPermission != PackageManager.PERMISSION_GRANTED)
            {
                needRequire = true;
                break;
            }
        }
        if (needRequire)
        {
            ActivityCompat.requestPermissions(this, accessPermissions, PERMISSION_REQUEST_CODE_CAMERA);
            return;
        }
        NotifyCameraPermission(true);
    }

    int getRotationDegree()
    {
        return 90 * ((WindowManager)(getSystemService(WINDOW_SERVICE))).getDefaultDisplay().getRotation();
    }

    void setImmersiveSticky()
    {
        View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    public void EnableUI()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run()
            {
                try
                {
                    LayoutInflater layoutInflater = (LayoutInflater) getBaseContext().getSystemService(LAYOUT_INFLATER_SERVICE);
                    View popup_view = layoutInflater.inflate(R.layout.main, null);

                    RelativeLayout mainLayout = new RelativeLayout(_saved_instance);
                    ViewGroup.MarginLayoutParams params = new ViewGroup.MarginLayoutParams(-1, -1);
                    params.setMargins(0, 0, 0, 0);
                    _saved_instance.setContentView(mainLayout, params);
                }
                catch (WindowManager.BadTokenException e)
                {
                    Log.e(DBG_TAG, "UI exception happened: " + e.getMessage());
                }
            }});
    }

    private native static void NotifyCameraPermission(boolean granted);

    static
    {
        System.loadLibrary("cam_ncnn_win");
    }
}