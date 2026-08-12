from pathlib import Path
import hashlib

root = Path('merged_project')

def edit(rel, fn):
    p = root / rel
    s = p.read_text()
    ns = fn(s)
    if ns == s:
        raise SystemExit(f'no change applied: {rel}')
    p.write_text(ns)

edit('app/build.gradle.kts', lambda s: s.replace('versionCode = 11', 'versionCode = 12').replace(
    'versionName = "CR_BETA_3.1+Toolbar2.9"',
    'versionName = "CR_BETA_3.1+Toolbar2.9-isolated"'))

def restore_app(s):
    s = s.replace('import android.content.Intent;\n', '')
    s = s.replace('import android.provider.Settings;\n', '')
    block = '''\n        if (Settings.canDrawOverlays(this)) {\n            try { startService(new Intent(this, com.bbq20kbd.toolbar.test.ToolbarService.class)); }\n            catch (Exception ignored) { }\n        }\n'''
    if block not in s:
        raise SystemExit('CoverRecentsApp toolbar bootstrap block missing')
    return s.replace(block, '')
edit('app/src/main/java/apps/ijp/coverrecents/CoverRecentsApp.java', restore_app)

def restore_main(s):
    block = '''\n        Button toolbar = new Button(this);\n        toolbar.setText("하드웨어 키보드 툴바 설정");\n        toolbar.setOnClickListener(v ->\n                startActivity(new Intent(this, com.bbq20kbd.toolbar.test.MainActivity.class)));\n        root.addView(toolbar, matchWrap());\n'''
    if block not in s:
        raise SystemExit('MainActivity toolbar block missing')
    return s.replace(block, '')
edit('app/src/main/java/apps/ijp/coverrecents/MainActivity.java', restore_main)

mp = root / 'app/src/main/AndroidManifest.xml'
s = mp.read_text()
s = s.replace('android:name="com.bbq20kbd.toolbar.test.MainActivity"\n            android:label="@string/toolbar_title"\n            android:exported="false" />',
'''android:name="com.bbq20kbd.toolbar.test.MainActivity"\n            android:label="하드웨어 키보드 툴바"\n            android:process=":kbdtoolbar"\n            android:taskAffinity="apps.ijp.coverrecents.kbdtoolbar"\n            android:exported="true" />''')
s = s.replace('android:name="com.bbq20kbd.toolbar.test.ClipboardCaptureActivity"\n            android:exported="false"',
              'android:name="com.bbq20kbd.toolbar.test.ClipboardCaptureActivity"\n            android:process=":kbdtoolbar"\n            android:exported="false"')
s = s.replace('android:authorities="${applicationId}.clipboard"\n            android:exported="false"',
              'android:authorities="${applicationId}.kbdtoolbar.clipboard"\n            android:process=":kbdtoolbar"\n            android:exported="false"')
s = s.replace('android:name="com.bbq20kbd.toolbar.test.ToolbarService"\n            android:exported="false" />',
              'android:name="com.bbq20kbd.toolbar.test.ToolbarService"\n            android:process=":kbdtoolbar"\n            android:exported="false" />')
s = s.replace('android:name="com.bbq20kbd.toolbar.test.KeyboardAccessibilityService"\n            android:label="하드웨어 키보드 툴바"\n            android:permission=',
              'android:name="com.bbq20kbd.toolbar.test.KeyboardAccessibilityService"\n            android:label="하드웨어 키보드 툴바"\n            android:process=":kbdtoolbar"\n            android:permission=')
s = s.replace('@xml/keyboard_toolbar_config', '@xml/kbdtoolbar_accessibility_service_config')
s = s.replace('android:name=".service.RecentsEngine"\n            android:label="커버 화면 제스처"\n            android:permission=',
              'android:name=".service.RecentsEngine"\n            android:permission=')
required = [':kbdtoolbar', '${applicationId}.kbdtoolbar.clipboard', '@xml/kbdtoolbar_accessibility_service_config']
if not all(x in s for x in required):
    raise SystemExit('manifest isolation replacement incomplete')
mp.write_text(s)

sp = root / 'app/src/main/res/values/strings.xml'
s = sp.read_text()
s = s.replace('    <string name="toolbar_title">하드웨어 키보드 툴바</string>\n', '')
s = s.replace('name="toolbar_accessibility_description"', 'name="kbdtoolbar_accessibility_description"')
sp.write_text(s)
oldxml = root / 'app/src/main/res/xml/keyboard_toolbar_config.xml'
newxml = root / 'app/src/main/res/xml/kbdtoolbar_accessibility_service_config.xml'
xs = oldxml.read_text().replace('@string/toolbar_accessibility_description', '@string/kbdtoolbar_accessibility_description')
newxml.write_text(xs)
oldxml.unlink()

for rel in [
    'app/src/main/java/com/bbq20kbd/toolbar/test/ClipboardCaptureActivity.java',
    'app/src/main/java/com/bbq20kbd/toolbar/test/ClipboardImageProvider.java',
    'app/src/main/java/com/bbq20kbd/toolbar/test/KeyboardAccessibilityService.java',
    'app/src/main/java/com/bbq20kbd/toolbar/test/ToolbarService.java',
]:
    p = root / rel
    s = p.read_text()
    s = s.replace('getPackageName() + ".clipboard"', 'getPackageName() + ".kbdtoolbar.clipboard"')
    s = s.replace('new File(getFilesDir(), "clipboard_images")', 'new File(new File(getFilesDir(), "kbdtoolbar"), "clipboard_images")')
    s = s.replace('new File(getContext().getFilesDir(), "clipboard_images")', 'new File(new File(getContext().getFilesDir(), "kbdtoolbar"), "clipboard_images")')
    s = s.replace('getSharedPreferences("toolbar", MODE_PRIVATE)', 'getSharedPreferences("kbdtoolbar", MODE_PRIVATE)')
    p.write_text(s)

expected = {
'app/src/main/java/apps/ijp/coverrecents/CoverRecentsApp.java':'e916282c5ae354b9a9edc9e24bfd3018c03034b6a80f02d9cf8e81b42d6b5b3a',
'app/src/main/java/apps/ijp/coverrecents/MainActivity.java':'cae2bd0b0fd9584326e88685af5fbd8c50ae154c80bd6aafdeb78bf65d606841',
'app/src/main/java/apps/ijp/coverrecents/fold/FoldShortcutActivity.java':'a8134c67135094ba99a81f157b8a584e6a8fccb16ca23faf70301deef785f524',
'app/src/main/java/apps/ijp/coverrecents/fold/FoldStateController.java':'c85f5f2a9e43b80ae1805aedaf7bf050138550313e147bdf09fee94718d00e51',
'app/src/main/java/apps/ijp/coverrecents/service/ConfigActivity.java':'3d211230d45e93eead0744bae0ef9d3b256ca076fa520c68666be6cae71d6561',
'app/src/main/java/apps/ijp/coverrecents/service/Essentials.java':'919a826169eb62d7e3a07b48fa502178542164b1bd3331a7511c4eb9adc1b26f',
'app/src/main/java/apps/ijp/coverrecents/service/OriginalEventSelectors.java':'5234f92dd33ebbc24336fdb24c16ac1a3816e6ae80427986515a7b3602363df3',
'app/src/main/java/apps/ijp/coverrecents/service/OriginalValues.java':'5c274620bdf8a48e59c917e38b23131eb6bc1a17732dc311fcba6fdd88c2a47a',
'app/src/main/java/apps/ijp/coverrecents/service/RecentsEngine.java':'8581cf4beb2bd93044937bd24e83be9eb429eba1afe55496e300b64004876dea',
'app/src/main/java/apps/ijp/coverrecents/service/ToastActivity.java':'1cb5422871ae58213ec899f2b172f1d42aec468b91b2c784be022bf4d8fc4124',
'app/src/main/java/apps/ijp/coverrecents/service/UnlockActivity.java':'7f7bfa56d131f95c0a4687bce1574f48c12cd1eba874dec49a8ba6e71c98a124',
}
for rel, want in expected.items():
    got = hashlib.sha256((root/rel).read_bytes()).hexdigest()
    if got != want:
        raise SystemExit(f'CoverRecents changed: {rel}: {got} != {want}')

for p in (root/'app/src/main/java/com/bbq20kbd/toolbar/test').glob('*.java'):
    t=p.read_text()
    if 'getSharedPreferences("toolbar"' in t or 'getPackageName() + ".clipboard"' in t:
        raise SystemExit(f'unisolated toolbar data path: {p}')
print('Isolation verification OK')
