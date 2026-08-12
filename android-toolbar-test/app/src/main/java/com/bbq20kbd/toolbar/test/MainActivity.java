package com.bbq20kbd.toolbar.test;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {
    private TextView status;
    private boolean openedOverlaySettings;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        int p = dp(24);
        root.setPadding(p, p, p, p);
        root.setBackgroundColor(Color.rgb(250, 250, 250));

        TextView title = new TextView(this);
        title.setText("HW Keyboard Toolbar Test");
        title.setTextSize(22f);
        title.setTextColor(Color.BLACK);
        root.addView(title, new LinearLayout.LayoutParams(-1, -2));

        status = new TextView(this);
        status.setTextSize(16f);
        status.setTextColor(Color.DKGRAY);
        LinearLayout.LayoutParams statusLp = new LinearLayout.LayoutParams(-1, -2);
        statusLp.topMargin = dp(18);
        root.addView(status, statusLp);

        Button permission = new Button(this);
        permission.setText("오버레이 권한 열기");
        permission.setOnClickListener(v -> openOverlayPermission());
        LinearLayout.LayoutParams buttonLp = new LinearLayout.LayoutParams(-1, -2);
        buttonLp.topMargin = dp(18);
        root.addView(permission, buttonLp);

        Button stop = new Button(this);
        stop.setText("테스트 종료 / 툴바 제거");
        stop.setOnClickListener(v -> {
            stopService(new Intent(this, ToolbarService.class));
            Toast.makeText(this, "툴바 서비스를 종료했습니다.", Toast.LENGTH_SHORT).show();
            refreshStatus();
        });
        LinearLayout.LayoutParams stopLp = new LinearLayout.LayoutParams(-1, -2);
        stopLp.topMargin = dp(8);
        root.addView(stop, stopLp);

        TextView info = new TextView(this);
        info.setText("오버레이 권한을 허용한 뒤 앱으로 돌아오면 자동 시작합니다.\n" +
                "외장 알파벳 하드웨어 키보드가 연결되면 화면 맨 아래 한 줄 툴바가 나타나고, 연결을 끊으면 사라집니다.");
        info.setTextColor(Color.GRAY);
        info.setTextSize(14f);
        LinearLayout.LayoutParams infoLp = new LinearLayout.LayoutParams(-1, -2);
        infoLp.topMargin = dp(18);
        root.addView(info, infoLp);

        setContentView(root);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (Settings.canDrawOverlays(this)) {
            startService(new Intent(this, ToolbarService.class));
        } else if (!openedOverlaySettings) {
            openedOverlaySettings = true;
            openOverlayPermission();
        }
        refreshStatus();
    }

    private void refreshStatus() {
        if (status == null) return;
        if (Settings.canDrawOverlays(this)) {
            status.setText("오버레이 권한: 허용됨\n서비스: 시작 요청됨");
        } else {
            status.setText("오버레이 권한: 허용 필요");
        }
    }

    private void openOverlayPermission() {
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION));
        }
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
