package com.yyang.camncnnwin;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.NativeActivity;
import android.content.Context;
import android.content.res.AssetManager;
import android.content.pm.PackageManager;
import android.graphics.Color;
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
import android.widget.Button;
import android.widget.PopupWindow;
import android.widget.RelativeLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;

import static android.hardware.camera2.CameraMetadata.LENS_FACING_BACK;


public class MainActivity extends NativeActivity implements ActivityCompat.OnRequestPermissionsResultCallback
{
    volatile MainActivity _saved_instance;

    private final String DBG_TAG = "CAM2NCNN2WIN";
    private static final int PERMISSION_REQUEST_CODE_CAMERA = 1;

    PopupWindow _popup_window;
    TextView _ncnn_datatype_textview;
    TextView _ncnn_result_textview;
    Button _vkimagemat_button;
    Button _vkmat_button;
    Button _detect_button;

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        _saved_instance  = this;

        NetworkInit(getAssets());

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
        if (_popup_window != null && _popup_window.isShowing())
        {
            _popup_window.dismiss();
            _popup_window = null;
        }
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
            Log.e(DBG_TAG, "found legacy camera Device, this demo needs camera2 device");
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
                    if (_popup_window != null)
                    {
                        _popup_window.dismiss();
                    }
                    LayoutInflater layoutInflater = (LayoutInflater) getBaseContext().getSystemService(LAYOUT_INFLATER_SERVICE);
                    View popup_view = layoutInflater.inflate(R.layout.main, null);

                    _popup_window = new PopupWindow(
                            popup_view,
                        WindowManager.LayoutParams.MATCH_PARENT,
                        WindowManager.LayoutParams.WRAP_CONTENT);

                    RelativeLayout mainLayout = new RelativeLayout(_saved_instance);
                    ViewGroup.MarginLayoutParams params = new ViewGroup.MarginLayoutParams(-1, -1);
                    params.setMargins(0, 0, 0, 0);
                    _saved_instance.setContentView(mainLayout, params);

                    _popup_window.showAtLocation(mainLayout, Gravity.BOTTOM | Gravity.START, 0, 0);
                    _popup_window.update();

                    _ncnn_datatype_textview = (TextView) popup_view.findViewById(R.id.currrent_ncnn_data_type);
                    _ncnn_result_textview = (TextView) popup_view.findViewById(R.id.detect_result);

                    _vkimagemat_button = (Button) popup_view.findViewById(R.id.button_vkimagemat);
                    _vkimagemat_button.setOnClickListener(new View.OnClickListener() {
                        @Override
                        public void onClick(View v)
                        {
                            UpateNcnnType(true);
                            _ncnn_datatype_textview.setText("currrent ncnn data type: VkImageMat");
                        }
                    });
                    _vkimagemat_button.setEnabled(true);

                    _vkmat_button = (Button) popup_view.findViewById(R.id.button_vkmat);
                    _vkmat_button.setOnClickListener(new View.OnClickListener() {
                        @Override
                        public void onClick(View v)
                        {
                            UpateNcnnType(false);
                            _ncnn_datatype_textview.setText("currrent ncnn data type: VkMat");
                        }
                    });
                    _vkmat_button.setEnabled(true);

                    _detect_button = (Button) popup_view.findViewById(R.id.button_squeezenet);
                    _detect_button.setOnClickListener(new View.OnClickListener() {
                        @Override
                        public void onClick(View v)
                        {
                            NetworkDetect();
                            _ncnn_result_textview.setText("detect result: null");
                        }
                    });
                    _detect_button.setEnabled(true);
                }
                catch (WindowManager.BadTokenException e)
                {
                    Log.e(DBG_TAG, "UI exception happened: " + e.getMessage());
                }
            }});
    }

    native static void UpateNcnnType(boolean vkimagemat_flag);
    native static void NetworkInit(AssetManager mgr);
    native static void NetworkDetect();
    private native static void NotifyCameraPermission(boolean granted);

    static
    {
        System.loadLibrary("cam_ncnn_win");
    }
}