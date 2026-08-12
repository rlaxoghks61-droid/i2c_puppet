package com.flip5.coverhelper;

import android.content.Context;
import android.os.Binder;
import android.util.Log;

import java.io.*;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Runs inside a Shizuku UserService process (shell uid 2000 with ADB backend). */
public final class CoverWatcherService extends Binder {
    private static final String TAG = "Flip5CoverWatcher";
    private static final int DISPLAY_ID = 1;
    private static final int INDEX = 82;
    private static final int TOP_INSET = 60;
    private static final String EXCLUDED_PACKAGE = "com.nhn.android.nmap";
    private static final String COVER_HOME = "com.android.systemui.subscreen.SubHomeActivity";
    private static final String VISUAL_PACKAGE = "com.flip5.coverstatusbar";
    private static final String VISUAL_COMPONENT = "com.flip5.coverstatusbar/.MainActivity";

    private static final String ICON_BLACKLIST_KEY = "icon_blacklist";
    private static final String BATTERY_SLOT = "battery";
    private static final File BLACKLIST_BACKUP = new File("/data/local/tmp/flip5_cover_original_icon_blacklist.txt");

    private static final AtomicBoolean STARTED = new AtomicBoolean(false);
    private static String originalIconBlacklist = "__ABSENT__";
    private static boolean batteryHiddenForCoverHome = false;

    private static Class<?> insetsClass;
    private static Class<?> wctClass;
    private static Class<?> windowOrganizerClass;
    private static Class<?> binderClass;
    private static Object owner;
    private static int statusBarsType;
    private static int forceFlags;
    private static Method addInsetsMethod;
    private static Method removeInsetsMethod;
    private static final Set<Integer> appliedTaskIds = new LinkedHashSet<>();

    public CoverWatcherService() {
        startOnce();
    }

    public CoverWatcherService(Context context) {
        startOnce();
    }

    private static void startOnce() {
        if (!STARTED.compareAndSet(false, true)) return;
        Thread t = new Thread(CoverWatcherService::runForever, "flip5-cover-watcher");
        t.setDaemon(false);
        t.start();
    }

    private static void runForever() {
        try {
            init();
            initBatterySettings();
            normalizeBatterySettingsFromBackup();
            startVisualApp();

            Runtime.getRuntime().addShutdownHook(new Thread(() -> {
                try { restoreBatterySettings(); } catch (Throwable ignored) {}
                try { removeFromExistingTasks(); } catch (Throwable ignored) {}
            }, "flip5-cover-cleanup"));

            Log.i(TAG, "START shell uid watcher: apps=60px, coverHome=0px, NaverMap=0px");
            String lastFocus = "";
            long lastVisualCheck = 0;

            while (true) {
                String frontTop = getCoverFrontTopFromRootTasks();
                boolean coverHomeFront = frontTop != null && frontTop.contains(COVER_HOME);
                setCoverHomeBatteryHidden(coverHomeFront);

                List<Object> infos = getRunningTasks();
                for (Object info : infos) {
                    int displayId = getInt(info, "displayId", DISPLAY_ID);
                    if (displayId != DISPLAY_ID) continue;

                    int wm = invokeInt(info, "getWindowingMode", -1);
                    if (!(wm == 0 || wm == 1)) continue;

                    boolean focused = getBoolean(info, "isFocused", false);
                    boolean visible = getBoolean(info, "isVisible", getBoolean(info, "visible", false));
                    if (!(focused || visible)) continue;

                    int taskId = getInt(info, "taskId", -1);
                    if (taskId < 0) continue;

                    Object token = getFieldValue(info, "token");
                    Object top = getFieldValue(info, "topActivity");
                    if (top == null) continue;
                    String topString = String.valueOf(top);

                    boolean excluded = topString.contains(EXCLUDED_PACKAGE);
                    boolean coverHomeTask = topString.contains(COVER_HOME);
                    boolean shouldInset = !excluded && !coverHomeTask;

                    String now = taskId + " " + topString + " inset=" + (shouldInset ? TOP_INSET : 0);
                    if (!now.equals(lastFocus)) {
                        Log.i(TAG, "TASK " + now);
                        lastFocus = now;
                    }

                    if (!shouldInset) {
                        if (appliedTaskIds.contains(taskId)) {
                            removeInset(token, taskId);
                            appliedTaskIds.remove(taskId);
                        }
                    } else if (!appliedTaskIds.contains(taskId)) {
                        addInset(token, taskId, TOP_INSET);
                        appliedTaskIds.add(taskId);
                    }
                }

                long nowMs = System.currentTimeMillis();
                if (nowMs - lastVisualCheck > 15000L) {
                    lastVisualCheck = nowMs;
                    if (!isVisualProcessAlive()) startVisualApp();
                }

                Thread.sleep(250L);
            }
        } catch (Throwable t) {
            Log.e(TAG, "FAILED", t);
            try { restoreBatterySettings(); } catch (Throwable ignored) {}
            try { removeFromExistingTasks(); } catch (Throwable ignored) {}
            STARTED.set(false);
        }
    }

    private static void initBatterySettings() throws Exception {
        if (BLACKLIST_BACKUP.exists()) {
            originalIconBlacklist = readFile(BLACKLIST_BACKUP).trim();
            if (originalIconBlacklist.isEmpty()) originalIconBlacklist = "__ABSENT__";
            return;
        }
        String v = runSettings("get", "secure", ICON_BLACKLIST_KEY);
        if (v == null || v.isEmpty() || "null".equalsIgnoreCase(v)) originalIconBlacklist = "__ABSENT__";
        else originalIconBlacklist = v;
        writeFile(BLACKLIST_BACKUP, originalIconBlacklist);
    }

    private static void normalizeBatterySettingsFromBackup() throws Exception {
        restoreBatterySettingsInternal();
        batteryHiddenForCoverHome = false;
    }

    private static synchronized void setCoverHomeBatteryHidden(boolean hide) throws Exception {
        if (hide == batteryHiddenForCoverHome) return;
        if (hide) {
            String current = runSettings("get", "secure", ICON_BLACKLIST_KEY);
            if (current == null || current.isEmpty() || "null".equalsIgnoreCase(current)) current = "";
            LinkedHashSet<String> slots = new LinkedHashSet<>();
            for (String part : current.split(",")) {
                String s = part.trim();
                if (!s.isEmpty()) slots.add(s);
            }
            slots.add(BATTERY_SLOT);
            StringBuilder joined = new StringBuilder();
            for (String slot : slots) {
                if (joined.length() != 0) joined.append(',');
                joined.append(slot);
            }
            runSettings("put", "secure", ICON_BLACKLIST_KEY, joined.toString());
            batteryHiddenForCoverHome = true;
        } else {
            restoreBatterySettingsInternal();
            batteryHiddenForCoverHome = false;
        }
    }

    private static synchronized void restoreBatterySettings() throws Exception {
        restoreBatterySettingsInternal();
        batteryHiddenForCoverHome = false;
    }

    private static void restoreBatterySettingsInternal() throws Exception {
        if ("__ABSENT__".equals(originalIconBlacklist)) runSettings("delete", "secure", ICON_BLACKLIST_KEY);
        else runSettings("put", "secure", ICON_BLACKLIST_KEY, originalIconBlacklist);
    }

    private static String runSettings(String... args) throws Exception {
        String[] cmd = new String[args.length + 1];
        cmd[0] = "/system/bin/settings";
        System.arraycopy(args, 0, cmd, 1, args.length);
        return runCommand(cmd).trim();
    }

    private static boolean isVisualProcessAlive() {
        try {
            String out = runCommand(new String[]{"/system/bin/pidof", VISUAL_PACKAGE}).trim();
            return !out.isEmpty();
        } catch (Throwable ignored) {
            return false;
        }
    }

    private static void startVisualApp() {
        try {
            runCommand(new String[]{"/system/bin/am", "start", "-n", VISUAL_COMPONENT});
            Log.i(TAG, "visual status bar started");
        } catch (Throwable t) {
            Log.w(TAG, "visual start failed", t);
        }
    }

    private static String runCommand(String[] cmd) throws Exception {
        Process p = new ProcessBuilder(cmd).redirectErrorStream(true).start();
        BufferedReader br = new BufferedReader(new InputStreamReader(p.getInputStream()));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            if (sb.length() != 0) sb.append('\n');
            sb.append(line);
        }
        int rc = p.waitFor();
        if (rc != 0) throw new IOException("rc=" + rc + " out=" + sb + " cmd=" + Arrays.toString(cmd));
        return sb.toString();
    }

    private static String readFile(File f) throws Exception {
        BufferedReader r = new BufferedReader(new FileReader(f));
        try {
            StringBuilder b = new StringBuilder();
            String s;
            while ((s = r.readLine()) != null) {
                if (b.length() != 0) b.append('\n');
                b.append(s);
            }
            return b.toString();
        } finally { r.close(); }
    }

    private static void writeFile(File f, String s) throws Exception {
        FileOutputStream os = new FileOutputStream(f, false);
        try { os.write(s.getBytes("UTF-8")); os.flush(); }
        finally { os.close(); }
    }

    private static void init() throws Exception {
        insetsClass = Class.forName("android.graphics.Insets");
        wctClass = Class.forName("android.window.WindowContainerTransaction");
        windowOrganizerClass = Class.forName("android.window.WindowOrganizer");
        binderClass = Class.forName("android.os.Binder");
        owner = binderClass.getDeclaredConstructor().newInstance();

        Class<?> type = Class.forName("android.view.WindowInsets$Type");
        Method statusBars = type.getDeclaredMethod("statusBars");
        statusBars.setAccessible(true);
        statusBarsType = ((Number) statusBars.invoke(null)).intValue();

        try {
            Class<?> src = Class.forName("android.view.InsetsSource");
            Field f = src.getDeclaredField("FLAG_FORCE_CONSUMING");
            f.setAccessible(true);
            forceFlags = f.getInt(null);
        } catch (Throwable t) {
            forceFlags = 4;
        }

        addInsetsMethod = findAddInsetsMethod();
        addInsetsMethod.setAccessible(true);
        removeInsetsMethod = findRemoveInsetsMethod();
        removeInsetsMethod.setAccessible(true);
    }

    private static void addInset(Object token, int taskId, int topInset) throws Exception {
        if (token == null) throw new IllegalStateException("null token taskId=" + taskId);
        Object wct = wctClass.getDeclaredConstructor().newInstance();
        addInsetsMethod.invoke(wct, token, owner, INDEX, statusBarsType,
                newInsets(0, topInset, 0, 0), null, forceFlags);
        applyWct(wct);
    }

    private static void removeInset(Object token, int taskId) throws Exception {
        if (token == null) throw new IllegalStateException("null token taskId=" + taskId);
        Object wct = wctClass.getDeclaredConstructor().newInstance();
        removeInsetsMethod.invoke(wct, token, owner, INDEX, statusBarsType);
        applyWct(wct);
    }

    private static void removeFromExistingTasks() throws Exception {
        if (appliedTaskIds.isEmpty() || removeInsetsMethod == null || owner == null) return;
        List<Object> infos = getRunningTasks();
        Object wct = wctClass.getDeclaredConstructor().newInstance();
        int count = 0;
        for (Object info : infos) {
            int taskId = getInt(info, "taskId", -1);
            if (!appliedTaskIds.contains(taskId)) continue;
            Object token = getFieldValue(info, "token");
            if (token == null) continue;
            removeInsetsMethod.invoke(wct, token, owner, INDEX, statusBarsType);
            count++;
        }
        if (count > 0) applyWct(wct);
        appliedTaskIds.clear();
    }

    private static Object newInsets(int l, int t, int r, int b) throws Exception {
        Method of = insetsClass.getDeclaredMethod("of", int.class, int.class, int.class, int.class);
        of.setAccessible(true);
        return of.invoke(null, l, t, r, b);
    }

    private static Method findAddInsetsMethod() throws Exception {
        for (Method m : wctClass.getDeclaredMethods()) {
            Class<?>[] p = m.getParameterTypes();
            if (!m.getName().equals("addInsetsSource")) continue;
            if (p.length == 7
                    && p[0].getName().equals("android.window.WindowContainerToken")
                    && p[1].getName().equals("android.os.IBinder")
                    && p[2] == int.class
                    && p[3] == int.class
                    && p[4].getName().equals("android.graphics.Insets")
                    && p[5].getName().equals("[Landroid.graphics.Rect;")
                    && p[6] == int.class) return m;
        }
        throw new NoSuchMethodException("Samsung addInsetsSource(Insets...)");
    }

    private static Method findRemoveInsetsMethod() throws Exception {
        for (Method m : wctClass.getDeclaredMethods()) {
            if (m.getName().equals("removeInsetsSource") && m.getParameterTypes().length == 4) return m;
        }
        throw new NoSuchMethodException("removeInsetsSource");
    }

    private static void applyWct(Object wct) throws Exception {
        Object organizer = windowOrganizerClass.getDeclaredConstructor().newInstance();
        Method apply = windowOrganizerClass.getDeclaredMethod("applyTransaction", wctClass);
        apply.setAccessible(true);
        apply.invoke(organizer, wct);
    }

    @SuppressWarnings("unchecked")
    private static List<Object> getRunningTasks() throws Exception {
        Class<?> atm = Class.forName("android.app.ActivityTaskManager");
        Method getService = atm.getDeclaredMethod("getService");
        getService.setAccessible(true);
        Object svc = getService.invoke(null);
        Class<?> iAtm = Class.forName("android.app.IActivityTaskManager");
        Method m = iAtm.getMethod("getTasks", int.class, boolean.class, boolean.class, int.class);
        return (List<Object>) m.invoke(svc, 64, false, false, DISPLAY_ID);
    }

    private static String getCoverFrontTopFromRootTasks() throws Exception {
        List<Object> infos = getRootTaskInfos();
        String fallback = null;
        for (Object info : infos) {
            int wm = invokeInt(info, "getWindowingMode", -1);
            if (!(wm == 0 || wm == 1)) continue;
            boolean focused = getBoolean(info, "isFocused", false);
            boolean visible = getBoolean(info, "visible", getBoolean(info, "isVisible", false));
            if (!(focused || visible)) continue;
            Object top = getFieldValue(info, "topActivity");
            if (top == null) continue;
            String s = String.valueOf(top);
            if (focused) return s;
            if (fallback == null) fallback = s;
        }
        return fallback;
    }

    @SuppressWarnings("unchecked")
    private static List<Object> getRootTaskInfos() throws Exception {
        Class<?> atm = Class.forName("android.app.ActivityTaskManager");
        Method getService = atm.getDeclaredMethod("getService");
        getService.setAccessible(true);
        Object svc = getService.invoke(null);
        Class<?> iAtm = Class.forName("android.app.IActivityTaskManager");
        Method m = iAtm.getMethod("getAllRootTaskInfosOnDisplay", int.class);
        return (List<Object>) m.invoke(svc, DISPLAY_ID);
    }

    private static Object getFieldValue(Object obj, String name) {
        if (obj == null) return null;
        Class<?> c = obj.getClass();
        while (c != null) {
            try {
                Field f = c.getDeclaredField(name);
                f.setAccessible(true);
                return f.get(obj);
            } catch (Throwable ignored) {
                c = c.getSuperclass();
            }
        }
        return null;
    }

    private static int getInt(Object obj, String name, int def) {
        Object v = getFieldValue(obj, name);
        return v instanceof Number ? ((Number) v).intValue() : def;
    }

    private static boolean getBoolean(Object obj, String name, boolean def) {
        Object v = getFieldValue(obj, name);
        return v instanceof Boolean ? (Boolean) v : def;
    }

    private static int invokeInt(Object obj, String method, int def) {
        try {
            Method m = obj.getClass().getMethod(method);
            Object v = m.invoke(obj);
            return v instanceof Number ? ((Number) v).intValue() : def;
        } catch (Throwable t) {
            return def;
        }
    }
}
