package net.lonelytransistor.respectlauncher;

import android.accessibilityservice.AccessibilityService;
import android.content.Intent;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.util.Log;
import android.view.accessibility.AccessibilityEvent;

import java.io.IOException;

public class ActivityMonitor extends AccessibilityService {
    private static final String TAG = "ActivityMonitor";

    private boolean adbPaired = false;

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        if (!adbPaired) {
            SharedPreferences sharedPreferences = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
            adbPaired = sharedPreferences.getBoolean("ADB_PAIRED", false);
            return;
        }
        if (event.getEventType() == AccessibilityEvent.TYPE_VIEW_FOCUSED |
            event.getEventType() == AccessibilityEvent.TYPE_WINDOWS_CHANGED) {
            String pkg = String.valueOf(event.getPackageName());
            MainServer.init(getApplicationContext(), (stdout, returnCode) -> {
                if (MainServer.mLaunchers.containsKey(pkg) &&
                        !MainServer.mLauncher.equals(pkg) &&
                        !"com.android.tv.settings".equals(pkg) &&
                        !"com.android.settings".equals(pkg)) {
                    MainServer.system("am force-stop " + pkg, (stdout1, returnCode1) -> {
                        startActivity(MainServer.mLaunchers.get(MainServer.mLauncher).launchIntent);
                    });
                    Log.i(TAG, "Force closed " + pkg + " and launched " + MainServer.mLauncher);
                }
            });
        }
    }
    @Override
    public void onInterrupt() {
    }
}
