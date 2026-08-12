package com.flip5.coverstatusbar;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;

public class MainActivity extends Activity {
    private static final int REQ_PHONE = 620;
    private boolean overlayRequestLaunched;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        startIfReady();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (overlayRequestLaunched && Settings.canDrawOverlays(this)) {
            overlayRequestLaunched = false;
            startIfReady();
        }
    }

    private void startIfReady() {
        if (!Settings.canDrawOverlays(this)) {
            if (!overlayRequestLaunched) {
                overlayRequestLaunched = true;
                Intent i = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:" + getPackageName()));
                startActivity(i);
            }
            return;
        }

        if (Build.VERSION.SDK_INT >= 23
                && checkSelfPermission(Manifest.permission.READ_PHONE_STATE)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.READ_PHONE_STATE}, REQ_PHONE);
            return;
        }

        startOverlayService();
        finishAndRemoveTask();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_PHONE) {
            startOverlayService();
            finishAndRemoveTask();
        }
    }

    private void startOverlayService() {
        Intent svc = new Intent(this, OverlayService.class);
        if (Build.VERSION.SDK_INT >= 26) {
            startForegroundService(svc);
        } else {
            startService(svc);
        }
    }
}
