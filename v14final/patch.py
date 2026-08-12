from pathlib import Path
import re

root = Path('android-toolbar-v14')

b = root / 'app/build.gradle.kts'
s = b.read_text()
s = re.sub(r'versionCode\s*=\s*\d+', 'versionCode = 16', s, count=1)
s = re.sub(r'versionName\s*=\s*"[^"]+"', 'versionName = "2.3"', s, count=1)
b.write_text(s)

x = root / 'app/src/main/res/xml/accessibility_service_config.xml'
s = x.read_text()
if 'android:canTakeScreenshot=' not in s:
    s = s.replace('    android:canRetrieveWindowContent="true"\n',
                  '    android:canRetrieveWindowContent="true"\n    android:canTakeScreenshot="true"\n')
x.write_text(s)

p = root / 'app/src/main/java/com/bbq20kbd/toolbar/test/KeyboardAccessibilityService.java'
s = p.read_text()
s = s.replace('import android.graphics.Rect;\n', 'import android.graphics.Bitmap;\nimport android.graphics.Rect;\nimport android.hardware.HardwareBuffer;\n')
s = s.replace('import java.util.ArrayDeque;\n', 'import java.io.File;\nimport java.io.FileOutputStream;\nimport java.util.ArrayDeque;\n')
marker = '    public String getSelectedText() {\n'
method = '''    public interface ScreenshotCallback {\n        void onSuccess(String path, String mime);\n        void onFailure(int errorCode);\n    }\n\n    public void captureScreenshot(int displayId, ScreenshotCallback callback) {\n        try {\n            takeScreenshot(displayId, getMainExecutor(), new TakeScreenshotCallback() {\n                @Override public void onSuccess(ScreenshotResult screenshot) {\n                    HardwareBuffer buffer = screenshot.getHardwareBuffer();\n                    Bitmap wrapped = null, software = null;\n                    try {\n                        wrapped = Bitmap.wrapHardwareBuffer(buffer, screenshot.getColorSpace());\n                        if (wrapped == null) throw new IllegalStateException("wrap screenshot failed");\n                        software = wrapped.copy(Bitmap.Config.ARGB_8888, false);\n                        if (software == null) throw new IllegalStateException("copy screenshot failed");\n                        File dir = new File(getFilesDir(), "clipboard_images");\n                        if (!dir.exists() && !dir.mkdirs()) throw new IllegalStateException("mkdir failed");\n                        File file = new File(dir, "screenshot_" + System.currentTimeMillis() + ".png");\n                        try (FileOutputStream out = new FileOutputStream(file)) {\n                            if (!software.compress(Bitmap.CompressFormat.PNG, 100, out)) throw new IllegalStateException("png failed");\n                        }\n                        if (callback != null) callback.onSuccess(file.getAbsolutePath(), "image/png");\n                    } catch (Exception e) {\n                        Log.w("HWKeyboardInput", "Screenshot save failed", e);\n                        if (callback != null) callback.onFailure(ERROR_TAKE_SCREENSHOT_INTERNAL_ERROR);\n                    } finally {\n                        if (software != null) software.recycle();\n                        if (wrapped != null) wrapped.recycle();\n                        try { buffer.close(); } catch (Exception ignored) { }\n                    }\n                }\n                @Override public void onFailure(int errorCode) {\n                    if (callback != null) callback.onFailure(errorCode);\n                }\n            });\n        } catch (Exception e) {\n            Log.w("HWKeyboardInput", "takeScreenshot failed", e);\n            if (callback != null) callback.onFailure(ERROR_TAKE_SCREENSHOT_INTERNAL_ERROR);\n        }\n    }\n\n'''
if marker not in s: raise SystemExit('accessibility marker missing')
s = s.replace(marker, method + marker, 1)
p.write_text(s)

p = root / 'app/src/main/java/com/bbq20kbd/toolbar/test/ToolbarService.java'
s = p.read_text()
old = '        addPanelTitle(content, "클립보드");\n\n        List<ClipboardEntry> history = loadHistory();\n'
new = '        addPanelTitle(content, "클립보드");\n\n        TextView screenshot = panelAction("스크린샷 추가", v -> captureScreenshotForClipboard());\n        screenshot.setTextSize(12f);\n        content.addView(screenshot, new LinearLayout.LayoutParams(-1, dp(30)));\n\n        List<ClipboardEntry> history = loadHistory();\n'
if old not in s: raise SystemExit('clipboard header marker missing')
s = s.replace(old, new, 1)
s = s.replace('            showPanel(content, dp(84));\n', '            showPanel(content, dp(114));\n', 1)
s = s.replace('        grid.setColumnCount(3);\n', '        grid.setColumnCount(4);\n', 1)
old = '''        scroll.addView(grid);\n        int rowCount = (history.size() + 2) / 3;\n        int visibleRows = Math.min(2, Math.max(1, rowCount));\n        int scrollHeight = dp(72 * visibleRows);\n        content.addView(scroll, new LinearLayout.LayoutParams(-1, scrollHeight));\n        showPanel(content, dp(32) + scrollHeight);\n    }\n\n    private Bitmap decodeThumbnail'''
new = '''        int remainder = history.size() % 4;\n        if (remainder != 0) {\n            for (int i = remainder; i < 4; i++) {\n                View spacer = new View(uiContext());\n                GridLayout.LayoutParams lp = new GridLayout.LayoutParams();\n                lp.width = 0; lp.height = dp(68);\n                lp.columnSpec = GridLayout.spec(GridLayout.UNDEFINED, 1f);\n                lp.setMargins(dp(2), dp(2), dp(2), dp(2));\n                grid.addView(spacer, lp);\n            }\n        }\n        scroll.addView(grid);\n        int rowCount = (history.size() + 3) / 4;\n        int visibleRows = Math.min(2, Math.max(1, rowCount));\n        int scrollHeight = dp(72 * visibleRows);\n        content.addView(scroll, new LinearLayout.LayoutParams(-1, scrollHeight));\n        showPanel(content, dp(62) + scrollHeight);\n    }\n\n    private void captureScreenshotForClipboard() {\n        KeyboardAccessibilityService a = KeyboardAccessibilityService.get();\n        if (a == null) {\n            Toast.makeText(uiContext(), "접근성 서비스를 켜야 합니다.", Toast.LENGTH_SHORT).show();\n            return;\n        }\n        final int displayId = overlayDisplayId;\n        removePanelViewOnly();\n        hideToolbar();\n        mainHandler.postDelayed(() -> a.captureScreenshot(displayId, new KeyboardAccessibilityService.ScreenshotCallback() {\n            @Override public void onSuccess(String path, String mime) {\n                mainHandler.post(() -> {\n                    rememberImage(path, mime);\n                    updateToolbar();\n                    if ("clipboard".equals(activePanelKey)) renderClipboardPanel();\n                    Toast.makeText(uiContext(), "스크린샷을 클립보드에 추가했습니다.", Toast.LENGTH_SHORT).show();\n                });\n            }\n            @Override public void onFailure(int errorCode) {\n                mainHandler.post(() -> {\n                    updateToolbar();\n                    if ("clipboard".equals(activePanelKey)) renderClipboardPanel();\n                    Toast.makeText(uiContext(), "스크린샷 캡처 실패: " + errorCode, Toast.LENGTH_SHORT).show();\n                });\n            }\n        }), 140);\n    }\n\n    private Bitmap decodeThumbnail'''
if old not in s: raise SystemExit('clipboard layout marker missing')
s = s.replace(old, new, 1)
p.write_text(s)
