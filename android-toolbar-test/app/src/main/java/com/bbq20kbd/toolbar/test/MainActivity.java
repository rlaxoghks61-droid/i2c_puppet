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
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {
    private TextView status;
    private TextView diagnostics;
    private boolean openedOverlaySettings;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        int p = dp(20);
        root.setPadding(p, p, p, p);
        root.setBackgroundColor(Color.rgb(250, 250, 250));

        TextView title = new TextView(this);
        title.setText("HW Keyboard Toolbar Test v2");
        title.setTextSize(22f);
        title.setTextColor(Color.BLACK);
        root.addView(title, new LinearLayout.LayoutParams(-1, -2));

        status = new TextView(this);
        status.setTextSize(16f);
        status.setTextColor(Color.DKGRAY);
        LinearLayout.LayoutParams statusLp = new LinearLayout.LayoutParams(-1, -2);
        statusLp.topMargin = dp(14);
        root.addView(status, statusLp);

        Button permission = new Button(this);
        permission.setText("오버레이 권한 열기");
        permission.setOnClickListener(v -> openOverlayPermission());
        LinearLayout.LayoutParams lp1 = new LinearLayout.LayoutParams(-1, -2);
        lp1.topMargin = dp(12);
        root.addView(permission, lp1);

        Button force = new Button(this);
        force.setText("1. 키보드 무시하고 툴바 강제 표시");
        force.setOnClickListener(v -> {
            Intent i = new Intent(this, ToolbarService.class);
            i.setAction(ToolbarService.ACTION_FORCE_SHOW);
            startService(i);
            Toast.makeText(this, "강제 표시 요청", Toast.LENGTH_SHORT).show();
            refreshDiagnosticsDelayed();
        });
        LinearLayout.LayoutParams lp2 = new LinearLayout.LayoutParams(-1, -2);
        lp2.topMargin = dp(8);
        root.addView(force, lp2);

        Button auto = new Button(this);
        auto.setText("2. 하드웨어 키보드 자동 감지 모드");
        auto.setOnClickListener(v -> {
            Intent i = new Intent(this, ToolbarService.class);
            i.setAction(ToolbarService.ACTION_AUTO);
            startService(i);
            refreshDiagnosticsDelayed();
        });
        LinearLayout.LayoutParams lp3 = new LinearLayout.LayoutParams(-1, -2);
        lp3.topMargin = dp(8);
        root.addView(auto, lp3);

        Button refresh = new Button(this);
        refresh.setText("3. 진단 정보 새로고침");
        refresh.setOnClickListener(v -> refreshDiagnostics());
        LinearLayout.LayoutParams lp4 = new LinearLayout.LayoutParams(-1, -2);
        lp4.topMargin = dp(8);
        root.addView(refresh, lp4);

        Button stop = new Button(this);
        stop.setText("툴바 제거 / 서비스 종료");
        stop.setOnClickListener(v -> {
            Intent hide = new Intent(this, ToolbarService.class);
            hide.setAction(ToolbarService.ACTION_HIDE);
            startService(hide);
            stopService(new Intent(this, ToolbarService.class));
            refreshDiagnosticsDelayed();
        });
        LinearLayout.LayoutParams lp5 = new LinearLayout.LayoutParams(-1, -2);
        lp5.topMargin = dp(8);
        root.addView(stop, lp5);

        diagnostics = new TextView(this);
        diagnostics.setTextSize(12f);
        diagnostics.setTextColor(Color.BLACK);
        diagnostics.setTextIsSelectable(true);
        diagnostics.setPadding(0, dp(12), 0, dp(24));
        root.addView(diagnostics, new LinearLayout.LayoutParams(-1, -2));

        ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (Settings.canDrawOverlays(this)) {
            Intent i = new Intent(this, ToolbarService.class);
            i.setAction(ToolbarService.ACTION_AUTO);
            startService(i);
        } else if (!openedOverlaySettings) {
            openedOverlaySettings = true;
            openOverlayPermission();
        }
        refreshStatus();
        refreshDiagnosticsDelayed();
    }

    private void refreshStatus() {
        if (status == null) return;
        status.setText(Settings.canDrawOverlays(this)
                ? "오버레이 권한: 허용됨"
                : "오버레이 권한: 허용 필요");
    }

    private void refreshDiagnostics() {
        refreshStatus();
        if (diagnostics != null) diagnostics.setText(ToolbarService.buildDiagnostics(this));
    }

    private void refreshDiagnosticsDelayed() {
        if (diagnostics != null) diagnostics.postDelayed(this::refreshDiagnostics, 300);
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
