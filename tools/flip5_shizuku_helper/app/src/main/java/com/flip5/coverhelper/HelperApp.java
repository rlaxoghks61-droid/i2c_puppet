package com.flip5.coverhelper;

import android.app.Application;
import android.content.ComponentName;
import android.content.Context;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.IBinder;

import java.util.concurrent.atomic.AtomicBoolean;

import rikka.shizuku.Shizuku;

public final class HelperApp extends Application {
    public static final int REQ_SHIZUKU = 2401;
    private static final AtomicBoolean BINDING = new AtomicBoolean(false);
    private static volatile boolean connected = false;
    private static volatile String lastStatus = "Shizuku waiting";

    private static final Shizuku.UserServiceArgs USER_SERVICE_ARGS =
            new Shizuku.UserServiceArgs(new ComponentName(
                    "com.flip5.coverhelper",
                    CoverWatcherService.class.getName()))
                    .daemon(true)
                    .processNameSuffix("cover_watcher")
                    .debuggable(false)
                    .version(1);

    private static final ServiceConnection CONNECTION = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            connected = service != null && service.pingBinder();
            BINDING.set(false);
            lastStatus = connected ? "Watcher running as Shizuku shell" : "Watcher binder invalid";
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            connected = false;
            BINDING.set(false);
            lastStatus = "Watcher disconnected; waiting for Shizuku";
        }
    };

    private static final Shizuku.OnBinderReceivedListener BINDER_RECEIVED = HelperApp::startWatcher;
    private static final Shizuku.OnBinderDeadListener BINDER_DEAD = () -> {
        connected = false;
        BINDING.set(false);
        lastStatus = "Shizuku stopped; auto-restart when it returns";
    };

    @Override
    public void onCreate() {
        super.onCreate();
        Shizuku.addBinderReceivedListenerSticky(BINDER_RECEIVED);
        Shizuku.addBinderDeadListener(BINDER_DEAD);
        startWatcher();
    }

    public static void kick(Context ignored) {
        startWatcher();
    }

    public static synchronized void startWatcher() {
        try {
            if (!Shizuku.pingBinder()) {
                lastStatus = "Shizuku is not running";
                return;
            }
            if (Shizuku.isPreV11()) {
                lastStatus = "Shizuku v11+ required";
                return;
            }
            if (Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED) {
                lastStatus = "Shizuku permission required";
                return;
            }
            if (connected || !BINDING.compareAndSet(false, true)) return;

            lastStatus = "Starting watcher...";
            Shizuku.bindUserService(USER_SERVICE_ARGS, CONNECTION);
        } catch (Throwable t) {
            BINDING.set(false);
            connected = false;
            lastStatus = "Start failed: " + t.getClass().getSimpleName() + ": " + t.getMessage();
        }
    }

    public static String status() {
        return lastStatus;
    }

    public static boolean isConnected() {
        return connected;
    }
}
