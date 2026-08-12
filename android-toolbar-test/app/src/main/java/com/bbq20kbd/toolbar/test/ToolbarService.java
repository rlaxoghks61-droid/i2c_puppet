package com.bbq20kbd.toolbar.test;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.hardware.input.InputManager;
import android.os.IBinder;
import android.provider.Settings;
import android.util.Log;
import android.view.Display;
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

    private static final int COVER_DISPLAY_ID = 1;
    private static volatile String lastOverlayError = "없음";
    private static volatile String lastOverlayState = "서비스 미실행";
    private static volatile int lastTargetDisplayId = -1;

    private InputManager inputManager;
    private WindowManager windowManager;
    private Context overlayContext;
    private View toolbar;
    private boolean forceShow;

    @Override
    public void onCreate() {
        super.onCreate();
        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
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
            lastOverlayState = "외장 알파벳 키보드 감지됨 - 커버 Display 1 표시 시도";
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

    private boolean prepareCoverWindowManager() {
        try {
            DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
            Display cover = dm != null ? dm.getDisplay(COVER_DISPLAY_ID) : null;
            if (cover == null) {
                lastTargetDisplayId = -1;
                lastOverlayState = "커버 Display 1을 찾지 못함";
                lastOverlayError = "DisplayManager.getDisplay(1) == null";
                return false;
            }

            if (overlayContext == null || lastTargetDisplayId != COVER_DISPLAY_ID || windowManager == null) {
                Context displayContext = createDisplayContext(cover);
                overlayContext = displayContext.createWindowContext(
                        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, null);
                windowManager = overlayContext.getSystemService(WindowManager.class);
                lastTargetDisplayId = cover.getDisplayId();
            }

            if (windowManager == null) {
                lastOverlayState = "커버 Display 1 WindowManager 없음";
                lastOverlayError = "getSystemService(WindowManager.class) == null";
                return false;
            }
            return true;
        } catch (Throwable t) {
            windowManager = null;
            overlayContext = null;
            lastTargetDisplayId = -1;
            lastOverlayError = t.getClass().getSimpleName() + ": " + String.valueOf(t.getMessage());
            lastOverlayState = "커버 Display 1 WindowContext 생성 실패";
            Log.e("HWToolbarTest", "prepareCoverWindowManager failed", t);
            return false;
        }
    }

    private void showToolbar() {
        if (!Settings.canDrawOverlays(this)) {
            lastOverlayState = "표시 실패: 오버레이 권한 없음";
            return;
        }
        if (toolbar != null) return;
        if (!prepareCoverWindowManager()) return;

        Context c = overlayContext != null ? overlayContext : this;

        LinearLayout bar = new LinearLayout(c);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(10), 0, dp(6), 0);
        bar.setBackgroundColor(Color.argb(255, 235, 235, 235));

        TextView status = new TextView(c);
        status.setText(forceShow ? "⌨ COVER TEST (Display 1)" : "⌨ 하드웨어 키보드");
        status.setTextSize(14f);
        status.setTextColor(Color.BLACK);
        LinearLayout.LayoutParams textLp = new LinearLayout.LayoutParams(0, -2, 1f);
        bar.addView(status, textLp);

        Button ime = new Button(c);
        ime.setText("IME");
        ime.setAllCaps(false);
        ime.setMinWidth(0);
        ime.setMinimumWidth(0);
        ime.setOnClickListener(v -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.showInputMethodPicker();
        });
        bar.addView(ime, new LinearLayout.LayoutParams(dp(70), dp(44)));

        Button close = new Button(c);
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
        lp.setTitle("HWKeyboardToolbarCoverDisplay1");

        try {
            windowManager.addView(bar, lp);
            toolbar = bar;
            lastOverlayError = "없음";
            lastOverlayState = forceShow
                    ? "커버 Display 1 강제 툴바 표시 성공"
                    : "커버 Display 1 자동 툴바 표시 성공";
        } catch (Throwable t) {
            toolbar = null;
            lastOverlayError = t.getClass().getSimpleName() + ": " + String.valueOf(t.getMessage());
            lastOverlayState = "커버 Display 1 WindowManager.addView 실패";
            Log.e("HWToolbarTest", "cover addView failed", t);
        }
    }

    private void hideToolbar() {
        if (toolbar == null || windowManager == null) return;
        try {
            windowManager.removeView(toolbar);
        } catch (Throwable t) {
            Log.e("HWToolbarTest", "removeView failed", t);
        }
        toolbar = null;
    }

    public static String buildDiagnostics(Context context) {
        StringBuilder sb = new StringBuilder();
        sb.append("오버레이 권한: ").append(Settings.canDrawOverlays(context) ? "허용됨" : "없음").append('\n');
        sb.append("목표 Display ID: ").append(COVER_DISPLAY_ID).append('\n');
        sb.append("실제 WindowContext Display ID: ").append(lastTargetDisplayId).append('\n');
        sb.append("상태: ").append(lastOverlayState).append('\n');
        sb.append("마지막 addView 오류: ").append(lastOverlayError).append("\n\n");

        sb.append("Display 목록:\n");
        try {
            DisplayManager dm = (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);
            Display[] displays = dm != null ? dm.getDisplays() : new Display[0];
            for (Display d : displays) {
                sb.append("displayId=").append(d.getDisplayId())
                        .append(" state=").append(d.getState())
                        .append(" name=").append(d.getName())
                        .append(" mode=").append(d.getMode().getPhysicalWidth())
                        .append('x').append(d.getMode().getPhysicalHeight())
                        .append('\n');
            }
        } catch (Throwable t) {
            sb.append("Display 조회 오류: ").append(t.getClass().getSimpleName())
                    .append(": ").append(String.valueOf(t.getMessage())).append('\n');
        }

        sb.append("\nInputDevice 목록:\n");
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
        Context c = overlayContext != null ? overlayContext : this;
        return Math.round(value * c.getResources().getDisplayMetrics().density);
    }
}
