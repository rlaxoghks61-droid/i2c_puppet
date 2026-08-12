package com.flip5.coverhelper;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import rikka.shizuku.Shizuku;

public final class MainActivity extends Activity {
    private TextView status;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private final Shizuku.OnRequestPermissionResultListener permissionListener = (requestCode, grantResult) -> {
        if (requestCode != HelperApp.REQ_SHIZUKU) return;
        if (grantResult == PackageManager.PERMISSION_GRANTED) {
            HelperApp.startWatcher();
        }
        updateStatus();
    };

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            updateStatus();
            handler.postDelayed(this, 1000);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Shizuku.addRequestPermissionResultListener(permissionListener);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(40, 60, 40, 40);

        TextView title = new TextView(this);
        title.setText("Flip5 Cover Helper");
        title.setTextSize(22f);
        root.addView(title);

        status = new TextView(this);
        status.setTextSize(16f);
        status.setPadding(0, 30, 0, 30);
        root.addView(status);

        Button start = new Button(this);
        start.setText("Shizuku 권한 / 시작");
        start.setOnClickListener(v -> requestOrStart());
        root.addView(start);

        TextView note = new TextView(this);
        note.setPadding(0, 30, 0, 0);
        note.setText("한 번 권한을 허용하면 Shizuku가 살아나는 즉시 60px watcher를 자동으로 다시 시작합니다.\n" +
                "커버 홈: 0px + 삼성 배터리 숨김 / 일반 앱: 60px / 네이버지도: 0px\n" +
                "상태바 v6.1 앱도 watcher 시작 시 자동 실행합니다.");
        root.addView(note);

        setContentView(root);
        requestOrStart();
        handler.post(refresh);
    }

    private void requestOrStart() {
        try {
            if (!Shizuku.pingBinder()) {
                updateStatusText("Shizuku가 실행 중이 아닙니다. 먼저 Shizuku를 시작하세요.");
                return;
            }
            if (Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED) {
                HelperApp.startWatcher();
            } else if (!Shizuku.shouldShowRequestPermissionRationale()) {
                Shizuku.requestPermission(HelperApp.REQ_SHIZUKU);
                updateStatusText("Shizuku 권한 요청 중...");
            } else {
                updateStatusText("Shizuku에서 이 앱 권한을 허용해야 합니다.");
            }
        } catch (Throwable t) {
            updateStatusText("오류: " + t);
        }
    }

    private void updateStatus() {
        String shizuku;
        try {
            shizuku = Shizuku.pingBinder() ? "Shizuku: running, uid=" + Shizuku.getUid() : "Shizuku: stopped";
        } catch (Throwable t) {
            shizuku = "Shizuku: unavailable";
        }
        updateStatusText(shizuku + "\n" + HelperApp.status());
    }

    private void updateStatusText(String text) {
        if (status != null) status.setText(text);
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(refresh);
        Shizuku.removeRequestPermissionResultListener(permissionListener);
        super.onDestroy();
    }
}
