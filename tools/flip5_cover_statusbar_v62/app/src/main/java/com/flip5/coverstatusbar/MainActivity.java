package com.flip5.coverstatusbar;

import android.Manifest;
import android.app.Activity;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;

public class MainActivity extends Activity {
    private static final int REQ_PHONE = 620;
    private static final String CHANNEL_ID = "flip5_cover_statusbar";

    private boolean overlayRequestLaunched;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        continuePermissionFlow();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (overlayRequestLaunched) {
            if (!Settings.canDrawOverlays(this)) {
                return;
            }
            overlayRequestLaunched = false;
        }
        continuePermissionFlow();
    }

    private void continuePermissionFlow() {
        // 1) Overlay permission: Android exposes this through a system settings screen.
        if (!Settings.canDrawOverlays(this)) {
            if (!overlayRequestLaunched) {
                overlayRequestLaunched = true;
                Intent i = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:" + getPackageName()));
                startActivity(i);
            }
            return;
        }

        // 2) Phone state: normal runtime permission dialog inside the app.
        if (Build.VERSION.SDK_INT >= 23
                && checkSelfPermission(Manifest.permission.READ_PHONE_STATE)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.READ_PHONE_STATE}, REQ_PHONE);
            return;
        }

        // 3) Notifications: this app intentionally keeps targetSdk <= 32 for the existing
        // long-running overlay behavior. On Android 13+, creating the channel while this
        // Activity is in the foreground causes Android to show its notification permission
        // dialog automatically for targetSdk <= 32 apps.
        ensureNotificationChannel();

        startOverlayService();
        finishAndRemoveTask();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_PHONE) {
            // Continue even if the user declines. The status bar still works and will use
            // fallback cellular graphics instead of live telephony details.
            ensureNotificationChannel();
            startOverlayService();
            finishAndRemoveTask();
        }
    }

    private void ensureNotificationChannel() {
        if (Build.VERSION.SDK_INT < 26) return;

        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (nm == null) return;

        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "Flip5 Cover StatusBar",
                NotificationManager.IMPORTANCE_MIN);
        channel.setShowBadge(false);
        channel.setSound(null, null);
        nm.createNotificationChannel(channel);
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
