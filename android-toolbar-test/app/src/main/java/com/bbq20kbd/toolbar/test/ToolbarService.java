package com.bbq20kbd.toolbar.test;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.hardware.input.InputManager;
import android.os.IBinder;
import android.provider.Settings;
import android.util.Log;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public class ToolbarService extends Service implements InputManager.InputDeviceListener {
    public static final String ACTION_AUTO = "com.bbq20kbd.toolbar.test.AUTO";
    public static final String ACTION_FORCE_SHOW = "com.bbq20kbd.toolbar.test.FORCE_SHOW";
    public static final String ACTION_HIDE = "com.bbq20kbd.toolbar.test.HIDE";

    private static volatile String lastOverlayError = "없음";
    private static volatile String lastOverlayState = "서비스 미실행";

    private InputManager inputManager;
    private WindowManager windowManager;
    private View toolbar;
    private boolean forceShow;

    @Override
    public void onCreate() {
        super.onCreate();
        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        windowManager = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        if (inputManager != null) inputManager.registerInputDeviceListener(this, null);
        lastOverlayState = "서비스 실행됨";
        updateToolbar();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : null;
        if (ACTION_FORCE_SHOW.equals(action)) {
            forceShow = true;
            showToolbar();
        } else if (ACTION_HIDE.equals(action)) {
            forceShow = false;
            hideToolbar();
        } else {
            forceShow = false;
            updateToolbar();
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (inputManager != null) inputManager.unregisterInputDeviceListener(this);
        hideToolbar();
        lastOverlayState = "서비스 종료됨";
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
        if (forceShow) {
            showToolbar();
            return;
        }
        if (!Settings.canDrawOverlays(this)) {
            lastOverlayState = "오버레이 권한 없음";
            hideToolbar();
            return;
        }
        if (hasExternalAlphabeticKeyboard()) {
            lastOverlayState = "외장 알파벳 키보드 감지됨";
            showToolbar();
        } else {
            lastOverlayState = "외장 알파벳 키보드 미감지";
            hideToolbar();
        }
    }

    private boolean hasExternalAlphabeticKeyboard() {
        int[] ids = InputDevice.getDeviceIds();
        for (int id : ids) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null || d.isVirtual()) continue;
            int sources = d.getSources();
            if ((sources & InputDevice.SOURCE_KEYBOARD) != InputDevice.SOURCE_KEYBOARD) continue;
            if (d.getKeyboardType() != InputDevice.KEYBOARD_TYPE_ALPHABETIC) continue;
            return true;
        }
        return false;
    }

    private void showToolbar() {
        if (!Settings.canDrawOverlays(this)) {
            lastOverlayState = "표시 실패: 오버레이 권한 없음";
            return;
        }
        if (toolbar != null || windowManager == null) return;

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(10), 0, dp(6), 0);
        bar.setBackgroundColor(Color.argb(255, 235, 235, 235));

        TextView status = new TextView(this);
        status.setText(forceShow ? "⌨  TEST TOOLBAR (강제 표시)" : "⌨  하드웨어 키보드");
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
        close.setOnClickListener(v -> {
            forceShow = false;
            hideToolbar();
        });
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
        lp.setTitle("HWKeyboardToolbarTestV2");

        try {
            windowManager.addView(bar, lp);
            toolbar = bar;
            lastOverlayError = "없음";
            lastOverlayState = forceShow ? "강제 툴바 표시 성공" : "자동 툴바 표시 성공";
        } catch (Exception e) {
            toolbar = null;
            lastOverlayError = e.getClass().getSimpleName() + ": " + String.valueOf(e.getMessage());
            lastOverlayState = "WindowManager.addView 실패";
            Log.e("HWToolbarTest", "addView failed", e);
        }
    }

    private void hideToolbar() {
        if (toolbar == null || windowManager == null) return;
        try {
            windowManager.removeView(toolbar);
        } catch (Exception e) {
            Log.e("HWToolbarTest", "removeView failed", e);
        }
        toolbar = null;
    }

    public static String buildDiagnostics(Context context) {
        StringBuilder sb = new StringBuilder();
        sb.append("오버레이 권한: ").append(Settings.canDrawOverlays(context) ? "허용됨" : "없음").append('\n');
        sb.append("상태: ").append(lastOverlayState).append('\n');
        sb.append("마지막 addView 오류: ").append(lastOverlayError).append("\n\n");
        sb.append("InputDevice 목록:\n");
        int[] ids = InputDevice.getDeviceIds();
        if (ids.length == 0) sb.append("(없음)\n");
        for (int id : ids) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null) continue;
            sb.append("#").append(id)
                    .append("  ").append(d.getName())
                    .append("\n  virtual=").append(d.isVirtual())
                    .append("  keyboardType=").append(d.getKeyboardType())
                    .append("  sources=0x").append(Integer.toHexString(d.getSources()))
                    .append('\n');
        }
        return sb.toString();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
