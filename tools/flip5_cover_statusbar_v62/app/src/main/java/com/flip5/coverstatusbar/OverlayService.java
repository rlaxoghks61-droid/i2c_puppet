package com.flip5.coverstatusbar;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.Drawable;
import android.hardware.display.DisplayManager;
import android.media.AudioManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.telephony.ServiceState;
import android.telephony.SignalStrength;
import android.telephony.TelephonyManager;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextClock;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class OverlayService extends Service {
    private static final String CHANNEL_ID = "flip5_cover_statusbar";
    private static final int NOTIFICATION_ID = 6201;
    private static final int COVER_DISPLAY_ID = 1;
    private static final int COVER_WIDTH_PX = 748;
    private static final int BAR_HEIGHT_PX = 60;
    private static final int RIGHT_SAFE_MARGIN_PX = 32;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private WindowManager windowManager;
    private FrameLayout root;
    private Context coverWindowContext;
    private Resources systemUiResources;

    private TextClock clock;
    private ImageView ringer;
    private ImageView wifi;
    private ImageView cell;
    private ImageView data;
    private TextView battery;

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            try {
                ensureOverlay();
                updateStatus();
                updateNativeBatteryBlacklist();
            } catch (Throwable ignored) {
            }
            handler.postDelayed(this, 1000L);
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        startInForeground();
        handler.post(tick);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        handler.removeCallbacks(tick);
        removeOverlay();
        restoreOriginalBlacklist();
        super.onDestroy();
    }

    private void startInForeground() {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Flip5 Cover StatusBar",
                    NotificationManager.IMPORTANCE_MIN);
            channel.setShowBadge(false);
            channel.setSound(null, null);
            nm.createNotificationChannel(channel);
        }

        Notification.Builder builder = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        Notification n = builder
                .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
                .setContentTitle("Flip5 Cover StatusBar")
                .setContentText("커버 상태바 유지 중")
                .setOngoing(true)
                .setShowWhen(false)
                .build();
        startForeground(NOTIFICATION_ID, n);
    }

    private void ensureOverlay() {
        if (!Settings.canDrawOverlays(this)) {
            return;
        }
        if (root != null && root.isAttachedToWindow()) {
            return;
        }
        removeOverlay();

        DisplayManager dm = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        if (dm == null) return;
        Display cover = dm.getDisplay(COVER_DISPLAY_ID);
        if (cover == null) return;

        Context displayContext = createDisplayContext(cover);
        if (Build.VERSION.SDK_INT >= 30) {
            coverWindowContext = displayContext.createWindowContext(
                    WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, null);
        } else {
            coverWindowContext = displayContext;
        }

        windowManager = (WindowManager) coverWindowContext.getSystemService(WINDOW_SERVICE);
        if (windowManager == null) return;

        try {
            Context sysui = coverWindowContext.createPackageContext(
                    "com.android.systemui", Context.CONTEXT_IGNORE_SECURITY);
            systemUiResources = sysui.getResources();
        } catch (Throwable t) {
            systemUiResources = null;
        }

        buildViews();

        WindowManager.LayoutParams lp = new WindowManager.LayoutParams();
        // Fixed logical width is intentional: do not let WindowManager recalculate
        // our right edge from a changing app/inset frame on the cover display.
        lp.width = COVER_WIDTH_PX;
        lp.height = BAR_HEIGHT_PX;
        lp.type = WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY;
        lp.flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS;
        lp.format = PixelFormat.TRANSLUCENT;
        lp.gravity = Gravity.TOP | Gravity.LEFT;
        lp.x = 0;
        lp.y = 0;
        if (Build.VERSION.SDK_INT >= 28) {
            lp.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        }

        windowManager.addView(root, lp);
    }

    private void buildViews() {
        root = new FrameLayout(coverWindowContext);
        root.setClipChildren(false);
        root.setClipToPadding(false);

        LinearLayout cluster = new LinearLayout(coverWindowContext);
        cluster.setOrientation(LinearLayout.HORIZONTAL);
        cluster.setGravity(Gravity.CENTER_VERTICAL);
        cluster.setClipChildren(false);
        cluster.setClipToPadding(false);

        FrameLayout.LayoutParams clusterLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                BAR_HEIGHT_PX,
                Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        clusterLp.rightMargin = RIGHT_SAFE_MARGIN_PX;
        root.addView(cluster, clusterLp);

        clock = new TextClock(coverWindowContext);
        clock.setFormat24Hour("HH:mm");
        clock.setTextSize(14f);
        clock.setGravity(Gravity.CENTER_VERTICAL | Gravity.CENTER_HORIZONTAL);
        cluster.addView(clock, slot(72));

        ringer = iconView();
        cluster.addView(ringer, slot(28));

        wifi = iconView();
        cluster.addView(wifi, slot(36));

        cell = iconView();
        cluster.addView(cell, slot(33));

        data = iconView();
        cluster.addView(data, slot(36));

        battery = new TextView(coverWindowContext);
        battery.setTextSize(14f);
        battery.setSingleLine(true);
        battery.setGravity(Gravity.CENTER_VERTICAL | Gravity.RIGHT);
        cluster.addView(battery, slot(72));
    }

    private LinearLayout.LayoutParams slot(int widthPx) {
        return new LinearLayout.LayoutParams(widthPx, BAR_HEIGHT_PX);
    }

    private ImageView iconView() {
        ImageView v = new ImageView(coverWindowContext);
        v.setScaleType(ImageView.ScaleType.CENTER_INSIDE);
        return v;
    }

    private void updateStatus() {
        if (root == null || !root.isAttachedToWindow()) return;

        boolean night = (coverWindowContext.getResources().getConfiguration().uiMode
                & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        int fg = night ? Color.WHITE : Color.BLACK;
        clock.setTextColor(fg);
        battery.setTextColor(fg);

        updateRinger(fg);
        updateWifi(fg);
        updateCell(fg);
        updateBattery();
    }

    private void updateRinger(int fg) {
        AudioManager am = (AudioManager) getSystemService(AUDIO_SERVICE);
        if (am == null) {
            hideButKeepSlot(ringer);
            return;
        }
        int mode = am.getRingerMode();
        if (mode == AudioManager.RINGER_MODE_VIBRATE) {
            setIcon(ringer, "sec_stat_sys_ringer_vibrate", fg);
        } else if (mode == AudioManager.RINGER_MODE_SILENT) {
            setIcon(ringer, "sec_stat_sys_ringer_silent", fg);
        } else {
            hideButKeepSlot(ringer);
        }
    }

    private void updateWifi(int fg) {
        ConnectivityManager cm = (ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE);
        WifiManager wm = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
        if (cm == null || wm == null) {
            hideButKeepSlot(wifi);
            return;
        }

        try {
            Network active = cm.getActiveNetwork();
            NetworkCapabilities caps = active == null ? null : cm.getNetworkCapabilities(active);
            if (caps == null || !caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                hideButKeepSlot(wifi);
                return;
            }

            WifiInfo info = wm.getConnectionInfo();
            if (info == null) {
                hideButKeepSlot(wifi);
                return;
            }

            int level = WifiManager.calculateSignalLevel(info.getRssi(), 5);
            String prefix = "stat_sys_wifi_signal_";
            if (Build.VERSION.SDK_INT >= 30) {
                int standard = info.getWifiStandard();
                if (standard == 5) {
                    prefix = "stat_sys_wifi5_signal_";
                } else if (standard == 6) {
                    if (info.getFrequency() >= 5925) {
                        prefix = "stat_sys_6ewifi_signal_";
                    } else {
                        prefix = "stat_sys_wifi6_signal_";
                    }
                } else if (standard >= 8) {
                    prefix = "stat_sys_wifi7_signal_";
                }
            }

            if (!setIcon(wifi, prefix + level, fg)) {
                setIcon(wifi, "stat_sys_wifi_signal_" + level, fg);
            }
        } catch (Throwable t) {
            hideButKeepSlot(wifi);
        }
    }

    private void updateCell(int fg) {
        TelephonyManager tm = (TelephonyManager) getSystemService(TELEPHONY_SERVICE);
        if (tm == null) {
            hideButKeepSlot(cell);
            hideButKeepSlot(data);
            return;
        }

        try {
            if (Build.VERSION.SDK_INT >= 23
                    && checkSelfPermission(Manifest.permission.READ_PHONE_STATE)
                    != PackageManager.PERMISSION_GRANTED) {
                hideButKeepSlot(cell);
                hideButKeepSlot(data);
                return;
            }

            ServiceState state = tm.getServiceState();
            if (state == null || state.getState() != ServiceState.STATE_IN_SERVICE) {
                hideButKeepSlot(cell);
                hideButKeepSlot(data);
                return;
            }

            int level = 4;
            if (Build.VERSION.SDK_INT >= 28) {
                SignalStrength ss = tm.getSignalStrength();
                if (ss != null) level = ss.getLevel();
            }
            if (!setIcon(cell, "stat_sys_signal_" + level, fg)) {
                setIcon(cell, "stat_sys_signal_null_4", fg);
            }

            int networkType = tm.getDataNetworkType();
            if (networkType == TelephonyManager.NETWORK_TYPE_NR) {
                setIcon(data, "stat_sys_data_connected_5g", fg);
            } else if (networkType == TelephonyManager.NETWORK_TYPE_LTE) {
                setIcon(data, "stat_sys_data_connected_lte", fg);
            } else {
                hideButKeepSlot(data);
            }
        } catch (Throwable t) {
            hideButKeepSlot(cell);
            hideButKeepSlot(data);
        }
    }

    private void updateBattery() {
        BatteryManager bm = (BatteryManager) getSystemService(BATTERY_SERVICE);
        if (bm == null) return;
        int pct = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY);
        if (pct >= 0 && pct <= 100) {
            battery.setText(pct + "%");
        }
    }

    private boolean setIcon(ImageView view, String resourceName, int fg) {
        if (view == null || systemUiResources == null) {
            hideButKeepSlot(view);
            return false;
        }
        try {
            int id = systemUiResources.getIdentifier(
                    resourceName, "drawable", "com.android.systemui");
            if (id == 0) {
                hideButKeepSlot(view);
                return false;
            }
            Drawable d = systemUiResources.getDrawable(id, null);
            view.setImageDrawable(d);
            view.setColorFilter(fg);
            view.setVisibility(View.VISIBLE);
            return true;
        } catch (Throwable t) {
            hideButKeepSlot(view);
            return false;
        }
    }

    private void hideButKeepSlot(ImageView view) {
        if (view == null) return;
        view.setImageDrawable(null);
        // INVISIBLE instead of GONE keeps every slot width constant, so battery x never changes.
        view.setVisibility(View.INVISIBLE);
    }

    private void updateNativeBatteryBlacklist() {
        if (Build.VERSION.SDK_INT >= 23
                && checkSelfPermission("android.permission.WRITE_SECURE_SETTINGS")
                != PackageManager.PERMISSION_GRANTED) {
            return;
        }

        DisplayManager dm = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        if (dm == null) return;
        Display cover = dm.getDisplay(COVER_DISPLAY_ID);
        Display main = dm.getDisplay(Display.DEFAULT_DISPLAY);
        if (cover == null || main == null) return;

        boolean coverActive = cover.getState() == Display.STATE_ON;
        boolean mainInactive = main.getState() == Display.STATE_OFF;
        if (coverActive && mainInactive) {
            addBatteryToBlacklist();
        } else {
            restoreOriginalBlacklist();
        }
    }

    private void addBatteryToBlacklist() {
        SharedPreferences p = getSharedPreferences("secure_state", MODE_PRIVATE);
        if (!p.getBoolean("saved", false)) {
            String original = Settings.Secure.getString(getContentResolver(), "icon_blacklist");
            p.edit()
                    .putBoolean("saved", true)
                    .putString("original", original == null ? "__NULL__" : original)
                    .apply();
        }

        String current = Settings.Secure.getString(getContentResolver(), "icon_blacklist");
        Set<String> slots = parseSlots(current);
        if (slots.add("battery")) {
            Settings.Secure.putString(
                    getContentResolver(), "icon_blacklist", joinSlots(slots));
        }
    }

    private void restoreOriginalBlacklist() {
        if (Build.VERSION.SDK_INT >= 23
                && checkSelfPermission("android.permission.WRITE_SECURE_SETTINGS")
                != PackageManager.PERMISSION_GRANTED) {
            return;
        }
        SharedPreferences p = getSharedPreferences("secure_state", MODE_PRIVATE);
        if (!p.getBoolean("saved", false)) return;
        String original = p.getString("original", "__NULL__");
        if ("__NULL__".equals(original)) {
            Settings.Secure.putString(getContentResolver(), "icon_blacklist", null);
        } else {
            Settings.Secure.putString(getContentResolver(), "icon_blacklist", original);
        }
    }

    private Set<String> parseSlots(String value) {
        Set<String> out = new LinkedHashSet<>();
        if (value == null || value.trim().isEmpty()) return out;
        String[] parts = value.split(",");
        for (String s : parts) {
            String t = s.trim();
            if (!t.isEmpty()) out.add(t);
        }
        return out;
    }

    private String joinSlots(Set<String> slots) {
        List<String> list = new ArrayList<>(slots);
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < list.size(); i++) {
            if (i > 0) b.append(',');
            b.append(list.get(i));
        }
        return b.toString();
    }

    private void removeOverlay() {
        if (windowManager != null && root != null) {
            try {
                windowManager.removeViewImmediate(root);
            } catch (Throwable ignored) {
            }
        }
        root = null;
        windowManager = null;
        coverWindowContext = null;
        systemUiResources = null;
    }
}
