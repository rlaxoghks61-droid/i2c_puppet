package com.bbq20kbd.toolbar.test;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.hardware.input.InputManager;
import android.os.IBinder;
import android.provider.Settings;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public class ToolbarService extends Service implements InputManager.InputDeviceListener {
    private InputManager inputManager;
    private WindowManager windowManager;
    private View toolbar;

    @Override
    public void onCreate() {
        super.onCreate();
        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        windowManager = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        if (inputManager != null) inputManager.registerInputDeviceListener(this, null);
        updateToolbar();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        updateToolbar();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (inputManager != null) inputManager.unregisterInputDeviceListener(this);
        hideToolbar();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override public void onInputDeviceAdded(int deviceId) { updateToolbar(); }
    @Override public void onInputDeviceRemoved(int deviceId) { updateToolbar(); }
    @Override public void onInputDeviceChanged(int deviceId) { updateToolbar(); }

    private void updateToolbar() {
        if (!Settings.canDrawOverlays(this)) {
            hideToolbar();
            return;
        }
        if (hasExternalAlphabeticKeyboard()) showToolbar();
        else hideToolbar();
    }

    private boolean hasExternalAlphabeticKeyboard() {
        int[] ids = InputDevice.getDeviceIds();
        for (int id : ids) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null || d.isVirtual()) continue;
            if ((d.getSources() & InputDevice.SOURCE_KEYBOARD) != InputDevice.SOURCE_KEYBOARD) continue;
            if (d.getKeyboardType() != InputDevice.KEYBOARD_TYPE_ALPHABETIC) continue;
            return true;
        }
        return false;
    }

    private void showToolbar() {
        if (toolbar != null || windowManager == null) return;

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(10), 0, dp(6), 0);
        bar.setBackgroundColor(Color.argb(245, 245, 245, 245));

        TextView status = new TextView(this);
        status.setText("⌨  하드웨어 키보드");
        status.setTextSize(14f);
        status.setTextColor(Color.BLACK);
        LinearLayout.LayoutParams textLp = new LinearLayout.LayoutParams(0, -2, 1f);
        bar.addView(status, textLp);

        Button ime = new Button(this);
        ime.setText("IME");
        ime.setAllCaps(false);
        ime.setMinWidth(0);
        ime.setMinimumWidth(0);
        ime.setOnClickListener(v -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.showInputMethodPicker();
        });
        bar.addView(ime, new LinearLayout.LayoutParams(dp(70), dp(44)));

        Button close = new Button(this);
        close.setText("×");
        close.setTextSize(20f);
        close.setMinWidth(0);
        close.setMinimumWidth(0);
        close.setOnClickListener(v -> hideToolbar());
        bar.addView(close, new LinearLayout.LayoutParams(dp(52), dp(44)));

        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                dp(52),
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.BOTTOM;
        lp.setTitle("HWKeyboardToolbarTest");

        try {
            windowManager.addView(bar, lp);
            toolbar = bar;
        } catch (Exception ignored) {
            toolbar = null;
        }
    }

    private void hideToolbar() {
        if (toolbar == null || windowManager == null) return;
        try {
            windowManager.removeView(toolbar);
        } catch (Exception ignored) {
        }
        toolbar = null;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
